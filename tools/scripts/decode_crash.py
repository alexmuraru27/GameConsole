#!/usr/bin/env python3
"""Decode a GameConsole crash back to source lines — the offline half of D1.

A game fault is reported two ways with no debug probe: the on-screen crash banner
("<game> crashed / PC 0x2001A3F4 DACCVIOL") and an appended line in Crashes/crash.log
on the SD card. Game binaries are linked at their final RAM addresses (linker/app.ld),
so those PC/LR values map straight onto the game's .elf with no rebasing — this script
just wraps arm-none-eabi-addr2line to turn them into file:line.

Usage:
    decode_crash.py <game.elf> <PC> [LR ...]         # bare addresses (0x… or hex)
    decode_crash.py <game.elf> --line "<crash.log line>"
    decode_crash.py <game.elf> < crash.log           # pipe a log line on stdin

Examples:
    decode_crash.py build/GameXO/GameXO.elf 0x2001A3F4 0x2001A2C0
    decode_crash.py build/GameXO/GameXO.elf --line \\
        "[12345] GameXO.bin MEMMANAGE PC=0x2001A3F4 LR=0x2001A2C0 ... flags: DACCVIOL"
    grep GameXO Crashes/crash.log | tail -1 | decode_crash.py build/GameXO/GameXO.elf

The ARM toolchain is found on PATH, or via $GCC_PATH / $ADDR2LINE (same knobs the
Makefiles use). Standard library only.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

TOOL = "arm-none-eabi-addr2line"

# GAME_RAM window (linker/common.ld): a game's code/rodata/data all link here.
GAME_RAM_START = 0x20018000
GAME_RAM_END = 0x20020000

# "PC=0x…" / "LR=0x…" as emitted in the crash.log line and banner.
LABELLED = re.compile(r"\b(PC|LR)\s*=?\s*(0x[0-9a-fA-F]+)", re.IGNORECASE)


def find_addr2line():
    """Locate arm-none-eabi-addr2line via $ADDR2LINE, $GCC_PATH, then PATH."""
    override = os.environ.get("ADDR2LINE")
    if override:
        return override
    gcc_path = os.environ.get("GCC_PATH")
    if gcc_path:
        candidate = os.path.join(gcc_path, TOOL)
        if os.path.isfile(candidate):
            return candidate
    return shutil.which(TOOL)


def parse_targets(text):
    """Extract [(label, address)] from free-form text.

    A crash.log line (or banner) carries labelled PC=/LR= — those win. Otherwise the
    text is a list of bare addresses: label them PC, LR, then A3, A4, … in order.
    """
    labelled = [(m.group(1).upper(), int(m.group(2), 16)) for m in LABELLED.finditer(text)]
    if labelled:
        return labelled

    addrs = []
    for token in text.replace(",", " ").split():
        try:
            addrs.append(int(token, 16))
        except ValueError:
            pass  # skip non-hex tokens (a stray word, the game name, …)
    labels = ["PC", "LR"] + [f"A{i}" for i in range(3, len(addrs) + 1)]
    return list(zip(labels, addrs))


def decode(tool, elf, addr):
    """addr2line one address; return its (possibly multi-line, inlined) text."""
    result = subprocess.run(
        [tool, "-e", elf, "-f", "-C", "-i", "-p", hex(addr)],
        capture_output=True,
        text=True,
    )
    out = result.stdout.strip()
    return out if out else "(addr2line produced no output)"


def main():
    parser = argparse.ArgumentParser(
        description="Decode a GameConsole crash PC/LR to source lines via addr2line.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("elf", help="the crashed game's .elf (e.g. build/GameXO/GameXO.elf)")
    parser.add_argument("addr", nargs="*", help="PC LR … as 0x-hex, or a whole crash.log line")
    parser.add_argument("--line", help="a raw Crashes/crash.log line to parse PC=/LR= out of")
    args = parser.parse_args()

    if not os.path.isfile(args.elf):
        parser.error(f"no such ELF: {args.elf}")

    if args.line:
        text = args.line
    elif args.addr:
        text = " ".join(args.addr)
    elif not sys.stdin.isatty():
        text = sys.stdin.read()
    else:
        parser.error("no addresses given — pass PC/LR, --line, or pipe a crash.log line")

    targets = parse_targets(text)
    if not targets:
        parser.error("found no addresses to decode in the input")

    tool = find_addr2line()
    if tool is None:
        sys.exit(
            f"{TOOL} not found. Put the ARM toolchain on PATH, or set $GCC_PATH / $ADDR2LINE."
        )

    print(f"ELF: {args.elf}")
    for label, addr in targets:
        note = "" if GAME_RAM_START <= addr < GAME_RAM_END else "  (outside GAME_RAM — wrong ELF?)"
        print(f"\n{label} {hex(addr)}{note}")
        for frame in decode(tool, args.elf, addr).splitlines():
            print(f"    {frame}")


if __name__ == "__main__":
    main()
