"""In-window Yes/No confirmation overlay.

A native QMessageBox is a top-level window and Wayland compositors ignore
programmatic placement of top-level windows, so it cannot be reliably
centered over the app. This overlay is a plain child widget instead: it
dims the whole window and renders its panel centered on every platform.

Keys: Enter/Y confirm, Escape/N cancel. All other input is swallowed while
the overlay is up (including the main window's shortcuts).
"""

from PyQt6.QtCore import QEvent, QEventLoop, Qt
from PyQt6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

_PANEL_MIN_WIDTH = 280


def confirm(parent_window, question):
    """Show a modal in-window Yes/No question; returns True on Yes."""
    return _ConfirmOverlay(parent_window, question).run()


class _ConfirmOverlay(QWidget):
    def __init__(self, parent, question):
        super().__init__(parent)
        self._result = False
        self._loop = QEventLoop(self)

        self.setObjectName("confirmOverlay")
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self.setStyleSheet("#confirmOverlay { background: rgba(0, 0, 0, 90); }")

        self._panel = QFrame(self)
        self._panel.setObjectName("confirmPanel")
        self._panel.setStyleSheet(
            "#confirmPanel { background: palette(window);"
            " border: 1px solid palette(dark); border-radius: 6px; }"
        )
        layout = QVBoxLayout(self._panel)
        label = QLabel(question)
        label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(label)
        buttons = QHBoxLayout()
        self._yes_button = QPushButton("Yes")
        self._yes_button.clicked.connect(lambda: self._finish(True))
        no_button = QPushButton("No")
        no_button.clicked.connect(lambda: self._finish(False))
        buttons.addStretch()
        buttons.addWidget(self._yes_button)
        buttons.addWidget(no_button)
        buttons.addStretch()
        layout.addLayout(buttons)

    def run(self):
        self.setGeometry(self.parentWidget().rect())
        self.show()
        self.raise_()
        self._panel.adjustSize()
        self._panel.resize(
            max(self._panel.width(), _PANEL_MIN_WIDTH), self._panel.height()
        )
        self._panel.move(self.rect().center() - self._panel.rect().center())
        self._yes_button.setFocus()
        self.grabKeyboard()
        self._loop.exec()
        self.releaseKeyboard()
        self.hide()
        self.deleteLater()
        return self._result

    def _finish(self, result):
        self._result = result
        self._loop.quit()

    # ---- modality: swallow everything that is not an answer ----

    def keyPressEvent(self, event):
        key = event.key()
        if key in (Qt.Key.Key_Return, Qt.Key.Key_Enter, Qt.Key.Key_Y):
            self._finish(True)
        elif key in (Qt.Key.Key_Escape, Qt.Key.Key_N):
            self._finish(False)

    def event(self, event):
        # keep the main window's QShortcuts (Ctrl+S, ...) inactive
        if event.type() == QEvent.Type.ShortcutOverride:
            event.accept()
            return True
        return super().event(event)

    def mousePressEvent(self, _event):
        pass  # clicks outside the panel must not reach the widgets below
