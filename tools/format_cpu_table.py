#!/usr/bin/env python3
"""Align the columns of a `.cpu` instruction table.

A row is three `|`-separated columns. Aligning them makes the encoding patterns
line up so bit fields can be read down the page, which is most of the point of
writing an instruction set this way.

Widths are computed per table rather than per file: the `ed` table's mnemonics
are much wider than `base`'s, and one width for the whole file would leave the
common rows swimming in space.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def is_row(line: str) -> bool:
    """Mirrors `is_row` in Lexical.hpp: a row is a non-comment, non-declaration line with a `|`."""
    stripped = line.strip()
    return (
        bool(stripped)
        and not stripped.startswith("#")
        and not stripped.startswith("vocab ")
        and not stripped.startswith("table ")
        and "|" in stripped
    )


def starts_table(line: str) -> bool:
    stripped = line.strip()
    return stripped == "table" or stripped.startswith("table ")


def split_row(line: str) -> list[str]:
    return [column.strip() for column in line.strip().split("|")]


def format_table(lines: list[str]) -> list[str]:
    """Format one run of lines, aligning any rows among them to shared widths."""
    rows = [split_row(line) for line in lines if is_row(line)]
    if not rows:
        return lines
    # The last column is not padded, so only the leading ones need widths.
    columns = max(len(row) for row in rows)
    widths = [max(len(row[at]) for row in rows if len(row) > at) for at in range(columns - 1)]

    formatted = []
    for line in lines:
        if not is_row(line):
            formatted.append(line)
            continue
        parts = split_row(line)
        padded = [part.ljust(widths[at]) for at, part in enumerate(parts[:-1])]
        formatted.append(" | ".join([*padded, parts[-1]]).rstrip() + "\n")
    return formatted


def format_file(text: str) -> str:
    """Split into per-table runs, format each, and stitch back together."""
    lines = text.splitlines(keepends=True)
    output: list[str] = []
    current: list[str] = []
    for line in lines:
        if starts_table(line):
            output.extend(format_table(current))
            current = []
        current.append(line)
    output.extend(format_table(current))
    return "".join(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--check", action="store_true", help="report rather than rewrite")
    args = parser.parse_args()

    changed = False
    for path in args.files:
        original = path.read_text()
        formatted = format_file(original)
        if formatted == original:
            continue
        changed = True
        if args.check:
            print(f"{path}: columns are not aligned", file=sys.stderr)
        else:
            path.write_text(formatted)
            print(f"{path}: aligned", file=sys.stderr)
    return 1 if changed else 0


if __name__ == "__main__":
    sys.exit(main())
