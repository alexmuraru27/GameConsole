#ifndef __MP_WIRE_H
#define __MP_WIRE_H

#include <stdint.h>
#include <stdbool.h>
#include "multiplayer_interface.h" /* MP_NAME_MAX */
#include "network_protocol.h"      /* NP_MP_MAC_LEN */

/*
 * ESP-NOW multiplayer wire codec — the byte-level protocol, isolated from the
 * stateful session logic in mp_session.c so the on-wire format can be audited
 * (and fuzzed) on its own. Everything here is pure: the builders serialize
 * explicit inputs into a caller buffer and the reader/parsers walk a received
 * buffer with bounds checks. No session state, no globals.
 *
 * Every ESP-NOW packet starts with a 1-byte CHANNEL tag; a SYS packet's second
 * byte is the SYS message type. See docu/espnow.md for the full protocol.
 */

/* CHANNEL tags (first byte of every packet). */
#define MP_CH_SYS 0U /* session protocol (beacon / join / roster / heartbeat / bye) */
#define MP_CH_APP 1U /* opaque game payload, delivered to the inbound mailbox        */

/* SYS message types (second byte of a MP_CH_SYS packet). */
enum
{
    MP_SYS_BEACON = 0,      /* host -> broadcast: {gid_len,gid,player_count,name_len,name} */
    MP_SYS_JOIN_REQ = 1,    /* client -> host:    {name_len, name}                        */
    MP_SYS_JOIN_ACCEPT = 2, /* host -> client:    {assigned_index, <roster>}              */
    MP_SYS_JOIN_REJECT = 3, /* host -> client:    {reason}                                */
    MP_SYS_ROSTER = 4,      /* host -> all:       {<roster>}                              */
    MP_SYS_HEARTBEAT = 5,   /* any -> broadcast:  {}                                      */
    MP_SYS_BYE = 6,         /* any -> broadcast:  {}  (graceful leave)                    */
};
/* <roster> = {count, count x {index, mac[6], name_len, name}} */

/* Advertised game identity on the wire (the .bin basename, lower-cased). */
#define MP_GAME_ID_MAX 24U

/* ------------------------------------------------------------------ *
 *  MpReader — a bounds-checked cursor over a received SYS body. Every
 *  read fails (returns false / clamps) rather than running off the end,
 *  replacing the hand-rolled offset arithmetic the handlers used to
 *  duplicate.
 * ------------------------------------------------------------------ */
typedef struct
{
    const uint8_t *p;
    uint8_t len;
    uint8_t off;
} MpReader;

void mpReaderInit(MpReader *r, const uint8_t *p, uint8_t len);
uint8_t mpReaderRemaining(const MpReader *r);

/* Read one byte. False if none remain. */
bool mpReadU8(MpReader *r, uint8_t *out);
/* Read exactly n bytes into dst. False (and no advance) if fewer than n remain. */
bool mpReadBytes(MpReader *r, uint8_t *dst, uint8_t n);
/* Read a u8 length-prefix then expose that many bytes in place, clamped to what
 * remains. On success *name points into the buffer, *name_len is the clamped
 * length, and the cursor advances past them. False only if the length byte is
 * absent. (The caller copies with its own MP_NAME_MAX clamp, as before.) */
bool mpReadNameRef(MpReader *r, const uint8_t **name, uint8_t *name_len);

/* ------------------------------------------------------------------ *
 *  Builders — serialize into `buf`, return the byte count written.
 * ------------------------------------------------------------------ */

/* One roster entry as seen by the builder (a view onto the session's peer). */
typedef struct
{
    uint8_t index;
    const uint8_t *mac; /* NP_MP_MAC_LEN bytes */
    const char *name;   /* NUL-terminated                 */
} MpWirePeer;

uint8_t mpWireBuildSimple(uint8_t *buf, uint8_t sys_type);
uint8_t mpWireBuildJoinReject(uint8_t *buf, uint8_t reason);
uint8_t mpWireBuildJoinReq(uint8_t *buf, const char *self_name);
uint8_t mpWireBuildBeacon(uint8_t *buf, const char *game_id, uint8_t player_count, const char *self_name);

/* Roster body {count, count x entry} appended at write offset `w`. */
uint8_t mpWireBuildRosterBody(uint8_t *buf, uint8_t w, const MpWirePeer *peers, uint8_t count);
uint8_t mpWireBuildRoster(uint8_t *buf, const MpWirePeer *peers, uint8_t count);
uint8_t mpWireBuildJoinAccept(uint8_t *buf, uint8_t assigned_index, const MpWirePeer *peers, uint8_t count);

/* ------------------------------------------------------------------ *
 *  Beacon parse — the one self-contained SYS parse. Fields point into
 *  `body`; `ok` is false on a malformed (too-short) beacon.
 * ------------------------------------------------------------------ */
typedef struct
{
    bool ok;
    const uint8_t *game_id;
    uint8_t game_id_len;
    uint8_t player_count;
    const uint8_t *name;
    uint8_t name_len;
} MpBeaconFields;

MpBeaconFields mpWireParseBeacon(const uint8_t *body, uint8_t blen);

#endif /* __MP_WIRE_H */
