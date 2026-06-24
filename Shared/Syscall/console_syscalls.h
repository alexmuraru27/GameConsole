#ifndef __CONSOLE_SYSCALLS_H
#define __CONSOLE_SYSCALLS_H

#include <stdint.h>
#include "stdbool.h"
#include "renderer_interface.h"
#include "font_interface.h"
#include "asset_interface.h"
#include "settings_interface.h"
#include "joystick_interface.h"

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

/* ---- buzzer ---- */
uint8_t buzzerGetMaxTracks(void);
bool buzzerPlay(uint8_t track_number, bool is_looped, const uint16_t *notes_data, uint16_t notes_number);
bool buzzerPlayWithFlag(uint8_t track_number, bool is_looped, const uint16_t *notes_data, uint16_t notes_number, bool *on_done_flag);
bool buzzerPause(uint8_t track_number);
bool buzzerResume(uint8_t track_number);
bool buzzerStop(uint8_t track_number);
void buzzerStopAll(void);

/* ---- joysticks ---- */
bool joystickGetRBtnUp(void);
bool joystickGetRBtnRight(void);
bool joystickGetRBtnDown(void);
bool joystickGetRBtnLeft(void);
bool joystickGetLBtnUp(void);
bool joystickGetLBtnRight(void);
bool joystickGetLBtnDown(void);
bool joystickGetLBtnLeft(void);
bool joystickGetSpecialBtn1(void);
bool joystickGetSpecialBtn2(void);
JoystickAxisState joystickGetRAnalogY(void);
JoystickAxisState joystickGetRAnalogX(void);
JoystickAxisState joystickGetLAnalogY(void);
JoystickAxisState joystickGetLAnalogX(void);
bool joystickIsAnyButtonPressed(void);

/* ---- renderer ---- */
void rendererInit(void);
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

/* ---- lifecycle ---- */
/* Return control to the console. Does not return. A game may call this to quit;
 * the game's _game_start also calls it after main() returns. */
void gameExit(void) __attribute__((noreturn));

#endif /* __CONSOLE_SYSCALLS_H */
