#ifndef __ESPNOW_LINK_H
#define __ESPNOW_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include "network_protocol.h"
#include "multiplayer_interface.h" /* MP_MSG_MAX */

/*
 * Console-side typed wrappers for the ESP-NOW commands (NP_CMD_MP_*). Each call is
 * one framed UART transaction (reusing network.c's framing via networkTransact).
 * This layer is a thin, stateless pipe: it serializes outbound packets and parses
 * inbound ones, addressing peers by raw MAC. ALL session logic — discovery, peer
 * indices, heartbeat, roster — lives one level up in mp_session.c.
 */

/* The largest packet payload this console produces or cares about: a 1-byte
 * channel tag + a full app message. (A foreign ESP-NOW device could emit up to
 * NP_MP_PKT_MAX bytes; espnowLinkService drops anything larger than this — it
 * can't be one of ours.) Kept well under NP_MP_PKT_MAX to save RAM. */
#define MP_WIRE_DATA_MAX (MP_MSG_MAX + 4U)

/* One ESP-NOW packet at the transport boundary: `mac` is the destination on the
 * way out, the source on the way in. */
typedef struct
{
    uint8_t mac[NP_MP_MAC_LEN];
    uint8_t len;
    uint8_t data[MP_WIRE_DATA_MAX];
} EspNowPacket;

/* Put the ESP into ESP-NOW mode on `channel` (1..11). On success fills
 * `self_mac_out` (6 bytes) with the ESP's own MAC — this console's network
 * identity — and returns true. */
bool espnowLinkBegin(uint8_t channel, uint8_t self_mac_out[NP_MP_MAC_LEN]);

/* Leave ESP-NOW mode (the ESP returns to idle station mode). */
void espnowLinkEnd(void);

/* The per-frame exchange, split so the round-trip overlaps the game's render():
 *
 *   espnowLinkSend    — serialize `n_out` outbound packets and arm the send over
 *                       the async TX, returning at once. `out` is fully consumed
 *                       before returning, so the caller may reuse the same array
 *                       for the inbound collect. Returns false on a TX failure.
 *   espnowLinkCollect — read the reply to the in-flight send (filled into the RX
 *                       ring meanwhile) into `in`, up to `max_in`. Returns the
 *                       number of inbound packets (0 on transport failure / empty).
 *
 * Exactly one Send is outstanding at a time (one command, one reply, in order):
 * mp_session.c calls Send after the game's update() and Collect at the start of
 * the next frame. */
bool espnowLinkSend(const EspNowPacket *out, uint8_t n_out);
uint8_t espnowLinkCollect(EspNowPacket *in, uint8_t max_in);

/* Blocking convenience = Send then Collect (waits the round-trip out). The SAME
 * array may back `out` and `in`. Used off the per-frame path (e.g. the BYE on
 * session stop), not where overlap matters. */
uint8_t espnowLinkService(const EspNowPacket *out, uint8_t n_out,
                          EspNowPacket *in, uint8_t max_in);

#endif /* __ESPNOW_LINK_H */
