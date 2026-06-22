# Memory Layout

The STM32F407VET6 provides **128 KB SRAM** at `0x20000000`, **64 KB CCM** at `0x10000000`, and **512 KB flash** at `0x08000000`. An external **AT24C512 EEPROM** (64 KB) sits on I2C1 for persistent settings.

## SRAM regions

Defined in `common.ld`.

| Region         | Origin     | Size | Perms | Purpose                                                                           |
| -------------- | ---------- | ---- | ----- | --------------------------------------------------------------------------------- |
| SHARED_RAM     | 0x20000000 | 2K   | rw    | Read-only console-info page (version, screen dims) games may map RO               |
| CONSOLE_RAM    | 0x20000800 | 94K  | rw    | Console firmware .data, .bss, MSP (kernel) stack, PSP (console) stack, DMA buffers, renderer, FatFs, SDIO, audio |
| GAME_RAM       | 0x20018000 | 32K  | rwx   | Game .text, .rodata, .data, .bss, stack — loaded from .bin at runtime             |

Total: 2K + 94K + 32K = 128K ✓

`GAME_RAM` is deliberately a power-of-two size (32K) aligned to its size (`0x20018000`), so a single ARMv7-M MPU region confines an unprivileged game to it with no sub-region tricks. GameXO uses ~14K of it (≈5K code+data, 4K stack), leaving generous headroom.

## CCM (Core-Coupled Memory)

| Region         | Origin     | Size | Perms | Purpose                                |
| -------------- | ---------- | ---- | ----- | -------------------------------------- |
| GAME_RAM_ASSET | 0x10000000 | 64K  | rw    | Asset arena — .pak-loaded buffers.     |

**CCM is D-bus only on STM32F4 — it cannot execute code.** The game's `.text`, `.rodata`, `.data`, and `.bss` live in `GAME_RAM` (regular SRAM, on both I-bus and D-bus). CCM holds the asset arena (SDIO is PIO, so CPU copies FIFO → CCM works). CCM also **cannot do DMA** — any future DMA-backed I/O must use SRAM buffers.

## Flash

| Region        | Origin     | Size | Perms | Purpose                                                     |
| ------------- | ---------- | ---- | ----- | ----------------------------------------------------------- |
| CONSOLE_FLASH | 0x08000000 | 512K | rx    | Console firmware: .isr_vector, .text, .rodata, LMA of .data |

The console firmware runs directly from flash. Its `.data` section LMA is in flash (copied to CONSOLE_RAM by startup code), and `.bss` is zeroed at boot.


## Game binary layout

The `.bin` file on the SD card is a flat image of the game's `GAME_RAM` contents:

```
GAME_RAM  (in .bin)
├── .game_header      28 B     GameBinaryHeader (magic, ABI version, callbacks)
├── .text                      game code
├── .rodata                    constants and strings
├── .data                      initialized globals
```

`.bss` and `.asset_area` are `NOLOAD` — they occupy zero bytes in the `.bin` file. Because the game is RAM-resident, every section is linked at its final `GAME_RAM` address, so copying the whole image to `GAME_RAM` lands each section in place: no per-section table and no separate LMA copy.

### GameBinaryHeader

A 28-byte prefix at offset 0 of the binary. The loader reads it, validates the game, and the kernel drives the named callbacks — it needs nothing else (the game manages its own `.bss` in the bootstrap):

| Field          | Meaning                                                       |
| -------------- | ------------------------------------------------------------ |
| `magic`        | `0x47414D45` ("GAME")                                         |
| `abi_version`  | must equal `CONSOLE_ABI_VERSION`, or the loader refuses it    |
| `entry_point`  | `_game_start()` — one-time C-runtime bootstrap (zero `.bss`, run ctors) |
| `frame_return` | `_game_return()` — return trampoline the kernel installs as each callback's LR |
| `init`         | game init — the OS calls it once after the bootstrap         |
| `update`       | game update — the OS calls it every frame                    |
| `render`       | game render — the OS calls it every frame                    |

`entry_point`/`frame_return` are filled by `DECLARE_GAME_HEADER` from the shared syscall library; the game only names `init`/`update`/`render`.

### Loading sequence

1. Read the 28-byte header; reject on bad magic or mismatched ABI version
2. Copy the whole `.bin` to `GAME_RAM` base (`.text`/`.rodata`/`.data` land at their linked addresses)
3. `kernelRunGame()` programs the MPU regions, then drives the game **OS-first**: it invokes each callback by building a fresh **unprivileged** PSP exception frame at the top of `GAME_RAM` and entering via an SVC (`SYS_INVOKE`), parking the console's context on the MSP
4. The first invoke runs `_game_start` (zero `.bss`, `__libc_init_array`), the second runs `init`; each returns to the OS via the `_game_return` trampoline (`SYS_FRAME_DONE`), leaving the MPU/session up
5. The OS then loops `update`/`render` (servicing the console + pacing the frame in between). The game ends the session with `gameExit()` (an `SVC` / `SYS_EXIT`), or crashes — either way **PendSV** switches back to the parked console context, releases the MPU regions, and resumes the loader

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

Game saves are keyed by the game's `.bin` name (extension stripped, matched case-insensitively). A game declares `has_settings` in its binary header; the loader then binds a slot for it (created on first need) and the running game reads/writes it through the settings `ConsoleAPI`. When all 48 slots are taken, writes return `STORAGE_FULL` — nothing is evicted automatically; callers free space via the list / delete / evict-oldest APIs. Corrupt directory entries or data slots are freed automatically on init by `settingsStorageCleanupCorrupted()`.

## Linker scripts

| File         | Used by          | Purpose                                                                            |
| ------------ | ---------------- | ---------------------------------------------------------------------------------- |
| `common.ld`  | Both             | MEMORY region definitions, the `.game_header` output section, region-bound symbols (`__game_ram_start/size`, …) the kernel uses for the MPU and pointer validation |
| `console.ld` | Console firmware | `.isr_vector`, `.text`, `.rodata` → flash; `.data`, `.bss` → CONSOLE_RAM           |
| `game.ld`    | Games            | `.text`, `.rodata`, `.data`, `.bss`, `._user_heap_stack` → GAME_RAM; `.asset_area` → GAME_RAM_ASSET |

`common.ld` is included by both linker scripts via `INCLUDE "../common.ld"`. The ASSERTs validate that `GAME_RAM` stays 32K-aligned at `0x20018000` (so it is a single clean MPU region) and that the SRAM region sizes sum to 128K.
