# Creating a Game + Console API

Everything you need to build a game for the console: the project setup, the boot
template, and the full API a game can call.

A game is a standalone `.bin` loaded from the SD card and run **unprivileged**,
isolated from the console by the MPU. It reaches the console only through **SVC
syscalls** — there is no shared struct and no direct linkage to console code. The
isolation internals are in [`../kernel.md`](../kernel.md); this is the how-to.

The **OS owns the game loop**. A game is not a `main()` that runs forever — it is
three callbacks the console calls: `init` once, then `update` then `render` every
frame, back to back. The console runs the loop **uncapped** and does its own work
(the WiFi link, etc.) between frames, so a game must return promptly from each
callback rather than spin. The OS does not cap the frame rate; a game that wants a
fixed rate paces itself by calling `delay()` once per frame (e.g. GameXO holds
~60 FPS). Game state lives in globals (it persists across calls); locals do not.

---

## Quick start

1. **Linker script** — set `LDSCRIPT = $(REPO_ROOT)/linker/app.ld` in your Makefile
   and pass `-L$(REPO_ROOT)/linker` (so its `INCLUDE "common.ld"` resolves). It places
   the game at its `GAME_RAM` addresses and reserves the CCM asset arena. The image is
   RAM-resident, so there is no LMA copy.

2. **Link the syscall stubs + headers** — the API is a set of typed C functions
   whose bodies are tiny SVC trampolines. Compile the shared stub library into your
   game and add the include paths:

   ```make
   C_SOURCES   += ../Shared/Syscall/console_syscalls.c
   C_INCLUDES  += -I../Shared/Api -I../Shared/Syscall
   ```

3. **No `startup.s`** — a game has no vector table and no hand-written entry. The
   12→28-byte binary header is emitted by the `DECLARE_GAME_HEADER` macro (step 4),
   and the C-runtime bootstrap (`_game_start`: zero `.bss` → run ctors) plus the
   callback return trampoline (`_game_return`) come from the shared
   `console_syscalls.c` linked in step 2. You write only your three callbacks.

4. **Write the callbacks + the header** — include the umbrella header, write
   `init`/`update`/`render`, and declare the header at file scope:

   ```c
   #include "game_console_api.h"

   static void gameInit(void)
   {
       gameLog("Hello from my game!");   // game-local printf() is unreliable; use gameLog
       rendererInit();
       // load assets, set up state (in globals — locals don't persist between calls)
   }

   static void gameUpdate(void)
   {
       // read input, advance game logic
       if (joystickGetSpecialBtn2())
       {
           gameExit();   // Special Button 2 returns to the console (by convention)
       }
   }

   static void gameRender(void)
   {
       // submit sprites + rendererRender()
   }

   DECLARE_GAME_HEADER(gameInit, gameUpdate, gameRender);
   ```

   There is **no** runtime handshake — the loader already validated the binary's
   magic and ABI version before running it. The OS calls `gameInit` once, then loops
   `gameUpdate`/`gameRender`; `gameExit()` (or a crash) ends the session.

5. **Build & deploy** — build with the Makefile, then copy `<game>.bin` and its
   matching `<game>.pak` (same base name) to the SD card root. `make deploy` copies
   the built `.bin` to the SD mount point set in `common.mk`.

---

## What a game may touch

Running unprivileged, the MPU confines a game to exactly two regions; everything
else (console RAM, peripherals, flash) traps as a **recoverable** fault that drops
the player back to the menu with a "crashed" banner.

| Region | Size | Use |
| ------ | ---- | --- |
| `GAME_RAM` (`0x20018000`) | 32 KB | your code + rodata + data + bss + stack |
| CCM asset arena (`0x10000000`) | 64 KB | scratch buffers for `.pak`-loaded assets (data only — not executable) |

Keep total code+data+stack under 32 KB. Stream large assets through the CCM arena
rather than holding them all resident — its bounds are exposed by the ConsoleAPI as
`ASSET_ARENA_START` / `ASSET_ARENA_END` / `ASSET_ARENA_SIZE` (no need to declare the
linker symbols yourself). See [`../memory.md`](../memory.md).

---

## The binary header (`DECLARE_GAME_HEADER`)

The header is the first 28 bytes of the `.bin`, emitted by the macro straight from
the shared ABI constants and your callback names (so nothing is duplicated):

```
   offset  0:  magic         0x47414D45  "GAME"
   offset  4:  abi_version   CONSOLE_ABI_VERSION   (loader refuses a mismatch)
   offset  8:  entry_point   &_game_start    one-time C-runtime bootstrap   ┐ filled by
   offset 12:  frame_return  &_game_return   callback return trampoline      ┘ the macro
   offset 16:  init          your gameInit                                   ┐ your three
   offset 20:  update        your gameUpdate                                  │ callbacks
   offset 24:  render        your gameRender                                 ┘
```

The macro fills `entry_point`/`frame_return` from the shared syscall library
(you never name them). The OS invokes `entry_point` once to set up the C runtime,
then `init`, then loops `update`/`render`. Each callback runs unprivileged and
returns to the OS through `frame_return` (an SVC) — a game cannot return into
console code directly. `gameExit()` ends the session early.

---

## API reference

Include `game_console_api.h` (it pulls in the typed prototypes and the shared data
types). Every function below is a direct call; the SVC trap is hidden inside it.
The console range-checks every pointer you pass, so a bad pointer is rejected (or,
for an out-of-bounds access you make yourself, faults and recovers) rather than
corrupting the console. For module internals see [`../API_README.md`](../API_README.md).

### System time
```c
uint32_t getSysTime(void);              // milliseconds since boot
void     delay(uint32_t ms);            // busy-wait
```

### Buzzer (5-track synth; notes are interleaved uint16 {freq_hz, ms} pairs)
```c
uint8_t buzzerGetMaxTracks(void);
bool    buzzerPlay(uint8_t track, bool loop, const uint16_t *notes, uint16_t note_count);
bool    buzzerPlayWithFlag(uint8_t track, bool loop, const uint16_t *notes,
                           uint16_t note_count, bool *on_done_flag);
bool    buzzerPause(uint8_t track);
bool    buzzerResume(uint8_t track);
bool    buzzerStop(uint8_t track);
void    buzzerStopAll(void);
```
> Keep note data alive while it plays — the buzzer reads it from your RAM each tick.
> Call `buzzerStopAll()` before returning so nothing reads freed game memory.

### Joysticks (digital buttons + 3-state analog axes)
```c
bool joystickGetRBtnUp(void);    bool joystickGetLBtnUp(void);
bool joystickGetRBtnRight(void); bool joystickGetLBtnRight(void);
bool joystickGetRBtnDown(void);  bool joystickGetLBtnDown(void);
bool joystickGetRBtnLeft(void);  bool joystickGetLBtnLeft(void);
bool joystickGetSpecialBtn1(void);   // confirm / select (by convention)
bool joystickGetSpecialBtn2(void);   // back / quit (by convention)
JoystickAxisState joystickGetRAnalogX(void);  // Off / Negative / Positive
JoystickAxisState joystickGetRAnalogY(void);
JoystickAxisState joystickGetLAnalogX(void);
JoystickAxisState joystickGetLAnalogY(void);
bool joystickIsAnyButtonPressed(void);
```

### Renderer (320×240 RGB565 scanline sprite compositor)
```c
void     rendererInit(void);
void     rendererClear(void);
void     rendererSetBackground(uint16_t color);                 // RGB565
void     rendererSubmitLayer(Layer layer, const Sprite *sprites, uint16_t count);
void     rendererRender(void);                                  // composite + DMA to panel
uint16_t rendererGetWidthPixels(void);                          // 320
uint16_t rendererGetHeightPixels(void);                         // 240
uint16_t rendererSystemColor(uint8_t system_index);             // Pixel Forge palette → RGB565
```
Submit per-layer `Sprite` arrays (`LAYER_BG`/`LAYER_FG`/`LAYER_UI`), then
`rendererRender()`. A `Sprite`'s `pixels`/`palette` may point into your RAM, the
CCM arena, or console flash (font glyphs). Types live in `renderer_interface.h`;
the deep dive is [`../renderer.md`](../renderer.md).

### Assets (stream one blob at a time from the bound `<game>.pak`)
```c
uint8_t assetLoaderGetAssetMetadata(uint32_t asset_id, AssetMetaData *out);   // id/size/crc
uint8_t assetLoaderGetAssetData(uint32_t asset_id, uint8_t *buffer, uint32_t buffer_size);
```
Both return `0` (`ASSET_LOADER_RET_OK`) on success. `assetLoaderGetAssetData`
copies the blob into your buffer and verifies its CRC32. Carve `buffer` from the
CCM arena. Asset IDs come from the generated `<name>AssetEnum.h`.

```c
AssetMetaData meta;
assetLoaderGetAssetMetadata(ASSET_ID_HERO, &meta);
uint8_t buffer[meta.size];
if (assetLoaderGetAssetData(ASSET_ID_HERO, buffer, sizeof(buffer)) == 0U)
{
    // buffer now holds the CRC-verified asset blob
}
```

### Settings (one save slot per game, keyed by the `.bin` name)
```c
uint8_t settingsRead(uint16_t expected_version, uint8_t *buffer, uint16_t *size);  // size in=cap/out=actual
uint8_t settingsWrite(uint16_t version, const uint8_t *data, uint16_t size);       // size ≤ 2042
uint8_t settingsClear(void);
```
Return `SettingsStorageStatus` (0 == OK). The slot is bound automatically and
created on first write; CRC-16 protected.

### Fonts (built-in console fonts; no glyphs shipped in the game)
```c
uint16_t fontGlyphW(FontSize size);                         // glyph width  (FONT_3x5/5x5/8x8)
uint16_t fontGlyphH(FontSize size);                         // glyph height
void     fontGet(uint8_t ch, FontSize size, const uint8_t **pixels);   // glyph → drop into a Sprite
uint16_t fontSize(FontSize size, uint8_t scale);            // bytes a scaled glyph needs
void     fontScale(uint8_t ch, FontSize size, uint8_t scale, uint8_t *dst);  // scale into your buffer
```

### Logging & lifecycle
```c
void gameLog(const char *fmt, ...);   // info log on the GAME channel over SWO (printf-style)
void gameExit(void);                  // return to the console (does not return)
```
`gameLog` is formatted in your binary and handed over as bytes, so no format
string reaches the console. Call `gameExit()` from `update` (by convention on
Special Button 2) to return to the console — there is no `main()` to fall out of.

---

## Reference

- [Kernel / game isolation](../kernel.md) — privilege model, the syscall ABI, MPU protection, fault recovery
- [ConsoleAPI module internals](../API_README.md)
- [Memory layout](../memory.md) — SRAM/CCM map, game binary layout
- [Example game source](../../Apps/GameXO/) — the reference implementation
- [Renderer benchmark game](../../Apps/TestRenderer/) — an endless scroller that sweeps its sprite load 50–100% of the renderer budget with a live FPS overlay; streams its tiles from a `.pak` into both the CCM arena and a GAME_RAM buffer
