#!/usr/bin/env python3
# =============================================================================
# tools/resolve_upload_port.py
#
# Decide which serial device a USB upload target may write to, or refuse.
#
# The Makefile used to carry `UPLOAD_PORT ?= /dev/ttyUSB0`. That is a guess, and
# on a two-board bench it is a guess about *which board*. It also hid a second
# fault: `pio run -t upload --upload-port <p>` did not take -- PlatformIO logged
# "Looking for upload port... Auto-detected: /dev/ttyUSB0" and aimed an ESP32-P4
# image at the artoo-esp32 anyway. Nothing was written (esptool refused: "This
# chip is ESP32, not ESP32-P4"), but the connect attempt left the artoo in the
# ROM download stub and off the network. Only the chip check stood between a
# named port and the wrong board.
#
# So this resolver never guesses, and the Makefile exports the result as
# PLATFORMIO_UPLOAD_PORT (the form measured to actually reach PlatformIO):
#
#   UPLOAD_PORT set     -> use it, after checking the device exists.
#   exactly one device  -> use it. Unambiguous, so nothing is being assumed.
#   two or more devices -> REFUSE and list them. This is the case that bit us:
#                          there is no right answer and picking one is the bug.
#   no devices          -> REFUSE.
#
# Ports are reported by their /dev/serial/by-id/ name where one exists, because
# that name survives a replug and ttyUSB0/ttyACM0 do not.
#
# Usage:
#   python3 tools/resolve_upload_port.py --env firebeetle2   # -> /dev/ttyACM0
#   python3 tools/resolve_upload_port.py --list
# =============================================================================

import argparse
import glob
import os
import sys

BY_ID_DIR = "/dev/serial/by-id"


TTY_PATTERNS = ("/dev/ttyUSB*", "/dev/ttyACM*")


def discover(by_id_dir: str = BY_ID_DIR,
             patterns: tuple[str, ...] = TTY_PATTERNS) -> list[tuple[str, str | None]]:
    """Return [(device_path, stable_by_id_name_or_None)], sorted by device.

    /dev/serial/by-id is the stable view but is absent on some systems and for
    some adapters, so the tty glob is the fallback rather than the other way
    round: a device that has no by-id entry must still be offered, or the
    resolver refuses a bench it could have served.
    """
    by_device: dict[str, str | None] = {}

    if os.path.isdir(by_id_dir):
        for name in sorted(os.listdir(by_id_dir)):
            link = os.path.join(by_id_dir, name)
            try:
                device = os.path.realpath(link)
            except OSError:
                continue
            if os.path.exists(device):
                by_device[device] = name

    for pattern in patterns:
        for device in glob.glob(pattern):
            by_device.setdefault(device, None)

    return sorted(by_device.items())


def describe(candidates: list[tuple[str, str | None]]) -> str:
    if not candidates:
        return "  (none found)"
    lines = []
    for device, stable in candidates:
        lines.append(f"  {device}" + (f"    {stable}" if stable else ""))
    return "\n".join(lines)


def resolve(requested: str | None, candidates: list[tuple[str, str | None]],
            env: str | None, origin: str | None = None,
            warn=lambda msg: print(msg, file=sys.stderr)) -> str:
    """Return the port to upload to, or raise SystemExit with an explanation.

    `origin` is make's `$(origin UPLOAD_PORT)`. A value someone typed on the
    command line or wrote in user.mk is a decision; a value inherited from the
    ambient shell environment may just be an old export the operator has
    forgotten, which is indistinguishable from a guess at the moment it aims an
    image at a board. Both are honoured -- silently ignoring an exported
    variable would be its own surprise -- but the ambient one says so out loud
    when there is more than one board to be wrong about.
    """
    devices = [device for device, _ in candidates]
    target = f" for env '{env}'" if env else ""

    if requested:
        if origin == "environment" and len(devices) > 1:
            warn(f"warning: UPLOAD_PORT={requested} comes from the shell "
                 f"environment, not this command.\n"
                 f"         {len(devices)} boards are attached; if that is the "
                 f"wrong one, pass UPLOAD_PORT= explicitly or unset it.")
        # An explicit choice is honoured, but a typo must not fall through to a
        # guess -- that is how a named port becomes the wrong board.
        real = os.path.realpath(requested)
        if requested in devices or real in devices or os.path.exists(real):
            return requested
        raise SystemExit(
            f"UPLOAD_PORT={requested} does not exist.\n"
            f"Serial devices present:\n{describe(candidates)}")

    if not devices:
        raise SystemExit(
            f"No serial device found{target}, and UPLOAD_PORT is not set.\n"
            "Attach the board, or pass UPLOAD_PORT=/dev/...")

    if len(devices) > 1:
        raise SystemExit(
            f"{len(devices)} serial devices are attached, so the target"
            f"{target} is ambiguous.\n"
            f"{describe(candidates)}\n"
            "Pass the one you mean, e.g. "
            f"make <target> UPLOAD_PORT={devices[0]}\n"
            "Refusing to pick: choosing wrong here writes one board's image "
            "to another.")

    return devices[0]


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Resolve the USB upload port, or refuse")
    p.add_argument("--env", help="PlatformIO env the upload is for (messages only)")
    p.add_argument("--list", action="store_true", help="List serial devices and exit")
    p.add_argument("--by-id-dir", default=BY_ID_DIR,
                   help="Override the stable-name directory (tests)")
    p.add_argument("--origin", help="make's $(origin UPLOAD_PORT)")
    args = p.parse_args(argv)

    candidates = discover(args.by_id_dir)
    if args.list:
        print(describe(candidates))
        return 0

    requested = os.environ.get("UPLOAD_PORT", "").strip() or None
    print(resolve(requested, candidates, args.env, args.origin))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
