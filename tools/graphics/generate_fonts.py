#!/usr/bin/env python3
"""Generate bitmap-font glyph assets for the GameConsole in three sizes.

Drives the Pixel Forge core (Canvas + storage) so every glyph is a real
GfxAsset .bin (+ .c companion), identical to what the editor would export.
Glyphs are authored as ASCII art below ('#' = ink, '.'/' ' = transparent) and
written one file per glyph into Assets/Gfx/fonts/{3_5,5_5,8_8}/.

  - 3x5 has distinct lowercase with descenders (traced from a reference font).
  - 5x5 is small-caps: a-z reuse the A-Z shapes (descender lowercase is
    illegible at 5px tall with no room below the baseline).
  - 8x8 has distinct lowercase with descenders.

Filenames are ASCII-coded ("065_A", "097_a", "046_period") so A/a never
collide on a case-insensitive filesystem. The .c arrays are named
font<w>x<h>_<ascii> (e.g. font5x5_065) to stay valid, unique C identifiers.

Usage:
    python3 tools/graphics/generate_fonts.py            # write all sizes
    python3 tools/graphics/generate_fonts.py --preview  # also print every glyph
    python3 tools/graphics/generate_fonts.py --dry-run  # validate only
    python3 tools/graphics/generate_fonts.py --size 5_5 # one size
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pixelforge.canvas import Canvas
from pixelforge.constants import GFX_FMT_2BPP
from pixelforge.storage import pack_pixels, save_bin, save_c_file

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONTS_DIR = os.path.join(REPO_ROOT, "Assets", "Gfx", "fonts")
C_FONT_INC_DIR = os.path.join(REPO_ROOT, "Console", "Inc", "Fonts")
C_FONT_SRC_DIR = os.path.join(REPO_ROOT, "Console", "Src", "Fonts")

# Glyph pixels are drawn with palette slot 1 (the first drawable index; slot 0
# is the transparent background). All three non-transparent slots are set to
# black, so the font renders solid black regardless of which index is used.
INK_SLOT = 1
BLACK_INDEX = 13  # system palette index 13 = (0, 0, 0)
PALETTE = [BLACK_INDEX, BLACK_INDEX, BLACK_INDEX, BLACK_INDEX]

# --- C font module (compiled straight into the Console, no .pak loader) ---
# One .h + .c per size covering the full printable-ASCII range 0x20-0x7E (96
# positions).  Undesigned codepoints get a blank glyph.  The packed pixel data
# uses the same 2bpp layout the renderer consumes (4 px/byte, MSB first), so
# each glyph's .pixels pointer drops straight into a Sprite.  The default
# palette is RGB565 white-on-transparent; pass your own via Sprite.palette for
# coloured text.
C_FONT_FIRST_CP = 0x20   # ' '
C_FONT_LAST_CP = 0x7E    # '~'
C_FONT_TABLE_LEN = C_FONT_LAST_CP - C_FONT_FIRST_CP + 1  # 95
C_FONT_PALETTE_RGB565 = [0x0000, 0xFFFF, 0xFFFF, 0xFFFF]

# Punctuation in the chosen "common subset", mapped to filesystem-safe labels.
PUNCT_LABELS = {
    " ": "space", "!": "exclaim", '"': "dquote", "#": "hash", "%": "percent",
    "&": "amp", "'": "quote", "(": "lparen", ")": "rparen", "*": "star",
    "+": "plus", ",": "comma", "-": "dash", ".": "period", "/": "slash",
    ":": "colon", ";": "semicolon", "=": "equals", "?": "question", "@": "at",
}
PUNCT = list(PUNCT_LABELS.keys())
UPPER = [chr(c) for c in range(ord("A"), ord("Z") + 1)]
LOWER = [chr(c) for c in range(ord("a"), ord("z") + 1)]
DIGITS = [chr(c) for c in range(ord("0"), ord("9") + 1)]
CHARSET = UPPER + LOWER + DIGITS + PUNCT


def label_for(ch):
    if ch in PUNCT_LABELS:
        return PUNCT_LABELS[ch]
    return ch  # letters and digits are filesystem-safe as themselves


# ---------------------------------------------------------------------------
# 3x5 font (3 wide, 5 tall). Distinct lowercase, traced from a 3x5 reference
# font image: caps/digits fill rows 0-3 (baseline row 3), lowercase use the
# full 5 rows with ascenders on row 0 and descenders on row 4.
# ---------------------------------------------------------------------------
F3 = {
    'A': ["###", "#.#", "###", "#.#", "..."],
    'B': ["##.", "###", "#.#", "###", "..."],
    'C': ["###", "#..", "#..", "###", "..."],
    'D': ["##.", "#.#", "#.#", "##.", "..."],
    'E': ["###", "##.", "#..", "###", "..."],
    'F': ["###", "#..", "##.", "#..", "..."],
    'G': ["###", "#..", "#.#", "###", "..."],
    'H': ["#.#", "###", "#.#", "#.#", "..."],
    'I': ["###", ".#.", ".#.", "###", "..."],
    'J': ["###", ".#.", ".#.", "##.", "..."],
    'K': ["#.#", "##.", "##.", "#.#", "..."],
    'L': ["#..", "#..", "#..", "###", "..."],
    'M': ["###", "###", "###", "#.#", "..."],
    'N': ["###", "#.#", "#.#", "#.#", "..."],
    'O': ["###", "#.#", "#.#", "###", "..."],
    'P': ["###", "#.#", "###", "#..", "..."],
    'Q': ["###", "#.#", "###", "..#", "..."],
    'R': ["###", "###", "##.", "#.#", "..."],
    'S': ["###", "#..", ".##", "###", "..."],
    'T': ["###", ".#.", ".#.", ".#.", "..."],
    'U': ["#.#", "#.#", "#.#", "###", "..."],
    'V': ["#.#", "#.#", "#.#", ".#.", "..."],
    'W': ["#.#", "###", "###", "###", "..."],
    'X': ["#.#", "##.", ".##", "#.#", "..."],
    'Y': ["#.#", "###", ".#.", ".#.", "..."],
    'Z': ["###", ".##", "#..", "###", "..."],
    'a': ["...", ".##", "###", "###", "..."],
    'b': ["#..", "###", "#.#", "###", "..."],
    'c': ["...", "###", "#..", "###", "..."],
    'd': ["..#", "###", "#.#", "###", "..."],
    'e': ["...", "##.", "##.", "###", "..."],
    'f': [".##", ".#.", "###", ".#.", "..."],
    'g': ["...", "###", "###", "..#", "###"],
    'h': ["#..", "###", "#.#", "#.#", "..."],
    'i': [".#.", "...", ".#.", ".#.", "..."],
    'j': ["..#", "...", "..#", "..#", ".##"],
    'k': ["#..", "#.#", "##.", "#.#", "..."],
    'l': [".#.", ".#.", ".#.", ".##", "..."],
    'm': ["...", "###", "###", "#.#", "..."],
    'n': ["...", "###", "#.#", "#.#", "..."],
    'o': ["...", "###", "#.#", "###", "..."],
    'p': ["...", "###", "#.#", "###", "#.."],
    'q': ["...", "###", "#.#", "###", "..#"],
    'r': ["...", "###", "#..", "#..", "..."],
    's': ["...", ".##", ".#.", "##.", "..."],
    't': [".#.", "###", ".#.", ".##", "..."],
    'u': ["...", "#.#", "#.#", "###", "..."],
    'v': ["...", "#.#", "#.#", ".#.", "..."],
    'w': ["...", "#.#", "###", "###", "..."],
    'x': ["...", "#.#", ".#.", "#.#", "..."],
    'y': ["...", "#.#", "###", "..#", "##."],
    'z': ["...", "###", ".#.", "..#", "##."],
    '1': [".##", "..#", "..#", "..#", "..."],
    '2': ["###", "..#", "##.", "###", "..."],
    '3': ["###", ".##", "..#", "###", "..."],
    '4': ["#.#", "#.#", "###", "..#", "..."],
    '5': ["###", "##.", "..#", "###", "..."],
    '6': ["###", "#..", "###", "###", "..."],
    '7': ["###", "..#", ".##", "..#", "..."],
    '8': ["###", "###", "#.#", "###", "..."],
    '9': ["###", "###", "..#", "###", "..."],
    '0': ["###", "#.#", "#.#", "###", "..."],
    ' ': ["...", "...", "...", "...", "..."],
    '!': [".#.", ".#.", "...", ".#.", "..."],
    '"': ["#.#", "#.#", "...", "...", "..."],
    '#': ["#.#", "###", "#.#", "###", "..."],
    '%': ["#.#", "..#", "##.", "#.#", "..."],
    '&': [".#.", "#.#", "#.#", "...", "..."],
    "'": [".#.", ".#.", "...", "...", "..."],
    '(': ["..#", ".#.", ".#.", "..#", "..."],
    ')': [".#.", "..#", "..#", ".#.", "..."],
    '*': ["#.#", ".#.", "#.#", "...", "..."],
    '+': [".#.", "###", ".#.", "...", "..."],
    ',': ["...", "...", "...", "..#", ".##"],
    '-': ["...", "###", "...", "...", "..."],
    '.': ["...", "...", "...", ".#.", "..."],
    '/': ["..#", ".#.", ".#.", "#..", "..."],
    ':': ["...", ".#.", "...", ".#.", "..."],
    ';': ["...", ".#.", "...", ".#.", "##."],
    '=': ["###", "...", "###", "...", "..."],
    '?': ["###", "..#", "...", ".#.", "..."],
    '@': ["###", "#.#", "###", "#..", "..."],
}

# ---------------------------------------------------------------------------
# 5x5 font (5 wide, 5 tall). Small-caps; a-z alias A-Z below.
# ---------------------------------------------------------------------------
F5 = {
    "A": [".###.", "#...#", "#####", "#...#", "#...#"],
    "B": ["####.", "#...#", "####.", "#...#", "####."],
    "C": [".####", "#....", "#....", "#....", ".####"],
    "D": ["####.", "#...#", "#...#", "#...#", "####."],
    "E": ["#####", "#....", "###..", "#....", "#####"],
    "F": ["#####", "#....", "###..", "#....", "#...."],
    "G": [".####", "#....", "#..##", "#...#", ".###."],
    "H": ["#...#", "#...#", "#####", "#...#", "#...#"],
    "I": ["#####", "..#..", "..#..", "..#..", "#####"],
    "J": ["..###", "...#.", "...#.", "#..#.", ".##.."],
    "K": ["#...#", "#..#.", "###..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#...#", "#...#"],
    "N": ["#...#", "##..#", "#.#.#", "#..##", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", ".###."],
    "P": ["####.", "#...#", "####.", "#....", "#...."],
    "Q": [".###.", "#...#", "#...#", "#..#.", ".##.#"],
    "R": ["####.", "#...#", "####.", "#..#.", "#...#"],
    "S": [".####", "#....", ".###.", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#.."],
    "U": ["#...#", "#...#", "#...#", "#...#", ".###."],
    "V": ["#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "W": ["#...#", "#...#", "#.#.#", "##.##", "#...#"],
    "X": ["#...#", ".#.#.", "..#..", ".#.#.", "#...#"],
    "Y": ["#...#", ".#.#.", "..#..", "..#..", "..#.."],
    "Z": ["#####", "...#.", "..#..", ".#...", "#####"],
    "0": [".###.", "#..##", "#.#.#", "##..#", ".###."],
    "1": ["..#..", ".##..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "..##.", ".#...", "#####"],
    "3": ["####.", "....#", ".###.", "....#", "####."],
    "4": ["#..#.", "#..#.", "#####", "...#.", "...#."],
    "5": ["#####", "#....", "####.", "....#", "####."],
    "6": [".###.", "#....", "####.", "#...#", ".###."],
    "7": ["#####", "...#.", "..#..", ".#...", ".#..."],
    "8": [".###.", "#...#", ".###.", "#...#", ".###."],
    "9": [".###.", "#...#", ".####", "....#", ".###."],
    " ": [".....", ".....", ".....", ".....", "....."],
    "!": ["..#..", "..#..", "..#..", ".....", "..#.."],
    '"': [".#.#.", ".#.#.", ".....", ".....", "....."],
    "#": [".#.#.", "#####", ".#.#.", "#####", ".#.#."],
    "%": ["##..#", "##.#.", "..#..", ".#.##", "#..##"],
    "&": [".##..", "#..#.", ".##..", "#..#.", ".##.#"],
    "'": ["..#..", "..#..", ".....", ".....", "....."],
    "(": ["..##.", ".#...", ".#...", ".#...", "..##."],
    ")": [".##..", "...#.", "...#.", "...#.", ".##.."],
    "*": [".....", ".#.#.", "..#..", ".#.#.", "....."],
    "+": [".....", "..#..", "#####", "..#..", "....."],
    ",": [".....", ".....", ".....", "..#..", ".#..."],
    "-": [".....", ".....", ".###.", ".....", "....."],
    ".": [".....", ".....", ".....", ".....", "..#.."],
    "/": ["....#", "...#.", "..#..", ".#...", "#...."],
    ":": [".....", "..#..", ".....", "..#..", "....."],
    ";": [".....", "..#..", ".....", "..#..", ".#..."],
    "=": [".....", ".###.", ".....", ".###.", "....."],
    "?": [".###.", "#...#", "..##.", ".....", "..#.."],
    "@": [".###.", "#..##", "#.###", "#....", ".###."],
}

# ---------------------------------------------------------------------------
# 8x8 font (8 wide, 8 tall). Distinct lowercase with descenders.
# ---------------------------------------------------------------------------
F8 = {
    "A": ["..####..", ".#....#.", ".#....#.", ".######.", ".#....#.", ".#....#.", ".#....#.", "........"],
    "B": [".#####..", ".#....#.", ".#....#.", ".#####..", ".#....#.", ".#....#.", ".#####..", "........"],
    "C": ["..####..", ".#....#.", ".#......", ".#......", ".#......", ".#....#.", "..####..", "........"],
    "D": [".#####..", ".#....#.", ".#....#.", ".#....#.", ".#....#.", ".#....#.", ".#####..", "........"],
    "E": [".######.", ".#......", ".#......", ".#####..", ".#......", ".#......", ".######.", "........"],
    "F": [".######.", ".#......", ".#......", ".#####..", ".#......", ".#......", ".#......", "........"],
    "G": ["..####..", ".#....#.", ".#......", ".#..###.", ".#....#.", ".#....#.", "..####..", "........"],
    "H": [".#....#.", ".#....#.", ".#....#.", ".######.", ".#....#.", ".#....#.", ".#....#.", "........"],
    "I": [".#####..", "...#....", "...#....", "...#....", "...#....", "...#....", ".#####..", "........"],
    "J": ["...####.", ".....#..", ".....#..", ".....#..", ".#...#..", ".#...#..", "..###...", "........"],
    "K": [".#....#.", ".#...#..", ".#..#...", ".###....", ".#..#...", ".#...#..", ".#....#.", "........"],
    "L": [".#......", ".#......", ".#......", ".#......", ".#......", ".#......", ".######.", "........"],
    "M": [".#....#.", ".##..##.", ".#.##.#.", ".#....#.", ".#....#.", ".#....#.", ".#....#.", "........"],
    "N": [".#....#.", ".##...#.", ".#.#..#.", ".#..#.#.", ".#...##.", ".#....#.", ".#....#.", "........"],
    "O": ["..####..", ".#....#.", ".#....#.", ".#....#.", ".#....#.", ".#....#.", "..####..", "........"],
    "P": [".#####..", ".#....#.", ".#....#.", ".#####..", ".#......", ".#......", ".#......", "........"],
    "Q": ["..####..", ".#....#.", ".#....#.", ".#....#.", ".#..#.#.", ".#...#..", "..###.#.", "........"],
    "R": [".#####..", ".#....#.", ".#....#.", ".#####..", ".#..#...", ".#...#..", ".#....#.", "........"],
    "S": ["..####..", ".#....#.", ".#......", "..####..", "......#.", ".#....#.", "..####..", "........"],
    "T": [".######.", "...#....", "...#....", "...#....", "...#....", "...#....", "...#....", "........"],
    "U": [".#....#.", ".#....#.", ".#....#.", ".#....#.", ".#....#.", ".#....#.", "..####..", "........"],
    "V": [".#....#.", ".#....#.", ".#....#.", ".#....#.", "..#..#..", "..#..#..", "...##...", "........"],
    "W": [".#....#.", ".#....#.", ".#....#.", ".#....#.", ".#.##.#.", ".##..##.", ".#....#.", "........"],
    "X": [".#....#.", "..#..#..", "...##...", "...##...", "...##...", "..#..#..", ".#....#.", "........"],
    "Y": [".#....#.", "..#..#..", "...##...", "....#...", "....#...", "....#...", "....#...", "........"],
    "Z": [".######.", "......#.", ".....#..", "...##...", ".#......", "#.......", ".######.", "........"],
    "a": ["........", "........", "..####..", "......#.", "..#####.", ".#....#.", "..#####.", "........"],
    "b": [".#......", ".#......", ".#####..", ".#....#.", ".#....#.", ".#....#.", ".#####..", "........"],
    "c": ["........", "........", "..#####.", ".#......", ".#......", ".#......", "..#####.", "........"],
    "d": ["......#.", "......#.", "..#####.", ".#....#.", ".#....#.", ".#....#.", "..#####.", "........"],
    "e": ["........", "........", "..####..", ".#....#.", ".######.", ".#......", "..####..", "........"],
    "f": ["...###..", "..#.....", ".####...", "..#.....", "..#.....", "..#.....", "..#.....", "........"],
    "g": ["........", "........", "..#####.", ".#....#.", ".#....#.", "..#####.", "......#.", "..####.."],
    "h": [".#......", ".#......", ".#####..", ".#....#.", ".#....#.", ".#....#.", ".#....#.", "........"],
    "i": ["...#....", "........", "..##....", "...#....", "...#....", "...#....", "..###...", "........"],
    "j": [".....#..", "........", "....##..", ".....#..", ".....#..", ".....#..", ".#...#..", "..###..."],
    "k": [".#......", ".#......", ".#...#..", ".#..#...", ".###....", ".#..#...", ".#...#..", "........"],
    "l": ["..##....", "...#....", "...#....", "...#....", "...#....", "...#....", "....##..", "........"],
    "m": ["........", "........", ".##.##..", ".#.#.#..", ".#.#.#..", ".#.#.#..", ".#.#.#..", "........"],
    "n": ["........", "........", ".#####..", ".#....#.", ".#....#.", ".#....#.", ".#....#.", "........"],
    "o": ["........", "........", "..####..", ".#....#.", ".#....#.", ".#....#.", "..####..", "........"],
    "p": ["........", "........", ".#####..", ".#....#.", ".#....#.", ".#####..", ".#......", ".#......"],
    "q": ["........", "........", "..#####.", ".#....#.", ".#....#.", "..#####.", "......#.", "......#."],
    "r": ["........", "........", ".#.###..", ".##.....", ".#......", ".#......", ".#......", "........"],
    "s": ["........", "........", "..#####.", ".#......", "..####..", "......#.", ".#####..", "........"],
    "t": ["..#.....", "..#.....", ".####...", "..#.....", "..#.....", "..#.....", "...###..", "........"],
    "u": ["........", "........", ".#....#.", ".#....#.", ".#....#.", ".#....#.", "..#####.", "........"],
    "v": ["........", "........", ".#....#.", ".#....#.", "..#..#..", "..#..#..", "...##...", "........"],
    "w": ["........", "........", ".#....#.", ".#....#.", ".#.##.#.", ".##..##.", ".#....#.", "........"],
    "x": ["........", "........", ".#....#.", "..#..#..", "...##...", "..#..#..", ".#....#.", "........"],
    "y": ["........", "........", ".#....#.", ".#....#.", ".#....#.", "..#####.", "......#.", "..####.."],
    "z": ["........", "........", ".######.", "....#...", "...#....", "..#.....", ".######.", "........"],
    "0": ["..####..", ".#....#.", ".#...##.", ".#..#.#.", ".##...#.", ".#....#.", "..####..", "........"],
    "1": ["...##...", "..###...", "...##...", "...##...", "...##...", "...##...", "..####..", "........"],
    "2": ["..####..", ".#....#.", "......#.", "....##..", "..##....", ".#......", ".######.", "........"],
    "3": ["..####..", ".#....#.", "......#.", "...###..", "......#.", ".#....#.", "..####..", "........"],
    "4": ["....##..", "...#.#..", "..#..#..", ".#...#..", ".######.", ".....#..", ".....#..", "........"],
    "5": [".######.", ".#......", ".#####..", "......#.", "......#.", ".#....#.", "..####..", "........"],
    "6": ["..####..", ".#....#.", ".#......", ".#####..", ".#....#.", ".#....#.", "..####..", "........"],
    "7": [".######.", "......#.", ".....#..", "....#...", "...#....", "...#....", "...#....", "........"],
    "8": ["..####..", ".#....#.", ".#....#.", "..####..", ".#....#.", ".#....#.", "..####..", "........"],
    "9": ["..####..", ".#....#.", ".#....#.", "..#####.", "......#.", ".#....#.", "..####..", "........"],
    " ": ["........", "........", "........", "........", "........", "........", "........", "........"],
    "!": ["...##...", "...##...", "...##...", "...##...", "...##...", "........", "...##...", "........"],
    '"': ["..#..#..", "..#..#..", "..#..#..", "........", "........", "........", "........", "........"],
    "#": ["..#..#..", "..#..#..", ".######.", "..#..#..", ".######.", "..#..#..", "..#..#..", "........"],
    "%": [".##...#.", ".##..#..", "....#...", "...#....", "..#..##.", ".#...##.", "#.......", "........"],
    "&": ["..###...", ".#...#..", ".#...#..", "..###...", ".#.#..#.", ".#..##..", "..###.#.", "........"],
    "'": ["...##...", "...##...", "...#....", "........", "........", "........", "........", "........"],
    "(": ["....#...", "...#....", "..#.....", "..#.....", "..#.....", "...#....", "....#...", "........"],
    ")": ["...#....", "....#...", ".....#..", ".....#..", ".....#..", "....#...", "...#....", "........"],
    "*": ["........", "...#....", ".#.#.#..", "..###...", ".#.#.#..", "...#....", "........", "........"],
    "+": ["........", "...#....", "...#....", ".#####..", "...#....", "...#....", "........", "........"],
    ",": ["........", "........", "........", "........", "........", "...##...", "...#....", "..#....."],
    "-": ["........", "........", "........", ".#####..", "........", "........", "........", "........"],
    ".": ["........", "........", "........", "........", "........", "...##...", "...##...", "........"],
    "/": ["......#.", ".....#..", "....#...", "...#....", "..#.....", ".#......", "#.......", "........"],
    ":": ["........", "...##...", "...##...", "........", "...##...", "...##...", "........", "........"],
    ";": ["........", "...##...", "...##...", "........", "...##...", "...#....", "..#.....", "........"],
    "=": ["........", "........", ".#####..", "........", ".#####..", "........", "........", "........"],
    "?": ["..####..", ".#....#.", "......#.", "....##..", "...#....", "........", "...#....", "........"],
    "@": ["..####..", ".#....#.", "#..##.#.", "#.#..##.", "#.#####.", "#.......", "..####..", "........"],
}

FONTS = {
    "3_5": (3, 5, F3),
    "5_5": (5, 5, F5),
    "8_8": (8, 8, F8),
}


def resolve_glyph(table, ch):
    """Glyph rows for a char, aliasing lowercase to uppercase for small-caps fonts."""
    if ch in table:
        return table[ch]
    if ch.isalpha() and ch.upper() in table:
        return table[ch.upper()]
    return None


def validate(name, w, h, table):
    """Return a list of dimension problems for the whole charset."""
    problems = []
    for ch in CHARSET:
        rows = resolve_glyph(table, ch)
        if rows is None:
            problems.append(f"{name}: missing glyph for {ch!r}")
            continue
        if len(rows) != h:
            problems.append(f"{name}: {ch!r} has {len(rows)} rows, expected {h}")
        for i, row in enumerate(rows):
            if len(row) != w:
                problems.append(f"{name}: {ch!r} row {i} is {len(row)} wide, expected {w}")
    return problems


def build_canvas(rows, w, h):
    canvas = Canvas(w, h, GFX_FMT_2BPP, palette=PALETTE)
    for y, row in enumerate(rows):
        for x, cell in enumerate(row):
            if cell == "#":
                canvas.set(x, y, INK_SLOT)
    return canvas


def preview(name, w, h, table):
    print(f"\n=== {name} ({w}x{h}) ===")
    for ch in CHARSET:
        rows = resolve_glyph(table, ch)
        print(f"[{ch if ch != ' ' else 'SPACE'}] U+{ord(ch):04X}")
        for row in rows:
            print("  " + row.replace("#", "█").replace(".", "·"))


def generate(size, w, h, table, dry_run):
    out_dir = os.path.join(FONTS_DIR, size)
    os.makedirs(out_dir, exist_ok=True)
    count = 0
    for ch in CHARSET:
        rows = resolve_glyph(table, ch)
        canvas = build_canvas(rows, w, h)
        stem = f"{ord(ch):03d}_{label_for(ch)}"
        cname = f"font{w}x{h}_{ord(ch):03d}"
        if not dry_run:
            save_bin(canvas, os.path.join(out_dir, stem + ".bin"))
            save_c_file(canvas, os.path.join(out_dir, stem + ".c"), cname)
        count += 1
    return count, out_dir


def _c_byte_line(values, indent=8):
    s = " " * indent
    return s + ", ".join(f"0x{b:02X}" for b in values) + ","


def _write_c_font_module(label, w, h, table):
    """Write font<W>x<H>.h → Console/Inc/Fonts/ and .c → Console/Src/Fonts/.

    The generated code is self-contained: compile-and-include — no .pak
    dependency.  A FatFont struct holds an O(1) glyph table indexed by
    codepoint-0x20, a shared palette, and the uniform glyph dimensions so
    the caller can set up a Sprite without hard-coding sizes.

    Blanks fill every ASCII position the font doesn't define, so sparse
    designs (like our 82-glyph "common subset") still map cleanly.
    """
    macro = label.upper()
    size_name = f"FONT_{label[4:]}".upper().replace("X", "x")  # font3x5 -> FONT_3x5
    os.makedirs(C_FONT_INC_DIR, exist_ok=True)
    os.makedirs(C_FONT_SRC_DIR, exist_ok=True)
    # Build packed data for every codepoint 0x20-0x7E
    glyph_data = {}  # cp -> bytes (or b"" for blank)
    for cp in range(C_FONT_FIRST_CP, C_FONT_LAST_CP + 1):
        ch = chr(cp)
        if ch in table:
            canvas = build_canvas(table[ch], w, h)
            glyph_data[cp] = pack_pixels(canvas)
        elif ch.isalpha() and ch.upper() in table:
            canvas = build_canvas(table[ch.upper()], w, h)
            glyph_data[cp] = pack_pixels(canvas)
        else:
            # blank glyph — all transparent, same byte size as any real glyph
            blank_canvas = build_canvas(["." * w] * h, w, h)
            glyph_data[cp] = b"\x00" * len(pack_pixels(blank_canvas))

    stride = len(glyph_data[C_FONT_FIRST_CP]) // h

    # --- shared font_types.h (written first, idempotent) ---
    types_path = os.path.join(C_FONT_INC_DIR, "font_types.h")
    if not os.path.exists(types_path):
        with open(types_path, "w") as f:
            f.write("// Generated by tools/graphics/generate_fonts.py\n")
            f.write("#ifndef __FONT_TYPES_H\n")
            f.write("#define __FONT_TYPES_H\n\n")
            f.write("#include <stdint.h>\n\n")
            f.write("#define FONT_TABLE_LEN 95U  // 0x20-0x7E\n\n")
            f.write("/** Which built-in console font size. */\n")
            f.write("typedef enum {\n")
            f.write("    FONT_3x5 = 0,\n")
            f.write("    FONT_5x5 = 1,\n")
            f.write("    FONT_8x8 = 2,\n")
            f.write("} FontSize;\n\n")
            f.write("/** One glyph in the font. .pixels drops straight into a Sprite. */\n")
            f.write("typedef struct {\n")
            f.write("    const uint8_t *pixels;\n")
            f.write("} FontGlyph;\n\n")
            f.write("/** Full printable-ASCII font, indexed glyphs[codepoint - 0x20]. */\n")
            f.write("typedef struct {\n")
            f.write("    const FontGlyph glyphs[FONT_TABLE_LEN];\n")
            f.write("    const uint16_t palette[4];\n")
            f.write("    uint8_t glyph_w, glyph_h;\n")
            f.write("    FontSize  size;\n")
            f.write("} Font;\n\n")
            f.write("#endif /* __FONT_TYPES_H */\n")

    # --- per-size .h (Console/Inc/Fonts/) ---
    h_path = os.path.join(C_FONT_INC_DIR, f"{label}.h")
    with open(h_path, "w") as f:
        header_guard = f"__{macro}_H"
        f.write("// Generated by tools/graphics/generate_fonts.py\n")
        f.write(f"#ifndef {header_guard}\n")
        f.write(f"#define {header_guard}\n\n")
        f.write('#include "font_types.h"\n\n')
        f.write(f"#define {macro}_GLYPH_W {w}U\n")
        f.write(f"#define {macro}_GLYPH_H {h}U\n")
        f.write(f"#define {macro}_GLYPH_STRIDE {stride}U\n")
        f.write(f"#define {macro}_FONT_SIZE {size_name}\n\n")
        f.write(f"extern const Font {label};\n\n")
        f.write(f"#endif /* {header_guard} */\n")

    # --- .c (Console/Src/Fonts/) ---
    c_path = os.path.join(C_FONT_SRC_DIR, f"{label}.c")
    with open(c_path, "w") as f:
        f.write("// Generated by tools/graphics/generate_fonts.py\n")
        f.write(f'#include "{label}.h"\n\n')
        f.write("// Packed 2bpp glyph data (4 px/byte, MSB-first row-major)\n")
        for cp in range(C_FONT_FIRST_CP, C_FONT_LAST_CP + 1):
            data = glyph_data[cp]
            name = f"_{label}_glyph_data_{(cp):03d}"
            if data and any(b for b in data):
                f.write(f"static const uint8_t {name}[{len(data)}U] = {{\n")
                for i in range(0, len(data), stride):
                    f.write(_c_byte_line(data[i : i + stride]) + "\n")
                f.write("};\n")
            else:
                # All-zero data doesn't need storage in flash — use a shared blank
                pass
        blank_name = f"_{label}_glyph_blank"
        blank_size = stride * h
        f.write(f"\n// shared blank glyph ({blank_size} zero bytes)\n")
        f.write(f"static const uint8_t {blank_name}[{blank_size}U] = {{0}};\n\n")

        f.write(f"const Font {label} = {{\n")
        f.write("    .glyphs = {\n")
        for i, cp in enumerate(range(C_FONT_FIRST_CP, C_FONT_LAST_CP + 1)):
            data = glyph_data[cp]
            ch = chr(cp)
            # Use a loose-ish Unicode comment — wide chars are escaped
            label_ch = ch if 0x21 <= cp <= 0x7E and ch != '"' and ch != "\\" else ""
            comment = f" // 0x{cp:02X} {label_ch}" if label_ch else f" // 0x{cp:02X}"
            if data and any(b for b in data):
                f.write(f"        [{(cp - C_FONT_FIRST_CP):3d}U] = {{.pixels = _{label}_glyph_data_{cp:03d}}},{comment}\n")
            else:
                f.write(f"        [{(cp - C_FONT_FIRST_CP):3d}U] = {{.pixels = {blank_name}}},{comment}\n")
        f.write("    },\n")
        f.write("    .palette = {\n")
        f.write(_c_word_line(C_FONT_PALETTE_RGB565) + "\n")
        f.write("    },\n")
        f.write(f"    .glyph_w = {macro}_GLYPH_W,\n")
        f.write(f"    .glyph_h = {macro}_GLYPH_H,\n")
        f.write(f"    .size    = {size_name},\n")
        f.write("};\n")

    return h_path, c_path


def _c_word_line(words, indent=8):
    """uint16_t comma-list, hex-padded to 4 digits."""
    return " " * indent + ", ".join(f"0x{w:04X}U" for w in words) + ","


def main():
    parser = argparse.ArgumentParser(description="Generate GameConsole bitmap fonts.")
    parser.add_argument("--size", choices=list(FONTS), help="only this size")
    parser.add_argument("--preview", action="store_true", help="print every glyph")
    parser.add_argument("--dry-run", action="store_true", help="validate, do not write")
    parser.add_argument("--no-bin", action="store_true",
                        help="skip per-glyph .bin/.c; emit only C font modules")
    args = parser.parse_args()

    sizes = [args.size] if args.size else list(FONTS)

    problems = []
    for size in sizes:
        w, h, table = FONTS[size]
        problems += validate(size, w, h, table)
    if problems:
        print("Validation failed:")
        for p in problems:
            print("  " + p)
        return 1

    for size in sizes:
        w, h, table = FONTS[size]
        label = f"font{w}x{h}"
        if args.preview:
            preview(size, w, h, table)
        kwargs = dict(dry_run=args.dry_run)
        if not args.no_bin:
            count, _ = generate(size, w, h, table, **kwargs)
            action = "validated" if args.dry_run else "wrote"
            print(f"{size}: {action} {count} per-glyph .bin/.c")
        h_path, c_path = _write_c_font_module(label, w, h, table)
        if args.dry_run:
            print(f"       validated C module ({label}.h + {label}.c)")
        else:
            print(f"       wrote {h_path} + {c_path}")
    print(f"\nCharset: {len(CHARSET)} typed glyphs ({len(UPPER)} upper, {len(LOWER)} lower, "
          f"{len(DIGITS)} digit, {len(PUNCT)} punct) → {C_FONT_TABLE_LEN}-entry 0x20-0x7E table")
    return 0


if __name__ == "__main__":
    sys.exit(main())
