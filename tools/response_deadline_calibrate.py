#!/usr/bin/env python3
"""Measure response-phase durations so the deadline can be set from evidence (ADR 0024).

The response-phase deadline must clear the slowest response the controller
legitimately produces, and the margin has to be stated rather than assumed.
This sweeps the routes that produce responses -- every read-only API endpoint,
every static asset the filesystem image ships, a browser-shaped concurrent page
load, and the upload path -- then reports what the controller itself measured.

The reading that matters is the controller's own `responseMaxMs`, not this
script's wall-clock timing. Client-side timing includes the network and this
process's own scheduling; `responseMaxMs` is the phase the deadline actually
governs, measured on the device between its first and last write.

Nothing here moves hardware. Every API call is a GET, the one exception being
the upload path, which is exercised through its own oversize-Content-Length
rejection so the response phase is measured without a byte reaching flash.
"""
from __future__ import annotations

import argparse
import http.client
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import socket
import sys
import time
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = REPO_ROOT / "data"
EVIDENCE_ROOT = REPO_ROOT / "tasks" / "evidence" / "webload"
DEFAULT_CONTROLLER = "10.0.0.22"
REQUEST_TIMEOUT_SECONDS = 30.0

# Read-only endpoints. POST routes are deliberately absent: this runs against a
# controller that may be attached to a droid, and a calibration sweep has no
# business commanding one. The response phases the deadline governs are the
# same shape either way -- a POST's reply is a small JSON body, far below the
# producers below.
GET_ROUTES = [
    "/api/status",
    "/api/health",
    "/api/identity",
    "/api/config",
    "/api/actions",
    "/api/logs",
    "/api/serial",
    "/api/rc",
    "/api/rc/map",
    "/api/wifi",
    "/api/validation",
    "/api/dome/layout",
    "/api/audio",
    "/api/audio/tracks",
    "/api/audio/mood-map",
    "/api/audio/catalog",
    "/api/seq",
    "/api/seq/list",
    "/api/seq/builtins",
    "/api/seq/last-run",
    "/api/coredump/status",
    "/api/admission/trace",
    "/api/profiler",
]

# The upload path's own guard rejects a Content-Length past the destination
# partition before any flash write (uploadContentLengthFits(), api_upload.h),
# and answers in the JSON shape the dashboard parses. That reply is the upload
# route's response phase, and measuring it is the whole point -- the receive
# phase is outside the deadline by design, so the criterion this satisfies is
# that the upload path does not trip it.
UPLOAD_PROBE_PATH = "/upload/firmware"
UPLOAD_PROBE_DECLARED_BYTES = 8 * 1024 * 1024


class CalibrationError(RuntimeError):
    """The sweep could not be completed."""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--controller", default=DEFAULT_CONTROLLER)
    parser.add_argument(
        "--rounds", type=int, default=3,
        help="how many times to sweep every route (default 3)",
    )
    parser.add_argument(
        "--page-load-concurrency", type=int, default=4,
        help="parallel asset fetches in the simulated page load (default 4)",
    )
    parser.add_argument(
        "--skip-upload-probe", action="store_true",
        help="omit the upload path's oversize-rejection measurement",
    )
    parser.add_argument("--out", type=Path, default=None)
    return parser


def shipped_assets() -> list[str]:
    """Every static path the filesystem image serves, largest first.

    Taken from data/ rather than hardcoded, so an asset added later is swept
    without anyone remembering to add it here. Sorted by size because the
    largest producers are what the deadline has to clear.
    """
    if not DATA_DIR.is_dir():
        raise CalibrationError(f"no data directory at {DATA_DIR}")
    files = [p for p in DATA_DIR.iterdir() if p.is_file() and not p.name.startswith(".")]
    files.sort(key=lambda p: p.stat().st_size, reverse=True)
    return [f"/{p.name}" for p in files]


class Session:
    """One keep-alive connection, reused for every request on it.

    Connection Admission paces *connections*, not requests (ADR 0022): the
    accept guard refuses past PA_ACCEPT_PER_SECOND, and a refusal arrives as a
    connection reset with no HTTP in it at all. A sweep that opened a fresh
    socket per request is therefore shed by the guard long before it learns
    anything about response phases -- which is exactly what happened on the
    first attempt.

    Reusing one connection is also the honest shape: the stack keeps
    connections alive (ADR 0023), so this is what a browser does.
    """

    def __init__(self, controller: str) -> None:
        self.controller = controller
        self._conn: http.client.HTTPConnection | None = None

    def _connect(self) -> http.client.HTTPConnection:
        if self._conn is None:
            self._conn = http.client.HTTPConnection(
                self.controller, 80, timeout=REQUEST_TIMEOUT_SECONDS
            )
        return self._conn

    def close(self) -> None:
        if self._conn is not None:
            try:
                self._conn.close()
            except OSError:
                pass
            self._conn = None

    def get(self, path: str) -> dict[str, Any]:
        """One request. Returns timing and outcome; never raises for HTTP status.

        A non-2xx is recorded rather than thrown: several routes legitimately
        answer 4xx on a controller with that component disabled, and their
        response phase is exactly as real as a 200's.

        One reconnect is attempted, because a keep-alive connection can be
        closed by the far end between requests for entirely ordinary reasons.
        """
        for attempt in (1, 2):
            started = time.monotonic()
            try:
                conn = self._connect()
                conn.request("GET", path, headers={"Host": self.controller})
                response = conn.getresponse()
                body = response.read()
                return {
                    "path": path,
                    "status": response.status,
                    "bytes": len(body),
                    "clientMs": round((time.monotonic() - started) * 1000, 1),
                    "encoding": response.getheader("Content-Encoding"),
                    "reconnected": attempt > 1,
                }
            except (http.client.HTTPException, TimeoutError, socket.timeout, OSError) as error:
                self.close()
                if attempt == 2:
                    return {
                        "path": path,
                        "status": None,
                        "error": str(error),
                        "clientMs": round((time.monotonic() - started) * 1000, 1),
                    }
                # Give the accept rate limiter a token back before reconnecting.
                time.sleep(0.5)
        raise AssertionError("unreachable")

    def status_json(self) -> dict[str, Any]:
        """/api/status, parsed. Kept separate from get() so the body is returned."""
        for attempt in (1, 2):
            try:
                conn = self._connect()
                conn.request("GET", "/api/status", headers={"Host": self.controller})
                response = conn.getresponse()
                body = response.read()
                if response.status != 200:
                    raise CalibrationError(f"/api/status answered {response.status}")
                return json.loads(body)
            except (http.client.HTTPException, TimeoutError, socket.timeout, OSError) as error:
                self.close()
                if attempt == 2:
                    raise CalibrationError(f"could not read /api/status: {error}") from error
                time.sleep(0.5)
        raise AssertionError("unreachable")


def upload_rejection_probe(controller: str) -> dict[str, Any]:
    """Exercise the upload route's response phase without writing flash.

    The declared Content-Length is above the destination partition, so the
    project's own guard refuses at the first chunk. Only the multipart preamble
    is actually sent -- the connection is closed rather than delivering the
    bytes the header promised, which is what keeps this cheap.
    """
    boundary = "----protoartooCalibrationProbe"
    preamble = (
        f"--{boundary}\r\n"
        'Content-Disposition: form-data; name="file"; filename="calibration.bin"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n"
    ).encode("ascii")
    head = (
        f"POST {UPLOAD_PROBE_PATH} HTTP/1.1\r\n"
        f"Host: {controller}\r\n"
        f"Content-Type: multipart/form-data; boundary={boundary}\r\n"
        f"Content-Length: {UPLOAD_PROBE_DECLARED_BYTES}\r\n"
        "\r\n"
    ).encode("ascii")

    started = time.monotonic()
    sock = socket.create_connection((controller, 80), timeout=REQUEST_TIMEOUT_SECONDS)
    try:
        sock.sendall(head + preamble)
        reply = b""
        sock.settimeout(REQUEST_TIMEOUT_SECONDS)
        while b"\r\n\r\n" not in reply:
            chunk = sock.recv(2048)
            if not chunk:
                break
            reply += chunk
        status_line = reply.split(b"\r\n", 1)[0].decode("ascii", errors="replace")
        return {
            "path": UPLOAD_PROBE_PATH,
            "declaredBytes": UPLOAD_PROBE_DECLARED_BYTES,
            "statusLine": status_line,
            "clientMs": round((time.monotonic() - started) * 1000, 1),
        }
    finally:
        try:
            sock.close()
        except OSError:
            pass


def run(args: argparse.Namespace) -> dict[str, Any]:
    assets = shipped_assets()
    print(f"Calibration sweep against {args.controller}: "
          f"{len(GET_ROUTES)} API routes, {len(assets)} assets, {args.rounds} round(s)")

    session = Session(args.controller)
    before = session.status_json()
    if "responseMaxMs" not in before:
        raise CalibrationError(
            "this controller does not publish responseMaxMs; it is running "
            "firmware without the response-phase deadline"
        )
    print(f"  baseline responseMaxMs={before['responseMaxMs']} "
          f"closures={before['responseDeadlineClosures']} "
          f"uptimeMs={before['uptimeMs']}")

    phases: dict[str, Any] = {}

    # Sequential sweep. One request at a time isolates each route's own phase,
    # so a slow reading can be attributed rather than merely observed.
    sequential: list[dict[str, Any]] = []
    for round_index in range(args.rounds):
        for path in GET_ROUTES + assets:
            result = session.get(path)
            # responseLastMs is read straight after, on the same connection, so
            # the value belongs to the request just made. The status read is a
            # response too and will overwrite it -- which is why the attribution
            # that matters comes from the controller's own log line naming the
            # route that set each new maximum.
            status = session.status_json()
            result["deviceResponseLastMs"] = status.get("responseLastMs")
            result["round"] = round_index + 1
            sequential.append(result)
    phases["sequential"] = sequential

    after_sequential = session.status_json()
    print(f"  after sequential sweep: responseMaxMs={after_sequential['responseMaxMs']}")

    # Browser-shaped load: the index plus its assets, fetched concurrently. This
    # is where a response phase can actually be slowed by something other than
    # its own size -- the server task is shared, so one request's phase includes
    # waiting behind the others.
    page_paths = ["/index.html"] + assets[: max(1, args.page_load_concurrency * 2)]

    # One connection per worker, each serving several requests in turn -- the
    # shape a browser actually uses, and the shape Connection Admission is
    # calibrated for. A connection per request would be shed by the accept
    # guard rather than measured.
    lanes: list[list[str]] = [[] for _ in range(max(1, args.page_load_concurrency))]
    for index, path in enumerate(page_paths):
        lanes[index % len(lanes)].append(path)

    def run_lane(paths: list[str]) -> list[dict[str, Any]]:
        lane = Session(args.controller)
        try:
            return [lane.get(path) for path in paths]
        finally:
            lane.close()

    with ThreadPoolExecutor(max_workers=len(lanes)) as pool:
        page_load = [r for lane in pool.map(run_lane, lanes) for r in lane]
    phases["pageLoad"] = page_load

    after_page_load = session.status_json()
    print(f"  after concurrent page load: responseMaxMs={after_page_load['responseMaxMs']}")

    if not args.skip_upload_probe:
        try:
            phases["uploadRejection"] = upload_rejection_probe(args.controller)
            print(f"  upload path: {phases['uploadRejection']['statusLine']}")
        except OSError as error:
            phases["uploadRejection"] = {"error": str(error)}
            print(f"  upload path probe failed: {error}")

    final = session.status_json()
    session.close()

    slowest_client = max(
        (r for r in sequential + page_load if r.get("clientMs") is not None),
        key=lambda r: r["clientMs"],
        default=None,
    )
    failures = [r for r in sequential + page_load if r.get("status") is None]

    device_max = final["responseMaxMs"]
    outcome = {
        "schemaVersion": 1,
        "issue": 92,
        "scenario": "response-deadline-calibration",
        "controller": args.controller,
        "rounds": args.rounds,
        "firmwareVersion": final.get("firmwareVersion"),
        "uptimeMsAtEnd": final.get("uptimeMs"),
        "deviceResponseMaxMs": device_max,
        "deviceResponseMaxMsBaseline": before["responseMaxMs"],
        "deadlineClosuresDuringSweep": (
            final["responseDeadlineClosures"] - before["responseDeadlineClosures"]
        ),
        "requestsIssued": len(sequential) + len(page_load),
        "requestFailures": failures,
        "slowestByClientClock": slowest_client,
        "phases": phases,
        "heapLargest8bitAtEnd": final.get("heapLargest8bit"),
        "inflightRequestsAtEnd": final.get("inflightRequests"),
    }

    out_path = args.out or (EVIDENCE_ROOT / "deadline-calibration" / "outcome.json")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(outcome, indent=2) + "\n")

    print()
    print("CALIBRATION")
    print(f"  requests issued          {outcome['requestsIssued']}")
    print(f"  request failures         {len(failures)}")
    print(f"  deadline closures        {outcome['deadlineClosuresDuringSweep']} "
          "(must be 0: no legitimate route may trip the guard)")
    print(f"  device responseMaxMs     {device_max} ms  <-- the number the deadline must clear")
    if slowest_client:
        print(f"  slowest by client clock  {slowest_client['clientMs']} ms "
              f"({slowest_client['path']})")
    print(f"  heapLargest8bit          {outcome['heapLargest8bitAtEnd']}")
    print()
    print(f"Evidence: {out_path}")
    print("The route that set each new maximum is named in the controller's own log "
          "(GET /api/logs): 'slowest response phase now N ms (<path>)'.")
    return outcome


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    try:
        outcome = run(args)
    except CalibrationError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    # A closure during a legitimate sweep is a failed calibration, not a result.
    return 0 if outcome["deadlineClosuresDuringSweep"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
