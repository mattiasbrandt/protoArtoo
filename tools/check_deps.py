#!/usr/bin/env python3
# =============================================================================
# tools/check_deps.py
#
# Development environment dependency checker for protoArtoo.
#
# Checks that required OS commands and Python packages are present and prints
# platform-appropriate install hints for anything missing.
#
# Usage:
#   python3 tools/check_deps.py    # direct
#   make check-deps                # via Makefile
# =============================================================================

import shutil
import sys

RESET  = "\033[0m"
RED    = "\033[31m"
GREEN  = "\033[32m"
YELLOW = "\033[33m"
CYAN   = "\033[36m"
BOLD   = "\033[1m"


# -----------------------------------------------------------------------------
# _detect_wsl()
# Returns True when running inside Windows Subsystem for Linux (any version).
# -----------------------------------------------------------------------------
def _detect_wsl() -> bool:
    try:
        with open("/proc/version") as f:
            return "microsoft" in f.read().lower()
    except OSError:
        return False


def _ok(label: str) -> None:
    print(f"  {GREEN}ok{RESET}      {label}")


def _warn(label: str, hints: list[str]) -> None:
    print(f"  {YELLOW}optional{RESET}  {label}")
    for hint in hints:
        print(f"            {hint}")


def _missing(label: str, hints: list[str]) -> None:
    print(f"  {RED}MISSING{RESET}   {label}")
    for hint in hints:
        print(f"            {hint}")


# -----------------------------------------------------------------------------
# check_command()
# Returns True if the named command is on PATH.
# -----------------------------------------------------------------------------
def check_command(name: str, hints: list[str]) -> bool:
    if shutil.which(name):
        _ok(f"{name}  (command)")
        return True
    _missing(f"{name}  (command)", hints)
    return False


# -----------------------------------------------------------------------------
# check_python_pkg()
# Returns True if the package can be imported.
# optional=True treats absence as a warning, not an error.
# -----------------------------------------------------------------------------
def check_python_pkg(import_name: str, display_name: str,
                     hints: list[str], optional: bool = False) -> bool:
    try:
        __import__(import_name)
        _ok(f"{display_name}  (python package)")
        return True
    except ImportError:
        if optional:
            _warn(f"{display_name}  (python package)", hints)
            return True  # not a hard failure
        _missing(f"{display_name}  (python package)", hints)
        return False


def main() -> None:
    is_wsl = _detect_wsl()

    print(f"\n{BOLD}protoArtoo — dependency check{RESET}")
    print(f"Python {sys.version.split()[0]}")
    if is_wsl:
        print(f"{CYAN}Environment: Windows Subsystem for Linux (WSL){RESET}")
    print()

    # -------------------------------------------------------------------------
    # WSL notice — USB serial devices need Windows-side passthrough.
    # Print once, up front, so it is not missed.
    # -------------------------------------------------------------------------
    if is_wsl:
        print(f"{CYAN}{BOLD}WSL notice — USB serial passthrough{RESET}")
        print(f"{CYAN}  USB serial devices (e.g. /dev/ttyUSB0) are NOT visible in WSL by default.{RESET}")
        print(f"{CYAN}  'make monitor' and 'make flash' require attaching the device first:{RESET}")
        print(f"{CYAN}    1. On Windows: install usbipd-win{RESET}")
        print(f"{CYAN}       winget install --interactive --exact dorssel.usbipd-win{RESET}")
        print(f"{CYAN}    2. In an elevated Windows terminal:{RESET}")
        print(f"{CYAN}       usbipd list                     <- find your device (e.g. CP210x){RESET}")
        print(f"{CYAN}       usbipd bind --busid <BUSID>     <- once per device{RESET}")
        print(f"{CYAN}       usbipd attach --wsl --busid <BUSID>{RESET}")
        print(f"{CYAN}    3. Back in WSL: ls /dev/ttyUSB* should now show the device.{RESET}")
        print(f"{CYAN}    Docs: https://learn.microsoft.com/windows/wsl/connect-usb{RESET}")
        print()

    issues = 0

    # -------------------------------------------------------------------------
    # OS commands
    # -------------------------------------------------------------------------
    print(f"{BOLD}Commands:{RESET}")
    if not check_command(
        "pio",
        [
            "Install PlatformIO CLI (pick one):",
            "  pipx install platformio         <- recommended (isolated, no PATH noise)",
            "    Fedora/RHEL:    sudo dnf install pipx  &&  pipx install platformio",
            "    Debian/Ubuntu:  sudo apt install pipx  &&  pipx install platformio",
            "    macOS:          brew install pipx      &&  pipx install platformio",
            "  pip install platformio",
            "  https://docs.platformio.org/en/latest/core/installation/",
            "After install, restart your shell so 'pio' is on PATH.",
        ],
    ):
        issues += 1

    # -------------------------------------------------------------------------
    # Python packages
    # -------------------------------------------------------------------------
    print(f"\n{BOLD}Python packages:{RESET}")

    # pyserial — required for tools/console_client.py (make monitor)
    if not check_python_pkg(
        "serial",
        "pyserial",
        [
            "Required for: tools/console_client.py  (make monitor)",
            "  Fedora/RHEL:    sudo dnf install python3-pyserial",
            "  Debian/Ubuntu:  sudo apt install python3-serial",
            "  pip install pyserial",
            "  pip install -r tools/requirements.txt  (installs all project tools at once)",
        ],
    ):
        issues += 1

    # questionary — optional; configure.py falls back to plain input() without it
    check_python_pkg(
        "questionary",
        "questionary",
        [
            "Optional for: tools/configure.py  (make setup / make setup-wifi)",
            "  Falls back to plain input() prompts if absent — not required.",
            "  Not available via dnf/apt — install with pip:",
            "    pip install questionary",
            "    pip install --user questionary      <- user-local, no sudo needed",
            "    pip install -r tools/requirements.txt  (all project tools at once)",
            "  In a virtual env:  source .venv/bin/activate && pip install questionary",
        ],
        optional=True,
    )

    # -------------------------------------------------------------------------
    # Summary
    # -------------------------------------------------------------------------
    if issues:
        print(
            f"\n{RED}{BOLD}{issues} required "
            f"{'dependency' if issues == 1 else 'dependencies'} missing.{RESET}"
        )
        print(f"Fix the items above, then re-run:  {BOLD}make check-deps{RESET}\n")
        sys.exit(1)
    else:
        print(f"\n{GREEN}{BOLD}All required dependencies present.{RESET}\n")
        sys.exit(0)


if __name__ == "__main__":
    main()
