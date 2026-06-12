"""The main window: palette bars, canvas, size/format controls, file handling.

Wires the Qt-free domain objects (Canvas via CanvasView, storage functions)
to the widgets. The window owns the active-slot state and keeps all controls
in sync with the canvas after every change (including undo/redo, which may
alter size, format and palette).
"""

import os

from PyQt6.QtCore import QPoint, QRegularExpression, Qt
from PyQt6.QtGui import QColor, QKeySequence, QRegularExpressionValidator, QShortcut
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from .. import storage
from ..constants import (
    COLORS_PER_FORMAT,
    DEFAULT_FILENAME,
    FORMAT_NAMES,
    GFX_FMT_2BPP,
    GFX_FMT_4BPP,
    MAX_SIZE,
    MIN_SIZE,
)
from ..palette import SYSTEM_PALETTE
from . import theme
from .canvas_view import CanvasView
from .confirm import confirm
from .palette_widgets import ColorSlotBar, SystemPaletteWidget

NEW_NAME_ITEM = "<New Name>"

_FORMAT_LABELS = {
    GFX_FMT_2BPP: "2bpp (3 colors + transparent)",
    GFX_FMT_4BPP: "4bpp (15 colors + transparent)",
}


class PixelForgeWindow(QMainWindow):
    def __init__(self, canvas, output_dir, filename):
        super().__init__()
        self.output_dir = output_dir
        self.active_slot = 1

        self.setWindowTitle("Pixel Forge")
        self._build_ui(canvas, filename)
        self._sync_controls()
        self.resize(1000, 760)

        if os.path.exists(self._output_paths()[0]):
            self.load()

    # ---------------- UI construction ----------------

    def _build_ui(self, canvas, filename):
        central = QWidget()
        root = QVBoxLayout(central)
        root.addLayout(self._build_palette_row())
        root.addLayout(self._build_slot_row())
        root.addWidget(self._build_canvas_area(canvas), stretch=1)
        root.addLayout(self._build_bottom_row(filename))
        self.setCentralWidget(central)

        QShortcut(QKeySequence("Ctrl+S"), self, self.save_clicked)
        QShortcut(QKeySequence("Ctrl+L"), self, self.load_clicked)
        QShortcut(QKeySequence("Ctrl+Z"), self, self.undo)
        QShortcut(QKeySequence("Ctrl+Y"), self, self.redo)
        QShortcut(QKeySequence("Ctrl+="), self, lambda: self._zoom(+1))
        QShortcut(QKeySequence("Ctrl++"), self, lambda: self._zoom(+1))
        QShortcut(QKeySequence("Ctrl+-"), self, lambda: self._zoom(-1))
        QShortcut(QKeySequence("Ctrl+0"), self, self._zoom_reset)

    def _build_palette_row(self):
        self.system_palette = SystemPaletteWidget()
        self.system_palette.color_picked.connect(self._system_color_picked)

        controls = QVBoxLayout()
        controls.addLayout(self._build_format_size_row())
        controls.addLayout(self._build_zoom_row())
        controls.addStretch()

        row = QHBoxLayout()
        row.addWidget(self.system_palette)
        row.addSpacing(20)
        row.addLayout(controls, stretch=1)
        return row

    def _build_format_size_row(self):
        row = QHBoxLayout()
        row.addWidget(QLabel("Format"))
        self.format_combo = QComboBox()
        for fmt, label in _FORMAT_LABELS.items():
            self.format_combo.addItem(label, fmt)
        self.format_combo.activated.connect(self._format_selected)
        row.addWidget(self.format_combo)
        row.addSpacing(20)
        row.addWidget(QLabel("Size"))
        self.width_spin = self._make_size_spin("Canvas width in pixels")
        row.addWidget(self.width_spin)
        row.addWidget(QLabel("×"))
        self.height_spin = self._make_size_spin("Canvas height in pixels")
        row.addWidget(self.height_spin)
        resize_button = QPushButton("Resize")
        resize_button.setToolTip(
            "Apply the new size; content stays anchored top-left"
        )
        resize_button.clicked.connect(self._resize_clicked)
        row.addWidget(resize_button)
        row.addStretch()
        return row

    def _make_size_spin(self, tooltip):
        spin = QSpinBox()
        spin.setRange(MIN_SIZE, MAX_SIZE)
        spin.setSuffix(" px")
        spin.setFixedWidth(90)
        spin.setToolTip(tooltip)
        return spin

    def _build_zoom_row(self):
        row = QHBoxLayout()
        row.addWidget(QLabel("Zoom"))
        for label, direction, tip in (
            ("-", -1, "Zoom out (Ctrl+-, Ctrl+wheel)"),
            ("+", +1, "Zoom in (Ctrl+=, Ctrl+wheel)"),
        ):
            button = QPushButton(label)
            button.setFixedWidth(28)
            button.setToolTip(tip)
            button.clicked.connect(lambda _, d=direction: self._zoom(d))
            row.addWidget(button)
        row.addSpacing(20)
        self.status_label = QLabel()
        row.addWidget(self.status_label)
        row.addStretch()
        return row

    def _build_slot_row(self):
        self.slot_bar = ColorSlotBar()
        self.slot_bar.slot_selected.connect(self._slot_selected)
        self.checker_check = QCheckBox("Checker BG")
        self.checker_check.setChecked(True)
        self.checker_check.setToolTip(
            "Show transparent pixels as a checkerboard; uncheck to preview"
            " them in the slot-0 background color"
        )
        self.checker_check.toggled.connect(self._checker_toggled)
        reset_button = QPushButton("Reset palette")
        reset_button.setToolTip("Restore the default slot colors (undoable)")
        reset_button.clicked.connect(self._reset_palette_clicked)
        row = QHBoxLayout()
        row.addWidget(self.slot_bar)
        row.addSpacing(20)
        row.addWidget(self.checker_check)
        row.addWidget(reset_button)
        row.addStretch()
        return row

    def _build_canvas_area(self, canvas):
        self.view = CanvasView(canvas)
        self.view.content_changed.connect(self._sync_controls)
        self.view.zoom_requested.connect(self._zoom)
        self.view.pan_requested.connect(self._view_panned)
        self.scroll = QScrollArea()
        self.scroll.setWidget(self.view)
        self.scroll.setWidgetResizable(False)
        self.scroll.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        return self.scroll

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
        row.addStretch()
        return row

    # ---------------- palette / slots ----------------

    def _slot_selected(self, slot):
        self.active_slot = slot
        self.view.active_slot = slot
        self._sync_controls()

    def _system_color_picked(self, system_index):
        # slot 0 stays transparent in the data; its color is the background
        # preview (shown when "Checker BG" is off)
        self.view.apply_slot_color(self.active_slot, system_index)

    def _checker_toggled(self, checked):
        self.view.set_checker_background(checked)
        self._sync_controls()

    def _reset_palette_clicked(self):
        if not self.view.apply_palette_reset():
            self.statusBar().showMessage("Palette is already the default", 2000)

    # ---------------- format / size ----------------

    def _format_selected(self, _index):
        fmt = self.format_combo.currentData()
        canvas = self.view.canvas
        if fmt == canvas.format:
            return
        lost = canvas.out_of_range_count(fmt)
        if lost and not confirm(
            self,
            f"Switching to {FORMAT_NAMES[fmt]} turns {lost} pixel(s)"
            " transparent. Continue?",
        ):
            self._sync_controls()
            return
        if self.active_slot >= COLORS_PER_FORMAT[fmt]:
            self._slot_selected(1)
        self.view.apply_format(fmt)

    def _resize_clicked(self):
        width = self.width_spin.value()
        height = self.height_spin.value()
        canvas = self.view.canvas
        if (width, height) == (canvas.width, canvas.height):
            return
        lost = canvas.cropped_pixel_count(width, height)
        if lost and not confirm(
            self, f"Resizing to {width}×{height} cuts off {lost} pixel(s). Continue?"
        ):
            self._sync_controls()
            return
        self.view.apply_resize(width, height)

    # ---------------- zoom / pan ----------------

    def _view_panned(self, dx, dy):
        # grab-and-drag: the content follows the mouse
        hbar = self.scroll.horizontalScrollBar()
        vbar = self.scroll.verticalScrollBar()
        hbar.setValue(hbar.value() - dx)
        vbar.setValue(vbar.value() - dy)

    def _zoom(self, direction, anchor=None):
        factor = 1.25 if direction > 0 else 0.8
        new_px = max(
            theme.CELL_PX_MIN,
            min(theme.CELL_PX_MAX, round(self.view.cell_px * factor)),
        )
        if new_px != self.view.cell_px:
            self._apply_zoom(new_px, anchor)

    def _zoom_reset(self):
        if self.view.cell_px != theme.CELL_PX:
            self._apply_zoom(theme.CELL_PX, None)

    def _apply_zoom(self, cell_px, anchor):
        # keep the canvas point under the anchor (mouse or viewport center) fixed
        hbar = self.scroll.horizontalScrollBar()
        vbar = self.scroll.verticalScrollBar()
        if anchor is None:
            anchor = QPoint(
                hbar.value() + self.scroll.viewport().width() // 2,
                vbar.value() + self.scroll.viewport().height() // 2,
            )
        cell_x = anchor.x() / self.view.cell_px
        cell_y = anchor.y() / self.view.cell_px
        viewport_x = anchor.x() - hbar.value()
        viewport_y = anchor.y() - vbar.value()
        self.view.set_cell_px(cell_px)
        hbar.setValue(round(cell_x * self.view.cell_px - viewport_x))
        vbar.setValue(round(cell_y * self.view.cell_px - viewport_y))

    # ---------------- editing callbacks ----------------

    def undo(self):
        if not self.view.undo():
            self.statusBar().showMessage("Nothing to undo", 2000)

    def redo(self):
        if not self.view.redo():
            self.statusBar().showMessage("Nothing to redo", 2000)

    def _sync_controls(self):
        """Mirror the canvas state (possibly changed by undo) in all controls."""
        canvas = self.view.canvas
        if self.active_slot >= canvas.color_count:
            self.active_slot = 1
            self.view.active_slot = 1
        for spin, value in (
            (self.width_spin, canvas.width),
            (self.height_spin, canvas.height),
        ):
            spin.blockSignals(True)
            spin.setValue(value)
            spin.blockSignals(False)
        self.format_combo.blockSignals(True)
        self.format_combo.setCurrentIndex(self.format_combo.findData(canvas.format))
        self.format_combo.blockSignals(False)
        self.slot_bar.set_state(
            [
                QColor(*SYSTEM_PALETTE[index])
                for index in canvas.palette[: canvas.color_count]
            ],
            self.active_slot,
            checker_bg=self.checker_check.isChecked(),
        )
        self.system_palette.set_highlight(canvas.palette[self.active_slot])
        self.status_label.setText(
            f"{canvas.width}×{canvas.height}  {FORMAT_NAMES[canvas.format]}"
            f"  payload {storage.payload_size(canvas)} B"
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

    def save_clicked(self):
        name = self.current_filename()
        if name is None:
            self.statusBar().showMessage("Enter a file name first", 3000)
            self.filename_combo.setFocus()
            return
        if not confirm(self, f'Save "{name}"?'):
            return
        os.makedirs(self.output_dir, exist_ok=True)
        bin_path, c_path = self._output_paths()
        canvas = self.view.canvas
        storage.save_bin(canvas, bin_path)
        storage.save_c_file(canvas, c_path, name)
        self.refresh_filename_list(select=name)
        self.statusBar().showMessage(f"Saved {bin_path} and {c_path}", 5000)

    def load_clicked(self):
        name = self.current_filename()
        if name is None:
            self.statusBar().showMessage("Pick a file to load first", 3000)
            return
        if not confirm(self, f'Load "{name}"?'):
            return
        self.load()

    def load(self):
        bin_path, _ = self._output_paths()
        if not os.path.exists(bin_path):
            self.statusBar().showMessage(f"File {bin_path} not found", 3000)
            return
        try:
            self.view.set_canvas(storage.load_bin(bin_path))
            self.statusBar().showMessage(f"Loaded {bin_path}", 5000)
        except Exception as e:
            self.statusBar().showMessage(f"Failed to load: {e}", 5000)

    def clear_clicked(self):
        if not confirm(self, "Clear the whole canvas?"):
            return
        if not self.view.apply_clear():
            self.statusBar().showMessage("Canvas is already empty", 2000)
