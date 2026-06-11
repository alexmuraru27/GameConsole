# This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
"""Note definitions: notes.yaml handling and NoteTable lookups.

The available pitches come from notes.yaml (name -> frequency_hz mapping);
see README.md for the file format.
"""

import os
import re

import yaml

# the 12 semitones of an octave, in keyboard-entry order
SEMITONE_ORDER = ["C", "CS", "D", "DS", "E", "F", "FS", "G", "GS", "A", "AS", "B"]

_PITCH_NAME_RE = re.compile(r"^([A-G]S?)(\d+)$")


class NoteTable:
    """Lookup helpers around the (name, frequency_hz) pairs from notes.yaml."""

    def __init__(self, notes):
        self.all = sorted(notes, key=lambda nf: nf[1])
        self.name_to_freq = dict(self.all)
        self.freq_to_name = {freq: name for name, freq in self.all}
        # pitched notes only (frequency > 0), highest first: piano roll row order
        self.pitched_desc = [
            (name, freq) for name, freq in reversed(self.all) if freq > 0
        ]
        self.octaves = sorted(
            {
                int(match.group(2))
                for name, _freq in self.all
                if (match := _PITCH_NAME_RE.match(name))
            }
        )

    def name_of(self, frequency_hz):
        return self.freq_to_name.get(frequency_hz, str(frequency_hz))

    def frequency_of(self, pitch, octave):
        """Frequency for a pitch like "CS" in an octave, or None if unknown."""
        return self.name_to_freq.get(f"{pitch}{octave}")


def load_note_table(yaml_path):
    """Load the notes YAML; see README.md for the expected format."""
    if not os.path.exists(yaml_path):
        raise FileNotFoundError(
            f"{yaml_path} not found - create it as described in README.md"
        )
    with open(yaml_path, "r") as f:
        data = yaml.safe_load(f)
    return NoteTable(list(data["notes"].items()))
