# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Requires `arm-none-eabi-gcc` toolchain and `openocd` on PATH.

```bash
make all            # Build Console firmware + GameXO game + Shared (header check)
make flash          # Flash Console firmware via OpenOCD/STLink
make flashswo       # Flash and start SWO trace output via tools/scripts/swo.sh
make deploy         # Copy GameXO.bin to SD card at /mnt/sd (set SD_CARD_PATH in common.mk)
make clean          # Remove all build artifacts

make -C Console all # Build only the console firmware
make -C GameXO all  # Build only the GameXO game binary
```

`DEBUG=1` is on by default in `common.mk`. Add `GCC_PATH=<path>` if the ARM toolchain isn't on PATH.

## Architecture

This is an embedded game console platform targeting the STM32F407VET6. Two separate binaries are built and linked independently:

### Console (firmware, flashed to MCU)
Lives in `Console/`. Runs from flash at boot, initializes all hardware, shows the main menu, loads games from SD card, and exposes the **ConsoleAPI** to loaded games.

**Boot sequence** (`startup.s` → `main.c`):
1. `Reset_Handler`: copies `.data` from flash to RAM, zeros `.bss` and `.ccmram`, calls `__libc_init_array`, then `main()`
2. `SystemInit()` → `systemClockConfig()`: HSE 8MHz → PLL → 168MHz SYSCLK, SysTick at 1ms
3. `gameConsoleInit()`: `peripheralsInit()` (GPIO, timers, DMA, ADC, I2C, SWO, USART) → `devicesInit()` (ILI9341 display, renderer, joystick, EEPROM, FatFs mount, settings) → `gameConsoleExposeApi()`
4. `main()` then runs `init()` → loop of `update()` → `render()`

The production path is the main menu: `main()` calls `mainMenuInit()` then loops `mainMenuUpdate()` / `mainMenuRender()`. The menu (`Console/Src/MainMenu/main_menu.c`) is a centered-hero game picker — it lists the SD-card `.bin` games, browses with up/down, and launches the highlighted one with Special Button 1; `gameLoaderLoadGame()` blocks for the game's lifetime and the menu re-inits when it returns. The `renderer_testing.c` perf harness is still in the tree (and its source still compiles) but is only wired in when `RENDERER_TESTING` is defined; it is off by default.

**HardFault handler**: prints PC, LR, PSR, HFSR, CFSR, MMFAR, BFAR via `printf()`/SWO before halting — useful for debugging crashes.

### Games (`.bin` files, loaded at runtime from SD card)
`GameXO/` is the reference game. Game binaries are loaded into RAM and executed by the console OS. Each game:
- Uses `../game.ld` as its linker script
- Calls `DECLARE_API_HEADER_PTR(api_hdr_ptr)` to access all console functions via the shared RAM API struct
- Calls `DECLARE_GAME_BINARY_HEADER(entry_func)` at file scope (places a `GameBinaryHeader` into `.game_header` section for the loader)
- Returns to the console OS when Special Button 2 is pressed

**Game loading** (`game_loader.c`): reads the `GameBinaryHeader` from the `.bin` file, zeros the BSS region, copies text/rodata/data sections into their target RAM addresses using the offset `region_addr - header_start`, then jumps to `entry_point`.

**GameXO architecture**: a finite state machine (`CHOOSE → PLAYING → END`) in `game_state_manager.c` drives input, the AI opponent (minimax with alpha-beta pruning in `tic_tac_toe_logic.c`), and rendering. `game_assets.c` wraps the ConsoleAPI: it streams the packed graphics (X/O marks, cursor) from `GameXO.pak` into the CCM asset arena and builds `Sprite`s, plays packed buzzer tracks, and draws text with the console fonts. The board grid, marks, and cursor are drawn through the renderer's sprite API; all text uses the exposed console fonts (no glyphs are packed). Assets are authored with the in-repo tools under `GameXO/Assets/{Graphics,Music}` (per `GameXO/Assets/manifest.yaml`) and the GameXO Makefile runs the packer to emit `GameXO.pak` + `GameXOAssetEnum.h`.

### Shared (`Shared/`)
Header-only. Contains `Shared/Api/` — the public game API (`function_interface.h`, `header_interface.h`, `asset_interface.h`). Both Console and games include these headers.

### Memory Layout
See `docu/memory.md` for the full map. Quick reference:

| Region         | Origin      | Size | Purpose |
| -------------- | ----------- | ---- | ------- |
| SHARED_RAM     | 0x20000000  |   2K | ConsoleAPIHeader |
| CONSOLE_RAM    | 0x20000800  |  82K | Console firmware `.data`/`.bss`/stack, DMA buffers, renderer (SRAM) |
| GAME_RAM       | 0x20015000  |  44K | Game binary: code + rodata + data + bss + stack (SRAM, executable) |
| GAME_RAM_ASSET | 0x10000000  |  64K | Runtime asset arena for `.pak` loads — **CCM** (D-bus only, not executable) |
| CONSOLE_FLASH  | 0x08000000  | 512K | Console firmware |

CCM is D-bus only on the STM32F4, so **game code must run from SRAM (`GAME_RAM`), never CCM** — CCM holds the data-only asset arena. Games ship as two files on SD: `GameXO.bin` (header + code + rodata + data, all targeting SRAM) + `GameXO.pak` (assets, streamed lazily by ID into the CCM arena). The packer (`tools/packer/`) bundles assets and emits a C enum header. See `docu/memory.md` for the authoritative map.

### EEPROM
AT24C512 (64KB) on I2C1. `settings_storage.c` splits it into a 16KB console partition (system header + 48-entry game directory + console settings blob) and a 48KB games partition (48 × 1KB save slots). Game saves are keyed by `.bin` name, CRC-16-CCITT protected, auto-cleaned on init. See `docu/memory.md`.
The Console firmware writes a `ConsoleAPIHeader` struct (magic `0xDEADBEEF`, version 1) into `SHARED_RAM` at startup (`gameConsoleExposeApi()`). The struct contains a `ConsoleAPI` table of function pointers for: systime, buzzer, joystick, renderer, and asset loading. Games access it via the linker symbol `__game_console_api_start`. This is how games call renderer, joystick, buzzer, and asset functions — they never link against Console code directly.

## Documentation

Deep-dive docs live in `docu/`:
- `docu/API_README.md` — ConsoleAPI surface and module internals
- `docu/renderer.md` — the renderer, ground-up: frame pipeline, the hot loop, and the 24→76 FPS optimization journey
- `docu/memory.md` — authoritative SRAM/CCM/flash map, game binary layout, EEPROM layout, linker scripts
- `docu/HW.md` — Hardware schematics and full pinout
- `docu/game_creation.md` — Guide for creating new games for the platform

## Subsystems

### SD Card Stack
Four layers: **FatFs** (`ff.c`) → **disk I/O** (`diskio.c`) → **low-level driver** (`diskio_integration.c`) → **SDIO peripheral** (`sdio.c`). `sdInit()` sends CMD0/CMD8/ACMD41/CMD2/CMD3/CMD7/CMD16, switches to 4-bit bus mode, and sets the clock to ~12MHz. `disk_write()` is a **stub** (returns `RES_PARERR`) — the SD card is read-only. Card detect is on GPIO PD3 (active-low, `sdCardPresent()` in `gpio.c`).

### Renderer
Scanline **sprite compositor** (`Console/Src/Renderer/renderer.c`) — full deep-dive in `docu/renderer.md`.
- **Screen**: 320×240, RGB565, fed to the ILI9341 over FSMC.
- **Model**: games submit per-layer arrays of `Sprite` (indexed-color image + `x/y/w/h`, draw order `z`, `flags`, `palette`) across three layers — `LAYER_BG`, `LAYER_FG`, `LAYER_UI`. Pixels are 2bpp or 4bpp planar tiles (slot 0 transparent unless `SPRITE_OPAQUE`); `SPRITE_FLIP_H/V` flip in place.
- **Pipeline** (`rendererRender()`): counting-sort sprites by `z` within each layer → bin them into 16-scanline chunks → composite each chunk back-to-front (painter's algorithm) into one of two scanline buffers → DMA the chunk to the panel while the next composites. Optional per-chunk background fill (`rendererSetBackground`).
- **Hot-path tricks**: opaque two-pixels-per-store blitter, a packed `s_pair` palette LUT, a per-frame decoded-tile cache, and a "byte is fully opaque" table — together taking a busy screen from **24 → 76 FPS** (see `docu/renderer.md` §8 and the `renderer-perf-findings` notes).
- **Public API**: `rendererInit` / `rendererClear` / `rendererSetBackground` / `rendererSubmitLayer` / `rendererRender`, plus `rendererGetWidthPixels` / `rendererGetHeightPixels` and `rendererSystemColor` (Pixel Forge system-palette index → RGB565). The whole surface is exposed to games through the `ConsoleAPI`; the `Sprite`/`Layer`/`SpriteFlags` types live in `Shared/Api/renderer_interface.h`. The renderer is compiled `-O3` even in debug builds (`#pragma GCC optimize`) since it is the per-frame hot path.
- **RAM cost**: the scanline buffers + per-chunk bins + z-sort lists + tile cache dominate `CONSOLE_RAM` (~41 KB); `tools/memory_analysis` reports the live budget.

### Joystick
Two analog joysticks with digital d-pads + 2 special buttons. Polled by TIM7 ISR every 50ms. Digital buttons: 5ms debounce on each of 10 GPIO pins (active-low with pull-ups). Analog axes: ADC1 channels 10–13 (PC0–PC3) in DMA circular mode, thresholded at 2048±1500 to produce 3-state output (Mid/Low/High) with 5ms debounce. Public API returns `bool` for buttons and `JoystickAxisState` (Off/Negative/Positive) for axes.

### Buzzer
5-track software synthesizer (`buzzer.c`). TIM6 fires at 1ms, advances each active track's note counter. Notes are interleaved `uint16` pairs: `{frequency_hz, duration_ms}…`; frequency 0 = pause. The highest-numbered playing track drives TIM3 PWM output on PB5 at 50% duty. Supports play with optional done-flag, pause, resume, stop, loop. Frequency constants from C2 (65 Hz) through B8 (7902 Hz) defined in `buzzer.h`.

### EEPROM Settings
AT24C512 (64KB) on I2C1 at 400kHz. `settings_storage.c` provides a CRC-16 protected key/value store, split into a 16KB console partition and a 48KB games partition:
- System header at 0x0000 (magic/version, game count, monotonic write sequence, CRC)
- Game directory at 0x0100 (48 × `GameDirectoryEntry`: name key, state, write seq, CRC)
- Console settings at 0x1000 (one entity: version, data, CRC; remainder reserved)
- Game data at 0x4000 (48 × 1KB `GameDataEntity` slots: version, size, ≤1018B data, CRC)

Game saves are keyed by the `.bin` name (extension stripped, case-insensitive). A game opts in via `has_settings` in its binary header; the loader binds a slot (`settingsStorageBindGame`) and the game reaches it through the settings `ConsoleAPI`. When all 48 slots are full, writes return `STORAGE_FULL` — nothing is auto-evicted; callers manage space with the list / delete / `settingsStorageEvictOldest` APIs. All writes validate with CRC-16-CCITT (polynomial 0x1021, initial 0xFFFF). Corrupt entries are auto-cleaned on init via `settingsStorageCleanupCorrupted()`. The typed console blob lives in `console_settings_storage.c`.

### DMA
Two uses: **ADC1 DMA** (DMA2 Stream0, circular, 16-bit) transfers 4 ADC channels continuously, and **FSMC DMA** (DMA2 Stream6, memory-to-memory) bursts pixel data to the ILI9341 display for opaque tile rendering.

### Network
ESP-01 on USART1 (PA9/PA10, 921600 baud). **Not yet implemented** — `network.c` is a stub. The ESP-01 sketch in `Shared/Esp01s/Esp01s.ino` currently only blinks the LED and opens a serial connection.

### Logging
Severity-tagged logging over SWO/ITM (`Console/Src/Logger/logger.c`). Console code logs through macros:

```c
LOGGER_LOG_INFO(LOGGER_RENDERER, "%d sprites registered", sprite_count);
// -> [12345][I][REND] 42 sprites registered    (tick / level / channel)
```

The first argument is a **channel switch** from `logger_config.h` — the macro uses it both as a compile-time `0/1` guard and (stringized) as the printed tag, so the channel name comes for free. Levels are `LOGGER_LOG_{ERROR,WARN,INFO,DEBUG}`; a site survives only when its channel is `1` and its level `<= LOGGER_MAX_LEVEL`. Both are compile-time constants, so a silenced channel/level folds away entirely (format string included) — zero flash, zero cycles. `LOGGER_ENABLED 0` strips every site.

Enabled-path overhead is dominated by **bytes over SWO** — `ITM_SendChar` spin-waits ~5 µs/byte at the 2 MHz trace clock, which swamps the wrapper call, the level lookup, and the `LOGGER_` strip (~0.1 µs combined). The line is therefore kept short on purpose: raw tick, one-char level (`"EWID"[level]`), and a `%.4s`-capped channel tag. Output goes straight through `vprintf` (no intermediate buffer, no truncation of the message); requires `swoInit()` first (done in `peripheralsInit`).

Loaded games can't link the logger, so they log via the ConsoleAPI: `api.log("score %d", score)` routes to `loggerGameLog()` on the `LOGGER_GAME` channel (printed as `[..][I][GAME] score 5`). Game-local `printf` is unreliable — the game's `_write` has no backing `__io_putchar` — so `api.log` is the correct path for games.

## Pixel Forge (graphics creator)

`tools/graphics/pixel_forge.py` — PyQt6 pixel-art editor for free-form pictures (any W×H, not tiles). Exports a `GfxAsset` `.bin` (header + palette + packed pixels) and a `.c` companion; format declared in `tools/graphics/gfx_asset.h` (magic "GFX1", 2bpp = 4 colors / 4bpp = 16 colors, slot 0 transparent, system palette indices 0–63). Qt-free core (`pixelforge/canvas.py`, `storage.py`, `history.py`) + GUI layer (`pixelforge/gui/`). Unlike `tools/music_creator/`, this tool is written to be maintainable — no "vibecoded" disclaimer headers. See `tools/graphics/README.md`.

## Music Creator (buzzer composer)

`tools/music_creator/music_creator.py` — PyQt6 piano-roll editor for composing buzzer music. Exports interleaved `uint16` pairs (frequency_hz, duration_ms), 4 bytes per note; frequency 0 = pause. Dual output: `.bin` (magic `"NOT1"`, loadable save state + runtime asset) and `.c` companion. Requires PyQt6, pygame, numpy, PyYAML. Qt-free core (`timeline.py`, `storage.py`, `audio.py`, `notes.py`, `history.py`, `constants.py`) + Qt GUI layer (`gui/piano_roll.py`, `gui/main_window.py`, `gui/theme.py`). Pitch/frequency mapping in `notes.yaml`. Example tracks in `Assets/Music/`. See `tools/music_creator/README.md`.

## Asset Packer

`tools/packer/packer.py` — CLI tool that bundles loose binary assets into a single `.pak` container and emits a C enum header. Input is a YAML manifest; output is `<name>.pak` + `<name>AssetEnum.h`. Binary format defined in `tools/packer/pak_format.h` (magic `"PAK1"`, CRC32-verified per-entry and whole-file). Modular Python package (`assetpacker/format.py`, `assetpacker/manifest.py`, `assetpacker/builder.py`, `assetpacker/verify.py`, `assetpacker/codegen.py`). The generated enum type is named after the output (e.g. `Level1AssetId`) so multiple pack headers can coexist. Blobs are stored verbatim (no compression). See `tools/packer/README.md` for the full binary layout and C usage example.

## Memory Analysis

`tools/memory_analysis/memory_analysis.py` — CLI that parses GNU LD `.map` files (post-link section sizes) and/or `.ld` linker scripts (region capacities, no build required) into a per-region RAM/CCM/flash usage report: per-section breakdown, percentages, NOLOAD/LMA awareness, and free-space projections. With no args it auto-discovers the Console + GameXO maps under `build/` and compares them side by side; `--json` for machine output, `--no-color`/`--quiet` for plain or summary-only. Standard-library only (Python 3.10+). Modular package (`memoryanalysis/ld_parser.py`, `map_parser.py`, `model.py`, `report.py`, `cli.py`). This is the tool that answers "how much RAM is left?" — the renderer's static buffers dominate `CONSOLE_RAM`. See `tools/memory_analysis/README.md`.

## Asset System

The pipeline is **author → pack → load**:
1. **Author** — graphics in Pixel Forge (`.bin`/`.c`, 2bpp/4bpp) and music in Music Creator (`.bin`/`.c`).
2. **Pack** — the packer (`tools/packer/`) bundles the loose `.bin` assets from a YAML manifest into one `<name>.pak` container plus a generated `<name>AssetEnum.h` of asset IDs.
3. **Load** — at runtime a game streams one asset by ID with `assetLoaderGetAssetData(id, buffer, size)` (and `assetLoaderGetAssetMetadata` / `assetLoaderGetAssetHeader`) into a buffer carved from the CCM `GAME_RAM_ASSET` arena. Lazy loading lets total asset data exceed RAM. `AssetHeader` / `AssetMetaData` are declared in `Shared/Api/asset_interface.h`.

> **Status:** the `.pak` format, packer, CCM arena, and on-device loader are all implemented. When a game is loaded, `game_loader.c` derives the matching `<game>.pak` from the `.bin` name and binds it via `assetLoaderOpenPak()`; the file stays open for the game's lifetime and is closed on exit. Games then stream assets by id with `assetLoaderGetAssetData()`, which seeks into the bound pak, copies the blob into the game's buffer, and verifies its CRC32 (`crc32_calculate` in `Console/Src/Crc`). The older inline-asset macros (`DEFINE_ASSET_8/16/HEADER`) and the `DEFINE_TILE` tile encoder (whole `Shared/TileUtils/` dir) have been removed — graphics now come exclusively from Pixel Forge.

## Tools Architecture

The repo ships four Python tools that share one shape — a dependency-free **core** library holding the domain logic, wrapped by a thin frontend:
- **Pixel Forge** and **Music Creator** are PyQt6 desktop GUIs (Qt-free core + Qt GUI layer).
- **Asset Packer** and **Memory Analysis** are CLIs (core + argparse frontend).

Shared conventions:
- **Qt-free / UI-free core** library with the domain logic (no frontend dependency)
- **Thin frontend** (Qt GUI or CLI) that imports the core
- **VS Code launch configs** in `.vscode/launch.json` for each tool
- **PyYAML** is the shared config/manifest format for the GUI tools and the packer (Memory Analysis is standard-library only)

## Naming Conventions

| Element           | Convention       |
| ----------------- | ---------------- |
| Macros/Defines    | UPPER_SNAKE_CASE |
| Constants         | UPPER_SNAKE_CASE |
| Global variables  | g_snake_case     |
| Static globals    | s_snake_case     |
| Local variables   | snake_case       |
| Functions         | snake_case       |
| Struct/Enum types | PascalCase       |
| Struct members    | snake_case       |
| Typedefs          | PascalCase       |

## C Code Style

- **Allman style** for braces — opening brace on its own line:
  ```c
  if (condition)
  {
      // body
  }
  ```
- Always use curly braces `{}` around `if`, `for`, `while` bodies — even single-line statements
- Use 4-space indentation
- Use `/* */` for block comments, `//` for short inline notes
- One statement per line

## Debug Output

`printf()` routes to SWO/ITM (not USART). SWO is initialized at 2 MHz in `swoInit()`. `_write()` in `syscalls.c` sends each byte via `ITM_SendChar()`. View output with a debug probe that supports SWO trace (e.g., STLink + OpenOCD), or use `make flashswo` which pipes decoded ITM packets to stdout.

## Hardware

- **MCU**: STM32F407VET6 (Cortex-M4, FPU, 512K flash, 128K SRAM + 64K CCM)
- **Display**: ILI9341 240×320 via FSMC 16-bit 8080 parallel (PD/PE pins). Rotation 1 = landscape. Backlight on PA3, reset on PC7.
- **Joysticks**: 2 analog axes each (ADC1 PC0–PC3), 10 digital buttons (GPIO PA0–PA12, PB11–PB12)
- **Audio**: Passive buzzer on PB5 (TIM3_CH2)
- **Storage**: Built-in SD card via SDIO (PC8–PC12, PD2), FAT32, mount point `"0:"`. Card detect on PD3 (active-low).
- **EEPROM**: AT24C512 on I2C1 (PB8/PB9), address `0x50`, used for console settings
- **Network**: ESP-01 on USART1 (PA9/PA10), 921600 baud, controlled via PB10/PB6/PC6/PC13
- **Debug interface**: SWD (PA13/PA14) + SWO (PB3)

EEPROM layout: console partition 0x0000–0x3FFF (system header 0x0000, game directory 0x0100, console settings 0x1000), games partition 0x4000–0xFFFF (48 × 1KB save slots).

## Code Quality

Use good code practices, think about the architecture, write clean and maintainable code. Avoid patterns that look like generated or "vibecoded" code — this repo values deliberate, hand-crafted engineering.
