# This script is 100% vibecoded, don't trust what you see here, its not meant to be maintainable, it was meant to be quick
"""Buzzer music creator for the GameConsole.

A piano-roll editor producing (frequency_hz, duration_ms) uint16 pair
sequences for the console's passive buzzer.

Package layout:
  constants   - shared defaults and limits
  notes       - notes.yaml handling and NoteTable lookups
  timeline    - Qt-free domain model (notes on a millisecond time axis)
  storage     - .bin / .c file reading and writing
  audio       - square-wave synthesis and pygame.mixer playback
  gui         - PyQt6 widgets (piano roll, key gutter, main window)
"""

__version__ = "1.0.0"
