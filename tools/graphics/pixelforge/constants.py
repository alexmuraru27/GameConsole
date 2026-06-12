"""Shared constants and default paths."""

import os

PACKAGE_DIR = os.path.dirname(os.path.abspath(__file__))
TOOL_DIR = os.path.dirname(PACKAGE_DIR)

DEFAULT_OUTPUT_DIR = "."
DEFAULT_FILENAME = "picture"
DEFAULT_WIDTH = 32
DEFAULT_HEIGHT = 32
MIN_SIZE = 1
MAX_SIZE = 1024

# .bin file magic ("GFX1"); see gfx_asset.h for the C-side declarations
GFX_ASSET_MAGIC = b"GFX1"

# GfxFormat values (gfx_asset.h)
GFX_FMT_2BPP = 1
GFX_FMT_4BPP = 2

BITS_PER_PIXEL = {GFX_FMT_2BPP: 2, GFX_FMT_4BPP: 4}
COLORS_PER_FORMAT = {GFX_FMT_2BPP: 4, GFX_FMT_4BPP: 16}
FORMAT_NAMES = {GFX_FMT_2BPP: "2bpp", GFX_FMT_4BPP: "4bpp"}

SYSTEM_PALETTE_SIZE = 64

# palette slots kept in the model regardless of format (the 4bpp count),
# so switching 2bpp -> 4bpp -> 2bpp never loses slot color assignments
MAX_COLOR_SLOTS = 16

# default slot -> system palette assignment; slot 0 is the transparent /
# background slot (its color is never drawn). The first four entries match
# the defaults of the old tile creator.
DEFAULT_PALETTE = [32, 10, 5, 2, 22, 39, 26, 17, 43, 35, 21, 23, 55, 16, 0, 13]
