# Memory Layout

The STM32F407VET6 provides **128 KB SRAM** at `0x20000000`, **64 KB CCM** at `0x10000000`, and **512 KB flash** at `0x08000000`. An external **AT24C512 EEPROM** (64 KB) sits on I2C1 for persistent settings.

## SRAM regions

Defined in `common.ld`.

| Region         | Origin     | Size | Perms | Purpose                                                                           |
| -------------- | ---------- | ---- | ----- | --------------------------------------------------------------------------------- |
| SHARED_RAM     | 0x20000000 | 2K   | rw    | `ConsoleAPIHeader` struct — games read it at runtime                              |
| CONSOLE_RAM    | 0x20000800 | 62K  | rwx   | Console firmware .data, .bss, stack, heap, all DMA buffers, FatFs, SDIO, audio    |
| GAME_RAM_ASSET | 0x20010000 | 64K  | rw    | Runtime asset arena — game carves buffers here for .pak loads, zero bytes in .bin |

Total: 2K + 62K + 64K = 128K ✓

## CCM (Core-Coupled Memory)

| Region       | Origin     | Size | Perms | Purpose                                |
| ------------ | ---------- | ---- | ----- | -------------------------------------- |
| GAME_RAM_CCM | 0x10000000 | 64K  | rwx   | Entire game binary lives and runs here |

CCM is tightly coupled to the Cortex-M4 core (zero wait states, no bus contention with DMA). The game's `.text`, `.rodata`, `.data`, `.bss`, heap, and stack all live here. Note: CCM **cannot do DMA** — any game data that needs DMA must go through the SRAM asset arena.

## Flash

| Region        | Origin     | Size | Perms | Purpose                                                     |
| ------------- | ---------- | ---- | ----- | ----------------------------------------------------------- |
| CONSOLE_FLASH | 0x08000000 | 512K | rx    | Console firmware: .isr_vector, .text, .rodata, LMA of .data |

The console firmware runs directly from flash. Its `.data` section LMA is in flash (copied to CONSOLE_RAM by startup code), and `.bss` is zeroed at boot.


## Game binary layout

The `.bin` file on the SD card is a flat image of the game's CCM contents:

```
GAME_RAM_CCM  (in .bin)
├── .game_header      ~44 B    GameBinaryHeader (magic, boundary symbols, entry point)
├── .text                    game code
├── .rodata                  constants and strings
├── .data                    initialized globals
```

`.bss`, `._user_heap_stack`, and `.asset_area` are `NOLOAD` — they occupy zero bytes in the `.bin` file.

### GameBinaryHeader

Placed in `.game_header` at the start of the binary. The loader reads it to know where to copy each section:

| Field                 | Meaning                      |
| --------------------- | ---------------------------- |
| `magic`               | `0x47414D45` ("GAME")        |
| `header_start / end`  | Boundary of this struct      |
| `text_start / end`    | Code region (CCM)            |
| `ro_data_start / end` | Read-only data (CCM)         |
| `data_start / end`    | Initialized data (CCM)       |
| `bss_start / end`     | Zero-fill region (CCM)       |
| `entry_point`         | Function pointer to `main()` |

The loader computes file offsets as `region_addr - header_start` — this works because the `.bin` starts at `header_start` and all regions are contiguous.

### Loading sequence

1. Read header from the first `sizeof(GameBinaryHeader)` bytes of the `.bin`
2. `memset(bss_start, 0, bss_end - bss_start)` — zero BSS in CCM
3. Copy `.text`, `.rodata`, `.data` from the `.bin` into their CCM target addresses
4. `game_entry()` — jump to `entry_point` in CCM
5. Game returns when Special Button 2 is pressed

## Asset system

Games ship as **two files** on the SD card:

| File         | Contents                                                                   |
| ------------ | -------------------------------------------------------------------------- |
| `GameXO.bin` | Header + code + rodata + data (CCM only, no assets)                        |
| `GameXO.pak` | All tiles, audio, sprites in a [PAK1 container](../tools/packer/README.md) |

Assets are **lazily loaded** at runtime. The game calls `assetLoaderGetAssetData(id, buffer, size)` with a buffer carved from the 64 KB `GAME_RAM_ASSET` arena. The loader seeks the `.pak` file, walks the `PakEntry` table to find the matching ID, and copies the blob into the buffer. Nothing is pre-loaded — the game manages its own working set.

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
| `game.ld`    | Games            | `.text`, `.rodata`, `.data`, `.bss` → GAME_RAM_CCM; `.asset_area` → GAME_RAM_ASSET |

`common.ld` is included by both linker scripts via `INCLUDE "../common.ld"`. The ASSERTS validate that regions don't overlap and sizes sum to 128K.
