# Memory Layout

The STM32F407VET6 provides **128 KB SRAM** at `0x20000000`, **64 KB CCM** at `0x10000000`, and **512 KB flash** at `0x08000000`. An external **AT24C512 EEPROM** (64 KB) sits on I2C1 for persistent settings.

## SRAM regions

Defined in `common.ld`.

| Region         | Origin     | Size | Perms | Purpose                                                                           |
| -------------- | ---------- | ---- | ----- | --------------------------------------------------------------------------------- |
| SHARED_RAM     | 0x20000000 | 2K   | rw    | `ConsoleAPIHeader` struct — games read it at runtime                              |
| CONSOLE_RAM    | 0x20000800 | 82K  | rw    | Console firmware .data, .bss, stack, heap, DMA buffers, renderer, FatFs, SDIO, audio |
| GAME_RAM       | 0x20015000 | 44K  | rwx   | Game .text, .rodata, .data, .bss, stack, heap — loaded from .bin at runtime       |

Total: 2K + 82K + 44K = 128K ✓

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
├── .game_header      ~52 B    GameBinaryHeader (magic, boundary symbols, entry point)
├── .text                    game code
├── .rodata                  constants and strings
├── .data                    initialized globals
```

`.bss` and `.asset_area` are `NOLOAD` — they occupy zero bytes in the `.bin` file.

### GameBinaryHeader

Placed in `.game_header` at the start of the binary. The loader reads it to know where to copy each section:

| Field                 | Meaning                      |
| --------------------- | ---------------------------- |
| `magic`               | `0x47414D45` ("GAME")        |
| `header_start / end`  | Boundary of this struct      |
| `text_start / end`    | Code region (SRAM)           |
| `ro_data_start / end` | Read-only data (SRAM)        |
| `data_start / end`    | Initialized data (SRAM)      |
| `bss_start / end`     | Zero-fill region (SRAM)      |
| `stack_top`           | Initial PSP stack pointer (top of GAME_RAM) |
| `entry_point`         | Function pointer to `_game_start()` |

The loader computes file offsets as `region_addr - header_start` — this works because the `.bin` starts at `header_start` and all regions are contiguous.

### Loading sequence

1. Read header from the first `sizeof(GameBinaryHeader)` bytes of the `.bin`
2. `memset(bss_start, 0, bss_end - bss_start)` — zero BSS in SRAM
3. Copy `.text`, `.rodata`, `.data` from the `.bin` into their SRAM target addresses
4. Set PSP to `stack_top` (top of GAME_RAM), set `CONTROL[1]` to use PSP in Thread mode
5. `game_entry()` — jump to `entry_point` (`_game_start` in startup.s)
6. `_game_start` pushes LR, calls `__libc_init_array`, calls `main()`, pops PC
7. Game returns to the loader; loader clears `CONTROL[1]` (back to MSP) and closes file

The game runs on PSP (GAME_RAM) while the console and all exception handlers use MSP (CONSOLE_RAM).
If the game faults, the CPU switches to MSP for the handler — the game's
PSP stack is preserved untouched for post-mortem inspection.

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

AT24C512 (64 KB) on I2C1 at 400 kHz (`0x50`). Managed by `settings_storage.c` with CRC-16 protection.

| Offset | Size   | Content                                                                      |
| ------ | ------ | ---------------------------------------------------------------------------- |
| 0x0000 | 0x0200 | **System header** — version magic, entry count, CRC                          |
| 0x0200 | 0x01FE | **Directory** — 30 entries × 17 bytes (game ID, active flag, CRC per entry)  |
| 0x0400 | 0x0400 | **Console settings** — `ConsoleSettings` struct (audio enabled flag)         |
| 0x0800 | 0x7800 | **Data blocks** — 30 × 1024-byte `SettingsEntity` slots (version, data, CRC) |

All writes are CRC-16-CCITT validated (polynomial `0x1021`, initial `0xFFFF`). Corrupt directory entries or data blocks are auto-cleaned on init by `sanityCleanup()`.

## Linker scripts

| File         | Used by          | Purpose                                                                            |
| ------------ | ---------------- | ---------------------------------------------------------------------------------- |
| `common.ld`  | Both             | MEMORY region definitions, `.game_console_api` and `.game_header` output sections  |
| `console.ld` | Console firmware | `.isr_vector`, `.text`, `.rodata` → flash; `.data`, `.bss` → CONSOLE_RAM           |
| `game.ld`    | Games            | `.text`, `.rodata`, `.data`, `.bss`, `._user_heap_stack` → GAME_RAM; `.asset_area` → GAME_RAM_ASSET |

`common.ld` is included by both linker scripts via `INCLUDE "../common.ld"`. The ASSERTS validate that regions don't overlap and sizes sum to 128K.
