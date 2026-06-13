"""
GNU LD map file parser.

Parses the two key sections of a linker map file:

1. **Memory Configuration** — the fully-resolved region table (names, origins,
   lengths, attributes).  This is the same data that was declared in the
   linker script's ``MEMORY { … }`` block, but with all expressions evaluated.

2. **Linker script and memory map** — output-section headers giving each
   section's VMA, size, and (when different) LMA.

Returns an :class:`AppMemory` with both region capacities and actual section
placements, ready for the report formatter.
"""

import os
import re
from typing import Dict, List, Optional

from .model import AppMemory, MemoryRegion, SectionInfo

# ---------------------------------------------------------------------------
# Regex constants
# ---------------------------------------------------------------------------

# Section header — address + size on the same line:
#   .text           0x08000190     0x6608
#   .data           0x20000800       0x5c load address 0x080079f8
_RE_SECTION_SAME_LINE = re.compile(
    r'^\.(\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)(.*?)$'
)

# Section header — name only on this line, address + size on the *next*:
#   .game_console_api
#                   0x20000000        0x0
_RE_SECTION_NEXT_LINE = re.compile(
    r'^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)(.*?)$'
)

# Debug / metadata sections that never occupy target memory.
# Names are captured *without* the leading dot.
_DEBUG_PREFIXES = (
    'debug_', 'comment', 'ARM.attributes', 'debug_line_str',
)


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def parse_map_file(map_path: str) -> Optional[AppMemory]:
    """
    Parse a GNU LD linker map file.

    Returns an :class:`AppMemory` with regions, sections, and binary size,
    or ``None`` if the file cannot be read.
    """
    if not os.path.isfile(map_path):
        return None

    with open(map_path, 'r') as fh:
        lines = fh.read().splitlines()

    # ---- Memory Configuration table ----------------------------------
    regions = _parse_memory_config(lines)

    # ---- Section headers ---------------------------------------------
    sections = _parse_sections(lines)

    # ---- Associate .bin file -----------------------------------------
    bin_size = 0
    bin_path = None
    map_dir = os.path.dirname(os.path.abspath(map_path))
    base = os.path.splitext(os.path.basename(map_path))[0]
    candidate_bin = os.path.join(map_dir, base + '.bin')
    if os.path.isfile(candidate_bin):
        bin_size = os.path.getsize(candidate_bin)
        bin_path = candidate_bin

    # ---- Map sections → regions by VMA -------------------------------
    for sec in sections:
        sec.region = _region_for_vma(sec.vma, regions)

    # ---- Attribute flash LMA copies (e.g. .data init image) ----------
    for sec in sections:
        if sec.has_separate_lma and sec.lma is not None:
            lma_region = _region_for_vma(sec.lma, regions)
            if lma_region and lma_region != sec.region:
                _add_lma_copy(sections, sec, lma_region)

    return AppMemory(
        name=base,
        regions=regions,
        sections=sections,
        map_path=os.path.abspath(map_path),
        bin_path=bin_path,
        bin_size=bin_size,
    )


# ---------------------------------------------------------------------------
# Memory Configuration table
# ---------------------------------------------------------------------------

def _parse_memory_config(lines: List[str]) -> Dict[str, MemoryRegion]:
    """Extract the Memory Configuration table from map file lines."""
    regions: Dict[str, MemoryRegion] = {}

    start = None
    for i, line in enumerate(lines):
        if line.strip().startswith('Memory Configuration'):
            start = i
            break
    if start is None:
        return regions

    for j in range(start + 1, len(lines)):
        line = lines[j].strip()
        if not line:
            continue
        if line.startswith('Linker script'):
            break
        if line.startswith('*default*'):
            continue
        parts = line.split()
        if len(parts) >= 4 and parts[0] != 'Name':
            try:
                origin = int(parts[1], 16)
                length = int(parts[2], 16)
                attrs = parts[3]
                regions[parts[0]] = MemoryRegion(
                    name=parts[0], origin=origin, length=length, attributes=attrs,
                )
            except ValueError:
                pass
    return regions


# ---------------------------------------------------------------------------
# Section parsing
# ---------------------------------------------------------------------------

def _parse_sections(lines: List[str]) -> List[SectionInfo]:
    """
    Parse output section headers from the *Linker script and memory map* part.

    Handles two forms emitted by GNU LD:

    **Form A** — address + size on the same line as the name::

        .text           0x08000190     0x6608
        .data           0x20000800       0x5c load address 0x080079f8

    **Form B** — name on one line, address + size indented on the next::

        .game_console_api
                        0x20000000        0x0
    """
    sections: List[SectionInfo] = []

    map_start = 0
    for i, line in enumerate(lines):
        if line.strip().startswith('Linker script and memory map'):
            map_start = i + 1
            break
    if map_start == 0:
        return sections

    i = map_start
    while i < len(lines):
        line = lines[i]

        # Stop at /DISCARD/ — everything beyond is debug or metadata
        if line.startswith('/DISCARD/'):
            break

        # Form A: name, address, and size on one line
        m_a = _RE_SECTION_SAME_LINE.match(line)
        if m_a:
            sec_name = m_a.group(1)
            if _is_debug_section(sec_name):
                i += 1
                continue
            vma = int(m_a.group(2), 16)
            size = int(m_a.group(3), 16)
            lma = _extract_lma(m_a.group(4))
            sections.append(SectionInfo(
                name=sec_name, vma=vma, size=size,
                lma=lma, is_noload=_is_noload(sec_name),
            ))
            i += 1
            continue

        # Form B: name only — look ahead for address + size
        m_b_name = re.match(r'^\.(\S+)$', line)
        if m_b_name:
            sec_name = m_b_name.group(1)
            if _is_debug_section(sec_name):
                i += 1
                continue
            if i + 1 < len(lines):
                m_b_data = _RE_SECTION_NEXT_LINE.match(lines[i + 1])
                if m_b_data:
                    vma = int(m_b_data.group(1), 16)
                    size = int(m_b_data.group(2), 16)
                    lma = _extract_lma(m_b_data.group(3))
                    sections.append(SectionInfo(
                        name=sec_name, vma=vma, size=size,
                        lma=lma, is_noload=_is_noload(sec_name),
                    ))
                    i += 2
                    continue
            i += 1
            continue

        i += 1

    return sections


def _extract_lma(rest: str) -> Optional[int]:
    """Pull ``load address 0x…`` out of a section header's trailing text."""
    m = re.search(r'load address (0x[0-9a-fA-F]+)', rest)
    if m:
        return int(m.group(1), 16)
    return None


def _is_noload(name: str) -> bool:
    """Sections that are always NOLOAD in our linker scripts.

    *name* is without the leading dot.
    """
    return name in ('bss', '_user_heap_stack', 'asset_area')


def _is_debug_section(name: str) -> bool:
    """True if *name* (no leading dot) belongs to a debug / metadata section."""
    return any(name.startswith(p) for p in _DEBUG_PREFIXES)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _region_for_vma(vma: int, regions: Dict[str, MemoryRegion]) -> Optional[str]:
    """Return the region name whose address range contains *vma*."""
    for r in regions.values():
        if r.origin <= vma < r.origin + r.length:
            return r.name
    return None


def _add_lma_copy(
    sections: List[SectionInfo],
    source: SectionInfo,
    flash_region: str,
) -> None:
    """
    When a section's LMA is in flash (e.g. ``.data``), create a synthetic
    entry so flash usage accounts for the initialisation image.

    Skips NOLOAD sections (``.bss``, …) — their "load address" in the map
    is a linker bookkeeping artifact; nothing is stored there.
    """
    if source.is_noload or source.size == 0:
        return

    lma_start = source.lma
    # Avoid duplicates
    for s in sections:
        if s.region == flash_region and s.vma <= lma_start < s.vma + s.size:
            return

    sections.append(SectionInfo(
        name=f"{source.name} (LMA)",
        vma=lma_start,
        size=source.size,
        lma=None,
        is_noload=False,
        region=flash_region,
    ))
