#!/usr/bin/env python3
"""Where a shared class lives in data/style.css.

    python3 tools/css_where.py --sections
    python3 tools/css_where.py .btn
    python3 tools/css_where.py .number-cell .parts-table

--sections prints each banner comment and the line it starts on. A selector
prints the banner it sits under and the line, so a class is found without
reading the file. A line number here is a hint: the selector is the lookup.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

CSS = Path(__file__).resolve().parents[1] / "data" / "style.css"
BANNER = re.compile(r"/\*\s*[-=]{8,}")


def _title(lines: list[str], index: int) -> str:
    """The first words of a banner block. The banner line itself is rules."""
    for line in lines[index:index + 6]:
        text = line.strip().lstrip("/*").strip().rstrip("*/").strip()
        if text and not re.fullmatch(r"[-=]+", text):
            return text
    return "(untitled)"


def sections(lines: list[str]) -> list[tuple[int, str]]:
    found = []
    for i, line in enumerate(lines):
        if BANNER.search(line):
            found.append((i + 1, _title(lines, i)))
    return found


def section_at(found: list[tuple[int, str]], line_no: int) -> str:
    title = "(before the first banner)"
    for start, name in found:
        if start <= line_no:
            title = name
        else:
            break
    return title


def find_selector(lines: list[str], found: list[tuple[int, str]], selector: str) -> list[str]:
    # A class token, so .btn does not match .btn-sm.
    pattern = re.compile(r"(?<![\w-])" + re.escape(selector) + r"(?![\w-])")
    rows = []
    for i, line in enumerate(lines, 1):
        code = line.split("/*", 1)[0]
        if pattern.search(code):
            rows.append(f"{i:5d}  {section_at(found, i)}\n       {line.strip()}")
    return rows


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--sections", action="store_true", help="list banner sections")
    p.add_argument("selector", nargs="*", help="a CSS selector, for example .btn")
    args = p.parse_args(argv)
    if not args.sections and not args.selector:
        p.error("pass --sections or at least one selector")
    lines = CSS.read_text(encoding="utf-8").splitlines()
    found = sections(lines)
    if args.sections:
        for line_no, title in found:
            print(f"{line_no:5d}  {title}")
    missing = 0
    for selector in args.selector:
        rows = find_selector(lines, found, selector)
        print(f"# {selector}  ({len(rows)})")
        if not rows:
            missing += 1
            print("       (no line)")
        else:
            print("\n".join(rows))
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
