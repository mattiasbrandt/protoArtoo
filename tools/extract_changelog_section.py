#!/usr/bin/env python3
"""
Extract a single `## [x.y.z]` section body from CHANGELOG.md.

Usage: extract_changelog_section.py <version> [changelog_path]
  version: e.g. "1.0.0" (no leading "v")

Prints the section body (everything between this version's heading and the
next `## [...]` heading) to stdout. Exits non-zero if the version isn't found.

Used by .github/workflows/release.yml so GitHub Release notes are always
sourced from CHANGELOG.md rather than hand-written at tag time.
"""

import re
import sys


def extract(version: str, changelog_path: str) -> str:
    with open(changelog_path, "r") as f:
        content = f.read()

    pattern = re.compile(
        r"^##\s*\[" + re.escape(version) + r"\].*$",
        re.MULTILINE,
    )
    match = pattern.search(content)
    if not match:
        raise ValueError(f"No CHANGELOG.md section found for version {version}")

    start = match.end()
    next_heading = re.search(r"^##\s*\[", content[start:], re.MULTILINE)
    end = start + next_heading.start() if next_heading else len(content)
    return content[start:end].strip() + "\n"


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(f"usage: {sys.argv[0]} <version> [changelog_path]", file=sys.stderr)
        return 2

    version = sys.argv[1]
    changelog_path = sys.argv[2] if len(sys.argv) == 3 else "CHANGELOG.md"

    try:
        sys.stdout.write(extract(version, changelog_path))
    except (FileNotFoundError, ValueError) as exc:
        print(f"extract_changelog_section.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
