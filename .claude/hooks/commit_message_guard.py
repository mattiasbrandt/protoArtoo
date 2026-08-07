#!/usr/bin/env python3
"""PreToolUse hook: enforce commit message format for git commit commands."""

import json
import re
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
COMMIT_CMD_PATTERN = re.compile(r"(^|\s)git" + _GIT_GLOBAL_FLAGS + r"\s+commit(\s|$)")
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
