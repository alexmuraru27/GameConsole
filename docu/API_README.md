# GameConsole: Console & API Documentation

## Overview
GameConsole is a modular embedded game console platform based on the STM32F407 microcontroller. It provides a hardware abstraction layer and a rich API for game development, including graphics rendering, joystick input, sound, and asset management.

---

## Table of Contents
- [Architecture](#architecture)
- [Main Loop](#main-loop)
- [API Overview](#api-overview)
  - [System Time](#system-time)
  - [Sound (Buzzer)](#sound-buzzer)
  - [Joystick](#joystick)
  - [Renderer](#renderer)
  - [Asset Loader](#asset-loader)
  - [Logging](#logging)
- [Module Internals](#module-internals)
  - [System/Startup](#systemstartup)
  - [Renderer](#renderer-internal)
  - [Joystick](#joystick-internal)
  - [Buzzer](#buzzer-internal)
  - [Asset Loader](#asset-loader-internal)
  - [SDIO & Filesystem](#sdio--filesystem)
  - [Interrupts & Syscalls](#interrupts--syscalls)
- [Initialization Sequence](#initialization-sequence)
- [Example Usage](#example-usage)
- [File Structure](#file-structure)
- [See Also](#see-also)
- [License](#license)

---

## Architecture
- **MCU:** STM32F407
- **Main Components:**
  - Renderer (graphics)
  - Joystick (input)
  - Buzzer (sound)
  - Asset Loader (game data)
  - SDIO (storage)
  - Network (ESP-01 on USART1; SWO/ITM carries debug trace)

All modules are initialized in `gameConsoleInit()`.

---

## Main Loop

There are two distinct loops: the console's own menu loop, and the callback loop the OS runs on a loaded game.

**Console menu loop** (`Console/Src/main.c`). After bring-up, `main()` runs the menu forever; selecting a game hands control to `gameLoaderLoadGame()`, which blocks for the game's lifetime and returns to the menu when the game exits or crashes:

```c
int main(void) {
    gameConsoleInit();
    mainMenuInit();
    while (1) {
        watchdogKick();
        mainMenuUpdate();   // navigate menus, launch/download/flash actions
        mainMenuRender();   // draw the current menu screen
    }
}
```

There is no `syncFrame()`/`MAIN_SYNC_FPS`; the console does not cap its menu loop.

**Game callback loop** (OS-owned, run uncapped). A game is not a `main()` — it exposes three callbacks (`init`, `update`, `render`) via `DECLARE_GAME_HEADER`. The kernel runs `_game_start` (zero `.bss`, run ctors), calls `init` once, then loops `update`/`render` back to back every frame, servicing its own inter-frame work (network, input latch) at the seam. The OS does **not** pace the frame rate — a game that wants a fixed rate paces itself with `delay()`:

- **update()**: reads input, advances game state.
- **render()**: submits sprites/text and calls `rendererRender()`.
- **frame pacing**: caller-side. GameXO holds ~60 FPS by sleeping out the rest of each frame budget (`TARGET_FRAME_MS = 1000/60`, `paceFrame()`); TestRenderer runs flat out to measure throughput. For frame-rate-independent movement a game scales by `getDeltaTimeUs()`.

---

## API Overview
There is no shared `ConsoleAPI` struct. A loaded game runs unprivileged and reaches the console through **SVC syscalls**: the contract is the strongly-typed stubs in `Shared/Syscall/console_syscalls.h` (each stub loads a syscall id from `Shared/Syscall/syscall_numbers.h` into `r12`, args into `r0-r3`, and executes `svc #0`). The console dispatches them in `Console/Src/Kernel/syscall.c`, validating pointer arguments before running the real console function. The ABI is versioned (`CONSOLE_ABI_VERSION`, currently `8`); the loader refuses a game built against a different value. Below is a summary of the main API groups and their functions.

### System Time
- `getSysTime()`: Returns system uptime in ms.
- `delay(ms)`: Busy-wait delay.
- `getDeltaTimeUs()`: Microseconds between the previous two `update()` calls (for frame-rate-independent movement).

### Random
- `getRandom()`: A fresh 32-bit value from the STM32's hardware true-RNG — seed-free, statistically solid. For a range use `getRandom() % n`.


### Sound (Buzzer)
- `buzzerGetMaxTracks()`: Number of software synth tracks.
- `buzzerPlay(track, loop, notes, count)`: Play a `count`-note sequence (interleaved `{freq_hz, duration_ms}` pairs) on `track`, optionally looped.
- `buzzerPlayWithFlag(track, loop, notes, count, on_done_flag)`: As above, and sets `*on_done_flag` when the sequence finishes.
- `buzzerPause(track)`, `buzzerResume(track)`, `buzzerStop(track)`, `buzzerStopAll()`: Control playback.

### Input
The whole pad is read in one trap:
- `inputGetState(InputState *out)`: fills an `InputState` with the buttons and analog axes for this frame.
  - Each button is an `InputButtonState { bool held; bool pressed; bool released; }` — `r_up/r_right/r_down/r_left`, `l_up/l_right/l_down/l_left`, `special1`, `special2`. Read `in.special1.pressed` directly, no bit math. `pressed`/`released` are the rising/falling edges since the previous frame — use `pressed` for one-shot actions (btnp), `held` for continuous movement.
  - `left_x/left_y/right_x/right_y`: raw analog axes, centered + deadzoned to **−512..+512** (up / right positive), ready for racers / twin-stick aiming with no per-game calibration.
- Edges are latched once per frame by the OS at the frame seam, so reading twice in a frame is stable. Types live in `Shared/Api/joystick_interface.h`.

### Renderer
A scanline **sprite compositor** (full deep-dive in [`renderer.md`](renderer.md)). Games do **not** init the renderer — the kernel resets it (drops layers, disables the background) when it launches a game, so a game starts from a clean slate. The frame-submission surface is reached through syscalls; the `Sprite`/`Layer`/`SpriteFlags` types live in the shared API (`Shared/Api/renderer_interface.h`). Loaded games draw the same way the console does:
- `rendererClear()`: drop all layers (start of a frame).
- `rendererSetBackground(color)`: RGB565 fill where no sprite draws.
- `rendererSubmitLayer(layer, sprites, count)`: submit a game-owned `Sprite[]` for `LAYER_BG/FG/UI`.
- `rendererRender()`: Sort sprites by `z`, bin them into 16-line chunks, composite back-to-front, and DMA the frame to the ILI9341.
- `rendererDrawText(layer, x, y, z, font, scale, color, text)`: expand a string into glyph sprites console-side in one syscall (folds into the layer's z-sort; re-issue every frame).
- `rendererGetWidthPixels()` / `rendererGetHeightPixels()`: panel size (320x240).
- `rendererSystemColor(index)`: map a Pixel Forge system-palette index (0-63) to RGB565 — used to turn a loaded `GfxAsset`'s index palette into a render-ready RGB565 palette.

### Asset Loader
Serves assets out of the `.pak` bound to the running game. The game loader opens `<game>.pak` (same base name as the `.bin`) when the game starts and keeps it open for the game's lifetime, so a game never names a file — it just asks by id. Both calls return `ASSET_LOADER_RET_OK` (`0`) on success; see `Console/Inc/Loader/asset_loader.h` for the other return codes.
- `assetLoaderGetAssetMetadata(asset_id, *metadata)`: Look up an asset's `id`, `size`, and `crc32` without loading it (e.g. to size a buffer).
- `assetLoaderGetAssetData(asset_id, *buffer, size)`: Copy the asset blob into a caller buffer (must be ≥ the asset size) and verify its CRC32.

### Settings
Persistent per-game storage in the EEPROM, keyed by the running game's `.bin` name. No opt-in is needed: the loader binds a save slot for every game (the 2 KB slot is allocated lazily on the first `settingsWrite`), so the game never names a file. All three return a `SettingsStorageStatus` (`0` = `OK`; `NOT_FOUND` on first run, `STORAGE_FULL` when no slot is free, etc. — see `Shared/Api/settings_interface.h`).
- `settingsWrite(version, *data, size)`: Persist up to `SETTINGS_GAME_MAX_DATA` (2042) bytes tagged with a struct version.
- `settingsRead(expected_version, *buffer, *size)`: Load the save; `*size` is in=capacity / out=actual, and a version bump yields `VERSION_MISMATCH`.
- `settingsClear()`: Delete this game's save.

### Fonts
The console's bitmap fonts (3x5, 5x5, 8x8) live once in console flash and are drawn by games through the API — no game ships glyph data. Types (`Font`, `FontSize`) are in `Shared/Api/font_interface.h`.
- `fontGlyphW(size)` / `fontGlyphH(size)`: glyph dimensions for `FONT_3x5` / `FONT_5x5` / `FONT_8x8`.
- `fontGet(ch, size, *pixels)`: pointer to a glyph's 2bpp pixels — drops straight into a `Sprite` (pair it with your own RGB565 palette for color).
- `fontSize(size, scale)`: byte size of a glyph scaled by `scale` (to size a scratch buffer).
- `fontScale(ch, size, scale, *dst)`: nearest-neighbour scale a glyph into `dst`.

### Logging
- `log(fmt, ...)`: printf-style line routed to the console's SWO/ITM trace, tagged `[GAME]`. This is the **only reliable game-side logging path** — a game's own `printf` has no backing `_write`. Console firmware logs with the `LOGGER_LOG_{ERROR,WARN,INFO,DEBUG}(channel, ...)` macros instead; see the Logging subsystem in `CLAUDE.md`.

---

## Module Internals

### System/Startup
- **SystemInit**: Configures system clock and core peripherals.
- **syscalls.c**: Implements minimal newlib system calls for embedded C runtime (e.g., `_write`, `_read`, `_exit`).
- **stm32f4xx_it.c**: Defines interrupt handlers for faults and system exceptions (NMI, HardFault, etc.).

### Renderer (renderer.c/h)
- Scanline **sprite compositor** for the ILI9341 320×240 display. Games describe a frame as per-layer arrays of `Sprite` (indexed-color 2bpp/4bpp tiles with `x/y/w/h`, draw order `z`, flags, palette); the renderer z-sorts them, bins them into 16-line chunks, composites back-to-front, and DMA-streams each chunk to the panel while the next composites.
- 64-color system palette (RGB565). Three layers: `LAYER_BG`, `LAYER_FG`, `LAYER_UI`.
- Compiled `-O3` even in debug builds (it is the per-frame hot path) with a scanline hot-path compositor — see [`renderer.md`](renderer.md) for the full pipeline.
- Public functions:
  - `rendererInit()`: Build compositor tables, clear scanline buffers. **Console-owned, boot-time only** — called once from `devicesInit()`, not exposed to games.
  - `rendererResetState()`: Drop all layers + disable the background. The kernel calls it when it launches a game, so a game inherits a clean slate (not the menu's background).
  - `rendererClear()`: Drop all layers at the start of a frame.
  - `rendererSetBackground(color)`: RGB565 fill for pixels no sprite covers.
  - `rendererSubmitLayer(layer, sprites, count)`: Borrow a layer's sprite array for the frame.
  - `rendererRender()`: Composite and present the frame.
  - `rendererGetWidthPixels()`, `rendererGetHeightPixels()`: Screen dimensions (320×240).

### Joystick (joystick.c/h)
- Reads analog and digital joystick inputs via ADC and GPIO.
- Debounces and interprets button presses and analog stick positions.
- Internal functions:
  - `joystickInit()`: Configure ADC and GPIO for input.
  - `joystickReadData()`: TIM7 ISR poll — debounce the button GPIOs into the shared state.
  - `joystickPollFrame()`: Latch one frame — snapshot buttons, derive pressed/released edges vs the previous latch, sample+deadzone the axes. Callers (kernel frame seam, menus) call it once per frame, then read `joystickGetState(InputState *)`.
  - `joystickGetState(InputState *)`: Copy the latched snapshot. This is the whole read API — there are no per-button/per-axis getters.

### Buzzer (buzzer.c/h)
- Controls sound output for effects and music. 5 tracks over one PWM voice.
- Internal functions:
  - `buzzerInit()`: Set up timers and output pins.
  - `buzzerPlay()`, `buzzerPause()`, `buzzerResume()`, `buzzerStop()`: Playback control.
  - `buzzerPlayWithFlag()`: Play sound and trigger callback on completion.
  - `buzzerRefreshOutput()` (static): the single arbitration point — drives the PWM from the highest track that is *audibly* playing, falling through paused/rested/muted tracks instead of silencing lower ones.

### Asset Loader (asset_loader.c/h)
- Serves game assets (tiles, sounds, etc.) from the `.pak` bound to the running game on the SD card.
- The game loader calls `assetLoaderOpenPak()`/`assetLoaderClosePak()` to bind/unbind `<game>.pak` around the game's run; the handle stays open so reads seek straight into the file.
- Internal functions:
  - `assetLoaderOpenPak()` / `assetLoaderClosePak()` / `assetLoaderIsPakOpen()`: Bind, unbind, and query the active pak (driven by the game loader).
  - `assetLoaderGetAssetMetadata()`: Look up an asset's id/size/crc32 by id.
  - `assetLoaderGetAssetData()`: Copy an asset blob into a caller buffer and verify its CRC32.

### SDIO & Filesystem (sdio.c/h, ff.c/h)
- Manages SD card interface and FAT filesystem.
- Internal functions:
  - `f_mount()`: Mount filesystem.
  - `sdioInit()`: Initialize SDIO hardware.
  - `ff_*()`: FATFS file operations.

### Interrupts & Syscalls
- **stm32f4xx_it.c**: All core and peripheral interrupt handlers.
- **syscalls.c**: Minimal C runtime support for embedded environment.

---

## Initialization Sequence
`gameConsoleInit()` (`Console/Src/game_console.c`) brings the console up in phases (each grouped so the persisted mute/brightness can be applied before any boot sound):

1. **coreInit()**: SWO trace, fault decode (`faultsInit`), the SVC/PendSV syscall trap (`syscallInit`), GPIO, timers, buzzer, backlight PWM.
2. **storageInit()**: I2C, external EEPROM, settings store (`settingsStorageInit`).
3. **applyConsoleSettings()**: read the persisted `ConsoleSettings` and apply mute (`buzzerSetMute`) and backlight brightness (`backlightSetBrightness`) — **before** the boot beeps, so a muted console boots silent and a dimmed one comes up dim.
4. **peripheralsInit()**: DMA, USART, network (ESP-01), ADC, RNG.
5. **devicesInit()**: ILI9341 display, renderer (`rendererInit`), joystick, SD/FatFs mount.
6. **osServicesSetTextInput(keyboardModal)**: wire the on-screen keyboard into the kernel's `osTextInput` service.
7. **mpuInit()**: arm MPU confinement before any game can run.
8. **playBootSong()**, then **watchdogInit()**: chime and arm the last-resort reset backstop.

There is no API-exposure step — games reach the console through SVC syscalls, not a shared struct.

---

## Example Usage
Read input (one trap, edges + analog):
```c
void update() {
    InputState in;
    inputGetState(&in);
    if (in.special1.pressed) { /* fire once on the press */ }
    if (in.r_right.held)     { /* move right while held */ }
    x += (in.left_x * speed) / 512;      /* analog, already deadzoned to -512..+512 */
}
```

---

## File Structure
- `Console/Src/`: Main source files
- `Console/Inc/`: Headers
- `Apps/GameXO/`: Reference game (also `Apps/TestRenderer/`, `Apps/TestBuzzer/`)
- `Shared/`, `Assets/` (`Music/`): Shared resources

---

## Subdirectory Structure and Module Overview

The `Console` directory is organized into several submodules, each with its own header (`.h`) and source (`.c`) files. Here is a breakdown of the main subdirectories and their responsibilities:

### Devices
- **Files:** `buzzer.[ch]`, `ILI9341.[ch]`, `joystick.[ch]`
- **Purpose:** Low-level drivers for hardware devices.
  - `buzzer`: Sound output and control.
  - `ILI9341`: LCD display driver.
  - `joystick`: Input from analog/digital joysticks.

### Renderer
- **Files:** `renderer.[ch]`
- **Purpose:** Scanline-based graphics engine for the ILI9341 320×240 display.

### Loader
- **Files:** `asset_loader.[ch]`, `game_loader.[ch]`, `loader.[ch]`
- **Purpose:** Asset and game loading from storage. Handles asset metadata, data transfer, and game selection.

### Peripherals
- **Files:** `adc.[ch]`, `dma.[ch]`, `gpio.[ch]`, `sdio.[ch]`, `sysclock.[ch]`, `timer.[ch]`, `usart.[ch]`, `rng.[ch]`, `watchdog.[ch]`
- **Purpose:** Abstraction for microcontroller peripherals. Used by higher-level modules for hardware access. `gpio.c` is table-driven (a static pin-config table configures every pin at init); code drives individual pins through the `gpio.h` accessors — `gpioSetPin`/`gpioClearPin`/`gpioWritePin`/`gpioReadPin`/`gpioSetPinMode` — using named pin constants (e.g. `GPIO_DISPLAY_RST`) rather than raw port/pin literals.

### SettingsStorage
- **Files:** `settings_storage.c` (public key/value API), `settings_directory.c` (the game-directory / entity read-write mechanism), `console_settings_storage.c` (the typed console-settings blob); layout in `Console/Inc/SettingsStorage/settings_layout.h`.
- **Purpose:** CRC-16-protected EEPROM store — the flat directory + game save slots and the console settings entity.

### Kernel
- **Files:** `syscall.c` (SVC dispatch), `syscall_validate.c` (pointer/range validators for game arguments), `scheduler.c` (context switch / game loop), `mpu.c` (game confinement), `faults.c` + `crash_report.c` (fault decode + persisted crash record), `os_services.c` (kernel→UI service hooks).
- **Purpose:** Game isolation — games run unprivileged and trap into the console through SVC. See [`kernel.md`](kernel.md).

### Multiplayer
- **Files:** `mp_session.c` (role/peer table, discovery, join handshake, heartbeat, mailboxes), `mp_wire.c` (the wire-command codec).
- **Purpose:** ESP-NOW local multiplayer session logic (up to 4 consoles). See [`espnow.md`](espnow.md).

### FatFs
- **Files:** `diskio.[ch]`, `diskio_integration.[ch]`, `ff.[ch]`, `ffconf.h`, `ffunicode.c`
- **Purpose:** FAT filesystem support for SD card storage. Integrates with SDIO and provides file access to games and assets.

---

Each submodule is designed for separation of concerns, making the codebase modular and maintainable. For more details, see the respective header files in each subdirectory.

---

## See Also
- `game_console_api.h`: Full API definition
- `main.c`: Main application logic
- `game_console.c`: console bring-up (`gameConsoleInit`) and reboot (`gameConsoleReboot`)

---

## License
See `README.md` for license and authorship.

---

## Module Internals and Example Usages

### Devices
#### Buzzer
- **Purpose:** Sound output for effects and music.
- **Key Defines:** Musical note frequencies (e.g., `NOTE_C4`, `NOTE_D4`, ...).
- **Key Functions:**
  - `buzzerInit()`: Initialize hardware.
  - `buzzerPlay(track, loop, notes, count)`: Play a `count`-note sequence (interleaved `{freq_hz, duration_ms}` pairs), optionally looped.
  - `buzzerPause(track)`, `buzzerResume(track)`, `buzzerStop(track)`, `buzzerStopAll()`: Playback control.
- **Usage Example:**
  ```c
  static const uint16_t melody[] = { NOTE_C4, 200, NOTE_E4, 200, NOTE_G4, 400 };
  buzzerPlay(0, false, melody, sizeof(melody) / sizeof(uint16_t) / 2U);
  buzzerPause(0);
  buzzerResume(0);
  buzzerStop(0);
  ```

#### Joystick
- **Purpose:** Read analog and digital joystick input as one batched, edge-latched frame snapshot.
- **Key Types:**
  - `InputState`: one `InputButtonState { held; pressed; released; }` per button (`r_up … special2`) + `left_x/left_y/right_x/right_y` axes (−512..+512).
- **Key Functions:**
  - `joystickInit()`: Initialize ADC/GPIO.
  - `joystickPollFrame()`: Latch a frame (buttons + edges + axes). Call once per frame.
  - `joystickGetState(InputState *)`: Copy the latched snapshot. No per-button getters exist.
- **Usage Example:**
  ```c
  InputState in;
  joystickPollFrame();
  joystickGetState(&in);
  if (in.l_up.pressed) { /* move up */ }
  if (in.right_x > 256) { /* stick pushed right */ }
  ```

#### ILI9341 (LCD)
- **Purpose:** Low-level LCD display driver over the FSMC 16-bit parallel bus. Console-internal — games never call it directly; they draw through the renderer's sprite/text syscalls, and the renderer drives the panel.
- **Key Defines:** Color constants (e.g., `ILI9341_BLACK`, `ILI9341_WHITE`), `ILI9341_WIDTH` (320) / `ILI9341_HEIGHT` (240).
- **Key Functions (the whole public surface):**
  - `ili9341Init(rotation, window_width, window_height)`: Initialize the panel.
  - `ili9341FillScreen(color)`: Fill the whole screen with one RGB565 color.
  - `ili9341DrawScanlines(lines, lines_offset, array_size, data)`: Blit a horizontal band of composited RGB565 pixels — the renderer's chunk-DMA output path.

### Renderer
- **Purpose:** Scanline-based graphics engine for the ILI9341 display.
- **Key Functions:**
  - `rendererInit()`: Build compositor tables + scanline buffers (console boot only).
  - `rendererResetState()`: Drop layers + disable background (kernel calls it per game launch).
  - `rendererRender()`: Draw the current frame.
  - `rendererGetWidthPixels()`, `rendererGetHeightPixels()`: Screen dimensions (320×240).
- **Usage Example (game side — no init needed):**
  ```c
  rendererSetBackground(rendererSystemColor(0));
  rendererSubmitLayer(LAYER_BG, sprites, count);
  rendererRender();
  ```

### Loader
#### Asset Loader
- **Purpose:** Serve assets (tiles, sounds, etc.) from the game's bound `.pak` on the SD card.
- **Key Functions:**
  - `assetLoaderGetAssetMetadata(asset_id, *metadata)`: Get an asset's `id`/`size`/`crc32`.
  - `assetLoaderGetAssetData(asset_id, *buffer, buffer_size)`: Load (and CRC-verify) asset data into a buffer.
- **Usage Example:**
  ```c
  AssetMetaData meta;
  assetLoaderGetAssetMetadata(15, &meta);
  uint8_t buffer[meta.size];
  if (assetLoaderGetAssetData(15, buffer, sizeof(buffer)) == ASSET_LOADER_RET_OK)
  {
      // buffer holds the verified asset blob
  }
  ```

#### Game Loader
- **Purpose:** Load and manage game binaries.
- **Key Functions:**
  - `gameLoaderLoadGame(index)`: Load game by index.
  - `gameLoaderGetHeader(*header)`: Get game metadata.
  - `gameLoaderCloseGame()`: Unload game.
- **Usage Example:**
  ```c
  gameLoaderLoadGame(0);
  GameBinaryHeader header;
  gameLoaderGetHeader(&header);
  gameLoaderCloseGame();
  ```

#### Loader (File Loader)
- **Purpose:** File access and management for game binaries.
- **Key Functions:**
  - `loaderOpenFile(index)`: Open file by index.
  - `loaderGetFile()`: Get file handle.
  - `loaderCloseFile()`: Close file.
  - `loaderIsFileOpened()`: Check if file is open.
  - `loaderGetBinaryFilesNumberInDirectory()`: Count binaries.
  - `loaderGetFilenameByIndex(index, *out, *len)`: Get filename.
- **Usage Example:**
  ```c
  loaderOpenFile(0);
  FIL *file = loaderGetFile();
  loaderCloseFile();
  ```

# Renderer

The renderer is a scanline **sprite compositor** for the ILI9341 320×240 display: games describe a frame as per-layer `Sprite` lists, and the renderer z-sorts them, bins them into 16-line chunks, composites back-to-front (painter's algorithm), and DMA-streams each chunk to the panel while the next composites. The complete ground-up explanation — frame pipeline and the inner pixel loop — lives in **[`renderer.md`](renderer.md)**, which is the source of truth.

Quick reference:
- **Screen:** 320×240, RGB565
- **System palette:** 64 fixed colors (below)
- **Layers:** `LAYER_BG`, `LAYER_FG`, `LAYER_UI`
- **Pixel formats:** 2bpp / 4bpp planar tiles; slot 0 transparent unless `SPRITE_OPAQUE`; `SPRITE_FLIP_H/V`
- **Public API:** `rendererInit` (console boot only), `rendererResetState` (kernel, per game launch), `rendererClear`, `rendererSetBackground`, `rendererSubmitLayer`, `rendererDrawText`, `rendererRender`, `rendererGetWidthPixels`, `rendererGetHeightPixels`, `rendererSystemColor`

## System Palette
![system_palette](system_palette.png)

