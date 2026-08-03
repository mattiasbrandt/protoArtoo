#!/usr/bin/env python3
"""Report read-only admission state for the Issue #65 live A/B fixture.

Future execution commands are emitted only as argv data. This foundation never
executes them, opens the controller/serial/browser, or mutates the filesystem.
"""
from __future__ import annotations

import argparse
import copy
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any, Mapping, Sequence

import issue65_live_ab as planner


GIT_TIMEOUT_SECONDS = 5
REQUIRED_COMMANDS = (
    "git", "pio", "ping", "node", "python3", "sha256sum", "cp", "mkdir",
)


class Issue65RuntimeError(RuntimeError):
    """A bounded deployment or evidence operation could not be completed."""


@dataclass(frozen=True)
class Timeline:
    """Create records sharing wall-clock and monotonic chronology."""

    monotonic_origin_ns: int

    @classmethod
    def start(cls) -> Timeline:
        return cls(monotonic_origin_ns=time.monotonic_ns())

    def record(self, event: str, **fields: object) -> dict[str, object]:
        now_ns = time.monotonic_ns()
        return {
            "event": event,
            "wallTime": (
                datetime.now(timezone.utc)
                .isoformat(timespec="milliseconds")
                .replace("+00:00", "Z")
            ),
            "monotonicNs": now_ns,
            "elapsedMonotonicSeconds": (
                now_ns - self.monotonic_origin_ns
            ) / 1_000_000_000,
            **fields,
        }


def _fsync_directory(directory: Path) -> None:
    descriptor = os.open(directory, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def atomic_write_json(path: Path, value: object) -> None:
    """Replace one JSON artifact atomically with a durable same-dir temporary."""
    path = Path(path)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
            json.dump(value, temporary, indent=2)
            temporary.write("\n")
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_path, path)
        temporary_path = None
        _fsync_directory(path.parent)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def create_evidence_root(root: Path) -> Path:
    """Atomically claim a fresh run directory without overwriting evidence."""
    root = Path(root)
    root.parent.mkdir(parents=True, exist_ok=True)
    try:
        root.mkdir()
    except FileExistsError as error:
        raise Issue65RuntimeError(
            f"evidence root already exists; refusing overwrite: {root}"
        ) from error
    _fsync_directory(root.parent)
    return root


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _copy_file_no_overwrite(source: Path, destination: Path) -> None:
    with Path(source).open("rb") as input_file:
        with Path(destination).open("xb") as output_file:
            shutil.copyfileobj(input_file, output_file)
            output_file.flush()
            os.fsync(output_file.fileno())
    _fsync_directory(Path(destination).parent)


def capture_artifact_identity(
    generated_identity_source: Path,
    image: Path,
    identity_artifact: Path,
    digest_artifact: Path,
) -> dict[str, object]:
    """Capture generated JSON and the paired binary digest without shell tools."""
    generated_identity_source = Path(generated_identity_source)
    image = Path(image)
    identity_artifact = Path(identity_artifact)
    digest_artifact = Path(digest_artifact)
    for required in (generated_identity_source, image):
        if not required.is_file():
            raise Issue65RuntimeError(f"required artifact is absent: {required}")
    try:
        identity = json.loads(generated_identity_source.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise Issue65RuntimeError(
            f"generated identity is not valid JSON: {generated_identity_source}"
        ) from error
    _copy_file_no_overwrite(generated_identity_source, identity_artifact)
    digest = sha256_file(image)
    with digest_artifact.open("x", encoding="ascii") as output_file:
        output_file.write(f"{digest}  {image}\n")
        output_file.flush()
        os.fsync(output_file.fileno())
    _fsync_directory(digest_artifact.parent)
    return {
        "generatedIdentity": identity,
        "sha256": digest,
        "image": str(image),
        "imageBytes": image.stat().st_size,
        "identityArtifact": str(identity_artifact),
        "digestArtifact": str(digest_artifact),
    }


def run_logged(
    argv: list[str],
    *,
    cwd: Path,
    timeout_seconds: float,
    artifact: Path,
    timeline: Timeline,
    events: list[dict[str, object]],
    event: str,
    append: bool = False,
) -> subprocess.CompletedProcess[bytes]:
    """Run bounded list argv, combining output in one named evidence artifact."""
    if (
        not isinstance(argv, list)
        or not argv
        or any(not isinstance(part, str) or not part for part in argv)
    ):
        raise Issue65RuntimeError("command argv must be a non-empty list of strings")
    cwd = Path(cwd)
    artifact = Path(artifact)
    command_event = timeline.record(
        f"{event}-started",
        argv=list(argv),
        cwd=str(cwd),
        artifact=str(artifact),
    )
    events.append(command_event)
    return_code: int | None = None
    result: subprocess.CompletedProcess[bytes] | None = None
    timed_out = False
    try:
        with artifact.open("ab" if append else "xb") as log:
            try:
                result = subprocess.run(
                    argv,
                    cwd=cwd,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    check=False,
                    timeout=timeout_seconds,
                    shell=False,
                )
                return_code = result.returncode
            except subprocess.TimeoutExpired as error:
                timed_out = True
                raise Issue65RuntimeError(
                    f"{event} timed out after {timeout_seconds} seconds"
                ) from error
            except OSError as error:
                raise Issue65RuntimeError(
                    f"{event} could not start: {error}"
                ) from error
            finally:
                log.flush()
                os.fsync(log.fileno())
    except Issue65RuntimeError:
        raise
    except OSError as error:
        raise Issue65RuntimeError(
            f"{event} evidence logging failed: {error}"
        ) from error
    finally:
        events.append(timeline.record(
            f"{event}-finished",
            returnCode=return_code,
            timedOut=timed_out,
        ))
    if result is None:
        raise Issue65RuntimeError(f"{event} did not return a command result")
    if result.returncode:
        raise Issue65RuntimeError(
            f"{event} exited with return code {result.returncode}; see {artifact}"
        )
    return result


def classify_worktree_status(
    porcelain: str,
    allowed_paths: Sequence[str] = planner.VERSION_FILES,
) -> tuple[list[str], list[str]]:
    """Split porcelain-v1 entries into generated-version and rogue changes."""
    allowed_set = set(allowed_paths)
    allowed: list[str] = []
    rogue: list[str] = []
    for entry in porcelain.splitlines():
        if (
            len(entry) >= 4
            and entry[:2] != "??"
            and entry[3:] in allowed_set
        ):
            allowed.append(entry)
        else:
            rogue.append(entry)
    return allowed, rogue


def _runtime_binding(report: Mapping[str, Any]) -> dict[str, object]:
    plan = report["plannerPlan"]
    run = plan["run"]
    target = plan["target"]
    locked = planner.RUNS[run["id"]]
    expected_root = planner.EVIDENCE_ROOT / run["id"]
    expected_argv = future_execution_argv(
        run["id"], target["controller"], target["serialPort"],
    )
    if (
        run["role"] != locked.role
        or run["commit"] != locked.commit
        or plan["paths"]["worktree"] != locked.worktree
        or Path(plan["paths"]["evidenceDirectory"]) != expected_root
        or Path(plan["expectedEvidenceBundle"]["root"]) != expected_root
        or plan["failedAllocationEvidence"] != planner.failed_allocation_evidence()
        or report["futureExecutionArgv"] != expected_argv
    ):
        raise Issue65RuntimeError(
            "report commands, paths, or evidence do not match the locked planner run"
        )
    return {
        "run": {
            "id": run["id"],
            "role": run["role"],
            "commit": run["commit"],
        },
        "target": {
            "controller": target["controller"],
            "serialPort": target["serialPort"],
        },
        "failedAllocationEvidence": copy.deepcopy(
            plan["failedAllocationEvidence"]
        ),
    }


@dataclass
class EvidenceBundle:
    """Durably retain deployment state and abort evidence for one locked run."""

    root: Path
    timeline: Timeline
    manifest: dict[str, Any]
    outcome: dict[str, Any]

    @classmethod
    def create(
        cls,
        report: Mapping[str, Any],
        stage: str = "deployment-initialized",
    ) -> EvidenceBundle:
        plan = report["plannerPlan"]
        root = Path(plan["expectedEvidenceBundle"]["root"])
        if root != Path(plan["paths"]["evidenceDirectory"]):
            raise Issue65RuntimeError("planner evidence paths do not agree")
        binding = _runtime_binding(report)
        timeline = Timeline.start()
        created = timeline.record("evidence-bundle-created", stage=stage)
        common = {
            "schemaVersion": 1,
            "issue": planner.ISSUE,
            **binding,
            "stage": stage,
            "status": "IN_PROGRESS",
        }
        bundle = cls(
            root=root,
            timeline=timeline,
            manifest={**copy.deepcopy(common), "events": [created], "artifacts": {}},
            outcome={
                **copy.deepcopy(common),
                "primaryOutcome": "UNKNOWN",
                "recoveryFacts": [],
                "stopReasons": [],
            },
        )
        create_evidence_root(root)
        try:
            (root / "identity").mkdir()
            atomic_write_json(root / "manifest.json", bundle.manifest)
            atomic_write_json(root / "outcome.json", bundle.outcome)
        except BaseException as error:
            bundle.abort(error, stage="evidence-initialization-aborted")
            raise
        return bundle

    @property
    def events(self) -> list[dict[str, object]]:
        return self.manifest["events"]

    def update_stage(self, stage: str) -> None:
        self.manifest["stage"] = stage
        self.outcome["stage"] = stage
        self.events.append(self.timeline.record("stage", stage=stage))
        atomic_write_json(self.root / "manifest.json", self.manifest)
        atomic_write_json(self.root / "outcome.json", self.outcome)

    def abort(self, error: BaseException, *, stage: str) -> None:
        abort = {
            "type": type(error).__name__,
            "message": str(error),
            "timeline": self.timeline.record("aborted", stage=stage),
        }
        self.manifest.update(status="ABORTED", stage=stage, abort=abort)
        self.outcome.update(status="ABORTED", stage=stage, abort=copy.deepcopy(abort))
        self.events.append(abort["timeline"])
        for name, value in (
            ("manifest.json", self.manifest),
            ("outcome.json", self.outcome),
        ):
            try:
                atomic_write_json(self.root / name, value)
            except (OSError, TypeError, ValueError):
                pass


def _assert_exact_worktree(worktree: Path, expected_commit: str) -> None:
    head_ok, head = git_query(worktree, "rev-parse", "HEAD")
    if not head_ok or head != expected_commit:
        raise Issue65RuntimeError(
            f"worktree HEAD must be exact locked commit {expected_commit}; "
            f"found {head}"
        )
    status_ok, porcelain = git_query(
        worktree, "status", "--porcelain", "--untracked-files=all",
    )
    if not status_ok:
        raise Issue65RuntimeError(f"could not inspect worktree status: {porcelain}")
    _, rogue = classify_worktree_status(porcelain)
    if rogue:
        raise Issue65RuntimeError(
            f"worktree has disallowed changes: {', '.join(rogue)}"
        )


def ensure_exact_worktree(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
) -> Path:
    """Create an absent detached tree or validate the existing locked tree."""
    binding = _runtime_binding(report)
    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    expected_commit = binding["run"]["commit"]
    if not os.path.lexists(worktree):
        run_logged(
            list(report["futureExecutionArgv"]["worktreeAdd"]),
            cwd=planner.REPO_ROOT,
            timeout_seconds=60,
            artifact=bundle.root / "worktree.log",
            timeline=bundle.timeline,
            events=bundle.events,
            event="worktree-add",
        )
    elif not worktree.is_dir():
        raise Issue65RuntimeError(
            f"locked worktree path exists but is not a directory: {worktree}"
        )
    _assert_exact_worktree(worktree, str(expected_commit))
    return worktree


def _restore_generated_versions(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
    *,
    log: Path,
    append: bool,
) -> None:
    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    run_logged(
        list(
            report["futureExecutionArgv"][
                "generatedVersionRestoreBeforeEveryPio"
            ]
        ),
        cwd=worktree,
        timeout_seconds=30,
        artifact=log,
        timeline=bundle.timeline,
        events=bundle.events,
        event="generated-version-restore",
        append=append,
    )
    status_ok, porcelain = git_query(
        worktree, "status", "--porcelain", "--untracked-files=all",
    )
    if not status_ok or porcelain:
        raise Issue65RuntimeError(
            "generated-version restore did not leave the worktree clean: "
            + porcelain
        )


def _identity_contract(
    report: Mapping[str, Any],
    name: str,
) -> dict[str, Path]:
    contract = report["plannerPlan"]["expectedEvidenceBundle"][
        "artifactIdentity"
    ][name]
    return {
        "source": Path(contract["generatedIdentitySource"]),
        "image": Path(contract["image"]),
        "identity": Path(contract["identityArtifact"]),
        "digest": Path(contract["digestArtifact"]),
    }


def _require_artifacts(paths: Sequence[Path]) -> None:
    missing = [str(path) for path in paths if not Path(path).is_file()]
    if missing:
        raise Issue65RuntimeError(
            f"deployment evidence is incomplete: {', '.join(missing)}"
        )


def deploy_pair(
    report: Mapping[str, Any],
    bundle: EvidenceBundle,
    *,
    build_timeout_seconds: float = 600,
    upload_timeout_seconds: float = 180,
) -> dict[str, object]:
    """Build and deploy the locked firmware/filesystem pair in planner order."""
    worktree = Path(report["plannerPlan"]["paths"]["worktree"])
    build_log = bundle.root / "build.log"
    uploadfs_log = bundle.root / "uploadfs.log"
    firmware_upload_log = bundle.root / "firmware-upload.log"
    stage = "deployment-started"
    try:
        bundle.update_stage(stage)
        ensure_exact_worktree(report, bundle)

        stage = "firmware-build"
        bundle.update_stage(stage)
        _restore_generated_versions(
            report, bundle, log=build_log, append=False,
        )
        run_logged(
            list(report["futureExecutionArgv"]["buildFirmware"]),
            cwd=worktree,
            timeout_seconds=build_timeout_seconds,
            artifact=build_log,
            timeline=bundle.timeline,
            events=bundle.events,
            event="firmware-build",
            append=True,
        )
        firmware_contract = _identity_contract(report, "firmware")
        firmware = capture_artifact_identity(
            firmware_contract["source"],
            firmware_contract["image"],
            firmware_contract["identity"],
            firmware_contract["digest"],
        )

        stage = "filesystem-upload"
        bundle.update_stage(stage)
        _restore_generated_versions(
            report, bundle, log=uploadfs_log, append=False,
        )
        run_logged(
            list(report["futureExecutionArgv"]["uploadFilesystem"]),
            cwd=worktree,
            timeout_seconds=upload_timeout_seconds,
            artifact=uploadfs_log,
            timeline=bundle.timeline,
            events=bundle.events,
            event="filesystem-upload",
            append=True,
        )
        filesystem_contract = _identity_contract(report, "filesystem")
        filesystem = capture_artifact_identity(
            filesystem_contract["source"],
            filesystem_contract["image"],
            filesystem_contract["identity"],
            filesystem_contract["digest"],
        )

        stage = "firmware-upload"
        bundle.update_stage(stage)
        _restore_generated_versions(
            report, bundle, log=uploadfs_log, append=True,
        )
        if sha256_file(firmware_contract["image"]) != firmware["sha256"]:
            raise Issue65RuntimeError(
                "prebuilt firmware changed after identity capture"
            )
        run_logged(
            list(report["futureExecutionArgv"]["uploadPrebuiltFirmware"]),
            cwd=worktree,
            timeout_seconds=upload_timeout_seconds,
            artifact=firmware_upload_log,
            timeline=bundle.timeline,
            events=bundle.events,
            event="firmware-upload",
        )

        required = [
            build_log,
            uploadfs_log,
            firmware_upload_log,
            firmware_contract["identity"],
            firmware_contract["digest"],
            filesystem_contract["identity"],
            filesystem_contract["digest"],
        ]
        _require_artifacts(required)
        bundle.manifest["artifacts"]["firmware"] = firmware
        bundle.manifest["artifacts"]["filesystem"] = filesystem
        bundle.update_stage("deployment-complete")
        return {"firmware": firmware, "filesystem": filesystem}
    except BaseException as error:
        bundle.abort(error, stage=f"{stage}-aborted")
        raise


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


def ready_create_check(name: str, subject: str) -> dict[str, str]:
    return {
        "name": name,
        "subject": subject,
        "status": "READY_CREATE",
        "failure": "",
        "detail": "detached worktree is absent and may be created by a future executor",
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
            ready_create_check(name, str(worktree))
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
    entries = porcelain.splitlines() if status_ok else []
    allowed_paths = set(planner.VERSION_FILES)
    disallowed = [
        entry for entry in entries
        if len(entry) < 4 or entry[:2] == "??" or entry[3:] not in allowed_paths
    ]
    admissible = status_ok and not disallowed
    admissible_detail = (
        porcelain if not status_ok
        else "worktree has no tracked or untracked changes" if not entries
        else "only tracked generated version JSON files are dirty"
        if admissible
        else f"disallowed worktree changes: {', '.join(disallowed)}"
    )
    return [
        check(
            "worktree-exact-commit", str(worktree), exact,
            "WORKTREE_COMMIT_MISMATCH", exact_detail,
        ),
        check(
            "worktree-clean", str(worktree), admissible,
            "WORKTREE_DIRTY", admissible_detail,
        ),
    ]


def validate_prior_outcome(
    outcome: object,
    expected_run: str,
    terminal_outcomes: frozenset[str],
) -> tuple[bool, str]:
    """Validate the locked terminal-outcome schema used for run ordering."""
    if not isinstance(outcome, dict):
        return False, "prior outcome must be a JSON object"
    run = outcome.get("run")
    if not isinstance(run, dict):
        return False, "prior outcome requires top-level run object"
    if run.get("id") != expected_run:
        return False, f"prior outcome run.id must be {expected_run}"
    expected_commit = planner.RUNS[expected_run].commit
    if run.get("commit") != expected_commit:
        return False, f"prior outcome run.commit must be {expected_commit}"
    primary = outcome.get("primaryOutcome")
    if not isinstance(primary, str) or primary not in terminal_outcomes:
        return False, "primaryOutcome must be a terminal planner outcome"
    return True, (
        f"terminal {expected_run} outcome matches locked commit and planner "
        "vocabulary"
    )


def prior_outcome_checks(
    run_id: str,
    terminal_outcomes: frozenset[str],
    evidence_root: Path = planner.EVIDENCE_ROOT,
) -> list[dict[str, str]]:
    checks = []
    for prior_run in planner.RUNS[run_id].required_completed_runs:
        artifact = evidence_root / prior_run / "outcome.json"
        valid = False
        detail = "required prior outcome artifact is absent"
        if artifact.is_file():
            try:
                outcome = json.loads(artifact.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as error:
                detail = f"prior outcome artifact is invalid: {error}"
            else:
                valid, detail = validate_prior_outcome(
                    outcome, prior_run, terminal_outcomes,
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
    allocation_contract = locked_plan["preflight"]["failedAllocationContract"]

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
    terminal_outcomes = frozenset(
        outcome for outcome in locked_plan["vocabulary"]["outcomes"]
        if outcome != "UNKNOWN"
    )
    local_checks.extend(prior_outcome_checks(args.run, terminal_outcomes))
    local_checks.append(check(
        "failed-allocation-contract",
        allocation_contract["authorizationUrl"],
        allocation_contract["status"] == "AUTHORIZED",
        "FAILED_ALLOCATION_CONTRACT_UNAUTHORIZED",
        (
            "positive evidence remains a stop condition; global zero failed "
            "allocations cannot be inferred. "
            + locked_plan["failedAllocationEvidence"]["limitation"]
        ),
    ))

    blockers = [
        {
            "code": item["failure"], "check": item["name"],
            "subject": item["subject"], "detail": item["detail"],
        }
        for item in local_checks if item["status"] == "BLOCKED"
    ]
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
        "executeRequested": bool(args.execute),
        "sideEffects": {
            "writes": 0,
            "networkRequests": 0,
            "serialOpens": 0,
            "worktreeMutations": 0,
            "browserLaunches": 0,
            "builds": 0,
            "uploads": 0,
            "probes": 0,
        },
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
