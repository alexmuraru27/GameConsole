"""Canvas domain model (Qt-free).

A Canvas is a width x height grid of palette-slot indices (0 = the
transparent/background slot) plus the slot -> system-palette assignment and
the export format. Every mutator returns True only when it actually changed
something, so callers can push undo snapshots for real changes only.

The palette always keeps MAX_COLOR_SLOTS entries; the format only decides
how many of them are usable, so toggling 2bpp -> 4bpp -> 2bpp keeps the
upper slot colors. Narrowing the format remaps out-of-range pixels to 0.
"""

from .constants import (
    BITS_PER_PIXEL,
    COLORS_PER_FORMAT,
    DEFAULT_PALETTE,
    GFX_FMT_2BPP,
    MAX_COLOR_SLOTS,
    SYSTEM_PALETTE_SIZE,
)


class Canvas:
    def __init__(self, width, height, fmt=GFX_FMT_2BPP, palette=None, pixels=None):
        if fmt not in BITS_PER_PIXEL:
            raise ValueError(f"unsupported format {fmt}")
        self.width = int(width)
        self.height = int(height)
        self.format = fmt
        self.palette = list(DEFAULT_PALETTE)
        if palette:
            entries = [int(i) % SYSTEM_PALETTE_SIZE for i in palette[:MAX_COLOR_SLOTS]]
            self.palette[: len(entries)] = entries
        if pixels is None:
            self.pixels = bytearray(self.width * self.height)
        else:
            self.pixels = bytearray(pixels)
            if len(self.pixels) != self.width * self.height:
                raise ValueError("pixel data does not match the canvas size")

    @property
    def bits_per_pixel(self):
        return BITS_PER_PIXEL[self.format]

    @property
    def color_count(self):
        """Usable palette slots in the current format (incl. transparent 0)."""
        return COLORS_PER_FORMAT[self.format]

    # ---------------- pixels ----------------

    def get(self, x, y):
        return self.pixels[y * self.width + x]

    def set(self, x, y, slot):
        index = y * self.width + x
        if self.pixels[index] == slot:
            return False
        self.pixels[index] = slot
        return True

    def clear(self):
        if not any(self.pixels):
            return False
        self.pixels = bytearray(self.width * self.height)
        return True

    # ---------------- size ----------------

    def resize(self, new_width, new_height):
        """Grow/shrink the canvas, keeping content anchored top-left."""
        if (new_width, new_height) == (self.width, self.height):
            return False
        new_pixels = bytearray(new_width * new_height)
        copy_width = min(self.width, new_width)
        for y in range(min(self.height, new_height)):
            src = y * self.width
            dst = y * new_width
            new_pixels[dst : dst + copy_width] = self.pixels[src : src + copy_width]
        self.pixels = new_pixels
        self.width, self.height = new_width, new_height
        return True

    def cropped_pixel_count(self, new_width, new_height):
        """Drawn pixels that resize() to the given size would cut off."""
        return sum(
            1
            for y in range(self.height)
            for x in range(self.width)
            if (x >= new_width or y >= new_height) and self.get(x, y)
        )

    # ---------------- format / palette ----------------

    def out_of_range_count(self, fmt):
        """Drawn pixels that set_format(fmt) would turn transparent."""
        limit = COLORS_PER_FORMAT[fmt]
        return sum(1 for slot in self.pixels if slot >= limit)

    def set_format(self, fmt):
        if fmt not in BITS_PER_PIXEL:
            raise ValueError(f"unsupported format {fmt}")
        if fmt == self.format:
            return False
        limit = COLORS_PER_FORMAT[fmt]
        for index, slot in enumerate(self.pixels):
            if slot >= limit:
                self.pixels[index] = 0
        self.format = fmt
        return True

    def set_slot_color(self, slot, system_index):
        if self.palette[slot] == system_index:
            return False
        self.palette[slot] = system_index
        return True

    def reset_palette(self):
        if self.palette == DEFAULT_PALETTE:
            return False
        self.palette = list(DEFAULT_PALETTE)
        return True

    # ---------------- memento ----------------

    def snapshot(self):
        """Opaque immutable state for UndoStack (the memento)."""
        return (
            self.width,
            self.height,
            self.format,
            tuple(self.palette),
            bytes(self.pixels),
        )

    def restore(self, state):
        width, height, fmt, palette, pixels = state
        self.width, self.height, self.format = width, height, fmt
        self.palette = list(palette)
        self.pixels = bytearray(pixels)
