#ifndef __MP_SESSION_H
#define __MP_SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include "multiplayer_interface.h" /* MpRole, MpStatus, MpHostInfo, MP_* limits */

/*
 * OS-level multiplayer session manager — the brain of console-to-console play.
 *
 * The ESP-01 (via espnow_link.c) is a dumb byte mover; this module owns ALL
 * session logic: discovery beacons, the host/join handshake, player-index
 * assignment, the roster, heartbeat ("ping-pong") liveness, and the inbound /
 * outbound app-message mailboxes a game reads through the mp* syscalls.
 *
 * It is driven by the game (which owns the lobby UI) through the kernel syscall
 * dispatcher (syscall.c), and serviced once per frame from the inter-frame seam
 * in game_loader.c's gameRuntimeService(). A host advertises the *running* game
 * (its .bin identity, taken from the loader) so a joiner only discovers hosts of
 * the same game. See docu/espnow.md for the full protocol and diagrams.
 */

/* True while a session OR a join-mode scan is in progress — i.e. while
 * mpSessionService() must run each frame. False means zero overhead. */
bool mpSessionActive(void);

/* Per-frame service: exchange one MP_SERVICE batch with the ESP (send due
 * beacons/heartbeats + queued messages, receive inbound), process the session
 * protocol, and sweep peer liveness. `now_ms` is getSysTime(). */
void mpSessionService(uint32_t now_ms);

/* ---- backends for the mp* syscalls (see console_syscalls.h for semantics) ---- */
MpRole mpSessionGetRole(void);
MpStatus mpSessionHostStart(void);
MpStatus mpSessionJoinStart(void);
int mpSessionScan(MpHostInfo *out, int max);
MpStatus mpSessionJoin(uint8_t host_handle);
void mpSessionStop(void);
uint8_t mpSessionGetSelfIndex(void);
uint8_t mpSessionGetPlayerCount(void);
bool mpSessionIsConnected(uint8_t index);
int mpSessionGetName(uint8_t index, char *buf, int max);
int mpSessionGetSelfName(char *buf, int max);
bool mpSessionSend(uint8_t dst_index, const uint8_t *data, uint16_t len);
int mpSessionReceive(uint8_t *src_index_out, uint8_t *data, uint16_t max);

#endif /* __MP_SESSION_H */
