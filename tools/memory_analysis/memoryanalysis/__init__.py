"""
Memory analysis tool for the GameConsole project — core library.

Parses GNU LD linker map files and linker scripts to report detailed
RAM / ROM / CCM usage with percentages and free-space projections for
each application (Console firmware and game cartridges).

The package follows the same layered architecture as the Music Creator
and Pixel Forge tools: a Qt-free core library (model, parsers, report
formatter) with a thin CLI layer on top.

Usage (via the launcher)::

    python3 tools/memory_analysis/memory_analysis.py
    python3 tools/memory_analysis/memory_analysis.py --json

Usage (as a module, from the tool directory)::

    python3 -m memoryanalysis
"""

__version__ = "1.0.0"
