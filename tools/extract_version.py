#!/usr/bin/env python3
"""
PlatformIO pre-build script to extract version from CHANGELOG.md and inject
it as a build flag (-DPA_VERSION="x.y.z").

This allows the firmware to report its version at runtime without manual
synchronization between CHANGELOG.md and config.h.
"""

import re

Import("env")


def get_version_from_changelog():
    """Extract latest version from CHANGELOG.md header."""
    try:
        with open("CHANGELOG.md", "r") as f:
            content = f.read()
        # Match "## [x.y.z]" or "## x.y.z" patterns
        match = re.search(r"##\s*\[?(\d+\.\d+\.\d+)\]?", content)
        if match:
            return match.group(1)
    except FileNotFoundError:
        pass
    return "0.0.0"


version = get_version_from_changelog()
env.Append(CPPDEFINES=[("PA_VERSION", f'\\"{version}\\"')])
print(f"[extract_version.py] Building with PA_VERSION={version}")
