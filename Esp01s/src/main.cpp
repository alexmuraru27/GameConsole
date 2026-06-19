/*
 * ESP-01S firmware — GameConsole WiFi module.
 *
 * Acts as the slave of the console<->ESP frame protocol (network_protocol.h):
 * the STM32 sends commands, this firmware scans/joins WiFi and performs HTTP
 * GETs, relaying the body back in chunks. That lets the console pull games and
 * its own ESP firmware from the PC update server.
 *
 * The console flashes this image via Settings -> "Upgrade WiFi module" (it reads
 * ESP01.bin off the SD card; see ../docu/flasher.md).
 */
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

extern "C"
{
#include "user_interface.h" /* wifi_set_country() — widen scan to channels 1-13 */
}

#include "network_protocol.h"
#include "protocol.h"

/* On-board LED (GPIO2, active-low) used as a WiFi-status light. */
static const uint8_t LED_PIN = 2;

/*
 * WiFi TX power cap (dBm, valid 0..20.5). The ESP-01S's tiny 3V3 supply sags
 * under full-power TX bursts (~300 mA peaks). The worst spike is the WPA2 4-way
 * handshake during associate: a brownout there corrupts instruction execution
 * and the chip dies with an IllegalInstruction inside the SDK RF code
 * (ieee80211_parse_rsn). Capping TX power cuts the peak current. 8.5 dBm is an
 * aggressive low setting to prove out the supply on a nearby AP — raise it for
 * range once a bulk cap on VCC makes the rail solid. NOTE: the real fix for a
 * marginal supply is hardware (a 470-1000 uF cap across the ESP VCC<->GND); this
 * only trims the peak the regulator has to deliver.
 */
static const float ESP_TX_POWER_DBM = 8.5f;

static void applyTxPower(void)
{
    /* Call once, after WiFi.mode() and BEFORE WiFi.begin(). Re-applying it while
     * an association is in flight mutates the PHY power tables under the SDK's RF
     * task and can crash it, so it is deliberately not called mid-connect. */
    WiFi.setOutputPower(ESP_TX_POWER_DBM);
}

/* Scratch for building response payloads (e.g. an HTTP_DATA chunk). */
static uint8_t s_buf[NP_MAX_PAYLOAD];

/* Open HTTP GET session state. */
static WiFiClient s_client;
static HTTPClient s_http;
static WiFiClient *s_stream = nullptr;
static long s_remaining = 0; /* bytes left when Content-Length is known, else -1 */
static bool s_http_active = false;

static void setLed(bool on)
{
    digitalWrite(LED_PIN, on ? LOW : HIGH); /* active-low */
}

/* ---- handlers ------------------------------------------------------ */

static void handleScan(void)
{
    /* Report the regulatory channel range actually in effect (confirms whether
     * 12/13 are scanned) and the RX antenna level the ESP is working with. */
    wifi_country_t cc;
    if (wifi_get_country(&cc))
    {
        np::logf(NP_LOG_INFO, "country %c%c, channels %d-%d", cc.cc[0], cc.cc[1],
                 cc.schan, cc.schan + cc.nchan - 1);
    }

    /* Synchronous scan, including hidden SSIDs. */
    const int n = WiFi.scanNetworks(false, true);
    np::logf(NP_LOG_INFO, "scan: %d AP(s) found", n);
    uint16_t off = 1u; /* leave room for the count byte */
    uint8_t count = 0u;

    for (int i = 0; i < n && count < 20; i++)
    {
        const String ssid = WiFi.SSID(i);
        /* Channel is logged so a missing 2.4GHz net can be traced to ch 12/13. */
        np::logf(NP_LOG_DEBUG, "  '%s' ch%d %ddBm %s", ssid.c_str(), WiFi.channel(i),
                 WiFi.RSSI(i), (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "open" : "sec");
        uint8_t ssid_len = (uint8_t)ssid.length();
        if (ssid_len > NP_SSID_MAX)
        {
            ssid_len = NP_SSID_MAX;
        }
        if (off + 3u + ssid_len > NP_MAX_PAYLOAD)
        {
            break;
        }
        s_buf[off++] = (uint8_t)(int8_t)WiFi.RSSI(i);
        s_buf[off++] = (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? NP_ENC_OPEN : NP_ENC_SECURED;
        s_buf[off++] = ssid_len;
        memcpy(&s_buf[off], ssid.c_str(), ssid_len);
        off += ssid_len;
        count++;
    }
    s_buf[0] = count;
    WiFi.scanDelete();
    np::sendFrame(NP_RSP_SCAN, s_buf, off);
}

static void sendStatus(void)
{
    uint8_t out[6];
    const wl_status_t st = WiFi.status();
    const bool connected = (st == WL_CONNECTED);
    out[0] = connected ? NP_STATE_CONNECTED : NP_STATE_DISCONNECTED;
    const IPAddress ip = WiFi.localIP();
    out[1] = ip[0];
    out[2] = ip[1];
    out[3] = ip[2];
    out[4] = ip[3];
    out[5] = (uint8_t)st; /* raw wl_status_t for diagnosing connect failures */
    setLed(connected);
    np::sendFrame(NP_RSP_STATUS, out, sizeof(out));
}

static void handleConnect(uint16_t len)
{
    const uint8_t *p = np::payload();
    uint16_t off = 0u;
    if (off >= len)
    {
        np::sendError(1);
        return;
    }
    const uint8_t ssid_len = p[off++];
    char ssid[NP_SSID_MAX + 1];
    char pass[NP_PASS_MAX + 1];
    if (off + ssid_len > len || ssid_len > NP_SSID_MAX)
    {
        np::sendError(1);
        return;
    }
    memcpy(ssid, &p[off], ssid_len);
    ssid[ssid_len] = '\0';
    off += ssid_len;

    uint8_t pass_len = (off < len) ? p[off++] : 0u;
    if (pass_len > NP_PASS_MAX || off + pass_len > len)
    {
        pass_len = 0u;
    }
    memcpy(pass, &p[off], pass_len);
    pass[pass_len] = '\0';

    np::logf(NP_LOG_INFO, "connecting to '%s' (pass %u chars)", ssid, (unsigned)pass_len);
    WiFi.mode(WIFI_STA);
    applyTxPower(); /* set TX power BEFORE associating — never mid-handshake */
    WiFi.begin(ssid, pass);
    /* Bounded, watchdog-safe wait: yields internally and returns as soon as the
     * result is known. Kept under the ESP8266 hardware watchdog (~8s) so a
     * failing association (e.g. wrong password, where the SDK retries hard)
     * can't spin long enough to reset the chip before we report the status. */
    WiFi.waitForConnectResult(6000);
    const bool connected = (WiFi.status() == WL_CONNECTED);
    np::logf(connected ? NP_LOG_INFO : NP_LOG_WARN, "connect %s (status %d), ip %s",
             connected ? "OK" : "FAILED", (int)WiFi.status(), WiFi.localIP().toString().c_str());
    sendStatus();
}

static void handleHttpOpen(uint16_t len)
{
    /* static, not stack: the URL must stay alive across the whole GET() call
     * chain (DNS + connect + header parse), and keeping 257 bytes off the 4 KB
     * cont-stack avoids overflowing it deep inside HTTPClient. */
    static char url[NP_URL_MAX + 1];
    uint16_t n = (len > NP_URL_MAX) ? NP_URL_MAX : len;
    memcpy(url, np::payload(), n);
    url[n] = '\0';

    if (s_http_active)
    {
        s_http.end();
        s_http_active = false;
    }

    uint16_t http_status = 0u;
    uint32_t content_length = NP_LENGTH_UNKNOWN;
    s_remaining = -1;
    s_stream = nullptr;

    np::logf(NP_LOG_INFO, "GET %s (heap %u)", url, (unsigned)ESP.getFreeHeap());
    if (WiFi.status() != WL_CONNECTED)
    {
        np::logf(NP_LOG_WARN, "GET aborted: WiFi not connected");
    }
    else if (!s_http.begin(s_client, url))
    {
        np::logf(NP_LOG_WARN, "GET begin() failed (bad URL?)");
    }
    else
    {
        /* Match the simple update server: HTTP/1.0, no keep-alive, no redirects.
         * This keeps HTTPClient off its chunked/reuse code paths and frees the
         * socket promptly between files. */
        s_http.useHTTP10(true);
        s_http.setReuse(false);
        s_http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        s_http.setTimeout(8000);
        const int code = s_http.GET();
        s_http_active = true;
        if (code > 0)
        {
            http_status = (uint16_t)code;
            const int sz = s_http.getSize();
            if (sz >= 0)
            {
                content_length = (uint32_t)sz;
                s_remaining = sz;
            }
            s_stream = s_http.getStreamPtr();
            np::logf(NP_LOG_INFO, "http %d, len %d", code, sz);
        }
        else
        {
            np::logf(NP_LOG_WARN, "http GET error %d", code);
        }
    }

    uint8_t out[6];
    out[0] = (uint8_t)(http_status & 0xFFu);
    out[1] = (uint8_t)(http_status >> 8u);
    out[2] = (uint8_t)(content_length & 0xFFu);
    out[3] = (uint8_t)((content_length >> 8u) & 0xFFu);
    out[4] = (uint8_t)((content_length >> 16u) & 0xFFu);
    out[5] = (uint8_t)((content_length >> 24u) & 0xFFu);
    np::sendFrame(NP_RSP_HTTP_OPEN, out, sizeof(out));
}

static void handleHttpRead(uint16_t len)
{
    uint16_t want = NP_MAX_PAYLOAD;
    if (len >= 2u)
    {
        const uint8_t *p = np::payload();
        want = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
    }
    if (want > NP_MAX_PAYLOAD)
    {
        want = NP_MAX_PAYLOAD;
    }
    if (s_remaining == 0 || s_stream == nullptr)
    {
        np::sendFrame(NP_RSP_HTTP_DATA, nullptr, 0u); /* EOF */
        return;
    }
    if (s_remaining > 0 && (long)want > s_remaining)
    {
        want = (uint16_t)s_remaining;
    }

    /* Gather up to `want` bytes; stop on idle timeout or a closed connection. */
    uint16_t got = 0u;
    uint32_t idle_deadline = millis() + 4000u;
    while (got < want)
    {
        const int avail = s_stream->available();
        if (avail > 0)
        {
            int chunk = s_stream->read(&s_buf[got], want - got);
            if (chunk > 0)
            {
                got += (uint16_t)chunk;
                idle_deadline = millis() + 4000u;
            }
        }
        else if (!s_http.connected() && s_stream->available() == 0)
        {
            break; /* server closed and buffer drained */
        }
        else if ((int32_t)(idle_deadline - millis()) <= 0)
        {
            break; /* stalled */
        }
        else
        {
            delay(1);
        }
    }

    if (s_remaining > 0)
    {
        s_remaining -= got;
    }
    np::sendFrame(NP_RSP_HTTP_DATA, s_buf, got);
}

static void handleHttpClose(void)
{
    if (s_http_active)
    {
        s_http.end();
        s_http_active = false;
    }
    s_stream = nullptr;
    s_remaining = 0;
    np::logf(NP_LOG_DEBUG, "http closed");
    np::sendFrame(NP_RSP_OK, nullptr, 0u);
}

/* ---- Arduino entry points ------------------------------------------ */

void setup()
{
    pinMode(LED_PIN, OUTPUT);
    setLed(false);
    Serial.setRxBufferSize(1024);
    Serial.begin(NETWORK_UART_BAUD);

    /* Boot banner with the reset cause. If this appears *during* a command (while
     * the console is reading the link), it proves the ESP reset mid-operation. A
     * brownout/power glitch reads as "Power On"/"External System"; a firmware
     * crash reads as "Exception" — and there we dump the Xtensa exception cause +
     * faulting PC so the crash names itself (cause 28/29 = bad pointer / overflow,
     * 9 = unaligned, 0 = illegal instr) instead of just "Exception". */
    const struct rst_info *ri = ESP.getResetInfoPtr();
    if (ri != nullptr && ri->reason == REASON_EXCEPTION_RST)
    {
        np::logf(NP_LOG_WARN, "ESP boot - proto v%u, reset: Exception cause=%u epc1=0x%08x excvaddr=0x%08x",
                 (unsigned)NETWORK_PROTOCOL_VERSION, (unsigned)ri->exccause,
                 (unsigned)ri->epc1, (unsigned)ri->excvaddr);
    }
    else
    {
        np::logf(NP_LOG_INFO, "ESP boot - proto v%u, reset: %s",
                 (unsigned)NETWORK_PROTOCOL_VERSION, ESP.getResetReason().c_str());
    }

    WiFi.mode(WIFI_STA);
    applyTxPower(); /* cap TX power up front to keep the supply from browning out */

    /* Modem sleep: power the RF modem down between AP beacons whenever the station
     * is associated and idle, cutting idle current (and heat) several-fold. Safe
     * for our slave model — only the radio sleeps, the CPU stays awake so UART
     * commands are still answered immediately. It does NOT reduce the TX peak
     * during scan/connect/HTTP (that draws full power regardless) — that spike is
     * the brownout source and needs a bulk cap on VCC, not firmware. */
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);

    /* Lowest CPU clock (80 MHz vs 160) — less core current, no downside here. */
    system_update_cpu_freq(80);

    /* Allow channels 1-13 so scans aren't limited to 1-11 (EU APs often use
     * 12/13). Manual policy with a world ("00") country code. */
    wifi_country_t country = {{'0', '0', 0}, 1, 13, WIFI_COUNTRY_POLICY_MANUAL};
    wifi_set_country(&country);

    WiFi.persistent(false); /* the console owns credential storage */
    WiFi.disconnect();
}

void loop()
{
    uint8_t type;
    uint16_t len;

    /* Block for a command (long timeout so we just wait for the master). */
    if (!np::readFrame(&type, &len, 60000u))
    {
        return;
    }

    switch (type)
    {
    case NP_CMD_PING:
    {
        /* Echo the protocol version so the console can verify a matching build. */
        const uint8_t ver = NETWORK_PROTOCOL_VERSION;
        np::sendFrame(NP_RSP_PONG, &ver, 1u);
        break;
    }
    case NP_CMD_SCAN:
        handleScan();
        break;
    case NP_CMD_CONNECT:
        handleConnect(len);
        break;
    case NP_CMD_STATUS:
        sendStatus();
        break;
    case NP_CMD_HTTP_OPEN:
        handleHttpOpen(len);
        break;
    case NP_CMD_HTTP_READ:
        handleHttpRead(len);
        break;
    case NP_CMD_HTTP_CLOSE:
        handleHttpClose();
        break;
    case NP_CMD_DISCONNECT:
        WiFi.disconnect();
        setLed(false);
        np::sendFrame(NP_RSP_OK, nullptr, 0u);
        break;
    default:
        np::logf(NP_LOG_WARN, "unknown cmd 0x%02X", type);
        np::sendError(0xFF);
        break;
    }
}
