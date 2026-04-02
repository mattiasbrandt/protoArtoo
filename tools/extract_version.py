#!/usr/bin/env python3
"""
PlatformIO pre-build script: inject PA_FIRMWARE_VERSION as a build flag.

Version string format:
  Released tag:   v0.3.0
  Between tags:   v0.3.0-N-gABCDEF   (N commits ahead, short hash)
  No tags at all: v0.0.0-dev

Strategy (in order):
  1. git describe --tags --always --dirty  -> preferred, includes commit distance
  2. Latest ## [x.y.z] entry in CHANGELOG.md + short git hash  -> fallback
  3. "v0.0.0-dev"                           -> last resort

The define name PA_FIRMWARE_VERSION must match the guard in include/config.h.
"""

import json
import os
import re
import subprocess

Import("env")  # noqa: F821  (PlatformIO injects this)


def _run(cmd):
    """Run a shell command; return stdout stripped, or '' on error."""
    try:
        return subprocess.check_output(
            cmd, stderr=subprocess.DEVNULL, cwd=env.subst("$PROJECT_DIR")
        ).decode().strip()
    except Exception:
        return ""


def _version_from_git():
    """Return version string from `git describe`, normalised to v-prefix."""
    raw = _run(["git", "describe", "--tags", "--always", "--long"])
    if not raw:
        return None
    # git describe output: "v0.3.0-0-gabcdef" or "v0.3.0-5-gabcdef"
    # If it already starts with 'v', keep it; otherwise prepend.
    if not raw.startswith("v"):
        raw = "v" + raw
    # Strip the trailing "-0-gHASH" when there are 0 commits since tag
    # so a clean tagged commit reports "v0.3.0" not "v0.3.0-0-gabcdef".
    clean = re.sub(r"-0-g[0-9a-f]+$", "", raw)
    return clean


def _version_from_changelog():
    """Extract latest version from CHANGELOG.md, append short git hash."""
    try:
        with open("CHANGELOG.md", "r") as f:
            content = f.read()
        match = re.search(r"##\s*\[?(\d+\.\d+\.\d+)\]?", content)
        if match:
            ver = "v" + match.group(1)
            sha = _run(["git", "rev-parse", "--short", "HEAD"])
            return f"{ver}-g{sha}" if sha else ver
    except FileNotFoundError:
        pass
    return None


version = _version_from_git() or _version_from_changelog() or "v0.0.0-dev"

# Inject as PA_FIRMWARE_VERSION — must match the #ifndef guard in include/config.h
env.Append(CPPDEFINES=[("PA_FIRMWARE_VERSION", f'\\"{version}\\"')])
print(f"[extract_version.py] PA_FIRMWARE_VERSION={version}")
# Write data/fs-version.json so the FS image carries the same version.
# A stale FS will show a different string than FW on the footer, making
# "forgot to uploadfs" immediately visible.
fs_version_path = os.path.join(env.subst("$PROJECT_DIR"), "data", "fs-version.json")
fs_version_str = f"fs-{version}"
with open(fs_version_path, "w") as f:
    json.dump({"fsVersion": fs_version_str}, f, indent=2)
    f.write("\n")
print(f"[extract_version.py] fs-version.json -> {fs_version_str}")
