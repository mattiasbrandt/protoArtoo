#!/usr/bin/env python3
"""Report read-only admission state for the Issue #65 live A/B fixture.

Future execution commands are emitted only as argv data. This foundation never
executes them, opens the controller/serial/browser, or mutates the filesystem.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

import issue65_live_ab as planner


GIT_TIMEOUT_SECONDS = 5
REQUIRED_COMMANDS = (
    "git", "pio", "ping", "node", "python3", "sha256sum", "cp", "mkdir",
)


def build_parser() -> planner.PlannerArgumentParser:
    parser = planner.PlannerArgumentParser(
        description=(
            "Report local admission state for an Issue #65 live A/B run. "
            "Runtime execution is not enabled."
        )
    )
    parser.add_argument("--run", required=True, choices=tuple(planner.RUNS))
    parser.add_argument("--controller", default=planner.DEFAULT_CONTROLLER)
    parser.add_argument("--serial-port", default=planner.DEFAULT_SERIAL_PORT)
    parser.add_argument("--execute", action="store_true")
    return parser


def check(
    name: str, subject: str, passed: bool, failure: str, detail: str,
) -> dict[str, str]:
    return {
        "name": name,
        "subject": subject,
        "status": "PASS" if passed else "BLOCKED",
        "failure": "" if passed else failure,
        "detail": detail,
    }


def git_query(worktree: Path, *arguments: str) -> tuple[bool, str]:
    """Run one bounded, read-only git query without a shell."""
    try:
        result = subprocess.run(
            ["git", "-C", str(worktree), *arguments],
            capture_output=True,
            check=False,
            text=True,
            timeout=GIT_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        return False, "git query timed out"
    except OSError as error:
        return False, f"git query unavailable: {error}"
    if result.returncode:
        return False, result.stderr.strip() or "git query failed"
    return True, result.stdout.strip()


def command_checks() -> list[dict[str, str]]:
    checks = []
    for name in REQUIRED_COMMANDS:
        resolved = shutil.which(name)
        checks.append(check(
            "command-available", name, resolved is not None,
            "COMMAND_UNAVAILABLE", resolved or f"{name} was not found on PATH",
        ))
    return checks


def worktree_checks(
    worktree: Path, expected_commit: str,
) -> list[dict[str, str]]:
    if not worktree.is_dir():
        return [
            check(
                name, str(worktree), False, "WORKTREE_ABSENT",
                "detached worktree is absent",
            )
            for name in ("worktree-exact-commit", "worktree-clean")
        ]

    head_ok, head = git_query(worktree, "rev-parse", "HEAD")
    exact = head_ok and head == expected_commit
    exact_detail = (
        f"HEAD is exact locked commit {expected_commit}" if exact
        else f"expected {expected_commit}, found {head}"
        if head_ok else head
    )
    status_ok, porcelain = git_query(
        worktree, "status", "--porcelain", "--untracked-files=all",
    )
    clean = status_ok and not porcelain
    clean_detail = (
        "worktree has no tracked or untracked changes" if clean
        else "worktree has tracked or untracked changes"
        if status_ok else porcelain
    )
    return [
        check(
            "worktree-exact-commit", str(worktree), exact,
            "WORKTREE_COMMIT_MISMATCH", exact_detail,
        ),
        check(
            "worktree-clean", str(worktree), clean,
            "WORKTREE_DIRTY", clean_detail,
        ),
    ]


def prior_outcome_checks(run_id: str) -> list[dict[str, str]]:
    checks = []
    for prior_run in planner.RUNS[run_id].required_completed_runs:
        artifact = planner.EVIDENCE_ROOT / prior_run / "outcome.json"
        valid = False
        detail = "required prior outcome artifact is absent"
        if artifact.is_file():
            try:
                outcome = json.loads(artifact.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as error:
                detail = f"prior outcome artifact is invalid: {error}"
            else:
                valid = isinstance(outcome, dict)
                detail = (
                    "required prior outcome artifact is a JSON object" if valid
                    else "prior outcome artifact must be a JSON object"
                )
        checks.append(check(
            "prior-outcome-artifact", str(artifact), valid,
            "RUN_ORDER_BLOCKED", detail,
        ))
    return checks


def future_execution_argv(
    run_id: str, controller: str, serial_port: str,
) -> dict[str, list[str]]:
    """Return inert argv arrays for a future, separately reviewed executor."""
    locked = planner.RUNS[run_id]
    tree = locked.worktree
    evidence = planner.EVIDENCE_ROOT / run_id
    identity = evidence / "identity"
    environment = planner.OTA_ENV
    return {
        "worktreeAdd": [
            "git", "worktree", "add", "--detach", tree, locked.commit,
        ],
        "generatedVersionRestoreBeforeEveryPio": [
            "git", "-C", tree, "restore", f"--source={locked.commit}", "--",
            *planner.VERSION_FILES,
        ],
        "buildFirmware": [
            "pio", "run", "--project-dir", tree, "-e", environment,
        ],
        "prepareIdentityEvidence": ["mkdir", "-p", str(identity)],
        "copyFirmwareIdentity": [
            "cp", f"{tree}/data/fw-version.json",
            str(identity / "fw-version.json"),
        ],
        "hashFirmware": [
            "sha256sum", f"{tree}/.pio/build/{environment}/firmware.bin",
        ],
        "uploadFilesystem": [
            "pio", "run", "--project-dir", tree, "-e", environment,
            "-t", "uploadfs", "--upload-port", controller,
        ],
        "copyFilesystemIdentity": [
            "cp", f"{tree}/data/fs-version.json",
            str(identity / "fs-version.json"),
        ],
        "hashFilesystem": [
            "sha256sum", f"{tree}/.pio/build/{environment}/littlefs.bin",
        ],
        "uploadPrebuiltFirmware": [
            "python3", f"{tree}/tools/ota_upload.py", "--env", environment,
            "--host", controller, "--timeout", "60", "--transfer-timeout", "60",
            "--file", f"{tree}/.pio/build/{environment}/firmware.bin",
        ],
        "serialMonitor": [
            "python3", f"{tree}/tools/serial_monitor.py",
            "--port", serial_port, "--stream",
        ],
        "pingSample": ["ping", "-c", "1", "-W", "1", controller],
        "browserCapture": [
            "node", str(planner.BROWSER_COLLECTOR), "--run", run_id,
            "--url", f"http://{controller}/wifi.html",
            "--out", str(evidence / "browser"),
            "--control-file", str(evidence / "control.json"),
        ],
    }


def build_report(args: argparse.Namespace) -> dict[str, object]:
    controller = planner.validate_controller(args.controller)
    serial_port = planner.validate_serial_port(args.serial_port)
    locked_plan = planner.build_plan(argparse.Namespace(
        run=args.run,
        controller=controller,
        serial_port=serial_port,
        allow_b2=args.run == "B2",
        dry_run=True,
    ))
    locked = planner.RUNS[args.run]
    evidence = planner.EVIDENCE_ROOT / args.run
    local_checks = command_checks()

    collector_exists = planner.BROWSER_COLLECTOR.is_file()
    local_checks.append(check(
        "browser-collector-exists", str(planner.BROWSER_COLLECTOR),
        collector_exists, "BROWSER_COLLECTOR_ABSENT",
        "browser collector is present" if collector_exists
        else "browser collector is absent",
    ))
    serial_exists = Path(serial_port).exists()
    local_checks.append(check(
        "serial-port-exists", serial_port, serial_exists, "SERIAL_PORT_ABSENT",
        "serial path is present" if serial_exists else "serial path is absent",
    ))
    evidence_absent = not evidence.exists()
    local_checks.append(check(
        "evidence-directory-absent", str(evidence), evidence_absent,
        "BLOCKED_NO_OVERWRITE",
        "evidence directory is absent" if evidence_absent
        else "evidence directory already exists",
    ))
    local_checks.extend(worktree_checks(Path(locked.worktree), locked.commit))
    local_checks.extend(prior_outcome_checks(args.run))

    blockers = [
        {
            "code": item["failure"], "check": item["name"],
            "subject": item["subject"], "detail": item["detail"],
        }
        for item in local_checks if item["status"] == "BLOCKED"
    ]
    allocation_contract = locked_plan["preflight"]["failedAllocationContract"]
    if not allocation_contract["globalCounterEvaluable"]:
        blockers.append({
            "code": "FAILED_ALLOCATION_SIGNAL_UNAVAILABLE",
            "check": "failed-allocation-contract",
            "subject": allocation_contract["authorizationUrl"],
            "detail": locked_plan["failedAllocationEvidence"]["limitation"],
        })
    if args.run == "B2":
        blockers.append({
            "code": "B2_NOT_ADMITTED",
            "check": "b2-admission",
            "subject": "B2",
            "detail": (
                "B2 requires post-A1/B1/A2 review and is never admitted by "
                "this runtime foundation"
            ),
        })

    return {
        "schemaVersion": 1,
        "issue": planner.ISSUE,
        "mode": "runtime-admission",
        "executeRequested": False,
        "sideEffects": {"writes": 0, "networkRequests": 0, "probes": 0},
        "readyForExecute": False,
        "admissionPassed": not blockers,
        "plannerPlan": locked_plan,
        "failedAllocationContract": allocation_contract,
        "failedAllocationEvidence": locked_plan["failedAllocationEvidence"],
        "localChecks": local_checks,
        "blockers": blockers,
        "futureExecutionArgv": future_execution_argv(
            args.run, controller, serial_port,
        ),
    }


def main(argv: list[str]) -> int:
    parser = build_parser()
    try:
        args = parser.parse_args(argv)
        if args.execute:
            print("ERROR: RUNTIME_EXECUTION_NOT_ENABLED", file=sys.stderr)
            return 2
        report = build_report(args)
    except planner.PlanError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
