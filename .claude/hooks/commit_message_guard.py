#!/usr/bin/env python3
"""PreToolUse hook: enforce phase commit message format for git commit commands."""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ALLOWED_TYPES = "feat|fix|docs|refactor|chore|test|style|perf"
ALLOWED_SCOPES = "drive|sbus|failsafe|dome|audio|servo|web|nvs|wifi|hw|plan|test|ci"
PHASE_PATTERN = re.compile(
    rf"^(?:{ALLOWED_TYPES})!?\(phase:v\d+\.\d+\.\d+/T\d{{2}}(?:/slice:[a-z0-9-]+)?\): .+",
    re.DOTALL,
)
LEGACY_SCOPE_PATTERN = re.compile(
    rf"^(?:{ALLOWED_TYPES})!?\((?:{ALLOWED_SCOPES})\): .+",
    re.DOTALL,
)
COMMIT_CMD_PATTERN = re.compile(r"(^|\s)git\s+commit(\s|$)")
ADD_CMD_PATTERN = re.compile(r"(^|\s)git\s+add(\s|$)")
# Handles -m and --message, both = and space separators, single/double quotes.
MESSAGE_ARG_PATTERN = re.compile(
    r"""(?:^|\s)(?:-m|--message)(?:=|\s+)([\"'])(.*?)\1""",
    re.DOTALL,
)
# Handles heredoc-style: -m "$(cat <<'EOF'\n...\nEOF\n)"
HEREDOC_MSG_PATTERN = re.compile(
    r"""(?:^|\s)(?:-m|--message)(?:=|\s+)"?\$\(cat\s+<<'?(\w+)'?[ \t]*\n(.*?)\n[ \t]*\1[ \t]*\n[ \t]*\)""",
    re.DOTALL,
)
COAUTHOR_LINE_PATTERN = re.compile(r"co-authored-by\s*:", re.IGNORECASE)
COAUTHOR_TRAILER_PATTERN = re.compile(
    r"--trailer(?:=|\s+)[^\n]*co-authored-by", re.IGNORECASE
)
VERSION_GLOBS = ("data/*version*.json",)


def _deny(reason: str) -> None:
    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }
    print(json.dumps(payload))


def _run_git(args: list[str], repo_dir: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", *args],
        cwd=repo_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def _extract_message(cmd: str) -> str | None:
    """Return the commit message string, or None if it cannot be parsed."""
    m = HEREDOC_MSG_PATTERN.search(cmd)
    if m:
        return m.group(2).strip()
    m = MESSAGE_ARG_PATTERN.search(cmd)
    if m:
        return m.group(2).strip()
    return None


def _strip_message_content(cmd: str) -> str:
    """Remove -m/--message values so their text cannot trigger false pattern matches."""
    cmd = HEREDOC_MSG_PATTERN.sub(" ", cmd)
    cmd = MESSAGE_ARG_PATTERN.sub(" ", cmd)
    return cmd


def _is_chained_add_then_commit(cmd: str) -> bool:
    cmd = _strip_message_content(cmd)
    add_match = ADD_CMD_PATTERN.search(cmd)
    commit_match = COMMIT_CMD_PATTERN.search(cmd)
    if not add_match or not commit_match:
        return False
    if add_match.start() >= commit_match.start():
        return False
    between = cmd[add_match.end():commit_match.start()]
    return bool(re.search(r"&&|;|\n", between))


def _version_files_staged(repo_dir: str) -> list[str]:
    """Return staged version file paths; empty list means no version files are being committed."""
    proc = _run_git(["diff", "--cached", "--name-only", "--", *VERSION_GLOBS], repo_dir)
    if proc.returncode != 0:
        return []
    return [p for p in (proc.stdout or "").splitlines() if p.strip()]


def _version_file_issues(repo_dir: str) -> list[str]:
    proc = _run_git(["status", "--porcelain", "--", *VERSION_GLOBS], repo_dir)
    if proc.returncode != 0:
        return []

    issues = []
    for raw in (proc.stdout or "").splitlines():
        line = raw.rstrip()
        if len(line) < 4:
            continue

        xy = line[:2]
        path = line[3:]
        idx_status = xy[0]
        wt_status = xy[1]

        if xy == "??":
            issues.append(f"{path} is untracked")
            continue

        if wt_status != " ":
            if idx_status != " ":
                issues.append(
                    f"{path} has mixed staged+unstaged changes (status '{xy}')"
                )
            else:
                issues.append(f"{path} has unstaged changes (status '{xy}')")

    return issues


def _version_pair_issue(repo_dir: str) -> str:
    fw_path = Path(repo_dir) / "data" / "fw-version.json"
    fs_path = Path(repo_dir) / "data" / "fs-version.json"

    if not fw_path.exists() or not fs_path.exists():
        return "data/fw-version.json and data/fs-version.json must both exist"

    try:
        fw_doc = json.loads(fw_path.read_text())
        fs_doc = json.loads(fs_path.read_text())
    except Exception:
        return "version metadata JSON parse failed (fw-version.json or fs-version.json)"

    fw = str(fw_doc.get("firmwareVersion", "")).strip()
    fs = str(fs_doc.get("fsVersion", "")).strip()
    expected_fs = f"fs-{fw}" if fw else ""

    if not fw:
        return "data/fw-version.json missing firmwareVersion"
    if not fs:
        return "data/fs-version.json missing fsVersion"
    if fs != expected_fs:
        return (
            "version mismatch: expected data/fs-version.json fsVersion='"
            + expected_fs
            + "' to match data/fw-version.json firmwareVersion='"
            + fw
            + "'"
        )

    return ""


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("tool_name") != "Bash":
        return 0

    cmd = str(data.get("tool_input", {}).get("command", "")).strip()
    if not COMMIT_CMD_PATTERN.search(cmd):
        return 0

    # Co-author check scans full cmd including heredoc body.
    if COAUTHOR_LINE_PATTERN.search(cmd) or COAUTHOR_TRAILER_PATTERN.search(cmd):
        _deny(
            "Commit blocked: co-author trailers are not allowed in this repository. "
            "Remove any 'Co-authored-by:' lines or --trailer co-authored-by entries."
        )
        return 0

    # Validate message format before anything else so format errors surface
    # regardless of chaining or version state.
    message = _extract_message(cmd)
    if message is None:
        _deny(
            "Commit blocked: could not parse commit message. "
            "Use a literal quoted -m/--message argument, for example: "
            'git commit -m "type(phase:vX.Y.Z/TNN): summary"'
        )
        return 0

    if not (PHASE_PATTERN.match(message) or LEGACY_SCOPE_PATTERN.match(message)):
        _deny(
            "Commit blocked: invalid commit message format. Expected one of: "
            "type(phase:vX.Y.Z/TNN): summary, "
            "type(phase:vX.Y.Z/TNN/slice:name): summary, "
            "or type(scope): summary (scope from CONTRIBUTING). "
            "Allowed types: feat|fix|docs|refactor|chore|test|style|perf."
        )
        return 0

    # Chained add+commit: staged state is untrustworthy before execution.
    if _is_chained_add_then_commit(cmd):
        _deny(
            "Commit blocked: cannot validate staged version metadata in a chained command "
            "(e.g. 'git add ... && git commit ...'). "
            "Run staging and commit as separate commands: first "
            "'git add data/*version*.json', then 'git commit -m \"...\"'."
        )
        return 0

    # Version checks only apply when version files are actually being staged.
    repo_dir = os.environ.get("CLAUDE_PROJECT_DIR", os.getcwd())
    if _version_files_staged(repo_dir):
        problems: list[str] = []
        version_issues = _version_file_issues(repo_dir)
        if version_issues:
            problems.append(
                "version metadata file(s) are not fully staged: "
                + "; ".join(version_issues)
                + ". Stage them explicitly with 'git add data/*version*.json' (or revert)."
            )
        pair_issue = _version_pair_issue(repo_dir)
        if pair_issue:
            problems.append(
                "version metadata consistency check failed: "
                + pair_issue
                + ". Run the build/version generation step and stage both data version files."
            )
        if problems:
            _deny("Commit blocked: " + " | ".join(problems))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
