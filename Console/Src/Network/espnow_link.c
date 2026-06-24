#include "espnow_link.h"
#include "network_internal.h"
#include "logger.h"

#include <string.h>

/*
 * ESP-NOW transport wrappers — see espnow_link.h. Stateless serializer over the
 * framed UART link: it turns EspNowPackets into NP_CMD_MP_* frames and back. The
 * ESP is a dumb mover; session logic lives in mp_session.c.
 */

/* Response timeouts. SERVICE is answered immediately (no WiFi blocking on the
 * ESP), so it is short; BEGIN/END do esp_now_init/deinit + WiFi.disconnect. */
#define ESPNOW_TIMEOUT_SERVICE 250U
#define ESPNOW_TIMEOUT_CONTROL 2000U

/* Outbound MP_SERVICE command-payload cap. Far below the 1 KB protocol max: one
 * service batches only a few small packets (beacon/heartbeat/roster + the odd app
 * message), so this comfortably holds a realistic frame while saving RAM. Any
 * packet that would overflow it simply waits for the next service. */
#define MP_CMD_MAX 896U

/* Outbound MP_SERVICE command payload, assembled here. Static (not stack): the
 * session is serviced from one privileged per-frame context, never reentrantly. */
static uint8_t s_cmd[MP_CMD_MAX];

bool espnowLinkBegin(uint8_t channel, uint8_t self_mac_out[NP_MP_MAC_LEN])
{
    const uint8_t payload[1] = {channel};
    uint8_t rsp_type = 0U;
    uint16_t rsp_len = 0U;
    const uint8_t *rp = NULL;

    if (!networkTransact(NP_CMD_MP_BEGIN, payload, 1U, &rsp_type, &rsp_len, &rp, ESPNOW_TIMEOUT_CONTROL))
    {
        LOGGER_LOG_WARN(LOGGER_NETWORK, "esp-now begin: no response");
        return false;
    }
    if (rsp_type != NP_RSP_MP_BEGIN || rsp_len < NP_MP_MAC_LEN || rp == NULL)
    {
        LOGGER_LOG_WARN(LOGGER_NETWORK, "esp-now begin: bad reply (type 0x%02X len %u)",
                        (unsigned)rsp_type, (unsigned)rsp_len);
        return false;
    }
    if (self_mac_out != NULL)
    {
        memcpy(self_mac_out, rp, NP_MP_MAC_LEN);
    }
    LOGGER_LOG_INFO(LOGGER_NETWORK, "esp-now begin ch%u, self %02X:%02X:%02X:%02X:%02X:%02X",
                    (unsigned)channel, rp[0], rp[1], rp[2], rp[3], rp[4], rp[5]);
    return true;
}

void espnowLinkEnd(void)
{
    uint8_t rsp_type = 0U;
    uint16_t rsp_len = 0U;
    (void)networkTransact(NP_CMD_MP_END, NULL, 0U, &rsp_type, &rsp_len, NULL, ESPNOW_TIMEOUT_CONTROL);
    LOGGER_LOG_INFO(LOGGER_NETWORK, "esp-now end");
}

uint8_t espnowLinkService(const EspNowPacket *out, uint8_t n_out,
                          EspNowPacket *in, uint8_t max_in)
{
    /* Assemble the outbound batch: {n, n x {dst mac[6], len:u8, bytes}}. Clamp to
     * the frame budget; any packet that does not fit is dropped this frame (the
     * session re-queues SYS traffic and the game re-sends as needed). */
    uint16_t w = 1U; /* reserve s_cmd[0] for the count */
    uint8_t n_sent = 0U;
    for (uint8_t i = 0U; i < n_out; i++)
    {
        const uint16_t need = NP_MP_MAC_LEN + 1U + out[i].len;
        if (out[i].len > MP_WIRE_DATA_MAX || w + need > MP_CMD_MAX)
        {
            break;
        }
        memcpy(&s_cmd[w], out[i].mac, NP_MP_MAC_LEN);
        w += NP_MP_MAC_LEN;
        s_cmd[w++] = out[i].len;
        memcpy(&s_cmd[w], out[i].data, out[i].len);
        w += out[i].len;
        n_sent++;
    }
    s_cmd[0] = n_sent;

    uint8_t rsp_type = 0U;
    uint16_t rsp_len = 0U;
    const uint8_t *rp = NULL;
    if (!networkTransact(NP_CMD_MP_SERVICE, s_cmd, w, &rsp_type, &rsp_len, &rp, ESPNOW_TIMEOUT_SERVICE))
    {
        return 0U; /* transport failure — caller treats as "no inbound this frame" */
    }
    if (rsp_type != NP_RSP_MP_SERVICE || rsp_len < 1U || rp == NULL)
    {
        return 0U;
    }

    /* Parse the inbound batch: {n, n x {src mac[6], len:u8, bytes}}. */
    uint16_t off = 0U;
    const uint8_t n_in = rp[off++];
    uint8_t got = 0U;
    for (uint8_t i = 0U; i < n_in && got < max_in; i++)
    {
        if ((uint16_t)(off + NP_MP_MAC_LEN + 1U) > rsp_len)
        {
            break;
        }
        uint8_t mac[NP_MP_MAC_LEN];
        memcpy(mac, &rp[off], NP_MP_MAC_LEN);
        off += NP_MP_MAC_LEN;
        const uint8_t plen = rp[off++];
        if ((uint16_t)(off + plen) > rsp_len)
        {
            break;
        }
        if (plen > MP_WIRE_DATA_MAX)
        {
            off += plen; /* foreign / oversized packet — skip it, keep parsing */
            continue;
        }
        memcpy(in[got].mac, mac, NP_MP_MAC_LEN);
        memcpy(in[got].data, &rp[off], plen);
        in[got].len = plen;
        off += plen;
        got++;
    }
    return got;
}
