# This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
"""Geometry and color constants for the piano roll widgets."""

from PyQt6.QtGui import QColor

# piano roll geometry (pixels)
CELL_W = 24
CELL_W_MIN = 8
CELL_W_MAX = 96
CELL_H = 16
GUTTER_W = 56
RULER_H = 22
MIN_COLS = 64

PLAYHEAD_TIMER_MS = 30

COLOR_ROW_NATURAL = QColor("#fafafa")
COLOR_ROW_SHARP = QColor("#e6e6e6")
COLOR_GRID = QColor("#d0d0d0")
COLOR_BEAT = QColor("#aaaaaa")
COLOR_OCTAVE = QColor("#707070")
COLOR_NOTE = QColor("#3f8f4f")
COLOR_NOTE_BORDER = QColor("#1e4427")
COLOR_NOTE_SELECTED = QColor("#e67e22")
COLOR_NOTE_OFFGRID = QColor("#ff1b1b")
COLOR_CURSOR = QColor(80, 120, 255, 45)
COLOR_PLAYHEAD = QColor("#d63031")
COLOR_PLAY_MARKER = QColor("#8e44ad")
COLOR_RULER_BG = QColor("#ececec")
COLOR_RULER_TICK = QColor("#999999")
COLOR_RULER_TEXT = QColor("#444444")
COLOR_KEY_NATURAL = QColor("#f5f5f5")
COLOR_KEY_SHARP = QColor("#3c3c3c")
COLOR_KEY_TEXT_NATURAL = QColor("#222222")
COLOR_KEY_TEXT_SHARP = QColor("#eeeeee")
COLOR_KEY_OCTAVE_BAR = QColor("#3f8f4f")
