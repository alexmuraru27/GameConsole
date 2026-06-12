"""The system palette grid and the drawing slot bar.

Both widgets are passive views: they emit a signal on click and are fed
their display state by the main window, which owns the active-slot logic.
"""

from PyQt6.QtCore import QRect, Qt, pyqtSignal
from PyQt6.QtGui import QColor, QPainter, QPen
from PyQt6.QtWidgets import QWidget

from ..palette import SYSTEM_PALETTE
from . import theme


class SystemPaletteWidget(QWidget):
    """The 64-color system palette; clicking picks a color for the active slot."""

    color_picked = pyqtSignal(int)  # system palette index 0-63

    def __init__(self):
        super().__init__()
        self._highlight = None
        rows = len(SYSTEM_PALETTE) // theme.SWATCH_COLS
        self.setFixedSize(
            theme.SWATCH_COLS * theme.SWATCH_PX, rows * theme.SWATCH_PX
        )
        self.setToolTip("Click a color to assign it to the active slot below")

    def set_highlight(self, system_index):
        """Mark the color currently assigned to the active slot (or None)."""
        if self._highlight != system_index:
            self._highlight = system_index
            self.update()

    def paintEvent(self, _event):
        painter = QPainter(self)
        size = theme.SWATCH_PX
        for index, rgb in enumerate(SYSTEM_PALETTE):
            x = (index % theme.SWATCH_COLS) * size
            y = (index // theme.SWATCH_COLS) * size
            painter.fillRect(x, y, size, size, QColor(*rgb))
            painter.setPen(theme.COLOR_SWATCH_BORDER)
            painter.drawRect(x, y, size - 1, size - 1)
        if self._highlight is not None:
            x = (self._highlight % theme.SWATCH_COLS) * size
            y = (self._highlight // theme.SWATCH_COLS) * size
            painter.setPen(QPen(theme.COLOR_ACTIVE_BORDER, 2))
            painter.drawRect(x + 1, y + 1, size - 3, size - 3)

    def mousePressEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton:
            return
        point = event.position().toPoint()
        index = (
            point.y() // theme.SWATCH_PX * theme.SWATCH_COLS
            + point.x() // theme.SWATCH_PX
        )
        if 0 <= index < len(SYSTEM_PALETTE):
            self.color_picked.emit(index)


class ColorSlotBar(QWidget):
    """The drawing slots of the current format; slot 0 is transparent."""

    slot_selected = pyqtSignal(int)

    def __init__(self):
        super().__init__()
        self._colors = []
        self._active = 1
        self._checker_bg = True
        self.setFixedHeight(theme.SLOT_PX + 2 * theme.SLOT_SPACING)
        self.setToolTip(
            "Active drawing color; slot 0 erases (transparent/background,"
            " its color is the preview background)"
        )

    def set_state(self, colors, active, checker_bg=True):
        """Show the given slot QColors with the active one highlighted.

        checker_bg picks how slot 0 is drawn: as the transparency
        checkerboard or as its assigned background color.
        """
        self._colors = list(colors)
        self._active = active
        self._checker_bg = checker_bg
        self.setFixedWidth(
            len(self._colors) * (theme.SLOT_PX + theme.SLOT_SPACING)
            + theme.SLOT_SPACING
        )
        self.update()

    def paintEvent(self, _event):
        painter = QPainter(self)
        for slot, color in enumerate(self._colors):
            rect = self._slot_rect(slot)
            if slot == 0 and self._checker_bg:
                theme.draw_checker(painter, rect)
            else:
                painter.fillRect(rect, color)
            if slot == self._active:
                painter.setPen(QPen(theme.COLOR_ACTIVE_BORDER, 3))
                painter.drawRect(rect.adjusted(1, 1, -2, -2))
            else:
                painter.setPen(theme.COLOR_SWATCH_BORDER)
                painter.drawRect(rect.adjusted(0, 0, -1, -1))

    def mousePressEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton:
            return
        point = event.position().toPoint()
        for slot in range(len(self._colors)):
            if self._slot_rect(slot).contains(point):
                self.slot_selected.emit(slot)
                return

    def _slot_rect(self, slot):
        x = theme.SLOT_SPACING + slot * (theme.SLOT_PX + theme.SLOT_SPACING)
        return QRect(x, theme.SLOT_SPACING, theme.SLOT_PX, theme.SLOT_PX)
