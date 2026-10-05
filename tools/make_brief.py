#!/usr/bin/env python3
"""Render a worker prompt from the coordinate-epic brief template.

    python3 tools/make_brief.py --issue <n> --slug <s> --worktree <path> --base <branch> [--out FILE]

Fills {ISSUE}, {WORKTREE}, {BASE}, {PIN_MARKER} (`<!-- coordinator-pin-<s> -->`)
and {STATUS_MARKER} (`<!-- worker-status-<n>-<s> -->`) in the part of
.claude/skills/coordinate-epic/worker-brief.md below its first `---` rule, and
writes the result to --out (default /tmp/brief-<s>.md).

Why: briefs were made by copying the previous one and running sed over the old
dispatch's spellings; one missed spelling sends a worker to another slice's pin
or makes it post under another slice's status marker (#470).

The tool refuses to write a brief that still holds a {...} placeholder. The one
exception is gh's own {owner} and {repo}, which gh fills when the worker runs
the command.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / ".claude" / "skills" / "coordinate-epic" / "worker-brief.md"

# A slug lands inside an HTML comment and inside a --jq string literal, so
# quotes, whitespace and "--" are refused rather than escaped.
SLUG_RE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
PLACEHOLDER_RE = re.compile(r"\{[A-Za-z_][A-Za-z0-9_]*\}")
GH_PLACEHOLDERS = {"{owner}", "{repo}"}
RULE = "\n---\n"


def pin_marker(slug: str) -> str:
    return f"<!-- coordinator-pin-{slug} -->"


def status_marker(issue: int, slug: str) -> str:
    return f"<!-- worker-status-{issue}-{slug} -->"


def leftover_placeholders(text: str) -> list[str]:
    return sorted({m for m in PLACEHOLDER_RE.findall(text) if m not in GH_PLACEHOLDERS})


def render(template: str, *, issue: int, slug: str, worktree: str, base: str) -> str:
    """Return the brief: the template body below its first rule, filled."""
    if RULE not in template:
        raise ValueError("template has no '---' rule separating the header from the brief")
    body = template.split(RULE, 1)[1].lstrip("\n")
    values = {
        "{ISSUE}": str(issue),
        "{WORKTREE}": worktree,
        "{BASE}": base,
        "{PIN_MARKER}": pin_marker(slug),
        "{STATUS_MARKER}": status_marker(issue, slug),
    }
    for key, value in values.items():
        body = body.replace(key, value)
    return body


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--issue", type=int, required=True)
    parser.add_argument("--slug", required=True, help="dispatch slug, e.g. 355rr")
    parser.add_argument("--worktree", required=True)
    parser.add_argument("--base", required=True, help="the epic's integration branch")
    parser.add_argument("--out", type=Path, help="default /tmp/brief-<slug>.md")
    parser.add_argument("--template", type=Path, default=TEMPLATE, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)

    if args.issue <= 0:
        parser.error(f"--issue must be a positive issue number, got {args.issue}")
    if not SLUG_RE.match(args.slug):
        parser.error(f"--slug {args.slug!r}: use lowercase letters, digits and single hyphens")
    for name in ("worktree", "base"):
        value = getattr(args, name)
        if not value.strip() or PLACEHOLDER_RE.search(value):
            parser.error(f"--{name} {value!r} is empty or holds a {{...}} placeholder")

    brief = render(args.template.read_text(encoding="utf-8"), issue=args.issue,
                   slug=args.slug, worktree=args.worktree, base=args.base)
    left = leftover_placeholders(brief)
    if left:
        print(f"make_brief: refusing to write; unfilled placeholders: {', '.join(left)}",
              file=sys.stderr)
        return 1

    out = args.out or Path(f"/tmp/brief-{args.slug}.md")
    out.write_text(brief, encoding="utf-8")
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
