#!/usr/bin/env python3
"""Prove the response-phase deadline reclaims stalled connections (ADR 0024, issue #92).

A request that has begun writing its response and does not finish within a
bounded time gets its socket dropped, freeing the slot for new connections.
This probe deliberately stalls a response by holding it open past the deadline,
then proves the reclaim happened by observing:

1. The deadline closures counter increments by --repeat (one per breach).
2. Concurrent /api/status requests on a separate connection continue to succeed.
3. The inflightRequests counter returns to its resting value after each breach.
4. The stalled socket is reset (not gracefully closed) by the server.

Reuses tools/issue65_live_ab_runtime.py's primitives (Timeline, NDJSON, atomic
JSON writers) rather than re-deriving them, following this repo's webload_*
convention.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import socket
import sys
import threading
import time
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import issue65_live_ab_runtime as r65  # noqa: E402  (reused, hardened primitives)

REPO_ROOT = Path(__file__).resolve().parents[1]
EVIDENCE_ROOT = REPO_ROOT / "tasks" / "evidence" / "webload"
DEFAULT_CONTROLLER = "10.0.0.22"
DEFAULT_PORT = 80
# The largest asset the filesystem image ships. It has to be larger than what
# the server can push out without the client ever reading -- the socket's own
# send queue (CONFIG_LWIP_TCP_SND_BUF_DEFAULT, 5744 bytes) plus whatever
# receive window the client advertises. Anything smaller completes normally and
# never enters a stalled response phase at all, which would make the probe
# report a failure with nothing wrong.
DEFAULT_ASSET_PATH = "/seq.js"
HANDSHAKE_DEADLINE_SECONDS = 10.0
STATUS_POLL_INTERVAL_SECONDS = 1.0
STATUS_REQUEST_DEADLINE_SECONDS = 3.0

# Client receive buffer, set before connect so it is reflected in the window
# advertised during the handshake. Small on purpose: the whole technique is to
# let the window fill and then never drain it, so the server's send blocks and
# the response phase runs out of time.
DEFAULT_RECEIVE_BUFFER_BYTES = 1024

# How often to look for the peer dropping the connection. This poll must never
# consume from the receive buffer, or it would reopen the window and un-stall
# the very response being measured -- see _wait_for_reclaim().
RECLAIM_POLL_INTERVAL_SECONDS = 0.05


class ProbeError(RuntimeError):
    """A probe setup or evidence-write step could not be completed."""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--controller", default=DEFAULT_CONTROLLER)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument(
        "--path", default=DEFAULT_ASSET_PATH,
        help=(
            "asset to stall. Must be larger than the server send queue plus the "
            f"client receive window, or it completes instead of stalling "
            f"(default {DEFAULT_ASSET_PATH})"
        ),
    )
    parser.add_argument(
        "--deadline-ms", type=int, default=8000,
        help=(
            "the firmware's PA_RESPONSE_DEADLINE_MS. Not enforced here; it is "
            "what each measured reclaim time is reported against (default 8000)"
        ),
    )
    parser.add_argument(
        "--repeat", type=int, default=3,
        help="number of repeated breach cycles (default 3, to prove no counter leak)"
    )
    parser.add_argument(
        "--receive-buffer", type=int, default=DEFAULT_RECEIVE_BUFFER_BYTES,
        help=(
            "client SO_RCVBUF, set before connect. Smaller stalls the response "
            f"sooner (default {DEFAULT_RECEIVE_BUFFER_BYTES})"
        ),
    )
    parser.add_argument(
        "--reclaim-timeout", type=float, default=None,
        help=(
            "how long to wait for the server to drop each stalled socket before "
            "calling the breach a miss (default: three times the deadline plus 5s)"
        ),
    )
    parser.add_argument(
        "--timeout", type=float, default=180.0,
        help="total run timeout in seconds (default 180s)"
    )
    parser.add_argument(
        "--out", type=Path, default=None,
        help="JSON evidence output path (defaults to tasks/evidence/webload/deadline-probe/outcome.json)"
    )
    return parser


class StatusMonitor:
    """Poll /api/status on an independent connection while breaches happen.

    Records success/failure of each poll and the key counter values, proving
    that other traffic is unaffected and slots are released.
    """

    def __init__(self, controller: str, samples_path: Path, timeline: r65.Timeline) -> None:
        self.controller = controller
        self.samples_path = samples_path
        self.timeline = timeline
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run, name="deadline-probe-status-monitor", daemon=True
        )
        self.error: BaseException | None = None
        self.samples: list[dict[str, Any]] = []

    def start(self) -> None:
        self._thread.start()

    def stop_and_join(self, timeout: float = 5.0) -> None:
        self._stop.set()
        self._thread.join(timeout)
        if self._thread.is_alive():
            raise ProbeError("status monitor did not stop within deadline")
        if self.error is not None:
            raise ProbeError(f"status monitor failed: {self.error}")

    def _sample(self) -> None:
        try:
            status = r65._http_json(
                f"http://{self.controller}/api/status", STATUS_REQUEST_DEADLINE_SECONDS,
            )
            success, error = True, None
        except r65.Issue65RuntimeError as request_error:
            status, success, error = None, False, str(request_error)
        record = r65.append_ndjson(
            self.samples_path, self.timeline, "status-sample",
            success=success, error=error,
            responseDeadlineClosures=(status or {}).get("responseDeadlineClosures"),
            responseDeadlineAgeMs=(status or {}).get("responseDeadlineAgeMs"),
            responseLastMs=(status or {}).get("responseLastMs"),
            responseMaxMs=(status or {}).get("responseMaxMs"),
            inflightRequests=(status or {}).get("inflightRequests"),
            inflightRequestsPeak=(status or {}).get("inflightRequestsPeak"),
            httpSocketsOpen=(status or {}).get("httpSocketsOpen"),
            heapLargest8bit=(status or {}).get("heapLargest8bit"),
        )
        self.samples.append(record)

    def _run(self) -> None:
        next_sample = time.monotonic()
        try:
            while not self._stop.is_set():
                now = time.monotonic()
                if now >= next_sample:
                    self._sample()
                    next_sample = max(
                        next_sample + STATUS_POLL_INTERVAL_SECONDS, time.monotonic(),
                    )
                self._stop.wait(max(0.01, min(0.2, next_sample - time.monotonic())))
        except BaseException as error:  # noqa: BLE001 - surfaced via stop_and_join()
            self.error = error


def _open_stalling_connection(
    controller: str, port: int, path: str, receive_buffer: int, timeline: r65.Timeline,
) -> tuple[socket.socket, dict[str, Any], float]:
    """Open a socket, read only the response headers, then stop reading forever.

    SO_RCVBUF is shrunk *before* connect so the small window is advertised in
    the handshake itself; setting it afterwards would leave the server free to
    push the whole body into a large window and finish normally.

    The headers are read one byte at a time. A bulk recv() would drain part of
    the body as well, and every byte drained is a byte of window handed back to
    the server -- on a small enough asset that is the difference between a
    stalled response and a completed one.

    Returns (socket, connect_record, monotonic time the request was sent).
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
    sock.settimeout(HANDSHAKE_DEADLINE_SECONDS)
    sock.connect((controller, port))

    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {controller}\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n"
    ).encode("ascii")
    sock.sendall(request)
    sent_at = time.monotonic()

    header_bytes = b""
    deadline = time.monotonic() + HANDSHAKE_DEADLINE_SECONDS
    while not header_bytes.endswith(b"\r\n\r\n"):
        if time.monotonic() > deadline:
            sock.close()
            raise ProbeError(f"{path} did not complete its headers within deadline")
        chunk = sock.recv(1)
        if not chunk:
            sock.close()
            raise ProbeError(f"{path} closed the connection during the handshake")
        header_bytes += chunk

    status_line = header_bytes.split(b"\r\n", 1)[0].decode("ascii", errors="replace")
    if " 200 " not in f" {status_line} ":
        sock.close()
        raise ProbeError(f"{path} returned unexpected status line: {status_line!r}")

    connect_record = timeline.record(
        "stalled-connection-opened",
        path=path,
        statusLine=status_line,
        localPort=sock.getsockname()[1],
        rcvbufBytes=sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF),
        headerBytes=len(header_bytes),
    )
    return sock, connect_record, sent_at


def _wait_for_reclaim(sock: socket.socket, timeout_seconds: float) -> tuple[str, float]:
    """Wait for the server to drop the stalled connection, and time it.

    Returns (outcome, seconds) where outcome is one of:
      "reset"    -- the peer sent RST, which is what a deadline breach does
                    (SO_LINGER l_linger=0 before the queued close)
      "closed"   -- the peer closed gracefully, which a breach does not do
      "open"     -- still connected when the timeout expired: no reclaim

    MSG_PEEK is what makes this measurement possible. An ordinary recv() would
    take bytes out of the receive buffer, reopening the window and letting the
    stalled response resume -- the probe would then be measuring its own
    polling rather than the deadline. Peeking inspects the queue without
    consuming it, so the window stays shut for the whole wait.
    """
    sock.setblocking(False)
    deadline = time.monotonic() + timeout_seconds
    started = time.monotonic()
    while True:
        try:
            if sock.recv(1, socket.MSG_PEEK) == b"":
                return "closed", time.monotonic() - started
        except (BlockingIOError, InterruptedError):
            pass  # Nothing new to look at; the connection is still up.
        except ConnectionResetError:
            return "reset", time.monotonic() - started
        except OSError as error:
            # Anything else that ends the connection counts as a reset for the
            # purpose of this measurement, but is recorded by its own errno so
            # a surprising one is not quietly folded in with ECONNRESET.
            return f"reset:errno={error.errno}", time.monotonic() - started

        if time.monotonic() >= deadline:
            return "open", time.monotonic() - started
        time.sleep(RECLAIM_POLL_INTERVAL_SECONDS)


def _summarize_samples(samples: list[dict[str, Any]]) -> dict[str, Any]:
    """Extract metrics from status samples."""
    reachable = [s for s in samples if s.get("success")]
    unreachable_count = len(samples) - len(reachable)

    closures = [
        s["responseDeadlineClosures"] for s in reachable
        if isinstance(s.get("responseDeadlineClosures"), int)
    ]
    inflight = [
        s["inflightRequests"] for s in reachable
        if isinstance(s.get("inflightRequests"), int)
    ]
    response_max = [
        s["responseMaxMs"] for s in reachable
        if isinstance(s.get("responseMaxMs"), int)
    ]

    return {
        "sampleCount": len(samples),
        "unreachableCount": unreachable_count,
        "responseDeadlineClosuresFirst": closures[0] if closures else None,
        "responseDeadlineClosuresLast": closures[-1] if closures else None,
        "inflightRequestsRestingValue": inflight[-1] if inflight else None,
        "responseMaxMsObserved": max(response_max) if response_max else None,
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    run_dir = (
        args.out.parent if args.out else EVIDENCE_ROOT / "deadline-probe"
    )
    run_dir.mkdir(parents=True, exist_ok=True)
    timeline = r65.Timeline.start()
    events: list[dict[str, Any]] = []

    reclaim_timeout = (
        args.reclaim_timeout
        if args.reclaim_timeout is not None
        else (args.deadline_ms / 1000.0) * 3.0 + 5.0
    )

    print(
        f"Response deadline probe: {args.repeat} breach(es) on {args.path}, "
        f"rcvbuf={args.receive_buffer}B, deadline={args.deadline_ms}ms, "
        f"reclaim timeout={reclaim_timeout:.1f}s"
    )

    monitor = StatusMonitor(args.controller, run_dir / "status-samples.ndjson", timeline)
    monitor.start()

    # Baseline before any breaches. Taken from the monitor's own polling rather
    # than a separate request, so the "before" reading comes through exactly the
    # path the "after" reading does.
    baseline_closures: int | None = None
    baseline_deadline = time.monotonic() + STATUS_POLL_INTERVAL_SECONDS * 4
    while baseline_closures is None and time.monotonic() < baseline_deadline:
        for sample in monitor.samples:
            if sample.get("success") and isinstance(
                sample.get("responseDeadlineClosures"), int
            ):
                baseline_closures = sample["responseDeadlineClosures"]
                break
        if baseline_closures is None:
            time.sleep(0.2)
    if baseline_closures is None:
        monitor.stop_and_join()
        raise ProbeError(
            "no baseline responseDeadlineClosures reading: the controller is "
            "unreachable, or it is running firmware without the deadline"
        )
    events.append(timeline.record("baseline", responseDeadlineClosures=baseline_closures))

    breach_results: list[dict[str, Any]] = []
    run_start = time.monotonic()

    for iteration in range(max(1, args.repeat)):
        if time.monotonic() - run_start > args.timeout:
            events.append(timeline.record("timeout-during-breaches"))
            break

        label = f"  breach {iteration + 1}/{args.repeat}:"
        try:
            sock, connect_record, sent_at = _open_stalling_connection(
                args.controller, args.port, args.path, args.receive_buffer, timeline,
            )
            events.append(connect_record)
            print(f"{label} headers in, no longer reading...", end="", flush=True)

            # No fixed sleep: the reclaim time is the measurement, so it is
            # waited for and timed rather than assumed to have happened.
            outcome, reclaim_seconds = _wait_for_reclaim(sock, reclaim_timeout)
            since_request_seconds = time.monotonic() - sent_at
            try:
                sock.close()
            except OSError:
                pass

            events.append(timeline.record(
                "breach-completed",
                iteration=iteration + 1,
                outcome=outcome,
                reclaimSeconds=reclaim_seconds,
                secondsSinceRequest=since_request_seconds,
            ))
            print(f" {outcome} after {reclaim_seconds:.2f}s")
            breach_results.append({
                "iteration": iteration + 1,
                "outcome": outcome,
                "reclaimSeconds": reclaim_seconds,
                "secondsSinceRequest": since_request_seconds,
                "reclaimed": outcome.startswith("reset"),
            })
        except (ProbeError, OSError) as error:
            events.append(timeline.record(
                "breach-failed",
                iteration=iteration + 1,
                error=str(error),
            ))
            print(f"{label} FAILED: {error}")
            breach_results.append({
                "iteration": iteration + 1,
                "failed": True,
                "error": str(error),
            })

        # Settle between breaches, long enough for the monitor to take at least
        # one clean sample with nothing stalled.
        if iteration < args.repeat - 1:
            time.sleep(2.0)

    monitor.stop_and_join()
    summary = _summarize_samples(monitor.samples)

    # One closure per breach, no more and no fewer. Fewer means a breach was
    # missed; more means the counter is being charged for writes rather than
    # for responses, which is the leak this repeat count exists to catch.
    closures_after = summary.get("responseDeadlineClosuresLast")
    closures_delta = (
        None if closures_after is None else closures_after - baseline_closures
    )
    attempted = len([b for b in breach_results if not b.get("failed")])
    closures_ok = closures_delta == attempted and attempted == args.repeat

    # Every poll on the other connection must have succeeded, throughout. This
    # is the "normal traffic on other connections is unaffected" evidence.
    all_polls_ok = summary["unreachableCount"] == 0

    # ADR 0017: inflightRequests must return to 0 immediately once load stops,
    # or 1 when the reading is itself an in-flight status poll.
    resting = summary.get("inflightRequestsRestingValue")
    inflight_ok = resting is not None and resting <= 1

    reclaimed_all = attempted > 0 and all(
        b.get("reclaimed") for b in breach_results if not b.get("failed")
    )
    reclaim_times = [
        b["reclaimSeconds"] for b in breach_results if b.get("reclaimed")
    ]

    outcome = {
        "schemaVersion": 1,
        "issue": 92,
        "scenario": "response-deadline-breach",
        "controller": args.controller,
        "path": args.path,
        "deadlineMs": args.deadline_ms,
        "receiveBufferBytes": args.receive_buffer,
        "repeatCount": args.repeat,
        "reclaimTimeoutSeconds": reclaim_timeout,
        "breachResults": breach_results,
        "statusPolls": summary,
        "verdict": {
            "closuresIncrementedOncePerBreach": {
                "expected": args.repeat,
                "breachesAttempted": attempted,
                "observed": closures_delta,
                "ok": closures_ok,
            },
            "allStatusPollsSucceeded": all_polls_ok,
            "inflightRequestsReturnedToResting": {
                "restingValue": resting,
                "ok": inflight_ok,
            },
            "everyBreachReclaimed": {
                "ok": reclaimed_all,
                "reclaimSecondsMin": min(reclaim_times) if reclaim_times else None,
                "reclaimSecondsMax": max(reclaim_times) if reclaim_times else None,
                "deadlineSeconds": args.deadline_ms / 1000.0,
            },
        },
        "pass": closures_ok and all_polls_ok and inflight_ok and reclaimed_all,
    }

    r65.atomic_write_json(run_dir / "outcome.json", outcome)
    for event in events:
        r65._append_ndjson_record(run_dir / "events.ndjson", event)

    return outcome


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        outcome = run(args)
    except (ProbeError, r65.Issue65RuntimeError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    verdict = outcome["verdict"]
    closures = verdict["closuresIncrementedOncePerBreach"]
    reclaim = verdict["everyBreachReclaimed"]
    polls = outcome["statusPolls"]

    print()
    print("OUTCOME")
    print(f"  closures            {closures['observed']} for "
          f"{closures['breachesAttempted']}/{closures['expected']} breaches  "
          f"ok={closures['ok']}")
    print(f"  reclaimed           ok={reclaim['ok']}  "
          f"{reclaim['reclaimSecondsMin']}..{reclaim['reclaimSecondsMax']} s "
          f"against a {reclaim['deadlineSeconds']} s deadline")
    print(f"  status polls        {polls['sampleCount']} total, "
          f"{polls['unreachableCount']} unreachable  ok={verdict['allStatusPollsSucceeded']}")
    print(f"  inflight resting    "
          f"{verdict['inflightRequestsReturnedToResting']['restingValue']}  "
          f"ok={verdict['inflightRequestsReturnedToResting']['ok']}")
    print(f"  responseMaxMs seen  {polls.get('responseMaxMsObserved')}")
    print(f"  PASS                {outcome['pass']}")
    print()
    print(f"Evidence: {(args.out.parent if args.out else EVIDENCE_ROOT / 'deadline-probe').resolve()}/")
    return 0 if outcome["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
