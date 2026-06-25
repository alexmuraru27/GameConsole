/*
 * WiFi station subsystem — see wifi.h. Scans (passively, channels 1-11), joins
 * an AP with console-supplied credentials, and reports link status. The radio
 * regulatory setup lives here too, in wifiInit().
 */
#include "wifi.h"

#include <ESP8266WiFi.h>
#include <string.h>

extern "C"
{
#include "user_interface.h" /* country/scan/phy/cpu-freq SDK calls */
}

#include "network_protocol.h"
#include "protocol.h"

/* On-board LED (GPIO2, active-low) used as a WiFi-status light. */
static const uint8_t LED_PIN = 2;

/*
 * WiFi TX power (dBm, valid 0..20.5). Set to the chip maximum for best range.
 * We briefly capped this low chasing a suspected supply brownout, but a stiff
 * bench supply (3.35 V / 1.5 A) reproduced the crashes identically, ruling power
 * out — the faults were a regulatory-channel scan bug, not a sagging rail — so
 * there is no reason to under-drive the radio.
 */
static const float ESP_TX_POWER_DBM = 20.5f;

/* Scratch for building the scan-result response payload. */
static uint8_t s_buf[NP_MAX_PAYLOAD];

static void setLed(bool on)
{
    digitalWrite(LED_PIN, on ? LOW : HIGH); /* active-low */
}

static void applyTxPower(void)
{
    /* Call once, after WiFi.mode() and BEFORE WiFi.begin(). Re-applying it while
     * an association is in flight mutates the PHY power tables under the SDK's RF
     * task and can crash it, so it is deliberately not called mid-connect. */
    WiFi.setOutputPower(ESP_TX_POWER_DBM);
}

/* ---- scan ---------------------------------------------------------- */

/* Passive-scan result, filled by the SDK callback (scanDoneCb) and consumed by
 * wifiHandleScan. The response payload is built straight into s_buf as
 * [count][rssi,enc,ssid_len,ssid...]..., so no extra buffer is needed. */
static volatile bool s_scan_done = false;
static volatile uint16_t s_scan_len = 0u;
static volatile uint8_t s_scan_count = 0u;

/* SDK scan-complete callback. Runs in the SDK task context, cooperatively with
 * the main loop (which is parked in wifiHandleScan's delay() wait), so writing
 * the shared s_buf / flags here is safe — no preemption, no reentrancy. */
static void scanDoneCb(void *arg, STATUS status)
{
    uint16_t off = 1u; /* leave room for the count byte */
    uint8_t count = 0u;
    if (status == OK)
    {
        for (struct bss_info *bss = (struct bss_info *)arg;
             bss != nullptr && count < 20u; bss = STAILQ_NEXT(bss, next))
        {
            uint8_t ssid_len = bss->ssid_len;
            if (ssid_len > NP_SSID_MAX)
            {
                ssid_len = NP_SSID_MAX;
            }
            if (off + 3u + ssid_len > NP_MAX_PAYLOAD)
            {
                break;
            }
            /* Channel is logged so a weak/missing net can be tied to ch 12/13.
             * ssid is not NUL-terminated, so print with an explicit length. */
            np::logf(NP_LOG_DEBUG, "  '%.*s' ch%d %ddBm %s", (int)ssid_len, (const char *)bss->ssid,
                     (int)bss->channel, (int)bss->rssi, (bss->authmode == AUTH_OPEN) ? "open" : "sec");
            s_buf[off++] = (uint8_t)bss->rssi;
            s_buf[off++] = (bss->authmode == AUTH_OPEN) ? NP_ENC_OPEN : NP_ENC_SECURED;
            s_buf[off++] = ssid_len;
            memcpy(&s_buf[off], bss->ssid, ssid_len);
            off += ssid_len;
            count++;
        }
    }
    else
    {
        np::logf(NP_LOG_WARN, "scan callback status %d", (int)status);
    }
    s_buf[0] = count;
    s_scan_len = off;
    s_scan_count = count;
    s_scan_done = true;
}

void wifiHandleScan(void)
{
    /* Report the regulatory channel range actually in effect (confirms whether
     * 12/13 are scanned) and the free heap (a low/fragmented heap can crash the
     * SDK scan allocator). */
    wifi_country_t cc;
    if (wifi_get_country(&cc))
    {
        np::logf(NP_LOG_INFO, "country %c%c, channels %d-%d, heap %u", cc.cc[0], cc.cc[1],
                 cc.schan, cc.schan + cc.nchan - 1, (unsigned)ESP.getFreeHeap());
    }

    /* PASSIVE scan across every channel in the regulatory range. A passive scan
     * only listens for beacons, so it is safe on the passive-only channels 12/13
     * (an ACTIVE probe-request sweep there makes the NONOS RF code take a bad
     * branch and die with an IllegalInstruction). The async result arrives via
     * scanDoneCb; we wait for it watchdog-safely. */
    struct scan_config config;
    memset(&config, 0, sizeof(config));
    config.channel = 0;             /* 0 = every channel allowed by the country */
    config.show_hidden = 0;
    config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
    config.scan_time.passive = 120; /* ms/channel; ~1.6 s across 13 channels */

    s_scan_done = false;
    if (!wifi_station_scan(&config, scanDoneCb))
    {
        np::logf(NP_LOG_WARN, "scan start failed");
        s_buf[0] = 0u;
        np::sendFrame(NP_RSP_SCAN, s_buf, 1u);
        return;
    }

    const uint32_t deadline = millis() + 8000u;
    while (!s_scan_done && (int32_t)(deadline - millis()) > 0)
    {
        delay(10); /* yields to the SDK/RF task so the callback can run */
    }
    if (!s_scan_done)
    {
        np::logf(NP_LOG_WARN, "scan timeout");
        s_buf[0] = 0u;
        np::sendFrame(NP_RSP_SCAN, s_buf, 1u);
        return;
    }

    np::logf(NP_LOG_INFO, "scan: %d AP(s)", (int)s_scan_count);
    np::sendFrame(NP_RSP_SCAN, s_buf, s_scan_len);
}

/* ---- status / connect / disconnect --------------------------------- */

void wifiSendStatus(void)
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

void wifiHandleConnect(uint16_t len)
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
    /* Force 802.11b PHY: the lowest-rate, longest-range, most sensitive mode, for
     * link margin on the ESP-01S's weak antenna. Set here (before begin), not in
     * setup(), so it can never crash-loop the boot. Caveat: an AP set to "802.11n
     * only" (legacy/b rates disabled) won't accept an 11b station — if connects
     * start failing on such a router, switch this to PHY_MODE_11N. */
    wifi_set_phy_mode(PHY_MODE_11B);
    WiFi.begin(ssid, pass);
    /* Bounded, watchdog-safe wait: yields internally and returns as soon as the
     * result is known. Kept under the ESP8266 hardware watchdog (~8s) so a
     * failing association (e.g. wrong password, where the SDK retries hard)
     * can't spin long enough to reset the chip before we report the status. */
    WiFi.waitForConnectResult(6000);
    const bool connected = (WiFi.status() == WL_CONNECTED);
    np::logf(connected ? NP_LOG_INFO : NP_LOG_WARN, "connect %s (status %d), ip %s",
             connected ? "OK" : "FAILED", (int)WiFi.status(), WiFi.localIP().toString().c_str());
    wifiSendStatus();
}

void wifiHandleDisconnect(void)
{
    WiFi.disconnect();
    setLed(false);
    np::sendFrame(NP_RSP_OK, nullptr, 0u);
}

/* ---- bring-up ------------------------------------------------------ */

void wifiInit(void)
{
    pinMode(LED_PIN, OUTPUT);
    setLed(false);

    WiFi.mode(WIFI_STA);
    applyTxPower(); /* full TX power up front, before associating */
    /* NOTE: PHY mode is set per-connect in wifiHandleConnect(), not here — forcing
     * it in setup() risks crash-looping the boot before the firmware can answer. */

    /* Modem sleep: power the RF modem down between AP beacons whenever the station
     * is associated and idle, cutting idle current (and heat) several-fold. Safe
     * for our slave model — only the radio sleeps, the CPU stays awake so UART
     * commands are still answered immediately. It does NOT reduce the TX peak
     * during scan/connect/HTTP (that draws full power regardless) — that spike is
     * the brownout source and needs a bulk cap on VCC, not firmware. */
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);

    /* Lowest CPU clock (80 MHz vs 160) — less core current, no downside here. */
    system_update_cpu_freq(80);

    /* Restrict Wi-Fi to channels 1-11 (FCC) with a *real* country code, MANUAL
     * policy. This is required, and the choice is research-backed:
     *   - The SDK default ({cc="CN", 1-13, AUTO}) scans 12/13, and the ESP8266
     *     PHY crashes there: LoadProhibited (cause 28) in phy_dig_spur_set, a
     *     per-channel spur-suppression routine. This is the well-documented
     *     "avoid Wi-Fi channels 12-13-14 on ESP devices" hardware limitation
     *     (Olimex; Espressif Wi-Fi channel-selection guidelines).
     *   - The world code "00" (any nchan) crash-loops the boot, so the code must
     *     be a real one.
     * "US"/1-11 is the proven config that booted and ran the whole download
     * pipeline. An AP on 12/13 cannot be reached reliably on this PHY — set such
     * a router to a channel within 1-11 instead. */
    wifi_country_t country = {{'U', 'S', 0}, 1, 11, WIFI_COUNTRY_POLICY_MANUAL};
    wifi_set_country(&country);

    WiFi.persistent(false); /* the console owns credential storage */
    WiFi.disconnect();
}
