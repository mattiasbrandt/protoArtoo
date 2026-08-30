#!/usr/bin/env python3
"""P4 ESP-Hosted soak harness -- implements the #184 verdict contract (#197).

Drives bringup/p4_hosted_bench.cpp (env firebeetle2_hosted_bench) over HTTP/
SSE to answer: is ESP-Hosted reliable enough, beyond the reduced manual smoke
#184 already ran? Every device run here is coordinator/operator-initiated;
this tool never flashes, never calls `make ota`, and never touches
bringup/p4_hosted_bench.cpp or platformio.ini.

Structural rule (non-negotiable, #197 pinned comment): there is exactly ONE
SSE frame-parsing implementation (SseFrameParser, driven only through
BenchClient.stream_sse()), and --self-test exercises it by starting a local
http.server fixture and driving that same production entry point against it
-- never a second, hand-rolled parse loop that only the test sees. Three
prior attempts on this ticket failed by drifting from that rule; see the
issue's pinned comment for the full attempt log.

Endpoint contract, read directly from bringup/p4_hosted_bench.cpp rather than
assumed (b990b88, 1ee0640):
  GET  /api/health    -> "OK" liveness.
  GET  /api/status    -> JSON; see handleStatus() (line ~840) for the full
                          field set this script relies on.
  GET  /api/events    -> SSE, one monotonic-counter frame per second. Every
                          frame is `data: <n>\\r\\n\\r\\n` (emitSseFrame() calls
                          events.send(frame) with id=0, event=nullptr,
                          reconnect=0, and PsychicEventSource.cpp's
                          _generateEventMessage_impl() only emits id:/event:/
                          retry: lines when those arguments are truthy) --
                          there is never an id: or event: line on real frames.
  POST /api/c6/reset   -> Asynchronous (b990b88). 202 {requestId,
                          resetScheduled: true, responseGraceMs} proves
                          *scheduling*, not an edge -- GPIO54 does not fall
                          until responseGraceMs later. 503/409 with
                          {resetScheduled: false, reason} on rejection.

Fixture-derivation and ESP_RST_* constants were read from the pinned vendor
source and the ESP-IDF header (see SseFrame's docstring and
BAD_RESET_REASONS below) rather than invented -- the #197 pinned comment
documents two prior guesses that were wrong for exactly this reason.
"""
from __future__ import annotations

import argparse
import dataclasses
import http.client
import http.server
import json
import random
import socket
import struct
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Optional
from urllib.parse import parse_qs, urlsplit

# ---------------------------------------------------------------------------
# Constants read from source -- never invented (AGENTS.md "no guessing").
# ---------------------------------------------------------------------------

# include/web_event_stream.h:35 -- production's live SSE admission cap. The
# bench firmware (bringup/p4_hosted_bench.cpp) has no such cap of its own
# (ADR 0030 / #184's SSE-fidelity note), so this value exists only to decide
# which sse_soak runs count toward the #184 verdict: "exactly 3 concurrent
# SSE clients remain live... Higher counts may be run and logged, but carry
# no verdict."
PA_ADMISSION_MAX_SSE_CLIENTS = 3

# ~/.platformio-p4/packages/framework-arduinoespressif32-libs/esp32p4/include/
# esp_system/include/esp_system.h -- esp_reset_reason_t. Unnumbered enum
# starting at ESP_RST_UNKNOWN = 0; the mapping below is that declaration
# order, read directly. The #197 pinned comment records a prior attempt that
# guessed [1, 3, 4, 5], which would reject a normal power-on (1 is
# ESP_RST_POWERON, not a fault) and miss ESP_RST_TASK_WDT entirely (6, not
# 5) -- the one reset AGENTS.md names as the project's top concern.
ESP_RESET_REASON_NAMES = {
    0: "ESP_RST_UNKNOWN",
    1: "ESP_RST_POWERON",
    2: "ESP_RST_EXT",
    3: "ESP_RST_SW",
    4: "ESP_RST_PANIC",
    5: "ESP_RST_INT_WDT",
    6: "ESP_RST_TASK_WDT",
    7: "ESP_RST_WDT",
    8: "ESP_RST_DEEPSLEEP",
    9: "ESP_RST_BROWNOUT",
    10: "ESP_RST_SDIO",
    11: "ESP_RST_USB",
    12: "ESP_RST_JTAG",
    13: "ESP_RST_EFUSE",
    14: "ESP_RST_PWR_GLITCH",
    15: "ESP_RST_CPU_LOCKUP",
}
# PANIC, INT_WDT, TASK_WDT, WDT and CPU_LOCKUP are unambiguously crash-shaped
# resets on their own (the header's own comments: "due to exception/panic",
# "due to interrupt watchdog", "due to task watchdog", "due to other
# watchdogs", "due to CPU lock up (double exception)"). POWERON/EXT/SW/
# DEEPSLEEP/BROWNOUT/SDIO/USB/JTAG/EFUSE/PWR_GLITCH/UNKNOWN are not, by
# themselves, evidence of a panic or watchdog reset.
BAD_RESET_REASONS = {4, 5, 6, 7, 15}

DEFAULT_HEALTH_PATH = "/api/health"
DEFAULT_STATUS_PATH = "/api/status"
DEFAULT_RESET_PATH = "/api/c6/reset"
DEFAULT_SSE_PATH = "/api/events"

EXIT_NO_IMMEDIATE_BLOCKER = 0
EXIT_SELF_TEST_FAILURE = 1
EXIT_NO_GO = 2
EXIT_INVALID_UNKNOWN = 3


# ---------------------------------------------------------------------------
# SSE frame parsing -- the single implementation (#197 pinned comment part B:
# a prior attempt kept two, one tested-but-unused, one live-but-untested).
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class SseFrame:
    """One complete SSE event, as delimited by a blank line.

    bringup/p4_hosted_bench.cpp:813 calls `events.send(frame)` with only the
    payload -- id=0, event=nullptr, reconnect=0 are PsychicEventSource.h:86's
    defaults, never overridden by this bench. PsychicEventSource.cpp's
    _generateEventMessage_impl() (line 236, seeded at
    .pio/libdeps/firebeetle2_hosted_bench/PsychicHttp/src/PsychicEventSource.cpp)
    only emits the retry:/id:/event: lines when those arguments are truthy,
    so every real frame from this firmware is exactly
    `data: <n>\\r\\n\\r\\n` -- id and event are always None. The other fields
    are still parsed (not merely assumed absent) so this stays correct
    against the general SSE framing, and so a firmware change that started
    sending them would show up in the frame itself rather than being
    silently discarded.
    """

    id: Optional[int]
    event: Optional[str]
    retry: Optional[int]
    data: Optional[str]

    @property
    def counter(self) -> Optional[int]:
        """This bench's payload is always the ASCII decimal frame counter
        (p4_hosted_bench.cpp:812, `snprintf(frame, sizeof(frame), "%lu",
        frameCount)`). None if the payload actually received is not a bare
        non-negative integer, so a payload-shape change surfaces as a parse
        anomaly rather than a silently-wrong counter value."""
        if self.data is not None and self.data.isdigit():
            return int(self.data)
        return None


class SseFrameParser:
    """Incremental SSE frame parser. feed(chunk) returns any frames that
    chunk completed; finish() reports a truncated partial frame, if one is
    pending, when the connection ends. This is the ONLY frame-parsing
    implementation in this file -- both --self-test and the live drivers
    reach it exclusively through BenchClient.stream_sse()."""

    def __init__(self) -> None:
        self._buf = b""
        self._id: Optional[int] = None
        self._event: Optional[str] = None
        self._retry: Optional[int] = None
        self._data_lines: list[str] = []
        self._frame_started = False

    def feed(self, chunk: bytes) -> list[SseFrame]:
        self._buf += chunk
        frames: list[SseFrame] = []
        while b"\n" in self._buf:
            raw_line, self._buf = self._buf.split(b"\n", 1)
            if raw_line.endswith(b"\r"):
                raw_line = raw_line[:-1]
            line = raw_line.decode("utf-8", errors="replace")
            if line == "":
                if self._frame_started:
                    frames.append(self._flush())
                # A blank line with no preceding field is legal SSE (no-op
                # per the WHATWG spec), not truncation.
                continue
            self._frame_started = True
            if line.startswith(":"):
                continue  # SSE comment line.
            field, _, value = line.partition(":")
            if value.startswith(" "):
                value = value[1:]
            if field == "data":
                self._data_lines.append(value)
            elif field == "id":
                if value.isdigit() or (value.startswith("-") and value[1:].isdigit()):
                    self._id = int(value)
            elif field == "event":
                self._event = value
            elif field == "retry":
                if value.isdigit():
                    self._retry = int(value)
            # Unknown field names are ignored, per spec.
        return frames

    def _flush(self) -> SseFrame:
        frame = SseFrame(
            id=self._id,
            event=self._event,
            retry=self._retry,
            data="\n".join(self._data_lines) if self._data_lines else None,
        )
        self._id = None
        self._event = None
        self._retry = None
        self._data_lines = []
        self._frame_started = False
        return frame

    def has_pending_partial_frame(self) -> bool:
        return self._frame_started or bool(self._buf)

    def finish(self) -> Optional[str]:
        """Call once the connection has ended. Returns a description of a
        truncated in-progress frame if one is pending, else None. A
        truncated frame is reported, never silently dropped."""
        if not self.has_pending_partial_frame():
            return None
        pending_data = "\n".join(self._data_lines) if self._data_lines else None
        return (
            f"truncated mid-frame: id={self._id!r} event={self._event!r} "
            f"data={pending_data!r} trailing_bytes={self._buf!r}"
        )


def count_frame_gaps(counters: list[int]) -> int:
    """Count discontinuities in a sequence of frame counters. This bench's
    SSE payload IS the monotonic frame counter (see SseFrame.counter), so a
    gap is any place the sequence does not advance by exactly 1. Single
    implementation, used identically by --self-test's assertions and by
    run_sse_soak()'s total_frame_gaps FAIL condition."""
    gaps = 0
    for previous, current in zip(counters, counters[1:]):
        if current - previous != 1:
            gaps += 1
    return gaps


# ---------------------------------------------------------------------------
# BenchClient -- the one production entry point every driver and --self-test
# both call.
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class SseStreamResult:
    frames: list[SseFrame]
    truncated: bool
    truncated_detail: Optional[str]
    error: Optional[str]
    connect_ok: bool
    status_line: Optional[str]
    elapsed_s: float


class BenchClient:
    """The production entry point for every HTTP/SSE interaction with the
    bench firmware. --self-test drives this exact class against a local
    http.server fixture; the live drivers drive it against the device --
    there is no second, parallel implementation for either purpose."""

    def __init__(self, device: str, port: int = 80, connect_timeout_s: float = 10.0) -> None:
        self.device = device
        self.port = port
        self.connect_timeout_s = connect_timeout_s

    def get_json(self, path: str) -> tuple[int, dict]:
        conn = http.client.HTTPConnection(self.device, self.port, timeout=self.connect_timeout_s)
        try:
            conn.request("GET", path)
            resp = conn.getresponse()
            body = resp.read()
            return resp.status, (json.loads(body) if body else {})
        finally:
            conn.close()

    def post_json(self, path: str, payload: Optional[dict] = None) -> tuple[int, dict]:
        conn = http.client.HTTPConnection(self.device, self.port, timeout=self.connect_timeout_s)
        try:
            if payload is not None:
                body = json.dumps(payload).encode("utf-8")
                headers = {"Content-Type": "application/json"}
            else:
                body = b""
                headers = {}
            conn.request("POST", path, body=body, headers=headers)
            resp = conn.getresponse()
            resp_body = resp.read()
            return resp.status, (json.loads(resp_body) if resp_body else {})
        finally:
            conn.close()

    def stream_sse(
        self,
        path: str,
        on_frame: Callable[[SseFrame, float], None],
        stop: threading.Event,
        read_chunk_timeout_s: float = 5.0,
        abrupt_stop: bool = False,
    ) -> SseStreamResult:
        """Open one long-lived SSE connection and read it until `stop` is
        set, the peer closes it, or a transport fault occurs.

        Every transport-shaped exception (refused/reset connection, timeout,
        malformed handshake) is caught HERE and folded into the returned
        SseStreamResult.error -- never swallowed silently, and never left to
        surface as an unrelated crash in a soak thread. (#197 pinned
        comment, rejected attempt #1 part A: its exception handler
        referenced a `metrics` name that was neither a parameter nor a
        module global, so a wedged link raised NameError instead of being
        recorded. There is no such free variable here -- every field the
        caller needs comes back on SseStreamResult.)

        Uses resp.fp.read1(), not resp.read(): verified empirically before
        writing this method that http.client's resp.read(amt), when
        Content-Length is absent (true for this streaming response -- see
        PsychicEventSourceResponse::send()), tries to fill the full `amt`
        before returning and discards already-buffered bytes when the
        socket timeout fires first. read1() returns as soon as one
        underlying socket read succeeds, which is what a 1-frame-per-second
        protocol needs. The same test showed CPython's SocketIO marks
        itself permanently unusable ("cannot read from timed out object")
        after any read timeout -- so a timeout below is treated as terminal
        for the connection and never retried in place.
        """
        started = time.monotonic()
        frames: list[SseFrame] = []
        parser = SseFrameParser()
        conn = http.client.HTTPConnection(self.device, self.port, timeout=self.connect_timeout_s)
        status_line: Optional[str] = None
        error: Optional[str] = None
        connect_ok = False
        resp: Optional[http.client.HTTPResponse] = None
        raw_sock: Optional[socket.socket] = None
        try:
            conn.connect()
            connect_ok = True
            # Captured once, right after connect(), and used from here on
            # instead of conn.sock: verified empirically (AttributeError on
            # conn.sock.setsockopt after getresponse()) that
            # HTTPConnection.getresponse() unconditionally nulls conn.sock
            # for any response with neither Content-Length nor chunked
            # Transfer-Encoding -- see HTTPResponse.begin()'s "if the
            # connection remains open, and we aren't using chunked, and a
            # content-length was not provided, then assume that the
            # connection WILL close". That is exactly this SSE response
            # (PsychicEventSourceResponse::send() sets neither), so
            # conn.sock cannot be relied on past getresponse() -- the
            # underlying socket object itself is still live via resp.fp
            # (sock.makefile()'s refcounting keeps the fd open), which is
            # why the read loop below works at all; raw_sock is what to use
            # for socket options or an explicit close.
            raw_sock = conn.sock
            raw_sock.settimeout(read_chunk_timeout_s)
            conn.request(
                "GET", path,
                headers={
                    "Accept": "text/event-stream",
                    "Cache-Control": "no-cache",
                    "Connection": "keep-alive",
                },
            )
            resp = conn.getresponse()
            status_line = f"{resp.status} {resp.reason}"
            if resp.status != 200:
                error = f"unexpected status: {status_line}"
            else:
                content_type = resp.getheader("Content-Type", "") or ""
                if "text/event-stream" not in content_type:
                    error = f"unexpected Content-Type: {content_type!r}"
                else:
                    while not stop.is_set():
                        try:
                            chunk = resp.fp.read1(4096)
                        except (socket.timeout, TimeoutError) as timeout_error:
                            if not stop.is_set():
                                error = (
                                    f"read timed out after {read_chunk_timeout_s}s with "
                                    f"no data (stream stalled): {timeout_error}"
                                )
                            break
                        except OSError as read_error:
                            error = f"read failed: {read_error}"
                            break
                        if not chunk:
                            break  # peer closed cleanly (EOF)
                        for frame in parser.feed(chunk):
                            frames.append(frame)
                            on_frame(frame, time.monotonic())
                    if abrupt_stop and stop.is_set() and error is None:
                        # SO_LINGER(on=1, linger=0): the close below sends
                        # RST instead of FIN + orderly shutdown -- standard
                        # POSIX socket semantics, used to make "abort
                        # mid-stream" (the #197 acceptance wording for the
                        # reconnect-storm driver) an actual abrupt
                        # disconnect rather than a clean close.
                        raw_sock.setsockopt(
                            socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0),
                        )
        except (OSError, ConnectionError, TimeoutError, socket.timeout, http.client.HTTPException) as transport_error:
            error = f"transport fault: {transport_error}"
        finally:
            # conn.close() is a no-op once getresponse() has already closed
            # `conn` itself (see above) -- resp.close() is what actually
            # releases the underlying socket (and honors any SO_LINGER just
            # set on it) in that case. Fall back to conn.close() only when
            # the handshake never got as far as a response (e.g. connect()
            # or request() itself failed).
            try:
                if resp is not None:
                    resp.close()
                else:
                    conn.close()
            except OSError:
                pass
        truncated_detail = parser.finish()
        return SseStreamResult(
            frames=frames,
            truncated=truncated_detail is not None,
            truncated_detail=truncated_detail,
            error=error,
            connect_ok=connect_ok,
            status_line=status_line,
            elapsed_s=time.monotonic() - started,
        )


# ---------------------------------------------------------------------------
# Trust-boundary helpers. A missing key means unknown, not False -- an
# absent boolean must never read as a confident negative (or positive).
# ---------------------------------------------------------------------------


def _require_field(body: dict, field: str, expected_type: type, context: str) -> Any:
    """One-shot, hard-raising validation for baseline/response checks that
    happen once per driver, before any evidence has been gathered -- a
    contract violation here is cheap to fail loudly on. Callers that need
    to survive an occasional malformed sample inside a long polling loop
    use _collect_field / _safe_field below instead."""
    if field not in body:
        raise KeyError(f"{context}: response is missing required field {field!r}: {body!r}")
    value = body[field]
    if not isinstance(value, expected_type):
        raise TypeError(
            f"{context}: field {field!r} has type {type(value).__name__}, "
            f"expected {expected_type}: {value!r}"
        )
    return value


def _collect_field(samples: list[dict], field: str, expected_type: type, anomalies: list[str]) -> list:
    """Soft validation for repeated polling samples collected over a long
    (potentially multi-hour) run: one malformed sample is recorded as an
    anomaly and skipped, rather than discarding the whole run's evidence."""
    values = []
    for index, sample in enumerate(samples):
        if field not in sample:
            anomalies.append(f"sample[{index}] missing field {field!r}")
            continue
        value = sample[field]
        if not isinstance(value, expected_type):
            anomalies.append(
                f"sample[{index}] field {field!r} has type {type(value).__name__}, expected {expected_type}"
            )
            continue
        values.append(value)
    return values


def capture_status(client: BenchClient) -> dict:
    """One /api/status snapshot. Raises whatever get_json() raises
    (connection errors) -- callers decide whether "device unreachable" is
    fatal at that point in the run."""
    _, body = client.get_json(DEFAULT_STATUS_PATH)
    return body


TRANSPORT_EXCEPTIONS = (OSError, ConnectionError, TimeoutError, socket.timeout, http.client.HTTPException)


# ---------------------------------------------------------------------------
# Driver 1 -- SSE soak: N concurrent long-lived readers, gap detection,
# heap-trend and reboot/reset-reason guards, recovery-ladder visibility.
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class ClientSoakResult:
    index: int
    frame_count: int
    counters: list[int]
    gaps: int
    error: Optional[str]
    truncated: bool
    connect_ok: bool


def _sse_soak_worker(
    client: BenchClient, index: int, stop: threading.Event,
    results: list[ClientSoakResult], first_frame_event: threading.Event,
) -> None:
    counters: list[int] = []

    def on_frame(frame: SseFrame, _ts: float) -> None:
        if frame.counter is not None:
            counters.append(frame.counter)
        first_frame_event.set()

    stream_result = client.stream_sse(DEFAULT_SSE_PATH, on_frame, stop, read_chunk_timeout_s=5.0)
    results.append(ClientSoakResult(
        index=index, frame_count=len(counters), counters=counters,
        gaps=count_frame_gaps(counters), error=stream_result.error,
        truncated=stream_result.truncated, connect_ok=stream_result.connect_ok,
    ))


def run_sse_soak(
    client: BenchClient, num_clients: int, duration_s: float,
    status_poll_interval_s: float, heap_tolerance_pct: float, early_stall_check_s: float,
) -> dict:
    baseline = capture_status(client)
    baseline_boot_count = _require_field(baseline, "bootCount", int, "baseline /api/status")
    baseline_reset_reason = _require_field(baseline, "resetReason", int, "baseline /api/status")
    baseline_heap = _require_field(baseline, "largestFree8bitBlock", int, "baseline /api/status")
    # hostedTransportUpEventCount is posted by the SDIO driver's own
    # transport_active_cb() (bringup/p4_hosted_bench.cpp:574-589), independent
    # of anything this sketch believes -- the corroborating signal #184 added
    # after WiFi.status() was shown to report CONNECTED through a dead
    # transport. Tracked the same way as bootCount: a required baseline plus
    # a soft per-sample collection below.
    baseline_transport_up_event_count = _require_field(
        baseline, "hostedTransportUpEventCount", int, "baseline /api/status"
    )

    # #184: "exactly 3 concurrent SSE clients... Higher counts may be run
    # and logged, but carry no verdict." Read narrowly: it is the ABOVE-cap
    # case the contract excludes from the verdict (an admission-refusal
    # scenario production doesn't accept in the first place); at-or-below
    # the cap is still meaningful transport evidence, just not "at the
    # production concurrency target" (recorded separately below).
    counts_toward_verdict = num_clients <= PA_ADMISSION_MAX_SSE_CLIENTS
    tested_at_production_cap = num_clients == PA_ADMISSION_MAX_SSE_CLIENTS

    stop_events = [threading.Event() for _ in range(num_clients)]
    first_frame_events = [threading.Event() for _ in range(num_clients)]
    results: list[ClientSoakResult] = []
    threads = []
    for i in range(num_clients):
        t = threading.Thread(
            target=_sse_soak_worker,
            args=(client, i, stop_events[i], results, first_frame_events[i]),
            name=f"sse-soak-{i}", daemon=True,
        )
        threads.append(t)
        t.start()

    status_samples: list[dict] = []
    stop_polling = threading.Event()

    def _poll_status() -> None:
        while not stop_polling.is_set():
            try:
                status_samples.append(capture_status(client))
            except TRANSPORT_EXCEPTIONS as error:
                status_samples.append({"_pollError": str(error)})
            except json.JSONDecodeError as error:
                status_samples.append({"_pollError": f"malformed JSON: {error}"})
            stop_polling.wait(status_poll_interval_s)

    poller = threading.Thread(target=_poll_status, name="sse-soak-status-poll", daemon=True)
    poller.start()

    started = time.monotonic()
    # Early-fail: if by early_stall_check_s no worker has received even one
    # frame, don't burn the operator's whole --duration on a dead stream.
    # #184's NO-GO vocabulary names this case explicitly: "SSE immediately
    # stalls".
    early_deadline = started + min(early_stall_check_s, duration_s)
    while time.monotonic() < early_deadline and not any(e.is_set() for e in first_frame_events):
        time.sleep(0.2)
    immediate_stall = not any(e.is_set() for e in first_frame_events)

    if not immediate_stall:
        remaining = duration_s - (time.monotonic() - started)
        if remaining > 0:
            time.sleep(remaining)

    for event in stop_events:
        event.set()
    for t in threads:
        t.join(timeout=15.0)
    stop_polling.set()
    poller.join(timeout=status_poll_interval_s + 15.0)

    results.sort(key=lambda r: r.index)

    total_frames = sum(r.frame_count for r in results)
    total_gaps = sum(r.gaps for r in results)
    clients_failed = [r for r in results if r.error is not None]
    zero_frame_clients = [r for r in results if r.frame_count == 0]

    reachable_samples = [s for s in status_samples if "_pollError" not in s]
    schema_anomalies: list[str] = []
    boot_count_samples = _collect_field(reachable_samples, "bootCount", int, schema_anomalies)
    heap_samples = _collect_field(reachable_samples, "largestFree8bitBlock", int, schema_anomalies)
    sse_clients_samples = _collect_field(reachable_samples, "sseClientsConnected", int, schema_anomalies)
    ladder_samples = [s.get("recoveryLadderState") for s in reachable_samples if "recoveryLadderState" in s]
    transport_up_event_count_samples = _collect_field(
        reachable_samples, "hostedTransportUpEventCount", int, schema_anomalies
    )

    reasons: list[str] = []

    boot_count_advanced = False
    boot_count_end = baseline_boot_count
    if boot_count_samples:
        boot_count_end = boot_count_samples[-1]
        boot_count_advanced = any(b != baseline_boot_count for b in boot_count_samples)
    elif reachable_samples:
        reasons.append("no /api/status poll sample carried a valid bootCount -- cannot "
                        "confirm the P4 did not reboot during the soak")

    baseline_reset_reason_bad = baseline_reset_reason in BAD_RESET_REASONS

    min_heap = min([baseline_heap] + heap_samples) if heap_samples else baseline_heap
    heap_floor = baseline_heap * (1 - heap_tolerance_pct / 100.0)
    heap_trend_bad = min_heap < heap_floor

    max_sse_clients_observed = max(sse_clients_samples, default=0)
    admission_reached_target = max_sse_clients_observed >= num_clients

    ladder_reached_degraded = "degraded" in ladder_samples

    # transport_active_cb() only ever increments; a rise during the soak
    # means the SDIO link independently reported at least one additional
    # active transition (recovery ladder or otherwise) beyond the initial
    # boot-time connect captured in the baseline -- not itself a FAIL
    # condition (a ladder that fires and recovers without reaching
    # 'degraded' is the ladder working as designed), but the corroborating
    # count #184 named this field for.
    transport_up_event_count_end = (
        transport_up_event_count_samples[-1]
        if transport_up_event_count_samples
        else baseline_transport_up_event_count
    )
    transport_up_events_during_soak = transport_up_event_count_end - baseline_transport_up_event_count

    if immediate_stall:
        reasons.append(
            f"SSE immediately stalled: no client received a frame within {early_stall_check_s}s"
        )
    if total_frames == 0:
        reasons.append("total_frames_received == 0 (measured nothing)")
    if zero_frame_clients:
        reasons.append(
            f"{len(zero_frame_clients)} client(s) received zero frames: "
            f"{[r.index for r in zero_frame_clients]}"
        )
    if total_gaps > 0:
        reasons.append(f"total_frame_gaps == {total_gaps} (frame continuity broken)")
    if clients_failed:
        reasons.append(
            f"{len(clients_failed)} client(s) reported a transport fault: "
            f"{[(r.index, r.error) for r in clients_failed]}"
        )
    if boot_count_advanced:
        reasons.append(
            f"bootCount advanced from {baseline_boot_count} to {boot_count_end} during "
            "the soak (the P4 rebooted; ADR 0032 forbids relying on a host restart)"
        )
    if baseline_reset_reason_bad:
        reasons.append(
            f"resetReason at soak start was "
            f"{ESP_RESET_REASON_NAMES.get(baseline_reset_reason, baseline_reset_reason)} -- "
            "the device was already in a crash-shaped reset state before this run began"
        )
    if heap_trend_bad:
        reasons.append(
            f"largestFree8bitBlock fell to {min_heap} from baseline {baseline_heap} "
            f"(beyond {heap_tolerance_pct}% tolerance, floor {heap_floor:.0f})"
        )
    if counts_toward_verdict and not admission_reached_target and total_frames > 0:
        reasons.append(
            f"server-reported sseClientsConnected never reached {num_clients} "
            f"(max observed {max_sse_clients_observed}) though the harness held "
            f"{num_clients} connection(s) open"
        )
    if ladder_reached_degraded:
        reasons.append(
            "recoveryLadderState reached 'degraded' during the soak -- the bounded "
            "transport-failure recovery ladder exhausted its attempts and is terminal "
            "for this boot by design"
        )

    verdict = ("FAIL" if reasons else "PASS") if counts_toward_verdict else "OBSERVATION_ONLY"

    return {
        "driver": "sse_soak",
        "verdict": verdict,
        "countsTowardVerdict": counts_toward_verdict,
        "testedAtProductionCap": tested_at_production_cap,
        "reasons": reasons,
        "numClientsRequested": num_clients,
        "durationSRequested": duration_s,
        "immediateStall": immediate_stall,
        "totalFramesReceived": total_frames,
        "totalFrameGaps": total_gaps,
        "clientsFailed": len(clients_failed),
        "perClient": [
            {
                "index": r.index, "frameCount": r.frame_count, "gaps": r.gaps,
                "error": r.error, "truncated": r.truncated, "connectOk": r.connect_ok,
                "firstCounter": r.counters[0] if r.counters else None,
                "lastCounter": r.counters[-1] if r.counters else None,
            }
            for r in results
        ],
        "baselineBootCount": baseline_boot_count,
        "finalBootCount": boot_count_end,
        "bootCountAdvanced": boot_count_advanced,
        "baselineResetReason": ESP_RESET_REASON_NAMES.get(baseline_reset_reason, baseline_reset_reason),
        "baselineResetReasonBad": baseline_reset_reason_bad,
        "baselineLargestFree8bitBlock": baseline_heap,
        "minLargestFree8bitBlockObserved": min_heap,
        "heapTolerancePct": heap_tolerance_pct,
        "maxSseClientsConnectedObserved": max_sse_clients_observed,
        "admissionReachedTarget": admission_reached_target,
        "recoveryLadderReachedDegraded": ladder_reached_degraded,
        "recoveryLadderStatesObserved": sorted(set(s for s in ladder_samples if s is not None)),
        "baselineHostedTransportUpEventCount": baseline_transport_up_event_count,
        "finalHostedTransportUpEventCount": transport_up_event_count_end,
        "hostedTransportUpEventCountAdvancedBy": transport_up_events_during_soak,
        "statusPollSampleCount": len(status_samples),
        "statusPollUnreachableCount": len(status_samples) - len(reachable_samples),
        "statusPollSchemaAnomalies": schema_anomalies[:50],
        "note": (
            "Per-client frameCount is not cross-checked against the server's "
            "sseFramesSent delta as an independent pass/fail gate: clients connect "
            "at different points in the shared global counter, so raw counts are "
            "not directly comparable across different connection windows (#184's "
            "own accepted run showed exactly this: client counts of 1401/1403/1399 "
            "against a server delta of 1397). totalFrameGaps == 0 is the authoritative "
            "per-client continuity check; firstCounter/lastCounter above let an "
            "operator do the cross-check by hand if needed."
        ),
    }


# ---------------------------------------------------------------------------
# Driver 2 -- reconnect storm: concurrent clients repeatedly connect to
# /api/events and abort mid-stream.
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class StormCycleResult:
    connect_ok: bool
    frame_count: int
    error: Optional[str]


def _storm_worker(
    client: BenchClient, stop_all: threading.Event,
    cycle_min_s: float, cycle_max_s: float, cycles: list[StormCycleResult],
) -> None:
    while not stop_all.is_set():
        hold_s = random.uniform(cycle_min_s, cycle_max_s)
        cycle_stop = threading.Event()

        def _timer(cycle_stop: threading.Event = cycle_stop, hold_s: float = hold_s) -> None:
            stop_all.wait(hold_s)
            cycle_stop.set()

        timer_thread = threading.Thread(target=_timer, daemon=True)
        timer_thread.start()

        frame_count_holder = [0]

        def on_frame(_frame: SseFrame, _ts: float, holder: list[int] = frame_count_holder) -> None:
            holder[0] += 1

        # Short read-chunk timeout so the abort lands close to hold_s even
        # though a single read() call can otherwise block for the whole
        # timeout window (see BenchClient.stream_sse's docstring).
        result = client.stream_sse(
            DEFAULT_SSE_PATH, on_frame, cycle_stop, read_chunk_timeout_s=0.25, abrupt_stop=True,
        )
        timer_thread.join(timeout=2.0)
        cycles.append(StormCycleResult(
            connect_ok=result.connect_ok, frame_count=frame_count_holder[0], error=result.error,
        ))


def run_reconnect_storm(
    client: BenchClient, storm_clients: int, duration_s: float,
    cycle_min_s: float, cycle_max_s: float, settle_s: float, heap_tolerance_pct: float,
) -> dict:
    baseline = capture_status(client)
    baseline_sse_clients = _require_field(baseline, "sseClientsConnected", int, "baseline /api/status")
    baseline_heap = _require_field(baseline, "largestFree8bitBlock", int, "baseline /api/status")
    baseline_boot_count = _require_field(baseline, "bootCount", int, "baseline /api/status")

    stop_all = threading.Event()
    cycles_by_worker: list[list[StormCycleResult]] = [[] for _ in range(storm_clients)]
    threads = []
    for i in range(storm_clients):
        t = threading.Thread(
            target=_storm_worker,
            args=(client, stop_all, cycle_min_s, cycle_max_s, cycles_by_worker[i]),
            name=f"reconnect-storm-{i}", daemon=True,
        )
        threads.append(t)
        t.start()

    time.sleep(duration_s)
    stop_all.set()
    for t in threads:
        t.join(timeout=cycle_max_s + 10.0)

    time.sleep(settle_s)
    post = capture_status(client)
    post_sse_clients = _require_field(post, "sseClientsConnected", int, "post-storm /api/status")
    post_heap = _require_field(post, "largestFree8bitBlock", int, "post-storm /api/status")
    post_boot_count = _require_field(post, "bootCount", int, "post-storm /api/status")

    all_cycles = [c for worker_cycles in cycles_by_worker for c in worker_cycles]
    total_frames = sum(c.frame_count for c in all_cycles)
    connect_failures = [c for c in all_cycles if not c.connect_ok]
    unexpected_errors = [c for c in all_cycles if c.connect_ok and c.error is not None]

    heap_floor = baseline_heap * (1 - heap_tolerance_pct / 100.0)

    reasons: list[str] = []
    if not all_cycles:
        reasons.append("reconnect storm completed zero cycles (measured nothing)")
    if connect_failures:
        reasons.append(
            f"{len(connect_failures)} of {len(all_cycles)} cycle(s) failed to establish "
            "the SSE connection at all"
        )
    if unexpected_errors:
        reasons.append(
            f"{len(unexpected_errors)} cycle(s) reported a transport fault during a "
            f"deliberate hold window, before the harness aborted it: "
            f"{[c.error for c in unexpected_errors][:5]}"
        )
    if post_sse_clients > baseline_sse_clients:
        reasons.append(
            f"sseClientsConnected did not return to baseline after settling "
            f"({post_sse_clients} > baseline {baseline_sse_clients}) -- leaked socket(s)"
        )
    if post_heap < heap_floor:
        reasons.append(
            f"largestFree8bitBlock did not recover within tolerance after the storm "
            f"({post_heap} < floor {heap_floor:.0f}, baseline {baseline_heap})"
        )
    if post_boot_count != baseline_boot_count:
        reasons.append(
            f"bootCount changed from {baseline_boot_count} to {post_boot_count} during "
            "the reconnect storm (the P4 rebooted)"
        )

    verdict = "FAIL" if reasons else "PASS"
    return {
        "driver": "reconnect_storm",
        "verdict": verdict,
        "reasons": reasons,
        "stormClients": storm_clients,
        "durationSRequested": duration_s,
        "cycleMinS": cycle_min_s,
        "cycleMaxS": cycle_max_s,
        "cycleCount": len(all_cycles),
        "totalFramesReceived": total_frames,
        "connectFailures": len(connect_failures),
        "unexpectedErrorsDuringHold": len(unexpected_errors),
        "baselineSseClientsConnected": baseline_sse_clients,
        "postSseClientsConnected": post_sse_clients,
        "baselineLargestFree8bitBlock": baseline_heap,
        "postLargestFree8bitBlock": post_heap,
        "heapTolerancePct": heap_tolerance_pct,
        "baselineBootCount": baseline_boot_count,
        "postBootCount": post_boot_count,
    }


# ---------------------------------------------------------------------------
# Driver 3 -- C6 reset recovery: schedule an abrupt C6 reset, prove
# host-side rejoin without a P4 restart, prove a fresh SSE stream resumes.
# ---------------------------------------------------------------------------


def run_c6_reset_recovery(
    client: BenchClient, recovery_timeout_s: float, poll_interval_s: float,
    heap_tolerance_pct: float, sse_resume_timeout_s: float,
) -> dict:
    baseline = capture_status(client)
    baseline_boot_count = _require_field(baseline, "bootCount", int, "baseline /api/status")
    baseline_reset_reason = _require_field(baseline, "resetReason", int, "baseline /api/status")
    baseline_heap = _require_field(baseline, "largestFree8bitBlock", int, "baseline /api/status")

    if baseline_reset_reason in BAD_RESET_REASONS:
        return {
            "driver": "c6_reset_recovery",
            "verdict": "INVALID",
            "reasons": [
                "device was already in a crash-shaped reset state "
                f"({ESP_RESET_REASON_NAMES.get(baseline_reset_reason, baseline_reset_reason)}) "
                "before this test began -- required evidence (a clean starting point) is missing"
            ],
        }

    status_code, reset_body = client.post_json(DEFAULT_RESET_PATH)

    if status_code in (503, 409):
        reason = reset_body.get("reason", "<no reason field>")
        return {
            "driver": "c6_reset_recovery",
            "verdict": "INVALID",
            "reasons": [
                f"reset was rejected ({status_code}): {reason} -- required evidence (a "
                "scheduled and executed reset) is missing, per #184's INVALID/UNKNOWN row"
            ],
            "rejectionStatus": status_code,
            "rejectionReason": reason,
        }
    if status_code != 202:
        return {
            "driver": "c6_reset_recovery",
            "verdict": "FAIL",
            "reasons": [f"POST /api/c6/reset returned unexpected status {status_code}: {reset_body}"],
        }

    reset_scheduled = _require_field(reset_body, "resetScheduled", bool, "POST /api/c6/reset response")
    if not reset_scheduled:
        return {
            "driver": "c6_reset_recovery",
            "verdict": "INVALID",
            "reasons": [f"202 response did not set resetScheduled: true: {reset_body}"],
        }
    response_grace_ms = _require_field(reset_body, "responseGraceMs", int, "POST /api/c6/reset response")

    # The 202 proves scheduling, not an edge (#184: "A harness that treats
    # the response as 'the pulse has happened' will mis-time every recovery
    # measurement"). GPIO54 does not fall until responseGraceMs after this.
    request_accepted_at = time.monotonic()

    recovered = False
    recovery_reasons: list[str] = []
    saw_unreachable = False
    poll_schema_anomalies: list[str] = []
    last_status: Optional[dict] = None
    deadline = request_accepted_at + recovery_timeout_s
    while time.monotonic() < deadline:
        try:
            last_status = capture_status(client)
        except TRANSPORT_EXCEPTIONS:
            saw_unreachable = True
            time.sleep(poll_interval_s)
            continue
        except json.JSONDecodeError:
            saw_unreachable = True
            time.sleep(poll_interval_s)
            continue

        boot_count_field = last_status.get("bootCount")
        if not isinstance(boot_count_field, int):
            poll_schema_anomalies.append(f"poll sample missing/invalid bootCount: {last_status!r}")
            time.sleep(poll_interval_s)
            continue
        if boot_count_field != baseline_boot_count:
            recovery_reasons.append(
                f"bootCount changed from {baseline_boot_count} to {boot_count_field} -- the "
                "P4 host rebooted. ADR 0032 forbids relying on a host restart, and #184's "
                "NO-GO condition is exactly 'abrupt C6 reset cannot rejoin without P4 restart'"
            )
            break

        reset_reason_field = last_status.get("resetReason")
        if isinstance(reset_reason_field, int) and reset_reason_field in BAD_RESET_REASONS:
            recovery_reasons.append(
                "resetReason became "
                f"{ESP_RESET_REASON_NAMES.get(reset_reason_field, reset_reason_field)} during recovery"
            )
            break

        wifi_connected = last_status.get("wifiConnected")
        hosted_initialized = last_status.get("hostedIsInitialized")
        # Missing/non-bool means unknown, not "not yet connected" -- treat
        # as still-recovering (keep polling) rather than asserting either
        # way on an unrecognized shape.
        if wifi_connected is True and hosted_initialized is True:
            recovered = True
            break

        time.sleep(poll_interval_s)

    recovered_at_s = time.monotonic() - request_accepted_at

    if not recovery_reasons and not recovered:
        recovery_reasons.append(
            f"did not observe wifiConnected + hostedIsInitialized again within "
            f"{recovery_timeout_s}s of the scheduled reset"
        )

    sse_resumed = False
    sse_frame_count = 0
    sse_resume_error = None
    if recovered:
        resume_stop = threading.Event()

        def _resume_timer(resume_stop: threading.Event = resume_stop) -> None:
            resume_stop.wait(sse_resume_timeout_s)
            resume_stop.set()

        timer = threading.Thread(target=_resume_timer, daemon=True)
        timer.start()
        frames_holder: list[SseFrame] = []

        def on_frame(frame: SseFrame, _ts: float, holder: list[SseFrame] = frames_holder) -> None:
            holder.append(frame)
            if len(holder) >= 2:
                resume_stop.set()

        resume_result = client.stream_sse(
            DEFAULT_SSE_PATH, on_frame, resume_stop, read_chunk_timeout_s=2.0,
        )
        timer.join(timeout=2.0)
        sse_frame_count = len(frames_holder)
        sse_resumed = sse_frame_count > 0
        sse_resume_error = resume_result.error
        if not sse_resumed:
            recovery_reasons.append(
                f"a fresh SSE stream did not advance within {sse_resume_timeout_s}s of "
                f"recovery (stream error: {sse_resume_error})"
            )

    post_heap = (last_status or {}).get("largestFree8bitBlock")
    heap_recovered: Optional[bool] = None
    if isinstance(post_heap, int):
        heap_floor = baseline_heap * (1 - heap_tolerance_pct / 100.0)
        heap_recovered = post_heap >= heap_floor
        if not heap_recovered:
            recovery_reasons.append(
                f"largestFree8bitBlock did not recover within tolerance after reset+rejoin "
                f"({post_heap} < floor {heap_floor:.0f}, baseline {baseline_heap})"
            )

    verdict = "PASS" if recovered and sse_resumed and not recovery_reasons else "FAIL"

    return {
        "driver": "c6_reset_recovery",
        "verdict": verdict,
        "reasons": recovery_reasons,
        "requestId": reset_body.get("requestId"),
        "responseGraceMs": response_grace_ms,
        "resetPulseMsFromStatus": (last_status or {}).get("resetPulseMs"),
        "sawUnreachableWindow": saw_unreachable,
        "pollSchemaAnomalies": poll_schema_anomalies[:50],
        "recoveredAtSeconds": round(recovered_at_s, 3) if recovered else None,
        "recovered": recovered,
        "sseResumed": sse_resumed,
        "sseFrameCountAfterRecovery": sse_frame_count,
        "sseResumeError": sse_resume_error,
        "baselineBootCount": baseline_boot_count,
        "finalBootCount": (last_status or {}).get("bootCount"),
        "baselineLargestFree8bitBlock": baseline_heap,
        "finalLargestFree8bitBlock": post_heap,
        "heapTolerancePct": heap_tolerance_pct,
        "heapRecoveredWithinTolerance": heap_recovered,
        "resetEvidenceBoundary": (last_status or {}).get(
            "resetEvidenceBoundary",
            "GPIO API results require external logic capture plus C6 UART reboot proof",
        ),
        "note": (
            "This driver proves host-side rejoin (Hosted link teardown + "
            "hostedIsInitialized() + /api/status answering again through the same "
            "link, per #184's 2026-08-29 coordinator ruling) and SSE freshness. It "
            "does NOT prove the physical GPIO54 edge or pulse width -- that requires "
            "a synchronized logic capture, a coordinator/operator action this "
            "host-side harness cannot perform."
        ),
    }


# ---------------------------------------------------------------------------
# Orchestration and the #184 verdict vocabulary.
# ---------------------------------------------------------------------------


def _run_driver_safely(name: str, fn: Callable[..., dict], *args: Any) -> dict:
    """Bounds the blast radius of a response-contract violation to "this
    driver is INVALID" rather than crashing the whole (possibly
    multi-hour) run with a bare traceback. Only contract-shape errors
    (_require_field's KeyError/TypeError) are caught here -- anything else
    is a bug in this harness and must propagate, per AGENTS.md "never
    swallow an error to keep moving"."""
    try:
        return fn(*args)
    except (KeyError, TypeError) as contract_error:
        return {
            "driver": name,
            "verdict": "INVALID",
            "reasons": [f"response contract violation while running {name}: {contract_error}"],
        }


def _compose_overall_verdict(driver_results: dict[str, dict]) -> tuple[str, int]:
    verdicts = [d["verdict"] for d in driver_results.values()]
    if any(v == "INVALID" for v in verdicts):
        return "INVALID / UNKNOWN", EXIT_INVALID_UNKNOWN
    if any(v == "FAIL" for v in verdicts):
        return "NO-GO", EXIT_NO_GO
    # PASS and OBSERVATION_ONLY (num_clients > production's cap) both fall
    # through here -- #184: "Higher counts may be run and logged, but carry
    # no verdict."
    return "NO IMMEDIATE BLOCKER", EXIT_NO_IMMEDIATE_BLOCKER


def run(args: argparse.Namespace) -> tuple[dict, int]:
    client = BenchClient(args.device, args.port, connect_timeout_s=args.connect_timeout_s)

    # Preflight: an unreachable device, or a C6 that never came up, cannot
    # produce any of the required evidence -- #184's INVALID/UNKNOWN row
    # ("required evidence is missing"), not NO-GO (NO-GO is a live link
    # that then fails; this is "there was never a link to test").
    try:
        preflight_status = capture_status(client)
    except TRANSPORT_EXCEPTIONS as error:
        return {
            "schemaVersion": 1, "issue": 197, "device": args.device, "port": args.port,
            "verdict": "INVALID / UNKNOWN",
            "reasons": [f"device unreachable at {args.device}:{args.port}: {error}"],
            "drivers": {},
        }, EXIT_INVALID_UNKNOWN

    hosted_initialized = preflight_status.get("hostedIsInitialized")
    wifi_connected = preflight_status.get("wifiConnected")
    if hosted_initialized is not True or wifi_connected is not True:
        return {
            "schemaVersion": 1, "issue": 197, "device": args.device, "port": args.port,
            "verdict": "INVALID / UNKNOWN",
            "reasons": [
                "device is reachable but not a confirmed-ready C6: "
                f"hostedIsInitialized={hosted_initialized!r} wifiConnected={wifi_connected!r} "
                "(missing/false means the required evidence -- an established Hosted "
                "link -- is absent, not that the link failed)"
            ],
            "drivers": {},
        }, EXIT_INVALID_UNKNOWN

    drivers_to_run = (
        ["sse_soak", "reconnect_storm", "c6_reset_recovery"] if args.driver == "all" else [args.driver]
    )
    driver_results: dict[str, dict] = {}

    if "sse_soak" in drivers_to_run:
        driver_results["sse_soak"] = _run_driver_safely(
            "sse_soak", run_sse_soak, client, args.num_clients, args.duration,
            args.status_poll_interval_s, args.heap_recovery_tolerance_pct, args.early_stall_check_s,
        )
    if "reconnect_storm" in drivers_to_run:
        driver_results["reconnect_storm"] = _run_driver_safely(
            "reconnect_storm", run_reconnect_storm, client, args.storm_clients, args.storm_duration,
            args.storm_cycle_min_s, args.storm_cycle_max_s, args.storm_settle_s,
            args.heap_recovery_tolerance_pct,
        )
    if "c6_reset_recovery" in drivers_to_run:
        driver_results["c6_reset_recovery"] = _run_driver_safely(
            "c6_reset_recovery", run_c6_reset_recovery, client, args.reset_recovery_timeout_s,
            args.reset_poll_interval_s, args.heap_recovery_tolerance_pct, args.sse_resume_timeout_s,
        )

    verdict, exit_code = _compose_overall_verdict(driver_results)
    return {
        "schemaVersion": 1, "issue": 197, "device": args.device, "port": args.port,
        "verdict": verdict, "drivers": driver_results,
    }, exit_code


# ---------------------------------------------------------------------------
# --self-test: starts a local http.server fixture serving byte-exact
# PsychicEventSource framing and drives BenchClient (the real production
# entry point) against it. No inline parse loop.
# ---------------------------------------------------------------------------


def _fixture_frame_bytes(counter: int) -> bytes:
    """Byte-exact reproduction of PsychicEventSource.cpp's
    _generateEventMessage_impl() (line 236) for exactly the call
    bringup/p4_hosted_bench.cpp's emitSseFrame() makes: events.send(frame)
    with event=nullptr, id=0, reconnect=0 (PsychicEventSource.h:86
    defaults). Because id and reconnect are 0 (falsy in the vendor's
    `if (id)` / `if (reconnect)` checks) and event is NULL, only the data:
    line and the unconditional blank-line terminator are ever emitted --
    never id:/event:/retry:."""
    return f"data: {counter}\r\n\r\n".encode("ascii")


FIXTURE_STATUS_BODY = {
    "firmwareVersion": "self-test",
    "bootCount": 1,
    "resetReason": 1,  # ESP_RST_POWERON
    "wifiConnected": True,
    "hostedIsInitialized": True,
    "sseFramesSent": 42,
    "sseClientsConnected": 1,
    "largestFree8bitBlock": 123456,
    "recoveryLadderState": "idle",
    "hostedTransportFailureCount": 0,
    "hostedTransportUpEventCount": 0,
    "recoveryAttemptCount": 0,
    "recoveryRecoveredCount": 0,
}
FIXTURE_RESET_BODY = {"requestId": 7, "resetScheduled": True, "responseGraceMs": 1000}


class _FixtureHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format: str, *args: Any) -> None:  # noqa: A002 - stdlib signature
        pass  # keep self-test output focused on assertions, not access logs

    def do_GET(self) -> None:
        path = urlsplit(self.path).path
        if path == "/api/events":
            self._serve_sse()
        elif path == "/api/status":
            self._serve_json(200, FIXTURE_STATUS_BODY)
        else:
            self.send_error(404)

    def do_POST(self) -> None:
        if self.path == "/api/c6/reset":
            length = int(self.headers.get("Content-Length", 0) or 0)
            self.rfile.read(length)
            self._serve_json(202, FIXTURE_RESET_BODY)
        else:
            self.send_error(404)

    def _serve_json(self, status: int, body_dict: dict) -> None:
        body = json.dumps(body_dict).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_sse(self) -> None:
        query = parse_qs(urlsplit(self.path).query)
        mode = query.get("mode", ["normal"])[0]

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()

        if mode == "normal":
            for n in (0, 1, 2):
                self.wfile.write(_fixture_frame_bytes(n))
            self.wfile.flush()
            self.close_connection = True
        elif mode == "gapped":
            for n in (0, 1, 3, 4, 6):
                self.wfile.write(_fixture_frame_bytes(n))
            self.wfile.flush()
            self.close_connection = True
        elif mode == "truncated":
            self.wfile.write(_fixture_frame_bytes(0))
            self.wfile.write(b"data: 1\r\n")  # deliberately missing the closing blank line
            self.wfile.flush()
            self.close_connection = True
        elif mode == "connreset":
            # Send one real frame first: verified empirically (before
            # writing this fixture) that RSTing before any data has been
            # sent has nothing in flight to discard, so Linux's recv()
            # reports a plain EOF (0 bytes) rather than ECONNRESET -- the
            # reset needs to land while the client is actively expecting
            # more data on an established stream to reproduce a genuine
            # mid-stream ConnectionResetError.
            self.wfile.write(_fixture_frame_bytes(0))
            self.wfile.flush()
            # SO_LINGER(1, 0) makes close() send an RST instead of a clean
            # FIN -- but only if THIS close() call is the one that actually
            # tears the socket down. Verified empirically that it is not
            # enough to set it and let socketserver do its normal teardown:
            # BaseServer.shutdown_request() calls
            # `request.shutdown(socket.SHUT_WR)` (a graceful half-close)
            # BEFORE close_request()'s close(), independent of SO_LINGER, so
            # the client always saw a clean EOF instead of a reset. Closing
            # the socket here, before that framework teardown runs, is what
            # makes the RST land on the wire. socketserver's own
            # shutdown()/close() calls on this now-already-closed fd
            # afterwards are `except OSError: pass`-guarded no-ops.
            self.connection.setsockopt(
                socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0),
            )
            self.connection.close()
            self.close_connection = True
        else:
            self.close_connection = True


def _start_fixture_server() -> tuple[http.server.ThreadingHTTPServer, threading.Thread]:
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _FixtureHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread


def _stop_fixture_server(server: http.server.ThreadingHTTPServer, thread: threading.Thread) -> None:
    server.shutdown()
    server.server_close()
    thread.join(timeout=5.0)


def _run_sse_scenario(
    name: str, mode: str, check: Callable[[SseStreamResult, list[SseFrame]], None], failures: list[str],
) -> None:
    server, thread = _start_fixture_server()
    try:
        port = server.server_address[1]
        client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)
        frames_seen: list[SseFrame] = []
        stop = threading.Event()
        result = client.stream_sse(
            f"/api/events?mode={mode}",
            on_frame=lambda frame, _ts: frames_seen.append(frame),
            stop=stop,
            read_chunk_timeout_s=2.0,
        )
        try:
            check(result, frames_seen)
            print(f"  PASS  {name}")
        except AssertionError as failure:
            print(f"  FAIL  {name}: {failure}")
            failures.append(f"{name}: {failure}")
    finally:
        _stop_fixture_server(server, thread)


def _check_normal(result: SseStreamResult, frames: list[SseFrame]) -> None:
    assert result.error is None, f"unexpected error: {result.error}"
    assert not result.truncated, f"unexpectedly truncated: {result.truncated_detail}"
    counters = [f.counter for f in frames]
    assert counters == [0, 1, 2], f"expected [0, 1, 2], got {counters}"
    assert all(f.id is None and f.event is None for f in frames), (
        "this firmware never sends id:/event: -- a non-None value means the parser "
        "invented a field the fixture did not send"
    )
    assert count_frame_gaps(counters) == 0, "normal frames must report zero gaps"


def _check_gapped(result: SseStreamResult, frames: list[SseFrame]) -> None:
    assert result.error is None, f"unexpected error: {result.error}"
    counters = [f.counter for f in frames]
    assert counters == [0, 1, 3, 4, 6], f"expected [0, 1, 3, 4, 6], got {counters}"
    gaps = count_frame_gaps(counters)
    assert gaps == 2, f"expected 2 gap events (1->3, 4->6), got {gaps}"


def _check_truncated(result: SseStreamResult, frames: list[SseFrame]) -> None:
    counters = [f.counter for f in frames]
    assert counters == [0], f"expected exactly the one complete frame [0], got {counters}"
    assert result.truncated, "a truncated fixture must be reported as truncated, not silently dropped"
    assert result.truncated_detail is not None and "data=" in result.truncated_detail, (
        f"truncated_detail should describe the pending partial frame, got {result.truncated_detail!r}"
    )


def _check_connreset(result: SseStreamResult, _frames: list[SseFrame]) -> None:
    assert result.connect_ok, "the TCP connect itself succeeded; only the stream broke"
    assert result.error is not None, (
        "a reset mid-stream must be recorded on SseStreamResult.error, not swallowed and "
        "not left to crash the caller with an unrelated exception"
    )


def _run_json_scenarios(failures: list[str]) -> None:
    server, thread = _start_fixture_server()
    try:
        port = server.server_address[1]
        client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)
        try:
            status_code, body = client.get_json("/api/status")
            assert status_code == 200, f"expected 200, got {status_code}"
            assert body == FIXTURE_STATUS_BODY, f"body mismatch: {body}"
            print("  PASS  get_json() parses /api/status through the real BenchClient")
        except AssertionError as failure:
            print(f"  FAIL  get_json(): {failure}")
            failures.append(f"get_json: {failure}")
        try:
            status_code, body = client.post_json("/api/c6/reset")
            assert status_code == 202, f"expected 202, got {status_code}"
            assert body == FIXTURE_RESET_BODY, f"body mismatch: {body}"
            print("  PASS  post_json() parses /api/c6/reset through the real BenchClient")
        except AssertionError as failure:
            print(f"  FAIL  post_json(): {failure}")
            failures.append(f"post_json: {failure}")
    finally:
        _stop_fixture_server(server, thread)


def run_self_test() -> int:
    print(
        "Running offline self-tests -- the real BenchClient.stream_sse()/get_json()/"
        "post_json() against a local http.server fixture serving byte-exact "
        "PsychicEventSource framing. No device required, no inline parse loop.\n"
    )
    failures: list[str] = []

    _run_sse_scenario("normal frames parse with zero gaps", "normal", _check_normal, failures)
    _run_sse_scenario("gapped fixture reports exactly 2 gaps", "gapped", _check_gapped, failures)
    _run_sse_scenario(
        "truncated fixture is reported, not silently dropped", "truncated", _check_truncated, failures,
    )
    _run_sse_scenario(
        "mid-stream reset is recorded on the result, never raised", "connreset", _check_connreset, failures,
    )
    _run_json_scenarios(failures)

    if failures:
        print(f"\n{len(failures)} self-test failure(s) detected.")
        return EXIT_SELF_TEST_FAILURE
    print("\nAll self-tests passed.")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "P4 ESP-Hosted soak harness implementing the #184 verdict contract "
            "(issue #197). Every device run is coordinator/operator-run; this tool "
            "never flashes and never calls `make ota`."
        ),
    )
    parser.add_argument(
        "--self-test", action="store_true",
        help="run offline self-tests against a local http.server fixture (no device "
             "required); exits non-zero if any assertion fails",
    )
    parser.add_argument(
        "--device", default=None,
        help="bench device hostname or IP. bringup/p4_hosted_bench.cpp does not "
             "register mDNS, so there is no usable default -- required for any real run",
    )
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--connect-timeout-s", type=float, default=10.0)
    parser.add_argument(
        "--driver", choices=["all", "sse_soak", "reconnect_storm", "c6_reset_recovery"], default="all",
    )
    parser.add_argument(
        "--duration", type=float, default=1800.0,
        help="sse_soak duration in seconds (default 1800 = 30 minutes, matching #184's "
             "'tens of minutes')",
    )
    parser.add_argument(
        "--num-clients", type=int, default=PA_ADMISSION_MAX_SSE_CLIENTS,
        help=f"concurrent SSE clients for sse_soak. Only {PA_ADMISSION_MAX_SSE_CLIENTS} "
             "(production's live cap, PA_ADMISSION_MAX_SSE_CLIENTS) counts toward the "
             "verdict; higher counts are logged as observation only, per #184",
    )
    parser.add_argument("--status-poll-interval-s", type=float, default=5.0)
    parser.add_argument(
        "--early-stall-check-s", type=float, default=10.0,
        help="if no sse_soak client has received a single frame within this many "
             "seconds, fail fast as 'SSE immediately stalled' instead of waiting out "
             "the full --duration",
    )
    parser.add_argument("--storm-clients", type=int, default=3)
    parser.add_argument("--storm-duration", type=float, default=120.0)
    parser.add_argument("--storm-cycle-min-s", type=float, default=0.5)
    parser.add_argument("--storm-cycle-max-s", type=float, default=3.0)
    parser.add_argument("--storm-settle-s", type=float, default=5.0)
    parser.add_argument("--reset-recovery-timeout-s", type=float, default=30.0)
    parser.add_argument("--reset-poll-interval-s", type=float, default=0.5)
    parser.add_argument("--sse-resume-timeout-s", type=float, default=10.0)
    parser.add_argument(
        "--heap-recovery-tolerance-pct", type=float, default=20.0,
        help="allowed percentage drop in largestFree8bitBlock -- used both for the "
             "long-soak heap-trend check and for the reset-recovery heap comparison -- "
             "before recording a fragmentation FAIL",
    )
    parser.add_argument("--json", default=None, help="also write the full report to this path")
    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    if not args.device:
        parser.error("--device is required (the bench sketch has no mDNS name to default to)")
    if args.num_clients < 1:
        parser.error("--num-clients must be >= 1")
    if args.storm_clients < 1:
        parser.error("--storm-clients must be >= 1")

    report, exit_code = run(args)
    rendered = json.dumps(report, indent=2, default=str)
    print(rendered)
    if args.json:
        Path(args.json).write_text(rendered + "\n")
    print(f"\nVerdict: {report['verdict']} (exit {exit_code})", file=sys.stderr)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
