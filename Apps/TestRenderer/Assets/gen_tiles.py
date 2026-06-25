#!/usr/bin/env python3
"""Generate TestRenderer's sprite assets as GfxAsset (.bin) files.

TestRenderer is a renderer *benchmark*, so its asset set is deliberately built to
exercise the whole compositor, not an ideal path:

  * 64 distinct sprites  -> blows the 8-slot decoded-tile cache, so most frames hit
    the *uncached* 2bpp/4bpp compositors, not just cached copies.
  * 32 opaque / 32 transparent -> a median load across both paths (a real tile game
    splits the same way: solid terrain vs shaped objects).
  * variable sizes (8x8 .. 64x32) -> small sprites cache (<=256 px), big ones can't
    (TILE_CACHE_MAX_PX), so they force the unpacking path and span several chunks.
  * 2bpp and 4bpp, opaque and transparent -> all four format/opacity combos.

Art is built procedurally from a few primitives (fills, discs, boxes) rather than
hand-counted ASCII, so a 60x30 banner is correct-by-construction. The GfxAsset
format stores palette slots as indices into the console's 64-colour *system
palette*; every authored RGB is mapped to its nearest system entry.

Re-run after editing (`python3 gen_tiles.py`); the .bin files are committed, and
the Makefile only runs the packer over Assets/manifest.yaml.
"""
import os
import struct

# ---- Console 64-colour system palette, RGB565 (Console/.../renderer.c). ----
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
SYS888 = [(((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63,
           (v & 0x1F) * 255 // 31) for v in SYS565]


def nearest_index(rgb):
    return min(range(64), key=lambda i: sum((a - b) ** 2 for a, b in zip(rgb, SYS888[i])))


# ---- GfxAsset .bin format (gfx_asset.h) ----
GFX_MAGIC = b"GFX1"
FMT2, FMT4 = 1, 2
GFX_FLAG_IS_4BPP, GFX_FLAG_OPAQUE = 0x1, 0x2
HEADER = struct.Struct("<4s5I")  # magic, width, height, format, dataSize, flags


def slot_of(ch):
    return ord(ch) - ord('0') if ch <= '9' else ord(ch) - ord('a') + 10


def pack_pixels(slots, w, h, bits):
    stride = (w * bits + 7) // 8
    ppb = 8 // bits
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            out[y * stride + x // ppb] |= slots[y * w + x] << (8 - bits * (x % ppb + 1))
    return bytes(out)


def write_tile(out_dir, name, art_whc, palette, fmt):
    art, w, h = art_whc
    bits = 4 if fmt == FMT4 else 2
    slots = [slot_of(c) for c in art]
    assert len(slots) == w * h, f"{name}: art is {len(slots)} px, expected {w}x{h}={w*h}"
    assert max(slots) < (1 << bits), f"{name}: slot {max(slots)} out of range for {bits}bpp"
    pixels = pack_pixels(slots, w, h, bits)
    indices = bytes(nearest_index(c) for c in palette)
    flags = GFX_FLAG_IS_4BPP if fmt == FMT4 else 0
    if 0 not in slots:                          # no transparent pixel -> opaque
        flags |= GFX_FLAG_OPAQUE
        # Opaque art can't use slot 0 (slot 0 IS the transparency flag), so it
        # draws in slots 1..N. Shift the index table right by one so authored
        # colour k lands on the art's slot k (slot 0 becomes an unused dup).
        # Transparent tiles already align (their palettes start with slot-0 = TR).
        indices = bytes([indices[0]]) + indices[:-1]
    with open(os.path.join(out_dir, name + ".bin"), "wb") as f:
        f.write(HEADER.pack(GFX_MAGIC, w, h, fmt, len(pixels), flags) + pixels + indices)
    return flags, w, h


# ======================================================================
#  Drawing primitives. Builders return (art_string, w, h). A canvas is a
#  list-of-rows of single-char slot ids ('0'..'f'); slot 0 is transparent.
# ======================================================================
def canvas(w, h, bg='0'):
    return [[bg] * w for _ in range(h)]


def fin(g):
    h, w = len(g), len(g[0])
    return "".join("".join(r) for r in g), w, h


def rows(*r):
    h, w = len(r), len(r[0])
    assert all(len(x) == w for x in r), "ragged art rows"
    return "".join(r), w, h


def box(g, x0, y0, x1, y1, fill, edge=None):
    for y in range(max(0, y0), min(len(g), y1 + 1)):
        for x in range(max(0, x0), min(len(g[0]), x1 + 1)):
            g[y][x] = edge if (edge and (x in (x0, x1) or y in (y0, y1))) else fill


def disc(g, cx, cy, rx, ry, fill, edge=None, inner=0.58):
    for y in range(len(g)):
        for x in range(len(g[0])):
            d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
            if d <= 1.0:
                g[y][x] = fill if d <= inner else (edge or fill)


def diamond(g, cx, cy, r, fill, edge=None):
    for y in range(len(g)):
        for x in range(len(g[0])):
            d = abs(x - cx) + abs(y - cy)
            if d <= r:
                g[y][x] = edge if (edge and d >= r - 0) and d == r else fill


def hline(g, x0, x1, y, c):
    for x in range(x0, x1 + 1):
        if 0 <= x < len(g[0]) and 0 <= y < len(g):
            g[y][x] = c


def vline(g, x, y0, y1, c):
    for y in range(y0, y1 + 1):
        if 0 <= x < len(g[0]) and 0 <= y < len(g):
            g[y][x] = c


def px(g, x, y, c):
    if 0 <= y < len(g) and 0 <= x < len(g[0]):
        g[y][x] = c


# ---- Opaque pattern fills (slots 1..3 only -> opaque fast path). ----
def dither(w, h, a='1', b='2', spot=None, period=9):
    g = canvas(w, h, a)
    n = 0
    for y in range(h):
        for x in range(w):
            g[y][x] = a if (x + y) % 2 == 0 else b
            n += 1
            if spot and n % period == 0:
                g[y][x] = spot
    return fin(g)


def bricks(w, h, course=8):
    g = canvas(w, h, '2')
    for y in range(h):
        for x in range(w):
            if y % course == course - 1:
                g[y][x] = '1'                       # mortar bed
            elif y % course == 0:
                g[y][x] = '3'                       # lit top edge
            elif (y % (course * 2) < course and x % 8 == 7) or \
                 (y % (course * 2) >= course and x % 8 == 3):
                g[y][x] = '1'                       # offset head joint
    return fin(g)


def planks(w, h, vertical):
    g = canvas(w, h, '2')
    for y in range(h):
        for x in range(w):
            t = x if vertical else y
            g[y][x] = '2' if (t // 4) % 2 == 0 else '3'
            if t % 4 == 3:
                g[y][x] = '1'
    return fin(g)


def waves(w, h):
    g = canvas(w, h, '2')
    for y in range(h):
        for x in range(w):
            g[y][x] = '1' if y % 4 in (2, 3) else '2'
            if y % 4 == 0 and (x + (y // 4) * 2) % 6 < 2:
                g[y][x] = '3'
    return fin(g)


def checker(w, h, n=4):
    g = canvas(w, h, '1')
    for y in range(h):
        for x in range(w):
            g[y][x] = '1' if ((x // n) + (y // n)) % 2 == 0 else '2'
    return fin(g)


def cobblestone(w, h):
    # Offset rows of rounded stones (slot 1) with a lit top edge (slot 3) on dark
    # mortar (slot 2). Reads as cobblestone, not a flat transparency checker.
    g = canvas(w, h, '2')
    for i, ry in enumerate(range(0, h, 4)):
        off = (i % 2) * 4
        for rx in range(-4 + off, w, 8):
            box(g, rx + 1, ry, rx + 6, ry + 2, '1')
            hline(g, rx + 1, rx + 5, ry, '3')
    return fin(g)


def vgrad(w, h, *bands):
    g = canvas(w, h, bands[0])
    nb = len(bands)
    for y in range(h):
        idx = min(nb - 1, y * nb // h)
        for x in range(w):
            g[y][x] = bands[idx]
            if (y * nb) % h < 2 and (x + y) % 2 == 0 and idx > 0:
                g[y][x] = bands[idx - 1]
    return fin(g)


# ---- Opaque shape builders. ----
def roof():
    g = canvas(16, 16, '2')
    for y in range(16):
        for x in range(16):
            if (x + (y // 4) * 2) % 8 == 0:
                g[y][x] = '1'
            elif y % 4 == 0:
                g[y][x] = '3'
    return fin(g)


def window():
    g = canvas(16, 16, '1')          # stone frame
    box(g, 3, 3, 12, 12, '2', '1')   # glass
    hline(g, 3, 12, 7, '1'); vline(g, 7, 3, 12, '1')
    px(g, 4, 4, '3'); px(g, 9, 9, '3')  # glints
    return fin(g)


def stars():
    g = canvas(16, 16, '1')
    for (x, y) in [(2, 3), (7, 1), (12, 4), (4, 9), (10, 11), (14, 8), (1, 13), (8, 6)]:
        g[y][x] = '3'
    return fin(g)


def mountain(w, h):
    # TRANSPARENT silhouette: triangular peaks (one per 32px), sky shows above them
    # so the renderer's background paints the gaps. slot 0 = sky (transparent).
    g = canvas(w, h)
    for x in range(w):
        d = abs((x % 32) - 16) / 16.0              # 0 at a peak, 1 in the valley
        top = min(int(2 + d * (h - 4)), h - 1)     # transparent above, rock below
        for y in range(top, h):
            g[y][x] = '1'                          # rock body
        g[top][x] = '3' if d < 0.35 else '2'       # snow near the high peaks, ridge elsewhere
    return fin(g)


def hills(w, h):
    # TRANSPARENT rolling silhouette (one mound per 24px), sky shows above.
    g = canvas(w, h)
    for x in range(w):
        d = abs((x % 24) - 12) / 12.0
        top = min(int(1 + d * (h - 1)), h - 1)
        for y in range(top, h):
            g[y][x] = '1'
        g[top][x] = '2'                            # lit crest
    return fin(g)


def cloudbank(w, h):
    g = canvas(w, h, '1')
    for cx in range(8, w, 16):
        disc(g, cx, h // 2, 11, h * 0.55, '3', '2')
    return fin(g)


def tower(w, h):
    g = bricks(w, h, 8)[0]
    g = [list(g[i * w:(i + 1) * w]) for i in range(h)]
    box(g, 0, 0, w - 1, 1, '3')                    # crenellations on top
    for x in range(0, w, 4):
        box(g, x, 0, x + 1, 2, '1')
    box(g, w // 2 - 2, h - 7, w // 2 + 1, h - 1, '1', '3')  # door
    return fin(g)


def boulder(w, h):
    g = canvas(w, h, '1')
    disc(g, w / 2, h / 2 + 2, w / 2 - 1, h / 2 - 1, '2', '1', inner=0.5)
    disc(g, w / 2 - 3, h / 2 - 2, 5, 4, '3', '2', inner=0.4)
    box(g, 0, h - 2, w - 1, h - 1, '1')            # ground shadow base
    return fin(g)


def archway(w, h):
    g = canvas(w, h, '1')                          # stone block
    disc(g, w / 2, h, w / 2 - 3, h - 4, '3', '2')  # arch opening (lit interior)
    box(g, 0, 0, w - 1, 2, '2', '1')               # keystone band
    return fin(g)


def marble(w, h):                                  # 4bpp opaque, soft veins
    g = canvas(w, h, '2')
    for y in range(h):
        for x in range(w):
            g[y][x] = '2' if (x + y) % 3 else '3'
            if (x - y) % 9 == 0:
                g[y][x] = '4'
            if (x + 2 * y) % 13 == 0:
                g[y][x] = '1'
    return fin(g)


def crack(art_whc):
    art, w, h = art_whc
    g = [list(art[i * w:(i + 1) * w]) for i in range(h)]
    for (x, y) in [(7, 2), (7, 3), (8, 4), (8, 5), (9, 6), (8, 7), (9, 9), (10, 10)]:
        px(g, x, y, '1')
    return fin(g)


# ---- Transparent shape builders (slot 0 = see-through). ----
def coin8():
    return rows("00111100", "01233210", "12332321", "12322321",
                "12322321", "12332321", "01233210", "00111100")


def gem8():
    return rows("00133100", "01322310", "13222231", "13222231",
                "01322310", "00132310", "00013100", "00001000")


def heart8():
    return rows("01100110", "12211221", "12222221", "12222221",
                "01222210", "00122100", "00011000", "00000000")


def key8():
    return rows("00133000", "01211100", "01211000", "00121000",
                "00121000", "00121300", "00121100", "00011000")


def star8():
    return rows("00011000", "00011000", "11111111", "01111110",
                "00111100", "00111100", "01100110", "11000011")


def flower8(p):
    return rows("0p000p00".replace('p', p), "p1p0p1p0".replace('p', p),
                "0p131p00".replace('p', p), "0p333p00".replace('p', p),
                "0p131p00".replace('p', p), "00121000", "00121000", "00021000")


def butterfly8():
    return rows("01000010", "13100131", "13310331", "13133131",
                "01313310", "00131100", "00010000", "00000000")


def potion():
    g = canvas(16, 16)
    box(g, 6, 1, 9, 3, '1')                # cork
    disc(g, 8, 10, 5, 5, '2', '1', inner=0.45)
    box(g, 6, 4, 9, 7, '2', '1')           # neck
    px(g, 6, 9, '3'); px(g, 7, 8, '3')     # shine
    return fin(g)


def ring():
    g = canvas(16, 16)
    disc(g, 8, 9, 5, 5, '1', '1', inner=1.1)
    disc(g, 8, 9, 3, 3, '0', '0', inner=1.1)  # hollow centre
    box(g, 7, 7, 8, 7, '0')
    px(g, 8, 3, '3'); px(g, 7, 3, '2'); px(g, 9, 3, '2')  # gemstone
    return fin(g)


def apple():
    g = canvas(16, 16)
    disc(g, 8, 9, 5, 5, '1', '2', inner=0.55)
    vline(g, 8, 2, 4, '3')                  # stem
    px(g, 9, 3, '2')                        # leaf
    px(g, 6, 7, '3')                        # shine
    return fin(g)


def bush():
    g = canvas(16, 16)
    disc(g, 5, 10, 4, 4, '1', '2')
    disc(g, 11, 10, 4, 4, '1', '2')
    disc(g, 8, 7, 5, 4, '3', '2', inner=0.4)
    box(g, 0, 14, 15, 15, '2')
    return fin(g)


def mushroom():
    g = canvas(16, 16)
    disc(g, 8, 7, 6, 4, '1', '1', inner=1.1)   # red cap
    for (x, y) in [(5, 5), (10, 6), (8, 8)]:
        px(g, x, y, '3')                       # spots
    box(g, 6, 10, 9, 14, '2', '2')             # stem
    return fin(g)


def sapling():
    g = canvas(16, 16)
    vline(g, 8, 8, 14, '1')
    disc(g, 8, 6, 4, 4, '2', '3')
    return fin(g)


def tree(w, h):                                # 32x48 big tree
    g = canvas(w, h)
    box(g, w // 2 - 3, h - 18, w // 2 + 2, h - 1, '1', '1')  # trunk
    disc(g, w / 2, h * 0.32, w / 2 - 1, h * 0.30, '2', '3')  # canopy
    disc(g, w * 0.30, h * 0.45, 7, 6, '2', '3')
    disc(g, w * 0.70, h * 0.45, 7, 6, '2', '3')
    return fin(g)


def torch():
    return rows("0000000000000000", "0000000330000000", "0000003223000000",
                "0000003223000000", "0000000330000000", "0000000110000000",
                "0000000110000000", "0000000110000000", "0000000110000000",
                "0000000110000000", "0000000110000000", "0000000000000000",
                "0000000000000000", "0000000000000000", "0000000000000000",
                "0000000000000000")


def lantern():
    g = canvas(16, 16)
    vline(g, 8, 0, 2, '1')                  # hook
    box(g, 5, 3, 10, 12, '1', '1')          # frame
    box(g, 6, 5, 9, 10, '3', '2')           # flame glow
    return fin(g)


def barrel():
    g = canvas(16, 16)
    disc(g, 8, 8, 6, 7, '1', '1', inner=1.1)
    box(g, 2, 5, 13, 6, '3'); box(g, 2, 10, 13, 11, '3')  # hoops
    box(g, 4, 2, 11, 13, '2', None)
    box(g, 2, 5, 13, 6, '3'); box(g, 2, 10, 13, 11, '3')
    return fin(g)


def crate():
    g = canvas(16, 16)
    box(g, 2, 2, 13, 13, '2', '1')
    box(g, 2, 2, 13, 13, '1', None)
    box(g, 3, 3, 12, 12, '2')
    hline(g, 3, 12, 7, '1'); vline(g, 7, 3, 12, '1')
    px(g, 4, 4, '3'); px(g, 11, 11, '1')
    return fin(g)


def chest(w, h):                               # 24x16
    g = canvas(w, h)
    box(g, 2, 6, w - 3, h - 2, '1', '1')       # body
    box(g, 3, 7, w - 4, h - 3, '2')
    disc(g, w / 2 - 1, 6, w / 2 - 2, 4, '1', '1', inner=1.1)  # lid
    box(g, w // 2 - 2, 5, w // 2, 9, '3', '1')  # lock
    return fin(g)


def sign():
    g = canvas(16, 16)
    vline(g, 8, 8, 15, '1')                  # post
    box(g, 2, 2, 13, 9, '2', '1')            # board
    hline(g, 4, 11, 4, '1'); hline(g, 4, 9, 6, '1')
    return fin(g)


def fence(w, h):                               # 32x16
    g = canvas(w, h)
    for x in range(2, w, 8):
        box(g, x, 2, x + 1, h - 1, '1', None)  # posts
    hline(g, 0, w - 1, 5, '2'); hline(g, 0, w - 1, 10, '2')  # rails
    return fin(g)


def banner(w, h):                              # 60x30 big hanging banner
    g = canvas(w, h)
    box(g, 1, 0, w - 2, 1, '1')                # rod
    box(g, 3, 2, w - 4, h - 6, '1', '1')       # cloth
    box(g, 4, 3, w - 5, h - 7, '2')
    for x in range(8, w - 6, 10):              # vertical stripes
        vline(g, x, 3, h - 7, '1')
    disc(g, w / 2, h / 2 - 1, 7, 7, '3', '1')  # emblem
    diamond(g, w // 2, (h - 7), 4, '3', '1')   # pennant tip
    for x in range(4, w - 5, 6):               # frayed bottom
        px(g, x, h - 5, '1')
    return fin(g)


def well(w, h):                                # 32x24
    g = canvas(w, h)
    box(g, 1, h - 9, w - 2, h - 1, '1', '1')   # stone ring
    box(g, 2, h - 8, w - 3, h - 2, '2')
    box(g, 5, h - 6, w - 6, h - 2, '0')        # dark opening
    box(g, 0, 0, w - 1, 1, '3')                # roof beam
    vline(g, 2, 1, h - 9, '1'); vline(g, w - 3, 1, h - 9, '1')  # posts
    return fin(g)


# ---- 4bpp transparent actors. ----
def hero():
    return rows("0000011111100000", "0000111111110000", "0001111111111000",
                "0001222222221000", "0001232232321000", "0001222222221000",
                "0000122222210000", "0000444444440000", "0004444444444000",
                "0004444444444000", "0004444444444000", "0000455555540000",
                "0000555005550000", "0000555005550000", "0000666006660000",
                "0006660000666000")


def slime():
    return rows("0000000000000000", "0000000000000000", "0000000000000000",
                "0000000000000000", "0000011111100000", "0000111111110000",
                "0001113113111000", "0011111111111100", "0111111111111110",
                "0111111111111110", "0111111111111110", "0111111111111110",
                "0011111111111100", "0001111111111000", "0021021021021200",
                "0000000000000000")


def bat():
    return rows("0000000000000000", "0000000000000000", "0000000000000000",
                "0100000000000010", "1310000000000131", "1331000110001331",
                "0133101111011331", "0013312112213310", "0001321111231000",
                "0000132442310000", "0000013113100000", "0000001221000000",
                "0000000110000000", "0000000000000000", "0000000000000000",
                "0000000000000000")


def skeleton(w, h):                            # 16x24 4bpp
    g = canvas(w, h)
    disc(g, 8, 4, 4, 4, '1', '2', inner=0.5)   # skull
    px(g, 6, 4, '3'); px(g, 9, 4, '3')         # eye sockets (transparent? use dark)
    g[4][6] = '3'; g[4][9] = '3'
    box(g, 7, 8, 8, 16, '1')                   # spine
    for ry in (10, 13, 16):                    # ribs
        hline(g, 4, 11, ry, '2')
    box(g, 5, 17, 6, 22, '1'); box(g, 9, 17, 10, 22, '1')  # legs
    return fin(g)


def boss(w, h):                                # 48x48 4bpp big enemy (golem)
    g = canvas(w, h)
    disc(g, w / 2, h * 0.55, w * 0.42, h * 0.40, '1', '2', inner=0.7)  # body
    disc(g, w / 2, h * 0.30, w * 0.22, h * 0.18, '1', '2', inner=0.6)  # head
    g[int(h * 0.28)][int(w * 0.42)] = '4'; g[int(h * 0.28)][int(w * 0.58)] = '4'  # eyes
    box(g, 4, int(h * 0.5), 10, int(h * 0.8), '2', '1')   # left arm
    box(g, w - 11, int(h * 0.5), w - 5, int(h * 0.8), '2', '1')  # right arm
    for (cx, cy) in [(0.40, 0.55), (0.60, 0.6), (0.5, 0.7)]:      # cracks/glow
        g[int(h * cy)][int(w * cx)] = '3'
    return fin(g)


def bird():
    g = canvas(16, 16)
    disc(g, 8, 9, 4, 3, '1', '2')              # body
    px(g, 11, 7, '1'); px(g, 12, 7, '1')       # head
    px(g, 13, 8, '3')                          # beak
    hline(g, 3, 6, 7, '2'); hline(g, 3, 5, 6, '2')  # wing
    return fin(g)


def fish():
    g = canvas(16, 16)
    disc(g, 7, 8, 5, 3, '1', '2')
    px(g, 11, 7, '3')
    g[8][3] = '0'; g[7][2] = '2'; g[8][2] = '2'; g[9][2] = '2'  # tail
    hline(g, 12, 14, 6, '2'); hline(g, 12, 14, 10, '2')
    return fin(g)


def cloud(w, h):                               # 32x16 transparent cloud
    g = canvas(w, h)
    disc(g, 10, 10, 7, 5, '2', '1')
    disc(g, 20, 9, 8, 5, '2', '1')
    disc(g, 26, 11, 5, 4, '2', '1')
    disc(g, 15, 7, 6, 4, '3', '2')
    return fin(g)


# ======================================================================
#  Palettes + the 64-sprite table. (name, (art,w,h), palette, fmt)
# ======================================================================
TR = (0, 0, 0)
GRN, GRN_D, GRN_L = (70, 160, 60), (40, 110, 45), (130, 200, 90)
BRN, BRN_D, BRN_L = (120, 80, 45), (80, 52, 30), (165, 115, 70)
GRY, GRY_D, GRY_L = (110, 112, 122), (60, 62, 74), (175, 178, 190)
BLU, BLU_D, SKYB = (70, 130, 210), (40, 85, 160), (150, 200, 240)
GOLD, GOLD_D, CREAM = (240, 200, 45), (155, 110, 20), (255, 238, 170)
RED, RED_D, PINK = (210, 60, 55), (150, 30, 35), (255, 150, 150)
WHT, NIGHT, BONE = (240, 242, 248), (28, 32, 70), (225, 222, 205)


def P(*c, n=4):
    return list(c) + [TR] * (n - len(c))


STONE = P(GRY_D, GRY, GRY_L)

OPAQUE = [
    ("grass",      dither(16, 16, '1', '2', '3', 9),  P(GRN, GRN_D, GRN_L), FMT2),
    ("dirt",       dither(16, 16, '1', '2', '3', 11), P(BRN, BRN_D, BRN_L), FMT2),
    ("dirt_dark",  dither(16, 16, '1', '2', '3', 11), P(BRN_D, (55, 36, 20), BRN), FMT2),
    ("stone",      bricks(16, 16, 16),                STONE, FMT2),
    ("mossy",      bricks(16, 16, 16),                P(GRY_D, GRN_D, GRN), FMT2),
    ("sand",       dither(16, 16, '2', '3', '1', 13), P((150, 120, 70), (215, 190, 125), (240, 220, 165)), FMT2),
    ("cobble",     cobblestone(16, 16),               P(GRY, GRY_D, GRY_L), FMT2),
    ("path",       dither(16, 16, '1', '2', '3', 8),  P((140, 110, 80), (110, 86, 62), (165, 140, 105)), FMT2),
    ("water",      waves(16, 16),                     P(BLU_D, BLU, SKYB), FMT2),
    ("snow",       dither(16, 16, '3', '2', '1', 15), P((180, 190, 210), (228, 234, 246), WHT), FMT2),
    ("lava",       waves(16, 16),                     P((150, 40, 10), (220, 80, 20), (255, 185, 70)), FMT2),
    ("ice",        dither(16, 16, '2', '3', '1', 17), P((140, 175, 205), (195, 222, 242), WHT), FMT2),
    ("brick",      bricks(16, 16, 8),                 P((60, 30, 26), (165, 70, 55), (205, 105, 85)), FMT2),
    ("brick_dark", bricks(16, 16, 8),                 P((40, 22, 20), (105, 50, 42), (140, 70, 56)), FMT2),
    ("wood_h",     planks(16, 16, False),             P(BRN_D, BRN, BRN_L), FMT2),
    ("wood_v",     planks(16, 16, True),              P(BRN_D, BRN, BRN_L), FMT2),
    ("plaster",    dither(16, 16, '2', '3', '1', 23), P((150, 140, 120), (210, 200, 180), (230, 222, 205)), FMT2),
    ("floor",      planks(16, 16, False),             P((90, 62, 38), (140, 100, 62), (118, 84, 52)), FMT2),
    ("roof_red",   roof(),                            P((110, 40, 38), (190, 70, 58), (230, 115, 100)), FMT2),
    ("roof_blue",  roof(),                            P((40, 55, 110), (60, 100, 180), (115, 155, 225)), FMT2),
    ("window",     window(),                          P((70, 48, 30), (120, 205, 240), (250, 235, 150)), FMT2),
    ("sky_day",    vgrad(16, 16, '1', '1', '2', '2', '3'), P(SKYB, (180, 215, 245), (210, 232, 250)), FMT2),
    ("sky_dusk",   vgrad(16, 16, '1', '1', '2', '3', '3'), P((70, 70, 135), (215, 130, 90), (250, 200, 130)), FMT2),
    ("stars",      stars(),                           P(NIGHT, (55, 60, 105), CREAM), FMT2),
    ("marble",     marble(16, 16),                    P((120, 122, 140), (175, 178, 195), (215, 218, 230), (240, 242, 250), n=16), FMT4),
    ("mountain",   mountain(64, 32),                  P(TR, (140, 158, 190), (180, 195, 218), (240, 244, 250)), FMT2),
    ("hills",      hills(64, 16),                     P(TR, (120, 170, 110), (175, 205, 150)), FMT2),
    ("cloudbank",  cloudbank(48, 24),                 P((175, 185, 205), (225, 230, 240), WHT), FMT2),
    ("stonewall",  bricks(48, 32, 8),                 STONE, FMT2),
    ("tower",      tower(32, 48),                      STONE, FMT2),
    ("boulder",    boulder(32, 32),                    P(GRY_D, GRY, GRY_L), FMT2),
    ("archway",    archway(32, 32),                    P(GRY_D, GRY, (250, 235, 150)), FMT2),
]

TRANSP = [
    ("coin",     coin8(),       P(TR, GOLD_D, GOLD, CREAM), FMT2),
    ("gem",      gem8(),        P(TR, (30, 120, 160), (60, 200, 230), (200, 250, 255)), FMT2),
    ("heart",    heart8(),      P(TR, RED_D, RED, PINK), FMT2),
    ("key",      key8(),        P(TR, GOLD_D, GOLD, CREAM), FMT2),
    ("star",     star8(),       P(TR, GOLD, CREAM), FMT2),
    ("flower_r", flower8('1'),  P(TR, RED, (90, 160, 60), CREAM), FMT2),
    ("flower_b", flower8('1'),  P(TR, (90, 110, 220), (90, 160, 60), CREAM), FMT2),
    ("butterfly", butterfly8(), P(TR, (90, 110, 220), CREAM), FMT2),
    ("potion",   potion(),      P(TR, (90, 70, 50), (180, 60, 170), CREAM), FMT2),
    ("ring",     ring(),        P(TR, GOLD, (60, 200, 230), CREAM), FMT2),
    ("apple",    apple(),       P(TR, RED, GRN_D, CREAM), FMT2),
    ("bush",     bush(),        P(TR, GRN_D, GRN, GRN_L), FMT2),
    ("mushroom", mushroom(),    P(TR, RED, BONE, CREAM), FMT2),
    ("sapling",  sapling(),     P(TR, BRN, GRN, GRN_L), FMT2),
    ("torch",    torch(),       P(TR, (120, 72, 35), (240, 140, 30), (255, 232, 95)), FMT2),
    ("lantern",  lantern(),     P(TR, (70, 50, 30), (255, 200, 90), (255, 240, 160)), FMT2),
    ("barrel",   barrel(),      P(TR, BRN_D, BRN, (90, 70, 45)), FMT2),
    ("crate",    crate(),       P(TR, BRN_D, BRN, BRN_L), FMT2),
    ("sign",     sign(),        P(TR, BRN_D, BRN_L, CREAM), FMT2),
    ("tree",     tree(32, 48),  P(TR, BRN_D, GRN, GRN_L), FMT2),
    ("chest",    chest(24, 16), P(TR, (90, 60, 30), (160, 110, 55), GOLD), FMT2),
    ("fence",    fence(32, 16), P(TR, BRN_D, BRN, BRN_L), FMT2),
    ("well",     well(32, 24),  P(TR, GRY_D, GRY, BRN), FMT2),
    ("cloud",    cloud(32, 16), P(TR, (200, 210, 228), (235, 240, 248), WHT), FMT2),
    ("banner",   banner(60, 30), P(TR, (120, 30, 35), (190, 55, 55), GOLD), FMT2),
    ("hero",     hero(),        P(TR, (45, 65, 170), (240, 195, 155), (25, 18, 18),
                                  (205, 55, 55), (95, 72, 145), (70, 45, 32), n=16), FMT4),
    ("slime",    slime(),       P(TR, (90, 205, 95), (45, 130, 55), (15, 15, 15), n=16), FMT4),
    ("bat",      bat(),         P(TR, (60, 40, 70), (120, 80, 140), (200, 160, 220), (255, 230, 90), n=16), FMT4),
    ("bird",     bird(),        P(TR, (60, 90, 180), (140, 170, 230), GOLD), FMT2),
    ("fish",     fish(),        P(TR, (210, 130, 50), (250, 190, 110), WHT), FMT2),
    ("skeleton", skeleton(16, 24), P(TR, BONE, (150, 148, 130), (40, 40, 40), n=16), FMT4),
    ("boss",     boss(48, 48),  P(TR, (120, 120, 132), (172, 170, 182), (255, 165, 60),
                                  (255, 100, 50), n=16), FMT4),
]


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Graphics")
    os.makedirs(out_dir, exist_ok=True)
    tiles = OPAQUE + TRANSP
    print(f"Generating {len(tiles)} sprites into {out_dir}:")
    n_op = n_tr = 0
    for name, art, palette, fmt in tiles:
        flags, w, h = write_tile(out_dir, name, art, palette, fmt)
        opq = bool(flags & GFX_FLAG_OPAQUE)
        n_op += opq
        n_tr += not opq
        print(f"  {name:11} {w:2}x{h:<2} {'4bpp' if fmt == FMT4 else '2bpp'} "
              f"{'opaque' if opq else 'transp'}")
    print(f"total {len(tiles)}: {n_op} opaque, {n_tr} transparent")


if __name__ == "__main__":
    main()
