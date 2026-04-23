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
TOP_DECL = re.compile(r"^(constant|integer|integer_array|string|string_array|synonym|attribute|variable|grammar)\b")

DEFAULT_COLUMN = 16
DECL_TRAILING_SPACES = 5


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


def lint_decl_group(lines):
    """Format a run of same-keyword top-level declarations.

    Finds the longest name in the group and pads each name with spaces
    so values (when present) all line up one column past the longest
    name. Padding is plain spaces, never tabs.

    `attribute` lines can carry multiple names on one line, so they are
    just whitespace-normalized (no column alignment).
    """
    parsed = []
    for line in lines:
        stripped = line.lstrip(" \t")
        parts = stripped.split(None, 2)
        parsed.append(parts)

    keyword = parsed[0][0]

    if keyword == "attribute":
        out = []
        for line in lines:
            tokens = line.split()
            out.append(" ".join(tokens) + "\n")
        return out

    max_name = 0
    for parts in parsed:
        if len(parts) >= 2:
            max_name = max(max_name, len(parts[1]))

    out = []
    for parts in parsed:
        if len(parts) == 1:
            out.append(f"{parts[0]}\n")
        elif len(parts) == 2:
            out.append(f"{parts[0]} {parts[1]}\n")
        else:
            pad = " " * (max_name - len(parts[1]) + DECL_TRAILING_SPACES)
            value = " ".join(parts[2].split())
            out.append(f"{parts[0]} {parts[1]}{pad}{value}\n")
    return out


def lint_grammar_group(lines):
    """Align `grammar` lines so their `>target` columns line up.

    The `>target` sits `DECL_TRAILING_SPACES` past the longest pattern
    in the group. Pattern tokens (everything between `grammar` and `>`)
    are whitespace-normalized to single spaces.
    """
    parsed = []
    for line in lines:
        stripped = line.lstrip(" \t")
        idx = stripped.find(">")
        if idx < 0:
            parsed.append((None, stripped.rstrip()))
            continue
        pattern = " ".join(stripped[:idx].split())
        target = stripped[idx:].rstrip()
        parsed.append((pattern, target))

    max_pattern = max((len(p) for p, _ in parsed if p is not None), default=0)

    out = []
    for pattern, target in parsed:
        if pattern is None:
            out.append(target + "\n")
        else:
            pad = " " * (max_pattern - len(pattern) + DECL_TRAILING_SPACES)
            out.append(f"{pattern}{pad}{target}\n")
    return out


def lint_text(text: str, column: int) -> str:
    raw_lines = text.splitlines(keepends=True)
    out = []
    in_block = False
    i = 0
    while i < len(raw_lines):
        line = raw_lines[i]
        if BLOCK_START.match(line):
            in_block = True
            out.append(line)
            i += 1
            continue
        if in_block:
            stripped = line.lstrip(" \t")
            if not stripped.strip() or stripped.startswith("{"):
                in_block = False
                out.append(line)
                i += 1
                continue
            out.append(lint_line(line, column))
            i += 1
            continue
        m = TOP_DECL.match(line)
        if m:
            keyword = m.group(1)
            group = []
            j = i
            while j < len(raw_lines):
                mj = TOP_DECL.match(raw_lines[j])
                if not mj or mj.group(1) != keyword:
                    break
                group.append(raw_lines[j])
                j += 1
            if keyword == "grammar":
                out.extend(lint_grammar_group(group))
            else:
                out.extend(lint_decl_group(group))
            i = j
            continue
        out.append(line)
        i += 1
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
            # surrogateescape lets us round-trip non-UTF-8 bytes (e.g.
            # smart quotes from Windows-1252) without losing data.
            with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
                original = f.read()
        except OSError as e:
            print(f"warning: cannot read {path}: {e}", file=sys.stderr)
            continue
        formatted = lint_text(original, args.column)
        if formatted != original:
            if args.check:
                changed.append(path)
                print(f"would reformat {path}")
            else:
                with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
                    f.write(formatted)
                changed.append(path)
                print(f"reformatted {path}")

    if args.check and changed:
        sys.exit(1)
    if not changed:
        print("no changes needed")


if __name__ == "__main__":
    main()
