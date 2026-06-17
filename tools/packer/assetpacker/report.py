"""Human-readable summary of a built pak."""

from __future__ import annotations

from pathlib import Path

from .format import ENTRY_SIZE, HEADER_SIZE, PakInfo
from .manifest import Manifest


def format_summary(pak_path: Path, header_path: Path, manifest: Manifest, info: PakInfo) -> str:
    """Build the human-readable summary printed after a successful pack."""
    names = {spec.id: spec.name for spec in manifest.assets}
    entries_size = info.asset_count * ENTRY_SIZE
    data_size = info.pak_size - info.data_offset

    lines = [
        f"{pak_path}  -  {info.asset_count} assets, {info.pak_size} bytes",
        "",
        f"  magic        0x{info.magic:08X}  \"{_magic_text(info.magic)}\"",
        f"  version      {info.version}",
        f"  assetCount   {info.asset_count}",
        f"  dataOffset   {info.data_offset}  (header {HEADER_SIZE} + entries {info.asset_count} x {ENTRY_SIZE} = {entries_size})",
        f"  pakSize      {info.pak_size}  (dataOffset {info.data_offset} + data {data_size})",
        f"  pakCrc       0x{info.pak_crc:08X}",
        "",
    ]

    rows = [
        [
            str(entry.id),
            names.get(entry.id, "?"),
            str(entry.offset),
            str(info.data_offset + entry.offset),
            str(entry.size),
            f"0x{entry.crc32:08X}",
        ]
        for entry in info.entries
    ]
    table = _render_table(
        ["id", "name", "rel.off", "abs.off", "size", "crc32"],
        rows,
        right_align={0, 2, 3, 4},
    )
    lines += [f"  {line}" for line in table]
    lines += ["", f"  {header_path}  ({info.asset_count} ids)", "  verification: OK"]
    return "\n".join(lines)


def _magic_text(magic: int) -> str:
    return magic.to_bytes(4, "little").decode("ascii", "replace")


def _render_table(headers: list[str], rows: list[list[str]], right_align: set[int]) -> list[str]:
    widths = [len(header) for header in headers]
    for row in rows:
        widths = [max(width, len(cell)) for width, cell in zip(widths, row)]

    def render(cells: list[str]) -> str:
        line = "  ".join(
            cell.rjust(width) if i in right_align else cell.ljust(width)
            for i, (cell, width) in enumerate(zip(cells, widths))
        )
        return line.rstrip()  # drop padding after the final column

    return [render(headers), render(["-" * width for width in widths]), *(render(row) for row in rows)]
