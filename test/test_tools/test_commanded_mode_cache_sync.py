"""The Commanded Mode setters sync the config cache by field, not by snapshot.

`commandedSetStationary()` is on the Core 1 SBUS drive path: `rcInputTask`
calls it once per frame whenever the sticks are off centre
(`src/tasks/rc_input.cpp`), and the tier-2 trigger loop calls it again
(`src/rc_dispatcher_helpers.cpp`). It used to read the whole 944-byte
`ConfigSnapshot` out of the config cache, set one bool, and write all 944
bytes back through `configCacheApply()`. That cost a 944-byte stack local on
a Core 1 frame, copied 1888 bytes under the cache lock per toggle, and -
because `configCacheApply()` raises `RobotState.rcConfigDirty` - made
`RcInputTask` rebuild its cached mapping config every time, although
`stationary` is not in the RC processor config at all
(`include/rc_input_processor.h` reads it only as `stationaryLocked` state).

ADR 0011's 2026-09-04 amendment settles it: Commanded Mode setters sync the
config cache by field. `include/config_cache.h`'s `configCacheSetStationary()`
is that field setter, and `test_config_store.cpp` holds it to writing one
field and leaving the dirty flag alone.

WHY A SOURCE-CONTRACT TEST AND NOT A NATIVE ONE. `src/commanded_modes.cpp`
is not in `platformio.ini`'s native `build_src_filter`; `native_test_stubs.cpp`
records `commandedSetStationary()` rather than running it, because the SBUS
task it belongs to is not in the native build either. So the field setter is
provable natively and the caller is not. This is what holds the caller, in the
same shape `test_queue_send_timeout_zero.py` uses to hold the Core 1 queue
sends: the rule stops being a habit at one call site.

A future setter that genuinely needs the whole snapshot adds itself to
ALLOWED_SNAPSHOT_ROUND_TRIPS with a written reason - a conscious decision,
never a silent exemption.
"""

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
COMMANDED_MODES = REPO_ROOT / "src" / "commanded_modes.cpp"

# The whole-snapshot cache calls. configCacheSetStationary() and the other
# by-field accessors are deliberately absent: they are the shape this rule
# asks for.
SNAPSHOT_CACHE_CALLS = ("configCacheRead", "configCacheApply")
SNAPSHOT_CALL_RE = re.compile(r"\b(" + "|".join(SNAPSHOT_CACHE_CALLS) + r")\s*\(")

# Deliberate exceptions, keyed "<function>:<call>" and valued with the reason.
# Empty on purpose: no Commanded Mode setter needs the whole snapshot today.
ALLOWED_SNAPSHOT_ROUND_TRIPS: dict[str, str] = {}

# The Core 1 call sites that make this file a real-time path. Asserted so the
# rule's premise is checked against the tree rather than remembered.
SBUS_CALLER_FILES = (
    REPO_ROOT / "src" / "tasks" / "rc_input.cpp",
    REPO_ROOT / "src" / "rc_dispatcher_helpers.cpp",
)


def strip_comments_and_strings(text: str) -> str:
    """Blank out comments and string/char literals, preserving line structure.

    The rule is about calls, and this file's comments name the calls it must
    not make - so a scan over raw text would report its own documentation.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif ch == "/" and i + 1 < n and text[i + 1] == "*":
            out.append("  ")
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        elif ch in ('"', "'"):
            quote = ch
            out.append(" ")
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\" and i + 1 < n:
                    out.append("  ")
                    i += 2
                    continue
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append(" ")
            i += 1
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def find_snapshot_round_trips(text: str) -> list[tuple[int, str]]:
    """Return (line number, call name) for every whole-snapshot cache call."""
    stripped = strip_comments_and_strings(text)
    found = []
    for match in SNAPSHOT_CALL_RE.finditer(stripped):
        line = stripped.count("\n", 0, match.start()) + 1
        found.append((line, match.group(1)))
    return found


class CommandedModeCacheSyncTest(unittest.TestCase):
    def test_the_setters_never_round_trip_the_whole_snapshot(self):
        text = COMMANDED_MODES.read_text(encoding="utf-8")
        offenders = [
            f"src/commanded_modes.cpp:{line}: {call}() - a whole-snapshot config "
            f"cache round trip on the Core 1 SBUS path"
            for line, call in find_snapshot_round_trips(text)
            if f"{call}" not in ALLOWED_SNAPSHOT_ROUND_TRIPS
        ]
        self.assertEqual([], offenders, "\n" + "\n".join(offenders))

    def test_the_field_setter_is_the_one_it_uses_instead(self):
        """A file that synced nothing at all would pass the rule vacuously."""
        text = strip_comments_and_strings(COMMANDED_MODES.read_text(encoding="utf-8"))
        self.assertIn(
            "configCacheSetStationary(",
            text,
            "commandedSetStationary() no longer syncs the config cache at all - the "
            "next config save would revert the commanded mode from a stale cache",
        )

    def test_a_snapshot_round_trip_would_be_reported(self):
        """The rule is shown failing, not assumed to."""
        source = (
            "void commandedSetSomething(bool on) {\n"
            "    // configCacheRead(&cfg) in a comment is not a call\n"
            '    const char *s = "configCacheApply(cfg)";\n'
            "    ConfigSnapshot cfg = {};\n"
            "    configCacheRead(&cfg);\n"
            "    cfg.system.something = on;\n"
            "    configCacheApply(cfg);\n"
            "}\n"
        )
        self.assertEqual(
            [(5, "configCacheRead"), (7, "configCacheApply")],
            find_snapshot_round_trips(source),
            "the scan did not read exactly the two real calls - the commented and "
            "quoted ones must not be seen",
        )

    def test_the_core_1_sbus_callers_still_exist(self):
        """The premise: this file is on a real-time path, checked not recalled."""
        for path in SBUS_CALLER_FILES:
            text = strip_comments_and_strings(path.read_text(encoding="utf-8"))
            rel = path.relative_to(REPO_ROOT)
            self.assertIn(
                "commandedSetStationary(",
                text,
                f"{rel} no longer calls commandedSetStationary() - re-check whether "
                "this rule still guards a Core 1 path",
            )
            self.assertIn(
                "SRC_SBUS",
                text,
                f"{rel} no longer names SRC_SBUS - re-check the RC dispatch path",
            )


if __name__ == "__main__":
    unittest.main()
