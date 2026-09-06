#!/usr/bin/env python3
"""Verify that an env's custom_sdkconfig actually held in the built framework libs.

WHY THIS EXISTS
---------------
pioarduino applies `custom_sdkconfig` by recompiling the Arduino/IDF framework
libs inside the SHARED PlatformIO packages directory. Whether it does that is
decided by a stamp in `<project>/sdkconfig.defaults`, whose first line is a hash
of the env's custom_sdkconfig text. The stamp is per project directory; the libs
it guards are global and shared by every worktree on the machine.

The rebuild path is `Reinstall` (rmtree + extract PRISTINE libs, i.e. every
override OFF) -> `Compile Arduino IDF libs` -> `Copied compiled`. Pristine is an
intermediate state of the repair itself. So if that sequence does not complete
while the stamp already matches, the machine is left holding pristine libs behind
a stamp that says "already built for this config", and every later build links
them - silently, with no warning and no marker.

That happened on 2026-08-29: the artoo-esp32 image silently grew 111 264 B of
flash and 1 188 B of static RAM on an unchanged commit, and every existing check
still passed. The build budget cannot catch it (a non-envelope image was still
comfortably inside its limit), and the build log cannot be trusted either -
platformio.ini says so itself, because a Kconfig `select` in an unrelated
component can turn an `=n` back on after the log printed `Replace: ...=n`.

The only reliable evidence is the RESOLVED sdkconfig the libs were actually built
with, which is what this script reads. It automates a check platformio.ini's own
comments already prescribe by hand.

USAGE
    python3 tools/check_framework_envelope.py [--env artoo_esp32] [--quiet]

Exit codes: 0 = every declared override held; 1 = at least one did not (or the
resolved sdkconfig is missing, i.e. nothing has been built yet).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLATFORMIO_INI = ROOT / "platformio.ini"
BUDGETS = ROOT / "tools" / "build_budgets.json"

# Kconfig writes a disabled bool as a comment, never as `KEY=n`. Reading the file
# without knowing that is how a correct config reads as a missing one.
DISABLED_RE = "^# {key} is not set$"


def read_ini() -> str:
    return PLATFORMIO_INI.read_text()


def section_body(ini: str, name: str) -> str:
    """Return the raw body of [name], or '' when the section is absent."""
    match = re.search(
        r"^\[" + re.escape(name) + r"\]\n(.*?)(?=^\[|\Z)", ini, re.S | re.M
    )
    return match.group(1) if match else ""


def custom_sdkconfig_lines(body: str) -> list[str]:
    """The continuation lines of a section's `custom_sdkconfig = ` block."""
    match = re.search(r"^custom_sdkconfig\s*=\s*\n((?:[ \t]+.*\n)+)", body, re.M)
    return match.group(1).splitlines() if match else []


def expand(ini: str, lines: list[str], _depth: int = 0) -> list[str]:
    """Resolve `${section.custom_sdkconfig}` references, which is how an env
    composes a shared envelope with its own board-specific overrides."""
    if _depth > 5:  # a malformed ini should fail loudly, not hang
        raise RuntimeError("custom_sdkconfig reference nested more than 5 deep")
    out: list[str] = []
    for line in lines:
        ref = re.search(r"\$\{([A-Za-z0-9_:]+)\.custom_sdkconfig\}", line)
        if ref:
            out += expand(ini, custom_sdkconfig_lines(section_body(ini, ref.group(1))), _depth + 1)
        else:
            out.append(line)
    return out


def declared_overrides(ini: str, env: str) -> dict[str, str]:
    """Every CONFIG_* override the env declares, envelope references expanded."""
    lines = expand(ini, custom_sdkconfig_lines(section_body(ini, f"env:{env}")))
    overrides: dict[str, str] = {}
    for line in lines:
        stripped = line.strip()
        if stripped.startswith(";") or "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        key, value = key.strip(), value.strip()
        if key.startswith("CONFIG_"):
            overrides[key] = value
    return overrides


def resolved_sdkconfig_path(env: str) -> Path:
    """Locate the sdkconfig the framework libs for this env were built with.

    Each chip target gets its own PLATFORMIO_CORE_DIR (see the Makefile), so the
    path depends on which platform the env belongs to - reading the artoo core
    dir while checking a P4 env would silently check the wrong libs.

    The libs directory is NOT always the platform name. The ESP32-P4 links
    `esp32p4_es`, a silicon-revision variant selected by
    CONFIG_ESP32P4_SELECTS_REV_LESS_V3, while a stale `esp32p4` sits beside it. This
    function used to derive the directory from the platform key, so every P4 check
    read a months-old file carrying stock values and reported overrides as violated
    on a correctly built image. The registry declares the directory explicitly
    (`libs_dir`) for the same reason it declares `core_dir`: it is a platform fact,
    and guessing it produces a confident wrong answer.
    """
    budgets = json.loads(BUDGETS.read_text())
    platforms = budgets.get("platforms", {})

    chip, core_dir = "esp32", "~/.platformio"
    for name, spec in platforms.items():
        if env in spec.get("envs", []):
            chip = spec.get("libs_dir", name)
            core_dir = spec.get("core_dir", core_dir)
            break

    core = Path(os.path.expanduser(core_dir))
    return core / "packages" / "framework-arduinoespressif32-libs" / chip / "sdkconfig"


def actual_value(resolved: str, key: str) -> str:
    if re.search(DISABLED_RE.format(key=re.escape(key)), resolved, re.M):
        return "n"
    match = re.search(r"^%s=(.*)$" % re.escape(key), resolved, re.M)
    return match.group(1).strip() if match else "ABSENT"


def check(env: str, quiet: bool = False, sdkconfig: Path | None = None) -> int:
    overrides = declared_overrides(read_ini(), env)
    if not overrides:
        if not quiet:
            print(f"[envelope] {env}: no custom_sdkconfig overrides declared - nothing to check")
        return 0

    # sdkconfig overrides the location only; the expectations still come from
    # platformio.ini, so a caller cannot weaken the check by pointing it
    # elsewhere - only aim it at a different build's output (or a test fixture).
    path = sdkconfig or resolved_sdkconfig_path(env)
    if not path.exists():
        print(f"[envelope] FAIL {env}: no resolved sdkconfig at {path}")
        print("[envelope] The framework libs have not been built for this env yet - build first.")
        return 1

    resolved = path.read_text()
    mismatches = [
        (key, want, actual_value(resolved, key))
        for key, want in sorted(overrides.items())
        if actual_value(resolved, key) != want
    ]

    if not mismatches:
        if not quiet:
            print(f"[envelope] PASS {env}: all {len(overrides)} declared overrides held")
            print(f"[envelope] verified against {path}")
        return 0

    print(f"[envelope] FAIL {env}: {len(mismatches)} of {len(overrides)} declared overrides did NOT hold")
    print(f"[envelope] resolved sdkconfig: {path}")
    for key, want, actual in mismatches:
        print(f"[envelope]   {key:<45} declared={want:<8} actual={actual}")
    print()
    print("[envelope] The image you just built is NOT the one platformio.ini describes.")
    print("[envelope] Most likely the framework rebuild did not complete: the libs are")
    print("[envelope] pristine while the project stamp still claims they were rebuilt.")
    print("[envelope] Repair (nothing else building on this machine):")
    print("[envelope]   rm -f sdkconfig.defaults")
    print("[envelope]   make build BUILD_ENV=%s" % env)
    print("[envelope] The log must show all three of: 'Reinstall', 'Compile Arduino IDF")
    print("[envelope] libs', 'Copied compiled'. Then re-run this check.")
    print("[envelope] If a key still will not hold, a Kconfig `select` in another")
    print("[envelope] component may be re-enabling it - that is a real finding, not noise.")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--env", default="artoo_esp32", help="PlatformIO env to check")
    parser.add_argument("--quiet", action="store_true", help="print only on failure")
    parser.add_argument(
        "--sdkconfig",
        type=Path,
        default=None,
        help="read this resolved sdkconfig instead of the env's built one "
        "(diagnostics and tests; expectations still come from platformio.ini)",
    )
    args = parser.parse_args()
    return check(args.env, args.quiet, args.sdkconfig)


if __name__ == "__main__":
    sys.exit(main())
