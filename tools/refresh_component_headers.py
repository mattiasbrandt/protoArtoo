# PlatformIO pre: extra script -- copy the rebuilt managed components' headers into
# the framework libs package, the step pioarduino's HybridCompile leaves out.
#
# TODO(#437): delete this script, and its line in [env:firebeetle2], once
# pioarduino's idf_lib_copy() copies component include dirs itself. Without this
# script a P4 build compiles against headers from a different component version
# than the libraries it links.
#
# WHY THIS EXISTS
# ---------------
# firebeetle2 declares custom_sdkconfig, so pioarduino recompiles the Arduino IDF
# libs from source ("HybridCompile"). That rebuild re-resolves every managed
# component to the newest version its range allows (the framework manifest asks
# for esp_wifi_remote ^1.2.2, esp_hosted ^2.9.2, ...) and copies the rebuilt
# archives, linker scripts and sdkconfig.h back into the libs package. It never
# copies the components' include dirs (idf_lib_copy() in pioarduino's
# builder/frameworks/espidf.py, unchanged through 55.03.312-1). So Arduino code
# keeps compiling against the headers the framework tarball shipped, while it
# links libraries built from newer ones.
#
# Measured on #437 (2026-09-29): esp_wifi_remote 1.6.5 inserted
# wifi_task_stack_size before `magic` in wifi_init_config_t. Arduino's
# WiFiGeneric.cpp filled the tarball's 148-byte layout, esp_hosted read the
# 152-byte one, the ESP32-C6 received a garbage magic and refused esp_wifi_init
# with ESP_ERR_INVALID_ARG, and the board had no WiFi at all. The same pass found
# 40 stale headers across 7 components.
#
# HOW IT RUNS
# -----------
# After the rebuild, pioarduino starts a child `pio run -e <env>` for the Arduino
# compile. This script runs at the top of that child: the rebuilt archives are in
# place and managed_components/ holds exactly the sources they were built from.
# That moment is recognised by the libs package having been rebuilt (its
# top-level sdkconfig exists) while this script's stamp is absent, because the
# reinstall that starts every rebuild replaces the whole package, stamp included.
#
# The copy records what it wrote in a stamp inside the package. Every later build
# re-hashes those headers against the stamp and fails on any difference, so a
# package altered by anything else is caught before it is compiled against.
from __future__ import annotations

import hashlib
import json
import re
import shutil
from pathlib import Path

STAMP_NAME = ".pa-component-headers.json"
TAG = "[component-headers]"


def managed_include_dirs(include_dir: Path) -> list[Path]:
    """The package's include dirs that belong to managed components.

    The component manager names a managed component `<namespace>__<name>`
    (espressif__esp_hosted, joltwallet__littlefs); ESP-IDF's own components
    never carry the double underscore.
    """
    return sorted(p for p in include_dir.iterdir() if p.is_dir() and "__" in p.name)


def component_version(component_dir: Path) -> str:
    manifest = component_dir / "idf_component.yml"
    if not manifest.is_file():
        return "UNKNOWN"
    match = re.search(r"^version:\s*['\"]?([^'\"\s]+)", manifest.read_text(), re.M)
    return match.group(1) if match else "UNKNOWN"


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# The rebuild copies its archives into the package moments before it writes the
# package's top-level sdkconfig; an hour of slack covers a slow machine. The
# tarball's own archives keep their release-day timestamps.
REBUILT_WINDOW_S = 3600


def rebuilt_in_last_rebuild(lib_dir: Path, component: str, marker: Path) -> bool:
    """Whether the last rebuild replaced this component's archive.

    Components the rebuild does not resolve (the camera, video and TFLite stack
    on the P4) keep the tarball's archive, and so the tarball's headers still
    match them. The archive name can carry a `_2` suffix (pioarduino #533).
    """
    since = marker.stat().st_mtime - REBUILT_WINDOW_S
    return any(a.stat().st_mtime >= since for a in lib_dir.glob(f"lib{component}*.a"))


def refresh(include_dir: Path, managed_dir: Path, lib_dir: Path, marker: Path) -> dict:
    """Copy every header the package holds for a rebuilt managed component from
    that component's source in managed_dir, where the two differ.

    Returns the stamp to record: per component, its version and the hash of
    each header now in the package. `missing` lists rebuilt components that
    managed_dir does not have; the caller must treat that as a failure, because
    those headers cannot be matched to the libraries. `not_rebuilt` lists
    components the rebuild left alone; their tarball headers match their
    tarball archives and are not touched.
    """
    report: dict = {"components": {}, "updated": [], "missing": [], "orphans": [], "not_rebuilt": []}
    for pkg_dir in managed_include_dirs(include_dir):
        src_dir = managed_dir / pkg_dir.name
        if not src_dir.is_dir():
            if rebuilt_in_last_rebuild(lib_dir, pkg_dir.name, marker):
                report["missing"].append(pkg_dir.name)
            else:
                report["not_rebuilt"].append(pkg_dir.name)
            continue
        files: dict[str, str] = {}
        for pkg_file in sorted(p for p in pkg_dir.rglob("*") if p.is_file()):
            rel = pkg_file.relative_to(pkg_dir)
            src_file = src_dir / rel
            if not src_file.is_file():
                # The tarball ships a header this component version no longer
                # has. Nothing includes it from the new sources; record it.
                report["orphans"].append(f"{pkg_dir.name}/{rel}")
                continue
            if pkg_file.read_bytes() != src_file.read_bytes():
                shutil.copy2(src_file, pkg_file)
                report["updated"].append(f"{pkg_dir.name}/{rel}")
            files[str(rel)] = _sha256(pkg_file)
        report["components"][pkg_dir.name] = {
            "version": component_version(src_dir),
            "files": files,
        }
    return report


def verify(include_dir: Path, stamp: dict) -> list[str]:
    """Every stamped header whose bytes no longer match the stamp."""
    changed: list[str] = []
    for name, entry in sorted(stamp.get("components", {}).items()):
        for rel, digest in sorted(entry.get("files", {}).items()):
            path = include_dir / name / rel
            if not path.is_file() or _sha256(path) != digest:
                changed.append(f"{name}/{rel}")
    return changed


def run(include_dir: Path, rebuilt_marker: Path, managed_dir: Path) -> str:
    """Refresh or verify one libs package. Returns the line to print; raises
    SystemExit with the repair recipe when the build must not go on."""
    if not rebuilt_marker.is_file():
        return f"{TAG} libs not rebuilt yet; checked after the rebuild"

    stamp_path = include_dir / STAMP_NAME
    if stamp_path.is_file():
        stamp = json.loads(stamp_path.read_text())
        changed = verify(include_dir, stamp)
        if changed:
            raise SystemExit(
                f"{TAG} FAIL: {len(changed)} component header(s) in {include_dir} changed "
                f"since they were matched to the rebuilt libraries: "
                + ", ".join(changed[:8]) + (" ..." if len(changed) > 8 else "")
                + f"\n{TAG} Something rewrote the libs package after the rebuild. Repair, "
                "nothing else building on this machine: rm -f sdkconfig.defaults, then "
                "rebuild this env (see #437)."
            )
        versions = ", ".join(f"{n.split('__', 1)[1]} {e['version']}" for n, e in sorted(stamp["components"].items()))
        return f"{TAG} headers match the rebuilt libraries ({versions})"

    report = refresh(include_dir, managed_dir, include_dir.parent / "lib", rebuilt_marker)
    if report["missing"]:
        raise SystemExit(
            f"{TAG} FAIL: the last rebuild replaced the libraries of "
            + ", ".join(report["missing"])
            + f", but {managed_dir} has no such component, so their headers cannot be matched "
            "to the libraries. managed_components/ was probably re-resolved by another "
            "env's rebuild. Repair: rm -f sdkconfig.defaults, then rebuild this env "
            "(see #437)."
        )
    stamp_path.write_text(json.dumps({"components": report["components"]}, indent=1, sort_keys=True) + "\n")
    line = (
        f"{TAG} matched {sum(len(e['files']) for e in report['components'].values())} headers "
        f"of {len(report['components'])} components to the rebuilt libraries; "
        f"{len(report['updated'])} were stale and replaced"
    )
    if report["not_rebuilt"]:
        line += f"; {len(report['not_rebuilt'])} component(s) not rebuilt, tarball headers kept"
    if report["orphans"]:
        line += f"; {len(report['orphans'])} tarball header(s) have no source and were left: " + ", ".join(report["orphans"])
    return line


if "Import" in globals():  # executed by SCons as an extra script
    Import("env")  # noqa: F821 - provided by SCons

    _libs = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs"))  # noqa: F821
    _board = env.BoardConfig()  # noqa: F821
    _variant = (_board.get("build.chip_variant", "") or _board.get("build.mcu")).lower()
    print(
        run(
            include_dir=_libs / _variant / "include",
            rebuilt_marker=_libs / "sdkconfig",
            managed_dir=Path(env.subst("$PROJECT_DIR")) / "managed_components",  # noqa: F821
        )
    )
