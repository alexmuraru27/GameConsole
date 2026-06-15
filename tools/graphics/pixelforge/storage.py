"""Reading and writing the .bin and .c output files.

.bin format (all little-endian), declared in gfx_asset.h for C projects:

    GfxAssetHeader:
        uint32 magic       "GFX1"
        uint32 width       pixels
        uint32 height      pixels
        uint32 format      GfxFormat (1 = 2bpp, 2 = 4bpp)
        uint32 dataSize    pixel data bytes after the header
        uint32 flags       GFX_FLAG_* bitmask (bit 0 = 4bpp, bit 1 = opaque)
    pixel rows:            dataSize bytes, row-major, each row padded to a
                           byte boundary; 2bpp = 4 px/byte MSB-first,
                           4bpp = 2 px/byte high nibble first
    palette:               one byte per color (system palette index),
                           4 (2bpp) or 16 (4bpp) entries

The start of a file maps onto the GfxAsset struct in gfx_asset.h (header +
pixel data); the palette is a separate block following it.

Files written before the flags word existed have a 4-int header; load_bin
accepts both layouts.
"""

import os
import re
import struct

from .canvas import Canvas
from .constants import (
    BITS_PER_PIXEL,
    COLORS_PER_FORMAT,
    FORMAT_NAMES,
    GFX_ASSET_MAGIC,
    GFX_FLAG_IS_4BPP,
    GFX_FLAG_OPAQUE,
    GFX_FMT_4BPP,
    MAX_SIZE,
    SYSTEM_PALETTE_SIZE,
)

_HEADER_STRUCT = struct.Struct("<4s5I")        # magic, w, h, format, dataSize, flags
_LEGACY_HEADER_STRUCT = struct.Struct("<4s4I")  # pre-flags files (no flags word)
_C_BYTES_PER_LINE = 16


def header_flags(canvas):
    """Flags bitmask, laid out to match the renderer's SpriteFlags low bits."""
    flags = 0
    if canvas.format == GFX_FMT_4BPP:
        flags |= GFX_FLAG_IS_4BPP
    if canvas.is_opaque():
        flags |= GFX_FLAG_OPAQUE
    return flags


def _flags_c_expr(canvas):
    """The header flags as a C expression of the GFX_FLAG_* names (or '0')."""
    names = []
    if canvas.format == GFX_FMT_4BPP:
        names.append("GFX_FLAG_IS_4BPP")
    if canvas.is_opaque():
        names.append("GFX_FLAG_OPAQUE")
    return " | ".join(names) if names else "0"


def row_stride(width, bits):
    """Bytes per packed pixel row (rows are padded to whole bytes)."""
    return (width * bits + 7) // 8


def data_size(canvas):
    """dataSize the canvas serializes to (packed pixel bytes)."""
    return row_stride(canvas.width, canvas.bits_per_pixel) * canvas.height


def payload_size(canvas):
    """All bytes after the header: pixel data + the palette block."""
    return data_size(canvas) + COLORS_PER_FORMAT[canvas.format]


def pack_pixels(canvas):
    """Pack slot indices into the row-padded 2bpp/4bpp layout."""
    bits = canvas.bits_per_pixel
    stride = row_stride(canvas.width, bits)
    pixels_per_byte = 8 // bits
    packed = bytearray(stride * canvas.height)
    for y in range(canvas.height):
        for x in range(canvas.width):
            shift = 8 - bits * (x % pixels_per_byte + 1)
            packed[y * stride + x // pixels_per_byte] |= canvas.get(x, y) << shift
    return bytes(packed)


def unpack_pixels(packed, width, height, bits):
    """Inverse of pack_pixels(); returns row-major slot indices."""
    stride = row_stride(width, bits)
    pixels_per_byte = 8 // bits
    mask = (1 << bits) - 1
    pixels = bytearray(width * height)
    for y in range(height):
        for x in range(width):
            shift = 8 - bits * (x % pixels_per_byte + 1)
            byte = packed[y * stride + x // pixels_per_byte]
            pixels[y * width + x] = (byte >> shift) & mask
    return pixels


def save_bin(canvas, bin_path):
    pixels = pack_pixels(canvas)
    palette = bytes(canvas.palette[: canvas.color_count])
    header = _HEADER_STRUCT.pack(
        GFX_ASSET_MAGIC,
        canvas.width,
        canvas.height,
        canvas.format,
        len(pixels),
        header_flags(canvas),
    )
    with open(bin_path, "wb") as f:
        f.write(header + pixels + palette)


def load_bin(bin_path):
    with open(bin_path, "rb") as f:
        raw = f.read()
    if len(raw) < _LEGACY_HEADER_STRUCT.size:
        raise ValueError(f"{bin_path} is too small for a GfxAssetHeader")
    # magic/width/height/format/dataSize sit at the same offsets in both layouts.
    magic, width, height, fmt, pixel_size = _LEGACY_HEADER_STRUCT.unpack(
        raw[: _LEGACY_HEADER_STRUCT.size]
    )
    if magic != GFX_ASSET_MAGIC:
        raise ValueError(f"{bin_path} is not a GFX1 file")
    if fmt not in BITS_PER_PIXEL:
        raise ValueError(f"{bin_path} has unsupported format {fmt}")
    if not (1 <= width <= MAX_SIZE and 1 <= height <= MAX_SIZE):
        raise ValueError(f"{bin_path} has an invalid size {width}x{height}")
    bits = BITS_PER_PIXEL[fmt]
    color_count = COLORS_PER_FORMAT[fmt]
    if pixel_size != row_stride(width, bits) * height:
        raise ValueError(f"{bin_path} dataSize does not match its size/format")

    # The flags word is optional: accept both the current and the legacy header.
    body = pixel_size + color_count
    if len(raw) == _HEADER_STRUCT.size + body:
        payload = raw[_HEADER_STRUCT.size :]
    elif len(raw) == _LEGACY_HEADER_STRUCT.size + body:
        payload = raw[_LEGACY_HEADER_STRUCT.size :]
    else:
        raise ValueError(f"{bin_path} has an inconsistent GfxAssetHeader")

    # The opaque flag is recomputed from the pixels on the next save, so the
    # stored value is informational only and need not be carried on the Canvas.
    pixels = payload[:pixel_size]
    palette = [b % SYSTEM_PALETTE_SIZE for b in payload[pixel_size:]]
    return Canvas(width, height, fmt, palette, unpack_pixels(pixels, width, height, bits))


def save_c_file(canvas, c_path, name):
    """Write a C asset: GfxAssetHeader struct + palette array + pixel array."""
    macro = re.sub(r"\W", "_", name).upper()
    array = re.sub(r"\W", "_", name).lower()
    stride = row_stride(canvas.width, canvas.bits_per_pixel)
    packed = pack_pixels(canvas)
    format_name = FORMAT_NAMES[canvas.format].upper()
    with open(c_path, "w") as f:
        f.write("// Generated by pixel_forge.py - do not edit manually\n")
        f.write('#include "gfx_asset.h"\n\n')
        f.write(f"#define {macro}_WIDTH {canvas.width}U\n")
        f.write(f"#define {macro}_HEIGHT {canvas.height}U\n")
        f.write(f"#define {macro}_DATA_SIZE {data_size(canvas)}U\n\n")
        f.write(f"const GfxAssetHeader {array}_gfx_header = {{\n")
        f.write("    .magic = GFX_ASSET_MAGIC,\n")
        f.write(f"    .width = {macro}_WIDTH,\n")
        f.write(f"    .height = {macro}_HEIGHT,\n")
        f.write(f"    .format = GFX_FMT_{format_name},\n")
        f.write(f"    .dataSize = {macro}_DATA_SIZE,\n")
        f.write(f"    .flags = {_flags_c_expr(canvas)},\n")
        f.write("};\n\n")
        f.write(
            "// system palette indices (slot 0 is transparent, its color is"
            " the backdrop)\n"
        )
        f.write(
            f"const uint8_t {array}_gfx_palette[GFX_PALETTE_COLORS_{format_name}]"
            " = {\n"
        )
        f.write(_byte_line(canvas.palette[: canvas.color_count]) + "\n")
        f.write("};\n\n")
        f.write(f"// pixels: {FORMAT_NAMES[canvas.format]}, {stride} byte(s) per row\n")
        f.write(f"const uint8_t {array}_gfx_data[{macro}_DATA_SIZE] = {{\n")
        for y in range(canvas.height):
            row = packed[y * stride : (y + 1) * stride]
            for start in range(0, stride, _C_BYTES_PER_LINE):
                line = _byte_line(row[start : start + _C_BYTES_PER_LINE])
                if start == 0:
                    line += f" // row {y}"
                f.write(line + "\n")
        f.write("};\n")


def _byte_line(values):
    return "    " + ", ".join(f"0x{value:02X}" for value in values) + ","


def list_bin_names(directory):
    """Base names (no extension) of all .bin files in a directory."""
    if not os.path.isdir(directory):
        return []
    return sorted(
        os.path.splitext(f)[0] for f in os.listdir(directory) if f.endswith(".bin")
    )
