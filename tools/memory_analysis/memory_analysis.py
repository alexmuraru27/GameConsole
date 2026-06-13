#!/usr/bin/env python3
"""
Memory analysis tool for the GameConsole project.

Parses GNU LD linker map files and linker scripts to report detailed
RAM / ROM / CCM usage with percentages and free-space projections for
each application (Console firmware and game cartridges).

Usage::

    python3 tools/memory_analysis/memory_analysis.py
    python3 tools/memory_analysis/memory_analysis.py --map build/Console/GameConsole.map
    python3 tools/memory_analysis/memory_analysis.py --ld game.ld --name "GameXO"
    python3 tools/memory_analysis/memory_analysis.py --json

See README.md for full documentation.
"""

import sys
import os

# Ensure the parent directory is on the path so `memoryanalysis` is importable.
_tool_dir = os.path.dirname(os.path.abspath(__file__))
if _tool_dir not in sys.path:
    sys.path.insert(0, _tool_dir)

from memoryanalysis.cli import main

if __name__ == '__main__':
    main()
