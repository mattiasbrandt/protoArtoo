#!/usr/bin/env python3
"""Every firmware env in platformio.ini must resolve a PA_BOARD build flag.

include/config.h refuses to compile without -DPA_BOARD=... (its pin map and
capability ladders select on it), so an env that overrides `build_flags`
without carrying the flag builds nothing. The first v1.1.0 release run found
exactly that: the three sound-backend release envs (artoo_esp32_chirp,
artoo_esp32_mp3trigger, artoo_esp32_dysv5w) had re-declared `build_flags` for
their PA_AUDIO_DRIVER and never carried PA_BOARD, and neither the verification
workflow nor the slice gate builds them, so the defect surfaced at tag time.

This resolves each env's effective `build_flags` the way PlatformIO does for
the purpose of this check - an env's own `build_flags` wins, `${section.option}`
references are expanded, and an env without its own `build_flags` inherits
through `extends` - and fails naming every env that ends up without the flag.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLATFORMIO_INI = ROOT / "platformio.ini"

SECTION_RE = re.compile(r"(?ms)^\[([^\]]+)\]\n(.*?)(?=^\[|\Z)")
OPTION_RE = re.compile(r"(?ms)^{name}\s*=[ \t]*(.*?)(?=^\S|\Z)")
REF_RE = re.compile(r"\$\{([^.}]+)\.([A-Za-z_]+)\}")


def _strip_comments(body: str) -> str:
    return "\n".join(line for line in body.split("\n") if not line.lstrip().startswith(";"))


def load_sections(text: str) -> dict[str, str]:
    return {name: _strip_comments(body) for name, body in SECTION_RE.findall(text)}


def option(sections: dict[str, str], section: str, name: str) -> str | None:
    body = sections.get(section)
    if body is None:
        return None
    match = OPTION_RE.pattern.replace("{name}", re.escape(name))
    found = re.search(match, body)
    return found.group(1) if found else None


def effective_option(sections: dict[str, str], section: str, name: str, seen=()) -> str:
    """The option's text for `section`, with `${x.y}` expanded and `extends` followed."""
    if section in seen:
        raise AssertionError(f"extends cycle through {section}")
    own = option(sections, section, name)
    if own is not None:
        def expand(m):
            return effective_option(sections, m.group(1), m.group(2), seen + (section,))
        return REF_RE.sub(expand, own)
    parent = option(sections, section, "extends")
    if parent:
        return effective_option(sections, parent.strip(), name, seen + (section,))
    return ""


class EveryFirmwareEnvDeclaresItsBoard(unittest.TestCase):
    def test_every_env_resolves_a_pa_board_flag(self):
        sections = load_sections(PLATFORMIO_INI.read_text())
        envs = [s for s in sections if s.startswith("env:") and s != "env:native"]
        self.assertGreater(len(envs), 5, "platformio.ini parse found too few envs")
        missing = [
            env for env in envs
            if "-DPA_BOARD=PA_BOARD_" not in effective_option(sections, env, "build_flags")
        ]
        self.assertEqual(
            missing, [],
            "envs whose effective build_flags carry no -DPA_BOARD=PA_BOARD_<variant>; "
            "include/config.h refuses to compile them: " + ", ".join(missing),
        )

    def test_native_env_is_exempt_but_still_declares_a_board(self):
        # The host test env compiles config.h too, so it names a board as well;
        # it is excluded above only because it is not a firmware image.
        sections = load_sections(PLATFORMIO_INI.read_text())
        self.assertIn("-DPA_BOARD=PA_BOARD_", effective_option(sections, "env:native", "build_flags"))


if __name__ == "__main__":
    unittest.main()
