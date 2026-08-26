#!/usr/bin/env python3
"""Check that all supported environments stay within build-size budgets.

Reads budgets from tools/build_budgets.json, builds each environment,
measures the resulting binary, and reports against the budget.
Fails if any environment exceeds its budget.
"""

import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUDGETS_FILE = ROOT / "tools" / "build_budgets.json"


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


def main():
    budgets = load_budgets()
    envs = budgets.get("envs", {})

    if not envs:
        print("ERROR: No environments in budgets file", file=sys.stderr)
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

        if actual_size is None:
            print(f"✗ {env_name}: BUILD FAILED", file=sys.stderr)
            all_ok = False
            results.append((env_name, None, flash_budget_bytes, False))
            continue

        # Check hard ceiling first (non-negotiable)
        over_ceiling = actual_size > flash_ceiling_bytes
        # Check soft budget (deliberate headroom)
        over_budget = actual_size > flash_budget_bytes
        status = "✓" if (not over_ceiling and not over_budget) else "✗"

        results.append((env_name, actual_size, flash_budget_bytes, not over_budget and not over_ceiling))

        if over_ceiling:
            # Hard ceiling violation
            overage = actual_size - flash_ceiling_bytes
            pct_used = (actual_size / flash_ceiling_bytes) * 100
            print(f"{status} {env_name}: {actual_size} bytes ({pct_used:.1f}%) - EXCEEDS HARD CEILING by {overage} bytes",
                  file=sys.stderr)
            all_ok = False
        elif over_budget:
            # Budget violation
            overage = actual_size - flash_budget_bytes
            pct_used = (actual_size / flash_budget_bytes) * 100
            print(f"{status} {env_name}: {actual_size} bytes ({pct_used:.1f}%) - OVER BUDGET by {overage} bytes",
                  file=sys.stderr)
            all_ok = False
        else:
            # Within budget
            headroom = flash_budget_bytes - actual_size
            pct_used = (actual_size / flash_budget_bytes) * 100
            print(f"{status} {env_name}: {actual_size} bytes ({pct_used:.1f}%) - {headroom} bytes headroom",
                  file=sys.stderr)

    print("", file=sys.stderr)
    print(f"Summary: {len([r for r in results if r[3]])} passed, "
          f"{len([r for r in results if not r[3]])} failed",
          file=sys.stderr)

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
