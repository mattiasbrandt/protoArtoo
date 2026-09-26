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
2. every Output row Setting has an entry under its row key;
3. every audio Setting has an entry under the name its door takes it under
   (`scream`, `snd_int_quiet`, `volume`), in `SETTING_WORDS` beside the droid's,
   and so does each CHIRP catalog binding part (`bank`, `page`, `index`);
4. every field of every Record (the Droid Build, guided Setup's record) has an
   entry under its form name whose `path` is the field's GET path, and every
   field an act takes (move a Part, capture an end, reverse an Output) has an
   entry under its form name (ADR 0068, second amendment). A Record is not a
   Setting and an act stores nothing, but each can be refused, and a refusal
   of either is worded from the same table;
5. every entry for a Setting or a Record field states when it takes effect,
   `applies: "immediate" | "at-reboot" | "restart-required"`, and it is the
   token the firmware declaration states (include/apply_timing.h). The page
   reads the timing from the entry and the Commit Step is what decides it, so
   the two are held equal here. An act stores nothing and states none;
6. no page script in data/ names a Component Toggle's label or a sound track's
   label beside the field it is the label of - its form name, its component
   key or its track key, quoted on the same line. That is the shape a second
   label table takes (`["domeEsc", "Dome ESC"]`, `{ label: "Scream", key:
   "scream" }`), and the entry's `label` is the one home. The same word naming
   something else - the Sound page, a sequence called Scream - is not about the
   Setting and is not flagged; nor is a component key that is a plain word
   (`drive`, `audio`), which names pages and lanes too, so for those toggles the
   form name is what marks a line as about the Setting.

And, because the words check is the one place every declaration is read and
the Preferences double in the native tests enforces neither: no NVS key is
declared twice, and none runs past the 15 characters an ESP-IDF key may have.
A longer key is refused by NVS on a droid and nowhere else.

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
# ESP-IDF NVS keys are at most 15 characters (NVS_KEY_NAME_MAX_SIZE - 1).
NVS_KEY_MAX_LEN = 15


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


def _timing(errors: list[str], name: str, entry: str, declared: str) -> None:
    """The entry states the firmware's timing token, or an error says which."""
    applies = re.search(r'applies:\s*"([\w-]+)"', entry)
    if applies is None:
        errors.append(f"{name}'s words do not say when it takes effect; the firmware declares {declared}")
    elif applies.group(1) != declared:
        errors.append(f"{name}'s words say it takes effect {applies.group(1)}, but the firmware "
                      f"declares {declared}")


def _labels_named_twice(errors: list[str], droid: dict[str, str], settings: Path | None,
                        pages: list[Path]) -> None:
    """Rule 6: a toggle's or a track's label beside that field's own quoted name."""
    named: dict[str, list[str]] = {}
    for setting in setting_declarations.droid_settings(settings):
        if setting.form.startswith("enable") and setting.path and setting.path.startswith("components."):
            # The component key identifies the toggle only where it is not a
            # plain word: "drive" and "audio" also name a page, a lane and a
            # step type, so for those the form name has to be on the line.
            key = setting.path.split(".")[1]
            named[setting.form] = [setting.form] + ([key] if key != key.lower() else [])
    for setting in setting_declarations.audio_settings(settings):
        if setting.is_track:
            named[setting.name] = [setting.name]
    for name, identifiers in named.items():
        label = re.search(r'label:\s*"([^"]+)"', droid.get(name, ""))
        if label is None:
            continue
        quoted = [rf'["\'`]{re.escape(ident)}["\'`]' for ident in identifiers]
        literal = re.compile(rf'["\'`]{re.escape(label.group(1))}["\'`]')
        for page in pages:
            for number, line in enumerate(page.read_text(encoding="utf-8").splitlines(), 1):
                if literal.search(line) and any(re.search(q, line) for q in quoted):
                    errors.append(f"{page.name}:{number} names {name}'s label {label.group(1)!r} itself; "
                                  f"it is read from the entry ({WEB_API.name})")


def check(errors: list[str], settings: Path | None = None, web_api: Path | None = None,
          records: list[Path] | None = None, acts: Path | None = None,
          pages: list[Path] | None = None) -> None:
    droid, row = browser_words(web_api)
    if pages is None:
        pages = [page for page in sorted(WEB_API.parent.glob("*.js")) if page.name != WEB_API.name]
    _labels_named_twice(errors, droid, settings, pages)
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
        _timing(errors, setting.form, entry, setting.timing)
    audio = setting_declarations.audio_settings(settings)
    keys = [(d.form, d.nvs_key) for d in setting_declarations.droid_settings(settings)]
    keys += [(a.name, a.nvs_key) for a in audio]
    seen: dict[str, str] = {}
    for name, key in keys:
        if len(key) > NVS_KEY_MAX_LEN:
            errors.append(f"{name}'s NVS key {key!r} is {len(key)} characters; "
                          f"NVS keys are at most {NVS_KEY_MAX_LEN}")
        if key in seen:
            errors.append(f"{name} and {seen[key]} both declare the NVS key {key!r}")
        seen.setdefault(key, name)
    for setting in audio:
        if setting.name not in droid:
            errors.append(
                f"{setting.name} is a declared audio Setting with no words in SETTING_WORDS "
                f"({WEB_API.name}) - a refusal of it would reach the Sound page as its wire name"
            )
            continue
        _timing(errors, setting.name, droid[setting.name], setting.timing)
    for name in setting_declarations.catalog_binding_settings(settings):
        if name not in droid:
            errors.append(
                f"{name} is a declared CHIRP binding part with no words in SETTING_WORDS "
                f"({WEB_API.name})"
            )
    for setting in setting_declarations.row_settings(settings):
        if setting.key not in row:
            errors.append(
                f"{setting.key} is a declared Output row Setting with no words in "
                f"ROW_SETTING_WORDS ({WEB_API.name})"
            )
            continue
        _timing(errors, setting.key, row[setting.key], setting.timing)
    for field in setting_declarations.record_fields(records):
        entry = droid.get(field.form)
        if entry is None:
            errors.append(
                f"{field.form} is a declared Record field ({field.record}) with no words in "
                f"SETTING_WORDS ({WEB_API.name}) - a refusal of it would reach the page as its "
                "wire name"
            )
            continue
        path = re.search(r'path:\s*"([\w.]+)"', entry)
        if path is None or path.group(1) != field.path:
            errors.append(
                f"{field.form}'s words name its GET path as "
                f"{path.group(1) if path else 'nothing'}, but the firmware reads it at {field.path}"
            )
        _timing(errors, field.form, entry, field.timing)
    for form in setting_declarations.act_fields(acts):
        if form not in droid:
            errors.append(
                f"{form} is a declared act field with no words in SETTING_WORDS "
                f"({WEB_API.name}) - a refusal of it would reach the page as its wire name"
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
          f"Settings, {len(setting_declarations.audio_settings())} audio Settings, "
          f"{len(setting_declarations.row_settings())} Output row Settings, "
          f"{len(setting_declarations.record_fields())} Record fields, "
          f"{len(setting_declarations.act_fields())} act fields).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
