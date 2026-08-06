#!/usr/bin/env python3
"""Run the HTTP OTA upload rerun and emit machine-readable evidence (issue #80).

#80's implementation is merged and hardware-proven; the ticket stayed open for
one criterion — *uploads succeed with the admission cap and heap-floor checks
active, not bypassed* — which was unscoreable while `initPsychicWebServer()`
installed no middleware. Admission now runs on every request (ADR 0022), so the
criterion is testable, and this is what tests it repeatably instead of by hand.

Three phases, in increasing order of what they cost if they go wrong:

1. ``probe`` — an oversize POST to ``/upload/firmware``. Never calls
   ``Update.begin()``, so no flash is written and nothing reboots. It answers
   two questions at once:

   * **Does the multipart parser emit part callbacks?** The oversize guard lives
     in the chunk handler at ``index == 0`` (``src/web/api_upload.cpp:83``), so a
     413 *"larger than the app partition"* proves at least one callback ran. A
     400 *"no image received"* is the #96 signature — the whole body consumed
     with zero callbacks — and means the round-trip below cannot be scored yet.
   * **Did the request pass admission?** ``httpRequestsServed`` is published from
     the admitted branch of ``admissionMiddleware`` and from nowhere else
     (``src/web/web_request_psychic.cpp:633``), so a delta across the probe is
     positive evidence that an ``/upload/*`` request took the ordinary path
     rather than being exempt from it.

2. ``firmware`` — the real round-trip. Reboots the controller.

3. ``filesystem`` — the LittleFS round-trip. **Wipes learned sequences**, so it
   is opt-in with its own flag rather than riding along with ``--all``.

Every run starts by checking whether another session is already driving the
controller, because the board is shared and a measurement taken during
contention is not evidence. Two consecutive ``/api/status`` reads with nothing
else in flight differ by exactly one served request; anything above that aborts
the run unless ``--allow-contention`` marks the evidence as contended.

Reboots are confirmed by ``uptimeMs`` going *backwards*, not by a sleep: a
settling delay proves nothing on its own, and an uptime that decreased cannot be
a stale read of the pre-upload device.

Peak-heap evidence is taken from the upload's own success JSON rather than by
polling ``/api/status``: the device reboots about a second later and takes the
log ring with it, and polling from the host samples far too coarsely to catch a
transient dip.
"""
from __future__ import annotations

import argparse
import http.client
import json
from pathlib import Path
import socket
import sys
import time
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import issue65_live_ab_runtime as r65  # noqa: E402  (reused, hardened primitives)

REPO_ROOT = Path(__file__).resolve().parents[1]
EVIDENCE_ROOT = REPO_ROOT / "tasks" / "evidence" / "issue80"
DEFAULT_CONTROLLER = "protoartoo.local"
DEFAULT_PORT = 80

DEFAULT_FIRMWARE_IMAGE = REPO_ROOT / ".pio" / "build" / "protoArtoo" / "firmware.bin"
DEFAULT_FILESYSTEM_IMAGE = REPO_ROOT / ".pio" / "build" / "protoArtoo" / "littlefs.bin"
FIRMWARE_VERSION_FILE = REPO_ROOT / "data" / "fw-version.json"

# Above the 1,703,936-byte app partition plus the 4 KB multipart-framing
# allowance, and well below the 4 MB transport ceiling. Between those two bounds
# the per-target guard answers in the JSON shape data/firmware.js parses; above
# the ceiling PsychicHttp answers text/html before any callback runs, which
# would tell us nothing about our own parser.
PROBE_BODY_BYTES = 2_000_000

# Long enough for a 1.5 MB image over WiFi. #80 measured 12-17 s for firmware
# and 4 s for the filesystem; the margin covers a controller already serving a
# dashboard, since esp_http_server runs one round-robin task (ADR 0022).
UPLOAD_TIMEOUT_SECONDS = 120.0
STATUS_TIMEOUT_SECONDS = 10.0

# The reboot the controller schedules after a successful flash is 1000 ms out
# (requestSystemRestart), and a cold boot to a served /api/status is a few
# seconds more. Polled, not slept through.
REBOOT_POLL_INTERVAL_SECONDS = 2.0
REBOOT_DEADLINE_SECONDS = 120.0

# Sent in 64 KB writes. Small enough that a stalled socket surfaces as a
# timeout on one write rather than after the whole image, large enough that the
# host is never the bottleneck being measured.
SEND_CHUNK_BYTES = 64 * 1024

MULTIPART_BOUNDARY = "----protoArtooUploadRerunBoundary"

# Gap between the samples that decide whether another session is using the
# controller. Long enough that an ordinary dashboard page load lands inside the
# window rather than between two samples.
CONTENTION_SAMPLE_INTERVAL_SECONDS = 2.0

# Counters read on both sides of every phase. Named rather than "whatever the
# payload has" so a field disappearing from /api/status is reported as a missing
# field instead of silently dropping out of the evidence.
STATUS_FIELDS = (
    "uptimeMs",
    "firmwareVersion",
    "fsVersion",
    "resetReason",
    "heapFree",
    "heapMin",
    "heapLargest8bit",
    "httpRequestsServed",
    "httpSocketsAccepted",
    "httpSocketsOpen",
    "inflightRequests",
    "inflightRequestsPeak",
    "refusedInflightCap",
    "refusedHeapFloor",
    "refusedHeapFloorDiag",
    "tcpAcceptRejectHeap",
    "tcpAcceptRejectRate",
    "tcpAcceptRejectAgeMs",
    "acceptRejectLargestBlock",
    "acceptMinLargestBlockSeen",
    "acceptGuardMaxUs",
    "acceptGuardLastUs",
    "busyRecoveryPagesServed",
    "responseDeadlineClosures",
    "otaActive",
    "otaLastError",
    "estop",
)


class UploadRerunError(RuntimeError):
    """A rerun step could not be carried out, so no verdict can be reported."""


# --------------------------------------------------------------------------
# Device reads
# --------------------------------------------------------------------------


def read_status(host: str, port: int) -> dict[str, Any]:
    """One /api/status snapshot, reduced to the fields this rerun reasons about."""
    url = f"http://{host}:{port}/api/status"
    try:
        payload = r65._http_json(url, STATUS_TIMEOUT_SECONDS)
    except r65.Issue65RuntimeError as error:
        raise UploadRerunError(str(error)) from error

    snapshot: dict[str, Any] = {}
    missing: list[str] = []
    for field in STATUS_FIELDS:
        if field in payload:
            snapshot[field] = payload[field]
        else:
            missing.append(field)
    if missing:
        # Recorded rather than raised: a rerun that loses one counter is still
        # worth the evidence it did collect, and a silent omission would read
        # later as "the counter was zero".
        snapshot["_missingFields"] = missing
    return snapshot


def detect_contention(host: str, port: int, samples: int = 3) -> dict[str, Any]:
    """Is anyone else talking to this controller right now?

    The board is shared with other agent sessions, and a number taken while a
    second session is loading the dashboard is not evidence of anything. Two
    consecutive /api/status reads with nothing else in flight differ by exactly
    one served request -- this read itself. Anything above that is foreign
    traffic, and it is reported rather than left for whoever reads the evidence
    to notice.
    """
    served: list[int] = []
    sockets: list[int] = []
    for index in range(samples):
        if index > 0:
            time.sleep(CONTENTION_SAMPLE_INTERVAL_SECONDS)
        status = read_status(host, port)
        value = status.get("httpRequestsServed")
        if isinstance(value, int):
            served.append(value)
        value = status.get("httpSocketsAccepted")
        if isinstance(value, int):
            sockets.append(value)

    if len(served) < 2:
        return {
            "checked": False,
            "note": "httpRequestsServed was not readable; contention unknown",
        }

    # One request per sample is our own. Everything above that came from
    # somewhere else.
    foreign_requests = (served[-1] - served[0]) - (len(served) - 1)
    foreign_sockets = 0
    if len(sockets) >= 2:
        # Keep-alive means our samples may share one socket rather than open one
        # each, so this is a floor on foreign connections, not an exact count.
        foreign_sockets = max(0, (sockets[-1] - sockets[0]) - (len(sockets) - 1))

    elapsed = CONTENTION_SAMPLE_INTERVAL_SECONDS * (len(served) - 1)
    return {
        "checked": True,
        "samples": len(served),
        "windowSeconds": elapsed,
        "foreignRequests": foreign_requests,
        "foreignSocketsAtLeast": foreign_sockets,
        "busy": foreign_requests > 0,
    }


def counter_deltas(before: dict[str, Any], after: dict[str, Any]) -> dict[str, int]:
    """Integer deltas for every counter present and numeric on both sides.

    Only meaningful across a phase with no reboot in it — a reboot resets every
    counter, so the caller must not read a negative delta as shedding.
    """
    deltas: dict[str, int] = {}
    for field, start in before.items():
        end = after.get(field)
        if isinstance(start, bool) or isinstance(end, bool):
            continue
        if isinstance(start, int) and isinstance(end, int):
            deltas[field] = end - start
    return deltas


# --------------------------------------------------------------------------
# Upload
# --------------------------------------------------------------------------


def multipart_frame(field_name: str, filename: str) -> tuple[bytes, bytes]:
    """The bytes that wrap the image in a multipart/form-data body.

    Returned separately from the payload so the image is streamed from disk
    rather than concatenated into one host-side buffer, and so the caller can
    compute Content-Length without materialising the body.
    """
    preamble = (
        f"--{MULTIPART_BOUNDARY}\r\n"
        f'Content-Disposition: form-data; name="{field_name}"; filename="{filename}"\r\n'
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
    ).encode("ascii")
    epilogue = f"\r\n--{MULTIPART_BOUNDARY}--\r\n".encode("ascii")
    return preamble, epilogue


def post_multipart(
    host: str,
    port: int,
    path: str,
    field_name: str,
    filename: str,
    payload_size: int,
    payload_chunks: Any,
) -> dict[str, Any]:
    """POST a streamed multipart body and return the controller's answer.

    ``payload_chunks`` is an iterable of byte strings whose total length must
    equal ``payload_size``; the mismatch is not checked here because the
    connection's Content-Length is what the device parses against, and a short
    body surfaces as a timeout that is more informative than an assertion.
    """
    preamble, epilogue = multipart_frame(field_name, filename)
    content_length = len(preamble) + payload_size + len(epilogue)

    connection = http.client.HTTPConnection(host, port, timeout=UPLOAD_TIMEOUT_SECONDS)
    started = time.monotonic()
    try:
        connection.connect()
        connection.putrequest("POST", path, skip_accept_encoding=True)
        connection.putheader(
            "Content-Type", f"multipart/form-data; boundary={MULTIPART_BOUNDARY}"
        )
        connection.putheader("Content-Length", str(content_length))
        # Closed by us rather than kept alive: the controller reboots after a
        # successful flash, and a pooled connection would then be reused into a
        # device that is no longer there.
        connection.putheader("Connection", "close")
        connection.endheaders()

        connection.send(preamble)
        for chunk in payload_chunks:
            connection.send(chunk)
        connection.send(epilogue)

        response = connection.getresponse()
        body = response.read()
        elapsed = time.monotonic() - started
        result: dict[str, Any] = {
            "path": path,
            "httpStatus": response.status,
            "contentType": response.getheader("Content-Type"),
            "contentLength": content_length,
            "payloadBytes": payload_size,
            "wallSeconds": round(elapsed, 3),
            "bodyText": body.decode("utf-8", errors="replace")[:512],
        }
    except (OSError, socket.timeout, http.client.HTTPException) as error:
        raise UploadRerunError(
            f"POST {path} failed after {time.monotonic() - started:.1f}s: {error}"
        ) from error
    finally:
        connection.close()

    try:
        result["body"] = json.loads(result["bodyText"])
    except json.JSONDecodeError:
        # A non-JSON answer is itself a finding: it means the response came from
        # PsychicHttp's own error path rather than from our completion handler,
        # which is the contract data/firmware.js depends on.
        result["body"] = None
    return result


def file_chunks(path: Path) -> Any:
    with path.open("rb") as image:
        while True:
            chunk = image.read(SEND_CHUNK_BYTES)
            if not chunk:
                return
            yield chunk


def zero_chunks(total: int) -> Any:
    """Filler for the oversize probe. Never reaches flash: the guard rejects on
    Content-Length at the first chunk, before Update.begin() is called."""
    block = bytes(SEND_CHUNK_BYTES)
    remaining = total
    while remaining > 0:
        take = min(remaining, SEND_CHUNK_BYTES)
        yield block[:take]
        remaining -= take


# --------------------------------------------------------------------------
# Phases
# --------------------------------------------------------------------------


def classify_probe(result: dict[str, Any]) -> tuple[str, str]:
    """Map the oversize probe's answer onto what it says about #96.

    Returns (verdict, explanation). The verdict is the thing a later reader
    needs; the explanation is what makes it checkable without rerunning.
    """
    status = result["httpStatus"]
    body = result.get("body")
    error = body.get("error") if isinstance(body, dict) else None

    if status == 413 and isinstance(error, str) and "partition" in error:
        return (
            "parser-emits-parts",
            "The oversize guard fired, which lives in the chunk handler at "
            "index == 0. At least one multipart part callback ran, so the #96 "
            "signature (whole body consumed, zero callbacks) is absent.",
        )
    if status == 400 and error == "no image received":
        return (
            "issue96-present",
            "The completion handler reported kNoImage for a body that was fully "
            "transferred: MultipartProcessor consumed the request and emitted no "
            "part callbacks. This is #96; the round-trip cannot be scored yet.",
        )
    if body is None:
        return (
            "non-json-answer",
            "The answer did not parse as JSON, so it came from PsychicHttp's own "
            "error path rather than our completion handler. The response contract "
            "data/firmware.js reads is not being honoured.",
        )
    return (
        "unexpected",
        f"HTTP {status} with error {error!r} matches no known outcome of "
        "src/web/api_upload.cpp; treat the run as inconclusive.",
    )


def run_probe(host: str, port: int) -> dict[str, Any]:
    before = read_status(host, port)
    result = post_multipart(
        host,
        port,
        "/upload/firmware",
        "firmware",
        "oversize-probe.bin",
        PROBE_BODY_BYTES,
        zero_chunks(PROBE_BODY_BYTES),
    )
    after = read_status(host, port)
    deltas = counter_deltas(before, after)

    verdict, explanation = classify_probe(result)
    served = deltas.get("httpRequestsServed")
    # The probe request plus the two /api/status reads that bracket it. Anything
    # smaller means the upload request did not reach the admitted branch of the
    # middleware, which is where this counter is published from.
    admission_ran = isinstance(served, int) and served >= 3

    return {
        "phase": "probe",
        "statusBefore": before,
        "statusAfter": after,
        "deltas": deltas,
        "upload": result,
        "verdict": verdict,
        "explanation": explanation,
        "admissionMiddlewareRan": admission_ran,
        "admissionEvidence": (
            f"httpRequestsServed advanced by {served}; it is published only from "
            "the admitted branch of admissionMiddleware, so an /upload/* request "
            "took the ordinary admission path."
            if admission_ran
            else f"httpRequestsServed advanced by {served!r}, which does not "
            "account for the probe plus its two bracketing status reads."
        ),
    }


def wait_for_reboot(
    host: str,
    port: int,
    uptime_before_ms: int,
    expect_version: str | None,
) -> dict[str, Any]:
    """Poll until the controller is back on a *new* boot.

    Gated on uptimeMs having gone backwards rather than on elapsed wall time: a
    delay only makes a stale read likely, whereas a decreased uptime cannot be
    one. The version check is reported but not required, so a rerun against an
    image built elsewhere still records the boot it observed.
    """
    deadline = time.monotonic() + REBOOT_DEADLINE_SECONDS
    attempts = 0
    last_error: str | None = None
    while time.monotonic() < deadline:
        attempts += 1
        try:
            status = read_status(host, port)
        except UploadRerunError as error:
            # Expected while the device is down. Recorded so a run that never
            # comes back reports what it kept seeing.
            last_error = str(error)
            time.sleep(REBOOT_POLL_INTERVAL_SECONDS)
            continue

        uptime = status.get("uptimeMs")
        if isinstance(uptime, int) and uptime < uptime_before_ms:
            running = status.get("firmwareVersion")
            return {
                "rebooted": True,
                "attempts": attempts,
                "uptimeBeforeMs": uptime_before_ms,
                "uptimeAfterMs": uptime,
                "resetReason": status.get("resetReason"),
                "firmwareVersion": running,
                "expectedFirmwareVersion": expect_version,
                "versionMatches": (
                    None if expect_version is None else running == expect_version
                ),
                "status": status,
            }
        time.sleep(REBOOT_POLL_INTERVAL_SECONDS)

    return {
        "rebooted": False,
        "attempts": attempts,
        "uptimeBeforeMs": uptime_before_ms,
        "lastError": last_error,
        "note": (
            "uptimeMs never went backwards within the deadline. The controller "
            "either did not reboot or did not come back."
        ),
    }


def run_round_trip(
    host: str,
    port: int,
    target: str,
    image: Path,
    expect_version: str | None,
) -> dict[str, Any]:
    if not image.is_file():
        raise UploadRerunError(f"image not found: {image}")
    size = image.stat().st_size

    before = read_status(host, port)
    uptime_before = before.get("uptimeMs")
    if not isinstance(uptime_before, int):
        raise UploadRerunError("/api/status did not report an integer uptimeMs")

    field = "firmware" if target == "firmware" else "filesystem"
    result = post_multipart(
        host,
        port,
        f"/upload/{target}",
        field,
        image.name,
        size,
        file_chunks(image),
    )

    body = result.get("body")
    succeeded = (
        result["httpStatus"] == 200 and isinstance(body, dict) and body.get("ok") is True
    )

    record: dict[str, Any] = {
        "phase": target,
        "image": str(image),
        "imageBytes": size,
        "imageSha256": r65.sha256_file(image),
        "statusBefore": before,
        "upload": result,
        "succeeded": succeeded,
    }
    if succeeded:
        # The transfer's own evidence, carried in the response because the
        # reboot a second later takes the log ring with it.
        record["peakEvidence"] = {
            "bytes": body.get("bytes"),
            "minHeapFree": body.get("minHeapFree"),
            "durationMs": body.get("durationMs"),
        }
        record["reboot"] = wait_for_reboot(host, port, uptime_before, expect_version)
        record["statusAfter"] = record["reboot"].get("status")
    else:
        record["statusAfter"] = read_status(host, port)
        record["deltas"] = counter_deltas(before, record["statusAfter"])
    return record


# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------


def expected_firmware_version() -> str | None:
    try:
        payload = json.loads(FIRMWARE_VERSION_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    value = payload.get("firmwareVersion")
    return value if isinstance(value, str) else None


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default=DEFAULT_CONTROLLER)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument(
        "--probe",
        action="store_true",
        help="run the non-destructive oversize probe (no flash write, no reboot)",
    )
    parser.add_argument(
        "--firmware",
        action="store_true",
        help="run the firmware round-trip; reboots the controller",
    )
    parser.add_argument(
        "--filesystem",
        action="store_true",
        help="run the filesystem round-trip; WIPES LEARNED SEQUENCES",
    )
    parser.add_argument(
        "--rounds",
        type=int,
        default=1,
        help="repeat the firmware round-trip; #80 requires at least one repeat "
        "after a reboot, which --rounds 2 satisfies",
    )
    parser.add_argument("--firmware-image", type=Path, default=DEFAULT_FIRMWARE_IMAGE)
    parser.add_argument(
        "--filesystem-image", type=Path, default=DEFAULT_FILESYSTEM_IMAGE
    )
    parser.add_argument(
        "--expect-version",
        default=None,
        help="firmware version stamp the device should report after the flash; "
        "defaults to data/fw-version.json",
    )
    parser.add_argument(
        "--allow-contention",
        action="store_true",
        help="run even though another session is using the controller; the "
        "evidence is marked as taken under contention",
    )
    parser.add_argument(
        "--evidence",
        type=Path,
        default=None,
        help="write the evidence JSON here (default: a timestamped file under "
        "tasks/evidence/issue80/)",
    )
    return parser


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    if not (args.probe or args.firmware or args.filesystem):
        args.probe = True

    expect_version = args.expect_version or expected_firmware_version()
    run: dict[str, Any] = {
        "issue": 80,
        "host": args.host,
        "port": args.port,
        "expectedFirmwareVersion": expect_version,
        "phases": [],
    }

    exit_code = 0
    try:
        # Taken before anything else, because every number below is worthless if
        # a second session is driving the controller at the same time.
        contention = detect_contention(args.host, args.port)
        run["contention"] = contention
        if contention.get("busy"):
            print(
                f"contention: {contention['foreignRequests']} foreign request(s) "
                f"in {contention['windowSeconds']}s — another session is using "
                "the controller",
                file=sys.stderr,
            )
            if not args.allow_contention:
                run["verdict"] = "aborted-controller-busy"
                print(
                    "aborting; rerun when the board is free, or pass "
                    "--allow-contention to record the run as contended",
                    file=sys.stderr,
                )
                exit_code = 3
                raise SystemExit(exit_code)
            run["contendedEvidence"] = True

        if args.probe:
            probe = run_probe(args.host, args.port)
            run["phases"].append(probe)
            print(f"probe: {probe['verdict']} — {probe['explanation']}")
            print(f"probe: admission {probe['admissionEvidence']}")
            if probe["verdict"] != "parser-emits-parts":
                # Stop rather than flash: a round-trip run past a parser that
                # emits nothing produces a failure that says nothing about
                # #80's criterion.
                print(
                    "probe: refusing to run the round-trip; the upload path is "
                    "not working for a reason unrelated to admission (#96).",
                    file=sys.stderr,
                )
                run["verdict"] = "blocked-by-issue96"
                exit_code = 2
                raise SystemExit(exit_code)

        if args.firmware:
            for round_index in range(max(1, args.rounds)):
                record = run_round_trip(
                    args.host,
                    args.port,
                    "firmware",
                    args.firmware_image,
                    expect_version,
                )
                record["round"] = round_index + 1
                run["phases"].append(record)
                print(
                    f"firmware round {round_index + 1}: "
                    f"succeeded={record['succeeded']} "
                    f"peak={record.get('peakEvidence')} "
                    f"reboot={record.get('reboot', {}).get('rebooted')}"
                )
                if not record["succeeded"]:
                    exit_code = 1
                    break

        if args.filesystem:
            record = run_round_trip(
                args.host,
                args.port,
                "filesystem",
                args.filesystem_image,
                expect_version,
            )
            run["phases"].append(record)
            print(
                f"filesystem: succeeded={record['succeeded']} "
                f"peak={record.get('peakEvidence')} "
                f"reboot={record.get('reboot', {}).get('rebooted')}"
            )
            if not record["succeeded"]:
                exit_code = 1
    except SystemExit:
        pass
    except UploadRerunError as error:
        run["error"] = str(error)
        print(f"error: {error}", file=sys.stderr)
        exit_code = 1

    run.setdefault("verdict", "ok" if exit_code == 0 else "failed")

    evidence_path = args.evidence
    if evidence_path is None:
        EVIDENCE_ROOT.mkdir(parents=True, exist_ok=True)
        evidence_path = EVIDENCE_ROOT / f"upload-rerun-{int(time.time())}.json"
    r65.atomic_write_json(evidence_path, run)
    print(f"evidence: {evidence_path}")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
