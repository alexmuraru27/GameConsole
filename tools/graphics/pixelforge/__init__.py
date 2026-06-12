"""Pixel Forge - pixel graphics editor for the GameConsole.

The package is split into a Qt-free core and a thin GUI layer:

- ``canvas``    Canvas domain model (pixels, palette, format, size)
- ``history``   memento-based UndoStack
- ``storage``   .bin / .c serialization (format declared in gfx_asset.h)
- ``palette``   the console's 64-color system palette
- ``gui/``      PyQt6 widgets and the main window

The core modules import no Qt and can be tested headlessly.
"""
