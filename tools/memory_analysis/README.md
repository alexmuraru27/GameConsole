# Memory Analysis

A linker map and linker script analyzer for the GameConsole project.  Parses GNU LD ``.map`` files and ``.ld`` linker scripts to produce a detailed RAM / ROM / CCM usage report with per-section breakdowns, percentages, and free-space projections.

## Features

- **Dual input sources** — parse ``.map`` files for actual post-link section sizes, or ``.ld`` scripts for theoretical region capacities (no build required).
- **Per-section breakdown** — every linker output section (``.text``, ``.rodata``, ``.data``, ``.bss``, …) is listed with its size, address, and region placement.
- **NOLOAD awareness** — ``.bss``, ``._user_heap_stack``, and ``.asset_area`` are flagged as NOLOAD and included in runtime RAM usage but excluded from the binary image.
- **LMA tracking** — sections whose load address differs from their virtual address (e.g. ``.data`` init image in flash) are shown with their LMA and a synthetic entry in the flash region.
- **Cross-application comparison** — when multiple map files are loaded (Console + GameXO), a side-by-side summary table compares every region.
- **Colour terminal output** with Unicode bar charts; ``--no-color`` disables ANSI escapes.
- **JSON export** (``--json``) for machine consumption.
- **Quiet mode** (``--quiet``) prints one line per region.

## Requirements

- Python 3.10+ (standard library only — no external packages).

## Usage

```bash
# Auto-discover Console and GameXO .map files from default build locations
python3 tools/memory_analysis/memory_analysis.py

# Analyse a single map file
python3 tools/memory_analysis/memory_analysis.py --map build/GameXO/GameXO.map

# Compare two builds side by side
python3 tools/memory_analysis/memory_analysis.py --map build/Console/GameConsole.map build/GameXO/GameXO.map

# Parse linker scripts for theoretical region capacities (no build needed)
python3 tools/memory_analysis/memory_analysis.py --ld game.ld --name "GameXO (theoretical)"

# JSON output
python3 tools/memory_analysis/memory_analysis.py --json

# Quiet mode — one line per region
python3 tools/memory_analysis/memory_analysis.py --quiet
```

### Flags

| Flag | Description |
|---|---|
| ``--map PATH …`` | One or more ``.map`` files. Each becomes a separate application in the report. |
| ``--ld PATH …`` | One or more ``.ld`` linker scripts. Parsed together for region capacities (no sections). |
| ``--name NAME`` | Application name for ``--ld`` output (default: "Linker Script"). |
| ``--json`` | Output as JSON instead of a terminal report. |
| ``--quiet``, ``-q`` | Condensed output — one line per region, per app. |
| ``--no-color`` | Disable ANSI colour codes (useful when piping). |

When run with **no arguments** the tool auto-discovers ``build/Console/GameConsole.map`` and ``build/GameXO/GameXO.map`` from the project root.

## Output example

```
╔══════════════════════════════════════════════════════════════════════╗
║              GAMECONSOLE MEMORY ANALYSIS                            ║
╚══════════════════════════════════════════════════════════════════════╝

  ▸ Console  (.bin: 30.6 KB)
  ──────────────────────────────────────────────────────────────────

    CONSOLE_FLASH  (512.0 KB total, xr)
      isr_vector                  392 B  (  0.1%)
      text                     26,120 B  (  5.0%)
      rodata                    4,696 B  (  0.9%)
      ARM.exidx                     8 B  (  0.0%)
      data (LMA)                   92 B  (  0.0%)
      ────────────────────────────────────────────────────
      Used                     31,308 B  (  6.0%)  [█░░░░░░░░░░░░░░░░░░░]
      Free                    492,980 B  ( 94.0%)

    CONSOLE_RAM  (62.0 KB total, xrw)
      data                         92 B  (  0.1%) (LMA: 0x080079F8)
      bss                      19,176 B  ( 30.2%) [NOLOAD]
      _user_heap_stack          1,028 B  (  1.6%) [NOLOAD]
      ────────────────────────────────────────────────────
      Used                     20,296 B  ( 32.0%)  [██████░░░░░░░░░░░░░░]
      Free                     43,192 B  ( 68.0%)

  ▸ GameXO  (.bin: 8.0 KB)
  ──────────────────────────────────────────────────────────────────

    GAME_RAM_CCM  (64.0 KB total, xrw)
      game_header                  48 B  (  0.1%)
      text                      7,972 B  ( 12.2%)
      rodata                      104 B  (  0.2%)
      ARM.exidx                     8 B  (  0.0%)
      data                         96 B  (  0.1%)
      bss                         496 B  (  0.8%) [NOLOAD]
      _user_heap_stack          1,028 B  (  1.6%) [NOLOAD]
      ────────────────────────────────────────────────────
      Used                      9,752 B  ( 14.9%)  [███░░░░░░░░░░░░░░░░░]
      Free                     55,784 B  ( 85.1%)

    GAME_RAM_ASSET  (64.0 KB total, rw)
      asset_area               65,536 B  (100.0%) [NOLOAD]
      ────────────────────────────────────────────────────
      Used                     65,536 B  (100.0%)  [████████████████████]
      Free                          0 B  (  0.0%)

  ▸ Cross-Application Summary
  ──────────────────────────────────────────────────────────────────

        Region                Console               GameXO                  Capacity
    ────────────────────────────────────────────────────────────────────────────────────
    CONSOLE_FLASH          30.6 KB ( 6.0%)            N/A                 512.0 KB
    CONSOLE_RAM            19.8 KB (32.0%)            N/A                  62.0 KB
    GAME_RAM_CCM               N/A                 9.5 KB (14.9%)          64.0 KB
    GAME_RAM_ASSET             N/A                64.0 KB (100.0%)          64.0 KB
```

## Architecture

The tool follows the same layered pattern as Pixel Forge and Music Creator — a Qt-free core library with a thin CLI layer:

```
tools/memory_analysis/
├── memory_analysis.py        # Thin launcher — imports and runs cli.main()
├── memoryanalysis/           # Qt-free core package
│   ├── __init__.py           # Package metadata
│   ├── __main__.py           # python -m memoryanalysis support
│   ├── cli.py                # argparse CLI + auto-discovery logic
│   ├── model.py              # Data classes: MemoryRegion, SectionInfo, AppMemory
│   ├── utils.py              # Pure helpers: fmt_bytes, parse_hex_or_k, bar charts
│   ├── ld_parser.py          # GNU LD linker script parser (MEMORY regions)
│   ├── map_parser.py         # GNU LD map file parser (section sizes + placements)
│   └── report.py             # Terminal report + JSON output formatting
└── README.md                 # This file
```

### Data flow

```
.ld files ──▶ ld_parser.py  ──▶ MemoryRegion (capacities)
                                   │
.map files ─▶ map_parser.py ──▶ AppMemory ◀── SectionInfo[] (actual usage)
                                   │
                                   ▼
                              report.py  ──▶ terminal / JSON
```

### Key types

| Type | Source | Purpose |
|---|---|---|
| ``MemoryRegion`` | ``ld_parser`` or ``map_parser`` | Named address range with capacity and attributes (e.g. ``CONSOLE_FLASH``, 512K, ``rx``) |
| ``SectionInfo`` | ``map_parser`` | One linker output section — name, VMA, size, LMA, NOLOAD flag, and the region it was placed in |
| ``AppMemory`` | assembled by ``map_parser`` | Regions + sections + binary path for one application |

### LD parser capabilities

The LD script parser handles the subset of GNU LD syntax used by the GameConsole linker scripts:

- ``MEMORY { … }`` block extraction
- ``INCLUDE "…"`` directives (resolved recursively with cycle detection)
- Simple constant expressions: ``NAME = value;``, ``ORIGIN = expr, LENGTH = expr``
- Literals: hex (``0x…``), decimal, ``K`` / ``M`` suffixes
- Operators: ``+``, ``-``, ``*``, ``/``
- ``INCLUDE`` path resolution follows GNU LD behaviour: CWD first, then script directory, then project-root heuristic

For fully resolved values prefer ``.map`` files — they contain the linker's own evaluation of every expression.

### Map parser capabilities

Extracts two sections from the GNU LD map file:

1. **Memory Configuration** — the resolved region table (names, origins, lengths, attributes)
2. **Linker script and memory map** — output-section headers with VMA, size, and optional LMA

Handles both single-line and two-line section header formats emitted by GNU LD.  Debug sections (``.debug_*``, ``.comment``, ``.ARM.attributes``) are filtered out.  NOLOAD detection is based on conventional section names (``bss``, ``_user_heap_stack``, ``asset_area``).

### Report

- Per-application, per-region breakdown with per-section detail
- Colour-coded usage bars: green < 80%, yellow < 95%, red ≥ 95%
- Cross-application summary table when multiple apps are loaded
- JSON export mirrors the terminal report structure

## Design decisions

**Why parse ``.map`` files instead of ``.elf``?**  The map file is the linker's own accounting — it contains the fully-resolved Memory Configuration table, the exact output section headers with LMA/VMA pairs, and the per-object-file contribution breakdown.  ``arm-none-eabi-size`` on the ELF gives a good summary but lacks region attribution and LMA tracking.

**Why parse ``.ld`` files at all?**  So you can check theoretical capacities before building — useful when planning memory budgets for a new game.

**Standard library only.**  The tool runs on any machine with Python 3.10+, no ``pip install`` needed — it's just a development utility.

**No vivisected code.**  Every module is hand-crafted following the project's code-quality standards, with docstrings on every public function and class attribute.

## VS Code integration

A launch config is included in ``.vscode/launch.json``:

```json
{
    "name": "Memory Analysis",
    "type": "debugpy",
    "request": "launch",
    "program": "tools/memory_analysis/memory_analysis.py",
    "console": "integratedTerminal",
    "justMyCode": true
}
```

Press **F5** with "Memory Analysis" selected to run the report in the integrated terminal.
