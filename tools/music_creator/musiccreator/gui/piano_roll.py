# This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
"""The piano roll grid and its fixed piano-key gutter.

PianoRoll is a view/controller over a Timeline: it owns view state (grid
step, cursor, selection, playhead) and translates mouse input into Timeline
edits, notifying the main window through Qt signals. PianoGutter is the
fixed key column on the left, vertically synced with the roll's scroll
position by the main window.
"""

import math

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QPainter, QPen
from PyQt6.QtWidgets import QWidget

from ..constants import DEFAULT_DURATION_MS, DEFAULT_STEP_MS, UINT16_MAX
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


class PianoRoll(QWidget):
    """Time/pitch grid editing a Timeline.

    Mouse: left click places a note (drag right to stretch) or selects an
    existing one; right click deletes. Coordinates snap to the grid step.
    """

    selection_changed = pyqtSignal(object)  # TimelineNote | None
    preview_requested = pyqtSignal(int)  # frequency_hz
    content_changed = pyqtSignal()

    def __init__(self, pitches_desc):
        super().__init__()
        self.pitches = pitches_desc  # row 0 = highest pitch
        self.freq_row = {freq: row for row, (_n, freq) in enumerate(pitches_desc)}
        self.timeline = Timeline()
        self.selected = None
        self.step_ms = DEFAULT_STEP_MS
        self.note_len_ms = DEFAULT_DURATION_MS
        self.cursor_ms = 0
        self.playhead_ms = None
        self._stretching = False
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.update_size()

    # ---- geometry ----

    def x_of_ms(self, ms):
        return int(ms * theme.CELL_W / self.step_ms)

    def ms_of_x(self, x):
        return max(0, int(x * self.step_ms / theme.CELL_W))

    def snap(self, ms):
        return (ms // self.step_ms) * self.step_ms

    def update_size(self):
        cols = max(
            theme.MIN_COLS,
            math.ceil(self.timeline.end_ms() / self.step_ms) + 16,
            self.cursor_ms // self.step_ms + 8,
        )
        self.setFixedSize(cols * theme.CELL_W, len(self.pitches) * theme.CELL_H)
        self.update()

    # ---- editing ----

    def select(self, note):
        self.selected = note
        self.selection_changed.emit(note)
        self.update()

    def _keep_selection_valid(self):
        if self.selected is not None and self.selected not in self.timeline.notes:
            self.select(None)

    def place(self, start_ms, duration_ms, frequency_hz, preview=True):
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

    def delete_selected(self):
        if self.selected is not None:
            self.timeline.remove(self.selected)
            self.select(None)
            self.content_changed.emit()
            self.update()

    def resize_selected(self, duration_ms):
        if self.selected is None:
            return
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

    def set_step(self, step_ms):
        self.step_ms = step_ms
        self.cursor_ms = self.snap(self.cursor_ms)
        self.update_size()

    def set_content(self, sequence):
        """Replace the timeline with a loaded buzzer sequence."""
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
        for col in range(width // theme.CELL_W + 1):
            x = col * theme.CELL_W
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
        painter.fillRect(cursor_x, 0, theme.CELL_W, height, theme.COLOR_CURSOR)
        for note in self.timeline:
            row = self.freq_row.get(note.frequency_hz)
            if row is None:
                continue
            x = self.x_of_ms(note.start_ms)
            w = max(4, self.x_of_ms(note.end_ms) - x)
            color = (
                theme.COLOR_NOTE_SELECTED
                if note is self.selected
                else theme.COLOR_NOTE
            )
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
        note = self.timeline.note_at(exact_ms, frequency_hz)
        if event.button() == Qt.MouseButton.RightButton:
            if note is not None:
                self.timeline.remove(note)
                self._keep_selection_valid()
                self.content_changed.emit()
                self.update()
            return
        if event.button() != Qt.MouseButton.LeftButton:
            return
        if note is not None:
            self.select(note)
            self.cursor_ms = self.snap(note.end_ms)
            self.update()
            return
        self.place(self.snap(exact_ms), self.note_len_ms, frequency_hz)
        self._stretching = True

    def mouseMoveEvent(self, event):
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

    def mouseReleaseEvent(self, _event):
        self._stretching = False
