#!/usr/bin/env python3
"""First-run setup wizard for artoo_esp32.

Modes:
  - Default (no flags): writes user.mk with local build overrides only.
  - --wifi: writes src/secrets.h with WiFi credentials only.

Usage:
    make setup                         # user.mk
    make setup-wifi                    # src/secrets.h
    python3 tools/configure.py         # same as make setup
    python3 tools/configure.py --wifi  # WiFi credential flow

questionary is used for interactive prompts when installed.
Falls back to stdlib prompts on a fresh system — no pip install required.
Install optional deps: pip install -r tools/requirements.txt
"""
from __future__ import annotations

import argparse
import getpass
import os
import sys

# ---------------------------------------------------------------------------
# Repository layout
# ---------------------------------------------------------------------------

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
USER_MK_PATH = os.path.join(REPO_ROOT, "user.mk")
SECRETS_H_PATH = os.path.join(REPO_ROOT, "src", "secrets.h")

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------

DEFAULTS = {
    "OTA_IP": "10.0.0.22",
    "BUILD_ENV": "artoo_esp32",
    "UPLOAD_PORT": "/dev/ttyUSB0",
}

SECRETS_DEFAULTS = {
    "PA_AP_PASSWORD": "changeme123",
    "PA_STA_SSID": "",
    "PA_STA_PASSWORD": "",
}

# (display label, BUILD_ENV value)
AUDIO_BACKENDS = [
    ("DY-SV5W (default — confirmed hardware)", "artoo_esp32"),
    ("CHIRP", "artoo_esp32_chirp"),
    ("MP3 Trigger", "artoo_esp32_mp3trigger"),
]

# ---------------------------------------------------------------------------
# questionary — optional; stdlib fallback when absent
# ---------------------------------------------------------------------------

try:
    import questionary as _q
    _HAS_Q = True
except ImportError:
    _q = None  # type: ignore[assignment]
    _HAS_Q = False


def _select(message: str, choices: list[str], default: str) -> str | None:
    """Prompt the user to pick from a list. Returns None on cancellation."""
    if _HAS_Q:
        return _q.select(message, choices=choices, default=default).ask()
    print(message)
    for i, choice in enumerate(choices, 1):
        marker = " (default)" if choice == default else ""
        print(f"  {i}) {choice}{marker}")
    raw = input(f"Select [1-{len(choices)}] (Enter for default): ").strip()
    if not raw:
        return default
    if raw.isdigit() and 1 <= int(raw) <= len(choices):
        return choices[int(raw) - 1]
    return default


def _text(message: str, default: str) -> str | None:
    """Free-text prompt with default. Returns None on cancellation."""
    if _HAS_Q:
        return _q.text(message, default=default).ask()
    try:
        raw = input(f"{message} [{default}]: ").strip()
    except (EOFError, KeyboardInterrupt):
        return None
    return raw or default


def _password(message: str, default: str | None = None) -> str | None:
    """Masked password prompt. Returns None on cancellation."""
    if _HAS_Q:
        raw = _q.password(message).ask()
        if raw is None:
            return None
        if default is not None and raw == "":
            return default
        return raw
    prompt = message
    if default is not None:
        prompt += " [Enter keeps current]"
    prompt += ": "
    try:
        raw = getpass.getpass(prompt)
    except (EOFError, KeyboardInterrupt):
        return None
    if default is not None and raw == "":
        return default
    return raw


def _confirm(message: str, default: bool = True) -> bool | None:
    """Yes/no prompt. Returns None on cancellation."""
    if _HAS_Q:
        return _q.confirm(message, default=default).ask()
    hint = "Y/n" if default else "y/N"
    try:
        raw = input(f"{message} [{hint}]: ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        return None
    if not raw:
        return default
    return raw.startswith("y")


def _escape_cpp_string(value: str) -> str:
    """Escape a value for a C string literal."""
    return value.replace("\\", "\\\\").replace('"', '\\"')


def _validate_secret_value(value: str, label: str) -> bool:
    """Reject control characters that would break generated source."""
    if any(ch in value for ch in ("\n", "\r", "\x00")):
        print(f"Error: {label} contains unsupported control characters.", file=sys.stderr)
        return False
    return True


# ---------------------------------------------------------------------------
# user.mk helpers
# ---------------------------------------------------------------------------

def _read_user_mk() -> dict[str, str]:
    """Parse key=value pairs from user.mk. Returns empty dict if absent."""
    result: dict[str, str] = {}
    try:
        with open(USER_MK_PATH) as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" in line:
                    key, _, val = line.partition("=")
                    result[key.strip()] = val.strip()
    except OSError:
        pass
    return result


def _render_user_mk(ota_ip: str, build_env: str, upload_port: str) -> str:
    return (
        "# user.mk — generated by tools/configure.py; do not commit\n"
        f"OTA_IP      = {ota_ip}\n"
        f"BUILD_ENV   = {build_env}\n"
        f"UPLOAD_PORT = {upload_port}\n"
    )


# ---------------------------------------------------------------------------
# secrets.h helpers
# ---------------------------------------------------------------------------

def _read_secrets_h() -> dict[str, str]:
    """Read existing secrets if present; otherwise return defaults."""
    result = dict(SECRETS_DEFAULTS)
    try:
        with open(SECRETS_H_PATH) as fh:
            for line in fh:
                parts = line.strip().split(maxsplit=2)
                if len(parts) != 3 or parts[0] != "#define":
                    continue
                key = parts[1]
                if key not in result:
                    continue
                value = parts[2].strip()
                if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
                    value = value[1:-1]
                result[key] = value
    except OSError:
        pass
    return result


def _render_secrets_h(ap_password: str, sta_ssid: str, sta_password: str) -> str:
    return (
        "// =============================================================================\n"
        "// src/secrets.h\n"
        "//\n"
        "// Generated by tools/configure.py --wifi.\n"
        "// This file is gitignored and MUST stay local.\n"
        "// =============================================================================\n\n"
        "#pragma once\n\n"
        "// WiFi AP mode password (minimum 8 characters).\n"
        f"#define PA_AP_PASSWORD \"{_escape_cpp_string(ap_password)}\"\n\n"
        "// WiFi STA mode (optional — leave empty to skip STA connection).\n"
        f"#define PA_STA_SSID \"{_escape_cpp_string(sta_ssid)}\"\n"
        f"#define PA_STA_PASSWORD \"{_escape_cpp_string(sta_password)}\"\n"
    )


def _write_private_file(path: str, content: str) -> None:
    """Write with owner-only permissions to reduce credential exposure.

    Uses O_NOFOLLOW to atomically reject symlinks at open time, eliminating
    the TOCTOU race between a pre-open islink() check and the actual open().
    """
    # O_NOFOLLOW: if path is a symlink, os.open() raises OSError (ELOOP/ENOENT).
    # This is race-free unlike a separate islink() check.
    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o600)
    try:
        with os.fdopen(fd, "w") as fh:
            fh.write(content)
    finally:
        try:
            os.chmod(path, 0o600)
        except OSError:
            pass


def run_wifi_setup() -> int:
    """Run WiFi credential setup wizard (src/secrets.h)."""

    # --- Security banner ---
    print("WiFi credential setup")
    print("-" * 40)
    print("Credentials are written to src/secrets.h and compiled into the firmware.")
    print("This file is gitignored and set owner-read-only (0600).")
    print()
    print("Security reminders:")
    print("  - On a shared machine, set a unique AP password — other users can read")
    print("    your home directory.")
    print("  - Rotate credentials if the device is transferred or your network changes.")
    print("  - Use an isolated IoT VLAN for the STA SSID if possible.")
    print()

    if not _HAS_Q:
        print("Note: questionary not installed — using stdlib prompts.")
        print()

    existing = _read_secrets_h()
    if os.path.exists(SECRETS_H_PATH):
        print("src/secrets.h already exists (values hidden).")
        overwrite = _confirm("Overwrite with new WiFi credentials?", default=True)
        if overwrite is None or not overwrite:
            print("Cancelled. src/secrets.h unchanged.")
            return 1
        print()

    while True:
        ap_password = _password(
            "WiFi AP password (min 8 chars)",
            default=existing.get("PA_AP_PASSWORD", SECRETS_DEFAULTS["PA_AP_PASSWORD"]),
        )
        if ap_password is None:
            print("\nCancelled.")
            return 1
        if len(ap_password) < 8:
            print("Error: AP password must be at least 8 characters.", file=sys.stderr)
            continue
        if _validate_secret_value(ap_password, "AP password"):
            break

    sta_ssid = _text(
        "WiFi STA SSID (Enter keeps current; type - to clear/disable STA mode)",
        default=existing.get("PA_STA_SSID", ""),
    )
    if sta_ssid is None:
        print("\nCancelled.")
        return 1
    if sta_ssid == "-":
        sta_ssid = ""
    if not _validate_secret_value(sta_ssid, "STA SSID"):
        return 1

    if sta_ssid:
        sta_password = _password(
            "WiFi STA password (Enter keeps current; type - to set empty)",
            default=existing.get("PA_STA_PASSWORD", ""),
        )
        if sta_password is None:
            print("\nCancelled.")
            return 1
        if sta_password == "-":
            sta_password = ""
    else:
        sta_password = ""
    if not _validate_secret_value(sta_password, "STA password"):
        return 1

    content = _render_secrets_h(ap_password, sta_ssid, sta_password)
    print()
    print("Will write src/secrets.h with:")
    print(f"  AP password: set ({len(ap_password)} chars)")
    print(f"  STA SSID: {'<empty>' if not sta_ssid else sta_ssid}")
    if sta_ssid:
        print(f"  STA password: {'<empty>' if not sta_password else f'set ({len(sta_password)} chars)'}")
    else:
        print("  STA password: <empty>")
    print()
    print("Security note: credentials are never written to user.mk or command history.")
    print()

    confirmed = _confirm("Write src/secrets.h?", default=True)
    if confirmed is None or not confirmed:
        print("Cancelled.")
        return 1

    try:
        _write_private_file(SECRETS_H_PATH, content)
    except OSError as exc:
        print(f"Error writing {SECRETS_H_PATH}: {exc}", file=sys.stderr)
        return 1

    print()
    print(f"Written: {SECRETS_H_PATH}")
    print("Permissions set to owner-only where supported (0600).")
    return 0


# ---------------------------------------------------------------------------
# user.mk wizard entry point
# ---------------------------------------------------------------------------

def run() -> int:
    """Run the user.mk setup wizard. Returns process exit code."""

    # --- Warn if questionary is missing ---
    if not _HAS_Q:
        print("Note: questionary not installed — using plain prompts.")
        print("      Run 'pip install -r tools/requirements.txt' for a nicer experience.")
        print()

    # --- Check for existing user.mk ---
    existing = _read_user_mk()
    if existing:
        print("user.mk already exists with current settings:")
        for k, v in existing.items():
            print(f"  {k} = {v}")
        print()
        overwrite = _confirm("Overwrite?", default=True)
        if overwrite is None or not overwrite:
            print("Cancelled. user.mk unchanged.")
            return 1
        print()

    # --- Q1: Audio backend ---
    audio_labels = [label for label, _ in AUDIO_BACKENDS]
    default_build_env = existing.get("BUILD_ENV", DEFAULTS["BUILD_ENV"])

    default_label = next(
        (label for label, env in AUDIO_BACKENDS if env == default_build_env),
        audio_labels[0],
    )
    chosen_label = _select("Audio backend:", audio_labels, default=default_label)
    if chosen_label is None:
        print("\nCancelled.")
        return 1
    build_env = dict(AUDIO_BACKENDS)[chosen_label]

    # --- Q2: OTA IP ---
    ota_ip = _text(
        "OTA target IP address:",
        default=existing.get("OTA_IP", DEFAULTS["OTA_IP"]),
    )
    if ota_ip is None:
        print("\nCancelled.")
        return 1

    # --- Q3: USB upload port ---
    upload_port = _text(
        "USB upload port:",
        default=existing.get("UPLOAD_PORT", DEFAULTS["UPLOAD_PORT"]),
    )
    if upload_port is None:
        print("\nCancelled.")
        return 1

    # --- Q4: Confirm and write ---
    content = _render_user_mk(ota_ip, build_env, upload_port)
    print()
    print("Will write user.mk:")
    print()
    for line in content.splitlines():
        print(f"  {line}")
    print()
    print(
        "Note: user.mk is gitignored. "
        "Re-run 'make setup' to change settings at any time."
    )
    print()

    confirmed = _confirm("Write user.mk?", default=True)
    if confirmed is None or not confirmed:
        print("Cancelled.")
        return 1

    try:
        with open(USER_MK_PATH, "w") as fh:
            fh.write(content)
    except OSError as exc:
        print(f"Error writing {USER_MK_PATH}: {exc}", file=sys.stderr)
        return 1

    print()
    print(f"Written: {USER_MK_PATH}")
    print("Run 'make help' to see all available commands.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="artoo_esp32 local setup wizard")
    parser.add_argument(
        "--wifi",
        action="store_true",
        help="configure src/secrets.h (WiFi credentials)",
    )
    args = parser.parse_args(argv)
    if args.wifi:
        return run_wifi_setup()
    return run()


if __name__ == "__main__":
    sys.exit(main())
