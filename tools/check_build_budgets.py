#!/usr/bin/env python3
"""Check that all supported environments stay within build-size budgets.

Reads budgets from tools/build_budgets.json, builds each environment,
measures the resulting binary, and reports against the budget.
Fails if any environment exceeds its budget.

Environments that declare fs_budget_bytes are also imaged with `-t buildfs` and
measured, so the LittleFS image is budgeted the same way flash is. That check
exists because the filesystem had none: its size was hand-measured in tickets,
and both in-tree size comments had drifted years out of date without anything
noticing (#382, ADR 0065).
"""

import json
import os
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUDGETS_FILE = ROOT / "tools" / "build_budgets.json"

# LittleFS allocates in blocks of this size on both boards, and rounds every
# file up to one. It is why the image costs far more than its files add up to:
# measured on main, 40 files totalling 299,320 B occupy 397,312 B of blocks.
LITTLEFS_BLOCK_SIZE = 4096
ERASED_BLOCK = b"\xff" * LITTLEFS_BLOCK_SIZE


def load_budgets():
    """Load budgets from JSON file."""
    if not BUDGETS_FILE.exists():
        print(f"ERROR: Budget file not found: {BUDGETS_FILE}", file=sys.stderr)
        sys.exit(1)

    with open(BUDGETS_FILE) as f:
        return json.load(f)


def platform_for_env(env_name, registry):
    """Resolve an env to its platform spec. Explicit membership wins;
    everything else falls to the platform marked default."""
    default = None
    for key, spec in registry["platforms"].items():
        if env_name in spec.get("envs", []):
            return key, spec
        if spec.get("default"):
            if default is not None:
                raise ValueError("more than one default platform in registry")
            default = (key, spec)
    if default is None:
        raise ValueError(f"no platform for {env_name} and no default")
    return default


def get_platformio_core_dir(env_name, budgets):
    """Determine PLATFORMIO_CORE_DIR for an environment using the platforms registry."""
    if "platforms" not in budgets:
        raise ValueError("no platforms registry in build_budgets.json")
    _, spec = platform_for_env(env_name, budgets)
    return os.path.expanduser(spec["core_dir"])


def build_environment(env_name, budgets):
    """Build an environment and return the binary size in bytes, or None on error."""
    print(f"Building {env_name}...", file=sys.stderr)

    core_dir = get_platformio_core_dir(env_name, budgets)
    env = os.environ.copy()
    env["PLATFORMIO_CORE_DIR"] = core_dir

    try:
        result = subprocess.run(
            ["pio", "run", "-e", env_name],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=1800,
            env=env
        )

        if result.returncode != 0:
            print(f"  FAILED: pio run exited with code {result.returncode}", file=sys.stderr)
            return None

        # Get binary size
        bin_file = ROOT / ".pio" / "build" / env_name / "firmware.bin"
        if not bin_file.exists():
            print(f"  FAILED: No firmware.bin found at {bin_file}", file=sys.stderr)
            return None

        size = bin_file.stat().st_size
        print(f"  OK: {size} bytes", file=sys.stderr)
        return size

    except subprocess.TimeoutExpired:
        print(f"  FAILED: Build timed out", file=sys.stderr)
        return None
    except Exception as e:
        print(f"  FAILED: {e}", file=sys.stderr)
        return None


def filesystem_image_bytes(env_name, budgets):
    """Image the filesystem and return the bytes it actually allocates, or None.

    mklittlefs writes an image the full size of the partition, so littlefs.bin's
    own file size is the partition size and says nothing whatever about usage --
    quoting it as the measurement is the trap this function exists to close. A
    block that has never been written is left erased, all 0xFF, so what the
    filesystem costs is the count of blocks that are not.
    """
    print(f"Imaging filesystem for {env_name}...", file=sys.stderr)

    core_dir = get_platformio_core_dir(env_name, budgets)
    env = os.environ.copy()
    env["PLATFORMIO_CORE_DIR"] = core_dir

    try:
        result = subprocess.run(
            ["pio", "run", "-e", env_name, "-t", "buildfs"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=600,
            env=env,
        )

        if result.returncode != 0:
            print(f"  FAILED: pio run -t buildfs exited with code {result.returncode}", file=sys.stderr)
            return None

        image = ROOT / ".pio" / "build" / env_name / "littlefs.bin"
        if not image.exists():
            print(f"  FAILED: No littlefs.bin found at {image}", file=sys.stderr)
            return None

        data = image.read_bytes()
        if len(data) % LITTLEFS_BLOCK_SIZE:
            print(f"  FAILED: {image} is {len(data)} bytes, not a whole number of "
                  f"{LITTLEFS_BLOCK_SIZE}-byte blocks", file=sys.stderr)
            return None

        allocated = sum(
            1
            for i in range(len(data) // LITTLEFS_BLOCK_SIZE)
            if data[i * LITTLEFS_BLOCK_SIZE:(i + 1) * LITTLEFS_BLOCK_SIZE] != ERASED_BLOCK
        )
        size = allocated * LITTLEFS_BLOCK_SIZE
        print(f"  OK: {size} bytes in {allocated} blocks", file=sys.stderr)
        return size

    except subprocess.TimeoutExpired:
        print("  FAILED: filesystem image timed out", file=sys.stderr)
        return None
    except Exception as e:
        print(f"  FAILED: {e}", file=sys.stderr)
        return None


def check_one(kind, env_name, actual, budget, ceiling, results):
    """Report one measurement against its ceiling and budget. Returns True if ok.

    The ceiling is the partition and is not negotiable; the budget is the
    ratchet, raised deliberately with its arithmetic recorded in
    budget_rationale. Both are reported the same way for flash and filesystem so
    one reading habit covers the pair.
    """
    label = env_name if kind == "flash" else f"{env_name} fs"

    if actual is None:
        print(f"\u2717 {label}: BUILD FAILED", file=sys.stderr)
        results.append((label, None, budget, False))
        return False

    over_ceiling = actual > ceiling
    over_budget = actual > budget
    ok = not over_ceiling and not over_budget
    status = "\u2713" if ok else "\u2717"
    results.append((label, actual, budget, ok))

    if over_ceiling:
        pct = (actual / ceiling) * 100
        print(f"{status} {label}: {actual} bytes ({pct:.1f}%) - EXCEEDS HARD CEILING "
              f"by {actual - ceiling} bytes", file=sys.stderr)
    elif over_budget:
        pct = (actual / budget) * 100
        print(f"{status} {label}: {actual} bytes ({pct:.1f}%) - OVER BUDGET "
              f"by {actual - budget} bytes", file=sys.stderr)
    else:
        pct = (actual / budget) * 100
        print(f"{status} {label}: {actual} bytes ({pct:.1f}%) - {budget - actual} bytes headroom",
              file=sys.stderr)
    return ok


def main():
    # --exclude keeps an environment's budget on the books while taking its
    # build off a caller's path. AGENTS.md "Verification Scale": a check earns
    # its place on every pull request by catching something a pull request can
    # break. firebeetle2_bringup is a bench sketch that excludes all of src/,
    # so no ordinary change can move its size, and building it cost 5m19s of
    # every run. It is still measured where it matters - on main, and before a
    # release - just not on each iteration of a branch.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--exclude",
        default="",
        metavar="ENV[,ENV...]",
        help="comma-separated environments to skip; their budgets stay in the file",
    )
    args = parser.parse_args()
    excluded = {e.strip() for e in args.exclude.split(",") if e.strip()}

    budgets = load_budgets()
    envs = budgets.get("envs", {})

    if not envs:
        print("ERROR: No environments in budgets file", file=sys.stderr)
        sys.exit(1)

    unknown = excluded - set(envs)
    if unknown:
        # A typo in --exclude must not silently check everything: that is how a
        # budget quietly stops being enforced.
        print(f"ERROR: --exclude names unknown environments: {sorted(unknown)}", file=sys.stderr)
        sys.exit(1)

    envs = {k: v for k, v in envs.items() if k not in excluded}
    if excluded:
        print(f"Skipping {len(excluded)} environment(s): {', '.join(sorted(excluded))}", file=sys.stderr)

    if not envs:
        print("ERROR: every environment was excluded", file=sys.stderr)
        sys.exit(1)

    print(f"Checking {len(envs)} environments...\n", file=sys.stderr)

    all_ok = True
    results = []

    for env_name in sorted(envs.keys()):
        env_budget = envs[env_name]
        flash_ceiling_bytes = env_budget.get("flash_ceiling_bytes")
        flash_budget_bytes = env_budget.get("flash_budget_bytes")

        if flash_budget_bytes is None:
            print(f"WARNING: {env_name} has no flash_budget_bytes", file=sys.stderr)
            continue

        if flash_ceiling_bytes is None:
            print(f"WARNING: {env_name} has no flash_ceiling_bytes", file=sys.stderr)
            continue

        actual_size = build_environment(env_name, budgets)
        if not check_one("flash", env_name, actual_size, flash_budget_bytes,
                         flash_ceiling_bytes, results):
            all_ok = False

        fs_budget_bytes = env_budget.get("fs_budget_bytes")
        fs_ceiling_bytes = env_budget.get("fs_ceiling_bytes")
        if fs_budget_bytes is None or fs_ceiling_bytes is None:
            # Not every environment images a filesystem -- a bench sketch that
            # excludes src/ has no web UI to carry -- so a missing pair is a
            # quiet skip rather than the warning a missing flash budget earns.
            continue

        # Measured even when the firmware build failed: the two artifacts are
        # independent, and a broken build must not hide a filesystem regression.
        if not check_one("fs", env_name, filesystem_image_bytes(env_name, budgets),
                         fs_budget_bytes, fs_ceiling_bytes, results):
            all_ok = False

    print("", file=sys.stderr)
    print(f"Summary: {len([r for r in results if r[3]])} passed, "
          f"{len([r for r in results if not r[3]])} failed",
          file=sys.stderr)

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
