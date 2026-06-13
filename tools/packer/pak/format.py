"""The .pak binary format: constants, struct layouts, and CRC helpers.

This is the Python mirror of ``pak_format.h`` and the single source of truth for
the on-disk layout. Every field is a little-endian ``uint32``, so the structs are
contiguous with no padding:

    PakHeader (24 bytes): magic, version, assetCount, dataOffset, pakSize, pakCrc
    PakEntry  (16 bytes): id, offset, size, crc32
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

PAK_MAGIC = 0x314B4150  # "PAK1" little-endian
PAK_VERSION = 1

UINT32_MAX = 0xFFFFFFFF

# Little-endian layouts. The fixed PakHeader fields precede entries[].
HEADER_STRUCT = struct.Struct("<6I")  # magic, version, assetCount, dataOffset, pakSize, pakCrc
ENTRY_STRUCT = struct.Struct("<4I")   # id, offset, size, crc32

HEADER_SIZE = HEADER_STRUCT.size  # 24
ENTRY_SIZE = ENTRY_STRUCT.size    # 16

# Byte span of the pakCrc field inside the fixed header; excluded from pakCrc.
PAK_CRC_OFFSET = 20
PAK_CRC_SIZE = 4


@dataclass(frozen=True)
class ParsedEntry:
    """A PakEntry decoded from pak bytes."""

    id: int
    offset: int  # relative to dataOffset
    size: int
    crc32: int


@dataclass(frozen=True)
class PakInfo:
    """The header fields and entries decoded from a pak."""

    magic: int
    version: int
    asset_count: int
    data_offset: int
    pak_size: int
    pak_crc: int
    entries: tuple[ParsedEntry, ...]


def crc32(data: bytes) -> int:
    """CRC32 (zlib/IEEE) of ``data`` as an unsigned 32-bit value."""
    return zlib.crc32(data) & UINT32_MAX


def crc32_excluding_pakcrc(data: bytes | bytearray) -> int:
    """CRC32 over the whole pak except the 4 ``pakCrc`` bytes."""
    crc = zlib.crc32(data[:PAK_CRC_OFFSET])
    crc = zlib.crc32(data[PAK_CRC_OFFSET + PAK_CRC_SIZE:], crc)
    return crc & UINT32_MAX


def data_offset_for(asset_count: int) -> int:
    """Byte offset of the first blob for a pak holding ``asset_count`` assets."""
    return HEADER_SIZE + asset_count * ENTRY_SIZE


def write_pak_crc(pak: bytearray) -> None:
    """Compute ``pakCrc`` over an assembled pak buffer and patch it in place."""
    struct.pack_into("<I", pak, PAK_CRC_OFFSET, crc32_excluding_pakcrc(pak))
