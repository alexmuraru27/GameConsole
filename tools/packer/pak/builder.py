"""Assembling .pak bytes from a manifest."""

from __future__ import annotations

from pathlib import Path

from . import format as fmt
from .errors import PakError
from .manifest import AssetSpec, Manifest


def build_pak(manifest: Manifest, assets_root: Path) -> bytes:
    """Assemble the .pak bytes, reading each asset blob from disk.

    Relative asset paths are resolved against ``assets_root``. Blobs are stored
    back-to-back in manifest order.
    """
    asset_count = len(manifest.assets)
    data_offset = fmt.data_offset_for(asset_count)

    entries = bytearray()
    blobs = bytearray()
    offset = 0  # running offset relative to data_offset
    for spec in manifest.assets:
        blob = _read_asset(spec, assets_root)
        entries += fmt.ENTRY_STRUCT.pack(spec.id, offset, len(blob), fmt.crc32(blob))
        blobs += blob
        offset += len(blob)

    pak_size = data_offset + len(blobs)

    pak = bytearray()
    pak += fmt.HEADER_STRUCT.pack(fmt.PAK_MAGIC, fmt.PAK_VERSION, asset_count, data_offset, pak_size, 0)
    pak += entries
    pak += blobs
    assert len(pak) == pak_size, "internal: assembled size does not match pakSize"

    # pakCrc covers the whole file except its own 4 bytes; patch it in last.
    fmt.write_pak_crc(pak)
    return bytes(pak)


def _read_asset(spec: AssetSpec, assets_root: Path) -> bytes:
    path = spec.path if spec.path.is_absolute() else assets_root / spec.path
    try:
        return path.read_bytes()
    except OSError as exc:
        raise PakError(f"asset '{spec.name}' (id {spec.id}): cannot read {path}: {exc}") from exc
