"""Serialize / parse the manifest the console downloads.

The manifest is plain CSV on purpose: it is trivial to parse on the device (split
on commas and newlines — no JSON/YAML parser needed on the STM32). The first row
is a human-friendly header the console skips; ``crc32`` is lower-case hex.

    category,name,path,size,crc32,version
    games,GameXO.bin,games/GameXO.bin,7520,1a2b3c4d,1
    wifi,ESP01.bin,wifi/ESP01.bin,270416,deadbeef,3
"""
from __future__ import annotations

import csv
import io

from .catalog import ContentEntry

HEADER = ["category", "name", "path", "size", "crc32", "version"]


def to_csv(entries: list[ContentEntry]) -> str:
    """Render the catalog as the manifest CSV text."""
    buf = io.StringIO()
    writer = csv.writer(buf, lineterminator="\n")
    writer.writerow(HEADER)
    for e in entries:
        writer.writerow([e.category, e.name, e.path, e.size, f"{e.crc32:08x}", e.version])
    return buf.getvalue()


def parse_csv(text: str) -> list[ContentEntry]:
    """Inverse of :func:`to_csv` (handy for tests and tooling)."""
    entries: list[ContentEntry] = []
    rows = list(csv.reader(io.StringIO(text)))
    for row in rows[1:]:  # skip header
        if not row:
            continue
        category, name, path, size, crc, version = (col.strip() for col in row[:6])
        entries.append(ContentEntry(category, name, path, int(size), int(crc, 16), int(version)))
    return entries
