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

Every environment that records boot_heap_baseline_bytes also has its boot heap
figure checked: static .data + .bss plus the stack and TCB of every task each
boot on that chip creates, against the baseline plus boot_heap_threshold_bytes.
The arithmetic is tools/slice_verify.py's, imported, so the gate (which builds
artoo_esp32 only) and this script (which builds every env, the ESP32-P4 too)
cannot disagree. The figure is not runtime heap; see slice_verify.boot_heap_bytes.
There is no ACK here: growth past the threshold is either given back or a
deliberate, operator-approved re-stamp of the baseline (#468).

Every pio run's whole output, success or failure, is kept in
build-logs/<env>-<phase>.log, and a failed or timed-out run prints its last
lines and the log's path. The script used to discard that output, so CI said
only "pio run exited with code 1" for weeks while the firebeetle2 build was
red on main (#473).

After each env's firmware build, and again after its `buildfs`, the env's
framework envelope is checked (tools/check_framework_envelope.py): a `buildfs`
can half-run the shared pool's rebuild and exit 0 with the budget still
passing, which is how the 2026-08-29 image went out pristine.

`--env <name>` (repeatable) builds only the named envs, so each CI board job
builds its own. `--summary <path>` writes every row as a Markdown table --
flash and filesystem headroom, the boot heap against its limit, the envelope
-- for the Verification summary and pull request comment. It is written
whatever the verdict, a failed build included.
"""

import argparse
import contextlib
import json
import os
import subprocess
import sys
from pathlib import Path

import check_framework_envelope  # tools/, beside this script
import pio_lock  # tools/, beside this script
import slice_verify  # tools/, beside this script: the boot heap arithmetic

ROOT = Path(__file__).resolve().parents[1]
BUDGETS_FILE = ROOT / "tools" / "build_budgets.json"
BUILD_LOGS = ROOT / "build-logs"

# Lines of a failed run's log printed to stderr. A compile error usually sits
# above the linker's and scons' closing lines, so this is generous; the whole
# log is on disk (and uploaded by CI) when it is not enough.
FAILURE_TAIL_LINES = 100

# LittleFS allocates in blocks of this size on both boards, and rounds every
# file up to one. It is why the image costs far more than its files add up to:
# measured on main, 40 files totalling 299,320 B occupy 397,312 B of blocks.
LITTLEFS_BLOCK_SIZE = 4096
ERASED_BLOCK = b"\xff" * LITTLEFS_BLOCK_SIZE


def _bytes(n):
    return f"{n:,} B"


def summary_row(label, ok, measured="", limit="", headroom=""):
    """One row of the --summary table: what was measured, against what, and
    how much room is left. Every check_* function appends exactly one."""
    return (label, ok, measured, limit, headroom)


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


def run_pio(cmd, env, timeout, env_name, phase):
    """Run one pio command under the build lock, keeping its whole output.

    Writes stdout and stderr, interleaved as pio wrote them, to
    build-logs/<env>-<phase>.log whatever the outcome. Returns the exit
    code, None on a timeout. On a non-zero exit or a timeout the log's last
    FAILURE_TAIL_LINES lines and its path go to stderr: pio prints a compile
    or link error on stdout, so a stderr-only report shows nothing.
    """
    log = BUILD_LOGS / f"{env_name}-{phase}.log"
    log.parent.mkdir(exist_ok=True)
    # The framework pool is shared by every worktree and rebuilt in place,
    # so a build outside the machine-wide lock can strand it (AGENTS.md
    # "The build lock"). Waiting for the lock is not part of the timeout.
    with pio_lock.build_lock(cmd, env=env):
        try:
            result = subprocess.run(
                cmd,
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=timeout,
                env=env,
            )
            out, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as e:
            # CPython hands back what was captured before the kill as bytes,
            # even with text=True (TimeoutExpired.stdout is the raw buffer).
            raw = e.stdout or b""
            out = raw.decode(errors="replace") if isinstance(raw, bytes) else raw
            code = None
    log.write_text(out)
    if code != 0:
        print("\n".join(out.splitlines()[-FAILURE_TAIL_LINES:]), file=sys.stderr)
        print(f"  full log: {log}", file=sys.stderr)
    return code


def build_environment(env_name, budgets):
    """Build an environment and return the binary size in bytes, or None on error."""
    print(f"Building {env_name}...", file=sys.stderr)

    core_dir = get_platformio_core_dir(env_name, budgets)
    env = os.environ.copy()
    env["PLATFORMIO_CORE_DIR"] = core_dir

    cmd = ["pio", "run", "-e", env_name]
    try:
        code = run_pio(cmd, env, 1800, env_name, "firmware")
        if code is None:
            print("  FAILED: Build timed out", file=sys.stderr)
            return None
        if code != 0:
            print(f"  FAILED: pio run exited with code {code}", file=sys.stderr)
            return None

        # Get binary size
        bin_file = ROOT / ".pio" / "build" / env_name / "firmware.bin"
        if not bin_file.exists():
            print(f"  FAILED: No firmware.bin found at {bin_file}", file=sys.stderr)
            return None

        size = bin_file.stat().st_size
        print(f"  OK: {size} bytes", file=sys.stderr)
        return size

    except Exception as e:
        print(f"  FAILED: {e}", file=sys.stderr)
        return None


def filesystem_image_bytes(env_name, budgets):
    """Image the filesystem and return the bytes it actually allocates, or None.

    The builder (littlefs-python, in the platform's build_fs_image) writes an
    image the full size of the partition, so littlefs.bin's
    own file size is the partition size and says nothing whatever about usage --
    quoting it as the measurement is the trap this function exists to close. A
    block that has never been written is left erased, all 0xFF, so what the
    filesystem costs is the count of blocks that are not.

    Pricing a directory that is not this build's image, or the same bytes in
    another write order, is tools/fs_price.py. That number is a question.
    This function is still the one that can fail a build.
    """
    print(f"Imaging filesystem for {env_name}...", file=sys.stderr)

    core_dir = get_platformio_core_dir(env_name, budgets)
    env = os.environ.copy()
    env["PLATFORMIO_CORE_DIR"] = core_dir

    cmd = ["pio", "run", "-e", env_name, "-t", "buildfs"]
    try:
        code = run_pio(cmd, env, 600, env_name, "buildfs")
        if code is None:
            print("  FAILED: filesystem image timed out", file=sys.stderr)
            return None
        if code != 0:
            print(f"  FAILED: pio run -t buildfs exited with code {code}", file=sys.stderr)
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
        results.append(summary_row(label, False, "build failed", _bytes(budget)))
        return False

    over_ceiling = actual > ceiling
    over_budget = actual > budget
    ok = not over_ceiling and not over_budget
    status = "\u2713" if ok else "\u2717"
    measured = _bytes(actual)
    if kind == "fs":
        measured += f" ({actual // LITTLEFS_BLOCK_SIZE} blocks)"
    results.append(summary_row(label, ok, measured, _bytes(budget), f"{budget - actual:,} B"))

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


def check_envelope(env_name, after, results):
    """Check the env's framework envelope held after `after`. Returns True if ok.

    Its report goes to stderr with the rest of this script's, so the CI log
    reads in order. An env that declares no custom_sdkconfig passes: there is
    nothing pioarduino could have failed to apply.
    """
    label = f"{env_name} envelope after {after}"
    try:
        with contextlib.redirect_stdout(sys.stderr):
            ok = check_framework_envelope.check(env_name, quiet=True) == 0
    except Exception as e:
        print(f"  FAILED: envelope check for {env_name}: {e}", file=sys.stderr)
        ok = False
    results.append(summary_row(label, ok, "held" if ok else "did not hold"))
    status = "\u2713" if ok else "\u2717"
    print(f"{status} {label}", file=sys.stderr)
    return ok


def boot_heap_figure(env_name, budgets):
    """The env's boot heap figure from the ELF the build just linked, or None."""
    elf = ROOT / ".pio" / "build" / env_name / "firmware.elf"
    try:
        chip, spec = slice_verify.platform_for_env(env_name, budgets)
        static_ram = slice_verify.measure_static_ram(elf, spec)
        recipes = json.loads(slice_verify.TASK_RECIPES.read_text(encoding="utf-8"))
        return slice_verify.boot_heap_bytes(static_ram, chip, recipes)
    except Exception as e:
        print(f"  FAILED: boot heap figure for {env_name}: {e}", file=sys.stderr)
        return None


def check_boot_heap(env_name, env_budget, figure, results):
    """Report the boot heap figure against its baseline. Returns True if ok."""
    label = f"{env_name} boot heap"
    if figure is None:
        print(f"\u2717 {label}: NOT MEASURED", file=sys.stderr)
        results.append(summary_row(label, False, "not measured"))
        return False
    ok, detail, notes = slice_verify.boot_heap_verdict(figure, env_budget, None)
    # The limit is the baseline plus its threshold: growth up to the threshold
    # passes (slice_verify.boot_heap_verdict), so that is where the room ends.
    baseline = env_budget.get("boot_heap_baseline_bytes")
    threshold = env_budget.get("boot_heap_threshold_bytes")
    if baseline is not None and threshold is not None:
        limit = baseline + threshold
        results.append(summary_row(label, ok, _bytes(figure), _bytes(limit), f"{limit - figure:,} B"))
    else:
        results.append(summary_row(label, ok, _bytes(figure), "no baseline"))
    status = "\u2713" if ok else "\u2717"
    print(f"{status} {label}: {detail}", file=sys.stderr)
    for note in notes:
        print(f"    {note}", file=sys.stderr)
    if not ok and notes:
        print("    (this script takes no ACK: the slice gate does; here the bytes come back, or the"
              " operator re-stamps boot_heap_baseline_bytes)", file=sys.stderr)
    return ok


def write_summary(path, results):
    """Write the rows as a Markdown table, the way the Verification summary
    and pull request comment show them."""
    lines = ["| | Check | Measured | Limit | Headroom |", "|---|---|---|---|---|"]
    for label, ok, measured, limit, headroom in results:
        mark = "\u2705" if ok else "\u274c"
        lines.append(f"| {mark} | {label} | {measured} | {limit} | {headroom} |")
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--env", action="append", dest="envs", metavar="NAME",
                        help="build only this env (repeatable); default every env in "
                             "tools/build_budgets.json")
    parser.add_argument("--summary", metavar="PATH",
                        help="also write every row as a Markdown table to PATH")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    budgets = load_budgets()
    envs = budgets.get("envs", {})

    if not envs:
        print("ERROR: No environments in budgets file", file=sys.stderr)
        sys.exit(1)

    if args.envs:
        # An env with no budget would build nothing and pass, so a misspelt
        # --env in a workflow must fail rather than turn the job green.
        unknown = sorted(set(args.envs) - set(envs))
        if unknown:
            print(f"ERROR: no budget for {', '.join(unknown)} in {BUDGETS_FILE.name}; "
                  f"budgeted envs: {', '.join(sorted(envs))}", file=sys.stderr)
            return 2
        envs = {name: envs[name] for name in args.envs}

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

        # Read now, before buildfs or the next env can touch the pool. A failed
        # build has no resolved config worth reading, and its failure is
        # already the row's verdict.
        if actual_size is not None and not check_envelope(env_name, "firmware", results):
            all_ok = False

        if "boot_heap_baseline_bytes" in env_budget:
            figure = boot_heap_figure(env_name, budgets) if actual_size is not None else None
            if not check_boot_heap(env_name, env_budget, figure, results):
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

        # Whatever buildfs returned: a half-run rebuild of the pool exits 0,
        # and one that failed may still have reinstalled the pristine libs.
        if actual_size is not None and not check_envelope(env_name, "buildfs", results):
            all_ok = False

    print("", file=sys.stderr)
    print(f"Summary: {len([r for r in results if r[1]])} passed, "
          f"{len([r for r in results if not r[1]])} failed",
          file=sys.stderr)
    if args.summary:
        write_summary(args.summary, results)

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
