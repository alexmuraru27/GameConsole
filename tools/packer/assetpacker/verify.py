"""Re-parsing and validating built .pak bytes."""

from __future__ import annotations

from . import format as fmt
from .errors import PakVerificationError
from .format import PakInfo, ParsedEntry


def verify_pak(data: bytes) -> PakInfo:
    """Re-parse built pak bytes and validate every field.

    Checks magic/version/assetCount/dataOffset/pakSize, every entry's bounds,
    every asset CRC, and the whole-file pakCrc. Raises PakVerificationError on
    the first mismatch; returns the decoded PakInfo on success.
    """
    if len(data) < fmt.HEADER_SIZE:
        raise PakVerificationError(
            f"pak is {len(data)} bytes, smaller than the {fmt.HEADER_SIZE}-byte header"
        )

    magic, version, asset_count, data_offset, pak_size, pak_crc = fmt.HEADER_STRUCT.unpack_from(data)

    if magic != fmt.PAK_MAGIC:
        raise PakVerificationError(f"bad magic 0x{magic:08X}, expected 0x{fmt.PAK_MAGIC:08X}")
    if version != fmt.PAK_VERSION:
        raise PakVerificationError(f"unsupported version {version}, expected {fmt.PAK_VERSION}")
    if pak_size != len(data):
        raise PakVerificationError(f"pakSize {pak_size} != actual file size {len(data)}")

    expected_data_offset = fmt.data_offset_for(asset_count)
    if data_offset != expected_data_offset:
        raise PakVerificationError(
            f"dataOffset {data_offset} != expected {expected_data_offset} for {asset_count} assets"
        )
    if data_offset > pak_size:
        raise PakVerificationError(f"dataOffset {data_offset} exceeds pakSize {pak_size}")

    actual_pak_crc = fmt.crc32_excluding_pakcrc(data)
    if actual_pak_crc != pak_crc:
        raise PakVerificationError(
            f"pakCrc 0x{pak_crc:08X} != recomputed 0x{actual_pak_crc:08X}"
        )

    data_region = pak_size - data_offset
    entries: list[ParsedEntry] = []
    for index in range(asset_count):
        asset_id, offset, size, crc = fmt.ENTRY_STRUCT.unpack_from(
            data, fmt.HEADER_SIZE + index * fmt.ENTRY_SIZE
        )

        if offset > data_region or size > data_region - offset:
            raise PakVerificationError(
                f"asset id {asset_id} (entry {index}) blob [{offset}, {offset + size}) "
                f"is out of bounds for a {data_region}-byte data region"
            )

        start = data_offset + offset
        actual_crc = fmt.crc32(data[start:start + size])
        if actual_crc != crc:
            raise PakVerificationError(
                f"asset id {asset_id} (entry {index}) crc 0x{crc:08X} != recomputed 0x{actual_crc:08X}"
            )
        entries.append(ParsedEntry(asset_id, offset, size, crc))

    # Per-entry bounds plus exact coverage imply a gap-free, overlap-free tiling
    # of the data region.
    covered = sum(entry.size for entry in entries)
    if covered != data_region:
        raise PakVerificationError(
            f"asset blobs cover {covered} bytes but the data region is {data_region} bytes"
        )

    return PakInfo(magic, version, asset_count, data_offset, pak_size, pak_crc, tuple(entries))
