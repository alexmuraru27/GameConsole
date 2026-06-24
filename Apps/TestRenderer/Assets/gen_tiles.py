#!/usr/bin/env python3
"""Generate TestRenderer's tile assets as GfxAsset (.bin) files.

TestRenderer's tiles are simple enough to author here as ASCII art + an RGB
palette instead of in Pixel Forge. The GfxAsset format stores palette slots as
indices into the console's 64-colour *system palette*, so this script maps every
authored colour to its nearest system entry, packs the pixels in the same 2bpp/4bpp
row-padded layout the editor uses (see tools/graphics/pixelforge/storage.py), and
writes one "GFX1" .bin per tile into Graphics/, ready for the packer.

Re-run it after editing a tile (`python3 gen_tiles.py`); the .bin files are
committed, exactly like Pixel Forge output, and the Makefile only runs the packer.
"""
import os
import struct

# ---- The console's 64-colour system palette, RGB565 (Console/Src/Renderer/
# renderer.c s_system_palette, generated from tools/graphics/pixelforge/palette.py).
SYS565 = [
    0x630C, 0x0173, 0x0898, 0x3818, 0x6013, 0x7809, 0x7800, 0x60C0,
    0x39A0, 0x0A60, 0x02C0, 0x02C0, 0x0249, 0x0000, 0x0000, 0x0000,
    0xAD55, 0x033E, 0x31FF, 0x70DF, 0xA85E, 0xC871, 0xC903, 0xAA20,
    0x7360, 0x3380, 0x0500, 0x04E3, 0x0451, 0x0000, 0x0000, 0x0000,
    0xFFFF, 0x4DBF, 0x847F, 0xCB5F, 0xFADF, 0xFAFC, 0xFB8D, 0xFCC0,
    0xCE00, 0x8700, 0x4FA0, 0x2F8D, 0x2EDC, 0x4A69, 0x0000, 0x0000,
    0xFFFF, 0xBFFF, 0xCE9F, 0xEE3F, 0xFDFF, 0xFDFE, 0xFE38, 0xFEB3,
    0xEF30, 0xCF90, 0xBFD3, 0xAFD8, 0xAF9E, 0xBDD7, 0x0000, 0x0000,
]


def to888(v):
    return (((v >> 11) & 0x1F) * 255 // 31,
            ((v >> 5) & 0x3F) * 255 // 63,
            (v & 0x1F) * 255 // 31)


SYS888 = [to888(v) for v in SYS565]


def nearest_index(rgb):
    """The system-palette index whose colour is closest to rgb (sq. distance)."""
    return min(range(len(SYS888)),
               key=lambda i: sum((a - b) ** 2 for a, b in zip(rgb, SYS888[i])))


# ---- GfxAsset .bin format (gfx_asset.h) ----
GFX_MAGIC = b"GFX1"
GFX_FMT_2BPP, GFX_FMT_4BPP = 1, 2
GFX_FLAG_IS_4BPP, GFX_FLAG_OPAQUE = 0x1, 0x2
HEADER = struct.Struct("<4s5I")  # magic, width, height, format, dataSize, flags


def slot_of(ch):
    return ord(ch) - ord('0') if ch <= '9' else ord(ch) - ord('a') + 10


def pack_pixels(slots, width, height, bits):
    stride = (width * bits + 7) // 8
    ppb = 8 // bits
    out = bytearray(stride * height)
    for y in range(height):
        for x in range(width):
            shift = 8 - bits * (x % ppb + 1)
            out[y * stride + x // ppb] |= slots[y * width + x] << shift
    return bytes(out)


def write_tile(out_dir, name, art, palette, fmt):
    width = height = 16
    bits = 4 if fmt == GFX_FMT_4BPP else 2
    slots = [slot_of(c) for c in art]
    assert len(slots) == width * height, f"{name}: art must be 16x16"

    pixels = pack_pixels(slots, width, height, bits)
    indices = bytes(nearest_index(rgb) for rgb in palette)
    flags = (GFX_FLAG_IS_4BPP if fmt == GFX_FMT_4BPP else 0)
    if 0 not in slots:  # uses no transparent (slot-0) pixel -> opaque fast path
        flags |= GFX_FLAG_OPAQUE

    header = HEADER.pack(GFX_MAGIC, width, height, fmt, len(pixels), flags)
    path = os.path.join(out_dir, name + ".bin")
    with open(path, "wb") as f:
        f.write(header + pixels + indices)
    print(f"  {name:8} {('4bpp' if bits == 4 else '2bpp')} "
          f"{'opaque' if flags & GFX_FLAG_OPAQUE else 'transp'}  -> {path}")


# ---- Tile art (one char per pixel = palette slot) + the authored RGB palette.
# Slot 0 is the transparent/backdrop slot; (0,0,0) for sprites that use it. ----
TRANSPARENT = (0, 0, 0)

BRICK = (
    "3333333333333332" "1111111111111112" "1111111111111112" "1111111111111112"
    "1111111111111112" "1111111111111112" "1111111111111112" "2222222222222222"
    "3333333233333333" "1111111211111111" "1111111211111111" "1111111211111111"
    "1111111211111111" "1111111211111111" "1111111211111111" "2222222222222222")
STONE = (
    "2222222222222222" "2333333333333332" "2311111111111132" "2311111111111132"
    "2311111111111132" "2311111111111132" "2311111111111132" "2311111111111132"
    "2311111111111132" "2311111111111132" "2311111111111132" "2311111111111132"
    "2311111111111132" "2311111111111132" "2111111111111112" "2222222222222222")
GROUND = (
    "2222222222222222" "2122212221222122" "1111111111111111" "1113111111311111"
    "1111111111111111" "1131111113111111" "1111111111111111" "1111311111111131"
    "1111111111111111" "1311111111311111" "1111111111111111" "1111111311111111"
    "3111111111111311" "1111111111111111" "1111131111111111" "1111111111111111")
TORCH = (
    "0000000000000000" "0000000330000000" "0000003223000000" "0000003223000000"
    "0000000330000000" "0000000110000000" "0000000110000000" "0000000110000000"
    "0000000110000000" "0000000110000000" "0000000110000000" "0000000000000000"
    "0000000000000000" "0000000000000000" "0000000000000000" "0000000000000000")
COIN = (
    "0000011111100000" "0001133333311000" "0013322222233100" "0133222222223310"
    "0132222332222310" "1322223333222231" "1322233333322231" "1322233333322231"
    "1322233333322231" "1322223333222231" "0132222332222310" "0133222222223310"
    "0013322222233100" "0001133333311000" "0000011111100000" "0000000000000000")
HEART = (
    "0000000000000000" "0011100011100000" "0122210122210000" "1233321233321000"
    "1222222222221000" "1222222222221000" "0122222222210000" "0012222222100000"
    "0001222221000000" "0000122210000000" "0000012100000000" "0000001000000000"
    "0000000000000000" "0000000000000000" "0000000000000000" "0000000000000000")
HERO = (
    "0000011111100000" "0000111111110000" "0001111111111000" "0001222222221000"
    "0001232232321000" "0001222222221000" "0000122222210000" "0000444444440000"
    "0004444444444000" "0004444444444000" "0004444444444000" "0000455555540000"
    "0000555005550000" "0000555005550000" "0000666006660000" "0006660000666000")
SLIME = (
    "0000000000000000" "0000000000000000" "0000000000000000" "0000000000000000"
    "0000011111100000" "0000111111110000" "0001113113111000" "0011111111111100"
    "0111111111111110" "0111111111111110" "0111111111111110" "0111111111111110"
    "0011111111111100" "0001111111111000" "0021021021021200" "0000000000000000")


def pal4(*colors):
    """A 4-slot (2bpp) palette."""
    assert len(colors) == 4
    return list(colors)


def pal16(*colors):
    """A 16-slot (4bpp) palette, zero-padded to 16 like the C initialisers."""
    return list(colors) + [TRANSPARENT] * (16 - len(colors))


TILES = [
    ("brick",  BRICK,  pal4((45, 25, 22), (150, 55, 45), (105, 100, 105), (190, 90, 72)),   GFX_FMT_2BPP),
    ("stone",  STONE,  pal4((35, 35, 42), (125, 125, 135), (55, 55, 65), (180, 182, 195)),  GFX_FMT_2BPP),
    ("ground", GROUND, pal4((60, 40, 25), (120, 80, 45), (70, 165, 60), (95, 62, 35)),      GFX_FMT_2BPP),
    ("torch",  TORCH,  pal4(TRANSPARENT, (120, 72, 35), (240, 140, 30), (255, 232, 95)),    GFX_FMT_2BPP),
    ("coin",   COIN,   pal4(TRANSPARENT, (120, 90, 10), (240, 200, 40), (255, 245, 180)),   GFX_FMT_2BPP),
    ("heart",  HEART,  pal4(TRANSPARENT, (220, 50, 55), (150, 25, 35), (255, 150, 150)),    GFX_FMT_2BPP),
    ("hero",   HERO,   pal16(TRANSPARENT, (45, 65, 170), (240, 195, 155), (25, 18, 18),
                             (205, 55, 55), (95, 72, 145), (70, 45, 32)),                   GFX_FMT_4BPP),
    ("slime",  SLIME,  pal16(TRANSPARENT, (90, 205, 95), (45, 130, 55), (15, 15, 15)),      GFX_FMT_4BPP),
]


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Graphics")
    os.makedirs(out_dir, exist_ok=True)
    print(f"Generating {len(TILES)} tiles into {out_dir}:")
    for name, art, palette, fmt in TILES:
        write_tile(out_dir, name, art, palette, fmt)


if __name__ == "__main__":
    main()
