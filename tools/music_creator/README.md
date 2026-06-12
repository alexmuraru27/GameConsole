# Buzzer Music Creator


A piano-roll editor for composing buzzer music. It produces note sequences
as interleaved little-endian `uint16` pairs, 4 bytes per note:

```
frequency_hz, duration_ms, frequency_hz, duration_ms, ...
```

A frequency of `0` is a pause. The same format is written both as a raw
`.bin` file and as a `uint16_t` array in a `.c` file (see Output Formats).

![Music Creator GUI](music_creator_gui.png)

## Features

- **Piano-roll grid**: pitch on the Y axis (piano keys on the left), time on
  the X axis. Like classic 8-bit trackers.
- **Implicit pauses**: empty grid space between notes automatically becomes
  `PAUSE` (frequency 0) entries in the exported sequence.
- **Authentic playback**: square-wave synthesis approximating the buzzer,
  with playhead, pause/resume, and volume control.
- **Keyboard-first entry**: place notes without touching the mouse.
- **Dual output**: a `.bin` (loadable save state + runtime asset) and a
  human-readable `.c` companion.

## Requirements

- Python 3 with **PyQt6** (`apt install python3-pyqt6`), **pygame**,
  **numpy**, and **PyYAML**.

## Usage

```bash
python3 music_creator.py                      # output to ./Assets/Music, name "music"
python3 music_creator.py -o <dir> -f <name>   # custom output dir / start file
python3 music_creator.py --notes-yaml <file>  # alternative note set
```

There is also a **Music Creator** launch configuration in `.vscode/launch.json`.

An example track ships with the tool — open it with
`python3 music_creator.py -o . -f example` and press Space.

### Mouse

| Action                    | Effect                                                     |
| ------------------------- | ---------------------------------------------------------- |
| Left click empty cell     | Place a note at that pitch/time (previews it)              |
| Left click + drag right   | Stretch the note being placed                              |
| Left click a note         | Select it (orange) and preview it; ms box edits its length |
| Right click a note        | Delete it (click without dragging)                         |
| Right click + drag        | Pan the view in any direction                              |
| Click a piano key (left)  | Preview that pitch                                         |
| Click/drag the time ruler | Set the play marker (where playback starts)                |
| Mouse wheel / Shift+wheel | Scroll vertically / horizontally                           |
| Ctrl + wheel              | Zoom the timeline (anchored at the mouse)                  |

### Keyboard

| Key(s)                         | Effect                                                                                                |
| ------------------------------ | ----------------------------------------------------------------------------------------------------- |
| `1`–`9` `0` `-` `+`            | Place C, CS, D, DS, E, F, FS, G, GS, A, AS, B at cursor                                               |
| `Up` / `Down`                  | Transpose the selected note one pitch; with no selection, select octave (green bar on the piano keys) |
| `Left` / `Right`               | Move the cursor (blue column) one grid step                                                           |
| `P`                            | Advance the cursor, leaving a pause                                                                   |
| `Delete`                       | Delete the selected note                                                                              |
| `Escape`                       | Deselect                                                                                              |
| `Space`                        | Play / pause / resume (playback starts at the play marker)                                            |
| `Ctrl+Z` / `Ctrl+Y`            | Undo / redo the last edit                                                                             |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+0` | Zoom the timeline in / out / reset                                                                    |
| `Ctrl+S` / `Ctrl+L`            | Save / load                                                                                           |

Notes placed by keyboard use the current **ms** length and advance the
cursor by their duration, so melodies can be typed in sequence.

### Controls

- **Preset buttons / ms box**: length for new notes. With a note selected,
  the ms box edits that note directly; press Enter in the box to return
  focus to keyboard note entry.
- **Grid box**: time per grid cell (snapping resolution, 1–2000 ms; at 1 ms
  placement is effectively unsnapped — the buzzer's native resolution).
  Changing it only rescales the view; note timings are kept in milliseconds.
- **Zoom −/+ buttons**: horizontal zoom of the timeline (4–256 px per grid
  cell; purely visual, no effect on the data or snapping). For even more
  magnification, lower the Grid step — zoom is pixels *per grid cell*.
- **Off-grid frequencies**: entries whose frequency is not a named pitch
  (e.g. sound effects with jittered pitches) are drawn in teal on the
  nearest row. They can be selected, deleted and re-timed without losing
  their exact frequency; transposing snaps them to the row's named pitch.
- **Time ruler**: the strip above the grid. Click (or drag) to place the
  purple play marker — Play and Space start from there, so a section can be
  replayed while editing it. The marker defaults to 0 (track start) and the
  Stop button rewinds it there.
- **File dropdown**: `<New Name>` to type a fresh name, or pick an existing
  `.bin` from the output directory. Save refreshes the list.

## Output Formats

### `<name>.bin`

A `MusicTrackHeader` followed by the note data, all little-endian. The
structs are declared in [`music_track.h`](music_track.h) so C projects can
import the format directly:

```c
typedef struct __attribute__((packed))
{
    uint16_t frequency_hz; // 0 = pause
    uint16_t duration_ms;
} NoteEntry;

typedef struct __attribute__((packed))
{
    uint32_t magic;      // "NOT1"  (NOTE_ASSET_MAGIC = 0x31544F4E)
    uint32_t version;    // NOTE_ASSET_VERSION
    uint32_t noteCount;  // number of NoteEntry entries
    uint32_t dataSize;   // noteCount * sizeof(NoteEntry)
} MusicTrackHeader;

typedef struct __attribute__((packed))
{
    MusicTrackHeader header;
    NoteEntry entries[]; // header.noteCount entries
} MusicTrack;
```

```
offset  0: MusicTrackHeader            (16 bytes)
offset 16: NoteEntry[noteCount]       (4 bytes each)
```

A loaded file maps directly onto `MusicTrack`:

```c
const MusicTrack *track = (const MusicTrack *)buffer;
if (track->header.magic == NOTE_ASSET_MAGIC &&
    track->header.version == NOTE_ASSET_VERSION)
{
    play((const uint16_t *)track->entries, track->header.noteCount);
}
```

Loading still accepts legacy header-less files (raw pairs only).

### `<name>.c`

A human-readable companion to the `.bin` (the `.bin` is the file meant for
actual use):

```c
// Generated by music_creator.py - do not edit manually
#include "music_track.h"

#define MY_TUNE_NOTES_NUMBER 3U

const MusicTrackHeader my_tune_music_header = {
    .magic = NOTE_ASSET_MAGIC,
    .version = NOTE_ASSET_VERSION,
    .noteCount = MY_TUNE_NOTES_NUMBER,
    .dataSize = MY_TUNE_NOTES_NUMBER * sizeof(NoteEntry),
};

const uint16_t my_tune_music[MY_TUNE_NOTES_NUMBER * 2U] = {
    440U, 250U, // A4
    0U, 250U, // PAUSE
    523U, 500U, // C5
};
```

### `notes.yaml`

The available pitches, as `name: frequency_hz` pairs under a `notes:` key:

```yaml
notes:
  PAUSE: 0
  C4: 261
  CS4: 277
  A4: 440
  # ...
```

Rules:

- Names matching `<pitch><octave>` (`C4`, `CS4`, ... — `S` marks a sharp)
  form the piano roll rows and the octave list for keyboard entry.
- An entry with frequency `0` (conventionally `PAUSE`) never appears on the
  grid — silence is implicit — but is used to label pause entries in the
  generated `.c` files.
- Any other names are ignored by the grid but still label matching
  frequencies in the output.

The file shipped in this directory mirrors the firmware's note table; keep
the frequencies in sync with the values the console expects if you edit it.
If the file is missing the tool exits with an error — create it by hand
following the format above.

## Architecture

The code is split into a Qt-free core (testable without a display) and a
thin GUI layer:

```mermaid
graph TD
    EP[music_creator.py<br/>CLI entry point] --> NT[notes.py<br/>NoteTable, notes.yaml]
    EP --> AU[audio.py<br/>synth + Player]
    EP --> MW[gui/main_window.py<br/>MusicCreatorWindow]
    MW --> PR[gui/piano_roll.py<br/>PianoRoll + PianoGutter]
    MW --> ST[storage.py<br/>.bin / .c files]
    MW --> AU
    PR --> TL[timeline.py<br/>Timeline, TimelineNote]
    PR --> HI[history.py<br/>UndoStack]
    PR --> TH[gui/theme.py<br/>colors + geometry]
    TL --> CO[constants.py]
    NT --> CO

    classDef qt fill:#d6e4ff,stroke:#3b6fc9
    classDef core fill:#dcf2dc,stroke:#3f8f4f
    class MW,PR,TH qt
    class TL,ST,AU,NT,CO,HI core
```

### Module responsibilities

| Module               | Responsibility                                                                                                           | Depends on Qt |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------ | ------------- |
| `constants.py`       | Shared defaults, limits, repo paths                                                                                      | no            |
| `notes.py`           | Read `notes.yaml`, `NoteTable` lookups (name ↔ frequency, octaves)                                                       | no            |
| `timeline.py`        | `Timeline`/`TimelineNote` domain model: monophonic note placement, overlap trimming, conversion to/from buzzer sequences | no            |
| `history.py`         | `UndoStack`: memento-based undo/redo with burst coalescing                                                               | no            |
| `storage.py`         | `.bin` serialization, `.c` code generation, output dir listing                                                           | no            |
| `audio.py`           | Square-wave synthesis, `Player` (play/pause/resume/stop, position clock, volume)                                         | no            |
| `gui/theme.py`       | Geometry and color constants                                                                                             | yes           |
| `gui/piano_roll.py`  | `PianoRoll` (grid view/controller over a `Timeline`), `PianoGutter` (fixed key column), `TimeRuler` (play marker bar)    | yes           |
| `gui/confirm.py`     | In-window Yes/No overlay (centered on all platforms, incl. Wayland)                                                      | yes           |
| `gui/main_window.py` | Window layout, keyboard handling, transport, file UI                                                                     | yes           |
| `music_creator.py`   | Argument parsing, wiring, app lifecycle                                                                                  | yes           |

### Data model

The editor works on a **timeline** (notes with absolute start times in ms);
the firmware consumes a **sequence** (flat `(frequency, duration)` pairs
where silence is explicit). `timeline.py` converts between them losslessly:

```mermaid
flowchart LR
    subgraph Timeline [Timeline - editor model]
        N1["A4: start 0, 250ms"]
        N2["C5: start 500, 250ms"]
    end
    subgraph Sequence [Sequence - buzzer format]
        S1["(440, 250)"] --> S2["(0, 250) pause"] --> S3["(523, 250)"]
    end
    Timeline -- "to_sequence()<br/>gaps become pauses" --> Sequence
    Sequence -- "replace_from_sequence()<br/>pauses become gaps" --> Timeline
```

Invariants:

- The timeline is **monophonic** (one buzzer track): placing or resizing a
  note trims/overwrites anything it overlaps (`Timeline.clear_range`).
- All durations are clamped to `uint16` (65535 ms); longer gaps are split
  into multiple pause entries on export.
- Grid snapping is a **view** concern (`PianoRoll.snap`); the model keeps
  exact millisecond values, so changing the grid step never alters data.

### Event flow

```mermaid
sequenceDiagram
    participant U as User
    participant PR as PianoRoll (Qt)
    participant TL as Timeline (core)
    participant MW as MainWindow (Qt)
    participant PL as Player (core)

    U->>PR: left click on empty cell
    PR->>TL: place(start, dur, freq)
    TL-->>PR: TimelineNote (overlaps trimmed)
    PR-->>MW: selection_changed / preview_requested / content_changed
    MW->>PL: preview(freq)
    MW->>MW: sync ms box, status line

    U->>MW: Space
    MW->>TL: to_sequence()
    MW->>PL: play(sequence)
    loop every 30 ms
        MW->>PL: elapsed_ms()
        MW->>PR: set_playhead(ms) + auto-scroll
    end
```

### Design decisions

- **Qt-free core**: `timeline`, `storage`, `audio`, and `notes` import no Qt
  and can be unit-tested headlessly (`QT_QPA_PLATFORM=offscreen` is only
  needed for widget tests).
- **pygame for audio, Qt for UI**: pygame.mixer runs fine without a pygame
  window and avoids a QtMultimedia dependency.
- **`.bin` as save state**: one format for persistence and for the console's
  SD-card asset keeps load/save trivially round-trippable.
- **Identity-based selection**: the roll tracks the selected `TimelineNote`
  object, not an index, so trims/sorts can't invalidate the selection
  silently.
- **Wall-clock playhead**: pygame exposes no sample-accurate position, so
  `Player` tracks `time.monotonic()` and compensates for time spent paused.

## Development

Quick headless smoke test:

```bash
cd tools/music_creator
QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy python3 -c "
from musiccreator.timeline import Timeline
t = Timeline()
t.place(0, 250, 440); t.place(500, 250, 523)
assert t.to_sequence() == [(440, 250), (0, 250), (523, 250)]
t2 = Timeline(); t2.replace_from_sequence(t.to_sequence())
assert t2.to_sequence() == t.to_sequence()
print('OK')"
```
