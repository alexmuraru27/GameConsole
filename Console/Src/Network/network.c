#include "network.h"

#include <stdio.h>
#include <string.h>

#include "usart.h"
#include "gpio.h"
#include "sysclock.h"
#include "logger.h"

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

/* TX scratch: full frame. RX scratch: [type,len_lo,len_hi, payload...] for CRC. */
static uint8_t s_tx[NP_MAX_FRAME];
static uint8_t s_rx[3U + NP_MAX_PAYLOAD];

/* True while an HTTP GET session is open (between open and close). */
static bool s_http_open = false;

/* ---- Framing ------------------------------------------------------- */

/* Discard bytes the ESP left on the line (post-reset ROM/boot chatter, a late
 * reply after a timeout) so a transaction starts from a clean, idle line. The
 * STM32 RX is polled with only the 1-byte DR + shift register behind it, so any
 * unread leftover overruns and desyncs framing. Reads until the line is quiet
 * for `idle_ms`, or `max_ms` elapses. */
static void npDrainRx(uint32_t idle_ms, uint32_t max_ms)
{
    const uint32_t hard_deadline = getSysTime() + max_ms;
    while (getSysTime() < hard_deadline)
    {
        if (usartReadByte(idle_ms) < 0)
        {
            break; /* no byte for idle_ms — line is drained */
        }
    }
    usartFlushRx();
}

static void npBegin(void)
{
    /* Re-assert the runtime baud — the flasher may have left USART1 at 115200. */
    usartSetBaud(NETWORK_UART_BAUD);
    /* Clear any stale/late bytes so this command's response frames cleanly. */
    npDrainRx(3U, 40U);
}

static bool npSendFrame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    if (len > NP_MAX_PAYLOAD)
    {
        return false;
    }

    s_tx[0] = NP_SYNC0;
    s_tx[1] = NP_SYNC1;
    s_tx[2] = type;
    s_tx[3] = (uint8_t)(len & 0xFFU);
    s_tx[4] = (uint8_t)(len >> 8U);
    if (len > 0U && payload != NULL)
    {
        memcpy(&s_tx[NP_HEADER_SIZE], payload, len);
    }
    const uint16_t crc = np_crc16(&s_tx[2], (uint32_t)(1U + 2U + len)); /* type..payload */
    s_tx[NP_HEADER_SIZE + len] = (uint8_t)(crc & 0xFFU);
    s_tx[NP_HEADER_SIZE + len + 1U] = (uint8_t)(crc >> 8U);

    return usartWriteBytes(s_tx, (uint16_t)(NP_FRAME_OVERHEAD + len), NP_TX_TIMEOUT);
}

/* Read `n` bytes into `dst` before `deadline`. */
static bool npReadExact(uint8_t *dst, uint16_t n, uint32_t deadline)
{
    for (uint16_t i = 0U; i < n; i++)
    {
        const uint32_t now = getSysTime();
        if (now >= deadline)
        {
            return false;
        }
        const int b = usartReadByte(deadline - now);
        if (b < 0)
        {
            return false;
        }
        dst[i] = (uint8_t)b;
    }
    return true;
}

/* Buffered diagnostics from the ESP (NP_RSP_LOG). They are collected during a
 * receive and flushed to SWO only after the response frame is in, so the slow
 * SWO printf never stalls the polled receiver between back-to-back ESP frames. */
#define ESP_LOG_MAX 16U
#define ESP_LOG_MSG_MAX 96U
static uint8_t s_esp_log_level[ESP_LOG_MAX];
static char s_esp_log_msg[ESP_LOG_MAX][ESP_LOG_MSG_MAX];
static uint8_t s_esp_log_count;

static void espForwardLog(uint8_t level, const char *msg)
{
    switch (level)
    {
    case NP_LOG_ERROR:
        LOGGER_LOG_ERROR(LOGGER_ESP01, "%s", msg);
        break;
    case NP_LOG_WARN:
        LOGGER_LOG_WARN(LOGGER_ESP01, "%s", msg);
        break;
    case NP_LOG_DEBUG:
        LOGGER_LOG_DEBUG(LOGGER_ESP01, "%s", msg);
        break;
    default:
        LOGGER_LOG_INFO(LOGGER_ESP01, "%s", msg);
        break;
    }
}

/* Push buffered ESP logs to SWO. Done once the ESP has stopped sending (on a
 * good response OR a failed/timed-out transaction) so partial diagnostics from a
 * failure aren't lost. */
static void flushEspLogs(void)
{
    for (uint8_t i = 0U; i < s_esp_log_count; i++)
    {
        espForwardLog(s_esp_log_level[i], s_esp_log_msg[i]);
    }
    s_esp_log_count = 0U;
}

/* Read one frame off the wire into s_rx (payload at &s_rx[3]), validating sync +
 * CRC. Returns the frame type, or -1 on timeout / framing / CRC error. */
static int npReadRawFrame(uint16_t *out_len, uint32_t deadline)
{
    /* Hunt for the sync word, tolerating leading noise. */
    uint8_t sync_state = 0U;
    while (sync_state < 2U)
    {
        const uint32_t now = getSysTime();
        if (now >= deadline)
        {
            return -1;
        }
        const int b = usartReadByte(deadline - now);
        if (b < 0)
        {
            return -1;
        }
        if (sync_state == 0U)
        {
            sync_state = ((uint8_t)b == NP_SYNC0) ? 1U : 0U;
        }
        else /* saw SYNC0 */
        {
            sync_state = ((uint8_t)b == NP_SYNC1) ? 2U : (((uint8_t)b == NP_SYNC0) ? 1U : 0U);
        }
    }

    /* type + len (contiguous at s_rx[0..2] for the CRC span). */
    if (!npReadExact(&s_rx[0], 3U, deadline))
    {
        return -1;
    }
    const uint16_t len = (uint16_t)((uint16_t)s_rx[1] | ((uint16_t)s_rx[2] << 8U));
    if (len > NP_MAX_PAYLOAD || !npReadExact(&s_rx[3], len, deadline))
    {
        return -1;
    }

    uint8_t crc_bytes[2];
    if (!npReadExact(crc_bytes, 2U, deadline))
    {
        return -1;
    }
    const uint16_t crc_rx = (uint16_t)((uint16_t)crc_bytes[0] | ((uint16_t)crc_bytes[1] << 8U));
    if (crc_rx != np_crc16(&s_rx[0], (uint32_t)(3U + len)))
    {
        LOGGER_LOG_WARN(LOGGER_NETWORK, "frame crc mismatch");
        return -1;
    }

    *out_len = len;
    return s_rx[0];
}

/* Receive the response to a command. NP_RSP_LOG frames that precede it are
 * buffered and flushed to SWO once the response is fully read. */
static bool npRecvFrame(uint8_t *out_type, uint16_t *out_len, uint32_t timeout_ms)
{
    const uint32_t deadline = getSysTime() + timeout_ms;
    s_esp_log_count = 0U;

    for (;;)
    {
        uint16_t len = 0U;
        const int type = npReadRawFrame(&len, deadline);
        if (type < 0)
        {
            flushEspLogs(); /* surface any diagnostics that arrived before the failure */
            return false;
        }

        if (type == NP_RSP_LOG)
        {
            /* Buffer the message (level byte + text); flush later. */
            if (s_esp_log_count < ESP_LOG_MAX && len >= 1U)
            {
                uint16_t mlen = (uint16_t)(len - 1U);
                if (mlen > ESP_LOG_MSG_MAX - 1U)
                {
                    mlen = ESP_LOG_MSG_MAX - 1U;
                }
                s_esp_log_level[s_esp_log_count] = s_rx[3];
                memcpy(s_esp_log_msg[s_esp_log_count], &s_rx[4], mlen);
                s_esp_log_msg[s_esp_log_count][mlen] = '\0';
                s_esp_log_count++;
            }
            continue;
        }

        /* Response in hand — the ESP is done sending, so flush logs to SWO now. */
        flushEspLogs();
        *out_type = (uint8_t)type;
        *out_len = len;
        return true;
    }
}

/* Send a command and receive its response. Returns false on any failure. */
static bool npTransact(uint8_t cmd, const uint8_t *payload, uint16_t len,
                       uint8_t *rsp_type, uint16_t *rsp_len, uint32_t timeout_ms)
{
    if (!npSendFrame(cmd, payload, len))
    {
        return false;
    }
    return npRecvFrame(rsp_type, rsp_len, timeout_ms);
}

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
            *out_ver = (len >= 1U) ? s_rx[3] : 0U;
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
    const uint8_t *p = &s_rx[3];
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
            type == NP_RSP_STATUS && len >= 1U && s_rx[3] == NP_STATE_CONNECTED)
        {
            LOGGER_LOG_INFO(LOGGER_NETWORK, "connect '%s' -> ok (attempt %u/%u)",
                            ssid, (unsigned)attempt, (unsigned)NETWORK_CONNECT_ATTEMPTS);
            return true;
        }
        const unsigned wl_status = (len >= 6U) ? (unsigned)s_rx[3 + 5U] : 0xFFU;
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
    return (s_rx[3] == NP_STATE_CONNECTED);
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

    const uint8_t *p = &s_rx[3];
    const uint16_t status = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
    const uint32_t length = (uint32_t)p[2] | ((uint32_t)p[3] << 8U) | ((uint32_t)p[4] << 16U) | ((uint32_t)p[5] << 24U);
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

    uint8_t req[2] = {(uint8_t)(max & 0xFFU), (uint8_t)(max >> 8U)};
    uint8_t type;
    uint16_t len;
    if (!npTransact(NP_CMD_HTTP_READ, req, 2U, &type, &len, NP_TIMEOUT_HTTP_READ) || type != NP_RSP_HTTP_DATA)
    {
        return -1;
    }
    if (len > 0U)
    {
        memcpy(buf, &s_rx[3], len);
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
