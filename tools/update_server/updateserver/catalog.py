"""Scans the served content tree into a list of catalog entries.

The content root holds one subfolder per category, e.g.::

    content/
      games/   GameXO.bin, GameXO.pak, ...
      wifi/    ESP01.bin
      os/      GameConsole.bin

The category is simply the top-level folder name; everything under it (any depth)
is enumerated. For each file the catalog records its size and zlib CRC-32 (the
value the console recomputes to confirm a clean download) plus an advisory version
read from an optional ``versions.csv``.
"""
from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path

from .crc import crc32

MANIFEST_NAME = "manifest.csv"
VERSIONS_NAME = "versions.csv"

#: Files that live in the content root but are never themselves listed.
_RESERVED = {MANIFEST_NAME, VERSIONS_NAME}

#: Category used for loose files dropped directly in the root (no subfolder).
_DEFAULT_CATEGORY = "misc"


@dataclass(frozen=True)
class ContentEntry:
    """One downloadable file in the catalog."""

    category: str  #: top-level folder: games | wifi | os | ...
    name: str      #: file name, e.g. "GameXO.bin"
    path: str      #: POSIX path relative to the root, e.g. "games/GameXO.bin"
    size: int      #: size in bytes
    crc32: int     #: zlib/IEEE CRC-32, matches the console's crc32_calculate()
    version: int   #: advisory version (from versions.csv; defaults to 1)


class Catalog:
    """Enumerates the content root, caching CRCs by (path, mtime, size)."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self._crc_cache: dict[tuple[str, int, int], int] = {}

    def scan(self) -> list[ContentEntry]:
        """Return the current catalog, sorted by path for a stable manifest."""
        versions = self._load_versions()
        entries: list[ContentEntry] = []

        for file in sorted(self.root.rglob("*")):
            if not file.is_file() or file.name.startswith("."):
                continue
            rel = file.relative_to(self.root).as_posix()
            if rel in _RESERVED:
                continue

            parts = rel.split("/")
            category = parts[0] if len(parts) > 1 else _DEFAULT_CATEGORY
            size = file.stat().st_size
            crc = self._crc_for(file, rel, size)
            entries.append(
                ContentEntry(category, file.name, rel, size, crc, versions.get(rel, 1))
            )

        return entries

    # -- internals -------------------------------------------------------

    def _crc_for(self, file: Path, rel: str, size: int) -> int:
        """CRC-32 of ``file``, recomputed only when it changes on disk."""
        key = (rel, file.stat().st_mtime_ns, size)
        cached = self._crc_cache.get(key)
        if cached is None:
            cached = crc32(file.read_bytes())
            self._crc_cache[key] = cached
        return cached

    def _load_versions(self) -> dict[str, int]:
        """Read the optional ``versions.csv`` (``path,version`` rows)."""
        vfile = self.root / VERSIONS_NAME
        versions: dict[str, int] = {}
        if not vfile.is_file():
            return versions

        with vfile.open(newline="", encoding="utf-8") as fh:
            for row in csv.reader(fh):
                if not row or row[0].lstrip().startswith("#"):
                    continue
                key = row[0].strip()
                if len(row) < 2 or key.lower() == "path":  # header / malformed
                    continue
                try:
                    versions[key] = int(row[1])
                except ValueError:
                    pass
        return versions
