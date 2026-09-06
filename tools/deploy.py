#!/usr/bin/env python3
"""protoArtoo — interactive build & deploy wizard.

Run via:  make          (default target)
          python3 tools/deploy.py

questionary is required for arrow-key menus:
  pip install -r tools/requirements.txt

Falls back to plain numbered prompts when questionary is absent.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import threading

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OTA_TIMEOUT_SECONDS = "60"
OTA_TRANSFER_TIMEOUT_SECONDS = "60"
# Local (host) TCP port espota listens on for the device's OTA connect-back.
# Mirrors the Makefile's OTA_HOST_PORT default (#252 Finding 2) so the wizard
# and `make ota` behave identically on a default-deny inbound firewall.
OTA_HOST_PORT_DEFAULT = "32320"

# ── ANSI helpers ─────────────────────────────────────────────────────────────

RESET  = "\033[0m"
BOLD   = "\033[1m"
DIM    = "\033[2m"
RED    = "\033[31m"
GREEN  = "\033[32m"
YELLOW = "\033[33m"
CYAN   = "\033[36m"

def _c(*codes: str, text: str) -> str:
    return "".join(codes) + text + RESET

def dim(t: str)  -> str: return _c(DIM, text=t)
def bold(t: str) -> str: return _c(BOLD, text=t)
def ok(t: str)   -> str: return _c(GREEN, BOLD, text=t)
def err(t: str)  -> str: return _c(RED, BOLD, text=t)
def warn(t: str) -> str: return _c(YELLOW, text=t)
def info(t: str) -> str: return _c(CYAN, text=t)

SEP = dim("─" * 52)

# ── questionary / stdlib fallback ────────────────────────────────────────────

try:
    import questionary as _q
    _HAS_Q = True
except ImportError:
    _q = None   # type: ignore[assignment]
    _HAS_Q = False


def _select(message: str, choices: list[str], default: str | None = None) -> str | None:
    if _HAS_Q:
        return _q.select(message, choices=choices, default=default).ask()
    print(f"\n{message}")
    for i, c in enumerate(choices, 1):
        marker = info("  ← default") if c == default else ""
        print(f"  {dim(str(i) + ')')} {c}{marker}")
    raw = input(f"  Select [1–{len(choices)}] or Enter for default: ").strip()
    if not raw:
        return default
    if raw.isdigit() and 1 <= int(raw) <= len(choices):
        return choices[int(raw) - 1]
    return default


def _text(message: str, default: str) -> str | None:
    if _HAS_Q:
        return _q.text(message, default=default).ask()
    raw = input(f"  {message} [{default}]: ").strip()
    return raw or default


def _confirm(message: str, default: bool = True) -> bool | None:
    if _HAS_Q:
        return _q.confirm(message, default=default).ask()
    hint = "Y/n" if default else "y/N"
    raw = input(f"  {message} [{hint}]: ").strip().lower()
    if not raw:
        return default
    return raw.startswith("y")


# ── Subprocess runner ─────────────────────────────────────────────────────────

def run_cmd(args: list[str]) -> int:
    """Stream stdout + stderr in dim grey.
    On non-zero exit, re-print captured stderr highlighted so errors are
    easy to spot without scrolling back through build output.
    """
    stderr_lines: list[str] = []

    proc = subprocess.Popen(
        args,
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

    def _drain_out() -> None:
        assert proc.stdout
        for line in proc.stdout:
            sys.stdout.write(dim("  " + line.rstrip("\n")) + "\n")
            sys.stdout.flush()

    def _drain_err() -> None:
        assert proc.stderr
        for line in proc.stderr:
            stripped = line.rstrip("\n")
            stderr_lines.append(stripped)
            sys.stdout.write(dim("  " + stripped) + "\n")
            sys.stdout.flush()

    t_out = threading.Thread(target=_drain_out, daemon=True)
    t_err = threading.Thread(target=_drain_err, daemon=True)
    t_out.start(); t_err.start()
    t_out.join();  t_err.join()
    proc.wait()

    # On failure, re-highlight stderr so the relevant errors are obvious.
    if proc.returncode != 0 and stderr_lines:
        print()
        print(warn("── error output " + "─" * 35))
        for line in stderr_lines:
            if re.search(r"\berror\b", line, re.IGNORECASE):
                print(err("  " + line))
            elif re.search(r"\bwarning\b", line, re.IGNORECASE):
                print(warn("  " + line))
            else:
                print(dim("  " + line))
        print(warn("─" * 52))

    return proc.returncode


# ── Build matrix ──────────────────────────────────────────────────────────────

# (display label, env for USB/build, env for OTA)
AUDIO_MODULES: list[tuple[str, str, str]] = [
    ("🔊  DY-SV5W  (default)",  "artoo_esp32",           "artoo_esp32_ota"),
    ("🎵  CHIRP",               "artoo_esp32_chirp",      "artoo_esp32_chirp_ota"),
    ("🎙  MP3 Trigger",         "artoo_esp32_mp3trigger", "artoo_esp32_mp3trigger_ota"),
]

ACTIONS: list[tuple[str, str]] = [
    ("📡  Flash via OTA   (WiFi — no cables needed)",  "ota"),
    ("🔌  Flash via USB",                              "usb"),
    ("🔨  Build only      (compile, no flash)",        "build"),
    ("🧪  Run tests only",                             "test"),
    ("🌐  Upload web UI   (OTA filesystem only)",      "uploadfs"),
]


# ── user.mk reader ────────────────────────────────────────────────────────────

def _user_mk(key: str, default: str) -> str:
    try:
        with open(os.path.join(REPO_ROOT, "user.mk")) as fh:
            for line in fh:
                k, _, v = line.partition("=")
                if k.strip() == key:
                    return v.strip()
    except OSError:
        pass
    return default


# ── Wizard ────────────────────────────────────────────────────────────────────

def main() -> int:
    # Enable ANSI on Windows terminal; no-op on Linux/macOS
    os.system("")

    if not _HAS_Q:
        print(warn("\n⚠  questionary not installed — using plain prompts."))
        print(info("   pip install -r tools/requirements.txt  for arrow-key menus.\n"))

    print()
    print(bold("🤖  protoArtoo  —  build & deploy"))
    print(SEP)

    # ── Q1: Action ──
    print()
    action_label = _select(
        "What do you want to do?",
        choices=[a[0] for a in ACTIONS],
    )
    if action_label is None:
        print(dim("\nCancelled."))
        return 1
    action = dict(ACTIONS)[action_label]

    # ── Q2: Audio module (skip for test-only) ──
    env_usb = env_ota = "artoo_esp32"
    if action != "test":
        current_env  = _user_mk("BUILD_ENV", "artoo_esp32")
        default_audio = next(
            (a[0] for a in AUDIO_MODULES if a[1] == current_env),
            AUDIO_MODULES[0][0],
        )
        audio_label = _select(
            "Audio module:",
            choices=[a[0] for a in AUDIO_MODULES],
            default=default_audio,
        )
        if audio_label is None:
            print(dim("\nCancelled."))
            return 1
        _, env_usb, env_ota = next(a for a in AUDIO_MODULES if a[0] == audio_label)

    # ── Q3: Connection details ──
    ota_ip     = _user_mk("OTA_IP",      "artoo.local")
    upload_port = _user_mk("UPLOAD_PORT", "/dev/ttyUSB0")
    ota_host_port = _user_mk("OTA_HOST_PORT", OTA_HOST_PORT_DEFAULT)

    if action in ("ota", "uploadfs"):
        ota_ip = _text("OTA target (IP or mDNS host):", default=ota_ip) or ota_ip

    if action == "usb":
        upload_port = _text("USB port:", default=upload_port) or upload_port

    # ── Q4: Gate tests before flash? ──
    run_tests = False
    if action in ("ota", "usb"):
        print()
        run_tests = _confirm("🧪  Run unit tests before flash?  (recommended)", default=True) or False

    print()
    print(SEP)

    # ── Execute ───────────────────────────────────────────────────────────────

    # Tests
    if action == "test" or run_tests:
        print()
        print(bold("🧪  Running unit tests…"))
        print()
        rc = run_cmd(["pio", "test", "-e", "native"])
        if rc != 0:
            print()
            print(err(f"✗  Tests failed  (exit {rc})"))
            if action in ("ota", "usb"):
                print(err("   Flash aborted — fix tests first."))
            return rc
        print()
        print(ok("✅  Tests passed"))
        if action == "test":
            print()
            return 0

    # Build only
    if action == "build":
        print()
        print(bold(f"🔨  Building  [{env_usb}]…"))
        print()
        rc = run_cmd(["pio", "run", "-e", env_usb])
        if rc != 0:
            print()
            print(err(f"✗  Build failed  (exit {rc})"))
            return rc
        print()
        print(ok("✅  Build succeeded"))
        print()
        return 0

    # Upload filesystem
    if action == "uploadfs":
        print()
        print(bold(f"🌐  Uploading web UI  →  {ota_ip}…"))
        print()
        rc = run_cmd(["pio", "run", "-e", env_ota, "-t", "uploadfs", "--upload-port", ota_ip])
        if rc != 0:
            print()
            print(err(f"✗  Filesystem upload failed  (exit {rc})"))
            return rc
        print()
        print(ok(f"✅  Web UI uploaded  →  http://{ota_ip}"))
        print()
        return 0

    # OTA flash
    if action == "ota":
        print()
        print(bold(f"📡  Building OTA firmware  [{env_ota}]…"))
        print()
        rc = run_cmd(["pio", "run", "-e", env_ota])
        if rc != 0:
            print()
            print(err(f"✗  OTA build failed  (exit {rc})"))
            return rc
        print()
        print(bold(f"📡  Flashing via OTA  [{env_ota}]  →  {ota_ip}…"))
        print()
        rc = run_cmd([
            sys.executable,
            "tools/ota_upload.py",
            "--env",
            env_ota,
            "--host",
            ota_ip,
            "--timeout",
            OTA_TIMEOUT_SECONDS,
            "--transfer-timeout",
            OTA_TRANSFER_TIMEOUT_SECONDS,
            "--host-port",
            ota_host_port,
        ])
        if rc != 0:
            print()
            print(err(f"✗  OTA flash failed  (exit {rc})"))
            return rc
        print()
        print(ok(f"✅  Firmware flashed  →  http://{ota_ip}"))
        print()
        return 0

    # USB flash
    if action == "usb":
        print()
        print(bold(f"🔌  Flashing via USB  [{env_usb}]  →  {upload_port}…"))
        print()
        rc = run_cmd(["pio", "run", "-e", env_usb, "-t", "upload", "--upload-port", upload_port])
        if rc != 0:
            print()
            print(err(f"✗  USB flash failed  (exit {rc})"))
            return rc
        print()
        print(ok("✅  Firmware flashed via USB"))
        print()
        return 0

    return 0


if __name__ == "__main__":
    sys.exit(main())
