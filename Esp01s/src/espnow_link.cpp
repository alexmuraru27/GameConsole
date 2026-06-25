/*
 * ESP-NOW multiplayer transport — see espnow_link.h. A stateless byte mover: it
 * relays the console's outbound batch onto the air and drains inbound packets
 * (tagged with their source MAC) back. Session logic lives in mp_session.c on
 * the console.
 */
#include "espnow_link.h"

#include <ESP8266WiFi.h>
#include <espnow.h> /* SDK ESP-NOW API (esp_now_*) */
#include <string.h>

extern "C"
{
#include "user_interface.h" /* wifi_set_channel() */
}

#include "network_protocol.h"
#include "protocol.h"

static const uint8_t MP_BROADCAST_MAC[NP_MP_MAC_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool s_mp_active = false;
static uint8_t s_mp_channel = 1u;

/* Inbound packet ring, single-producer (the recv callback, SDK task context) /
 * single-consumer (espnowHandleService, the main loop) — a classic lock-free
 * ring, safe on the single-core NONOS SDK. A full ring drops the newest packet. */
#define MP_RX_SLOTS 8u
typedef struct
{
    uint8_t mac[NP_MP_MAC_LEN];
    uint8_t len;
    uint8_t data[NP_MP_PKT_MAX];
} MpRxSlot;
static MpRxSlot s_mp_rx[MP_RX_SLOTS];
static volatile uint8_t s_mp_rx_head; /* written by the callback */
static volatile uint8_t s_mp_rx_tail; /* read by the service handler */
static uint32_t s_mp_rx_dropped;

static void onEspNowRecv(uint8_t *mac, uint8_t *data, uint8_t len)
{
    const uint8_t next = (uint8_t)((s_mp_rx_head + 1u) % MP_RX_SLOTS);
    if (next == s_mp_rx_tail)
    {
        s_mp_rx_dropped++; /* ring full — newest packet dropped (console re-syncs) */
        return;
    }
    if (len > NP_MP_PKT_MAX)
    {
        len = NP_MP_PKT_MAX;
    }
    MpRxSlot *slot = &s_mp_rx[s_mp_rx_head];
    memcpy(slot->mac, mac, NP_MP_MAC_LEN);
    slot->len = len;
    memcpy(slot->data, data, len);
    s_mp_rx_head = next;
}

/* Ensure `mac` is a registered ESP-NOW peer before unicasting to it. New peers
 * are learned lazily from the console's send batch (it gets MACs from inbound
 * packets), so no explicit pairing command is needed. */
static void mpEnsurePeer(const uint8_t *mac)
{
    if (esp_now_is_peer_exist((uint8_t *)mac) == 0)
    {
        esp_now_add_peer((uint8_t *)mac, ESP_NOW_ROLE_COMBO, s_mp_channel, nullptr, 0);
    }
}

void espnowHandleBegin(uint16_t len)
{
    const uint8_t *p = np::payload();
    s_mp_channel = (len >= 1u) ? p[0] : 1u;

    /* Drop any AP association and pin the radio to the multiplayer channel — both
     * peers must share it. WIFI_STA (not AP) keeps the channel ours to set. */
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (s_mp_active)
    {
        esp_now_deinit(); /* restart cleanly if BEGIN is called twice */
        s_mp_active = false;
    }
    wifi_set_channel(s_mp_channel);

    if (esp_now_init() != 0)
    {
        np::logf(NP_LOG_ERROR, "esp-now init failed");
        np::sendError(1);
        return;
    }
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(onEspNowRecv);
    esp_now_add_peer((uint8_t *)MP_BROADCAST_MAC, ESP_NOW_ROLE_COMBO, s_mp_channel, nullptr, 0);

    s_mp_rx_head = 0u;
    s_mp_rx_tail = 0u;
    s_mp_rx_dropped = 0u;
    s_mp_active = true;

    uint8_t mac[NP_MP_MAC_LEN];
    WiFi.macAddress(mac);
    np::logf(NP_LOG_INFO, "esp-now up ch%u, mac %02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)s_mp_channel, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    np::sendFrame(NP_RSP_MP_BEGIN, mac, NP_MP_MAC_LEN);
}

void espnowHandleEnd(void)
{
    if (s_mp_active)
    {
        esp_now_deinit();
        s_mp_active = false;
    }
    np::logf(NP_LOG_DEBUG, "esp-now down");
    np::sendFrame(NP_RSP_OK, nullptr, 0u);
}

void espnowHandleService(uint16_t len)
{
    if (!s_mp_active)
    {
        np::sendError(2); /* SERVICE without BEGIN */
        return;
    }

    /* Outbound batch: {n, n x {dst mac[6], len:u8, bytes}}. Send each one. */
    const uint8_t *p = np::payload();
    uint16_t off = 0u;
    const uint8_t n_out = (len >= 1u) ? p[off++] : 0u;
    for (uint8_t i = 0u; i < n_out; i++)
    {
        if (off + NP_MP_MAC_LEN + 1u > len)
        {
            break;
        }
        const uint8_t *dst = &p[off];
        off += NP_MP_MAC_LEN;
        const uint8_t plen = p[off++];
        if (off + plen > len || plen > NP_MP_PKT_MAX)
        {
            break;
        }
        mpEnsurePeer(dst);
        esp_now_send((uint8_t *)dst, (uint8_t *)&p[off], plen);
        off += plen;
    }

    /* Inbound batch: drain the ring into {n, n x {src mac[6], len:u8, bytes}},
     * stopping before the response would exceed one frame. Leftovers wait for the
     * next service. Static (not stack): a 1 KB local risks the ~4 KB cont stack,
     * exactly like the HTTP handlers' s_buf. */
    static uint8_t out[NP_MAX_PAYLOAD];
    uint16_t w = 1u; /* reserve out[0] for the count */
    uint8_t n_in = 0u;
    while (s_mp_rx_tail != s_mp_rx_head)
    {
        const MpRxSlot *slot = &s_mp_rx[s_mp_rx_tail];
        const uint16_t need = NP_MP_MAC_LEN + 1u + slot->len;
        if (w + need > NP_MAX_PAYLOAD)
        {
            break;
        }
        memcpy(&out[w], slot->mac, NP_MP_MAC_LEN);
        w += NP_MP_MAC_LEN;
        out[w++] = slot->len;
        memcpy(&out[w], slot->data, slot->len);
        w += slot->len;
        n_in++;
        s_mp_rx_tail = (uint8_t)((s_mp_rx_tail + 1u) % MP_RX_SLOTS);
    }
    out[0] = n_in;
    np::sendFrame(NP_RSP_MP_SERVICE, out, w);
}
