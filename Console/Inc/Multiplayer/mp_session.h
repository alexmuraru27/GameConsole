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
 * dispatcher (syscall.c), and serviced each frame from the inter-frame seams in
 * game_loader.c (collect before update(), flush after). A host advertises the *running* game
 * (its .bin identity, taken from the loader) so a joiner only discovers hosts of
 * the same game. See docu/espnow.md for the full protocol and diagrams.
 */

/* True while a session OR a join-mode scan is in progress — i.e. while the
 * per-frame collect/send must run. False means zero overhead. */
bool mpSessionActive(void);

/* Per-frame service, pipelined across the frame so the ESP round-trip overlaps
 * render() instead of busy-waiting:
 *   mpSessionCollect — BEFORE update(): ingest the reply to last frame's request
 *                      (peer liveness, SYS protocol, app inbox).
 *   mpSessionFlush   — AFTER update(): assemble due beacons/heartbeats + queued
 *                      messages and arm the batch over the async TX, then sweep
 *                      peer liveness. The reply returns during render(). (Named
 *                      "flush", not "send", to avoid clashing with the mpSend
 *                      syscall backend mpSessionSend below.)
 * `now_ms` is getSysTime(). See the seam in game_loader.c. */
void mpSessionCollect(uint32_t now_ms);
void mpSessionFlush(uint32_t now_ms);

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
