# This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
"""Qt-free domain model: notes placed on a millisecond time axis.

The editor works on a Timeline (notes with absolute start times); the buzzer
firmware consumes a flat sequence of (frequency_hz, duration_ms) pairs where
silence is an explicit PAUSE (frequency 0) entry. Conversions between the two
representations live here: timeline gaps become pauses and vice versa.

The timeline is monophonic, mirroring a single buzzer track: placing or
resizing a note removes whatever it would overlap.
"""

from .constants import UINT16_MAX


class TimelineNote:
    """One note: where it starts, how long it lasts, what pitch it has."""

    __slots__ = ("start_ms", "duration_ms", "frequency_hz")

    def __init__(self, start_ms, duration_ms, frequency_hz):
        self.start_ms = start_ms
        self.duration_ms = duration_ms
        self.frequency_hz = frequency_hz

    @property
    def end_ms(self):
        return self.start_ms + self.duration_ms

    def __repr__(self):
        return (
            f"TimelineNote({self.start_ms}, {self.duration_ms},"
            f" {self.frequency_hz})"
        )


class Timeline:
    """An editable, monophonic collection of TimelineNotes."""

    def __init__(self):
        self.notes = []

    def __len__(self):
        return len(self.notes)

    def __iter__(self):
        return iter(self.notes)

    def end_ms(self):
        return max((note.end_ms for note in self.notes), default=0)

    def note_at(self, ms, frequency_hz):
        """The note of the given pitch sounding at the given time, if any."""
        for note in self.notes:
            if note.frequency_hz == frequency_hz and note.start_ms <= ms < note.end_ms:
                return note
        return None

    def clear_range(self, start_ms, end_ms, keep=None):
        """Free [start_ms, end_ms) of notes, trimming or dropping overlaps."""
        result = []
        for note in self.notes:
            if note is keep or note.end_ms <= start_ms or note.start_ms >= end_ms:
                result.append(note)
            elif note.start_ms < start_ms:
                note.duration_ms = start_ms - note.start_ms  # trim the tail
                result.append(note)
            # else: starts inside the range -> dropped
        self.notes = result

    def place(self, start_ms, duration_ms, frequency_hz):
        """Insert a note, overwriting anything it overlaps. Returns the note."""
        duration_ms = min(duration_ms, UINT16_MAX)
        self.clear_range(start_ms, start_ms + duration_ms)
        note = TimelineNote(start_ms, duration_ms, frequency_hz)
        self.notes.append(note)
        self.notes.sort(key=lambda n: n.start_ms)
        return note

    def resize(self, note, duration_ms):
        """Change a note's duration, overwriting anything newly overlapped."""
        note.duration_ms = min(duration_ms, UINT16_MAX)
        self.clear_range(note.start_ms, note.end_ms, keep=note)

    def remove(self, note):
        self.notes.remove(note)

    def clear(self):
        self.notes = []

    def snapshot(self):
        """Memento of the current content (for undo/redo)."""
        return [
            (note.start_ms, note.duration_ms, note.frequency_hz)
            for note in self.notes
        ]

    def restore(self, snapshot):
        """Replace the content with a snapshot() memento."""
        self.notes = [TimelineNote(*entry) for entry in snapshot]

    def to_sequence(self, start_ms=0):
        """Flatten to buzzer (frequency_hz, duration_ms) pairs; gaps -> pauses.

        With start_ms, the sequence covers only the timeline from that point:
        earlier notes are skipped and a note straddling start_ms is trimmed
        to its remainder. Gaps longer than UINT16_MAX ms are split into
        multiple pause entries.
        """
        sequence = []
        t = start_ms
        for note in sorted(self.notes, key=lambda n: n.start_ms):
            if note.end_ms <= start_ms:
                continue
            note_start = max(note.start_ms, start_ms)
            gap = note_start - t
            while gap > 0:
                chunk = min(gap, UINT16_MAX)
                sequence.append((0, chunk))
                gap -= chunk
            duration = note.end_ms - note_start
            sequence.append((note.frequency_hz, min(duration, UINT16_MAX)))
            t = note.end_ms
        return sequence

    def replace_from_sequence(self, sequence):
        """Rebuild the timeline from buzzer pairs; pauses become gaps."""
        self.notes = []
        t = 0
        for frequency_hz, duration_ms in sequence:
            if frequency_hz != 0:
                self.notes.append(TimelineNote(t, duration_ms, frequency_hz))
            t += duration_ms
