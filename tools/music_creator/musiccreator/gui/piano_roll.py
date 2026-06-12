# This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
"""The piano roll grid and its fixed piano-key gutter.

PianoRoll is a view/controller over a Timeline: it owns view state (grid
step, cursor, selection, playhead) and translates mouse input into Timeline
edits, notifying the main window through Qt signals. PianoGutter is the
fixed key column on the left, vertically synced with the roll's scroll
position by the main window.
"""

import bisect
import math

from PyQt6.QtCore import QPoint, Qt, pyqtSignal
from PyQt6.QtGui import QBrush, QPainter, QPen, QPolygon
from PyQt6.QtWidgets import QApplication, QWidget

from ..constants import DEFAULT_DURATION_MS, DEFAULT_STEP_MS, UINT16_MAX
from ..history import UndoStack
from ..timeline import Timeline
from . import theme


class PianoGutter(QWidget):
    """Piano-key column to the left of the roll; clicking previews a pitch."""

    preview_requested = pyqtSignal(int)  # frequency_hz

    def __init__(self, pitches_desc):
        super().__init__()
        self.pitches = pitches_desc  # row 0 = highest pitch
        self.offset = 0
        self.current_octave = None
        self.setFixedWidth(theme.GUTTER_W)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)

    def set_offset(self, value):
        self.offset = value
        self.update()

    def set_octave(self, octave):
        self.current_octave = octave
        self.update()

    def paintEvent(self, _event):
        painter = QPainter(self)
        first_row = self.offset // theme.CELL_H
        last_row = min(
            len(self.pitches), (self.offset + self.height()) // theme.CELL_H + 1
        )
        for row in range(first_row, last_row):
            name, _freq = self.pitches[row]
            y = row * theme.CELL_H - self.offset
            sharp = "S" in name
            painter.fillRect(
                0, y, theme.GUTTER_W, theme.CELL_H,
                theme.COLOR_KEY_SHARP if sharp else theme.COLOR_KEY_NATURAL,
            )
            is_octave_boundary = name.startswith("C") and not sharp
            painter.setPen(theme.COLOR_OCTAVE if is_octave_boundary else theme.COLOR_GRID)
            painter.drawLine(
                0, y + theme.CELL_H - 1, theme.GUTTER_W, y + theme.CELL_H - 1
            )
            painter.setPen(
                theme.COLOR_KEY_TEXT_SHARP if sharp else theme.COLOR_KEY_TEXT_NATURAL
            )
            painter.drawText(8, y + theme.CELL_H - 4, name)
            if self.current_octave is not None and name.endswith(
                str(self.current_octave)
            ):
                painter.fillRect(0, y, 4, theme.CELL_H, theme.COLOR_KEY_OCTAVE_BAR)

    def mousePressEvent(self, event):
        row = (event.pos().y() + self.offset) // theme.CELL_H
        if 0 <= row < len(self.pitches):
            self.preview_requested.emit(self.pitches[row][1])


class TimeRuler(QWidget):
    """Clickable time bar above the roll: places the playback start marker.

    Horizontally synced with the roll's scroll position by the main window,
    like PianoGutter is vertically.
    """

    play_start_changed = pyqtSignal(int)  # ms, snapped to the grid step

    def __init__(self):
        super().__init__()
        self.offset = 0
        self.step_ms = DEFAULT_STEP_MS
        self.cell_w = theme.CELL_W
        self.play_start_ms = 0
        self.setFixedHeight(theme.RULER_H)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setToolTip("Click to set where playback starts")

    def set_offset(self, value):
        self.offset = value
        self.update()

    def set_cell_w(self, cell_w):
        self.cell_w = cell_w
        self.update()

    def set_step(self, step_ms):
        self.step_ms = step_ms
        self.play_start_ms = (self.play_start_ms // step_ms) * step_ms
        self.update()

    def set_play_start(self, ms):
        self.play_start_ms = ms
        self.update()

    def _ms_at(self, x):
        ms = (x + self.offset) * self.step_ms / self.cell_w
        return max(0, int(ms // self.step_ms) * self.step_ms)

    def paintEvent(self, _event):
        painter = QPainter(self)
        height = self.height()
        painter.fillRect(self.rect(), theme.COLOR_RULER_BG)
        first_col = self.offset // self.cell_w
        last_col = (self.offset + self.width()) // self.cell_w + 1
        for col in range(first_col, last_col):
            x = col * self.cell_w - self.offset
            major = col % 4 == 0
            painter.setPen(theme.COLOR_RULER_TICK)
            painter.drawLine(x, height - (10 if major else 5), x, height - 1)
            if major:
                painter.setPen(theme.COLOR_RULER_TEXT)
                painter.drawText(x + 3, height - 9, f"{col * self.step_ms / 1000:g}")
        painter.setPen(theme.COLOR_RULER_TICK)
        painter.drawLine(0, height - 1, self.width(), height - 1)
        # playback start marker
        marker_x = int(self.play_start_ms * self.cell_w / self.step_ms) - self.offset
        painter.setPen(QPen(theme.COLOR_PLAY_MARKER, 2))
        painter.drawLine(marker_x, 0, marker_x, height)
        painter.setBrush(QBrush(theme.COLOR_PLAY_MARKER))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawPolygon(
            QPolygon(
                [
                    QPoint(marker_x - 5, 0),
                    QPoint(marker_x + 5, 0),
                    QPoint(marker_x, 8),
                ]
            )
        )

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.play_start_changed.emit(self._ms_at(event.pos().x()))

    def mouseMoveEvent(self, event):
        if event.buttons() & Qt.MouseButton.LeftButton:
            self.play_start_changed.emit(self._ms_at(event.pos().x()))


class PianoRoll(QWidget):
    """Time/pitch grid editing a Timeline.

    Mouse: left click places a note (drag right to stretch) or selects an
    existing one; right click deletes. Coordinates snap to the grid step.
    """

    selection_changed = pyqtSignal(object)  # TimelineNote | None
    preview_requested = pyqtSignal(int)  # frequency_hz
    content_changed = pyqtSignal()
    zoom_requested = pyqtSignal(int, int)  # direction (+1/-1), anchor x
    scroll_requested = pyqtSignal(int, bool)  # wheel delta, horizontal
    pan_requested = pyqtSignal(int, int)  # drag delta dx, dy in pixels

    def __init__(self, pitches_desc):
        super().__init__()
        self.pitches = pitches_desc  # row 0 = highest pitch
        self.freq_row = {freq: row for row, (_n, freq) in enumerate(pitches_desc)}
        self._freqs_asc = sorted(self.freq_row)
        self.timeline = Timeline()
        self.history = UndoStack()
        self.selected = None
        self.step_ms = DEFAULT_STEP_MS
        self.cell_w = theme.CELL_W
        self.note_len_ms = DEFAULT_DURATION_MS
        self.cursor_ms = 0
        self.play_start_ms = 0
        self.playhead_ms = None
        self._stretching = False
        self._pan_active = False
        self._pan_last_global = None
        self._right_press_note = None
        self._right_press_global = None
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.update_size()

    # ---- geometry ----

    def x_of_ms(self, ms):
        return int(ms * self.cell_w / self.step_ms)

    def ms_of_x(self, x):
        return max(0, int(x * self.step_ms / self.cell_w))

    def snap(self, ms):
        return (ms // self.step_ms) * self.step_ms

    def row_of(self, frequency_hz):
        """Row of a pitch; off-grid frequencies map to the nearest row."""
        row = self.freq_row.get(frequency_hz)
        if row is not None:
            return row
        if not self._freqs_asc or frequency_hz <= 0:
            return None
        idx = bisect.bisect_left(self._freqs_asc, frequency_hz)
        neighbors = self._freqs_asc[max(0, idx - 1) : idx + 1]
        nearest = min(neighbors, key=lambda freq: abs(freq - frequency_hz))
        return self.freq_row[nearest]

    def note_at_row(self, ms, row):
        """The note rendered on the given row sounding at the given time."""
        for note in self.timeline:
            if self.row_of(note.frequency_hz) == row and note.start_ms <= ms < note.end_ms:
                return note
        return None

    def update_size(self):
        cols = max(
            theme.MIN_COLS,
            math.ceil(self.timeline.end_ms() / self.step_ms) + 16,
            self.cursor_ms // self.step_ms + 8,
        )
        self.setFixedSize(cols * self.cell_w, len(self.pitches) * theme.CELL_H)
        self.update()

    def set_cell_w(self, cell_w):
        """Horizontal zoom: pixel width of one grid step."""
        self.cell_w = cell_w
        self.update_size()

    # ---- editing ----

    def select(self, note):
        self.selected = note
        self.selection_changed.emit(note)
        self.update()

    def _checkpoint(self, coalesce_id=None):
        """Record the pre-mutation state for undo."""
        self.history.push(self.timeline.snapshot(), coalesce_id)

    def undo(self):
        state = self.history.undo(self.timeline.snapshot())
        if state is None:
            return False
        self.timeline.restore(state)
        self._history_restored()
        return True

    def redo(self):
        state = self.history.redo(self.timeline.snapshot())
        if state is None:
            return False
        self.timeline.restore(state)
        self._history_restored()
        return True

    def _history_restored(self):
        # restored snapshots contain fresh note objects: drop the selection
        self.select(None)
        self.content_changed.emit()
        self.update_size()

    def _keep_selection_valid(self):
        if self.selected is not None and self.selected not in self.timeline.notes:
            self.select(None)

    def place(self, start_ms, duration_ms, frequency_hz, preview=True):
        self._checkpoint()
        note = self.timeline.place(start_ms, duration_ms, frequency_hz)
        self.cursor_ms = note.end_ms
        self.select(note)
        if preview:
            self.preview_requested.emit(frequency_hz)
        self.content_changed.emit()
        self.update_size()
        return note

    def place_at_cursor(self, frequency_hz):
        self.place(self.cursor_ms, self.note_len_ms, frequency_hz)

    def delete_note(self, note):
        self._checkpoint()
        self.timeline.remove(note)
        if self.selected is note:
            self.select(None)
        self.content_changed.emit()
        self.update()

    def delete_selected(self):
        if self.selected is not None:
            self.delete_note(self.selected)

    def transpose_selected(self, delta_rows):
        """Move the selected note by grid rows (negative = higher pitch)."""
        if self.selected is None:
            return False
        row = self.row_of(self.selected.frequency_hz)
        if row is None:
            return False
        new_row = row + delta_rows
        if not 0 <= new_row < len(self.pitches):
            return False
        # a burst of transposes on the same note undoes as one step
        self._checkpoint(("transpose", id(self.selected)))
        self.selected.frequency_hz = self.pitches[new_row][1]
        self.preview_requested.emit(self.selected.frequency_hz)
        self.content_changed.emit()
        self.update()
        return True

    def resize_selected(self, duration_ms):
        if self.selected is None or self.selected.duration_ms == duration_ms:
            return
        # spinbox edits arrive digit by digit: undo the run as one step
        self._checkpoint(("resize", id(self.selected)))
        self.timeline.resize(self.selected, duration_ms)
        self._keep_selection_valid()
        self.content_changed.emit()
        self.update_size()

    def advance_cursor(self, ms):
        self.cursor_ms = max(0, self.cursor_ms + ms)
        self.update_size()

    def set_playhead(self, ms):
        if ms != self.playhead_ms:
            self.playhead_ms = ms
            self.update()

    def set_play_start(self, ms):
        self.play_start_ms = ms
        self.update()

    def set_step(self, step_ms):
        self.step_ms = step_ms
        self.cursor_ms = self.snap(self.cursor_ms)
        self.update_size()

    def set_content(self, sequence):
        """Replace the timeline with a loaded buzzer sequence."""
        self._checkpoint()
        self.timeline.replace_from_sequence(sequence)
        self.select(None)
        self.cursor_ms = self.snap(self.timeline.end_ms())
        self.content_changed.emit()
        self.update_size()

    # ---- painting ----

    def paintEvent(self, _event):
        painter = QPainter(self)
        width = self.width()
        height = self.height()
        for row, (name, _freq) in enumerate(self.pitches):
            color = (
                theme.COLOR_ROW_SHARP if "S" in name else theme.COLOR_ROW_NATURAL
            )
            painter.fillRect(0, row * theme.CELL_H, width, theme.CELL_H, color)
        for col in range(width // self.cell_w + 1):
            x = col * self.cell_w
            painter.setPen(theme.COLOR_BEAT if col % 4 == 0 else theme.COLOR_GRID)
            painter.drawLine(x, 0, x, height)
        for row, (name, _freq) in enumerate(self.pitches):
            y = (row + 1) * theme.CELL_H - 1
            is_octave_boundary = name.startswith("C") and "S" not in name
            painter.setPen(
                theme.COLOR_OCTAVE if is_octave_boundary else theme.COLOR_GRID
            )
            painter.drawLine(0, y, width, y)
        cursor_x = self.x_of_ms(self.cursor_ms)
        painter.fillRect(cursor_x, 0, self.cell_w, height, theme.COLOR_CURSOR)
        # playback start marker
        marker_x = self.x_of_ms(self.play_start_ms)
        painter.setPen(QPen(theme.COLOR_PLAY_MARKER, 1, Qt.PenStyle.DashLine))
        painter.drawLine(marker_x, 0, marker_x, height)
        for note in self.timeline:
            row = self.row_of(note.frequency_hz)
            if row is None:
                continue
            x = self.x_of_ms(note.start_ms)
            w = max(4, self.x_of_ms(note.end_ms) - x)
            if note is self.selected:
                color = theme.COLOR_NOTE_SELECTED
            elif note.frequency_hz in self.freq_row:
                color = theme.COLOR_NOTE
            else:
                # off-grid frequency (e.g. SFX): drawn on the nearest row
                color = theme.COLOR_NOTE_OFFGRID
            painter.fillRect(
                x + 1, row * theme.CELL_H + 2, w - 2, theme.CELL_H - 4, color
            )
            painter.setPen(QPen(theme.COLOR_NOTE_BORDER, 1))
            painter.drawRect(
                x + 1, row * theme.CELL_H + 2, w - 2, theme.CELL_H - 4
            )
        if self.playhead_ms is not None:
            x = self.x_of_ms(self.playhead_ms)
            painter.setPen(QPen(theme.COLOR_PLAYHEAD, 2))
            painter.drawLine(x, 0, x, height)

    # ---- mouse ----

    def mousePressEvent(self, event):
        row = event.pos().y() // theme.CELL_H
        if not 0 <= row < len(self.pitches):
            return
        frequency_hz = self.pitches[row][1]
        exact_ms = self.ms_of_x(event.pos().x())
        note = self.note_at_row(exact_ms, row)
        if event.button() == Qt.MouseButton.RightButton:
            # decided on release: click deletes, drag pans the view
            self._right_press_note = note
            self._right_press_global = event.globalPosition()
            self._pan_last_global = event.globalPosition()
            self._pan_active = False
            return
        if event.button() != Qt.MouseButton.LeftButton:
            return
        if note is not None:
            self.select(note)
            self.preview_requested.emit(note.frequency_hz)
            self.cursor_ms = self.snap(note.end_ms)
            self.update()
            return
        self.place(self.snap(exact_ms), self.note_len_ms, frequency_hz)
        self._stretching = True

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
        if not self._stretching or self.selected is None:
            return
        start = self.selected.start_ms
        end = self.snap(self.ms_of_x(max(0, event.pos().x()))) + self.step_ms
        new_duration = min(max(self.step_ms, end - start), UINT16_MAX)
        if new_duration != self.selected.duration_ms:
            self.timeline.resize(self.selected, new_duration)
            self.cursor_ms = self.selected.end_ms
            self.selection_changed.emit(self.selected)
            self.content_changed.emit()
            self.update_size()

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.RightButton:
            if not self._pan_active and self._right_press_note is not None:
                self.delete_note(self._right_press_note)
            self._pan_active = False
            self._right_press_note = None
            self._pan_last_global = None
            self.unsetCursor()
            return
        self._stretching = False

    def wheelEvent(self, event):
        # the scroll area does not see wheel events ignored here, so all
        # scrolling is forwarded explicitly through scroll_requested
        modifiers = event.modifiers()
        if modifiers & Qt.KeyboardModifier.ControlModifier:
            direction = 1 if event.angleDelta().y() > 0 else -1
            self.zoom_requested.emit(direction, int(event.position().x()))
        else:
            delta = event.angleDelta().y() or event.angleDelta().x()
            horizontal = bool(modifiers & Qt.KeyboardModifier.ShiftModifier) or (
                event.angleDelta().y() == 0
            )
            self.scroll_requested.emit(delta, horizontal)
        event.accept()
