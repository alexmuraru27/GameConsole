"""
GNU LD linker script parser.

Extracts ``MEMORY { … }`` region definitions from linker scripts.
Handles ``INCLUDE`` directives (recursively), simple constant expressions
(``NAME = value;``), and the ``K`` / ``M`` size suffixes.

Because GNU LD's expression syntax is Turing-complete in the general case,
this parser handles the subset used by the GameConsole linker scripts:
literal integers, named constants, and the ``+`` ``-`` ``*`` ``/`` operators.

For fully resolved values prefer parsing a ``.map`` file (see
:mod:`map_parser`), which contains the linker's own evaluation of every
expression.
"""

import os
import re
from typing import Dict, Optional, Set

from .model import MemoryRegion
from .utils import parse_hex_or_k


def parse_ld_file(ld_path: str) -> Dict[str, MemoryRegion]:
    """
    Parse a GNU LD linker script and return every region defined in its
    ``MEMORY { … }`` block.

    ``INCLUDE`` directives are resolved recursively (with cycle detection)
    so that ``common.ld`` referenced by ``console.ld`` is automatically
    pulled in.

    Returns an empty dict if the file does not exist or contains no
    MEMORY block.
    """
    if not os.path.isfile(ld_path):
        return {}

    raw = _read_with_includes(ld_path)

    # ---- first pass: collect simple constant definitions -----------------
    constants: Dict[str, int] = {}
    for m in re.finditer(r'^(\w+)\s*=\s*(.+?);', raw, re.MULTILINE):
        name = m.group(1)
        expr = m.group(2).strip()
        try:
            constants[name] = _eval_expr(expr, constants)
        except (ValueError, KeyError):
            pass  # expression too complex — skip

    # ---- second pass: extract the MEMORY block ---------------------------
    mem_match = re.search(r'MEMORY\s*\{(.+?)\}', raw, re.DOTALL)
    if not mem_match:
        return {}

    regions: Dict[str, MemoryRegion] = {}
    for line in mem_match.group(1).splitlines():
        m = re.match(
            r'\s*(\w+)\s*\(([rwx]*)\)\s*:\s*'
            r'ORIGIN\s*=\s*(.+?),\s*LENGTH\s*=\s*(.+?)(?:,.*)?$',
            line,
        )
        if m:
            name = m.group(1)
            attrs = m.group(2)
            origin = _eval_expr(m.group(3).strip(), constants)
            length = _eval_expr(m.group(4).strip(), constants)
            regions[name] = MemoryRegion(
                name=name, origin=origin, length=length, attributes=attrs,
            )
    return regions


def _eval_expr(expr: str, constants: Dict[str, int]) -> int:
    """Evaluate a simple LD expression (literals, names, + - * /)."""
    tokens = re.split(r'(\s*[\+\-\*/]\s*)', expr)
    parts = []
    for token in tokens:
        token = token.strip()
        if not token:
            continue
        if token in constants:
            parts.append(str(constants[token]))
        elif token in ('+', '-', '*', '/'):
            parts.append(f' {token} ')
        else:
            parts.append(str(parse_hex_or_k(token)))
    try:
        return int(eval(''.join(parts), {}, {}))
    except Exception as exc:
        raise ValueError(f"Cannot evaluate linker expression: {expr}") from exc


def _guess_project_root() -> Optional[str]:
    """Walk up from this file's directory looking for ``common.ld``."""
    d = os.path.dirname(os.path.abspath(__file__))
    for _ in range(8):
        if os.path.isfile(os.path.join(d, 'common.ld')):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    return None


def _read_with_includes(ld_path: str, seen: Optional[Set[str]] = None) -> str:
    """Read an ``.ld`` file, resolving ``INCLUDE "…"`` directives inline."""
    if seen is None:
        seen = set()
    abs_path = os.path.abspath(ld_path)
    if abs_path in seen:
        return ''  # break circular includes
    seen.add(abs_path)

    with open(ld_path, 'r') as fh:
        raw = fh.read()

    def _resolve_include(m: re.Match) -> str:
        inc_path = m.group(1)
        if os.path.isabs(inc_path):
            return _read_with_includes(inc_path, seen)

        # GNU LD resolves relative INCLUDE paths against:
        #   1. CWD
        #   2. The directory containing the current script
        # We add a third fallback — the project root — because the user
        # may invoke us from any directory, unlike `make` which always
        # runs from a known subdirectory.
        candidates = [
            os.path.normpath(os.path.join(os.getcwd(), inc_path)),
            os.path.normpath(os.path.join(os.path.dirname(abs_path), inc_path)),
        ]
        # Heuristic: if the include has a "../" prefix, the target is
        # probably in the project root (a sibling to Console/ and GameXO/).
        # Try the basename relative to the directory that contains common.ld.
        if inc_path.startswith('..'):
            project_root = _guess_project_root()
            if project_root:
                candidates.append(
                    os.path.normpath(os.path.join(project_root, os.path.basename(inc_path)))
                )

        for candidate in candidates:
            if os.path.isfile(candidate):
                return _read_with_includes(candidate, seen)

        # Last resort: return the script-relative path so the error message
        # mentions a meaningful location.
        return _read_with_includes(candidates[1], seen)

    return re.sub(r'INCLUDE\s+"([^"]+)"', _resolve_include, raw)
