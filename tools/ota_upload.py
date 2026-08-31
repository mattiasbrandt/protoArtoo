#!/usr/bin/env python3
"""Run Espressif espota with a project-controlled transfer timeout.

The upstream espota.py upload loop hardcodes a 10 second socket timeout for each
firmware chunk. Seated protoArtoo OTA can legitimately take longer while the ESP32
is flash-writing under load, so this wrapper patches that constant in memory
without modifying the PlatformIO package on disk.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ota_board_guard  # noqa: E402  (board-identity pre-flight guard, #252 Finding 1)


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TIMEOUT_S = 60
DEFAULT_TRANSFER_TIMEOUT_S = 60


def _default_espota_path() -> Path:
    candidates: list[Path] = []

    core_dir = os.environ.get("PLATFORMIO_CORE_DIR")
    if core_dir:
        candidates.append(
            Path(core_dir) / "packages" / "framework-arduinoespressif32" / "tools" / "espota.py"
        )

    candidates.append(
        Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "espota.py"
    )

    for candidate in candidates:
        if candidate.exists():
            return candidate

    searched = ", ".join(str(candidate) for candidate in candidates)
    raise FileNotFoundError(f"could not find PlatformIO espota.py; searched: {searched}")


def _firmware_path(env_name: str) -> Path:
    return REPO_ROOT / ".pio" / "build" / env_name / "firmware.bin"


def _filesystem_path(env_name: str) -> Path:
    return REPO_ROOT / ".pio" / "build" / env_name / "littlefs.bin"


def _patched_espota_main(espota_path: Path, transfer_timeout: int):
    source = espota_path.read_text(encoding="utf-8")
    needle = "connection.settimeout(10)"
    patch_count = source.count(needle)
    if patch_count != 1:
        raise RuntimeError(
            f"expected one transfer timeout line in {espota_path}, found {patch_count}"
        )
    replacement = f"connection.settimeout({transfer_timeout})"
    patched = source.replace(needle, replacement, 1)

    namespace = {
        "__name__": "__protoartoo_patched_espota__",
        "__file__": str(espota_path),
    }
    exec(compile(patched, str(espota_path), "exec"), namespace)
    return namespace["main"]


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Upload protoArtoo firmware over ArduinoOTA with a longer transfer timeout."
    )
    parser.add_argument("--env", required=True, help="PlatformIO environment whose firmware.bin should be uploaded.")
    parser.add_argument("-i", "--host", required=True, help="ESP32 OTA IP address or mDNS host.")
    parser.add_argument(
        "-f",
        "--file",
        dest="firmware",
        help="Image path. Defaults to .pio/build/<env>/firmware.bin, or "
        ".pio/build/<env>/littlefs.bin with --spiffs.",
    )
    parser.add_argument(
        "-s",
        "--spiffs",
        action="store_true",
        help="Upload the LittleFS filesystem image instead of firmware.",
    )
    parser.add_argument(
        "--espota-path",
        help="Path to the PlatformIO espota.py implementation.",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT_S,
        help="Invitation/result timeout passed to espota.",
    )
    parser.add_argument(
        "--transfer-timeout",
        type=int,
        default=DEFAULT_TRANSFER_TIMEOUT_S,
        help="Per-chunk TCP receive timeout patched into espota's transfer loop.",
    )
    parser.add_argument("--port", type=int, default=3232, help="ESP32 ArduinoOTA port.")
    parser.add_argument("--host-ip", default="0.0.0.0", help="Local host bind address for espota.")
    parser.add_argument("--host-port", type=int, help="Local host port for espota.")
    parser.add_argument("--auth", default="", help="ArduinoOTA password, if configured.")
    parser.add_argument("--md5-target", action="store_true", help="Use MD5 password target mode.")
    parser.add_argument("--no-progress", action="store_true", help="Disable espota progress output.")
    parser.add_argument("--quiet", action="store_true", help="Disable espota debug output.")
    parser.add_argument(
        "--identity-port",
        type=int,
        default=ota_board_guard.DEFAULT_IDENTITY_PORT,
        help="HTTP port for the pre-flight /api/identity board check (the "
        "dashboard port, not --port/ArduinoOTA's 3232).",
    )
    parser.add_argument(
        "--identity-timeout",
        type=float,
        default=ota_board_guard.DEFAULT_IDENTITY_TIMEOUT_SECONDS,
        help="Seconds to wait for the pre-flight board check before refusing "
        "to push.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    # Board-identity guard, first thing, before any local build-artifact
    # lookup or network write: a wrong-board OTA push is destructive, and
    # nothing later in this function may run ahead of it (#252 Finding 1).
    try:
        reported_board = ota_board_guard.enforce_board_match(
            args.env,
            args.host,
            port=args.identity_port,
            timeout_seconds=args.identity_timeout,
        )
    except ota_board_guard.OtaBoardGuardError as error:
        sys.stderr.write(f"{error}\n")
        return 3
    print(
        f"OTA board check: {args.host} confirmed as board={reported_board!r}",
        flush=True,
    )

    espota_path = Path(args.espota_path) if args.espota_path else _default_espota_path()
    default_path = _filesystem_path(args.env) if args.spiffs else _firmware_path(args.env)
    image = Path(args.firmware) if args.firmware else default_path
    if not image.exists():
        kind = "Filesystem" if args.spiffs else "Firmware"
        build_target = "buildfs" if args.spiffs else "build"
        sys.stderr.write(f"{kind} image not found: {image}\n")
        sys.stderr.write(f"Build it first with: pio run -e {args.env} -t {build_target}\n")
        return 2

    espota_args = [
        "-i",
        args.host,
        "-I",
        args.host_ip,
        "-p",
        str(args.port),
        "-t",
        str(args.timeout),
        "-f",
        str(image),
    ]
    if args.spiffs:
        espota_args.append("-s")
    if args.host_port is not None:
        espota_args.extend(["-P", str(args.host_port)])
    if args.auth:
        espota_args.extend(["-a", args.auth])
    if args.md5_target:
        espota_args.append("--md5-target")
    if not args.quiet:
        espota_args.append("--debug")
    if not args.no_progress:
        espota_args.append("--progress")

    print(
        f"OTA upload: env={args.env} host={args.host} spiffs={args.spiffs} "
        f"timeout={args.timeout}s transfer_timeout={args.transfer_timeout}s",
        flush=True,
    )
    espota_main = _patched_espota_main(espota_path, args.transfer_timeout)
    return int(espota_main(espota_args))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
