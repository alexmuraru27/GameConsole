"""CLI entry point for Pixel Forge, the GameConsole graphics creator.

See README.md for usage and pixelforge/ for the implementation.
"""

import argparse
import sys

from PyQt6.QtWidgets import QApplication

from pixelforge.canvas import Canvas
from pixelforge.constants import (
    DEFAULT_FILENAME,
    DEFAULT_HEIGHT,
    DEFAULT_OUTPUT_DIR,
    DEFAULT_WIDTH,
    GFX_FMT_2BPP,
    GFX_FMT_4BPP,
    MAX_SIZE,
    MIN_SIZE,
)
from pixelforge.gui.main_window import PixelForgeWindow


def _size(text):
    value = int(text)
    if not MIN_SIZE <= value <= MAX_SIZE:
        raise argparse.ArgumentTypeError(
            f"size must be between {MIN_SIZE} and {MAX_SIZE}"
        )
    return value


def parse_args():
    parser = argparse.ArgumentParser(description="Pixel Forge graphics creator GUI")
    parser.add_argument(
        "-o",
        "--output-dir",
        default=DEFAULT_OUTPUT_DIR,
        help="Directory for .bin/.c output files and the file list"
        " (default: current directory)",
    )
    parser.add_argument(
        "-f",
        "--file",
        default=DEFAULT_FILENAME,
        help=f"Initial file name without extension (default: {DEFAULT_FILENAME})",
    )
    parser.add_argument(
        "--width",
        type=_size,
        default=DEFAULT_WIDTH,
        help=f"Canvas width in pixels for a new picture (default: {DEFAULT_WIDTH})",
    )
    parser.add_argument(
        "--height",
        type=_size,
        default=DEFAULT_HEIGHT,
        help=f"Canvas height in pixels for a new picture (default: {DEFAULT_HEIGHT})",
    )
    parser.add_argument(
        "--format",
        choices=["2bpp", "4bpp"],
        default="2bpp",
        help="Pixel format for a new picture (default: 2bpp)",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    fmt = GFX_FMT_2BPP if args.format == "2bpp" else GFX_FMT_4BPP
    canvas = Canvas(args.width, args.height, fmt)

    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    window = PixelForgeWindow(canvas, args.output_dir, args.file)
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
