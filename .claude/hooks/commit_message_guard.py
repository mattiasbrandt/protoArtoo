#!/usr/bin/env python3
"""PreToolUse hook: enforce commit message format for git commit commands.

The co-author ban applies to every commit. The type(scope) format is this
project's convention, so it applies only when the commit lands in this
project's repository (any of its worktrees). A session started here can
commit to another repo (dotfiles, say) with that repo's own convention.
When the target repo cannot be resolved, the format rule still applies.
"""

import json
import os
import re
import shlex
import subprocess
import sys

ALLOWED_TYPES = "feat|fix|docs|refactor|chore|test|style|perf"
ALLOWED_SCOPES = "drive|sbus|failsafe|dome|audio|servo|web|nvs|wifi|hw|plan|test|ci"
# The live convention per AGENTS.md "Commit scope format": plain
# type(scope): summary. The phase-era type(phase:vX.Y.Z/TNN) token is
# history only and no longer accepted.
SCOPE_PATTERN = re.compile(
    rf"^(?:{ALLOWED_TYPES})!?\((?:{ALLOWED_SCOPES})\): .+",
    re.DOTALL,
)
# Match git global flags that may appear before the subcommand:
#   short flags with optional value:  -C /path  -c key=val
#   long flags with optional =value:  --git-dir=/path  --work-tree=/path
_GIT_GLOBAL_FLAGS = r"(?:\s+(?:-\w+(?:\s+\S+)?|--[\w-]+(?:=\S+)?))*"
COMMIT_CMD_PATTERN = re.compile(
    r"(^|\s)git(?P<flags>" + _GIT_GLOBAL_FLAGS + r")\s+commit(\s|$)"
)
# A `cd <dir>` earlier in the same command, e.g. `cd ../repo && git commit`.
CD_PATTERN = re.compile(r"""(?:^|[;&|(]\s*)cd\s+("[^"]*"|'[^']*'|[^\s;&|)]+)""")
# Git global flags that change which repository the commit lands in.
_REPO_FLAGS = ("-C", "--git-dir", "--work-tree")
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


def _deny(reason: str) -> None:
    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }
    print(json.dumps(payload))


def _extract_message(cmd: str) -> str | None:
    """Return the commit message string, or None if it cannot be parsed."""
    m = HEREDOC_MSG_PATTERN.search(cmd)
    if m:
        return m.group(2).strip()
    m = MESSAGE_ARG_PATTERN.search(cmd)
    if m:
        return m.group(2).strip()
    return None


def _common_dir(git_args: list[str], cwd: str) -> str | None:
    """Return the repo's absolute git common dir (shared by all worktrees)."""
    try:
        r = subprocess.run(
            ["git", *git_args, "rev-parse", "--path-format=absolute", "--git-common-dir"],
            cwd=cwd, capture_output=True, text=True, timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if r.returncode != 0 or not r.stdout.strip():
        return None
    return os.path.realpath(r.stdout.strip())


def _expand(path: str, base: str) -> str | None:
    """Expand ~ and $VARS; None when a variable is left unresolved."""
    path = os.path.expandvars(os.path.expanduser(path))
    if "$" in path:
        return None
    return os.path.normpath(os.path.join(base, path))


def _targets_other_repo(cmd: str, commit: re.Match, session_cwd: str) -> bool:
    """True only when the commit provably lands outside this project's repo."""
    project_dir = os.environ.get("CLAUDE_PROJECT_DIR")
    if not project_dir:
        return False
    project = _common_dir([], project_dir)
    if project is None:
        return False

    cwd = session_cwd
    for m in CD_PATTERN.finditer(cmd[: commit.start()]):
        cwd = _expand(m.group(1).strip("\"'"), cwd)
        if cwd is None:
            return False

    try:
        flags = shlex.split(commit.group("flags"))
    except ValueError:
        return False
    git_args: list[str] = []
    for i, flag in enumerate(flags):
        name, _, value = flag.partition("=")
        if name not in _REPO_FLAGS:
            continue
        if not value:
            if i + 1 >= len(flags):
                return False
            value = flags[i + 1]
        # git resolves each value itself, relative to the directory it is in.
        value = os.path.expandvars(os.path.expanduser(value))
        if "$" in value:
            return False
        git_args += [name, value] if name == "-C" else [f"{name}={value}"]

    target = _common_dir(git_args, cwd)
    return target is not None and target != project


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("tool_name") != "Bash":
        return 0

    cmd = str(data.get("tool_input", {}).get("command", "")).strip()
    commit = COMMIT_CMD_PATTERN.search(cmd)
    if not commit:
        return 0

    # Co-author check scans full cmd including heredoc body.
    if COAUTHOR_LINE_PATTERN.search(cmd) or COAUTHOR_TRAILER_PATTERN.search(cmd):
        _deny(
            "Commit blocked: co-author trailers are not allowed in any commit. "
            "Remove any 'Co-authored-by:' lines or --trailer co-authored-by entries."
        )
        return 0

    # The format below is this project's convention; another repo keeps its own.
    if _targets_other_repo(cmd, commit, str(data.get("cwd") or os.getcwd())):
        return 0

    message = _extract_message(cmd)
    if message is None:
        _deny(
            "Commit blocked: could not parse commit message. "
            "Use a literal quoted -m/--message argument, for example: "
            'git commit -m "type(scope): summary"'
        )
        return 0

    if not SCOPE_PATTERN.match(message):
        _deny(
            "Commit blocked: invalid commit message format. Expected "
            "type(scope): summary (scope from CONTRIBUTING). "
            "Allowed types: feat|fix|docs|refactor|chore|test|style|perf."
        )
        return 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
