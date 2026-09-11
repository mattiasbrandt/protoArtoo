#!/usr/bin/env python3
"""
Turn CHANGELOG.md's `## [Unreleased]` section into a dated release section.

Part of the automatic release flow (issue #285). A minor or major release
still ships the curated maker-voice prose it always has -- but with releases
cut seconds after a merge, nobody gets a window in which to rename the
heading by hand. So the author keeps writing under `[Unreleased]`, exactly as
CONTRIBUTING.md's pull-request checklist already says, and CI stamps the
version and the date on it at release time.

Before:

    ## [Unreleased]

    ### Added
    - Something a builder can see.

    ## [1.2.0] - 2026-09-06

After `promote_changelog.py 1.3.0`:

    ## [Unreleased]

    ## [1.3.0] - 2026-09-11

    ### Added
    - Something a builder can see.

    ## [1.2.0] - 2026-09-06

Two behaviours the release workflow depends on:

* **An empty `[Unreleased]` is a failure, not an empty release.** A feature
  reached `main` with nothing written about it; publishing a bare heading
  would be worse than stopping, so this exits non-zero and the release does
  not happen. That is the ticket's acceptance criterion, enforced here.
* **Promoting twice is a no-op.** If the target section already exists the
  script says so and succeeds. The workflow pushes the promoted commit before
  it pushes the tag, so a run that dies between the two can simply be re-run
  rather than leaving `main` wedged with a promoted changelog and no tag.

Patch releases never come through here: their notes are generated from commit
subjects by tools/release_plan.py, and CHANGELOG.md says so.
"""

import argparse
import datetime
import re
import sys

UNRELEASED_HEADING = "## [Unreleased]"

# Any `## [...]` heading -- the section boundary, matching the rule
# tools/extract_changelog_section.py reads the file by.
_SECTION = re.compile(r"^##\s*\[", re.MULTILINE)


class PromoteError(Exception):
    """A condition the author has to fix before the release can proceed."""


def _find_unreleased(content):
    """Return (heading_start, body_start, body_end) for the Unreleased section."""
    match = re.search(r"^##\s*\[Unreleased\].*$", content, re.MULTILINE)
    if not match:
        raise PromoteError(
            f"no {UNRELEASED_HEADING} heading in CHANGELOG.md -- "
            "the release flow writes every section through it"
        )
    body_start = match.end()
    following = _SECTION.search(content, body_start)
    body_end = following.start() if following else len(content)
    return match.start(), body_start, body_end


def has_section(content, version):
    """True if CHANGELOG.md already carries a `## [version]` heading."""
    pattern = re.compile(r"^##\s*\[" + re.escape(version) + r"\]", re.MULTILINE)
    return bool(pattern.search(content))


def promote(content, version, date):
    """Return CHANGELOG.md with `[Unreleased]` renamed to `[version] - date`.

    Raises PromoteError when the Unreleased section has no content.
    """
    heading_start, body_start, body_end = _find_unreleased(content)
    body = content[body_start:body_end].strip()
    if not body:
        raise PromoteError(
            f"{UNRELEASED_HEADING} is empty, so version {version} has nothing to "
            "say for itself. Write the section before the change that bumps the "
            "minor or major version reaches main."
        )
    return (
        content[:heading_start]
        + f"{UNRELEASED_HEADING}\n\n"
        + f"## [{version}] - {date}\n\n"
        + body
        + "\n\n"
        + content[body_end:].lstrip("\n")
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Promote CHANGELOG.md's Unreleased section to a version."
    )
    parser.add_argument("version", help="the version being released, e.g. 1.3.0")
    parser.add_argument(
        "--path", default="CHANGELOG.md", help="changelog path (default: CHANGELOG.md)"
    )
    parser.add_argument(
        "--date", help="release date as YYYY-MM-DD (default: today, UTC)"
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="only report whether the section could be promoted; write nothing",
    )
    args = parser.parse_args(argv)

    date = args.date or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")

    try:
        with open(args.path, "r") as handle:
            content = handle.read()
    except FileNotFoundError:
        print(f"promote_changelog.py: no such file: {args.path}", file=sys.stderr)
        return 1

    if has_section(content, args.version):
        # Already done -- by an earlier run of this same workflow that failed
        # after pushing. Say so and succeed, so the re-run gets to the tag.
        print(
            f"promote_changelog.py: {args.path} already has a [{args.version}] "
            "section; nothing to promote."
        )
        return 0

    try:
        promoted = promote(content, args.version, date)
    except PromoteError as exc:
        print(f"promote_changelog.py: {exc}", file=sys.stderr)
        return 1

    if args.check:
        print(
            f"promote_changelog.py: {UNRELEASED_HEADING} is ready to become "
            f"[{args.version}] - {date}."
        )
        return 0

    with open(args.path, "w") as handle:
        handle.write(promoted)
    print(f"promote_changelog.py: {args.path} -> [{args.version}] - {date}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
