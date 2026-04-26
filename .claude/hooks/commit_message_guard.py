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
    rf"^(?:{ALLOWED_TYPES})!?\(phase:v\d+\.\d+\.\d+/T\d{{2}}(?:/slice:[a-z0-9-]+)?\): .+"
)
LEGACY_SCOPE_PATTERN = re.compile(
    rf"^(?:{ALLOWED_TYPES})!?\((?:{ALLOWED_SCOPES})\): .+"
)
COMMIT_CMD_PATTERN = re.compile(r"(^|\s)git\s+commit(\s|$)")
MESSAGE_ARG_PATTERN = re.compile(r"(?:^|\s)-m\s+([\"'])(.*?)\1")
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

        # Untracked version metadata file.
        if xy == "??":
            issues.append(f"{path} is untracked")
            continue

        # Any non-space worktree status means unstaged/mixed state.
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

    if COAUTHOR_LINE_PATTERN.search(cmd) or COAUTHOR_TRAILER_PATTERN.search(cmd):
        _deny(
            "Commit blocked: co-author trailers are not allowed in this repository. "
            "Remove any 'Co-authored-by:' lines or --trailer co-authored-by entries."
        )
        return 0

    repo_dir = os.environ.get("CLAUDE_PROJECT_DIR", os.getcwd())
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

    match = MESSAGE_ARG_PATTERN.search(cmd)
    if not match:
        problems.append(
            "commit message parse failed. Use a literal quoted -m argument, for example: "
            "git commit -m \"type(phase:vX.Y.Z/TNN): summary\""
        )
    else:
        message = match.group(2).strip()
        if not (PHASE_PATTERN.match(message) or LEGACY_SCOPE_PATTERN.match(message)):
            problems.append(
                "invalid commit scope/message. Expected one of: "
                "type(phase:vX.Y.Z/TNN): summary, "
                "type(phase:vX.Y.Z/TNN/slice:name): summary, "
                "or type(scope): summary (scope from CONTRIBUTING). "
                "Allowed types: feat|fix|docs|refactor|chore|test|style|perf."
            )

    if problems:
        _deny("Commit blocked: " + " | ".join(problems))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
