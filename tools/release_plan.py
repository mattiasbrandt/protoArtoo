#!/usr/bin/env python3
"""
Decide whether a range of commits on `main` is a release, and of what kind.

This is the brain behind .github/workflows/auto-release.yml (issue #285): a
merge to `main` releases itself, instead of waiting for someone to remember to
tag. Every decision it makes comes from the Conventional Commits type table in
CONTRIBUTING.md, which is the project's own already-enforced contract:

    feat            -> MINOR
    fix             -> PATCH
    feat! / fix!    -> MAJOR   (also: a BREAKING CHANGE: footer)
    everything else -> no release

Subcommands
-----------
decide            What should happen for the commits since the last release
                  tag? Emits bump / current / next / tier as JSON, and
                  optionally as GitHub Actions step outputs.
tier <tag>        Is this tag a patch release (notes and the source tag only)
                  or a full one (notes plus the firmware and filesystem
                  images)? A pure function of the tag string: semver says a
                  patch release always has a non-zero Z and a minor or major
                  release always has Z == 0, so no history lookup is needed.
notes <tag>       Generated release notes for a patch tag, from the commit
                  subjects in the range. Terse and clearly machine-written --
                  a patch cannot wait for someone to write maker-voice prose,
                  which is the whole reason the two tiers exist.

Every subcommand is safe to run standalone against a checkout:

    python3 tools/release_plan.py decide
    python3 tools/release_plan.py tier v1.2.1
    python3 tools/release_plan.py notes v1.2.1

Tag vocabulary matches tools/extract_version.py: only `v[0-9]*` names a
release. The repo's other tags (safepoint markers, sync-conflict leftovers)
are invisible here, exactly as they are to version stamping.
"""

import argparse
import json
import os
import re
import subprocess
import sys

# Tags that name a release. Must stay in step with tools/extract_version.py's
# RELEASE_TAG_GLOB -- the two scripts have to agree on what a version is.
RELEASE_TAG_GLOB = "v[0-9]*"

# Record and field separators for `git log --format`. Commit bodies contain
# blank lines and every printable character, so the usual newline-splitting
# does not survive them; ASCII unit/record separators do.
_FIELD = "\x1f"
_RECORD = "\x1e"

# type(scope): summary, with the breaking-change `!` in either position.
# CONTRIBUTING.md shows both spellings in its own examples -- `feat!(drive):`
# under "A breaking change is ... declared with", and `feat(hw)!:` in the
# example block below it -- so both are accepted rather than one being called
# a typo in the contract this script enforces.
_SUBJECT = re.compile(
    r"^(?P<type>[a-z]+)"
    r"(?P<bang_before>!)?"
    r"(?:\((?P<scope>[^)]*)\))?"
    r"(?P<bang_after>!)?"
    r": "
    r"(?P<summary>.+)$"
)

# A BREAKING CHANGE footer, per Conventional Commits 1.0.0. Either spelling of
# the token is valid there, and the footer must start its own line.
_BREAKING_FOOTER = re.compile(r"^BREAKING[ -]CHANGE:", re.MULTILINE)

# Which type produces which bump. Types absent from this map produce none --
# that is refactor, test, docs, chore, style and perf, matching CONTRIBUTING.md.
_BUMP_BY_TYPE = {"feat": "minor", "fix": "patch"}

# Ordered weakest to strongest, so the strongest bump in a range wins.
_BUMP_RANK = {None: 0, "patch": 1, "minor": 2, "major": 3}

# Heading shown for each type in generated patch notes. A type missing here
# falls into the collapsed "other commits" block.
_NOTES_HEADING = {"fix": "Fixed"}

# The bot that .github/workflows/version-sync.yml commits as. It writes one
# `chore(ci): sync version JSON ...` commit per push to main, which is
# machinery rather than a change: 87 of the 174 non-fix commits between
# v1.2.0 and 2026-09-11 were this bot, and listing them makes the generated
# notes unreadable. Excluded from notes only -- `decide` still reads every
# commit, so nothing can hide a version bump behind an author filter.
# Must stay in step with the author guard in version-sync.yml,
# verification.yml and auto-release.yml.
VERSION_SYNC_BOT_EMAIL = "41898282+github-actions[bot]@users.noreply.github.com"


class ReleasePlanError(Exception):
    """A condition the caller has to fix: no tags, an unparseable version."""


# ── git ──────────────────────────────────────────────────────────────────────


def _git(repo, *args, strip=True):
    """Run a git command in `repo`; return stdout, stripped unless told not to.

    `strip=False` matters for the separator-delimited log format below:
    Python counts the ASCII separators \\x1e and \\x1f as whitespace, so a
    plain .strip() silently eats the final record's terminators and the last
    commit in every range fails to parse.

    Errors are not swallowed: a failing git call raises, because every caller
    here is asking a question whose wrong answer would publish a wrong release.
    """
    out = subprocess.check_output(
        ["git", *args], cwd=repo, stderr=subprocess.PIPE
    ).decode()
    return out.strip() if strip else out


def latest_release_tag(repo, before=None):
    """Nearest release tag reachable from `before` (default HEAD), or None.

    `git describe --abbrev=0` walks history rather than sorting the tag
    namespace, so on a branch this answers "which release am I descended
    from", which is the question a release range actually asks.
    """
    args = ["describe", "--tags", "--abbrev=0", "--match", RELEASE_TAG_GLOB]
    if before:
        args.append(before)
    try:
        return _git(repo, *args) or None
    except subprocess.CalledProcessError:
        # No release tag is reachable. Not an error at this level -- the
        # caller decides whether that means "first release" or "give up".
        return None


def latest_full_release_tag(repo, before=None):
    """Nearest tag that carried firmware images, i.e. the nearest X.Y.0.

    Patch notes point the reader at this one, because it is where the most
    recent flashable images are attached.
    """
    args = [
        "tag", "--list", RELEASE_TAG_GLOB, "--sort=-v:refname",
        "--merged", before or "HEAD",
    ]
    for tag in _git(repo, *args).splitlines():
        tag = tag.strip()
        if not tag:
            continue
        try:
            major, minor, patch, pre = parse_version(tag)
        except ReleasePlanError:
            continue
        if patch == 0 and not pre:
            return tag
    return None


def commits_in(repo, frm, to):
    """Commits in `frm..to` (or everything up to `to` when frm is None).

    Returns a list of {"sha", "author", "subject", "body"}, newest first --
    git log's own order, which reads correctly in release notes.
    """
    rng = f"{frm}..{to}" if frm else to
    out = _git(
        repo,
        "log",
        f"--format=%H{_FIELD}%ae{_FIELD}%s{_FIELD}%b{_RECORD}",
        rng,
        strip=False,
    )
    commits = []
    for record in out.split(_RECORD):
        # git puts a newline after each formatted commit, so every record
        # after the first opens with one. Nothing else is trimmed: a commit
        # body is content.
        record = record.lstrip("\n")
        if not record:
            continue
        sha, author, subject, body = record.split(_FIELD, 3)
        commits.append(
            {"sha": sha, "author": author, "subject": subject, "body": body}
        )
    return commits


# ── conventional commits ─────────────────────────────────────────────────────


def parse_subject(subject):
    """Parse a Conventional Commits subject.

    Returns {"type", "scope", "breaking", "summary"} or None when the subject
    is not conventional -- a merge commit, or history from before the
    convention. None means "contributes nothing", never "reject the release":
    this script reports on what landed, it does not police it.
    """
    match = _SUBJECT.match(subject)
    if not match:
        return None
    return {
        "type": match.group("type"),
        "scope": match.group("scope"),
        "breaking": bool(match.group("bang_before") or match.group("bang_after")),
        "summary": match.group("summary"),
    }


def bump_for(commits):
    """Strongest version bump the commits call for, or None for no release.

    `commits` is a list of {"subject", "body"} -- the shape commits_in returns.
    """
    strongest = None
    for commit in commits:
        parsed = parse_subject(commit["subject"])
        if parsed is None:
            continue
        if parsed["breaking"] or _BREAKING_FOOTER.search(commit.get("body") or ""):
            bump = "major"
        else:
            bump = _BUMP_BY_TYPE.get(parsed["type"])
        if _BUMP_RANK[bump] > _BUMP_RANK[strongest]:
            strongest = bump
    return strongest


# ── versions ─────────────────────────────────────────────────────────────────


def parse_version(tag):
    """Split a release tag into (major, minor, patch, prerelease).

    Accepts the tag with or without its leading `v`. Build metadata (`+...`)
    is dropped; the prerelease part is returned so callers can tell
    `v1.3.0-rc.1` from `v1.3.0`.
    """
    text = tag[1:] if tag.startswith("v") else tag
    text = text.split("+", 1)[0]
    core, _, prerelease = text.partition("-")
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", core)
    if not match:
        raise ReleasePlanError(f"not a release version: {tag!r}")
    major, minor, patch = (int(part) for part in match.groups())
    return major, minor, patch, prerelease or None


def next_version(current, bump):
    """Apply `bump` to `current`, returning a bare `X.Y.Z` (no `v`).

    A prerelease on `current` is dropped: the release after `v1.3.0-rc.1` with
    a patch bump is `1.3.1`, not a further prerelease. This project has cut no
    prerelease since v1.0.0-alpha.1 and cuts them by hand when it does.
    """
    major, minor, patch, _ = parse_version(current)
    if bump == "major":
        return f"{major + 1}.0.0"
    if bump == "minor":
        return f"{major}.{minor + 1}.0"
    if bump == "patch":
        return f"{major}.{minor}.{patch + 1}"
    raise ReleasePlanError(f"not a bump: {bump!r}")


def tier_for_tag(tag):
    """'patch' (notes and the source tag only) or 'full' (notes plus images).

    Pure semver: a patch release is the only kind with a non-zero Z, so the
    tag alone decides. A prerelease keeps the tier of its core version, so
    `v1.3.0-rc.1` still builds images.
    """
    _, _, patch, _ = parse_version(tag)
    return "patch" if patch else "full"


# ── release notes ────────────────────────────────────────────────────────────


def _repo_url(repo):
    """https URL of the origin remote, or None when there is no usable one."""
    try:
        url = _git(repo, "remote", "get-url", "origin")
    except subprocess.CalledProcessError:
        return None
    if url.startswith("git@"):
        host, _, path = url.partition(":")
        url = f"https://{host.split('@', 1)[1]}/{path}"
    if url.endswith(".git"):
        url = url[: -len(".git")]
    return url if url.startswith("https://") else None


def _commit_line(commit):
    """One generated notes line for a conventional commit."""
    parsed = parse_subject(commit["subject"])
    scope = parsed["scope"]
    label = f"`{parsed['type']}({scope})`" if scope else f"`{parsed['type']}`"
    return f"- {label} {parsed['summary']}"


def patch_notes(repo, tag, commits, previous_tag, full_tag, repo_url=None):
    """Render the generated notes for a patch release.

    Deliberately machine-flavoured: it says so on the first line, so nobody
    reads terse commit subjects as this project's usual maker voice. The
    closing paragraph is the exception and is written in maker voice, because
    "a release with no files attached" is a new thing for a builder to meet
    and an unexplained empty release reads as a broken one.
    """
    range_text = f"`{previous_tag}..{tag}`" if previous_tag else f"up to `{tag}`"
    lines = [
        f"Generated from the commit subjects in {range_text}. "
        "Fixes only -- this release ships no firmware images, see below.",
        "",
    ]

    grouped = {}
    other = []
    for commit in commits:
        if commit.get("author") == VERSION_SYNC_BOT_EMAIL:
            continue
        parsed = parse_subject(commit["subject"])
        if parsed is None:
            continue
        heading = _NOTES_HEADING.get(parsed["type"])
        if heading:
            grouped.setdefault(heading, []).append(commit)
        else:
            other.append(commit)

    for heading in _NOTES_HEADING.values():
        if heading not in grouped:
            continue
        lines.append(f"### {heading}")
        lines.extend(_commit_line(commit) for commit in grouped[heading])
        lines.append("")

    if other:
        lines.append(
            f"<details><summary>Everything else in this range ({len(other)})</summary>"
        )
        lines.append("")
        lines.extend(_commit_line(commit) for commit in other)
        lines.append("")
        lines.append("</details>")
        lines.append("")

    lines.append("---")
    lines.append("")
    if full_tag:
        where = (
            f"[{full_tag}]({repo_url}/releases/tag/{full_tag})"
            if repo_url
            else f"`{full_tag}`"
        )
        lines.append(
            "**There are no files to download here, and that is on purpose.** A patch "
            "release ships the source tag and these notes, so a one-line fix does not "
            f"wait on four firmware builds. The newest flashable images are on {where}, "
            "and they were built before this fix -- to run it now, build from this tag; "
            "otherwise it reaches you with the next feature release."
        )
    else:
        lines.append(
            "**There are no files to download here, and that is on purpose.** A patch "
            "release ships the source tag and these notes, so a one-line fix does not "
            "wait on four firmware builds. Build from this tag to run it now; otherwise "
            "it reaches you with the next feature release."
        )
    return "\n".join(lines) + "\n"


# ── CLI ──────────────────────────────────────────────────────────────────────


def _emit_outputs(values):
    """Append key=value lines to $GITHUB_OUTPUT when running under Actions."""
    path = os.environ.get("GITHUB_OUTPUT")
    if not path:
        print(
            "release_plan.py: --github-output given but GITHUB_OUTPUT is not set",
            file=sys.stderr,
        )
        return False
    with open(path, "a") as handle:
        for key, value in values.items():
            handle.write(f"{key}={'' if value is None else value}\n")
    return True


def _cmd_decide(args):
    repo = args.repo
    frm = args.frm or latest_release_tag(repo)
    if frm is None:
        raise ReleasePlanError(
            "no release tag is reachable from HEAD, so there is no range to read. "
            "Tag a base version by hand before auto-release can take over."
        )
    commits = commits_in(repo, frm, args.to)
    bump = bump_for(commits)
    plan = {
        "bump": bump or "none",
        "current": frm,
        "commits": len(commits),
    }
    if bump:
        version = next_version(frm, bump)
        plan["next"] = version
        plan["tag"] = f"v{version}"
        plan["tier"] = tier_for_tag(version)
    else:
        plan["next"] = ""
        plan["tag"] = ""
        plan["tier"] = ""
    if args.github_output and not _emit_outputs(plan):
        return 1
    print(json.dumps(plan, indent=2))
    return 0


def _cmd_tier(args):
    tier = tier_for_tag(args.tag)
    if args.github_output and not _emit_outputs({"tier": tier}):
        return 1
    print(tier)
    return 0


def _cmd_notes(args):
    repo = args.repo
    previous = args.frm or latest_release_tag(repo, before=f"{args.tag}^")
    commits = commits_in(repo, previous, args.tag)
    full_tag = latest_full_release_tag(repo, before=f"{args.tag}^")
    sys.stdout.write(
        patch_notes(
            repo,
            args.tag,
            commits,
            previous,
            full_tag,
            repo_url=args.repo_url or _repo_url(repo),
        )
    )
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument(
        "--repo", default=".", help="path to the git checkout (default: .)"
    )
    sub = parser.add_subparsers(dest="command", required=True)

    decide = sub.add_parser("decide", help="what release do these commits call for?")
    decide.add_argument("--from", dest="frm", help="range start (default: last tag)")
    decide.add_argument("--to", default="HEAD", help="range end (default: HEAD)")
    decide.add_argument(
        "--github-output", action="store_true", help="also write $GITHUB_OUTPUT"
    )
    decide.set_defaults(func=_cmd_decide)

    tier = sub.add_parser("tier", help="patch or full release for this tag?")
    tier.add_argument("tag")
    tier.add_argument(
        "--github-output", action="store_true", help="also write $GITHUB_OUTPUT"
    )
    tier.set_defaults(func=_cmd_tier)

    notes = sub.add_parser("notes", help="generated notes for a patch tag")
    notes.add_argument("tag")
    notes.add_argument("--from", dest="frm", help="range start (default: previous tag)")
    notes.add_argument("--repo-url", help="override the origin URL used for links")
    notes.set_defaults(func=_cmd_notes)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except ReleasePlanError as exc:
        print(f"release_plan.py: {exc}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or b"").decode().strip()
        print(f"release_plan.py: git failed: {detail}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
