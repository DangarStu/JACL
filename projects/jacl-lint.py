#!/usr/bin/env python3
"""Align property lines within JACL location/object/player blocks, and
warn about unclosed string literals.

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

In addition, every line is scanned for an odd number of `"` characters
outside of `print:` blocks and `;` comments. JACL string literals run
until the parser sees a closing quote, possibly many lines later, so a
missing close on a `write "<div ...>"` silently absorbs the next write
into the literal. These are reported to stderr; --check exits non-zero
if any are found.

Usage:
    ./jacl-lint.py path/to/game.jacl [more.jacl ...]
    ./jacl-lint.py --check path/to/game.jacl    # exit 1 if changes needed
    ./jacl-lint.py --column 20 game.jacl        # custom alignment column
    ./jacl-lint.py projects/                    # recurse a directory
                                                # (.jacl + .library)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

BLOCK_START = re.compile(r"^(location|object|player)\b")
TOP_DECL = re.compile(r"^(constant|integer|integer_array|string|string_array|synonym|attribute|variable|grammar)\b")
PRINT_START = re.compile(r"^([ \t]*)print:\s*$")
# Print-block terminator. The JACL loader checks text_buffer[0] == '.'
# after jpp has stripped leading whitespace, so the source-level '.'
# can sit at any indent. We reformat it to match the opening 'print:'
# column for readability.
PRINT_END = re.compile(r"^[ \t]*\.\s*$")

DEFAULT_COLUMN = 16
DECL_TRAILING_SPACES = 5
PRINT_BODY_INDENT = 3


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
    in_print = False
    print_indent = ""
    i = 0
    while i < len(raw_lines):
        line = raw_lines[i]
        # Print-block re-indent has highest priority. Body lines are
        # rewritten so they sit PRINT_BODY_INDENT spaces past the
        # opening 'print:' keyword's own indent. Blank lines are
        # preserved verbatim. The '.' terminator (must be at column 0
        # for the JACL loader) ends the block.
        if in_print:
            if PRINT_END.match(line):
                in_print = False
                # Indent the '.' terminator to match the opening 'print:'.
                # jpp strips leading whitespace before the loader reads
                # the file, so this is purely a source-readability tweak.
                out.append(print_indent + ".\n")
                i += 1
                continue
            stripped_full = line.lstrip(" \t")
            if not stripped_full.strip():
                out.append(line)
            else:
                out.append(print_indent + " " * PRINT_BODY_INDENT + stripped_full)
            i += 1
            continue
        m_print = PRINT_START.match(line)
        if m_print:
            in_print = True
            print_indent = m_print.group(1)
            out.append(line)
            i += 1
            continue
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
            yield from sorted(p.rglob("*.library"))
        else:
            yield p


def check_unclosed_quotes(text: str, path: Path):
    """Warn about lines with an odd number of `"` chars.

    JACL string literals do not span lines in any well-formed code we
    have seen — but the parser is permissive: an unclosed `"` runs
    until the next `"` it finds (possibly many lines later), absorbing
    intervening source as part of the string. The same pattern has bit
    blackjacl.jacl, musicinterface.library, webapp.library and
    webinterface.library; in each case the bug was a missing closing
    quote on a `write "<div ...>` line, which silently corrupted the
    rendered HTML.

    Detection: outside of `print:` blocks and comments, every line that
    contains any `"` characters should contain an even count. Returns
    a list of (lineno, line) tuples for any odd-count line.
    """
    warnings = []
    in_print = False
    for lineno, line in enumerate(text.splitlines(), start=1):
        if in_print:
            if PRINT_END.match(line):
                in_print = False
            continue
        if PRINT_START.match(line):
            in_print = True
            continue
        stripped = line.lstrip(" \t")
        if not stripped or stripped.startswith("#"):
            continue
        # Strip an inline `;` comment (JACL ignores tokens past the
        # ones a keyword requires; convention is to start trailing
        # notes with `;`). Only honour `;` outside a quote.
        in_quote = False
        cut = len(line)
        for idx, ch in enumerate(line):
            if ch == '"':
                in_quote = not in_quote
            elif ch == ';' and not in_quote:
                cut = idx
                break
        scanned = line[:cut]
        if scanned.count('"') % 2 != 0:
            warnings.append((lineno, line.rstrip()))
    return warnings


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
    warned = False
    for path in iter_jacl_paths(args.paths):
        try:
            # surrogateescape lets us round-trip non-UTF-8 bytes (e.g.
            # smart quotes from Windows-1252) without losing data.
            with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
                original = f.read()
        except OSError as e:
            print(f"warning: cannot read {path}: {e}", file=sys.stderr)
            continue
        for lineno, snippet in check_unclosed_quotes(original, path):
            print(f"{path}:{lineno}: odd quote count -- likely unclosed string: {snippet}",
                  file=sys.stderr)
            warned = True
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

    if args.check and (changed or warned):
        sys.exit(1)
    if not changed and not warned:
        print("no changes needed")


if __name__ == "__main__":
    main()
