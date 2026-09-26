#!/usr/bin/env python3
"""Fail when a Setting the firmware declares has no words in the browser.

Each Setting is declared once, in src/config_settings.cpp (ADR 0068, amended
2026-09-26), and a refusal about it reaches a page only through the one words
table in data/web_api.js: `SETTING_WORDS` for the droid's Settings, keyed by
form name, and `ROW_SETTING_WORDS` for an Output's, keyed by row key. A
Setting with no entry there would reach the builder as its wire name, which
ADR 0059 forbids - so this fails instead:

1. every droid Setting GET reads has an entry under its form name, and that
   entry's `path` is the Setting's GET path (a restore's refusal can be named
   by either);
2. every Output row Setting has an entry under its row key.

An Output's wired tick has no GET path of its own - it travels on the row as
`wired` - so it is covered by rule 2, not 1.

Reports, never rewrites. Run it as `make check-setting-words`; the slice gate
runs it inside `tools/check_action_registry_drift.py`. Its unit tests are
test/test_tools/test_setting_words.py.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

import setting_declarations  # tools/, beside this script

ROOT = Path(__file__).resolve().parent.parent
WEB_API = ROOT / "data" / "web_api.js"


def _object_body(text: str, name: str) -> str:
    """The body of `const NAME = Object.freeze({ ... });`, braces balanced."""
    match = re.search(rf"const {name} = Object\.freeze\(\{{", text)
    if match is None:
        raise ValueError(f"{name} is not declared in {WEB_API.name}")
    depth = 1
    at = match.end()
    while depth and at < len(text):
        depth += {"{": 1, "}": -1}.get(text[at], 0)
        at += 1
    return text[match.end():at - 1]


def _entries(body: str) -> dict[str, str]:
    """Top-level `key: { ... }` entries of an object body, key -> entry text."""
    entries: dict[str, str] = {}
    depth = 0
    at = 0
    while at < len(body):
        if depth == 0:
            key = re.match(r'\s*(?://[^\n]*\n\s*)*"?([\w-]+)"?\s*:\s*\{', body[at:])
            if key:
                start = at + key.end()
                depth = 1
                end = start
                while depth and end < len(body):
                    depth += {"{": 1, "}": -1}.get(body[end], 0)
                    end += 1
                entries[key.group(1)] = body[start:end - 1]
                at = end
                depth = 0
                continue
        at += 1
    return entries


def browser_words(web_api: Path | None = None) -> tuple[dict[str, str], dict[str, str]]:
    text = (web_api or WEB_API).read_text(encoding="utf-8")
    return (_entries(_object_body(text, "SETTING_WORDS")),
            _entries(_object_body(text, "ROW_SETTING_WORDS")))


def check(errors: list[str], settings: Path | None = None, web_api: Path | None = None) -> None:
    droid, row = browser_words(web_api)
    for setting in setting_declarations.droid_settings(settings):
        if setting.path is None:
            continue
        entry = droid.get(setting.form)
        if entry is None:
            errors.append(
                f"{setting.form} is a declared Setting with no words in SETTING_WORDS "
                f"({WEB_API.name}) - a refusal of it would reach the page as its wire name"
            )
            continue
        path = re.search(r'path:\s*"([\w.]+)"', entry)
        if path is None or path.group(1) != setting.path:
            errors.append(
                f"{setting.form}'s words name its GET path as "
                f"{path.group(1) if path else 'nothing'}, but the firmware reads it at {setting.path}"
            )
    for setting in setting_declarations.row_settings(settings):
        if setting.key not in row:
            errors.append(
                f"{setting.key} is a declared Output row Setting with no words in "
                f"ROW_SETTING_WORDS ({WEB_API.name})"
            )


def main() -> int:
    errors: list[str] = []
    check(errors)
    if errors:
        print("Setting words drift detected:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"Setting words check passed ({len(setting_declarations.droid_settings())} droid "
          f"Settings, {len(setting_declarations.row_settings())} Output row Settings).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
