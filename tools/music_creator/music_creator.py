# This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
"""CLI entry point for the buzzer music creator.

See README.md for usage and musiccreator/ for the implementation.
"""

import argparse
import sys

from PyQt6.QtWidgets import QApplication

from musiccreator import audio
from musiccreator.audio import Player
from musiccreator.constants import (
    DEFAULT_FILENAME,
    DEFAULT_NOTES_YAML,
    DEFAULT_OUTPUT_DIR,
    DEFAULT_VOLUME_PERCENT,
)
from musiccreator.gui.main_window import MusicCreatorWindow
from musiccreator.notes import load_note_table


def parse_args():
    parser = argparse.ArgumentParser(description="Buzzer music creator GUI")
    parser.add_argument(
        "-o",
        "--output-dir",
        default=DEFAULT_OUTPUT_DIR,
        help=f"Directory for .bin/.c output files (default: {DEFAULT_OUTPUT_DIR})",
    )
    parser.add_argument(
        "-f",
        "--file",
        default=DEFAULT_FILENAME,
        help=f"Initial file name without extension (default: {DEFAULT_FILENAME})",
    )
    parser.add_argument(
        "--notes-yaml",
        default=DEFAULT_NOTES_YAML,
        help="Notes YAML file (see README.md for the format)",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    try:
        note_table = load_note_table(args.notes_yaml)
    except FileNotFoundError as e:
        print(e, file=sys.stderr)
        sys.exit(1)
    print(f"Loaded {len(note_table.all)} notes from {args.notes_yaml}")

    audio.init()
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    player = Player(volume=DEFAULT_VOLUME_PERCENT / 100.0)
    window = MusicCreatorWindow(note_table, player, args.output_dir, args.file)
    window.show()
    exit_code = app.exec()
    audio.shutdown()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
