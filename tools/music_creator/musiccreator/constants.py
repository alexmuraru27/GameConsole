# This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
"""Shared constants and default paths."""

import os

PACKAGE_DIR = os.path.dirname(os.path.abspath(__file__))
TOOL_DIR = os.path.dirname(PACKAGE_DIR)

DEFAULT_NOTES_YAML = os.path.join(TOOL_DIR, "notes.yaml")
DEFAULT_OUTPUT_DIR = "Assets/Music"
DEFAULT_FILENAME = "music"

# the buzzer sequence format stores both fields as uint16
UINT16_MAX = 65535

# .bin file magic ("NOT1") and format version,
# see music_track.h for the C-side declarations
NOTE_ASSET_MAGIC = b"NOT1"
NOTE_ASSET_VERSION = 1

DURATION_PRESETS_MS = [125, 250, 500, 1000]
DEFAULT_DURATION_MS = 250
DEFAULT_STEP_MS = 125
DEFAULT_VOLUME_PERCENT = 30
