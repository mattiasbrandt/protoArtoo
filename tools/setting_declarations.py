#!/usr/bin/env python3
"""The Settings src/config_settings.cpp declares, read out of its tables.

Each Setting is declared once in the firmware (ADR 0068, amended 2026-09-26):
a droid Setting in `kConfigSettings[]`, an audio Setting in `kAudioSettings[]`,
a CHIRP catalog binding part in `kCatalogBindingSettings[]` and an Output row
Setting in `kOutputRowSettings[]`. A Record's fields are declared in its own
module's `kFields[]` (the Records are listed in include/config_records.inc), and
an act's fields in `kActFields[]` in src/web/api_config_apply.cpp (ADR 0068,
second amendment). The checks that must agree
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
    r'(?P<path>nullptr|"[\w.]+"),\s*"(?P<key>\w+)",\s*(?:ApplyTiming::)?(?P<timing>\w+),'
)
_BRACE_RULE = re.compile(r"SettingRule::(\w+)")
# An audio Setting opens as one of the audio helpers, `PA_TRACK("scream",
# "snd_scream", ...`: the name its door takes it under, then its NVS key.
# PA_INTERVAL's interval is stored under its own name, so it has no second
# string.
_AUDIO = re.compile(
    r'PA_(?P<kind>[A-Z_]+)\(\s*"(?P<name>\w+)"(?:\s*,\s*"(?P<key>\w+)")?\s*,\s*(?P<timing>\w+),')
# An Output row Setting opens as `{"key", RowSettingStore::...`.
_ROW = re.compile(
    r'\{\s*"(?P<key>\w+)",\s*ApplyTiming::(?P<timing>\w+),\s*RowSettingStore::(?P<store>\w+)')

# A declaration's timing token (include/apply_timing.h), in the browser's
# spelling (data/apply_timing.js).
TIMING_TOKENS = {
    "Immediate": "immediate",
    "AtReboot": "at-reboot",
    "RestartRequired": "restart-required",
}


def _timing(token: str, name: str) -> str:
    """A declaration's timing in the browser's spelling, or a ValueError naming
    the declaration that states none this reader knows."""
    if token not in TIMING_TOKENS:
        raise ValueError(f"{name} declares no timing this reader knows (got {token!r}); "
                         f"one of {', '.join(TIMING_TOKENS)} (include/apply_timing.h)")
    return TIMING_TOKENS[token]


@dataclass(frozen=True)
class DroidSetting:
    form: str
    path: str | None
    nvs_key: str
    rule: str
    timing: str


@dataclass(frozen=True)
class RowSetting:
    key: str
    store: str
    timing: str


def _table(text: str, name: str) -> str:
    """The body of the array called `name`, from its `{` to the `};` closing it.

    The array may be sized (`kFields[FieldCount] = {`) or not (`kFields[] = {`).
    """
    match = re.search(rf"\b{name}\[\w*\] = \{{", text)
    start = match.start() if match else -1
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
            timing=_timing(match.group("timing"), match.group("form")),
        ))
    return found


def row_settings(source: Path | None = None) -> list[RowSetting]:
    text = (source or CONFIG_SETTINGS).read_text(encoding="utf-8")
    body = _table(text, "kOutputRowSettings")
    return [RowSetting(key=m.group("key"), store=m.group("store"), timing=_timing(m.group("timing"), m.group("key")))
            for m in _ROW.finditer(body)]


@dataclass(frozen=True)
class AudioSetting:
    name: str
    nvs_key: str
    timing: str
    kind: str  # the declaring helper: TRACK, OPTIONAL_TRACK, INTERVAL, CATEGORY_BOUND, MOOD_MASK, AUDIO_AT

    @property
    def is_track(self) -> bool:
        return self.kind in ("TRACK", "OPTIONAL_TRACK")


def audio_settings(source: Path | None = None) -> list[AudioSetting]:
    """Each audio Setting's name and NVS key, in table order."""
    text = (source or CONFIG_SETTINGS).read_text(encoding="utf-8")
    body = _table(text, "kAudioSettings")
    return [AudioSetting(name=m.group("name"), nvs_key=m.group("key") or m.group("name"),
                         timing=_timing(m.group("timing"), m.group("name")), kind=m.group("kind"))
            for m in _AUDIO.finditer(body)]


# A CHIRP catalog binding part opens as a brace entry `{"bank", nullptr, nullptr, ApplyTiming::T,`.
_BINDING = re.compile(r'\{\s*"(?P<name>\w+)",\s*nullptr,\s*nullptr,')


def catalog_binding_settings(source: Path | None = None) -> list[str]:
    """The name of each CHIRP catalog binding part (`bank`, `page`, `index`)."""
    text = (source or CONFIG_SETTINGS).read_text(encoding="utf-8")
    body = _table(text, "kCatalogBindingSettings")
    return [m.group("name") for m in _BINDING.finditer(body)]


CONFIG_RECORDS = ROOT / "include" / "config_records.inc"
CONFIG_APPLY = ROOT / "src" / "web" / "api_config_apply.cpp"

# A Record field is `{"form", "path", ApplyTiming::T, "example"}` in its module's kFields[].
_RECORD_FIELD = re.compile(
    r'\{\s*"(?P<form>\w+)",\s*"(?P<path>[\w.]+)",\s*ApplyTiming::(?P<timing>\w+),\s*"[^"]*"\s*\}')
# A Record is `PA_CONFIG_RECORD(Name, Value, "key")` in the list.
_RECORD = re.compile(r'^PA_CONFIG_RECORD\((?P<name>\w+),', re.MULTILINE)
# An act field is `{"form", "accepts"}` in kActFields[].
_ACT_FIELD = re.compile(r'\{\s*"(?P<form>\w+)",\s*"[^"]*"\s*\}')


@dataclass(frozen=True)
class RecordField:
    record: str
    form: str
    path: str
    timing: str


def record_modules(records: Path | None = None) -> list[Path]:
    """Each Record's module, named from the list the way include/config_records.inc
    says: `DroidBuild` is src/config_record_droid_build.cpp."""
    text = (records or CONFIG_RECORDS).read_text(encoding="utf-8")
    modules = []
    for match in _RECORD.finditer(text):
        snake = re.sub(r"(?<!^)(?=[A-Z])", "_", match.group("name")).lower()
        modules.append((records or CONFIG_RECORDS).parent.parent / "src" / f"config_record_{snake}.cpp")
    return modules


def record_fields(modules: list[Path] | None = None) -> list[RecordField]:
    """Every field of every Record, from each module's kFields[]."""
    found = []
    for module in record_modules() if modules is None else modules:
        body = _table(module.read_text(encoding="utf-8"), "kFields")
        found += [RecordField(record=module.stem, form=m.group("form"), path=m.group("path"),
                              timing=_timing(m.group("timing"), m.group("form")))
                  for m in _RECORD_FIELD.finditer(body)]
    return found


def act_fields(source: Path | None = None) -> list[str]:
    """The form name of every field an act takes, from kActFields[]."""
    body = _table((source or CONFIG_APPLY).read_text(encoding="utf-8"), "kActFields")
    return [m.group("form") for m in _ACT_FIELD.finditer(body)]
