#!/usr/bin/env python3
"""
PlatformIO pre-build script: inject PA_FIRMWARE_VERSION as a build flag.

Also runnable standalone (`python3 tools/extract_version.py`) outside a
PlatformIO build — used by CI to regenerate data/fw-version.json and
data/fs-version.json on push to main without running a full firmware build.

Version string format:
  Released tag:   v0.3.0
  Between tags:   v0.3.0-N-gABCDEF   (N commits ahead, short hash)
  No tags at all: v0.0.0-dev

Strategy (in order):
  1. git describe --tags --always --match 'v[0-9]*' + explicit dirty check
     -> preferred. The --match filter keeps non-release tags (safepoint
     markers and similar) from being mistaken for a version.
     (equivalent to --dirty, but blind to the two stamp files this script
     writes — otherwise every build after the first reports -dirty, including
     a pristine CI release checkout)
  2. Latest ## [x.y.z] entry in CHANGELOG.md + short git hash  -> fallback
  3. "v0.0.0-dev"                           -> last resort

The define name PA_FIRMWARE_VERSION must match the guard in include/config.h.
"""

import json
import os
import re
import subprocess

# Tags that name a release. Everything else in the tag namespace (safepoint
# markers, scratch tags) is invisible to version stamping.
RELEASE_TAG_GLOB = "v[0-9]*"

try:
    Import("env")  # noqa: F821  (PlatformIO injects this in a build context)
    PROJECT_DIR = env.subst("$PROJECT_DIR")  # noqa: F821
    _PIO_CONTEXT = True
except NameError:
    PROJECT_DIR = os.getcwd()
    _PIO_CONTEXT = False


def _run(cmd):
    """Run a shell command; return stdout stripped, or '' on error."""
    try:
        return subprocess.check_output(
            cmd, stderr=subprocess.DEVNULL, cwd=PROJECT_DIR
        ).decode().strip()
    except Exception:
        return ""


def _current_branch():
    """Return the current branch name, using CI ref info when HEAD is detached."""
    if os.environ.get("GITHUB_REF_TYPE") == "branch":
        ref_name = os.environ.get("GITHUB_REF_NAME")
        if ref_name:
            return ref_name
    return _run(["git", "rev-parse", "--abbrev-ref", "HEAD"])


def _tree_is_dirty():
    """True if any tracked file is modified, EXCLUDING the version stamps this
    script itself writes. The stamps are regenerated on every build, so letting
    them count would make every build after the first report -dirty — including
    a pristine CI release checkout."""
    out = _run([
        "git", "status", "--porcelain", "--untracked-files=no", "--",
        ".",
        ":(exclude)data/fw-version.json",
        ":(exclude)data/fs-version.json",
    ])
    return bool(out)


def _version_from_git():
    """Return version string from `git describe`, normalised to v-prefix."""
    raw = _run([
        "git", "describe", "--tags", "--always", "--long",
        # Only release tags describe a version. The repo also carries
        # non-release marker tags (e.g. safepoint/*), and without this filter
        # describe picks whichever tag is nearest -- which stamped builds with
        # a safepoint branch name instead of the release line.
        "--match", RELEASE_TAG_GLOB,
    ])
    if not raw:
        return None
    # Append -dirty in describe's own format so the normalisation below sees
    # exactly what `git describe --dirty` would have produced.
    if _tree_is_dirty():
        raw += "-dirty"
    # git describe output: "v0.3.0-0-gabcdef" or "v0.3.0-5-gabcdef"
    # If it already starts with 'v', keep it; otherwise prepend.
    if not raw.startswith("v"):
        raw = "v" + raw
    # Strip the trailing "-0-gHASH" when there are 0 commits since tag
    # so a clean tagged commit reports "v0.3.0" not "v0.3.0-0-gabcdef".
    # A dirty tree appends "-dirty" after the hash, so this leaves the full
    # descriptor (never collapsing to a clean-looking bare tag) in that case.
    clean = re.sub(r"-0-g[0-9a-f]+$", "", raw)
    # Non-main builds carry a semver build-metadata suffix so concurrent
    # feature branches never collide on version identity.
    branch = _current_branch()
    if branch and branch not in ("main", "HEAD"):
        clean = f"{clean}+{branch.replace('/', '-')}"
    return clean


def _version_from_changelog():
    """Extract latest version from CHANGELOG.md, append short git hash."""
    try:
        with open(os.path.join(PROJECT_DIR, "CHANGELOG.md"), "r") as f:
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

if _PIO_CONTEXT:
    # Inject as PA_FIRMWARE_VERSION — must match the #ifndef guard in include/config.h
    env.Append(CPPDEFINES=[("PA_FIRMWARE_VERSION", f'\\"{version}\\"')])  # noqa: F821
print(f"[extract_version.py] PA_FIRMWARE_VERSION={version}")

# Write data/fw-version.json as a tracked generated web asset for version display
# and release traceability.
# Runtime firmware truth still comes from PA_FIRMWARE_VERSION in /api/status.
fw_version_path = os.path.join(PROJECT_DIR, "data", "fw-version.json")
with open(fw_version_path, "w") as f:
    json.dump({"firmwareVersion": version}, f, indent=2)
    f.write("\n")
print(f"[extract_version.py] fw-version.json -> {version}")

# Write data/fs-version.json so the FS image carries the same version.
# A stale FS will show a different string than FW on the footer, making
# "forgot to uploadfs" immediately visible.
fs_version_path = os.path.join(PROJECT_DIR, "data", "fs-version.json")
fs_version_str = f"fs-{version}"
with open(fs_version_path, "w") as f:
    json.dump({"fsVersion": fs_version_str}, f, indent=2)
    f.write("\n")
print(f"[extract_version.py] fs-version.json -> {fs_version_str}")
