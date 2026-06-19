"""CRC32 helper.

The console verifies downloaded files with ``crc32_calculate`` (Console/Src/Crc),
which is the standard zlib/IEEE CRC-32 (reflected poly 0xEDB88320, init/final
0xFFFFFFFF). Python's :func:`zlib.crc32` is bit-for-bit identical, so the server
and the device agree without any custom implementation. The same variant is used
by the asset packer (tools/packer).
"""
from __future__ import annotations

import zlib

UINT32_MAX = 0xFFFFFFFF


def crc32(data: bytes) -> int:
    """Return the zlib/IEEE CRC-32 of ``data`` as an unsigned 32-bit integer."""
    return zlib.crc32(data) & UINT32_MAX
