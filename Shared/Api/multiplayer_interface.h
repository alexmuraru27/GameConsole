#ifndef __MULTIPLAYER_API_H
#define __MULTIPLAYER_API_H

#include <stdint.h>

/*
 * Multiplayer types shared by the console and loaded games. Local wireless
 * multiplayer rides ESP-NOW (connectionless 2.4 GHz, no AP) through the ESP-01S;
 * the console OS owns the whole session (discovery, peer table, player indices,
 * heartbeat liveness, message mailboxes) and a game reaches it through the
 * mp* syscalls in console_syscalls.h. A game never sees a MAC address — peers are
 * addressed by a small player index (0 = host), and hosts are picked from a
 * scanned list by an opaque handle.
 *
 * See docu/espnow.md for the full stack (protocol, sequence diagrams, ping-pong).
 */

/* This console's role in the current session. NONE means no session is active. */
typedef enum MpRole
{
    MP_ROLE_NONE = 0U,
    MP_ROLE_HOST = 1U,   /* advertises the game and assigns player slots (index 0) */
    MP_ROLE_CLIENT = 2U, /* discovered + joined a host (index 1..MP_MAX_PLAYERS-1)  */
} MpRole;

/* Result of a multiplayer control call (start / join). 0 == success. */
typedef enum MpStatus
{
    MP_OK = 0U,
    MP_ERR = 1U,     /* generic failure (no ESP link, wrong state, bad arg)        */
    MP_PENDING = 2U, /* accepted, still settling — poll mpGetRole/mpGetPlayerCount */
    MP_FULL = 3U,    /* the host's roster is full                                  */
} MpStatus;

/* Up to four consoles in one session: the host plus three joiners. */
#define MP_MAX_PLAYERS 4U

/* Pass as the destination index to mpSend() to reach every connected peer. */
#define MP_BROADCAST_INDEX 0xFFU

/* Largest app message a game may send/receive in one packet. Kept below the
 * 250-byte ESP-NOW frame limit, leaving room for the OS's 1-byte channel tag. */
#define MP_MSG_MAX 200U

/* Display-name length cap (excludes the NUL), as shown in the host/join list. */
#define MP_NAME_MAX 16U

/* One entry in the list mpScanHosts() returns to a joining game: a nearby console
 * hosting the SAME game (the OS filters the discovery beacons by game identity).
 * `handle` is an opaque id to hand back to mpJoin(); the game never sees a MAC. */
typedef struct MpHostInfo
{
    uint8_t handle;            /* opaque host id for mpJoin()                 */
    uint8_t player_count;      /* players already in that host's lobby (1..4) */
    char name[MP_NAME_MAX + 1U]; /* host's display name, NUL-terminated       */
} MpHostInfo;

#endif /* __MULTIPLAYER_API_H */
