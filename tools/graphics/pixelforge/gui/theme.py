"""Geometry and color constants for the Pixel Forge widgets."""

from PyQt6.QtGui import QColor

# canvas zoom (pixels per canvas cell)
CELL_PX = 16
CELL_PX_MIN = 2
CELL_PX_MAX = 64
GRID_MIN_CELL_PX = 6  # hide grid lines when cells get smaller than this

# system palette swatches (16 columns x 4 rows)
SWATCH_PX = 22
SWATCH_COLS = 16

# drawing slot swatches
SLOT_PX = 34
SLOT_SPACING = 6

COLOR_GRID = QColor(64, 64, 64, 70)
COLOR_SWATCH_BORDER = QColor("#202020")
COLOR_ACTIVE_BORDER = QColor("#ffd400")
COLOR_CHECKER_LIGHT = QColor("#d4d4d4")
COLOR_CHECKER_DARK = QColor("#a8a8a8")


def draw_checker(painter, rect):
    """Fill a rect with the 2x2 checkerboard used for transparency."""
    half_w = max(1, rect.width() // 2)
    half_h = max(1, rect.height() // 2)
    painter.fillRect(rect, COLOR_CHECKER_LIGHT)
    painter.fillRect(
        rect.x() + half_w, rect.y(), rect.width() - half_w, half_h,
        COLOR_CHECKER_DARK,
    )
    painter.fillRect(
        rect.x(), rect.y() + half_h, half_w, rect.height() - half_h,
        COLOR_CHECKER_DARK,
    )
