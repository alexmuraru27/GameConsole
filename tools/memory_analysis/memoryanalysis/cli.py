"""
Command-line interface for the memory analysis tool.

Parses arguments, discovers default build artifacts, delegates to the
parser and report modules, and prints the result.
"""

import argparse
import os
import sys
from typing import Dict, List

from .ld_parser import parse_ld_file
from .map_parser import parse_map_file
from .model import AppMemory, MemoryRegion
from .report import format_report, to_json
from .utils import resolve_path


def _find_project_root() -> str:
    """Walk up from this file's directory to find the project root.

    Anchored on ``common.mk`` (a stable repo-root marker) rather than a linker
    script, since the linker scripts live under ``linker/`` and not at the root.
    """
    d = os.path.dirname(os.path.abspath(__file__))
    for _ in range(6):
        if os.path.isfile(os.path.join(d, 'common.mk')):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    return os.getcwd()


def _app_name_from_map(map_path: str) -> str:
    """Derive a human-readable application name from a map file path."""
    basename = os.path.splitext(os.path.basename(map_path))[0]
    known = {
        'GameConsole': 'Console',
        'GameXO': 'GameXO',
        'console': 'Console',
        'game': 'GameXO',
    }
    return known.get(basename, basename)


def _default_map_paths(project_root: str) -> List[str]:
    """Discover every ``.map`` file under the build directory."""
    import glob as _glob
    return sorted(_glob.glob(os.path.join(project_root, 'build', '**', '*.map'), recursive=True))


def _default_ld_paths(project_root: str) -> List[str]:
    """Discover linker scripts under the project root (excluding common.ld)."""
    import glob as _glob
    return sorted(
        p for p in _glob.glob(os.path.join(project_root, '**', '*.ld'), recursive=True)
        if os.path.basename(p) != 'common.ld'
    )


def main(argv: List[str] = None):
    """Entry point — parse args, gather data, print report."""
    project_root = _find_project_root()

    parser = argparse.ArgumentParser(
        description='Analyze RAM/ROM/CCM usage for GameConsole applications.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s
  %(prog)s --map build/Console/GameConsole.map
  %(prog)s --map build/Console/GameConsole.map build/GameXO/GameXO.map
  %(prog)s --ld linker/app.ld --name "GameXO (theoretical)"
  %(prog)s --map build/GameXO/GameXO.map --json
  %(prog)s --quiet
        ''',
    )
    parser.add_argument(
        '--map', nargs='+', default=None,
        help='One or more linker .map files to analyze.  Each map becomes a '
             'separate application in the report.',
    )
    parser.add_argument(
        '--ld', nargs='+', default=None,
        help='One or more linker scripts (.ld) to parse for region capacities.  '
             'All scripts are parsed together (INCLUDE directives are followed) '
             'and reported as a single application.  Use when no .map file is '
             'available yet (e.g. before building).',
    )
    parser.add_argument(
        '--name', type=str, default='Linker Script',
        help='Application name for the --ld output.',
    )
    parser.add_argument(
        '--json', action='store_true',
        help='Output as JSON instead of a formatted terminal report.',
    )
    parser.add_argument(
        '--quiet', '-q', action='store_true',
        help='Show only the summary table (suppress per-section detail).',
    )
    parser.add_argument(
        '--no-color', action='store_true',
        help='Disable terminal colour codes.',
    )

    args = parser.parse_args(argv)
    apps: List[AppMemory] = []

    if args.map:
        # Explicit map file(s) — each becomes a separate application
        for map_path in args.map:
            resolved = resolve_path(map_path)
            app = parse_map_file(resolved)
            if app:
                app.name = _app_name_from_map(resolved)
                apps.append(app)
            else:
                print(f"Warning: cannot read map file: {map_path}", file=sys.stderr)

    elif args.ld:
        # LD-only mode: parse linker scripts for theoretical region capacities
        regions: Dict[str, MemoryRegion] = {}
        for ld_path in args.ld:
            resolved = resolve_path(ld_path)
            parsed = parse_ld_file(resolved)
            if parsed:
                regions.update(parsed)
        if regions:
            apps.append(AppMemory(name=args.name, regions=regions))
        else:
            print(f"Error: no MEMORY regions found in: {args.ld}", file=sys.stderr)
            sys.exit(1)

    else:
        # Default: auto-discover Console and GameXO map files
        default_maps = _default_map_paths(project_root)
        for map_path in default_maps:
            app = parse_map_file(map_path)
            if app:
                app.name = _app_name_from_map(map_path)
                apps.append(app)

        # Fallback: if no maps found, try linker scripts
        if not apps:
            for ld_path in _default_ld_paths(project_root):
                regions = parse_ld_file(ld_path)
                if regions:
                    name = _app_name_from_map(ld_path)
                    apps.append(AppMemory(name=name, regions=regions))

    if not apps:
        print(
            'Error: no map files found and no linker scripts provided.\n'
            'Run "make all" first to generate map files, or specify paths:\n'
            f'  {sys.argv[0]} --map build/Console/GameConsole.map\n'
            f'  {sys.argv[0]} --ld linker/console.ld',
            file=sys.stderr,
        )
        sys.exit(1)

    # Output
    if args.json:
        print(to_json(apps))
    elif args.quiet:
        # One-line-per-app summary
        from .utils import fmt_bytes
        for app in apps:
            bin_str = f"  .bin: {fmt_bytes(app.bin_size)}" if app.bin_size else ""
            print(f"{app.name}:{bin_str}")
            for rname in app.regions_with_usage():
                used = app.region_used(rname)
                cap = app.region_capacity(rname)
                pct = f"{(used / cap * 100):.1f}%" if cap > 0 else '--'
                print(f"  {rname:<20s}  {fmt_bytes(used):>8s} / {fmt_bytes(cap):>8s}  ({pct})")
        print()
    else:
        print(format_report(apps, use_color=not args.no_color))
