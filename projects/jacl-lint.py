#!/usr/bin/env python3
"""Align property lines within JACL location/object/player blocks.

Inside a block like:

    location surface : surface
     short       the "surface"
     has         ON_WATER KNOWN
     down        beneath_outpost

this tool rewrites each property line so the value column lines up at
a fixed column (default 16). Padding is plain spaces -- tab-agnostic,
so lines look aligned in every editor regardless of tab-width setting.

Lines that are blank, begin a code block ({+func, {look, etc.), or are
comments are passed through unchanged. Lines outside of a block (e.g.
top-level constants, function bodies) are also passed through.

Usage:
    ./jacl-lint.py path/to/game.jacl [more.jacl ...]
    ./jacl-lint.py --check path/to/game.jacl    # exit 1 if changes needed
    ./jacl-lint.py --column 20 game.jacl        # custom alignment column
    ./jacl-lint.py projects/                    # recurse a directory
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

BLOCK_START = re.compile(r"^(location|object|player)\b")

DEFAULT_COLUMN = 16


def lint_line(line: str, column: int) -> str:
    """Reformat a property line so the value starts at column `column`.

    Uses plain spaces for padding (no tabs) so alignment is independent
    of editor tab-width. Passes through blank/comment lines unchanged.
    """
    stripped = line.lstrip(" \t")
    if not stripped or stripped.startswith("#"):
        return line
    parts = stripped.split(None, 1)
    key = parts[0]
    if len(parts) == 1:
        return f" {key}\n"
    value = parts[1].rstrip()
    # Column is 1-indexed: leading space (col 1) + key + padding + value.
    # Padding count = column - 1 - 1 - len(key); at least one space so
    # very long keys don't glue to the value.
    padding = max(1, column - 2 - len(key))
    return f" {key}{' ' * padding}{value}\n"


def lint_text(text: str, column: int) -> str:
    out = []
    in_block = False
    for line in text.splitlines(keepends=True):
        if BLOCK_START.match(line):
            in_block = True
            out.append(line)
            continue
        if in_block:
            stripped = line.lstrip(" \t")
            if not stripped.strip() or stripped.startswith("{"):
                in_block = False
                out.append(line)
                continue
            out.append(lint_line(line, column))
        else:
            out.append(line)
    return "".join(out)


def iter_jacl_paths(paths):
    for p in paths:
        p = Path(p)
        if p.is_dir():
            yield from sorted(p.rglob("*.jacl"))
        else:
            yield p


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--check",
        action="store_true",
        help="Exit non-zero if any file would be changed; don't write.",
    )
    ap.add_argument(
        "--column",
        type=int,
        default=DEFAULT_COLUMN,
        help=f"1-indexed column for value alignment (default: {DEFAULT_COLUMN}).",
    )
    ap.add_argument("paths", nargs="+", help="JACL files or directories.")
    args = ap.parse_args()

    changed = []
    for path in iter_jacl_paths(args.paths):
        try:
            original = path.read_text()
        except OSError as e:
            print(f"warning: cannot read {path}: {e}", file=sys.stderr)
            continue
        formatted = lint_text(original, args.column)
        if formatted != original:
            if args.check:
                changed.append(path)
                print(f"would reformat {path}")
            else:
                path.write_text(formatted)
                changed.append(path)
                print(f"reformatted {path}")

    if args.check and changed:
        sys.exit(1)
    if not changed:
        print("no changes needed")


if __name__ == "__main__":
    main()
