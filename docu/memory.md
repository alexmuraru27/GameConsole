# Memory Layout

The STM32F407VET6 provides **128 KB SRAM** at `0x20000000`, **64 KB CCM** at `0x10000000`, and **512 KB flash** at `0x08000000`. An external **AT24C512 EEPROM** (64 KB) sits on I2C1 for persistent settings.

## SRAM regions

Defined in `linker/common.ld`. There is no shared-RAM window: a game reaches the console only through SVC syscalls, so the old 2 KB `SHARED_RAM` was reclaimed into `CONSOLE_RAM`.

| Region         | Origin     | Size | Perms | Purpose                                                                           |
| -------------- | ---------- | ---- | ----- | --------------------------------------------------------------------------------- |
| CONSOLE_RAM    | 0x20000000 | 96K  | rw    | Console firmware .data, .bss, MSP (kernel) stack, PSP (console) stack, DMA buffers, renderer, FatFs, SDIO, audio |
| GAME_RAM       | 0x20018000 | 32K  | rwx   | Game .text, .rodata, .data, .bss, stack — loaded from .bin at runtime             |

Total: 96K + 32K = 128K ✓

```
 0x20000000 ┌────────────────────────┐ ─┐
            │ CONSOLE_RAM   96K       │  │ kernel .data/.bss, MSP + PSP stacks,
            │  (console only)         │  │ renderer buffers, DMA, FatFs, SDIO, audio
 0x20018000 ├────────────────────────┤ ─┤ ◀ 32K-aligned: one clean MPU region
            │ GAME_RAM      32K  rwx  │  │ loaded game: code + rodata + data + bss + stack
 0x20020000 └────────────────────────┘ ─┘ top of SRAM (game stack grows down from here)
```

`GAME_RAM` is deliberately a power-of-two size (32K) aligned to its size (`0x20018000`), so a single ARMv7-M MPU region confines an unprivileged game to it with no sub-region tricks. GameXO uses ~8K of it (≈4K code+data, 4K stack), leaving generous headroom.

## CCM (Core-Coupled Memory)

| Region         | Origin     | Size | Perms | Purpose                                |
| -------------- | ---------- | ---- | ----- | -------------------------------------- |
| GAME_RAM_ASSET | 0x10000000 | 64K  | rw    | Asset arena — .pak-loaded buffers.     |

**CCM is D-bus only on STM32F4 — it cannot execute code.** The game's `.text`, `.rodata`, `.data`, and `.bss` live in `GAME_RAM` (regular SRAM, on both I-bus and D-bus). CCM holds the asset arena (SDIO is PIO, so CPU copies FIFO → CCM works). CCM also **cannot do DMA** — any future DMA-backed I/O must use SRAM buffers.

## Flash

The 512 KB flash is partitioned for power-fail-safe OS self-flashing (see the OS Self-Flash subsystem in `CLAUDE.md`). The map is the single source of truth in `Console/Inc/Flasher/flash_map.h`, mirrored by `linker/common.ld` (app) and `linker/bootloader.ld`:

| Region        | Origin     | Sectors | Size | Perms | Purpose                                                          |
| ------------- | ---------- | ------- | ---- | ----- | --------------------------------------------------------------- |
| BOOTLOADER    | 0x08000000 | 0       |  16K | rx    | Self-flash bootloader: reset vector + the apply/verify logic. SWD-flashed once; never self-updated. |
| CONSOLE_FLASH | 0x08004000 | 1-5     | 240K | rx    | Console firmware / app: .isr_vector, .text, .rodata, LMA of .data. App sets `SCB->VTOR` here. |
| OS_STAGING    | 0x08040000 | 6-7     | 256K | rx    | OS-update staging scratch: `OsStagingHeader` + the new image, written before a verified, committed swap. |

STM32F407 sector geometry: sectors 0-3 = 16K, sector 4 = 64K, sectors 5-7 = 128K (single bank — any flash access stalls while a program/erase is in flight, so the erase/program primitives run from SRAM via `.RamFunc`).

```
            sector  size   region
 0x08000000 ┌─0────┬─16K─┐ BOOTLOADER  reset vector + apply/verify  (SWD only, never self-updated)
 0x08004000 ├─1────┼─16K─┤ ┐
 0x08008000 │ 2    │ 16K │ │
 0x0800C000 │ 3    │ 16K │ │ APPLICATION (console OS, 240K)  ── app links here; VTOR → 0x08004000
 0x08010000 │ 4    │ 64K │ │
 0x08020000 │ 5    │128K │ ┘
 0x08040000 ├─6────┼128K─┤ ┐ STAGING (256K)  ── new image staged + verified here before the swap
 0x08060000 │ 7    │128K │ ┘    [OsStagingHeader @ 0x08040000 | image @ +0x200]
 0x08080000 └──────┴─────┘ end of flash
```

The bootloader copies a committed staging image into the APPLICATION region and re-verifies it by readback before booting it. See `docu/bootloader.md` for the full self-flash flow and recovery guarantees.

The bootloader runs first on reset and either applies a committed staging image (erase app → copy → **readback-CRC verify** → clear pending → reset) or jumps to the validated app. The console firmware runs directly from flash at `0x08004000`; its `.data` section LMA is in flash (copied to CONSOLE_RAM by startup code), and `.bss` is zeroed at boot.


## Game binary layout

The `.bin` file on the SD card is a flat image of the game's `GAME_RAM` contents:

```
GAME_RAM  (in .bin)
├── .game_header      32 B     GameBinaryHeader (magic, ABI version, callbacks, stack-guard base)
├── .text                      game code
├── .rodata                    constants and strings
├── .data                      initialized globals
                               ── (above, NOLOAD, not in .bin) ──
├── .bss                       zero-initialized globals (zeroed by the bootstrap)
├── .stack_guard      256 B    no-access MPU guard band just below the PSP (overflow → fault)
└── ._user_heap_stack          heap (0) + stack, growing down from the top of GAME_RAM
```

`.bss`, `.stack_guard`, and `.asset_area` are `NOLOAD` — they occupy zero bytes in the `.bin` file. Because the game is RAM-resident, every section is linked at its final `GAME_RAM` address, so copying the whole image to `GAME_RAM` lands each section in place: no per-section table and no separate LMA copy. The `.stack_guard` band is 256-aligned/256-sized so the kernel can map it as one ARMv7-M MPU region; its base is published in the header (`stack_guard`).

### GameBinaryHeader

A 32-byte prefix at offset 0 of the binary. The loader reads it, validates the game, and the kernel drives the named callbacks — it needs nothing else (the game manages its own `.bss` in the bootstrap):

| Field          | Meaning                                                       |
| -------------- | ------------------------------------------------------------ |
| `magic`        | `0x47414D45` ("GAME")                                         |
| `abi_version`  | must equal `CONSOLE_ABI_VERSION`, or the loader refuses it    |
| `entry_point`  | `_game_start()` — one-time C-runtime bootstrap (zero `.bss`, run ctors) |
| `frame_return` | `_game_return()` — return trampoline the kernel installs as each callback's LR |
| `init`         | game init — the OS calls it once after the bootstrap         |
| `update`       | game update — the OS calls it every frame                    |
| `render`       | game render — the OS calls it every frame                    |
| `stack_guard`  | base of the 256-byte stack-overflow guard band (the `.stack_guard` section, just below the descending PSP); the kernel maps it as a no-access MPU region |

`entry_point`/`frame_return`/`stack_guard` are filled by `DECLARE_GAME_HEADER` from the shared syscall library + app linker script; the game only names `init`/`update`/`render`.

### Loading sequence

1. Read the 32-byte header; reject on bad magic or mismatched ABI version
2. Copy the whole `.bin` to `GAME_RAM` base (`.text`/`.rodata`/`.data` land at their linked addresses)
3. `kernelRunGame()` programs the MPU regions, then drives the game **OS-first**: it invokes each callback by building a fresh **unprivileged** PSP exception frame at the top of `GAME_RAM` and entering via an SVC (`SYS_INVOKE`), parking the console's context on the MSP
4. The first invoke runs `_game_start` (zero `.bss`, `__libc_init_array`), the second runs `init`; each returns to the OS via the `_game_return` trampoline (`SYS_FRAME_DONE`), leaving the MPU/session up
5. The OS then loops `update`/`render` uncapped (servicing the console between frames; it does not pace — a game self-paces with `delay()` if it wants a fixed rate). The game ends the session with `gameExit()` (an `SVC` / `SYS_EXIT`), or crashes — either way **PendSV** switches back to the parked console context, releases the MPU regions, and resumes the loader

The game runs **unprivileged on PSP** (confined to `GAME_RAM` + the CCM asset arena by the MPU); the console and all exception handlers run privileged on MSP. A game fault is caught, decoded over SWO, and recovered from — control returns to the menu, which shows a "crashed" banner. See the Kernel subsystem in `CLAUDE.md` for the switch mechanics.

### Why game code is in SRAM, not CCM

On the STM32F4, CCM is connected to the D-bus only — **it cannot execute code**.
Attempting to fetch an instruction from CCM causes an IBUSERR (instruction bus error)
HardFault. This is a hardware limitation, not configurable.

The fix: game `.text`, `.rodata`, `.data`, `.bss`, and stack all live in `GAME_RAM`
(regular SRAM, on both I-bus and D-bus). CCM is used for the asset arena, since
SDIO reads are PIO (CPU copies the FIFO to the buffer) — no DMA required.

## Asset system

Games ship as **two files** on the SD card:

| File         | Contents                                                                   |
| ------------ | -------------------------------------------------------------------------- |
| `GameXO.bin` | Header + code + rodata + data (SRAM only, no assets)                        |
| `GameXO.pak` | All tiles, audio, sprites in a [PAK1 container](../tools/packer/README.md) |

Assets are **lazily loaded** at runtime. The game calls `assetLoaderGetAssetData(id, buffer, size)` with a buffer carved from the 64 KB `GAME_RAM_ASSET` arena (in CCM). The loader seeks the `.pak` file, walks the `PakEntry` table to find the matching ID, and copies the blob into the buffer. Nothing is pre-loaded — the game manages its own working set.

> **Status:** implemented. The game loader binds `<game>.pak` (derived from the `.bin` name) when a game starts and keeps it open for the game's lifetime; `assetLoaderGetAssetData()` resolves the `PakEntry` by id, copies the blob into the caller's buffer, and verifies its CRC32 before returning.

The [Asset Packer](../tools/packer/README.md) bundles loose binary assets from a YAML manifest and emits:
- `<name>.pak` — binary container with CRC32-verified entries
- `<name>AssetEnum.h` — C enum mapping asset names to IDs

## EEPROM layout

AT24C512 (64 KB) on I2C1 at 400 kHz (`0x50`). Managed by `settings_storage.c`, packed flat with no arbitrary padding. Both the console entity and each game entity are exactly **2048 B (2 KB)**. Every persisted record ends in a CRC-16-CCITT (polynomial `0x1021`, initial `0xFFFF`).

| Offset | Size   | Content                                                                        |
| ------ | ------ | ------------------------------------------------------------------------------ |
| 0x0000 | 0x0100 | **System header** — magic/version, game count, monotonic write sequence, CRC   |
| 0x0100 | 0x0845 | **Game directory** — 29 × `GameDirectoryEntry` (73 B each: name key, state, write seq, CRC) |
| 0x0945 | 0x0800 | **Console settings** — one `ConsoleSettingsEntity` (2 KB)                      |
| 0x1145 | 0xE800 | **Game data** — 29 slots × 2048 B each; directory entry *i* ↔ data slot *i*  |
| 0xF945 | 0x06BB | (unused — not enough for another full slot)                                    |

Each data slot holds a `GameDataEntity` (version, size, ≤2042 B data, CRC).

Game saves are keyed by the game's `.bin` name (extension stripped, matched case-insensitively). Binding is automatic — there is no `has_settings` header flag: the loader binds a slot for every game as it loads (`settingsStorageBindGame`), and the 2 KB slot is allocated lazily on the game's first write (a game that never writes never consumes a slot). The running game reads/writes it through the settings `ConsoleAPI`. When all 29 slots are taken, writes return `STORAGE_FULL` — nothing is evicted automatically; callers free space via the list / delete / evict-oldest APIs. Corrupt directory entries or data slots are freed automatically on init by `settingsStorageCleanupCorrupted()`.

## Linker scripts

All linker scripts live in `linker/`.

| File                 | Used by          | Purpose                                                                            |
| -------------------- | ---------------- | ---------------------------------------------------------------------------------- |
| `linker/common.ld`   | Both             | MEMORY region definitions, the `.game_header` output section, region-bound symbols (`__game_ram_start/size`, …) the kernel uses for the MPU and pointer validation |
| `linker/console.ld`  | Console firmware | `.isr_vector`, `.text`, `.rodata` → flash; `.data`, `.bss` → CONSOLE_RAM           |
| `linker/app.ld`      | Games / apps     | `.text`, `.rodata`, `.data`, `.bss`, `.stack_guard`, `._user_heap_stack` → GAME_RAM; `.asset_area` → GAME_RAM_ASSET |
| `linker/bootloader.ld` | Bootloader     | standalone sector-0 link map (its own MEMORY block, no INCLUDE) |

`linker/common.ld` is included by `app.ld` and `console.ld` via `INCLUDE "common.ld"`, resolved from the linker search path (the Makefiles pass `-L .../linker`). The ASSERTs validate that `GAME_RAM` stays 32K-aligned at `0x20018000` (so it is a single clean MPU region) and that the SRAM region sizes sum to 128K.
