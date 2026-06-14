"""GameConsole ``.pak`` asset-container library.

The on-disk format is defined in :mod:`pak.format` (mirroring ``asset_format.h``).
The pipeline is split by concern:

    manifest -> builder -> verify -> report
                        \\-> codegen (the C asset-id enum header)

See ``packer.py`` for the command-line front end and ``README.md`` for the
format and usage.
"""

from .builder import build_pak
from .codegen import render_enum_header
from .errors import PakError, PakVerificationError
from .format import (
    ENTRY_SIZE,
    HEADER_SIZE,
    PAK_MAGIC,
    PAK_VERSION,
    PakInfo,
    ParsedEntry,
)
from .manifest import AssetSpec, Manifest, load_manifest
from .report import format_summary
from .verify import verify_pak

__all__ = [
    "AssetSpec",
    "Manifest",
    "PakError",
    "PakVerificationError",
    "PakInfo",
    "ParsedEntry",
    "PAK_MAGIC",
    "PAK_VERSION",
    "HEADER_SIZE",
    "ENTRY_SIZE",
    "load_manifest",
    "build_pak",
    "verify_pak",
    "render_enum_header",
    "format_summary",
]
