#ifndef __CONSOLE_SYSCALLS_H
#define __CONSOLE_SYSCALLS_H

#include <stdint.h>
#include "stdbool.h"
#include "renderer_interface.h"
#include "font_interface.h"
#include "asset_interface.h"
#include "settings_interface.h"
#include "joystick_interface.h"
#include "multiplayer_interface.h"

/*
 * The strongly-typed console API as seen by a game. Every function here is a
 * thin SVC trampoline (defined in console_syscalls.c); the game links this and
 * calls these directly. There is no function-pointer table and no shared-RAM
 * struct: the prototypes are the contract, checked at compile time, and the
 * console dispatcher on the other side mirrors them. A game includes this through
 * game_console_api.h.
 */

/* ---- system time ---- */
uint32_t getSysTime(void);
void delay(uint32_t sys_time_delta);

/* Microseconds elapsed between the previous two update() calls — the OS measures
 * it (from the 168 MHz cycle counter) so a game can scale movement/animation by
 * real time and run identically at any frame rate. Returns 0 on the first frame;
 * clamped to a sane maximum so a momentary stall can't teleport things. Typical
 * use: pos += velocity_per_second * (getDeltaTimeUs() * 1e-6f). */
uint32_t getDeltaTimeUs(void);

/* A fresh 32-bit value from the console's hardware true-RNG — seed-free and
 * statistically solid (no getSysTime() tricks needed). For a bounded range use
 * getRandom() % n (a tiny modulo bias is irrelevant for gameplay). */
uint32_t getRandom(void);

/* ---- buzzer ---- */
uint8_t buzzerGetMaxTracks(void);
bool buzzerPlay(uint8_t track_number, bool is_looped, const uint16_t *notes_data, uint16_t notes_number);
bool buzzerPlayWithFlag(uint8_t track_number, bool is_looped, const uint16_t *notes_data, uint16_t notes_number, bool *on_done_flag);
bool buzzerPause(uint8_t track_number);
bool buzzerResume(uint8_t track_number);
bool buzzerStop(uint8_t track_number);
void buzzerStopAll(void);

/* ---- input ---- */
/* Read the whole pad in one trap. *out gets each button's held / pressed /
 * released flags (e.g. out->special1.pressed) plus the four analog axes
 * (-512..+512). pressed and released are the edges since the previous frame — use
 * pressed for one-shot actions (btnp), held for continuous movement. */
void inputGetState(InputState *out);

/* ---- renderer ---- */
void rendererClear(void);
void rendererSetBackground(uint16_t color);
void rendererSubmitLayer(Layer layer, const Sprite *sprites, uint16_t count);
void rendererRender(void);
uint16_t rendererGetWidthPixels(void);
uint16_t rendererGetHeightPixels(void);
uint16_t rendererSystemColor(uint8_t system_index);

/* ---- assets ---- */
uint8_t assetLoaderGetAssetMetadata(uint32_t asset_id, AssetMetaData *asset_metadata_out);
uint8_t assetLoaderGetAssetData(uint32_t asset_id, uint8_t *buffer, uint32_t buffer_size_bytes);

/* ---- settings (persisted for the running game, keyed by its .bin name) ---- */
uint8_t settingsRead(uint16_t expected_version, uint8_t *buffer, uint16_t *size);
uint8_t settingsWrite(uint16_t version, const uint8_t *data, uint16_t size);
uint8_t settingsClear(void);

/* ---- fonts (glyph data lives in console flash; games draw text through these) ---- */
uint16_t fontGlyphW(FontSize size);
uint16_t fontGlyphH(FontSize size);
void fontGet(uint8_t ch, FontSize size, const uint8_t **pixels);
uint16_t fontSize(FontSize size, uint8_t scale);
void fontScale(uint8_t ch, FontSize size, uint8_t scale, uint8_t *dst);

/* ---- logging ---- */
/* Formatted game-side into a bounded buffer, then handed to the console as raw
 * bytes — no format string ever reaches the console, so there is no way for a
 * game to make the console deref an arbitrary pointer via %s. */
void gameLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- OS UI services ---- */
/* Pop up the console's on-screen keyboard modally and let the player type a
 * string. Blocks (the OS runs the keyboard console-side) until the player
 * confirms with DONE or cancels; returns true on confirm, false on cancel. `buf`
 * (capacity `max`, NUL-terminated) receives the text and may be pre-filled to
 * offer a default. Handy for high-score name entry. `title` labels the screen.
 * The OS paints the keyboard over your game and restores your screen when it
 * closes; the game's next render() redraws as usual. Not for use mid-multiplayer:
 * it blocks the per-frame ESP-NOW service for as long as the keyboard is open. */
bool osTextInput(const char *title, char *buf, uint16_t max);

/* ---- multiplayer (ESP-NOW local wireless, up to MP_MAX_PLAYERS consoles) ----
 * The game owns the lobby UI and drives the session; the OS owns discovery, the
 * peer table, player indices, heartbeat liveness and the message mailboxes. A
 * host advertises THIS running game; a joiner only discovers hosts of the same
 * game. Send/receive are non-blocking mailbox ops — the OS exchanges them with
 * the ESP once per frame at the inter-frame seam. See docu/espnow.md. */

/* This console's role, or MP_ROLE_NONE when no session is active. */
MpRole mpGetRole(void);

/* Begin hosting: advertise this game and accept joiners (you become index 0). */
MpStatus mpHostStart(void);

/* Begin discovery: listen for hosts of this game (call mpScanHosts to read them). */
MpStatus mpJoinStart(void);

/* Copy up to `max` discovered hosts into `out`; returns the count (or -1). Only
 * valid after mpJoinStart(); the list refreshes as beacons arrive each frame. */
int mpScanHosts(MpHostInfo *out, int max);

/* Join a scanned host by its MpHostInfo.handle. On success the role becomes
 * MP_ROLE_CLIENT and mpGetSelfIndex()/mpGetPlayerCount() reflect the roster. */
MpStatus mpJoin(uint8_t host_handle);

/* End the session and release the ESP-NOW link. Safe to call when not in a
 * session. The OS also calls this automatically when the game exits or crashes. */
void mpStop(void);

/* This console's player index (0 = host). Meaningful once connected. */
uint8_t mpGetSelfIndex(void);

/* Number of players currently in the session (1..MP_MAX_PLAYERS). */
uint8_t mpGetPlayerCount(void);

/* Whether player `index` is present and within its heartbeat window. */
bool mpIsConnected(uint8_t index);

/* Copy player `index`'s display name into `buf` (NUL-terminated). Returns length. */
int mpGetName(uint8_t index, char *buf, int max);

/* Copy this console's own display name into `buf`. Returns length. */
int mpGetSelfName(char *buf, int max);

/* Queue `len` (<= MP_MSG_MAX) bytes for player `dst_index` (or MP_BROADCAST_INDEX
 * for everyone). Non-blocking; sent at the next inter-frame service. */
bool mpSend(uint8_t dst_index, const void *data, uint16_t len);

/* Pop one received message into `data` (up to `max`), storing the sender's index
 * in *src_index_out. Returns the byte count, or 0 when the mailbox is empty. */
int mpReceive(uint8_t *src_index_out, void *data, uint16_t max);

/* ---- lifecycle ---- */
/* Return control to the console. Does not return. A game may call this to quit;
 * the game's _game_start also calls it after main() returns. */
void gameExit(void) __attribute__((noreturn));

#endif /* __CONSOLE_SYSCALLS_H */
