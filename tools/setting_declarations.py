#!/usr/bin/env python3
"""The Settings src/config_settings.cpp declares, read out of its three tables.

Each Setting is declared once in the firmware (ADR 0068, amended 2026-09-26):
a droid Setting in `kConfigSettings[]`, an audio Setting in `kAudioSettings[]`
and an Output row Setting in `kOutputRowSettings[]`. The checks that must agree
with that one home -
tools/check_setting_words.py (the browser has words for every Setting) and
tools/check_component_registry_drift.py (a family's member key is a Setting's
NVS key) - read it through here rather than each keeping a pattern of its own.

This reads the source text, the way the other drift checks here do; it never
builds the firmware.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG_SETTINGS = ROOT / "src" / "config_settings.cpp"

# A droid Setting opens as `PA_RANGE("form", "path", "key", ...` (or PA_BOOL,
# PA_WORDS, PA_MEMBER), or as a brace entry `{"form", "path", "key", ...` for a
# rule the helpers do not spell. The path may be nullptr: an Output's wired
# tick, carried on its row.
_DROID = re.compile(
    r'(?:PA_(?P<macro>RANGE|BOOL|WORDS|MEMBER)\(|\{)\s*"(?P<form>\w+)",\s*'
    r'(?P<path>nullptr|"[\w.]+"),\s*"(?P<key>\w+)",'
)
_BRACE_RULE = re.compile(r"SettingRule::(\w+)")
# An audio Setting opens as one of the audio helpers, `PA_TRACK("scream",
# "snd_scream", ...`: the name its door takes it under, then its NVS key.
# PA_INTERVAL's interval is stored under its own name, so it has no second
# string.
_AUDIO = re.compile(r'PA_[A-Z_]+\(\s*"(?P<name>\w+)"(?:\s*,\s*"(?P<key>\w+)")?')
# An Output row Setting opens as `{"key", RowSettingStore::...`.
_ROW = re.compile(r'\{\s*"(?P<key>\w+)",\s*RowSettingStore::(?P<store>\w+)')


@dataclass(frozen=True)
class DroidSetting:
    form: str
    path: str | None
    nvs_key: str
    rule: str


@dataclass(frozen=True)
class RowSetting:
    key: str
    store: str


def _table(text: str, name: str) -> str:
    """The body of the array called `name`, from its `{` to the `};` closing it."""
    start = text.find(f"{name}[] = {{")
    if start < 0:
        raise ValueError(f"{name}[] is not declared in {CONFIG_SETTINGS.name}")
    end = text.find("\n};", start)
    if end < 0:
        raise ValueError(f"{name}[] is not closed in {CONFIG_SETTINGS.name}")
    return text[text.index("{", start) + 1:end]


def droid_settings(source: Path | None = None) -> list[DroidSetting]:
    text = (source or CONFIG_SETTINGS).read_text(encoding="utf-8")
    body = _table(text, "kConfigSettings")
    found = []
    matches = list(_DROID.finditer(body))
    for index, match in enumerate(matches):
        macro = match.group("macro")
        if macro:
            rule = macro.capitalize()
        else:
            tail_end = matches[index + 1].start() if index + 1 < len(matches) else len(body)
            rule_match = _BRACE_RULE.search(body, match.end(), tail_end)
            rule = rule_match.group(1) if rule_match else "UNKNOWN"
        path = match.group("path")
        found.append(DroidSetting(
            form=match.group("form"),
            path=None if path == "nullptr" else path.strip('"'),
            nvs_key=match.group("key"),
            rule=rule,
        ))
    return found


def row_settings(source: Path | None = None) -> list[RowSetting]:
    text = (source or CONFIG_SETTINGS).read_text(encoding="utf-8")
    body = _table(text, "kOutputRowSettings")
    return [RowSetting(key=m.group("key"), store=m.group("store")) for m in _ROW.finditer(body)]


@dataclass(frozen=True)
class AudioSetting:
    name: str
    nvs_key: str


def audio_settings(source: Path | None = None) -> list[AudioSetting]:
    """Each audio Setting's name and NVS key, in table order."""
    text = (source or CONFIG_SETTINGS).read_text(encoding="utf-8")
    body = _table(text, "kAudioSettings")
    return [AudioSetting(name=m.group("name"), nvs_key=m.group("key") or m.group("name"))
            for m in _AUDIO.finditer(body)]
