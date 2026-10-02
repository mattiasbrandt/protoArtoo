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
# -m, --message, and a short cluster whose last letter is m (-qam, -am).
# The cluster must end in m: -q is quiet and carries no message.
MESSAGE_ARG_PATTERN = re.compile(
    r"""(?:^|\s)(?:--message(?:=|\s+)|-[A-Za-z]*m(?:=|\s+))([\"'])(.*?)\1""",
    re.DOTALL,
)
# Handles heredoc-style: -m "$(cat <<'EOF'\n...\nEOF\n)" and -qam the same way.
HEREDOC_MSG_PATTERN = re.compile(
    r"""(?:^|\s)(?:--message(?:=|\s+)|-[A-Za-z]*m(?:=|\s+))"?\$\(cat\s+<<'?(\w+)'?[ \t]*\n(.*?)\n[ \t]*\1[ \t]*\n[ \t]*\)""",
    re.DOTALL,
)
NO_EDIT_PATTERN = re.compile(r"(?:^|\s)--no-edit(?:\s|$)")
# -F/--file is never a message this hook can read. It stays rejected, including
# when combined with --no-edit during a merge.
FILE_ARG_PATTERN = re.compile(r"(?:^|\s)(?:--file(?:=|\s)|-F(?:\s|$))")
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


def _absolute_git_path(git_args: list[str], cwd: str, name: str) -> str | None:
    """An absolute path inside the repo selected by git_args, not the session cwd."""
    try:
        r = subprocess.run(
            ["git", *git_args, "rev-parse", "--path-format=absolute", "--git-path", name],
            cwd=cwd, capture_output=True, text=True, timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if r.returncode != 0 or not r.stdout.strip():
        return None
    path = r.stdout.strip()
    if not os.path.isabs(path):
        return None
    return path


def _merge_in_progress(git_args: list[str], cwd: str) -> bool:
    """True when this worktree has MERGE_HEAD. A merge's own message is then the commit."""
    path = _absolute_git_path(git_args, cwd, "MERGE_HEAD")
    return path is not None and os.path.isfile(path)


def _merge_msg_has_coauthor(git_args: list[str], cwd: str) -> bool:
    """True when MERGE_MSG carries a co-author trailer, or cannot be read."""
    path = _absolute_git_path(git_args, cwd, "MERGE_MSG")
    if path is None or not os.path.isfile(path):
        return True
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return True
    return COAUTHOR_LINE_PATTERN.search(text) is not None


def _git_args_and_cwd(cmd: str, commit: re.Match, session_cwd: str) -> tuple[list[str], str] | None:
    """The commit's repo-selecting git args and the directory it runs in."""
    cwd = session_cwd
    for m in CD_PATTERN.finditer(cmd[: commit.start()]):
        cwd = _expand(m.group(1).strip("\"'"), cwd)
        if cwd is None:
            return None
    try:
        flags = shlex.split(commit.group("flags"))
    except ValueError:
        return None
    git_args: list[str] = []
    for i, flag in enumerate(flags):
        name, _, value = flag.partition("=")
        if name not in _REPO_FLAGS:
            continue
        if not value:
            if i + 1 >= len(flags):
                return None
            value = flags[i + 1]
        value = os.path.expandvars(os.path.expanduser(value))
        if "$" in value:
            return None
        git_args += [name, value] if name == "-C" else [f"{name}={value}"]
    return git_args, cwd


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

    located = _git_args_and_cwd(cmd, commit, session_cwd)
    if located is None:
        return False
    git_args, cwd = located
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

    if FILE_ARG_PATTERN.search(cmd):
        _deny(
            "Commit blocked: -F/--file is rejected. "
            "Pass a quoted -m/--message (a short cluster ending in m, such as -qam, counts)."
        )
        return 0

    located = _git_args_and_cwd(cmd, commit, str(data.get("cwd") or os.getcwd()))
    merging = located is not None and _merge_in_progress(*located)
    if NO_EDIT_PATTERN.search(cmd) and not merging:
        _deny(
            "Commit blocked: --no-edit is accepted only when MERGE_HEAD exists "
            "(a merge), including when -m is also present. "
            "Otherwise pass a quoted -m/--message, including -qam."
        )
        return 0

    message = _extract_message(cmd)
    if message is None and NO_EDIT_PATTERN.search(cmd) and merging:
        if _merge_msg_has_coauthor(*located):
            _deny(
                "Commit blocked: co-author trailers are not allowed in any commit. "
                "Remove any 'Co-authored-by:' lines from the merge message."
            )
            return 0
        return 0
    if message is None:
        _deny(
            "Commit blocked: could not parse commit message. "
            "Use a literal quoted -m/--message argument (a short cluster ending in m, such as -qam, counts), for example: "
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
