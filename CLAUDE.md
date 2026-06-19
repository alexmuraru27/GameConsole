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

make esp            # Build the ESP-01S WiFi firmware (Esp01s/) via PlatformIO
make deployesp      # Copy the built ESP firmware to SD as ESP01.bin (for "Upgrade WiFi module")
```

`DEBUG=1` is on by default in `common.mk`. Add `GCC_PATH=<path>` if the ARM toolchain isn't on PATH. `make esp` needs the PlatformIO CLI (`pio`); override with `PIO=<path>` if it isn't on PATH.

## Architecture

This is an embedded game console platform targeting the STM32F407VET6. Two separate binaries are built and linked independently:

### Console (firmware, flashed to MCU)
Lives in `Console/`. Runs from flash at boot, initializes all hardware, shows the main menu, loads games from SD card, and exposes the **ConsoleAPI** to loaded games.

**Boot sequence** (`startup.s` → `main.c`):
1. `Reset_Handler`: copies `.data` from flash to RAM, zeros `.bss` and `.ccmram`, calls `__libc_init_array`, then `main()`
2. `SystemInit()` → `systemClockConfig()`: HSE 8MHz → PLL → 168MHz SYSCLK, SysTick at 1ms
3. `gameConsoleInit()`: `coreInit()` (SWO, `faultsInit`, `syscallInit`, GPIO, timers, buzzer) → `storageInit()` (I2C, EEPROM, settings) → `applyConsoleSettings()` (read+apply the persisted mute *before* any boot sound) → `peripheralsInit()` (DMA, USART, ADC) → `devicesInit()` (ILI9341 display, renderer, joystick, FatFs mount) → `mpuInit()` (arm MPU confinement). The `beep_step()` progress scale runs only across `peripheralsInit`/`devicesInit` (after the mute is applied), so a muted console boots silent. There is no longer an API-exposure step — games reach the console through SVC syscalls, not a shared struct (see the Kernel subsystem).
4. `main()` then runs `init()` → loop of `update()` → `render()`

The production path is the main menu: `main()` calls `mainMenuInit()` then loops `mainMenuUpdate()` / `mainMenuRender()`. The `MainMenu/` module is a small **screen state machine** (`main_menu.c` is the orchestrator) over a shared theme/draw/input layer (`menu_common.c`): the **root** menu offers *Games* / *Settings* / *Poll Remote Games*. *Games* (`game_list.c`) is the centered-hero picker — lists the SD-card `.bin` games, browses with up/down, and launches the highlighted one with Special Button 1; `gameLoaderLoadGame()` blocks for the game's lifetime and the picker rebuilds its surface when it returns. *Settings* (`settings_menu.c`) is a tree of typed settings (currently one Buzzer Sound toggle, persisted via `ConsoleSettings.audio_enabled` and applied to `buzzerSetMute()`); the boot-time read-and-apply runs in `gameConsoleInit()` before the boot song so a muted console boots silent. *Poll Remote Games* is a stub. Controls everywhere: up/down move, Special Button 1 enters/confirms/toggles, Special Button 2 steps back a level. The `renderer_testing.c` perf harness is still in the tree (and its source still compiles) but is only wired in when `RENDERER_TESTING` is defined; it is off by default.

**Fault handlers** (`Console/Src/Kernel/faults.c`): MemManage/BusFault/UsageFault are enabled (not escalated to HardFault) and decoded — each prints its name, the faulting context (PSP=game / MSP=kernel), stacked PC/LR/PSR, the CFSR sub-flags by name, and MMFAR/BFAR via `printf()`/SWO. A fault in a running game is **recoverable** (the kernel switches back to the console and the menu shows a "crashed" banner); a fault in the kernel itself halts.

### Games (`.bin` files, loaded at runtime from SD card)
`GameXO/` is the reference game. Game binaries are loaded into RAM and run **unprivileged**, isolated from the console by the MPU. Each game:
- Uses `../game.ld` as its linker script
- Links `Shared/Syscall/console_syscalls.c` and calls the console through the strongly-typed SVC stubs in `console_syscalls.h` (e.g. `rendererSubmitLayer(...)`) — no shared struct, no direct linkage to console code
- Calls `DECLARE_GAME_BINARY_HEADER(entry_func)` at file scope (places a 12-byte `GameBinaryHeader` — magic, ABI version, entry — into the `.game_header` section at offset 0 of the `.bin`)
- Returns to the console by calling `gameExit()` (or letting `main()` return; `_game_start` calls `gameExit()` for it). Special Button 2 is the convention for "quit".

**Game loading** (`game_loader.c`): reads the 12-byte `GameBinaryHeader`, checks the magic and ABI version, copies the **whole flat image** to `GAME_RAM` (`.text`/`.rodata`/`.data` are linked at their final addresses, so no per-section table and no LMA copy), then hands off to `kernelRunGame()`. The game's `_game_start` zeroes its own `.bss` (the image is NOLOAD for `.bss`), runs ctors, and calls `main()`.

**GameXO architecture**: a finite state machine (`CHOOSE → PLAYING → END`) in `game_state_manager.c` drives input, the AI opponent (minimax with alpha-beta pruning in `tic_tac_toe_logic.c`), and rendering. `game_assets.c` wraps the ConsoleAPI: it streams the packed graphics (X/O marks, cursor) from `GameXO.pak` into the CCM asset arena and builds `Sprite`s, plays packed buzzer tracks, and draws text with the console fonts. The board grid, marks, and cursor are drawn through the renderer's sprite API; all text uses the exposed console fonts (no glyphs are packed). Assets are authored with the in-repo tools under `GameXO/Assets/{Graphics,Music}` (per `GameXO/Assets/manifest.yaml`) and the GameXO Makefile runs the packer to emit `GameXO.pak` + `GameXOAssetEnum.h`.

### Shared (`Shared/`)
`Shared/Api/` is header-only — the public data types both sides agree on (`renderer_interface.h`, `font_interface.h`, `asset_interface.h`, `settings_interface.h`, `joystick_interface.h`, `header_interface.h`, and the umbrella `game_console_api.h`). `Shared/Syscall/` holds the **syscall ABI**: `syscall_numbers.h` (ids + ABI version, the single source of truth for both sides), `console_syscalls.h` (the typed game-facing prototypes), and `console_syscalls.c` (the SVC stubs, compiled into each game).

### Memory Layout
See `docu/memory.md` for the full map. Quick reference:

| Region         | Origin      | Size | Purpose |
| -------------- | ----------- | ---- | ------- |
| CONSOLE_RAM    | 0x20000000  |  96K | Console firmware `.data`/`.bss`/MSP+PSP stacks, DMA buffers, renderer (SRAM) |
| GAME_RAM       | 0x20018000  |  32K | Game binary: code + rodata + data + bss + stack (SRAM, executable). One MPU region. |
| GAME_RAM_ASSET | 0x10000000  |  64K | Runtime asset arena for `.pak` loads — **CCM** (D-bus only, not executable) |
| CONSOLE_FLASH  | 0x08000000  | 512K | Console firmware |

`GAME_RAM` is a power-of-two size aligned to its size (`0x20018000`) so a single ARMv7-M MPU region confines an unprivileged game to it; the old 2K `SHARED_RAM` was reclaimed when the function-pointer API became SVC syscalls. CCM is D-bus only on the STM32F4, so **game code must run from SRAM (`GAME_RAM`), never CCM** — CCM holds the data-only asset arena. Games ship as two files on SD: `GameXO.bin` (12-byte header + code + rodata + data, all targeting SRAM) + `GameXO.pak` (assets, streamed lazily by ID into the CCM arena). See `docu/memory.md` for the authoritative map.

### EEPROM
AT24C512 (64KB) on I2C1. `settings_storage.c` splits it into a 16KB console partition (system header + 48-entry game directory + console settings blob) and a 48KB games partition (48 × 1KB save slots). Game saves are keyed by `.bin` name, CRC-16-CCITT protected, auto-cleaned on init. See `docu/memory.md`.
### Kernel / game isolation (`Console/Src/Kernel/`)
Games run as untrusted, unprivileged code; the console is the kernel. A game **never** calls console code directly — it traps in via SVC.
- **Syscall ABI** (`syscall.c`, `Shared/Syscall/`): each API call is a typed C stub that puts the syscall id in `r12`, args in `r0-r3`, and runs `svc #0`. `SVC_Handler` runs privileged on the **MSP (kernel stack)**, validates, dispatches to the real console function, and returns the result in the caller's `r0`. SVC is at the lowest priority so a long syscall (e.g. a full render) stays preemptible by the buzzer/joystick timer ISRs. Pointer arguments are range-checked (`gameCanRead`/`gameCanWrite`) so a game can't make the kernel touch console memory on its behalf; `gameLog` is formatted game-side and passed as raw bytes so no format string reaches the kernel.
- **Context switch** (`scheduler.c`): the console runs privileged on the MSP. `kernelRunGame()` builds the game's initial unprivileged PSP frame and enters it via an **SVC** (`SYS_LAUNCH`), which parks the console's context on the MSP. Control returns to the exact launch point via **PendSV** — pended either by the game's `SYS_EXIT` (clean) or by the fault handler (crash). A crash is recoverable because the faulting unprivileged context can be abandoned: PendSV tail-chains before the faulting instruction is retried.
- **MPU** (`mpu.c`): enabled with `PRIVDEFENA` (privileged console keeps the full map). A running game gets exactly two unprivileged regions: `GAME_RAM` (RWX) and the CCM asset arena (RW, execute-never). Anything else — console RAM, peripherals, flash — traps as a recoverable MemManage fault. Regions are released on exit/crash.

## Documentation

Deep-dive docs live in `docu/`:
- `docu/API_README.md` — ConsoleAPI surface and module internals
- `docu/kernel.md` — game isolation, ground-up: privilege/stack model, the SVC syscall ABI, the SVC-in/PendSV-out context switch, MPU protection, pointer validation, fault recovery, interrupt priorities (with diagrams)
- `docu/renderer.md` — the renderer, ground-up: frame pipeline, the hot loop, and the 24→76 FPS optimization journey
- `docu/memory.md` — authoritative SRAM/CCM/flash map, game binary layout, EEPROM layout, linker scripts
- `docu/flasher.md` — ESP-01 flashing, ground-up: the esp-serial-flasher submodule + bare-metal vtable port, USART1 driver, bootstrap sequence, the flash flow, build integration, and troubleshooting
- `docu/HW.md` — Hardware schematics and full pinout
- `docu/game_template/README.md` — guide for creating new games + the full game-facing API reference, with a copyable `startup.s` template

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
AT24C512 (64KB) on I2C1 at 400kHz. `settings_storage.c` provides a CRC-16 protected key/value store, packed flat with no arbitrary padding:
- System header at 0x0000 (magic/version, game count, monotonic write sequence, CRC)
- Game directory at 0x0100 (29 × `GameDirectoryEntry`: name key, state, write seq, CRC)
- Console settings at 0x0945 (one 2 KB `ConsoleSettingsEntity`: version, size, ≤2042B data, CRC)
- Game data at 0x1145 (29 × 2 KB `GameDataEntity` slots: version, size, ≤2042B data, CRC)

Game saves are keyed by the `.bin` name (extension stripped, case-insensitive). A game opts in via `has_settings` in its binary header; the loader binds a slot (`settingsStorageBindGame`) and the game reaches it through the settings `ConsoleAPI`. When all 48 slots are full, writes return `STORAGE_FULL` — nothing is auto-evicted; callers manage space with the list / delete / `settingsStorageEvictOldest` APIs. All writes validate with CRC-16-CCITT (polynomial 0x1021, initial 0xFFFF). Corrupt entries are auto-cleaned on init via `settingsStorageCleanupCorrupted()`. The typed console blob lives in `console_settings_storage.c`.

### DMA
Two uses: **ADC1 DMA** (DMA2 Stream0, circular, 16-bit) transfers 4 ADC channels continuously, and **FSMC DMA** (DMA2 Stream6, memory-to-memory) bursts pixel data to the ILI9341 display for opaque tile rendering.

### Network
ESP-01S on USART1 (PA9/PA10, 921600 baud runtime). The runtime network protocol is **not yet implemented** — the console-side API (`Console/Src/Network/network.c`, `networkInit`/`networkIsConnected`) is a stub. USART1 itself is brought up (`usart.c`: polled 8N1, PCLK2 84 MHz, default 115200) and is used today by the ESP Flasher. The console↔ESP wire contract (baud, protocol version, command IDs) is the header-only `Shared/Esp01s/network_protocol.h`, shared by both sides. The ESP-01S firmware itself is a separate PlatformIO target at **`Esp01s/`** (board `esp01_1m`, Arduino framework) — currently a blinky; build it with `make esp` and copy it to the SD card as `ESP01.bin` with `make deployesp` (for the flasher). See `Esp01s/README.md`.

### ESP Flasher
Reflashes the ESP-01 firmware from the SD card (Settings → **Upgrade WiFi module**). Built on the vendored `tools/esp-serial-flasher` submodule (HAL-free core; the bundled HAL `port/stm32_port.c` is **not** compiled). Our glue lives in `Console/Src/Flasher/`:
- `esp_flasher_port.c` — a bare-metal `esp_loader_port_ops_t` vtable over `usart.c` (polled byte I/O with timeouts) and the ESP-01 bootstrap pins exposed by `gpio.c` (`esp01SetEnable/Reset/Bootloader`: EN=PB10, RST=PB6, IO0=PC6, IO2=PC13). `enter_bootloader` drives IO0 low + pulses RST to enter the ROM download loader.
- `esp_flasher.c` — `espFlasherFlashFile(path, cb, ctx)`: streams the image off FatFs in 1 KB blocks, connects at 115200 with the ESP8266 RAM stub, programs at flash offset 0, MD5-verifies, then resets the ESP to boot the new firmware. Reports progress through the callback.
The UI (`MainMenu/wifi_update.c`) is a blocking modal (like launching a game): it looks for `ESP01.bin` at the card root, shows a progress bar, and waits for Special Button 2. Producing `ESP01.bin` (Arduino/PlatformIO) is out of scope for now. Logs on the `LOGGER_FLASHER` channel.

### Logging
Severity-tagged logging over SWO/ITM (`Console/Src/Logger/logger.c`). Console code logs through macros:

```c
LOGGER_LOG_INFO(LOGGER_RENDERER, "%d sprites registered", sprite_count);
// -> [12345][I][REND] 42 sprites registered    (tick / level / channel)
```

The first argument is a **channel switch** from `logger_config.h` — the macro uses it both as a compile-time `0/1` guard and (stringized) as the printed tag, so the channel name comes for free. Levels are `LOGGER_LOG_{ERROR,WARN,INFO,DEBUG}`; a site survives only when its channel is `1` and its level `<= LOGGER_MAX_LEVEL`. Both are compile-time constants, so a silenced channel/level folds away entirely (format string included) — zero flash, zero cycles. `LOGGER_ENABLED 0` strips every site.

Enabled-path overhead is dominated by **bytes over SWO** — `ITM_SendChar` spin-waits ~5 µs/byte at the 2 MHz trace clock, which swamps the wrapper call, the level lookup, and the `LOGGER_` strip (~0.1 µs combined). The line is therefore kept short on purpose: raw tick, one-char level (`"EWID"[level]`), and a `%.4s`-capped channel tag. Output goes straight through `vprintf` (no intermediate buffer, no truncation of the message); requires `swoInit()` first (done in `peripheralsInit`).

Loaded games can't link the logger, so they log via the ConsoleAPI: `api.log("score %d", score)` routes to `loggerGameLog()` on the `LOGGER_GAME` channel (printed as `[..][I][GAME] score 5`). Game-local `printf` is unreliable — the game's `_write` has no backing `__io_putchar` — so `api.log` is the correct path for games.

**Convention — always log in the console OS.** When writing or modifying console firmware (anything under `Console/Src/`), add logging as part of the change; do not leave new code silent. The logger is compile-time gated per channel/level (a silenced site folds away to zero flash and zero cycles — verified), so logging is free in production builds and there is no cost argument for omitting it. Follow these rules:
- **Map to the right channel** from `logger_config.h` (e.g. `LOGGER_CORE`, `LOGGER_KERNEL`, `LOGGER_SDIO`, `LOGGER_EEPROM`, …). Add a new channel there if a subsystem has none.
- **Level discipline:** `INFO` for once-per-boot / lifecycle milestones (init done, game launched/exited); `DEBUG` for per-driver detail and frequent-but-cold events; `WARN` for recoverable anomalies (rejected input, validation failures); `ERROR` for hard failures. Aim for INFO to read as the session narrative on its own, with DEBUG as the layer beneath it.
- **Log at boundaries, not internals:** public-API entry/exit, state transitions, and every error/early-return path — not every internal step.
- **Never log on hot paths or in ISRs.** The renderer per-frame compositor, the 1 ms buzzer ISR (and `buzzerStop`, which it calls), the 50 ms joystick ISR, the per-syscall SVC dispatch, and PendSV teardown stay silent — a single SWO line (~5 µs/byte) there breaks audio/frame timing. Log their *init* and *lifecycle*, never their per-tick work.
- **Don't instrument** third-party code (`ff.c`, `ffunicode.c`), pure data tables (fonts), the SWO sink itself (`logger.c`, `syscalls.c` — recursion risk), or `faults.c` (it uses raw `printf` by design).

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
