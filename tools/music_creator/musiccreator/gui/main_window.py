# This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
"""The main window: toolbar rows, piano roll, transport, and file handling.

Wires the Qt-free domain objects (Timeline via PianoRoll, audio.Player,
storage functions) to the widgets. All keyboard handling lives here so the
shortcuts work regardless of which child widget has focus.
"""

import os

from PyQt6.QtCore import Qt, QRegularExpression, QTimer
from PyQt6.QtGui import QKeySequence, QRegularExpressionValidator, QShortcut
from PyQt6.QtWidgets import (
    QButtonGroup,
    QComboBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSlider,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from .. import storage
from ..constants import (
    DEFAULT_DURATION_MS,
    DEFAULT_FILENAME,
    DEFAULT_STEP_MS,
    DEFAULT_VOLUME_PERCENT,
    DURATION_PRESETS_MS,
    UINT16_MAX,
)
from ..notes import SEMITONE_ORDER
from . import theme
from .piano_roll import PianoGutter, PianoRoll

NEW_NAME_ITEM = "<New Name>"

# keyboard note entry: 1-9, 0, -, + map to the 12 semitones C..B
NOTE_KEYS = {
    Qt.Key.Key_1: 0,
    Qt.Key.Key_2: 1,
    Qt.Key.Key_3: 2,
    Qt.Key.Key_4: 3,
    Qt.Key.Key_5: 4,
    Qt.Key.Key_6: 5,
    Qt.Key.Key_7: 6,
    Qt.Key.Key_8: 7,
    Qt.Key.Key_9: 8,
    Qt.Key.Key_0: 9,
    Qt.Key.Key_Minus: 10,
    Qt.Key.Key_Plus: 11,
    Qt.Key.Key_Equal: 11,  # unshifted + on most layouts
}


class MusicCreatorWindow(QMainWindow):
    def __init__(self, note_table, player, output_dir, filename):
        super().__init__()
        self.note_table = note_table
        self.player = player
        self.output_dir = output_dir
        self.current_duration_ms = DEFAULT_DURATION_MS
        self.current_octave = 4 if 4 in note_table.octaves else (
            note_table.octaves[0] if note_table.octaves else None
        )

        self.playhead_timer = QTimer(self)
        self.playhead_timer.setInterval(theme.PLAYHEAD_TIMER_MS)
        self.playhead_timer.timeout.connect(self._update_playhead)

        self.setWindowTitle("Buzzer Music Creator")
        self._build_ui(filename)
        self.gutter.set_octave(self.current_octave)
        self._update_status()
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setFocus()
        self.resize(1100, 700)
        QTimer.singleShot(0, self._scroll_to_default_octave)

        if os.path.exists(self._output_paths()[0]):
            self.load()

    # ---------------- UI construction ----------------

    def _build_ui(self, filename):
        central = QWidget()
        root = QVBoxLayout(central)
        root.addLayout(self._build_duration_row())
        root.addLayout(self._build_roll_row(), stretch=1)
        root.addLayout(self._build_bottom_row(filename))
        self.setCentralWidget(central)

        QShortcut(QKeySequence("Ctrl+S"), self, self.save_clicked)
        QShortcut(QKeySequence("Ctrl+L"), self, self.load_clicked)
        QShortcut(QKeySequence(Qt.Key.Key_Space), self, self.toggle_playback)

    def _build_duration_row(self):
        row = QHBoxLayout()
        self.preset_group = QButtonGroup(self)
        self.preset_group.setExclusive(False)
        for preset in DURATION_PRESETS_MS:
            button = QPushButton(f"{preset}")
            button.setCheckable(True)
            button.setFixedWidth(60)
            button.setChecked(preset == DEFAULT_DURATION_MS)
            button.clicked.connect(
                lambda _, p=preset: self.duration_spin.setValue(p)
            )
            self.preset_group.addButton(button, preset)
            row.addWidget(button)
        self.duration_spin = QSpinBox()
        self.duration_spin.setRange(1, UINT16_MAX)
        self.duration_spin.setValue(DEFAULT_DURATION_MS)
        self.duration_spin.setSuffix(" ms")
        self.duration_spin.setFixedWidth(100)
        self.duration_spin.setToolTip(
            "Length for new notes; edits the selected note directly"
        )
        self.duration_spin.valueChanged.connect(self.duration_changed)
        # Enter hands focus back to the window so keyboard note entry works
        self.duration_spin.lineEdit().returnPressed.connect(self.setFocus)
        row.addWidget(self.duration_spin)
        row.addSpacing(20)
        row.addWidget(QLabel("Grid"))
        self.step_spin = QSpinBox()
        self.step_spin.setRange(25, 2000)
        self.step_spin.setSingleStep(25)
        self.step_spin.setValue(DEFAULT_STEP_MS)
        self.step_spin.setSuffix(" ms")
        self.step_spin.setFixedWidth(90)
        self.step_spin.setToolTip("Time per grid cell (snapping)")
        self.step_spin.valueChanged.connect(lambda v: self.roll.set_step(v))
        self.step_spin.lineEdit().returnPressed.connect(self.setFocus)
        row.addWidget(self.step_spin)
        self.status_label = QLabel()
        row.addWidget(self.status_label)
        row.addStretch()
        return row

    def _build_roll_row(self):
        self.roll = PianoRoll(self.note_table.pitched_desc)
        self.roll.selection_changed.connect(self._roll_selection_changed)
        self.roll.preview_requested.connect(self.player.preview)
        self.roll.content_changed.connect(self._update_status)
        self.gutter = PianoGutter(self.note_table.pitched_desc)
        self.gutter.preview_requested.connect(self.player.preview)
        self.scroll = QScrollArea()
        self.scroll.setWidget(self.roll)
        self.scroll.setWidgetResizable(False)
        self.scroll.setFrameShape(QScrollArea.Shape.NoFrame)
        self.scroll.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.scroll.verticalScrollBar().valueChanged.connect(self.gutter.set_offset)
        row = QHBoxLayout()
        row.setSpacing(0)
        row.addWidget(self.gutter)
        row.addWidget(self.scroll, stretch=1)
        return row

    def _build_bottom_row(self, filename):
        row = QHBoxLayout()
        self.filename_combo = QComboBox()
        self.filename_combo.setEditable(True)
        self.filename_combo.setFixedWidth(200)
        self.filename_combo.lineEdit().setValidator(
            QRegularExpressionValidator(QRegularExpression(r"\w+"))
        )
        self.refresh_filename_list(select=filename)
        self.filename_combo.activated.connect(self._filename_selected)
        row.addWidget(self.filename_combo)
        for label, handler in [
            ("Save", self.save_clicked),
            ("Load", self.load_clicked),
            ("Clear", self.clear_clicked),
        ]:
            button = QPushButton(label)
            button.clicked.connect(handler)
            row.addWidget(button)
        row.addSpacing(30)
        play_button = QPushButton("Play")
        play_button.clicked.connect(self.play)
        row.addWidget(play_button)
        self.pause_button = QPushButton("Pause")
        self.pause_button.clicked.connect(self.pause_resume)
        row.addWidget(self.pause_button)
        stop_button = QPushButton("Stop")
        stop_button.clicked.connect(self.stop_playback)
        row.addWidget(stop_button)
        row.addSpacing(30)
        row.addWidget(QLabel("Vol"))
        volume_slider = QSlider(Qt.Orientation.Horizontal)
        volume_slider.setRange(0, 100)
        volume_slider.setValue(DEFAULT_VOLUME_PERCENT)
        volume_slider.setFixedWidth(120)
        volume_slider.valueChanged.connect(
            lambda v: self.player.set_volume(v / 100.0)
        )
        row.addWidget(volume_slider)
        row.addStretch()
        return row

    def _scroll_to_default_octave(self):
        target = f"C{self.current_octave}" if self.current_octave else None
        for row, (name, _freq) in enumerate(self.note_table.pitched_desc):
            if name == target:
                self.scroll.verticalScrollBar().setValue(
                    row * theme.CELL_H - self.scroll.viewport().height() // 2
                )
                return

    # ---------------- keyboard ----------------

    def change_octave(self, delta):
        octaves = self.note_table.octaves
        if not octaves:
            return
        idx = octaves.index(self.current_octave) + delta
        self.current_octave = octaves[max(0, min(len(octaves) - 1, idx))]
        self.gutter.set_octave(self.current_octave)

    def keyPressEvent(self, event):
        key = event.key()
        # Up/Down follow the keys visually (high pitches at the top)
        if key == Qt.Key.Key_Up:
            self.change_octave(+1)
        elif key == Qt.Key.Key_Down:
            self.change_octave(-1)
        elif key == Qt.Key.Key_Left:
            self.roll.advance_cursor(-self.roll.step_ms)
            self._ensure_cursor_visible()
        elif key == Qt.Key.Key_Right:
            self.roll.advance_cursor(self.roll.step_ms)
            self._ensure_cursor_visible()
        elif key == Qt.Key.Key_P:
            self.roll.advance_cursor(self.current_duration_ms)
            self._ensure_cursor_visible()
        elif key == Qt.Key.Key_Delete:
            self.roll.delete_selected()
        elif key == Qt.Key.Key_Escape:
            self.roll.select(None)
        elif key in NOTE_KEYS and self.current_octave is not None:
            pitch = SEMITONE_ORDER[NOTE_KEYS[key]]
            freq = self.note_table.frequency_of(pitch, self.current_octave)
            if freq is not None:
                self.roll.place_at_cursor(freq)
                self._ensure_cursor_visible()
        else:
            super().keyPressEvent(event)

    def _ensure_cursor_visible(self):
        self._scroll_horizontally_to(self.roll.x_of_ms(self.roll.cursor_ms))

    def _scroll_horizontally_to(self, x):
        bar = self.scroll.horizontalScrollBar()
        view_w = self.scroll.viewport().width()
        if x < bar.value() or x > bar.value() + view_w - theme.CELL_W * 2:
            bar.setValue(max(0, x - view_w // 2))

    # ---------------- playback ----------------

    def play(self):
        sequence = self.roll.timeline.to_sequence()
        if not sequence:
            self.statusBar().showMessage("Nothing to play", 2000)
            return
        self.stop_playback()
        self.player.play(sequence)
        self.playhead_timer.start()

    def stop_playback(self):
        self.player.stop()
        self.pause_button.setText("Pause")
        self.playhead_timer.stop()
        self.roll.set_playhead(None)

    def pause_resume(self):
        if not self.player.is_active:
            return
        if self.player.is_paused:
            self.player.resume()
            self.playhead_timer.start()
            self.pause_button.setText("Pause")
        else:
            self.player.pause()
            self.playhead_timer.stop()
            self.pause_button.setText("Resume")

    def toggle_playback(self):
        if self.player.is_active:
            self.pause_resume()
        else:
            self.play()

    def _update_playhead(self):
        if not self.player.is_active:
            self.stop_playback()
            return
        elapsed_ms = self.player.elapsed_ms()
        if elapsed_ms >= self.player.total_ms:
            self.stop_playback()
            return
        self.roll.set_playhead(int(elapsed_ms))
        self._scroll_horizontally_to(self.roll.x_of_ms(elapsed_ms))

    # ---------------- editing callbacks ----------------

    def _roll_selection_changed(self, note):
        if note is not None and note.duration_ms != self.duration_spin.value():
            self.duration_spin.blockSignals(True)
            self.duration_spin.setValue(note.duration_ms)
            self.duration_spin.blockSignals(False)
            self.duration_changed(note.duration_ms, resize=False)

    def duration_changed(self, value, resize=True):
        self.current_duration_ms = value
        self.roll.note_len_ms = value
        for button in self.preset_group.buttons():
            button.setChecked(self.preset_group.id(button) == value)
        # editing the ms box with a note selected re-times that note
        if resize and self.roll.selected is not None:
            self.roll.resize_selected(value)

    def _update_status(self):
        total_ms = self.roll.timeline.end_ms()
        entries = len(self.roll.timeline.to_sequence())
        self.status_label.setText(
            f"Notes: {len(self.roll.timeline)}  Entries: {entries}"
            f"  ({total_ms} ms / {total_ms / 1000.0:.1f} s)"
        )

    # ---------------- save / load ----------------

    def refresh_filename_list(self, select=None):
        self.filename_combo.blockSignals(True)
        self.filename_combo.clear()
        self.filename_combo.addItem(NEW_NAME_ITEM)
        self.filename_combo.addItems(storage.list_bin_names(self.output_dir))
        if select:
            self.filename_combo.setCurrentText(select)
        else:
            self.filename_combo.setCurrentIndex(0)
            self.filename_combo.clearEditText()
        self.filename_combo.blockSignals(False)

    def _filename_selected(self, _index):
        if self.filename_combo.currentText() == NEW_NAME_ITEM:
            self.filename_combo.clearEditText()
            self.filename_combo.setFocus()
        else:
            self.setFocus()

    def current_filename(self):
        text = self.filename_combo.currentText().strip()
        if not text or text == NEW_NAME_ITEM:
            return None
        return text

    def _output_paths(self):
        base = (
            self.current_filename() if hasattr(self, "filename_combo") else None
        ) or DEFAULT_FILENAME
        return (
            os.path.join(self.output_dir, base + ".bin"),
            os.path.join(self.output_dir, base + ".c"),
        )

    def _confirm(self, question):
        answer = QMessageBox.question(self, "Confirm", question)
        return answer == QMessageBox.StandardButton.Yes

    def save_clicked(self):
        sequence = self.roll.timeline.to_sequence()
        if not sequence:
            self.statusBar().showMessage("Nothing to save", 2000)
            return
        name = self.current_filename()
        if name is None:
            self.statusBar().showMessage("Enter a file name first", 3000)
            self.filename_combo.setFocus()
            return
        if not self._confirm(f'Save "{name}"?'):
            return
        os.makedirs(self.output_dir, exist_ok=True)
        bin_path, c_path = self._output_paths()
        storage.save_bin(sequence, bin_path)
        storage.save_c_file(sequence, c_path, name, self.note_table.name_of)
        self.refresh_filename_list(select=name)
        self.statusBar().showMessage(
            f"Saved {len(sequence)} entries to {bin_path} and {c_path}", 5000
        )

    def load_clicked(self):
        name = self.current_filename()
        if name is None:
            self.statusBar().showMessage("Pick a file to load first", 3000)
            return
        if not self._confirm(f'Load "{name}"?'):
            return
        self.load()

    def load(self):
        bin_path, _ = self._output_paths()
        if not os.path.exists(bin_path):
            self.statusBar().showMessage(f"File {bin_path} not found", 3000)
            return
        try:
            self.stop_playback()
            self.roll.set_content(storage.load_bin(bin_path))
            self.statusBar().showMessage(
                f"Loaded {len(self.roll.timeline)} notes from {bin_path}", 5000
            )
        except Exception as e:
            self.statusBar().showMessage(f"Failed to load: {e}", 5000)

    def clear_clicked(self):
        if not self._confirm("Clear the whole grid?"):
            return
        self.stop_playback()
        self.roll.set_content([])
        self.roll.cursor_ms = 0
        self.roll.update_size()
