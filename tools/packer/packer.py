#!/usr/bin/env python3
"""Command-line asset packer for the GameConsole ``.pak`` format.

Reads a YAML manifest of assets (id, name, path), writes ``<name>.pak`` and a
``<name>AssetEnum.h`` C header, and prints a summary. The pak is self-verified
before anything is written to disk.

The format and all logic live in the ``pak`` package; this file is just the CLI.
See README.md for the manifest format, binary layout, and CRC details.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from pak import (
    PakError,
    build_pak,
    format_summary,
    load_manifest,
    render_enum_header,
    verify_pak,
)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="packer.py",
        description="Pack binary assets listed in a YAML manifest into a .pak container.",
    )
    parser.add_argument("manifest", type=Path, help="path to the YAML manifest")
    parser.add_argument(
        "-o",
        "--output-name",
        required=True,
        help="base name for outputs: writes <name>.pak and <name>AssetEnum.h",
    )
    parser.add_argument(
        "-d",
        "--output-dir",
        type=Path,
        default=Path("."),
        help="directory for the generated .pak and header (default: current dir)",
    )
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=None,
        help="base directory for relative asset paths (default: the manifest's directory)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        manifest = load_manifest(args.manifest)
        assets_root = args.assets_root or args.manifest.resolve().parent
        pak_bytes = build_pak(manifest, assets_root)

        # Self-check the bytes before writing so a corrupt pak never lands on disk.
        info = verify_pak(pak_bytes)

        args.output_dir.mkdir(parents=True, exist_ok=True)
        pak_path = args.output_dir / f"{args.output_name}.pak"
        header_path = args.output_dir / f"{args.output_name}AssetEnum.h"
        pak_path.write_bytes(pak_bytes)
        header_path.write_text(render_enum_header(manifest, args.output_name))
    except PakError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(format_summary(pak_path, header_path, manifest, info))
    return 0


if __name__ == "__main__":
    sys.exit(main())
