#!/usr/bin/env python3
"""Stage one env's release images: the update pair and the blank-board parts.

A release used to carry only `<env>-firmware.bin` and `<env>-filesystem.bin`,
which update a running droid but cannot bring up a blank board: that also
needs the bootloader, the partition table and boot_app0, each at its own flash
offset, and no asset or doc named them (#473). This script stages all five
from the env's last build, plus `<env>-manifest.json`:

    {env, board, chip, tag, commit, firmware_version, filesystem_version,
     flash: [{part, file, offset, size, sha256}, ...]}   (sorted by offset)

Every offset comes from that build, never from a table typed here:

- bootloader, partitions, boot_app0 and their offsets: PlatformIO's build
  metadata, `extra.flash_images` in the env's idedata.json. A plain build
  never writes that file, so this script asks pio for it (`-t idedata`).
- firmware: the partition table the build wrote (partitions.bin), its
  `ota_0` row, the partition the bootloader starts after boot_app0 resets
  otadata. Where the metadata also carries `extra.application_offset`, the
  two must agree.
- filesystem: that partition table's `spiffs` row, the partition the
  LittleFS image is built for.

partitions.bin is read with the framework's own gen_esp32part.py, the tool
that wrote it, so its binary format is not re-implemented here.

Run it after the env's firmware build and `buildfs` (CI runs
tools/check_build_budgets.py first, so the images passed the same budgets and
envelope check Verification holds them to):

    python3 tools/package_release.py --env artoo_esp32 --out release-artifacts [--tag v1.4.0]

It fails, and stages nothing it was unsure of, on a missing part, an image
the metadata names that is not on disk, overlapping regions, an image larger
than its partition, or a pio or envelope failure. It never ships the
builder's firmware.factory.bin: that merge leaves LittleFS out and only
prints, without failing, when it cannot be made.
"""

import argparse
import contextlib
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import check_build_budgets  # tools/, beside this script: run_pio and the core dir
import check_framework_envelope  # tools/, beside this script

ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = ROOT / ".pio" / "build"

# The three images a blank board needs besides the app and the filesystem,
# by the basename the build metadata gives them. A build whose metadata lacks
# one of them is refused rather than shipped without it.
REQUIRED_EXTRA_IMAGES = {
    "bootloader.bin": "bootloader",
    "partitions.bin": "partitions",
    "boot_app0.bin": "boot_app0",
}


class PackagingError(Exception):
    """A condition that makes the staged set unsafe to flash."""


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 16), b""):
            digest.update(chunk)
    return digest.hexdigest()


def env_for_pio(env_name):
    """The environment pio runs in: this env's PlatformIO core dir."""
    env = os.environ.copy()
    env["PLATFORMIO_CORE_DIR"] = check_build_budgets.get_platformio_core_dir(
        env_name, check_build_budgets.load_budgets()
    )
    return env


def build_metadata(env_name, env):
    """Ask pio for the env's build metadata and return its parsed idedata.json.

    Runs under the build lock and keeps its log beside the build's
    (build-logs/<env>-idedata.log). The dump runs the env's pre-scripts and
    the platform's configuration again, which is the same step that can
    half-run the shared pool's rebuild during a `buildfs`, so the framework
    envelope is checked after it as well.
    """
    print(f"Reading build metadata for {env_name}...", file=sys.stderr)
    code = check_build_budgets.run_pio(
        ["pio", "run", "-e", env_name, "-t", "idedata"], env, 600, env_name, "idedata"
    )
    if code != 0:
        raise PackagingError(
            f"pio run -e {env_name} -t idedata "
            + ("timed out" if code is None else f"exited with code {code}")
        )
    with contextlib.redirect_stdout(sys.stderr):
        if check_framework_envelope.check(env_name, quiet=True) != 0:
            raise PackagingError(f"{env_name}: the framework envelope did not hold after -t idedata")
    path = BUILD_ROOT / env_name / "idedata.json"
    if not path.is_file():
        raise PackagingError(f"pio wrote no {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def board_and_chip(env_name, env):
    """The env's board id and its chip, as esptool names it (`esp32p4`).

    The board comes from the resolved project config (an env inherits it
    through `extends`), the chip from that board's manifest in the env's own
    platform. tools/build_budgets.json's platform keys are not chips: its
    `esp32` entry is the default pool for every env that is not a P4.
    """
    out = subprocess.check_output(
        ["pio", "project", "config", "--json-output"], cwd=ROOT, env=env, text=True
    )
    options = dict(dict(json.loads(out)).get(f"env:{env_name}", []))
    board = options.get("board")
    if not board:
        raise PackagingError(f"env:{env_name} names no board in platformio.ini")
    out = subprocess.check_output(
        ["pio", "boards", "--installed", "--json-output"], cwd=ROOT, env=env, text=True
    )
    mcus = {b["mcu"] for b in json.loads(out) if b["id"] == board and b["platform"] == "espressif32"}
    if len(mcus) != 1:
        raise PackagingError(
            f"board {board} resolves to {sorted(mcus) or 'no'} espressif32 MCU in "
            f"{env['PLATFORMIO_CORE_DIR']}; expected exactly one"
        )
    return board, mcus.pop().lower()


def load_gen_esp32part(env):
    """The framework's own partition-table tool, from this env's core dir."""
    path = (
        Path(env["PLATFORMIO_CORE_DIR"])
        / "packages" / "framework-arduinoespressif32" / "tools" / "gen_esp32part.py"
    )
    if not path.is_file():
        raise PackagingError(f"no {path}: the framework is not installed in this core dir")
    spec = importlib.util.spec_from_file_location("gen_esp32part", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.quiet = True  # its status() prints otherwise
    return module


def one_partition(table, ptype, subtype):
    rows = list(table.find_by_type(ptype, subtype))
    if len(rows) != 1:
        raise PackagingError(f"partitions.bin has {len(rows)} {ptype}/{subtype} rows, expected one")
    return rows[0]


def plan_images(env_name, idedata, env):
    """Every image to stage, as (part, source path, offset, region size or None).

    `region size` is the partition the image must fit, where there is one.
    """
    build_dir = BUILD_ROOT / env_name
    extra = idedata.get("extra", {})
    flash_images = extra.get("flash_images") or []
    images = []
    seen = {}
    for item in flash_images:
        path = Path(item["path"])
        name = path.name
        if "factory" in name:
            raise PackagingError(f"{name} is in the flash images; a factory image is never shipped")
        if name in seen:
            raise PackagingError(f"two flash images are both named {name}")
        if not path.is_file():
            raise PackagingError(f"the build metadata names {path}, which does not exist")
        part = REQUIRED_EXTRA_IMAGES.get(name, path.stem)
        seen[name] = part
        images.append((part, path, int(item["offset"], 0), None))
    missing = sorted(set(REQUIRED_EXTRA_IMAGES) - set(seen))
    if missing:
        raise PackagingError(
            f"the build metadata's flash_images lacks {', '.join(missing)}; "
            "a blank board cannot boot without it"
        )

    partitions_bin = next(path for part, path, _, _ in images if part == "partitions")
    gen = load_gen_esp32part(env)
    table = gen.PartitionTable.from_binary(partitions_bin.read_bytes())
    app = one_partition(table, "app", "ota_0")
    fs = one_partition(table, "data", "spiffs")

    declared = extra.get("application_offset")
    if declared is not None and int(declared, 0) != app.offset:
        raise PackagingError(
            f"extra.application_offset is {declared} but partitions.bin puts ota_0 at {app.offset:#x}"
        )

    for part, name in (("firmware", "firmware.bin"), ("filesystem", "littlefs.bin")):
        if not (build_dir / name).is_file():
            raise PackagingError(f"no {build_dir / name}: build the firmware and run buildfs first")
    images.append(("firmware", build_dir / "firmware.bin", app.offset, app.size))
    images.append(("filesystem", build_dir / "littlefs.bin", fs.offset, fs.size))
    return sorted(images, key=lambda image: image[2])


def check_layout(images):
    """No image larger than its partition, and no two images overlapping."""
    end_of_previous, previous = 0, None
    for part, path, offset, region in images:
        size = path.stat().st_size
        if size == 0:
            raise PackagingError(f"{path} is empty")
        if region is not None and size > region:
            raise PackagingError(f"{part} is {size:,} B, larger than its {region:,} B partition")
        if offset < end_of_previous:
            raise PackagingError(
                f"{part} at {offset:#x} overlaps {previous}, which ends at {end_of_previous:#x}"
            )
        end_of_previous, previous = offset + size, part


def read_version(name, key):
    path = ROOT / "data" / name
    try:
        return json.loads(path.read_text(encoding="utf-8"))[key]
    except (OSError, ValueError, KeyError) as e:
        raise PackagingError(f"cannot read {key} from {path}: {e}") from e


def package(env_name, out_dir, tag=None):
    env = env_for_pio(env_name)
    idedata = build_metadata(env_name, env)
    images = plan_images(env_name, idedata, env)
    check_layout(images)
    board, chip = board_and_chip(env_name, env)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()

    out_dir.mkdir(parents=True, exist_ok=True)
    # The update pair keeps its published names: the update guide and every
    # earlier release use them.
    names = {"firmware": f"{env_name}-firmware.bin", "filesystem": f"{env_name}-filesystem.bin"}
    flash = []
    for part, path, offset, _ in images:
        name = names.get(part, f"{env_name}-{part}.bin")
        target = out_dir / name
        shutil.copyfile(path, target)
        flash.append({
            "part": part,
            "file": name,
            "offset": f"{offset:#x}",
            "size": target.stat().st_size,
            "sha256": sha256(target),
        })

    manifest = {
        "env": env_name,
        "board": board,
        "chip": chip,
        "tag": tag or None,
        "commit": commit,
        # Stamped into data/ by tools/extract_version.py during this build.
        "firmware_version": read_version("fw-version.json", "firmwareVersion"),
        "filesystem_version": read_version("fs-version.json", "fsVersion"),
        "flash": flash,
    }
    manifest_path = out_dir / f"{env_name}-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest_path, manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--env", required=True, help="the PlatformIO env to stage")
    parser.add_argument("--out", required=True, type=Path, help="directory to stage into")
    parser.add_argument("--tag", default="", help="the release tag, empty for a dry run")
    args = parser.parse_args(argv)
    try:
        path, manifest = package(args.env, args.out, args.tag)
    except PackagingError as e:
        print(f"package_release.py: {args.env}: {e}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as e:
        print(f"package_release.py: {args.env}: {' '.join(e.cmd)} exited with {e.returncode}",
              file=sys.stderr)
        return 1
    print(f"Staged {len(manifest['flash'])} images and {path.name} in {args.out}", file=sys.stderr)
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
