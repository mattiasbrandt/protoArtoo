#!/usr/bin/env python3
"""UserPromptSubmit hook: remind Claude what repo-level "wrap up" means."""

import json
import re
import sys


WRAP_UP_RE = re.compile(
    r"\b(wrap\s*(?:-| )?up|wrap\s+this\s+up|close\s+this\s+out|finish\s+the\s+session)\b",
    re.IGNORECASE,
)

RESUME_HANDOFF_RE = re.compile(
    r"\b(for\s+the\s+night|for\s+today|it'?s\s+late|i\s+(?:need|have)\s+to\s+(?:leave|go|stop)|"
    r"can'?t\s+continue|pick\s+(?:it\s+)?up\s+tomorrow|resume\s+tomorrow)\b",
    re.IGNORECASE,
)

CONTEXT_HANDOFF_RE = re.compile(
    r"\b(context\s+(?:window|limit|full|pressure|is\s+getting\s+full|is\s+almost\s+full)|"
    r"running\s+out\s+of\s+context|"
    r"compact(?:ion)?|fresh\s+(?:agent|session)|next\s+session\s+prompt|handoff\s+skill)\b",
    re.IGNORECASE,
)


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("hook_event_name") != "UserPromptSubmit":
        return 0

    prompt = str(data.get("prompt") or "")
    if not WRAP_UP_RE.search(prompt):
        return 0

    resume_note = ""
    if RESUME_HANDOFF_RE.search(prompt):
        resume_note = (
            " User context indicates they cannot continue now; include a resumable "
            "handoff with exact next command/file, blockers, and what remains unverified."
        )
    if CONTEXT_HANDOFF_RE.search(prompt):
        resume_note += (
            " Context-window or mid-implementation handoff cue detected; suggest or invoke "
            "the community `handoff` skill after normal bookkeeping when volatile in-progress "
            "context needs a temporary next-session prompt, and report the handoff path."
        )

    sys.stderr.write(
        "protoArtoo wrap-up trigger detected. Apply AGENTS.md Wrap-up trigger: "
        "inspect repo state, update the active issue or public status docs as needed, "
        "file significant outcomes in MemPalace, commit verified slices when appropriate, "
        "and report the approved verification label plus remaining blockers. Make the next "
        "session restartable from a durable source: GitHub issue/docs, MemPalace status/search, "
        "or a temporary handoff document."
        f"{resume_note}\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
