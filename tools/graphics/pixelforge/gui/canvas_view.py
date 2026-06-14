"""The drawable canvas widget.

CanvasView owns the Canvas domain model and its UndoStack (the memento
caretaker): every mutating operation captures a snapshot *before* changing
anything, so pixel strokes, palette edits, resizes and format switches all
undo uniformly. One mouse stroke (press..release) is one undo step.

The widget's size is exactly the canvas size times the zoom level, so it is
meant to live inside a QScrollArea; Ctrl+wheel zooming is delegated to the
main window (which anchors the scroll position) via zoom_requested.
"""

from PyQt6.QtCore import QPoint, QRect, Qt, pyqtSignal
from PyQt6.QtGui import QColor, QPainter
from PyQt6.QtWidgets import QApplication, QWidget

from ..history import UndoStack
from ..palette import SYSTEM_PALETTE
from . import theme

_ERASE_SLOT = 0


class CanvasView(QWidget):
    content_changed = pyqtSignal()  # pixels, palette, size or format changed
    zoom_requested = pyqtSignal(int, QPoint)  # direction, anchor (widget coords)
    pan_requested = pyqtSignal(int, int)  # drag delta dx, dy in pixels

    def __init__(self, canvas):
        super().__init__()
        self._canvas = canvas
        self._history = UndoStack()
        self._stroke_state = None  # pre-stroke snapshot, pushed on first change
        self._stroke_slot = None  # slot painted by the current stroke
        # right button is decided on release: click erases, drag pans
        self._right_press_cell = None
        self._right_press_global = None
        self._pan_last_global = None
        self._pan_active = False
        self.active_slot = 1
        self.cell_px = theme.CELL_PX
        self._checker_background = True
        self._apply_widget_size()

    @property
    def canvas(self):
        return self._canvas

    def set_canvas(self, canvas):
        """Replace the whole content (load); resets the undo history."""
        self._canvas = canvas
        self._history.clear()
        self._refresh()

    # ---------------- undoable operations ----------------

    def apply_clear(self):
        return self._mutate(lambda c: c.clear())

    def apply_resize(self, width, height):
        return self._mutate(lambda c: c.resize(width, height))

    def apply_format(self, fmt):
        return self._mutate(lambda c: c.set_format(fmt))

    def apply_slot_color(self, slot, system_index):
        return self._mutate(lambda c: c.set_slot_color(slot, system_index))

    def apply_palette_reset(self):
        return self._mutate(lambda c: c.reset_palette())

    def _mutate(self, mutator):
        """Run a Canvas mutator, recording an undo snapshot if it changed."""
        before = self._canvas.snapshot()
        if not mutator(self._canvas):
            return False
        self._history.push(before)
        self._refresh()
        return True

    def undo(self):
        return self._exchange(self._history.undo)

    def redo(self):
        return self._exchange(self._history.redo)

    def _exchange(self, history_op):
        state = history_op(self._canvas.snapshot())
        if state is None:
            return False
        self._canvas.restore(state)
        self._refresh()
        return True

    # ---------------- background preview ----------------

    def set_checker_background(self, enabled):
        """Show slot-0 pixels as a checkerboard or as the slot-0 color.

        View-only: slot 0 stays the transparent/background slot in the data
        either way; its palette color is what the toggle previews.
        """
        if self._checker_background != enabled:
            self._checker_background = enabled
            self.update()

    # ---------------- zoom ----------------

    def set_cell_px(self, cell_px):
        cell_px = max(theme.CELL_PX_MIN, min(theme.CELL_PX_MAX, cell_px))
        if cell_px != self.cell_px:
            self.cell_px = cell_px
            self._apply_widget_size()
            self.update()

    def wheelEvent(self, event):
        if event.modifiers() & Qt.KeyboardModifier.ControlModifier:
            direction = 1 if event.angleDelta().y() > 0 else -1
            self.zoom_requested.emit(direction, event.position().toPoint())
            event.accept()
        else:
            event.ignore()  # let the scroll area scroll

    # ---------------- painting strokes / panning ----------------

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.RightButton:
            # decided on release: click erases the pixel, drag pans the view
            self._right_press_cell = self._cell_at(event.position().toPoint())
            self._right_press_global = event.globalPosition()
            self._pan_last_global = event.globalPosition()
            self._pan_active = False
            return
        if event.button() != Qt.MouseButton.LeftButton:
            return
        self._stroke_state = self._canvas.snapshot()
        self._stroke_slot = self.active_slot
        self._paint_cell(event.position().toPoint())

    def mouseMoveEvent(self, event):
        if (
            event.buttons() & Qt.MouseButton.RightButton
            and self._pan_last_global is not None
        ):
            global_pos = event.globalPosition()
            if not self._pan_active:
                moved = (global_pos - self._right_press_global).manhattanLength()
                if moved >= QApplication.startDragDistance():
                    self._pan_active = True
                    self._pan_last_global = global_pos
                    self.setCursor(Qt.CursorShape.ClosedHandCursor)
            else:
                delta = global_pos - self._pan_last_global
                self._pan_last_global = global_pos
                self.pan_requested.emit(int(delta.x()), int(delta.y()))
            return
        if self._stroke_slot is not None:
            self._paint_cell(event.position().toPoint())

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.RightButton:
            if not self._pan_active and self._right_press_cell is not None:
                x, y = self._right_press_cell
                self._mutate(lambda c: c.set(x, y, _ERASE_SLOT))
            self._right_press_cell = None
            self._right_press_global = None
            self._pan_last_global = None
            self._pan_active = False
            self.unsetCursor()
            return
        self._stroke_state = None
        self._stroke_slot = None

    def _paint_cell(self, point):
        cell = self._cell_at(point)
        if cell is None:
            return
        x, y = cell
        if not self._canvas.set(x, y, self._stroke_slot):
            return
        if self._stroke_state is not None:
            # first actual change of the stroke: record it as one undo step
            self._history.push(self._stroke_state)
            self._stroke_state = None
        self.update(QRect(x * self.cell_px, y * self.cell_px, self.cell_px, self.cell_px))
        self.content_changed.emit()

    def _cell_at(self, point):
        x = point.x() // self.cell_px
        y = point.y() // self.cell_px
        if 0 <= x < self._canvas.width and 0 <= y < self._canvas.height:
            return x, y
        return None

    # ---------------- rendering ----------------

    def paintEvent(self, event):
        painter = QPainter(self)
        cell = self.cell_px
        x_first = event.rect().left() // cell
        x_last = min(self._canvas.width - 1, event.rect().right() // cell)
        y_first = event.rect().top() // cell
        y_last = min(self._canvas.height - 1, event.rect().bottom() // cell)
        for y in range(y_first, y_last + 1):
            for x in range(x_first, x_last + 1):
                rect = QRect(x * cell, y * cell, cell, cell)
                slot = self._canvas.get(x, y)
                if slot == 0 and self._checker_background:
                    theme.draw_checker(painter, rect)
                else:
                    painter.fillRect(
                        rect, QColor(*SYSTEM_PALETTE[self._canvas.palette[slot]])
                    )
        if cell >= theme.GRID_MIN_CELL_PX:
            painter.setPen(theme.COLOR_GRID)
            for x in range(x_first, x_last + 2):
                painter.drawLine(x * cell, 0, x * cell, self.height())
            for y in range(y_first, y_last + 2):
                painter.drawLine(0, y * cell, self.width(), y * cell)
        self._draw_border(painter)

    def _draw_border(self, painter):
        """Outline the whole grid so its extent is visible at any zoom.

        Drawn as four edge bars (not the faint, zoom-gated grid lines) on top of
        the cells. The paintEvent clip keeps partial updates cheap and redraws
        the border wherever an edge cell is repainted over it.
        """
        border = theme.CANVAS_BORDER_PX
        width, height = self.width(), self.height()
        color = theme.COLOR_CANVAS_BORDER
        painter.fillRect(0, 0, width, border, color)            # top
        painter.fillRect(0, height - border, width, border, color)  # bottom
        painter.fillRect(0, 0, border, height, color)           # left
        painter.fillRect(width - border, 0, border, height, color)  # right

    def _apply_widget_size(self):
        self.setFixedSize(
            self._canvas.width * self.cell_px, self._canvas.height * self.cell_px
        )

    def _refresh(self):
        self._apply_widget_size()
        self.update()
        self.content_changed.emit()
