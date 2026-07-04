# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Version Control

**Never `git commit` and never `git push`.** Leave all changes in the working tree (staged or unstaged is fine) for the user to review and commit themselves. Do not run these even if asked — surface a proposed commit message instead and let the user run it.

## Build Commands

Requires `arm-none-eabi-gcc` toolchain and `openocd` on PATH.

```bash
make all            # Build the Apps (GameXO + TestRenderer), Bootloader, Console firmware + Shared (header check)
make flash          # Flash bootloader (sector 0) + Console app (sectors 1-5) via OpenOCD/STLink
make flashswo       # Flash and start SWO trace output via tools/scripts/swo.sh
make deploy         # Stage everything into the update-server tree: each app's .bin (+ .pak) (content/games), Console.bin (content/Firmware), and, if built, ESP01.bin (content/Firmware)
make clean          # Remove all build artifacts

make -C Console all # Build only the console firmware (app, links at 0x08004000)
make -C Apps/GameXO all      # Build only the GameXO game binary
make -C Apps/TestRenderer all # Build only the TestRenderer renderer-benchmark game
make -C Bootloader all # Build only the self-flash bootloader (sector 0, 0x08000000)

make esp            # Build the ESP-01S WiFi firmware (Esp01s/) via PlatformIO
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

The production path is the main menu: `main()` calls `mainMenuInit()` then loops `mainMenuUpdate()` / `mainMenuRender()`. The `MainMenu/` module is a small **screen state machine** (`main_menu.c` is the orchestrator) over a shared theme/draw/input layer (`menu_common.c`): the **root** menu offers *Games* / *Settings* / *Poll Updates* / *Reboot Console*. *Games* (`game_list.c`) is the centered-hero picker — lists the `.bin` games under `Games/`, browses with up/down, and launches the highlighted one with Special Button 1; `gameLoaderLoadGame()` blocks for the game's lifetime and the picker rebuilds its surface when it returns. *Settings* (`settings_menu.c`) is a tree of typed settings — a Buzzer Sound toggle (persisted via `ConsoleSettings.audio_enabled`, applied to `buzzerSetMute()`), a **WiFi** sub-category grouping *Networks* (scan/connect) and *Server address*, and a **Firmware** sub-category grouping *Upgrade OS* (`os_update.c`, self-flash the console firmware from `Firmware/Console.bin`) and *Upgrade WiFi module* (`wifi_update.c`); the boot-time mute read-and-apply runs in `gameConsoleInit()` before the boot song so a muted console boots silent. *Poll Updates* (`remote_update.c`) fetches the server manifest and lists each item — a game's `.bin` and its paired `.pak` collapse into one row (downloaded together), other files stand alone — diffing each against a local `Manifests/downloaded.csv` record of the last download (NEW / UPD / UpToDate) and highlighting CRC mismatches; Special Button 1 downloads the selected item (CRC-verified) and records its new CRC. *Reboot Console* calls `gameConsoleReboot()` (`NVIC_SystemReset`) for a full restart. Controls everywhere: up/down move, Special Button 1 enters/confirms/toggles, Special Button 2 steps back a level. The old in-console `renderer_testing.c` perf harness has been removed from the console; it now lives as an ordinary loadable game, `Apps/TestRenderer/` — an endless auto-scroller (almost no game logic) whose sprite load is **swept 50%→100%→50% of the renderer's shared sprite budget** (1050 sprites, which the renderer caps as a frame total rather than per layer; TestRenderer fills it as 3×350) by a real-time triangle wave, drawing live current/min/avg/max FPS plus the load % on screen so you can watch throughput track the load (it runs uncapped). Its tile graphics ship in `TestRenderer.pak` (generated from ASCII art by `Assets/gen_tiles.py`) and are streamed at init into both the CCM asset arena and a GAME_RAM buffer; the three 350-sprite per-layer arrays (21 KB) also live in the CCM arena.

```
Main menu (main_menu.c)
├── Games            game_list.c   — pick Games/*.bin, A launches (gameLoaderLoadGame blocks)
├── Settings         settings_menu.c (typed-node tree)
│   ├── Buzzer Sound      [ON/OFF] toggle  → ConsoleSettings.audio_enabled / buzzerSetMute
│   ├── WiFi
│   │   ├── Networks         wifi_menu.c   — scan / connect / save creds
│   │   └── Server address   keyboard.c    — edit Settings/server.txt
│   └── Firmware
│       ├── Upgrade OS           os_update.c   — self-flash Firmware/Console.bin (bootloader applies)
│       └── Upgrade WiFi module  wifi_update.c — flash Firmware/ESP01.bin to the ESP-01
├── Poll Updates     remote_update.c — fetch manifest, diff vs downloaded.csv, download to SD
└── Reboot Console   gameConsoleReboot() (NVIC_SystemReset)
```


**Fault handlers** (`Console/Src/Kernel/faults.c`): MemManage/BusFault/UsageFault are enabled (not escalated to HardFault) and decoded — each prints its name, the faulting context (PSP=game / MSP=kernel), stacked PC/LR/PSR, the CFSR sub-flags by name, and MMFAR/BFAR via `printf()`/SWO. A fault in a running game is **recoverable** (the kernel switches back to the console and the menu shows a "crashed" banner); a fault in the kernel itself halts.

### Games / Apps (`.bin` files, loaded at runtime from SD card)
Games live under `Apps/` — `Apps/GameXO/` is the reference game and `Apps/TestRenderer/` is the renderer benchmark. Game binaries are loaded into RAM and run **unprivileged**, isolated from the console by the MPU. **The OS owns the game loop but runs it uncapped**: a game is not a `main()` that runs forever but three callbacks the console calls — `init` once, then `update` then `render` every frame, back to back, with the console servicing its own work (USART/network, etc.) between frames. The OS does **not** cap the frame rate; a game that wants a fixed rate paces itself with `delay()` (GameXO sleeps out each frame to hold ~60 FPS; TestRenderer runs flat out to measure throughput). For **frame-rate-independent movement**, a game scales by `getDeltaTimeUs()` — the microseconds between the previous two `update()` calls, which the OS measures from the free-running 168 MHz cycle counter (`DWT->CYCCNT`) in `game_loader.c` (first frame returns 0; clamped against a stall). TestRenderer scrolls at a constant px/sec this way, so its speed is steady even as the load sweep swings the frame rate. Each game:
- Uses `linker/app.ld` as its linker script (the app Makefiles reference it as `$(REPO_ROOT)/linker/app.ld` and pass `-L$(REPO_ROOT)/linker` so its `INCLUDE "common.ld"` resolves). All linker scripts live in `linker/` — `common.ld` (shared MEMORY map), `app.ld` (games), `console.ld`, `bootloader.ld`.
- Links `Shared/Syscall/console_syscalls.c` and calls the console through the strongly-typed SVC stubs in `console_syscalls.h` (e.g. `rendererSubmitLayer(...)`) — no shared struct, no direct linkage to console code. That shared file also provides the universal `_game_start` (C-runtime bootstrap) and `_game_return` (callback return trampoline), so a game needs **no `startup.s`** and no vector table.
- Calls `DECLARE_GAME_HEADER(init, update, render)` at file scope in `main.c` (places a 28-byte `GameBinaryHeader` — magic, ABI version, the two runtime stubs, and the three game callbacks — into the `.game_header` section at offset 0 of the `.bin`; the macro fills the stubs, the game names its callbacks)
- Returns to the console by calling `gameExit()` from `update` (Special Button 2 is the convention for "quit"); a crash returns there too.

**Game loading** (`game_loader.c`): reads the 28-byte `GameBinaryHeader`, checks the magic and ABI version, copies the **whole flat image** to `GAME_RAM` (`.text`/`.rodata`/`.data` are linked at their final addresses, so no per-section table and no LMA copy), then hands off to `kernelRunGame()` together with `collect`/`flush` callbacks it runs around `update()` (the seam for inter-frame console work — `collect` ingests inbound before the game steps, `flush` emits outbound after it, split so an async ESP-NOW round-trip overlaps `render`; no frame pacing, games self-pace). The kernel invokes `_game_start` (which zeroes the game's `.bss` — NOLOAD in the `.bin` — and runs ctors), then `init`, then loops `collect`/`update`/`flush`/`render`.

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
| BOOTLOADER     | 0x08000000  |  16K | Self-flash bootloader (sector 0; SWD-flashed, never self-updated) |
| CONSOLE_FLASH  | 0x08004000  | 240K | Console firmware / app (sectors 1-5; VTOR here) |
| OS_STAGING     | 0x08040000  | 256K | OS-update staging scratch (sectors 6-7) |

`GAME_RAM` is a power-of-two size aligned to its size (`0x20018000`) so a single ARMv7-M MPU region confines an unprivileged game to it; the old 2K `SHARED_RAM` was reclaimed when the function-pointer API became SVC syscalls. CCM is D-bus only on the STM32F4, so **game code must run from SRAM (`GAME_RAM`), never CCM** — CCM holds the data-only asset arena. Games ship as two files in the SD `Games/` folder: `GameXO.bin` (28-byte header + code + rodata + data, all targeting SRAM) + `GameXO.pak` (assets, streamed lazily by ID into the CCM arena). See `docu/memory.md` for the authoritative map.

### EEPROM
AT24C512 (64KB) on I2C1. `settings_storage.c` splits it into a 16KB console partition (system header + 48-entry game directory + console settings blob) and a 48KB games partition (48 × 1KB save slots). Game saves are keyed by `.bin` name, CRC-16-CCITT protected, auto-cleaned on init. See `docu/memory.md`.
### Kernel / game isolation (`Console/Src/Kernel/`)
Games run as untrusted, unprivileged code; the console is the kernel. A game **never** calls console code directly — it traps in via SVC.
- **Syscall ABI** (`syscall.c`, `Shared/Syscall/`): each API call is a typed C stub that puts the syscall id in `r12`, args in `r0-r3`, and runs `svc #0`. `SVC_Handler` runs privileged on the **MSP (kernel stack)**, validates, dispatches to the real console function, and returns the result in the caller's `r0`. SVC is at the lowest priority so a long syscall (e.g. a full render) stays preemptible by the buzzer/joystick timer ISRs. Pointer arguments are range-checked (`gameCanRead`/`gameCanWrite`) so a game can't make the kernel touch console memory on its behalf; `gameLog` is formatted game-side and passed as raw bytes so no format string reaches the kernel.
- **Context switch** (`scheduler.c`): the console runs privileged on the MSP and **owns the game loop**. `kernelRunGame()` programs the MPU once, then drives the game callback-by-callback: each `kernelInvokeGame()` builds a fresh unprivileged PSP frame and enters it via an **SVC** (`SYS_INVOKE`), parking the console's context on the MSP; when the callback returns it traps straight back (`SYS_FRAME_DONE`) to the parked console frame, inline in the SVC handler, leaving the MPU/session up for the next callback (no PendSV on the per-frame path). The session ends — MPU released, control returned for good — via **PendSV**, pended either by `gameExit()`'s `SYS_EXIT` (clean) or by the fault handler (crash). A crash is recoverable because the faulting unprivileged context can be abandoned: PendSV tail-chains before the faulting instruction is retried.
- **MPU** (`mpu.c`): enabled with `PRIVDEFENA` (privileged console keeps the full map). A running game gets exactly two unprivileged regions: `GAME_RAM` (RWX) and the CCM asset arena (RW, execute-never). Anything else — console RAM, peripherals, flash — traps as a recoverable MemManage fault. Regions are released on exit/crash.

## Documentation

Deep-dive docs live in `docu/`:
- `docu/API_README.md` — ConsoleAPI surface and module internals
- `docu/kernel.md` — game isolation, ground-up: privilege/stack model, the SVC syscall ABI, the SVC-in/PendSV-out context switch, MPU protection, pointer validation, fault recovery, interrupt priorities (with diagrams)
- `docu/renderer.md` — the renderer, ground-up: frame pipeline, the hot loop, and the 24→76 FPS optimization journey
- `docu/memory.md` — authoritative SRAM/CCM/flash map, game binary layout, EEPROM layout, linker scripts
- `docu/flasher.md` — ESP-01 flashing, ground-up: the esp-serial-flasher submodule + bare-metal vtable port, USART1 driver, bootstrap sequence, the flash flow, build integration, and troubleshooting
- `docu/espnow.md` — ESP-NOW local multiplayer (up to 4 consoles), ground-up: the dumb-ESP/smart-console split, the polled per-frame service exchange, the wire commands, the session protocol (discovery beacon, join handshake, heartbeat ping-pong, roster), the game-facing kernel API, and GameXO's host-authoritative netcode (with diagrams)
- `docu/bootloader.md` — power-fail-safe console OS self-flashing, ground-up: why a bootloader is needed, the flash partition, the stage→commit→apply flow, the readback-CRC verification, and the interruption-recovery guarantees
- `docu/HW.md` — Hardware schematics and full pinout
- `docu/game_template/README.md` — guide for creating new games + the full game-facing API reference, with a copyable `startup.s` template
- `docu/game_template/multiplayer.md` — guide for creating **multiplayer** games: the design philosophy (host-authoritative, best-effort idempotent state), the lobby→gameplay flow, the exact `mp*` call sequence, the per-frame network-flow timing diagrams (calls/waits, the async-TX-armed round-trip overlapping `render()`, end-to-end latency), the capability/throughput limits, and a dedicated **real-time / continuous-streaming** section (Bomberman/racer: decoupled network tick, tight state packing, client-side interpolation + prediction)

## Subsystems

### SD Card Stack
Four layers: **FatFs** (`ff.c`) → **disk I/O** (`diskio.c`) → **low-level driver** (`diskio_integration.c`) → **SDIO peripheral** (`sdio.c`). `sdInit()` sends CMD0/CMD8/ACMD41/CMD2/CMD3/CMD7/CMD16, switches to 4-bit bus mode, and sets the transfer clock to 2 MHz (`sdioRaiseClock`, conservative — cards spec ≥25 MHz; both 1 MHz and 4 MHz were tried and didn't resolve the sustained-write corruption seen on this board, which points to a board-level power/signal issue rather than the clock). `disk_write()` is implemented (`sdWriteSingleBlock`/`sdWriteMultipleBlocks`) and **reads every block back, comparing it to the source and retrying up to 3× before failing** (`verifyWrite` in `diskio.c`), so a flaky card can't silently store wrong bytes. FatFs is built read-write (`FF_FS_READONLY 0`), so deletes/writes work — e.g. a download whose CRC doesn't match the manifest removes the partial file. Card detect is on GPIO PD3 (active-low, `sdCardPresent()` in `gpio.c`). Content is organized into directories — created (only if missing) at every mount by `ensureDirs()` in `loader.c`, names in `Inc/sd_layout.h`: `Games/` (game `.bin` + `.pak`), `Settings/` (`server.txt`), `Firmware/` (`ESP01.bin`), `Manifests/` (the fetched remote `manifest.csv` + the local `downloaded.csv` diff record).

### Renderer
Scanline **sprite compositor** (`Console/Src/Renderer/renderer.c`) — full deep-dive in `docu/renderer.md`.
- **Screen**: 320×240, RGB565, fed to the ILI9341 over FSMC.
- **Model**: games submit per-layer arrays of `Sprite` (indexed-color image + `x/y/w/h`, draw order `z`, `flags`, `palette`) across three layers — `LAYER_BG`, `LAYER_FG`, `LAYER_UI`. Pixels are 2bpp or 4bpp planar tiles (slot 0 transparent unless `SPRITE_OPAQUE`); `SPRITE_FLIP_H/V` flip in place.
- **Pipeline** (`rendererRender()`): counting-sort sprites by `z` within each layer → bin them into 16-scanline chunks → composite each chunk back-to-front (painter's algorithm) into one of two scanline buffers → DMA the chunk to the panel while the next composites. Optional per-chunk background fill (`rendererSetBackground`).
- **Hot-path tricks**: opaque two-pixels-per-store blitter, a packed `s_pair` palette LUT, a per-frame decoded-tile cache, and a "byte is fully opaque" table — together taking a busy screen from **24 → 76 FPS** (see `docu/renderer.md` §8 and the `renderer-perf-findings` notes).
- **Public API**: `rendererInit` / `rendererClear` / `rendererSetBackground` / `rendererSubmitLayer` / `rendererRender`, plus `rendererGetWidthPixels` / `rendererGetHeightPixels` and `rendererSystemColor` (Pixel Forge system-palette index → RGB565). The whole surface is exposed to games through the `ConsoleAPI`; the `Sprite`/`Layer`/`SpriteFlags` types live in `Shared/Api/renderer_interface.h`. The renderer is compiled `-O3` even in debug builds (`#pragma GCC optimize`) since it is the per-frame hot path.
- **RAM cost**: the scanline buffers + per-chunk bins + z-sort pool + tile cache dominate `CONSOLE_RAM` (~35 KB); `tools/memory_analysis` reports the live budget.

### Joystick
Two analog joysticks with digital d-pads + 2 special buttons. Polled by TIM7 ISR every 10ms. Digital buttons: 5ms debounce on each of 10 GPIO pins (active-low with pull-ups). Analog axes: ADC1 channels 10–13 (PC0–PC3) in DMA circular mode, thresholded at 2048±1500 to produce 3-state output (Mid/Low/High) with 5ms debounce. Public API returns `bool` for buttons and `JoystickAxisState` (Off/Negative/Positive) for axes.

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
Four uses, all on DMA2: **ADC1 DMA** (Stream0 ch0, circular, 16-bit) transfers 4 ADC channels continuously; **FSMC DMA** (Stream6 ch0, memory-to-memory) bursts pixel data to the ILI9341 display for opaque tile rendering; and the **USART1/ESP-01 link** (`usart.c`) uses **RX DMA** (Stream2 ch4, circular ring) draining `USART1->DR` into a 2 KB ring continuously so no byte is lost even when an ISR stalls the consumer (this is what makes the high runtime baud safe), plus **TX DMA** (Stream7 ch4, per-transfer) streaming each frame out before the code waits on the USART shift register to empty.

### Network
ESP-01S on USART1 (PA9/PA10), **923076 baud runtime** (`NETWORK_UART_BAUD` = the exact 84MHz/91 STM32 BRR, ~8× faster than 115200). `usart.c` is 8N1 over **DMA**: RX is a continuously-running circular DMA ring and TX is per-transfer DMA, so a byte is never dropped even when an ISR briefly delays the consumer — which is what makes the high baud safe (an earlier polled reader overran at this rate). Firmware *flashing* is a separate path pinned to the ESP ROM bootloader's 115200 (`USART1_DEFAULT_BAUD`). The console talks to the ESP over a **framed request/response protocol** (`Shared/Esp01s/network_protocol.h`, shared by both sides): `0xA5 0x5A | type | len:u16 | payload | crc16`. The console is the master — it sends one command and reads exactly one response; a corrupted byte fails the per-frame CRC and the transaction is retried. Commands: PING, SCAN, CONNECT, STATUS, HTTP_OPEN/READ/CLOSE.
- **Console driver** (`Console/Src/Network/network.c`): `networkScan/Connect/IsConnected` + `networkHttpOpen/Read/Close`. `networkInit()` (called in `peripheralsInit`) selects the runtime baud and power-cycles the ESP via EN so it boots fresh; connection is on demand using saved credentials. Per-frame CRC-16 is the inline `np_crc16` in the shared header.
- **ESP diagnostics → SWO**: the ESP can emit `NP_RSP_LOG` frames (`{level, text}`) before its response; `npRecvFrame` buffers them and flushes to SWO on the **`LOGGER_ESP01`** channel only after the response is fully read (so the slow SWO `printf` never stalls the polled receiver mid-stream). The ESP calls `np::logf()` (`Esp01s/src/protocol.h`) inside its handlers — scan results with per-AP channel, connect status, HTTP status, etc.
- **Download engine** (`Console/Src/Network/downloader.c`): server base address from `Settings/server.txt` — auto-created with an example template (`host:port`, scheme optional) if missing, and editable in Settings; `downloaderFetchManifest()` parses the CSV manifest (and saves a copy to `Manifests/manifest.csv`); `downloaderFetchFile()` streams a file to the SD card chunk-by-chunk, computing a streaming CRC-32 (`crc32_update/final` in `Console/Src/Crc`) and verifying it against the manifest (deletes the file on mismatch), reporting progress + speed. Downloads land in per-category dirs (caller-chosen: `games`→`Games/`, `Firmware`→`Firmware/`). A successful download is recorded in a local manifest (`Manifests/downloaded.csv`: `path,crc32`) via `downloaderRecordDownload()`; `downloaderLoadDownloaded()` reads it back so Poll Updates can diff the server manifest against the last-downloaded state.
- **UI**: WiFi connectivity settings live under Settings → **WiFi** — *Networks* (`wifi_menu.c`) scans, picks an AP, takes the password on the on-screen keyboard (`keyboard.c`), connects, and persists creds to console settings (`ConsoleSettings.wifi_*`, settings v2); *Server address* edits `Settings/server.txt` via the same keyboard. The two firmware upgrades live under Settings → **Firmware** (*Upgrade OS* and *Upgrade WiFi module*), which share the modal flash UI in `flash_ui.c`. Main menu → **Poll Updates** (`remote_update.c`) lists the full manifest with the NEW/UPD/UpToDate diff — the ESP firmware is just another row — over a shared progress UI (`download_ui.c`). WiFi credentials are stored in plaintext in EEPROM.

The ESP-01S firmware is a separate PlatformIO target at **`Esp01s/`** (board `esp01_1m`, Arduino): it implements the protocol slave over `ESP8266WiFi`/`ESP8266HTTPClient`, with the on-board LED as a WiFi-status light. `src/` is split by subsystem — `protocol.{h,cpp}` (the `np::` framing layer, owns the TX/RX buffers), `wifi.{h,cpp}` (scan/connect/status + radio bring-up), `http.{h,cpp}` (HTTP GET open/read/close), `espnow_link.{h,cpp}` (ESP-NOW `MP_*` transport), and `main.cpp` (just `setup()` + the `loop()` command dispatch). It pins the regulatory domain to a real country code on channels 1–11 (`wifi_set_country` `"US"`/1-11) and scans passively: the ESP8266 PHY is unreliable on the band-edge channels 12-13-14 (a `phy_dig_spur_set` crash, and a world-`"00"`/1-13 config crash-loops boot), so APs on 12/13 aren't reachable — set such routers to ch 1-11. Build with `make esp`, stage into the update-server tree with `make deploy`. USART1 is also driven at 115200 by the ESP Flasher (a separate path). See `Esp01s/README.md` and `tools/update_server/README.md`.

**ESP-NOW multiplayer** (up to 4 consoles, local wireless, no AP) rides the same ESP-01 link as a second mode. The design splits cleanly: the ESP is a **dumb byte mover** (three new commands in `network_protocol.h` — `NP_CMD_MP_BEGIN`/`MP_END`/`MP_SERVICE`, proto v3 — that init/deinit ESP-NOW, send a batch, and drain a recv ring, tagging each inbound packet with its source MAC), while **all session logic lives on the console** in `Console/Src/Multiplayer/mp_session.c`: role/peer table, player-index assignment, discovery beacons, the join handshake, the heartbeat "ping-pong" liveness, the roster, and the inbound/outbound app mailboxes. To keep the master/slave UART invariant the console **polls**, with the `MP_SERVICE` round-trip **pipelined across the frame** so it overlaps the game's `render()` instead of busy-waiting the CPU: `mpSessionFlush()` *arms* the outbound batch over the **async TX** (`usartWriteBytesStart`) after `update()`, the reply streams into the RX DMA ring during `render()`, and `mpSessionCollect()` reads it at the start of the next frame (`Console/Src/Network/espnow_link.c` splits into `espnowLinkSend`/`espnowLinkCollect`; `network.c` into `networkTransactSend`/`networkTransactCollect`). Inbound is one frame old (~16 ms) and a game's `mpSend`/`mpReceive` stay non-blocking mailbox ops. WiFi/HTTP and the ESP flasher keep the blocking `usartWriteBytes`/`networkTransact`. Games drive the lobby themselves through 13 new SVC syscalls (`mpHostStart`/`mpJoinStart`/`mpScanHosts`/`mpJoin`/`mpSend`/`mpReceive`/…, ABI v4) — a host advertises the running `.bin`, a joiner only discovers hosts of the same game. The player's display name is a console-wide Setting (Settings → **Player Name**, persisted in `ConsoleSettings` v3, default UID-derived). `Apps/GameXO` gains a Single/Host/Join mode menu and **host-authoritative** netcode (`xo_net.c`): the host owns the board and broadcasts state snapshots; the client sends move intents. v1 ESP-NOW is unencrypted (LAN-toy threat model). Logs on the **`LOGGER_MP`** channel. Full deep-dive with diagrams in `docu/espnow.md`.

### ESP Flasher
Reflashes the ESP-01 firmware from the SD card (Settings → **Firmware → Upgrade WiFi module**). Built on the vendored `tools/esp-serial-flasher` submodule (HAL-free core; the bundled HAL `port/stm32_port.c` is **not** compiled). Our glue lives in `Console/Src/Flasher/`:
- `esp_flasher_port.c` — a bare-metal `esp_loader_port_ops_t` vtable over `usart.c` (polled byte I/O with timeouts) and the ESP-01 bootstrap pins exposed by `gpio.c` (`esp01SetEnable/Reset/Bootloader`: EN=PB10, RST=PB6, IO0=PC6, IO2=PC13). `enter_bootloader` drives IO0 low + pulses RST to enter the ROM download loader.
- `esp_flasher.c` — `espFlasherFlashFile(path, cb, ctx)`: streams the image off FatFs in 1 KB blocks, connects at 115200 with the ESP8266 RAM stub, programs at flash offset 0, MD5-verifies, then resets the ESP to boot the new firmware. Reports progress through the callback.
The UI (`MainMenu/wifi_update.c`) is a blocking modal (like launching a game): it looks for `Firmware/ESP01.bin` on the card, then **pre-verifies the image's CRC-32** against the value recorded when it was downloaded (`downloaded.csv`, matched by basename): on a mismatch it shows both CRCs and lets the user choose whether to flash anyway (Special Button 1) or cancel (Special Button 2). The flasher's post-flash MD5 only proves the ESP stored what was read off the card, so a flaky SD read could otherwise brick the ESP and still report success — but the image can also legitimately differ (a hand-copied file, or a stale record), so it's a warning, not a hard block (if there's no download record at all it proceeds with a logged warning). It then shows a progress bar, and on success **reboots the whole console** (`gameConsoleReboot()`) so the ESP comes back up on the new firmware via the normal boot bring-up; on failure it waits for Special Button 2. The image is **kept** on the card (in `Firmware/`, so it never clutters the game list) for re-flashing without re-downloading. Producing `ESP01.bin` (Arduino/PlatformIO) comes from `make esp`. The modal UI helpers (status line + progress bar, CRC-mismatch confirm, recorded-CRC lookup) are shared with the OS self-flasher in `MainMenu/flash_ui.c`. Logs on the `LOGGER_FLASHER` channel.

### Console OS Self-Flash (bootloader + staging)
The console can reflash **its own firmware** from `Firmware/Console.bin` (Settings → **Firmware → Upgrade OS**), made power-fail-safe by a small **bootloader** in flash sector 0 that the self-flash never overwrites. The flash partition is the single source of truth in `Console/Inc/Flasher/flash_map.h` and is mirrored by `linker/common.ld`: sector 0 (16K @ `0x08000000`) bootloader, sectors 1-5 (240K @ `0x08004000`) the app, sectors 6-7 (256K @ `0x08040000`) update staging. The app links at `0x08004000` and sets `SCB->VTOR` to that base in `SystemInit`.

- **Bootloader** (`Bootloader/`): a separate, minimal target (reuses `Console/startup.s`, the shared `crc.c`, `flash_ll.c`, and `logger.c`; its own `linker/bootloader.ld` + `trace.c`). It runs first on every reset: if staging holds a committed, self-consistent `OsStagingHeader` (`magic`/`size`/`image_crc32`/`header_crc32`), it erases the app region, copies the staged image in, **CRC-verifies the app by readback, and only then clears the pending flag (programs the magic to 0) and resets to boot it** — otherwise it validates and jumps to the existing app. Power-fail safe because the apply is idempotent: the staging copy is intact internal flash, so an interrupted apply just re-runs on the next boot until the readback verifies. The bootloader is never self-updated (flash it once over SWD); flash via `make -C Bootloader flash`, or `make flash` for bootloader + app. It traces every decision and apply step over **SWO on the `LOGGER_BOOT` channel** (reusing the console's `logger.c`; `trace.c` provides `swoInit`/`_write`/`getSysTime` for the bootloader's 16 MHz clock) — boot banner, pending-or-not, each sector erased, copy progress, and the readback-verify result.
- **App side** (`Console/Src/Flasher/os_flasher.c`, UI `MainMenu/os_update.c`): streams `Firmware/Console.bin` into staging (never touching the running OS), verifies the staged copy by readback CRC, compares it to the recorded download CRC (warn-on-mismatch like the ESP flow), then writes the staging header **magic-last** and reboots into the bootloader to apply. A card yank or power loss during the long SD transfer leaves the running OS intact (staging is scratch); only after the verified header commit does the irreversible swap happen — and that runs in the bootloader from reliable internal flash, never the SD card.
- **Low-level flash** (`Console/Src/Flasher/flash_ll.c`, shared by both): erase/program run from `.RamFunc` (SRAM) with interrupts masked — on this single-bank part any flash access stalls while a program/erase is in flight, so the routine driving it cannot be fetched from flash; callers orchestrate from flash but never erase the sector they execute from. Logs on `LOGGER_FLASHER`. Deploy stages the OS image with `make deploy` → `content/Firmware/Console.bin` (downloaded to `Firmware/Console.bin`).

### Logging
Severity-tagged logging over SWO/ITM (`Console/Src/Logger/logger.c`). Console code logs through macros:

```c
LOGGER_LOG_INFO(LOGGER_RENDERER, "%d sprites registered", sprite_count);
// -> [12345][I][RENDERER] 42 sprites registered    (tick / level / channel)
```

The first argument is a **channel** from `logger_config.h` — the macro uses it both as the per-channel level threshold and (stringized) as the printed tag, so the channel name comes for free. Levels are `LOGGER_LOG_{ERROR,WARN,INFO,DEBUG}`. Each channel is assigned its own level in `logger_config.h` (`#define LOGGER_SDIO LOGGER_LEVEL_DEBUG`, `#define LOGGER_RENDERER LOGGER_LEVEL_NONE`, …); a site survives only when its severity is `<=` both that channel's level **and** the global `LOGGER_MAX_LEVEL` ceiling. `LOGGER_LEVEL_NONE` silences a channel entirely; the ordering is `NONE < ERROR < WARN < INFO < DEBUG`. All operands are compile-time constants, so a site below its channel's level folds away entirely (format string included) — zero flash, zero cycles. `LOGGER_ENABLED 0` strips every site, and `LOGGER_MAX_LEVEL` is the one-line global override (e.g. drop to `LOGGER_LEVEL_WARN` to strip all INFO/DEBUG from a production build regardless of per-channel settings).

Enabled-path overhead is dominated by **bytes over SWO** — `ITM_SendChar` spin-waits ~5 µs/byte at the 2 MHz trace clock, which swamps the wrapper call, the level lookup, and the `LOGGER_` strip (~0.1 µs combined). The line is therefore kept short on purpose: raw tick, one-char level (`"EWID"[level]`), and the channel tag with its `LOGGER_` prefix stripped (printed in full, e.g. `JOYSTICK`). Output goes straight through `vprintf` (no intermediate buffer, no truncation of the message); requires `swoInit()` first (done in `peripheralsInit`).

Loaded games can't link the logger, so they log via the ConsoleAPI: `api.log("score %d", score)` routes to `loggerGameLog()` on the `LOGGER_GAME` channel (printed as `[..][I][GAME] score 5`). Game-local `printf` is unreliable — the game's `_write` has no backing `__io_putchar` — so `api.log` is the correct path for games.

**Convention — always log in the console OS.** When writing or modifying console firmware (anything under `Console/Src/`), add logging as part of the change; do not leave new code silent. The logger is compile-time gated per channel/level (a silenced site folds away to zero flash and zero cycles — verified), so logging is free in production builds and there is no cost argument for omitting it. Follow these rules:
- **Map to the right channel** from `logger_config.h` (e.g. `LOGGER_CORE`, `LOGGER_KERNEL`, `LOGGER_SDIO`, `LOGGER_EEPROM`, …). Add a new channel there if a subsystem has none.
- **Level discipline:** `INFO` for once-per-boot / lifecycle milestones (init done, game launched/exited); `DEBUG` for per-driver detail and frequent-but-cold events; `WARN` for recoverable anomalies (rejected input, validation failures); `ERROR` for hard failures. Aim for INFO to read as the session narrative on its own, with DEBUG as the layer beneath it.
- **Log at boundaries, not internals:** public-API entry/exit, state transitions, and every error/early-return path — not every internal step.
- **Never log on hot paths or in ISRs.** The renderer per-frame compositor, the 1 ms buzzer ISR (and `buzzerStop`, which it calls), the 10 ms joystick ISR, the per-syscall SVC dispatch, and PendSV teardown stay silent — a single SWO line (~5 µs/byte) there breaks audio/frame timing. Log their *init* and *lifecycle*, never their per-tick work.
- **Don't instrument** third-party code (`ff.c`, `ffunicode.c`), pure data tables (fonts), the SWO sink itself (`logger.c`, `syscalls.c` — recursion risk), or `faults.c` (it uses raw `printf` by design).

## Pixel Forge (graphics creator)

`tools/graphics/pixel_forge.py` — PyQt6 pixel-art editor for free-form pictures (any W×H, not tiles). Exports a `GfxAsset` `.bin` (header + palette + packed pixels) and a `.c` companion; format declared in `tools/graphics/gfx_asset.h` (magic "GFX1", 2bpp = 4 colors / 4bpp = 16 colors, slot 0 transparent, system palette indices 0–63). Qt-free core (`pixelforge/canvas.py`, `storage.py`, `history.py`) + GUI layer (`pixelforge/gui/`). Unlike `tools/music_creator/`, this tool is written to be maintainable — no "vibecoded" disclaimer headers. See `tools/graphics/README.md`.

## Music Creator (buzzer composer)

`tools/music_creator/music_creator.py` — PyQt6 piano-roll editor for composing buzzer music. Exports interleaved `uint16` pairs (frequency_hz, duration_ms), 4 bytes per note; frequency 0 = pause. Dual output: `.bin` (magic `"NOT1"`, loadable save state + runtime asset) and `.c` companion. Requires PyQt6, pygame, numpy, PyYAML. Qt-free core (`timeline.py`, `storage.py`, `audio.py`, `notes.py`, `history.py`, `constants.py`) + Qt GUI layer (`gui/piano_roll.py`, `gui/main_window.py`, `gui/theme.py`). Pitch/frequency mapping in `notes.yaml`. Example tracks in `Assets/Music/`. See `tools/music_creator/README.md`.

## Asset Packer

`tools/packer/packer.py` — CLI tool that bundles loose binary assets into a single `.pak` container and emits a C enum header. Input is a YAML manifest; output is `<name>.pak` + `<name>AssetEnum.h`. Binary format defined in `tools/packer/pak_format.h` (magic `"PAK1"`, CRC32-verified per-entry and whole-file). Modular Python package (`assetpacker/format.py`, `assetpacker/manifest.py`, `assetpacker/builder.py`, `assetpacker/verify.py`, `assetpacker/codegen.py`). The generated enum type is named after the output (e.g. `Level1AssetId`) so multiple pack headers can coexist. Blobs are stored verbatim (no compression). See `tools/packer/README.md` for the full binary layout and C usage example.

## Memory Analysis

`tools/memory_analysis/memory_analysis.py` — CLI that parses GNU LD `.map` files (post-link section sizes) and/or `.ld` linker scripts (region capacities, no build required) into a per-region RAM/CCM/flash usage report: per-section breakdown, percentages, NOLOAD/LMA awareness, and free-space projections. With no args it auto-discovers the Console + GameXO maps under `build/` and compares them side by side; `--json` for machine output, `--no-color`/`--quiet` for plain or summary-only. Standard-library only (Python 3.10+). Modular package (`memoryanalysis/ld_parser.py`, `map_parser.py`, `model.py`, `report.py`, `cli.py`). This is the tool that answers "how much RAM is left?" — the renderer's static buffers dominate `CONSOLE_RAM`. See `tools/memory_analysis/README.md`.

## Update Server

`tools/update_server/update_server.py` — a stdlib-only HTTP server that hosts updatable content the console will pull over WiFi (once the runtime network link exists): games + their `.pak`s, the ESP-01 firmware (`ESP01.bin`), and — later — console OS images. The content root has one subfolder per **category** (`games/`, `Firmware/` — the console OS image and the ESP-01 firmware share `Firmware/`); the category is the folder name. It serves `GET /manifest.csv` (generated live: `category,name,path,size,crc32,version`) and `GET /<path>` for the files. The manifest is **CSV on purpose** — trivial to parse on the STM32 with no JSON/YAML parser — and `crc32` is the zlib/IEEE CRC-32, bit-for-bit identical to the console's `crc32_calculate` and the packer, so the device recomputes it to confirm a clean download (and to detect changes). An optional `versions.csv` (`path,version`) supplies advisory versions (default 1). Modular package (`updateserver/catalog.py`, `manifest.py`, `crc.py`) + argparse/`http.server` frontend; `--generate` writes `manifest.csv` and exits, and while serving a background thread rewrites the on-disk `manifest.csv` every `--refresh` seconds (default 15, `0` disables) so `make deploy`'d files appear without a restart. Downloads are confined to the content root: `http.server` strips `..`/percent-encoding, the handler additionally resolves symlinks and `403`s anything outside the root, and the catalog skips escaping symlinks (so the manifest never lists or leaks files elsewhere). LAN/dev tool (no auth/TLS). See `tools/update_server/README.md`.

## Asset System

The pipeline is **author → pack → load**:
1. **Author** — graphics in Pixel Forge (`.bin`/`.c`, 2bpp/4bpp) and music in Music Creator (`.bin`/`.c`).
2. **Pack** — the packer (`tools/packer/`) bundles the loose `.bin` assets from a YAML manifest into one `<name>.pak` container plus a generated `<name>AssetEnum.h` of asset IDs.
3. **Load** — at runtime a game streams one asset by ID with `assetLoaderGetAssetData(id, buffer, size)` (and `assetLoaderGetAssetMetadata` / `assetLoaderGetAssetHeader`) into a buffer carved from the CCM `GAME_RAM_ASSET` arena, whose bounds the ConsoleAPI exposes as `ASSET_ARENA_START` / `ASSET_ARENA_END` / `ASSET_ARENA_SIZE` (a game never re-declares the linker symbols). Lazy loading lets total asset data exceed RAM. `AssetHeader` / `AssetMetaData` and the arena macros are declared in `Shared/Api/asset_interface.h`.

> **Status:** the `.pak` format, packer, CCM arena, and on-device loader are all implemented. When a game is loaded, `game_loader.c` derives the matching `<game>.pak` from the `.bin` name and binds it via `assetLoaderOpenPak()`; the file stays open for the game's lifetime and is closed on exit. Games then stream assets by id with `assetLoaderGetAssetData()`, which seeks into the bound pak, copies the blob into the game's buffer, and verifies its CRC32 (`crc32_calculate` in `Console/Src/Crc`). The older inline-asset macros (`DEFINE_ASSET_8/16/HEADER`) and the `DEFINE_TILE` tile encoder (whole `Shared/TileUtils/` dir) have been removed — graphics now come exclusively from Pixel Forge.

## Tools Architecture

The repo ships five Python tools that share one shape — a dependency-free **core** library holding the domain logic, wrapped by a thin frontend:
- **Pixel Forge** and **Music Creator** are PyQt6 desktop GUIs (Qt-free core + Qt GUI layer).
- **Asset Packer** and **Memory Analysis** are CLIs (core + argparse frontend).
- **Update Server** is a CLI/HTTP server (core + argparse/`http.server` frontend).

Shared conventions:
- **Qt-free / UI-free core** library with the domain logic (no frontend dependency)
- **Thin frontend** (Qt GUI or CLI) that imports the core
- **VS Code launch configs** in `.vscode/launch.json` for each tool
- **PyYAML** is the shared config/manifest format for the GUI tools and the packer (Memory Analysis and the Update Server are standard-library only)

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
- **Network**: ESP-01 on USART1 (PA9/PA10), 923076 baud runtime over DMA (115200 for flashing), controlled via PB10/PB6/PC6/PC13
- **Debug interface**: SWD (PA13/PA14) + SWO (PB3)

EEPROM layout: console partition 0x0000–0x3FFF (system header 0x0000, game directory 0x0100, console settings 0x1000), games partition 0x4000–0xFFFF (48 × 1KB save slots).

## Code Quality

Use good code practices, think about the architecture, write clean and maintainable code. Avoid patterns that look like generated or "vibecoded" code — this repo values deliberate, hand-crafted engineering.
