#include "Network/network.h"
#include <stm32f407xx.h>
#include "Network/network_internal.h"

#include <stdio.h>
#include <string.h>

#include "Peripherals/usart.h"
#include "Devices/esp01.h"
#include "Peripherals/sysclock.h"
#include "Peripherals/systime.h"
#include "Logger/logger.h"

/*
 * Frame master for the ESP-01S link. Layout (network_protocol.h):
 *   0xA5 0x5A | type | len:u16 LE | payload | crc16:u16 LE
 *
 * Each public call is a single transaction: build + send a command frame, then
 * read exactly one response frame. The RX path reads type+len+payload into one
 * contiguous buffer so the CRC (which covers type..payload) is a single span.
 */

/* Per-command response timeouts (ms). WiFi scan/associate are slow on the ESP. */
#define NP_TIMEOUT_DEFAULT 2000U
#define NP_TIMEOUT_SCAN 9000U
#define NP_TIMEOUT_CONNECT 16000U /* must exceed the ESP's connect-wait (6 s) + boot/log slack so its result is received */
#define NETWORK_CONNECT_ATTEMPTS 4U /* WiFi association retries before giving up (transient assoc failures are common) */
#define NP_TIMEOUT_HTTP_OPEN 12000U
#define NP_TIMEOUT_HTTP_READ 4000U
#define NP_TX_TIMEOUT 1000U

/* True while an HTTP GET session is open (between open and close). */
static bool s_http_open = false;

/* The framing/transaction core (npBegin/npSendFrame/npTransact/npRxPayload/…) and
 * the transaction seam (networkTransact/Send/Collect) live in network_frame.c,
 * declared in network_internal.h. This file is the WiFi/HTTP command layer over it. */

/* ---- Public API ---------------------------------------------------- */

void networkRebootEsp(void)
{
    /* Select the runtime baud (the flasher may have left USART1 at its own rate). */
    usartSetBaud(NETWORK_UART_BAUD);
    s_http_open = false;

    /* Set the boot-from-flash straps BEFORE the power-cycle: the ESP samples them
     * when EN rises, and they must read GPIO0=high (run firmware, NOT the ROM
     * bootloader), GPIO2=high (module pull-up), RST released. Without this an EN
     * cycle could boot the ROM loader and the firmware would never run (silent). */
    esp01SetBootloader(false); /* IO0 -> input/high (firmware, not bootloader) */
    esp01SetReset(false);      /* RST high (not held in reset)                 */

    /* Power-cycle via EN/CH_PD so it boots fresh — clears stale WiFi/HTTP state
     * and, after a firmware flash, starts the new image cleanly. */
    esp01SetEnable(false);
    delay(20U);
    esp01SetEnable(true);

    /* The ESP8266 emits ROM boot text (at 74880 baud, so garbage to us) plus the
     * firmware's boot banner right after EN rises. Drain it to an idle line before
     * syncing, otherwise those leftover bytes overrun the polled RX and desync the
     * first frames. Bounded so a dead/absent ESP doesn't stall the boot. */
    npDrainRx(100U, 700U);
    LOGGER_LOG_INFO(LOGGER_NETWORK, "ESP rebooted (EN), link @ %u baud", (unsigned)NETWORK_UART_BAUD);
}

void networkInit(void)
{
    /* USART1 itself is configured by usartInit() in peripheralsInit(); bring the
     * link to the runtime baud, start the ESP fresh, and confirm the handshake
     * (the boot log then states plainly whether the ESP link is healthy). */
    networkRebootEsp();
    networkSync();
}

bool networkPing(void)
{
    npBegin();
    uint8_t type;
    uint16_t len;
    return npTransact(NP_CMD_PING, NULL, 0U, &type, &len, NP_TIMEOUT_DEFAULT) && type == NP_RSP_PONG;
}

/* Number/length of ping attempts during a sync (covers the ESP's post-reset boot). */
#define NP_SYNC_TRIES 6
#define NP_SYNC_TRY_MS 200U

/* Ping at `baud`, `tries` times. On a PONG, returns true and stores the reported
 * protocol version. Flushes the RX ring first since the baud just changed. */
static bool npPingAtBaud(uint32_t baud, int tries, uint8_t *out_ver)
{
    usartSetBaud(baud);
    usartFlushRx();
    for (int i = 0; i < tries; i++)
    {
        uint8_t type;
        uint16_t len;
        if (npTransact(NP_CMD_PING, NULL, 0U, &type, &len, NP_SYNC_TRY_MS) && type == NP_RSP_PONG)
        {
            *out_ver = (len >= 1U) ? npRxPayload()[0] : 0U;
            return true;
        }
        delay(50U);
    }
    return false;
}

/* Last-resort sync diagnostic: send one PING at the runtime baud and count ANY
 * bytes that come back (valid frame or not) over a short window. This splits the
 * two failure modes a plain "no response" can't:
 *   0 bytes  -> the ESP is silent: no power, EN/CH_PD low, held in reset, or it
 *               isn't running firmware (e.g. crash-looping on a boot brownout).
 *   >0 bytes -> the ESP IS transmitting but the frame never validated: a baud
 *               mismatch, RX/TX miswire, or line corruption. */
static void npDiagnoseSilence(void)
{
    usartSetBaud(NETWORK_UART_BAUD);
    usartFlushRx();
    (void)npSendFrame(NP_CMD_PING, NULL, 0U);

    uint8_t sample[24];
    uint32_t seen = 0U;
    const uint32_t until = getSysTime() + 300U;
    while (getSysTime() < until)
    {
        const int b = usartReadByte(until - getSysTime());
        if (b >= 0)
        {
            if (seen < sizeof(sample))
            {
                sample[seen] = (uint8_t)b;
            }
            seen++;
        }
    }

    if (seen == 0U)
    {
        LOGGER_LOG_WARN(LOGGER_NETWORK, "diag: 0 bytes after PING -> ESP silent (power/EN/reset, or not running firmware)");
    }
    else
    {
        LOGGER_LOG_WARN(LOGGER_NETWORK, "diag: %lu byte(s) after PING -> ESP alive but link garbled (baud/wiring/crc)",
                        (unsigned long)seen);
        /* Dump the head so the garble can be identified: readable ASCII => the
         * ESP8266 postmortem text (a crash loop); scrambled bytes => ROM boot
         * messages at 74880 baud (a reset loop) or a baud mismatch. */
        char hex[3U * sizeof(sample) + 1U];
        uint32_t pos = 0U;
        const uint32_t dump = (seen < sizeof(sample)) ? seen : (uint32_t)sizeof(sample);
        for (uint32_t i = 0U; i < dump; i++)
        {
            pos += (uint32_t)snprintf(&hex[pos], sizeof(hex) - pos, "%02X ", sample[i]);
        }
        LOGGER_LOG_WARN(LOGGER_NETWORK, "diag: head: %s", hex);
    }
}

bool networkSync(void)
{
    uint8_t ver = 0U;

    /* Primary: the configured runtime baud, with enough retries for the ESP's
     * post-reset boot. */
    if (npPingAtBaud(NETWORK_UART_BAUD, NP_SYNC_TRIES, &ver))
    {
        if (ver == NETWORK_PROTOCOL_VERSION)
        {
            LOGGER_LOG_INFO(LOGGER_NETWORK, "ESP synced (proto v%u) @ %u baud", (unsigned)ver, (unsigned)NETWORK_UART_BAUD);
            return true;
        }
        LOGGER_LOG_WARN(LOGGER_NETWORK, "ESP proto v%u != console v%u — reflash the ESP",
                        (unsigned)ver, (unsigned)NETWORK_PROTOCOL_VERSION);
        usartSetBaud(NETWORK_UART_BAUD);
        return false;
    }

    /* No answer at the runtime baud. Probe a couple of common alternates so a
     * stale/wrong-baud ESP firmware is diagnosed precisely instead of just
     * "no response" (the 115200 and 921600 images are the same size, so the
     * flasher's byte count can't catch a stale flash). */
    static const uint32_t alt_bauds[] = {921600u, 230400u, 115200u};
    for (unsigned i = 0U; i < sizeof(alt_bauds) / sizeof(alt_bauds[0]); i++)
    {
        if (alt_bauds[i] == NETWORK_UART_BAUD)
        {
            continue;
        }
        if (npPingAtBaud(alt_bauds[i], 2, &ver))
        {
            LOGGER_LOG_WARN(LOGGER_NETWORK, "ESP answered at %u baud, console is %u — reflash ESP with the current build",
                            (unsigned)alt_bauds[i], (unsigned)NETWORK_UART_BAUD);
            usartSetBaud(NETWORK_UART_BAUD);
            return false;
        }
    }

    LOGGER_LOG_WARN(LOGGER_NETWORK, "ESP sync failed: no response at any baud (wiring / power / firmware)");
    npDiagnoseSilence(); /* tell silent-ESP apart from alive-but-garbled */
    usartSetBaud(NETWORK_UART_BAUD);
    return false;
}

int networkScan(NetworkAp *out, int max)
{
    if (out == NULL || max <= 0)
    {
        return -1;
    }
    npBegin();

    uint8_t type;
    uint16_t len;
    if (!npTransact(NP_CMD_SCAN, NULL, 0U, &type, &len, NP_TIMEOUT_SCAN) || type != NP_RSP_SCAN || len < 1U)
    {
        LOGGER_LOG_WARN(LOGGER_NETWORK, "scan failed");
        return -1;
    }

    /* payload: count, then count x { rssi:i8, enc:u8, ssid_len:u8, ssid[ssid_len] } */
    const uint8_t *p = npRxPayload();
    uint16_t off = 0U;
    const uint8_t count = p[off++];
    int found = 0;
    for (uint8_t i = 0U; i < count && found < max; i++)
    {
        if (off + 3U > len)
        {
            break;
        }
        const int8_t rssi = (int8_t)p[off++];
        const uint8_t enc = p[off++];
        const uint8_t ssid_len = p[off++];
        if (off + ssid_len > len || ssid_len > NP_SSID_MAX)
        {
            break;
        }
        memcpy(out[found].ssid, &p[off], ssid_len);
        out[found].ssid[ssid_len] = '\0';
        out[found].rssi = rssi;
        out[found].enc = enc;
        off += ssid_len;
        found++;
    }
    LOGGER_LOG_INFO(LOGGER_NETWORK, "scan: %d AP(s)", found);
    return found;
}

bool networkConnect(const char *ssid, const char *pass)
{
    if (ssid == NULL)
    {
        return false;
    }

    const uint8_t ssid_len = (uint8_t)strnlen(ssid, NP_SSID_MAX);
    const uint8_t pass_len = (pass != NULL) ? (uint8_t)strnlen(pass, NP_PASS_MAX) : 0U;

    uint8_t payload[2U + NP_SSID_MAX + NP_PASS_MAX];
    uint16_t n = 0U;
    payload[n++] = ssid_len;
    memcpy(&payload[n], ssid, ssid_len);
    n += ssid_len;
    payload[n++] = pass_len;
    if (pass_len > 0U)
    {
        memcpy(&payload[n], pass, pass_len);
        n += pass_len;
    }

    /* Association can fail transiently (busy AP, weak signal, timing), so retry a
     * few times before giving up. Each attempt is one CONNECT command (the ESP
     * waits ~6 s for the result), so the total stays bounded. */
    for (uint8_t attempt = 1U; attempt <= NETWORK_CONNECT_ATTEMPTS; attempt++)
    {
        npBegin();
        uint8_t type = 0U;
        uint16_t len = 0U;
        if (npTransact(NP_CMD_CONNECT, payload, n, &type, &len, NP_TIMEOUT_CONNECT) &&
            type == NP_RSP_STATUS && len >= 1U && npRxPayload()[0] == NP_STATE_CONNECTED)
        {
            LOGGER_LOG_INFO(LOGGER_NETWORK, "connect '%s' -> ok (attempt %u/%u)",
                            ssid, (unsigned)attempt, (unsigned)NETWORK_CONNECT_ATTEMPTS);
            return true;
        }
        const unsigned wl_status = (len >= 6U) ? (unsigned)npRxPayload()[5U] : 0xFFU;
        LOGGER_LOG_WARN(LOGGER_NETWORK, "connect '%s' attempt %u/%u failed (wl_status %u)",
                        ssid, (unsigned)attempt, (unsigned)NETWORK_CONNECT_ATTEMPTS, wl_status);
        if (attempt < NETWORK_CONNECT_ATTEMPTS)
        {
            delay(500U); /* brief settle before re-associating */
        }
    }
    return false;
}

void networkDisconnect(void)
{
    npBegin();
    uint8_t type;
    uint16_t len;
    (void)npTransact(NP_CMD_DISCONNECT, NULL, 0U, &type, &len, NP_TIMEOUT_DEFAULT);
}

bool networkIsConnected(void)
{
    npBegin();
    uint8_t type;
    uint16_t len;
    if (!npTransact(NP_CMD_STATUS, NULL, 0U, &type, &len, NP_TIMEOUT_DEFAULT) || type != NP_RSP_STATUS || len < 1U)
    {
        return false;
    }
    return (npRxPayload()[0] == NP_STATE_CONNECTED);
}

bool networkHttpOpen(const char *url, uint32_t *content_length, uint16_t *http_status)
{
    if (url == NULL)
    {
        return false;
    }
    npBegin();

    const uint16_t url_len = (uint16_t)strnlen(url, NP_URL_MAX);
    uint8_t type;
    uint16_t len;
    if (!npTransact(NP_CMD_HTTP_OPEN, (const uint8_t *)url, url_len, &type, &len, NP_TIMEOUT_HTTP_OPEN))
    {
        LOGGER_LOG_WARN(LOGGER_NETWORK, "http open failed");
        return false;
    }
    if (type != NP_RSP_HTTP_OPEN || len < 6U)
    {
        return false;
    }

    const uint8_t *p = npRxPayload();
    const uint16_t status = np_rd16(&p[0]);
    const uint32_t length = np_rd32(&p[2]);
    if (http_status != NULL)
    {
        *http_status = status;
    }
    if (content_length != NULL)
    {
        *content_length = length;
    }
    s_http_open = true;
    LOGGER_LOG_INFO(LOGGER_NETWORK, "http %u, %lu bytes", (unsigned)status, (unsigned long)length);
    return (status == 200U);
}

int networkHttpRead(uint8_t *buf, uint16_t max)
{
    if (buf == NULL || max == 0U || !s_http_open)
    {
        return -1;
    }
    if (max > NP_MAX_PAYLOAD)
    {
        max = NP_MAX_PAYLOAD;
    }

    uint8_t req[2];
    np_wr16(req, max);
    uint8_t type;
    uint16_t len;
    if (!npTransact(NP_CMD_HTTP_READ, req, 2U, &type, &len, NP_TIMEOUT_HTTP_READ) || type != NP_RSP_HTTP_DATA)
    {
        return -1;
    }
    if (len > 0U)
    {
        memcpy(buf, npRxPayload(), len);
    }
    return (int)len; /* 0 = EOF */
}

void networkHttpClose(void)
{
    if (!s_http_open)
    {
        return;
    }
    npBegin();
    uint8_t type;
    uint16_t len;
    (void)npTransact(NP_CMD_HTTP_CLOSE, NULL, 0U, &type, &len, NP_TIMEOUT_DEFAULT);
    s_http_open = false;
}
