#!/usr/bin/env python3
"""PreToolUse hook: enforce phase commit message format for git commit commands."""

import json
import re
import sys

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


def _deny(reason: str) -> None:
    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }
    print(json.dumps(payload))


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

    match = MESSAGE_ARG_PATTERN.search(cmd)
    if not match:
        _deny(
            "Commit blocked: use non-interactive git commit with -m and required format: "
            "type(phase:vX.Y.Z/TNN): summary"
        )
        return 0

    message = match.group(2).strip()
    if PHASE_PATTERN.match(message) or LEGACY_SCOPE_PATTERN.match(message):
        return 0

    _deny(
        "Commit blocked: invalid commit scope/message. "
        "Expected one of: type(phase:vX.Y.Z/TNN): summary, "
        "type(phase:vX.Y.Z/TNN/slice:name): summary, "
        "or type(scope): summary (scope from CONTRIBUTING). "
        "Allowed types: feat|fix|docs|refactor|chore|test|style|perf."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
