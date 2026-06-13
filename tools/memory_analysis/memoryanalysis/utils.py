"""
Small pure helpers used across the memory_analysis package.

These have no domain knowledge — just string/number formatting and
filesystem utilities.
"""

import os
from typing import Optional


def parse_hex_or_k(token: str) -> int:
    """
    Parse a value that may be hex (``0x…``), decimal, or suffixed with K / M.

        >>> parse_hex_or_k('128K')
        131072
        >>> parse_hex_or_k('0x20000000')
        536870912
        >>> parse_hex_or_k('64')
        64
    """
    token = token.strip()
    multiplier = 1
    upper = token.upper()
    if upper.endswith('K'):
        multiplier = 1024
        token = token[:-1]
    elif upper.endswith('M'):
        multiplier = 1024 * 1024
        token = token[:-1]
    if token.lower().startswith('0x'):
        return int(token, 16) * multiplier
    return int(token, 0) * multiplier


def resolve_path(path_str: str, *, relative_to: Optional[str] = None) -> str:
    """Resolve a path that may contain ``~`` or be relative."""
    expanded = os.path.expanduser(path_str)
    if os.path.isabs(expanded):
        return os.path.normpath(expanded)
    if relative_to:
        return os.path.normpath(os.path.join(os.path.dirname(relative_to), expanded))
    return os.path.normpath(os.path.join(os.getcwd(), expanded))


def fmt_int(n: int) -> str:
    """Format an integer with commas as thousands separators.

        >>> fmt_int(26120)
        '26,120'
    """
    return f"{n:,}"


def fmt_bytes(n: int) -> str:
    """Human-readable byte size using SI units (1 KB = 1024 B).

        >>> fmt_bytes(31308)
        '30.6 KB'
        >>> fmt_bytes(92)
        '92 B'
    """
    if n >= 1024 * 1024:
        return f"{n / (1024 * 1024):.1f} MB"
    elif n >= 1024:
        return f"{n / 1024:.1f} KB"
    else:
        return f"{n} B"


def pct(used: int, capacity: int) -> str:
    """Return a fixed-width percentage string, e.g. ``' 12.3%'``."""
    if capacity == 0:
        return '  --  '
    return f"{(used / capacity) * 100.0:5.1f}%"


def pct_float(used: int, capacity: int) -> float:
    """Return the usage percentage as a float in [0, 100]."""
    if capacity == 0:
        return 0.0
    return (used / capacity) * 100.0


def bar(used: int, capacity: int, width: int = 20) -> str:
    """Draw a Unicode horizontal bar chart.

        >>> bar(31308, 524288, 10)
        '[█░░░░░░░░░]'
    """
    if capacity == 0:
        return '[' + '·' * width + ']'
    filled = max(1, round((used / capacity) * width)) if used > 0 else 0
    return '[' + '█' * filled + '░' * (width - filled) + ']'
