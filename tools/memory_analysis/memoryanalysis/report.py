"""
Terminal and JSON report formatting.

Produces the human-readable memory analysis report (with optional ANSI
colour) or a machine-readable JSON dump.
"""

import json
from typing import Dict, List, Set

from .model import AppMemory
from .utils import bar, fmt_bytes, fmt_int, pct, pct_float


# ---------------------------------------------------------------------------
# ANSI terminal colours
# ---------------------------------------------------------------------------

class _Color:
    """SGR terminal escapes.  Degrade gracefully when stdout is redirected."""
    HEADER = '\033[1;36m'
    TITLE = '\033[1;33m'
    LABEL = '\033[1;37m'
    DIM = '\033[2m'
    GREEN = '\033[32m'
    YELLOW = '\033[33m'
    RED = '\033[31m'
    RESET = '\033[0m'

    @classmethod
    def disable(cls):
        for attr in dir(cls):
            if not attr.startswith('_') and attr.isupper():
                setattr(cls, attr, '')


# ---------------------------------------------------------------------------
# Terminal report
# ---------------------------------------------------------------------------

def format_report(apps: List[AppMemory], *, use_color: bool = True) -> str:
    """Build the full terminal report string for a list of applications."""
    if not use_color:
        _Color.disable()

    lines: List[str] = []
    lines.append('')
    lines.append(
        f"{_Color.HEADER}"
        f"╔══════════════════════════════════════════════════════════════════════╗"
        f"{_Color.RESET}"
    )
    lines.append(
        f"{_Color.HEADER}║{_Color.RESET}              "
        f"{_Color.TITLE}GAMECONSOLE MEMORY ANALYSIS{_Color.RESET}"
        f"                            {_Color.HEADER}║{_Color.RESET}"
    )
    lines.append(
        f"{_Color.HEADER}"
        f"╚══════════════════════════════════════════════════════════════════════╝"
        f"{_Color.RESET}"
    )
    lines.append('')

    for app in apps:
        lines.extend(_format_app(app))
        lines.append('')

    if len(apps) > 1:
        lines.extend(_format_summary_table(apps))

    lines.append('')
    return '\n'.join(lines)


def _format_app(app: AppMemory) -> List[str]:
    """Format a single application's memory usage."""
    lines: List[str] = []

    bin_info = ''
    if app.bin_size:
        bin_info = (
            f"  ({_Color.DIM}.bin: {fmt_bytes(app.bin_size)}{_Color.RESET})"
        )
    lines.append(f"  {_Color.TITLE}▸ {app.name}{_Color.RESET}{bin_info}")
    lines.append(f"  {_Color.DIM}{'─' * 66}{_Color.RESET}")
    lines.append('')

    active_regions = app.regions_with_usage()

    # LD-only mode: show all known regions even without section data
    if not active_regions and not app.sections:
        active_regions = sorted(
            app.regions.keys(),
            key=lambda r: app.regions[r].origin,
        )
    elif not active_regions:
        lines.append(f'    {_Color.DIM}No section data available.{_Color.RESET}')
        return lines

    if not active_regions:
        lines.append(f'    {_Color.DIM}No regions found.{_Color.RESET}')
        return lines

    for region_name in active_regions:
        region = app.regions.get(region_name)
        if region is None:
            continue
        sections = app.get_sections_in_region(region_name)
        used = app.region_used(region_name)
        free = app.region_free(region_name)
        cap = region.length

        lines.append(
            f'    {_Color.LABEL}{region_name}{_Color.RESET}  '
            f'{_Color.DIM}({fmt_bytes(cap)} total, {region.attributes}){_Color.RESET}'
        )

        for sec in sections:
            if sec.size == 0:
                continue
            noload_tag = f' {_Color.DIM}[NOLOAD]{_Color.RESET}' if sec.is_noload else ''
            lma_tag = ''
            if sec.has_separate_lma and sec.lma is not None and not sec.is_noload:
                lma_tag = f' {_Color.DIM}(LMA: 0x{sec.lma:08X}){_Color.RESET}'
            lines.append(
                f'      {sec.name:<22s}  {fmt_int(sec.size):>7s} B  '
                f'({pct(sec.size, cap)}){noload_tag}{lma_tag}'
            )

        lines.append(f'      {_Color.DIM}{"─" * 52}{_Color.RESET}')
        usage_pct = pct_float(used, cap)
        if usage_pct < 80:
            color = _Color.GREEN
        elif usage_pct < 95:
            color = _Color.YELLOW
        else:
            color = _Color.RED
        lines.append(
            f'      {"Used":<22s}  {color}{fmt_int(used):>7s} B  '
            f'({pct(used, cap)}){_Color.RESET}  {bar(used, cap)}'
        )
        lines.append(
            f'      {"Free":<22s}  {fmt_int(free):>7s} B  ({pct(free, cap)})'
        )
        lines.append('')

    return lines


def _format_summary_table(apps: List[AppMemory]) -> List[str]:
    """Build a side-by-side summary comparing all applications."""
    lines: List[str] = []
    lines.append(f'  {_Color.TITLE}▸ Cross-Application Summary{_Color.RESET}')
    lines.append(f'  {_Color.DIM}{"─" * 66}{_Color.RESET}')
    lines.append('')

    # Collect all region names across all apps
    all_regions: List[str] = []
    seen: Set[str] = set()
    for app in apps:
        for rname in app.regions_with_usage():
            if rname not in seen:
                all_regions.append(rname)
                seen.add(rname)

    # Header
    header = f'    {"Region":<20s}'
    for app in apps:
        header += f'  {app.name:<20s}'
    header += f'  {"Capacity":>10s}'
    lines.append(f'    {_Color.LABEL}{header}{_Color.RESET}')
    lines.append(f'    {_Color.DIM}{"─" * (36 + 24 * len(apps))}{_Color.RESET}')

    for region_name in all_regions:
        row = f'    {region_name:<20s}'
        capacity = 0
        for app in apps:
            if region_name in app.regions and app.region_used(region_name) > 0:
                used = app.region_used(region_name)
                cap = app.regions[region_name].length
                capacity = cap
                pct_val = f"{(used / cap * 100):.1f}%" if cap > 0 else '--'
                row += f'  {fmt_bytes(used):>8s} ({pct_val:>5s}){"":>5s}'
            else:
                row += f'  {"N/A":>8s} {"":>12s}'
        row += f'  {fmt_bytes(capacity):>10s}'
        lines.append(row)

    lines.append('')
    return lines


# ---------------------------------------------------------------------------
# JSON output
# ---------------------------------------------------------------------------

def to_json(apps: List[AppMemory]) -> str:
    """Serialize all application memory data to a JSON string."""
    data = []
    for app in apps:
        regions_data: Dict = {}
        for rname, region in app.regions.items():
            used = app.region_used(rname)
            free = app.region_free(rname)
            sections = [
                {
                    'name': s.name,
                    'vma': f'0x{s.vma:08X}',
                    'size': s.size,
                    'lma': f'0x{s.lma:08X}' if s.lma else None,
                    'is_noload': s.is_noload,
                }
                for s in app.get_sections_in_region(rname)
            ]
            regions_data[rname] = {
                'origin': f'0x{region.origin:08X}',
                'length': region.length,
                'length_human': fmt_bytes(region.length),
                'attributes': region.attributes,
                'used': used,
                'used_human': fmt_bytes(used),
                'free': free,
                'free_human': fmt_bytes(free),
                'pct_used': (
                    round((used / region.length) * 100, 2)
                    if region.length > 0 else 0
                ),
                'sections': sections,
            }
        data.append({
            'name': app.name,
            'map_path': app.map_path,
            'bin_path': app.bin_path,
            'bin_size': app.bin_size,
            'bin_size_human': fmt_bytes(app.bin_size),
            'regions': regions_data,
        })
    return json.dumps(data, indent=2)
