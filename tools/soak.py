#!/usr/bin/env python3
"""protoArtoo soak harness -- implements the #184 verdict contract (#197).

Drives a protoArtoo controller over HTTP/SSE to answer: does the web stack
hold up over a long run, beyond the reduced manual smoke #184 already ran?
Every device run here is coordinator/operator-initiated; this tool never
flashes, never calls `make ota`, and never touches firmware sources.

Named `soak.py` rather than `p4_hosted_soak.py` since #194: the harness reads
more than the one ESP-Hosted bench image it was written for, and a name that
claims otherwise sends the next reader looking for a P4 in a run that has
none.

Two images can be driven, selected by --image and never sniffed (see
StatusSchema for why the declaration is checked rather than inferred):

  bench     bringup/p4_hosted_bench.cpp, env firebeetle2_hosted_bench. Built
            to be measured: bootCount, the raw esp_reset_reason_t, flat
            recovery-ladder counters, POST /api/c6/reset, and an /api/events
            stream whose payload is a monotonic frame counter.
  shipping  the firebeetle2 product image, src/web/web_server.cpp
            buildStatusJson(). No bootCount, resetReason as a string, the
            ladder nested under "hostedLink", no reset route at all (#243),
            and an /api/events stream of named status/rc/log events rather
            than a counter.

Structural rule (non-negotiable, #197 pinned comment): there is exactly ONE
SSE frame-parsing implementation (SseFrameParser, driven only through
BenchClient.stream_sse()), one continuity wiring point
(stream_sse_with_continuity()) and one status-field map per image
(StatusSchema); --self-test exercises all three by starting a local
http.server fixture and driving those same production entry points against
it -- never a second, hand-rolled parse loop that only the test sees. Three
prior attempts on this ticket failed by drifting from that rule; see the
issue's pinned comment for the full attempt log.

Bench endpoint contract, read directly from bringup/p4_hosted_bench.cpp
rather than assumed (b990b88, 1ee0640):
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

Shipping endpoint contract, read from src/web/web_seam_routes.cpp and
src/web/web_server.cpp:
  GET  /api/status    -> buildStatusJson():393; resetReasonName() at :401,
                          hostedLink at :724-742 behind PA_CAP_HOSTED_WIFI.
  GET  /api/events    -> SSE; eventStreamTask() (:793-900) broadcasts "rc"
                          every 1s tick, "status" on demand and "log" every
                          other tick, framed by webEventStreamFormatPrefix()
                          (src/web/web_event_stream.cpp:106) with millis() as
                          the id. api_events.cpp refuses a fourth stream with
                          503 (PA_ADMISSION_MAX_SSE_CLIENTS).
  POST /api/c6/reset   -> does not exist. run_c6_reset_recovery() refuses
                          rather than reporting anything (#243).

Fixture-derivation and ESP_RST_* constants were read from the pinned vendor
source, the ESP-IDF header and the firmware itself (see SseFrame's docstring,
BAD_RESET_REASONS and SHIPPING_CRASH_SHAPED_RESET_NAMES below) rather than
invented -- the #197 pinned comment documents two prior guesses that were
wrong for exactly this reason.
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

# src/reset_reason.cpp -- resetReasonName()'s switch, read in full. The
# shipping image publishes this NAME, not the enum value
# (src/web/web_server.cpp:401), and the mapping is deliberately not a
# bijection: every reason the switch does not case on (ESP_RST_CPU_LOCKUP,
# ESP_RST_PWR_GLITCH, ESP_RST_USB, ESP_RST_JTAG, ESP_RST_EFUSE) falls to its
# default and is published as "OTHER". A CPU lockup is therefore
# indistinguishable from a JTAG reset on that image, which is why the
# shipping classification below is tri-state: "OTHER" is recorded as unknown
# and never as "not crash-shaped". src/ is fenced for this ticket, so the
# collapse is reported on #197 rather than fixed here.
SHIPPING_CRASH_SHAPED_RESET_NAMES = {"PANIC", "INT_WDT", "TASK_WDT", "WDT"}
SHIPPING_CLEAN_RESET_NAMES = {"POWERON", "EXTERNAL", "SOFTWARE", "DEEPSLEEP", "BROWNOUT", "SDIO"}
SHIPPING_UNKNOWN_RESET_NAMES = {"UNKNOWN", "OTHER"}

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
    # None when the handshake never produced a response at all. Kept apart
    # from status_line so callers can classify a refusal without parsing
    # prose: the shipping image answers 503 when PA_ADMISSION_MAX_SSE_CLIENTS
    # streams are already open (src/web/api_events.cpp), which under a
    # reconnect storm at the cap is admission working, not a transport fault.
    status_code: Optional[int] = None


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
        status_code: Optional[int] = None
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
            status_code = resp.status
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
            status_code=status_code,
        )


# ---------------------------------------------------------------------------
# Trust-boundary helpers. A missing key means unknown, not False -- an
# absent boolean must never read as a confident negative (or positive).
# ---------------------------------------------------------------------------


def _type_mismatch(value: Any, expected_type: type) -> bool:
    """True when `value` may not be read as `expected_type`.

    bool is a subclass of int in Python, so a payload that sent
    `"bootCount": true` would pass a bare isinstance(value, int) check and be
    read as the integer 1 -- a JSON type error turned into a plausible
    reading, at exactly the trust boundary this harness exists to police."""
    if not isinstance(value, expected_type):
        return True
    return expected_type is int and isinstance(value, bool)


def _require_field(body: dict, field: str, expected_type: type, context: str,
                   container: Optional[str] = None) -> Any:
    """One-shot, hard-raising validation for baseline/response checks that
    happen once per driver, before any evidence has been gathered -- a
    contract violation here is cheap to fail loudly on. Callers that need
    to survive an occasional malformed sample inside a long polling loop
    use _collect_field / _safe_field below instead.

    `container` names a nested object to read the field out of: the shipping
    image publishes the recovery-ladder counters under "hostedLink"
    (src/web/web_server.cpp:724-742) where the bench publishes them flat at
    the top level. A missing container is reported as a missing container
    rather than as a missing field, so "this image publishes no such block at
    all" can never be read as "this one field happens to be absent"."""
    scope = body
    if container is not None:
        if container not in body:
            raise KeyError(
                f"{context}: response is missing required object {container!r}: {body!r}"
            )
        scope = body[container]
        if not isinstance(scope, dict):
            raise TypeError(
                f"{context}: {container!r} has type {type(scope).__name__}, expected a JSON object"
            )
        context = f"{context} {container!r}"
    if field not in scope:
        raise KeyError(f"{context}: response is missing required field {field!r}: {scope!r}")
    value = scope[field]
    if _type_mismatch(value, expected_type):
        raise TypeError(
            f"{context}: field {field!r} has type {type(value).__name__}, "
            f"expected {expected_type}: {value!r}"
        )
    return value


def _collect_field(samples: list[dict], field: str, expected_type: type, anomalies: list[str],
                   container: Optional[str] = None) -> list:
    """Soft validation for repeated polling samples collected over a long
    (potentially multi-hour) run: one malformed sample is recorded as an
    anomaly and skipped, rather than discarding the whole run's evidence.
    `container` is the nested-object path described on _require_field."""
    values = []
    path = field if container is None else f"{container}.{field}"
    for index, sample in enumerate(samples):
        scope = sample
        if container is not None:
            scope = sample.get(container)
            if not isinstance(scope, dict):
                anomalies.append(
                    f"sample[{index}] missing/invalid object {container!r} (needed for {path!r})"
                )
                continue
        if field not in scope:
            anomalies.append(f"sample[{index}] missing field {path!r}")
            continue
        value = scope[field]
        if _type_mismatch(value, expected_type):
            anomalies.append(
                f"sample[{index}] field {path!r} has type {type(value).__name__}, expected {expected_type}"
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
# SSE continuity models -- what "the stream stayed continuous" means on each
# image. The framing is the same on both (SseFrameParser reads it); what is
# carried inside it is not:
#
#   bench     bringup/p4_hosted_bench.cpp:812 sends the monotonic frame
#             counter as the whole payload, one frame per second, with no
#             id: and no event: line. Continuity is arithmetic.
#   shipping  eventStreamTask() (src/web/web_server.cpp:793-900) ticks once a
#             second while any client is connected and broadcasts "rc" every
#             tick, "status" on demand and "log" every other tick when there
#             are new lines. webEventStreamFormatPrefix()
#             (src/web/web_event_stream.cpp:106) puts millis() in `id:` and
#             the event name in `event:`; the payload is JSON. There is no
#             counter anywhere, so continuity is arrival timing plus framing
#             shape.
#
# Running the counter model against the shipping stream is not a smaller
# measurement, it is a vacuous one: no shipping payload parses as a bare
# integer, the counter list stays empty, and count_frame_gaps([]) == 0
# reports perfect continuity for a stream that delivered nothing at all.
# ---------------------------------------------------------------------------

# src/web/web_server.cpp:845/869/895 -- the only three event names the
# shipping firmware broadcasts. "rc" is the heartbeat: it is the one the tick
# emits unconditionally, where "status" is on demand and "log" only fires
# when there are new lines.
SHIPPING_SSE_EVENT_NAMES = ("status", "rc", "log")
SHIPPING_SSE_HEARTBEAT_EVENT = "rc"


class SseContinuityTracker:
    """One client's stream, judged by one image's continuity model. Fed
    exclusively through stream_sse_with_continuity() so a live soak and
    --self-test drive identical wiring -- a tracker the self-test feeds by
    hand would prove nothing about the path the device run takes."""

    model = ""

    def stream_started(self, ts: float) -> None:
        """Called once, immediately before the connection is opened. A model
        whose verdict does not depend on timing does not need it."""

    def observe(self, frame: SseFrame, ts: float) -> None:
        raise NotImplementedError

    def stream_ended(self, ts: float, stopped_by_harness: bool) -> None:
        """Called once the stream has ended. `stopped_by_harness` is False
        when the peer (or the transport) ended it before the run asked it
        to."""

    @property
    def frame_count(self) -> int:
        raise NotImplementedError

    def per_client_fields(self) -> dict:
        raise NotImplementedError

    @staticmethod
    def summarize(trackers: list[SseContinuityTracker], max_silence_s: float) -> tuple[dict, list[str]]:
        """Run-level fields and FAIL reasons across every client's tracker,
        in client-index order."""
        raise NotImplementedError


class CounterFrameTracker(SseContinuityTracker):
    """Bench continuity: the payload IS the counter, so a gap is any place
    the sequence does not advance by exactly 1.

    frame_count counts frames that carried a usable counter rather than
    frames received -- a payload that stops being a bare integer is a parse
    anomaly, and counting it as a delivered frame would hide it. This is the
    pre-schema harness's own behaviour (its per-client frameCount was
    len(counters)), kept identical so bench verdicts do not move."""

    model = "counter"

    def __init__(self) -> None:
        self.counters: list[int] = []

    def observe(self, frame: SseFrame, ts: float) -> None:
        if frame.counter is not None:
            self.counters.append(frame.counter)

    @property
    def frame_count(self) -> int:
        return len(self.counters)

    @property
    def gaps(self) -> int:
        return count_frame_gaps(self.counters)

    def per_client_fields(self) -> dict:
        return {
            "gaps": self.gaps,
            "firstCounter": self.counters[0] if self.counters else None,
            "lastCounter": self.counters[-1] if self.counters else None,
        }

    @staticmethod
    def summarize(trackers: list[SseContinuityTracker], max_silence_s: float) -> tuple[dict, list[str]]:
        # max_silence_s carries no verdict for this model: the bench stream's
        # continuity is arithmetic, and a stalled bench stream shows up as a
        # counter gap the moment it resumes (or as a read timeout if it does
        # not). Accepted for one call signature across both models.
        total_gaps = sum(t.gaps for t in trackers)
        fields = {"sseContinuityModel": "counter", "totalFrameGaps": total_gaps}
        reasons: list[str] = []
        if total_gaps > 0:
            reasons.append(f"total_frame_gaps == {total_gaps} (frame continuity broken)")
        return fields, reasons


class HeartbeatFrameTracker(SseContinuityTracker):
    """Shipping continuity: arrival timing plus framing shape.

    Three independent things are watched, because no one of them alone can
    tell a live stream from a dead one:

      silence   the longest interval with no frame at all, measured from the
                moment the connection opened (so "never delivered a first
                frame" is a silence, not an empty list nobody looks at) and,
                when the peer ends the stream early, through to that end.
      shape     every shipping broadcast passes an event name, so a frame
                without one means this is not the stream this model was
                written against -- recorded rather than absorbed.
      id        webEventStreamFormatPrefix() puts millis() in `id:`, which
                only ever moves forward on a running device.
    """

    model = "heartbeat"

    def __init__(self) -> None:
        self.frames = 0
        self.event_counts: dict[str, int] = {}
        self.frames_without_event = 0
        self.id_regressions = 0
        self.last_id: Optional[int] = None
        self.max_silence_s = 0.0
        self.max_heartbeat_silence_s = 0.0
        self.first_frame_latency_s: Optional[float] = None
        self.ended_early = False
        self._stream_started_at: Optional[float] = None
        self._last_frame_at: Optional[float] = None
        self._last_heartbeat_at: Optional[float] = None

    def stream_started(self, ts: float) -> None:
        # Both "last seen" clocks start at the connection, not at the first
        # frame: a stream that opens and then says nothing has to read as a
        # silence rather than as a client with no data to judge.
        self._stream_started_at = ts
        self._last_frame_at = ts
        self._last_heartbeat_at = ts

    def observe(self, frame: SseFrame, ts: float) -> None:
        self.frames += 1
        if self._last_frame_at is not None:
            self.max_silence_s = max(self.max_silence_s, ts - self._last_frame_at)
        self._last_frame_at = ts
        if self.first_frame_latency_s is None and self._stream_started_at is not None:
            self.first_frame_latency_s = ts - self._stream_started_at

        if frame.event:
            self.event_counts[frame.event] = self.event_counts.get(frame.event, 0) + 1
            if frame.event == SHIPPING_SSE_HEARTBEAT_EVENT:
                if self._last_heartbeat_at is not None:
                    self.max_heartbeat_silence_s = max(
                        self.max_heartbeat_silence_s, ts - self._last_heartbeat_at
                    )
                self._last_heartbeat_at = ts
        else:
            self.frames_without_event += 1

        if frame.id is not None:
            if self.last_id is not None and frame.id < self.last_id:
                self.id_regressions += 1
            self.last_id = frame.id

    def stream_ended(self, ts: float, stopped_by_harness: bool) -> None:
        self.ended_early = not stopped_by_harness
        if self.ended_early and self._last_frame_at is not None:
            # A stream the peer closed on its own leaves a silence the run
            # never asked for. Counting it is what makes a mid-run clean EOF
            # visible: without it, a stream that dies quietly halfway through
            # merely produces a smaller frame count, which no threshold here
            # would ever object to.
            self.max_silence_s = max(self.max_silence_s, ts - self._last_frame_at)

    @property
    def frame_count(self) -> int:
        return self.frames

    @property
    def heartbeat_frame_count(self) -> int:
        return self.event_counts.get(SHIPPING_SSE_HEARTBEAT_EVENT, 0)

    def per_client_fields(self) -> dict:
        return {
            "eventCounts": dict(sorted(self.event_counts.items())),
            "heartbeatEvent": SHIPPING_SSE_HEARTBEAT_EVENT,
            "heartbeatFrameCount": self.heartbeat_frame_count,
            "maxSilenceS": round(self.max_silence_s, 3),
            "maxHeartbeatSilenceS": round(self.max_heartbeat_silence_s, 3),
            "firstFrameLatencyS": (
                None if self.first_frame_latency_s is None else round(self.first_frame_latency_s, 3)
            ),
            "idRegressions": self.id_regressions,
            "framesWithoutEventName": self.frames_without_event,
            "endedBeforeHarnessStoppedIt": self.ended_early,
        }

    @staticmethod
    def summarize(trackers: list[SseContinuityTracker], max_silence_s: float) -> tuple[dict, list[str]]:
        reasons: list[str] = []
        observed_names: set[str] = set()
        for index, tracker in enumerate(trackers):
            observed_names.update(tracker.event_counts)
            if tracker.frames == 0:
                # A client that received nothing at all is already reported
                # by run_sse_soak()'s zero-frame condition; repeating it here
                # as a silence would say the same thing twice.
                continue
            if tracker.max_silence_s > max_silence_s:
                reasons.append(
                    f"client {index} saw {tracker.max_silence_s:.1f}s with no SSE frame "
                    f"(limit {max_silence_s}s) -- eventStreamTask() broadcasts once a "
                    "second while a client is connected"
                )
            if tracker.ended_early:
                reasons.append(
                    f"client {index}'s stream ended before the harness stopped it "
                    f"(after {tracker.frames} frame(s))"
                )
            if tracker.heartbeat_frame_count == 0:
                reasons.append(
                    f"client {index} received {tracker.frames} frame(s) but never a "
                    f"{SHIPPING_SSE_HEARTBEAT_EVENT!r} event -- that is the one the 1 Hz "
                    "tick emits unconditionally"
                )
            if tracker.frames_without_event > 0:
                reasons.append(
                    f"client {index} received {tracker.frames_without_event} frame(s) with no "
                    "event: name -- every shipping broadcast passes one, so this stream is "
                    "not the one this schema reads"
                )
            if tracker.id_regressions > 0:
                reasons.append(
                    f"client {index} saw {tracker.id_regressions} frame id(s) go backwards -- "
                    "id is millis() at broadcast time, so it only moves backwards across a "
                    "restart or a 49.7-day millis() wrap"
                )
        fields = {
            "sseContinuityModel": "heartbeat",
            "sseMaxSilenceLimitS": max_silence_s,
            "maxSilenceSObserved": round(max((t.max_silence_s for t in trackers), default=0.0), 3),
            "maxHeartbeatSilenceSObserved": round(
                max((t.max_heartbeat_silence_s for t in trackers), default=0.0), 3
            ),
            "totalHeartbeatFrames": sum(t.heartbeat_frame_count for t in trackers),
            "eventNamesObserved": sorted(observed_names),
            "unexpectedEventNames": sorted(observed_names.difference(SHIPPING_SSE_EVENT_NAMES)),
            "totalFramesWithoutEventName": sum(t.frames_without_event for t in trackers),
            "totalIdRegressions": sum(t.id_regressions for t in trackers),
        }
        return fields, reasons


def stream_sse_with_continuity(
    client: BenchClient, schema: StatusSchema, stop: threading.Event,
    read_chunk_timeout_s: float, path: str = DEFAULT_SSE_PATH,
    on_frame: Optional[Callable[[SseFrame, float], None]] = None,
) -> tuple[SseStreamResult, SseContinuityTracker]:
    """Open one SSE stream and feed every frame to this image's continuity
    tracker. The single wiring point between BenchClient.stream_sse() and the
    continuity model: the soak worker and --self-test both go through here,
    so a self-test that passes cannot be exercising wiring the device run
    does not use."""
    tracker = schema.new_continuity_tracker()
    tracker.stream_started(time.monotonic())

    def observe(frame: SseFrame, ts: float) -> None:
        tracker.observe(frame, ts)
        if on_frame is not None:
            on_frame(frame, ts)

    result = client.stream_sse(path, observe, stop, read_chunk_timeout_s=read_chunk_timeout_s)
    tracker.stream_ended(time.monotonic(), stopped_by_harness=stop.is_set())
    return result, tracker


# ---------------------------------------------------------------------------
# Status schema -- the two /api/status payloads this harness can drive.
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class ResetReasonAssessment:
    """What the image said the last reset was, and whether that is
    crash-shaped. `crash_shaped is None` means this image cannot tell -- it
    is never False, because "the mapping collapses this case" and "the device
    started cleanly" are different claims."""

    display: Any
    crash_shaped: Optional[bool]
    caveat: Optional[str] = None


@dataclasses.dataclass(frozen=True)
class LadderReading:
    """One sample of the bounded transport-failure recovery ladder. Both
    images publish the same five quantities under different names and, on
    shipping, inside a nested object."""

    state: str
    transport_failure_count: int
    transport_up_event_count: int
    attempt_count: int
    recovered_count: int


@dataclasses.dataclass
class LadderSamples:
    states: list[str]
    transport_failure_counts: list[int]
    transport_up_event_counts: list[int]
    attempt_counts: list[int]
    recovered_counts: list[int]


class StatusSchema:
    """How to read one firmware image's GET /api/status.

    The two images this harness can drive publish different payloads for the
    same measurements, and the differences are structural rather than
    cosmetic:

      bench     bringup/p4_hosted_bench.cpp handleStatus() -- built to be
                measured: bootCount (RTC_DATA_ATTR, survives a CPU reset and
                not a power cycle), resetReason as the raw esp_reset_reason_t
                int, the ladder counters flat at the top level, and
                POST /api/c6/reset.
      shipping  src/web/web_server.cpp buildStatusJson():393 -- built for the
                dashboard: no bootCount at all, resetReason as
                resetReasonName()'s string (:401), heapLargest8bit and
                sseClients rather than largestFree8bitBlock and
                sseClientsConnected, the ladder nested under "hostedLink"
                (:724-742, behind the PA_CAP_HOSTED_WIFI board capability
                gate), and no reset route anywhere in src/ (that is #243).

    Which schema is in use is an operator declaration (--image), never
    sniffed from the payload: sniffing turns a truncated or half-built
    response into a confident claim about which firmware is on the board, and
    every field would then be silently re-labelled. The declaration is
    checked against the payload instead (structural_mismatches()), so a wrong
    --image fails loudly at preflight.

    A field one image does not publish is a property of that image
    (publishes_boot_count), not a .get() that quietly returns None: absent
    must read as absent, never as zero."""

    name = ""
    reset_path: Optional[str] = None
    publishes_boot_count = False
    enforces_sse_client_cap = False
    reset_reason_kind = ""
    heap_field = ""
    sse_clients_field = ""
    restart_field = ""
    restart_verb = ""
    ladder_container: Optional[str] = None
    ladder_fields: dict[str, str] = {}
    continuity_tracker_class: type = SseContinuityTracker

    # -- continuity ------------------------------------------------------

    def new_continuity_tracker(self) -> SseContinuityTracker:
        return self.continuity_tracker_class()

    def summarize_continuity(
        self, trackers: list[SseContinuityTracker], max_silence_s: float,
    ) -> tuple[dict, list[str]]:
        return self.continuity_tracker_class.summarize(trackers, max_silence_s)

    # -- field map -------------------------------------------------------

    def fields_read(self) -> dict:
        """The exact payload paths this schema reads, published in every
        report so the field map is auditable from the evidence itself rather
        than only from this source file."""
        prefix = f"{self.ladder_container}." if self.ladder_container else ""
        return {
            "image": self.name,
            "heapLargestFreeBlock": self.heap_field,
            "sseClients": self.sse_clients_field,
            "restartEvidence": self.restart_field,
            "resetReason": f"resetReason ({self.reset_reason_kind})",
            "bootCount": "bootCount" if self.publishes_boot_count else "<not published>",
            "recoveryLadder": {key: prefix + name for key, name in self.ladder_fields.items()},
            "resetRoute": self.reset_path or "<no reset route on this image>",
        }

    # -- generic readers -------------------------------------------------

    def heap_largest_free(self, body: dict, context: str) -> int:
        return _require_field(body, self.heap_field, int, context)

    def collect_heap(self, samples: list[dict], anomalies: list[str]) -> list[int]:
        return _collect_field(samples, self.heap_field, int, anomalies)

    def sse_clients(self, body: dict, context: str) -> int:
        return _require_field(body, self.sse_clients_field, int, context)

    def collect_sse_clients(self, samples: list[dict], anomalies: list[str]) -> list[int]:
        return _collect_field(samples, self.sse_clients_field, int, anomalies)

    def restart_marker(self, body: dict, context: str) -> int:
        return _require_field(body, self.restart_field, int, context)

    def collect_restart_markers(self, samples: list[dict], anomalies: list[str]) -> list[int]:
        return _collect_field(samples, self.restart_field, int, anomalies)

    def ladder(self, body: dict, context: str) -> LadderReading:
        names = self.ladder_fields
        return LadderReading(
            state=_require_field(body, names["state"], str, context, container=self.ladder_container),
            transport_failure_count=_require_field(
                body, names["transportFailureCount"], int, context, container=self.ladder_container),
            transport_up_event_count=_require_field(
                body, names["transportUpEventCount"], int, context, container=self.ladder_container),
            attempt_count=_require_field(
                body, names["attemptCount"], int, context, container=self.ladder_container),
            recovered_count=_require_field(
                body, names["recoveredCount"], int, context, container=self.ladder_container),
        )

    def collect_ladder(self, samples: list[dict], anomalies: list[str]) -> LadderSamples:
        names = self.ladder_fields
        return LadderSamples(
            states=_collect_field(
                samples, names["state"], str, anomalies, container=self.ladder_container),
            transport_failure_counts=_collect_field(
                samples, names["transportFailureCount"], int, anomalies, container=self.ladder_container),
            transport_up_event_counts=_collect_field(
                samples, names["transportUpEventCount"], int, anomalies, container=self.ladder_container),
            attempt_counts=_collect_field(
                samples, names["attemptCount"], int, anomalies, container=self.ladder_container),
            recovered_counts=_collect_field(
                samples, names["recoveredCount"], int, anomalies, container=self.ladder_container),
        )

    def reset_reason_soft(self, sample: dict, anomalies: list[str]) -> Optional[ResetReasonAssessment]:
        """Per-poll reset-reason read for a long loop: one malformed sample
        is an anomaly, not the end of the run. Routed through the same
        reset_reason() the baseline uses -- there is one classification, not
        a soft copy of it that could drift from the strict one."""
        try:
            return self.reset_reason(sample, "poll /api/status")
        except (KeyError, TypeError) as contract_error:
            anomalies.append(str(contract_error))
            return None

    # -- per-image ------------------------------------------------------

    def reset_reason(self, body: dict, context: str) -> ResetReasonAssessment:
        raise NotImplementedError

    def restart_detected(self, baseline: int, markers: list[int]) -> bool:
        raise NotImplementedError

    def restart_report(self, baseline: int, final: int, detected: bool) -> dict:
        raise NotImplementedError

    def link_readiness(self, body: dict) -> tuple[bool, str]:
        """(ready, why-not). `ready` is True only on affirmative evidence of
        an established Hosted link; a missing field means the evidence is
        absent, which is not the same as the link having failed, and the
        message says so."""
        raise NotImplementedError

    def structural_mismatches(self, body: dict) -> list[str]:
        """Ways this payload contradicts the declared image."""
        raise NotImplementedError


def _marker_mismatch(body: dict, field: str, expected_type: type) -> Optional[str]:
    """One structural marker check, phrased as the mismatch it found.
    Reuses _require_field so the presence/type rules a marker is judged by
    are the same ones the drivers read the field with."""
    try:
        _require_field(body, field, expected_type, "declared image")
    except (KeyError, TypeError) as mismatch:
        return str(mismatch)
    return None


class BenchStatusSchema(StatusSchema):
    name = "bench"
    reset_path = DEFAULT_RESET_PATH
    publishes_boot_count = True
    # The bench streams through PsychicEventSource, which has no client cap
    # of its own (ADR 0030 / #184's SSE-fidelity note), so a refused stream
    # is never expected here.
    enforces_sse_client_cap = False
    reset_reason_kind = "esp_reset_reason_t int"
    heap_field = "largestFree8bitBlock"
    sse_clients_field = "sseClientsConnected"
    restart_field = "bootCount"
    restart_verb = "advanced"
    ladder_container = None
    # bringup/p4_hosted_bench.cpp handleStatus(), flat at the top level.
    ladder_fields = {
        "state": "recoveryLadderState",
        "transportFailureCount": "hostedTransportFailureCount",
        "transportUpEventCount": "hostedTransportUpEventCount",
        "attemptCount": "recoveryAttemptCount",
        "recoveredCount": "recoveryRecoveredCount",
    }
    continuity_tracker_class = CounterFrameTracker

    def reset_reason(self, body: dict, context: str) -> ResetReasonAssessment:
        value = _require_field(body, "resetReason", int, context)
        # The bench publishes the raw enum value and ESP_RESET_REASON_NAMES
        # was read from esp_system.h in full, so there is no collapsed
        # bucket here and no ambiguous case: every value is classified.
        return ResetReasonAssessment(
            display=ESP_RESET_REASON_NAMES.get(value, value),
            crash_shaped=value in BAD_RESET_REASONS,
        )

    def restart_detected(self, baseline: int, markers: list[int]) -> bool:
        # bootCount is RTC_DATA_ATTR: it survives a CPU reset and not a power
        # cycle, so ANY difference from the baseline is a restart -- a power
        # cycle resets it, which is a decrease, not an increase.
        return any(marker != baseline for marker in markers)

    def restart_report(self, baseline: int, final: int, detected: bool) -> dict:
        return {
            "baselineBootCount": baseline,
            "finalBootCount": final,
            "bootCountAdvanced": detected,
        }

    def link_readiness(self, body: dict) -> tuple[bool, str]:
        hosted_initialized = body.get("hostedIsInitialized")
        wifi_connected = body.get("wifiConnected")
        if hosted_initialized is True and wifi_connected is True:
            return True, ""
        return False, (
            "device is reachable but not a confirmed-ready C6: "
            f"hostedIsInitialized={hosted_initialized!r} wifiConnected={wifi_connected!r} "
            "(missing/false means the required evidence -- an established Hosted "
            "link -- is absent, not that the link failed)"
        )

    def structural_mismatches(self, body: dict) -> list[str]:
        mismatches = []
        for field, expected_type in (
            ("bootCount", int), ("resetReason", int), (self.heap_field, int),
            (self.sse_clients_field, int), (self.ladder_fields["state"], str),
        ):
            mismatch = _marker_mismatch(body, field, expected_type)
            if mismatch is not None:
                mismatches.append(mismatch)
        return mismatches


class ShippingStatusSchema(StatusSchema):
    name = "shipping"
    # No reset route exists anywhere in the shipping image: grep over src/ +
    # include/ returns zero hits for /api/c6/reset, and the seam route table
    # (src/web/web_seam_routes.cpp) registers none. run_c6_reset_recovery()
    # refuses on this, rather than reporting a pass it never provoked; the
    # reset path itself is #243, on epic #206.
    reset_path = None
    # buildStatusJson() publishes no bootCount at all. Recorded as a
    # structural property so nothing can read the absence as zero.
    publishes_boot_count = False
    # src/web/api_events.cpp refuses a stream once
    # PA_ADMISSION_MAX_SSE_CLIENTS are open, with 503 and a short body. Under
    # a reconnect storm at the cap that refusal is admission working as
    # designed, not a transport fault -- run_reconnect_storm() counts it
    # separately for exactly that reason.
    enforces_sse_client_cap = True
    reset_reason_kind = "resetReasonName() string"
    heap_field = "heapLargest8bit"
    sse_clients_field = "sseClients"
    # No bootCount, so the restart evidence is uptimeMs (millis(),
    # web_server.cpp:360) stepping backwards -- see restart_detected().
    restart_field = "uptimeMs"
    restart_verb = "went backwards"
    ladder_container = "hostedLink"
    # src/web/web_server.cpp:724-742, from HostedLinkStatusSnapshot
    # (include/hosted_link_status.h).
    ladder_fields = {
        "state": "phase",
        "transportFailureCount": "transportFailureCount",
        "transportUpEventCount": "transportUpEventCount",
        "attemptCount": "attemptCount",
        "recoveredCount": "recoveredCount",
    }
    continuity_tracker_class = HeartbeatFrameTracker

    def reset_reason(self, body: dict, context: str) -> ResetReasonAssessment:
        name = _require_field(body, "resetReason", str, context)
        if name in SHIPPING_CRASH_SHAPED_RESET_NAMES:
            return ResetReasonAssessment(display=name, crash_shaped=True)
        if name in SHIPPING_CLEAN_RESET_NAMES:
            return ResetReasonAssessment(display=name, crash_shaped=False)
        if name in SHIPPING_UNKNOWN_RESET_NAMES:
            return ResetReasonAssessment(
                display=name, crash_shaped=None,
                caveat=(
                    f"resetReasonName() (src/reset_reason.cpp) reports {name!r}, which does not "
                    "identify one reset: 'OTHER' is its default arm and collapses "
                    "ESP_RST_CPU_LOCKUP, ESP_RST_PWR_GLITCH, ESP_RST_USB, ESP_RST_JTAG and "
                    "ESP_RST_EFUSE into one name, and 'UNKNOWN' is esp_reset_reason() itself "
                    "saying it could not tell. Recorded as unknown rather than as a clean start"
                ),
            )
        return ResetReasonAssessment(
            display=name, crash_shaped=None,
            caveat=(
                f"resetReasonName() (src/reset_reason.cpp) does not produce {name!r} -- this "
                "payload is not the shipping mapping this schema was read from"
            ),
        )

    def restart_detected(self, baseline: int, markers: list[int]) -> bool:
        # The shipping image publishes no bootCount, so the restart evidence
        # is uptimeMs (millis()) stepping backwards: a reboot restarts
        # millis() at 0. Compared against the PREVIOUS sample rather than
        # against the baseline, so a device that reboots and then runs past
        # its old uptime still shows the step down.
        #
        # Two limits, stated rather than hidden: a reboot is missed only if
        # the device rebooted AND accumulated more uptime than the previous
        # sample before the next poll (which needs a poll interval longer
        # than the uptime at that point), and millis() wraps after ~49.7
        # days, which this cannot distinguish from a reboot -- both are
        # reported as a restart, which is the safe direction.
        previous = baseline
        for marker in markers:
            if marker < previous:
                return True
            previous = marker
        return False

    def restart_report(self, baseline: int, final: int, detected: bool) -> dict:
        # No bootCount key on a shipping report, deliberately: a consumer
        # looking for one finds nothing, which is the truth, rather than an
        # uptime reading wearing bootCount's name.
        return {
            "baselineUptimeMs": baseline,
            "finalUptimeMs": final,
            "uptimeMsWentBackwards": detected,
        }

    def link_readiness(self, body: dict) -> tuple[bool, str]:
        hosted_link = body.get(self.ladder_container)
        phase = hosted_link.get("phase") if isinstance(hosted_link, dict) else None
        wifi_connected = body.get("wifiConnected")
        if wifi_connected is True and phase == "idle":
            return True, ""
        return False, (
            "device is reachable but not a confirmed-ready C6: "
            f"wifiConnected={wifi_connected!r} hostedLink.phase={phase!r}. wifiConnected is "
            "WiFi.status() -- 'what does the radio believe' "
            "(src/web/web_network_manager_hosted.cpp:430-439), which #184 proved stays "
            "WL_CONNECTED through a dead SDIO transport, so it is corroborated here with "
            "the link supervisor's own phase: 'idle' means no transport failure is "
            "outstanding, 'armed'/'attempting' mean a recovery run is in flight, and "
            "'degraded' is terminal for this boot (ADR 0032). A missing hostedLink block "
            "means this image was not built with PA_CAP_HOSTED_WIFI, so the evidence is "
            "absent rather than negative"
        )

    def structural_mismatches(self, body: dict) -> list[str]:
        mismatches = []
        # bootCount must be ABSENT. Checked positively so that an image which
        # started publishing one is refused here rather than quietly read
        # through a schema that assumes it cannot exist.
        if "bootCount" in body:
            mismatches.append(
                "declared image: bootCount is present, but the shipping payload "
                "(src/web/web_server.cpp:393) publishes none -- this is not a shipping image"
            )
        for field, expected_type in (
            ("resetReason", str), (self.heap_field, int),
            (self.sse_clients_field, int), (self.restart_field, int),
        ):
            mismatch = _marker_mismatch(body, field, expected_type)
            if mismatch is not None:
                mismatches.append(mismatch)
        hosted_link = body.get(self.ladder_container)
        if not isinstance(hosted_link, dict):
            mismatches.append(
                f"declared image: response has no {self.ladder_container!r} object "
                "(src/web/web_server.cpp:724-742, PA_CAP_HOSTED_WIFI) -- the recovery-ladder "
                "evidence this harness exists to record is not published by this image"
            )
        elif _type_mismatch(hosted_link.get("phase"), str):
            mismatches.append(
                f"declared image: {self.ladder_container}.phase is "
                f"{hosted_link.get('phase')!r}, expected the hostedLinkPhaseName() string"
            )
        return mismatches


SCHEMAS: dict[str, StatusSchema] = {
    "bench": BenchStatusSchema(),
    "shipping": ShippingStatusSchema(),
}


def identify_schema(body: dict) -> Optional[StatusSchema]:
    """Diagnostic only: which declared schema, if exactly one, this payload
    satisfies. Used to name the likely image in a preflight mismatch message
    -- never to select a schema, for the reason in StatusSchema's docstring."""
    matches = [schema for schema in SCHEMAS.values() if not schema.structural_mismatches(body)]
    return matches[0] if len(matches) == 1 else None


# ---------------------------------------------------------------------------
# Driver 1 -- SSE soak: N concurrent long-lived readers, gap detection,
# heap-trend and reboot/reset-reason guards, recovery-ladder visibility.
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class ClientSoakResult:
    index: int
    tracker: SseContinuityTracker
    error: Optional[str]
    truncated: bool
    connect_ok: bool

    @property
    def frame_count(self) -> int:
        return self.tracker.frame_count


def _sse_soak_worker(
    client: BenchClient, schema: StatusSchema, index: int, stop: threading.Event,
    results: list[ClientSoakResult], first_frame_event: threading.Event,
) -> None:
    stream_result, tracker = stream_sse_with_continuity(
        client, schema, stop, read_chunk_timeout_s=5.0,
        on_frame=lambda _frame, _ts: first_frame_event.set(),
    )
    results.append(ClientSoakResult(
        index=index, tracker=tracker, error=stream_result.error,
        truncated=stream_result.truncated, connect_ok=stream_result.connect_ok,
    ))


def run_sse_soak(
    client: BenchClient, schema: StatusSchema, num_clients: int, duration_s: float,
    status_poll_interval_s: float, heap_tolerance_pct: float, early_stall_check_s: float,
    max_silence_s: float,
) -> dict:
    baseline = capture_status(client)
    baseline_restart_marker = schema.restart_marker(baseline, "baseline /api/status")
    baseline_reset = schema.reset_reason(baseline, "baseline /api/status")
    baseline_heap = schema.heap_largest_free(baseline, "baseline /api/status")
    # The recovery ladder, required at the baseline on both images. Its
    # transportUpEventCount is posted by the SDIO driver's own
    # transport_active_cb() (bringup/p4_hosted_bench.cpp:574-589 on the bench,
    # hostedTransportUpHandler() at src/web/web_network_manager_hosted.cpp:317
    # on the shipping image), independent of anything the sketch believes --
    # the corroborating signal #184 added after WiFi.status() was shown to
    # report CONNECTED through a dead transport. Required here rather than
    # read softly, for the same reason the driver that provokes the ladder
    # requires it: a soak with no ladder baseline cannot say afterwards
    # whether the ladder fired.
    baseline_ladder = schema.ladder(baseline, "baseline /api/status")

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
            args=(client, schema, i, stop_events[i], results, first_frame_events[i]),
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
    clients_failed = [r for r in results if r.error is not None]
    zero_frame_clients = [r for r in results if r.frame_count == 0]
    continuity_fields, continuity_reasons = schema.summarize_continuity(
        [r.tracker for r in results], max_silence_s
    )

    reachable_samples = [s for s in status_samples if "_pollError" not in s]
    schema_anomalies: list[str] = []
    restart_marker_samples = schema.collect_restart_markers(reachable_samples, schema_anomalies)
    heap_samples = schema.collect_heap(reachable_samples, schema_anomalies)
    sse_clients_samples = schema.collect_sse_clients(reachable_samples, schema_anomalies)
    ladder_samples = schema.collect_ladder(reachable_samples, schema_anomalies)

    reasons: list[str] = []

    restart_detected = False
    final_restart_marker = baseline_restart_marker
    if restart_marker_samples:
        final_restart_marker = restart_marker_samples[-1]
        restart_detected = schema.restart_detected(baseline_restart_marker, restart_marker_samples)
    elif reachable_samples:
        reasons.append(f"no /api/status poll sample carried a valid {schema.restart_field} -- cannot "
                        "confirm the P4 did not reboot during the soak")

    min_heap = min([baseline_heap] + heap_samples) if heap_samples else baseline_heap
    heap_floor = baseline_heap * (1 - heap_tolerance_pct / 100.0)
    heap_trend_bad = min_heap < heap_floor

    max_sse_clients_observed = max(sse_clients_samples, default=0)
    admission_reached_target = max_sse_clients_observed >= num_clients

    ladder_reached_degraded = "degraded" in ladder_samples.states

    # transport_active_cb() only ever increments; a rise during the soak
    # means the SDIO link independently reported at least one additional
    # active transition (recovery ladder or otherwise) beyond the initial
    # boot-time connect captured in the baseline -- not itself a FAIL
    # condition (a ladder that fires and recovers without reaching
    # 'degraded' is the ladder working as designed), but the corroborating
    # count #184 named this field for.
    transport_up_event_count_end = (
        ladder_samples.transport_up_event_counts[-1]
        if ladder_samples.transport_up_event_counts
        else baseline_ladder.transport_up_event_count
    )
    transport_up_events_during_soak = (
        transport_up_event_count_end - baseline_ladder.transport_up_event_count
    )

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
    reasons.extend(continuity_reasons)
    if clients_failed:
        reasons.append(
            f"{len(clients_failed)} client(s) reported a transport fault: "
            f"{[(r.index, r.error) for r in clients_failed]}"
        )
    if restart_detected:
        reasons.append(
            f"{schema.restart_field} {schema.restart_verb} from {baseline_restart_marker} to "
            f"{final_restart_marker} during the soak (the P4 rebooted; ADR 0032 forbids "
            "relying on a host restart)"
        )
    if baseline_reset.crash_shaped:
        reasons.append(
            f"resetReason at soak start was {baseline_reset.display} -- "
            "the device was already in a crash-shaped reset state before this run began"
        )
    if heap_trend_bad:
        reasons.append(
            f"{schema.heap_field} fell to {min_heap} from baseline {baseline_heap} "
            f"(beyond {heap_tolerance_pct}% tolerance, floor {heap_floor:.0f})"
        )
    if counts_toward_verdict and not admission_reached_target and total_frames > 0:
        reasons.append(
            f"server-reported {schema.sse_clients_field} never reached {num_clients} "
            f"(max observed {max_sse_clients_observed}) though the harness held "
            f"{num_clients} connection(s) open"
        )
    if ladder_reached_degraded:
        reasons.append(
            f"{schema.ladder_fields['state']} reached 'degraded' during the soak -- the bounded "
            "transport-failure recovery ladder exhausted its attempts and is terminal "
            "for this boot by design"
        )

    verdict = ("FAIL" if reasons else "PASS") if counts_toward_verdict else "OBSERVATION_ONLY"

    # Report-key convention, and the reason it is not uniform: heap and SSE
    # client keys keep one harness-level name across both images because both
    # images publish the SAME quantity under different names
    # (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), and the count of
    # open streams) -- statusFieldsRead above says which field each reading
    # actually came from. The restart evidence does NOT get that treatment:
    # bootCount is an RTC counter and uptimeMs is a millisecond clock, so
    # calling an uptime reading "baselineBootCount" would be an invented
    # measurement rather than a renamed one. Each image's restart keys are
    # therefore its own (schema.restart_report()).
    report = {
        "driver": "sse_soak",
        "verdict": verdict,
        "image": schema.name,
        "statusFieldsRead": schema.fields_read(),
        "countsTowardVerdict": counts_toward_verdict,
        "testedAtProductionCap": tested_at_production_cap,
        "reasons": reasons,
        "numClientsRequested": num_clients,
        "durationSRequested": duration_s,
        "immediateStall": immediate_stall,
        "totalFramesReceived": total_frames,
        "clientsFailed": len(clients_failed),
        "perClient": [
            dict(
                {
                    "index": r.index, "frameCount": r.frame_count,
                    "error": r.error, "truncated": r.truncated, "connectOk": r.connect_ok,
                },
                **r.tracker.per_client_fields(),
            )
            for r in results
        ],
        "baselineResetReason": baseline_reset.display,
        "baselineLargestFree8bitBlock": baseline_heap,
        "minLargestFree8bitBlockObserved": min_heap,
        "heapTolerancePct": heap_tolerance_pct,
        "maxSseClientsConnectedObserved": max_sse_clients_observed,
        "admissionReachedTarget": admission_reached_target,
        "recoveryLadderReachedDegraded": ladder_reached_degraded,
        "recoveryLadderStatesObserved": sorted(set(ladder_samples.states)),
        "baselineRecoveryLadderState": baseline_ladder.state,
        "baselineHostedTransportUpEventCount": baseline_ladder.transport_up_event_count,
        "finalHostedTransportUpEventCount": transport_up_event_count_end,
        "hostedTransportUpEventCountAdvancedBy": transport_up_events_during_soak,
        "statusPollSampleCount": len(status_samples),
        "statusPollUnreachableCount": len(status_samples) - len(reachable_samples),
        "statusPollSchemaAnomalies": schema_anomalies[:50],
        "note": (
            "Per-client frameCount is not cross-checked against the server's own frame "
            "counter as an independent pass/fail gate: clients connect at different "
            "points in a shared stream, so raw counts are not directly comparable across "
            "different connection windows (#184's own accepted run showed exactly this: "
            "client counts of 1401/1403/1399 against a server delta of 1397). The "
            "continuity model named in sseContinuityModel is the authoritative per-client "
            "check; the per-client fields let an operator do the cross-check by hand."
        ),
    }
    report.update(continuity_fields)
    report.update(schema.restart_report(baseline_restart_marker, final_restart_marker, restart_detected))
    # Never emitted as False when the image cannot tell: resetReasonName()
    # collapses several distinct reasons into "OTHER" on the shipping image,
    # so an unknown assessment omits the boolean entirely and carries the
    # reason it is unknown instead.
    if baseline_reset.crash_shaped is None:
        report["baselineResetReasonAssessment"] = "unknown"
        report["baselineResetReasonCaveat"] = baseline_reset.caveat
    else:
        report["baselineResetReasonBad"] = baseline_reset.crash_shaped
        report["baselineResetReasonAssessment"] = (
            "crashShaped" if baseline_reset.crash_shaped else "notCrashShaped"
        )
    return report


# ---------------------------------------------------------------------------
# Driver 2 -- reconnect storm: concurrent clients repeatedly connect to
# /api/events and abort mid-stream.
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class StormCycleResult:
    connect_ok: bool
    frame_count: int
    error: Optional[str]
    status_code: Optional[int]


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
            status_code=result.status_code,
        ))


def run_reconnect_storm(
    client: BenchClient, schema: StatusSchema, storm_clients: int, duration_s: float,
    cycle_min_s: float, cycle_max_s: float, settle_s: float, heap_tolerance_pct: float,
) -> dict:
    baseline = capture_status(client)
    baseline_sse_clients = schema.sse_clients(baseline, "baseline /api/status")
    baseline_heap = schema.heap_largest_free(baseline, "baseline /api/status")
    baseline_restart_marker = schema.restart_marker(baseline, "baseline /api/status")

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
    post_sse_clients = schema.sse_clients(post, "post-storm /api/status")
    post_heap = schema.heap_largest_free(post, "post-storm /api/status")
    post_restart_marker = schema.restart_marker(post, "post-storm /api/status")

    all_cycles = [c for worker_cycles in cycles_by_worker for c in worker_cycles]
    total_frames = sum(c.frame_count for c in all_cycles)
    connect_failures = [c for c in all_cycles if not c.connect_ok]
    # A stream refused at the client cap is not a transport fault: the
    # shipping image answers 503 once PA_ADMISSION_MAX_SSE_CLIENTS streams
    # are open (src/web/api_events.cpp), and a storm run at the cap races its
    # own closing sockets, so some cycles legitimately arrive one slot short.
    # Counted separately rather than excused: a run in which the cap refused
    # EVERY cycle measured no reconnect at all, which the zero-stream reason
    # below catches, and a cap that stops releasing slots shows up as the
    # leaked-socket reason.
    def is_capacity_refusal(cycle: StormCycleResult) -> bool:
        return schema.enforces_sse_client_cap and cycle.status_code == 503

    capacity_refusals = [c for c in all_cycles if is_capacity_refusal(c)]
    unexpected_errors = [
        c for c in all_cycles
        if c.connect_ok and c.error is not None and not is_capacity_refusal(c)
    ]
    streams_opened = [c for c in all_cycles if c.status_code == 200]

    heap_floor = baseline_heap * (1 - heap_tolerance_pct / 100.0)
    restart_detected = schema.restart_detected(baseline_restart_marker, [post_restart_marker])

    reasons: list[str] = []
    if not all_cycles:
        reasons.append("reconnect storm completed zero cycles (measured nothing)")
    elif not streams_opened:
        reasons.append(
            f"none of {len(all_cycles)} cycle(s) opened an SSE stream "
            f"({len(capacity_refusals)} refused at the client cap, "
            f"{len(connect_failures)} never connected) -- measured no reconnect at all"
        )
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
            f"{schema.sse_clients_field} did not return to baseline after settling "
            f"({post_sse_clients} > baseline {baseline_sse_clients}) -- leaked socket(s)"
        )
    if post_heap < heap_floor:
        reasons.append(
            f"{schema.heap_field} did not recover within tolerance after the storm "
            f"({post_heap} < floor {heap_floor:.0f}, baseline {baseline_heap})"
        )
    if restart_detected:
        reasons.append(
            f"{schema.restart_field} {schema.restart_verb} from {baseline_restart_marker} to "
            f"{post_restart_marker} during the reconnect storm (the P4 rebooted)"
        )

    verdict = "FAIL" if reasons else "PASS"
    report = {
        "driver": "reconnect_storm",
        "verdict": verdict,
        "image": schema.name,
        "statusFieldsRead": schema.fields_read(),
        "reasons": reasons,
        "stormClients": storm_clients,
        "durationSRequested": duration_s,
        "cycleMinS": cycle_min_s,
        "cycleMaxS": cycle_max_s,
        "cycleCount": len(all_cycles),
        "streamsOpened": len(streams_opened),
        "capacityRefusals": len(capacity_refusals),
        "totalFramesReceived": total_frames,
        "connectFailures": len(connect_failures),
        "unexpectedErrorsDuringHold": len(unexpected_errors),
        "baselineSseClientsConnected": baseline_sse_clients,
        "postSseClientsConnected": post_sse_clients,
        "baselineLargestFree8bitBlock": baseline_heap,
        "postLargestFree8bitBlock": post_heap,
        "heapTolerancePct": heap_tolerance_pct,
    }
    report.update(
        schema.restart_report(baseline_restart_marker, post_restart_marker, restart_detected)
    )
    return report


# ---------------------------------------------------------------------------
# Driver 3 -- C6 reset recovery: schedule an abrupt C6 reset, prove
# host-side rejoin without a P4 restart, prove a fresh SSE stream resumes.
# ---------------------------------------------------------------------------


def run_c6_reset_recovery(
    client: BenchClient, schema: StatusSchema, recovery_timeout_s: float, poll_interval_s: float,
    heap_tolerance_pct: float, sse_resume_timeout_s: float,
) -> dict:
    # Refused here, before any request is sent, rather than skipped by the
    # orchestrator: this driver's whole claim is that it provoked a C6 reset
    # and watched the link come back, and on an image with no reset route
    # there is nothing to provoke. A skip would leave a driver that can
    # report a pass without having measured anything, which is the exact
    # signature three earlier attempts on #197 were rejected for.
    if schema.reset_path is None:
        return {
            "driver": "c6_reset_recovery",
            "verdict": "UNAVAILABLE",
            "image": schema.name,
            "reasons": [
                f"the {schema.name} image publishes no C6 reset route -- POST /api/c6/reset "
                "exists only on bringup/p4_hosted_bench.cpp, and the shipping seam route "
                "table (src/web/web_seam_routes.cpp) registers none. The reset cannot be "
                "provoked, so nothing about recovery can be measured on this image. "
                "Shipping-image C6 reset recovery is tracked on #243; run this driver "
                "against --image bench"
            ],
        }

    baseline = capture_status(client)
    baseline_restart_marker = schema.restart_marker(baseline, "baseline /api/status")
    baseline_reset = schema.reset_reason(baseline, "baseline /api/status")
    baseline_heap = schema.heap_largest_free(baseline, "baseline /api/status")
    # Recovery-ladder baseline (#184, #197): this driver
    # deliberately provokes ESP_HOSTED_EVENT_TRANSPORT_FAILURE via the C6
    # reset it is about to trigger, so it is the one place in this harness
    # where these fields carry the most evidence. Types read from
    # bringup/p4_hosted_bench.cpp:937-957 -- recoveryLadderState is
    # recoveryPhaseName()'s const char* (idle/armed/attempting/degraded),
    # the rest are unsigned int counters.
    baseline_ladder = schema.ladder(baseline, "baseline /api/status")

    # `is not False`, not `is True`: an image whose reset-reason mapping
    # cannot classify the value it published has not shown a clean starting
    # point, and "unknown" must not pass a precondition that "crash-shaped"
    # would fail.
    if baseline_reset.crash_shaped is not False:
        return {
            "driver": "c6_reset_recovery",
            "verdict": "INVALID",
            "image": schema.name,
            "reasons": [
                "device was not in a confirmed-clean reset state "
                f"({baseline_reset.display}) before this test began -- required evidence "
                "(a clean starting point) is missing"
                + (f". {baseline_reset.caveat}" if baseline_reset.caveat else "")
            ],
        }

    status_code, reset_body = client.post_json(schema.reset_path)

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
    ladder_status_samples: list[dict] = []
    last_status: Optional[dict] = None
    # Empty until a poll actually reached the device: a recovery window in
    # which /api/status never answered has no readiness evidence to quote.
    link_why_not = ""
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

        # Recorded on every reachable poll regardless of what the rest of
        # this iteration decides (recording, not gating -- see the batch
        # extraction after the loop for why no new FAIL branch reads these).
        ladder_status_samples.append(last_status)

        restart_marker_field = last_status.get(schema.restart_field)
        if _type_mismatch(restart_marker_field, int):
            poll_schema_anomalies.append(
                f"poll sample missing/invalid {schema.restart_field}: {last_status!r}"
            )
            time.sleep(poll_interval_s)
            continue
        if schema.restart_detected(baseline_restart_marker, [restart_marker_field]):
            recovery_reasons.append(
                f"{schema.restart_field} {schema.restart_verb} from {baseline_restart_marker} to "
                f"{restart_marker_field} -- the P4 host rebooted. ADR 0032 forbids relying on a "
                "host restart, and #184's NO-GO condition is exactly 'abrupt C6 reset cannot "
                "rejoin without P4 restart'"
            )
            break

        poll_reset = schema.reset_reason_soft(last_status, poll_schema_anomalies)
        if poll_reset is not None and poll_reset.crash_shaped:
            recovery_reasons.append(f"resetReason became {poll_reset.display} during recovery")
            break

        # Missing/non-bool means unknown, not "not yet connected" -- treat
        # as still-recovering (keep polling) rather than asserting either
        # way on an unrecognized shape.
        link_ready, link_why_not = schema.link_readiness(last_status)
        if link_ready:
            recovered = True
            break

        time.sleep(poll_interval_s)

    recovered_at_s = time.monotonic() - request_accepted_at

    # Batch-extract the recovery-ladder telemetry gathered above, the same
    # way run_sse_soak() extracts its poll samples: soft per-field
    # validation through poll_schema_anomalies, never a silent default that
    # would misreport "ladder never fired" as "ladder field never sampled".
    # Purely observational -- see the comment on recoveryLadderReachedDegraded
    # below for why this does not gate the verdict.
    ladder_samples = schema.collect_ladder(ladder_status_samples, poll_schema_anomalies)

    final_recovery_ladder_state = (
        ladder_samples.states[-1] if ladder_samples.states else baseline_ladder.state
    )
    final_transport_failure_count = (
        ladder_samples.transport_failure_counts[-1]
        if ladder_samples.transport_failure_counts
        else baseline_ladder.transport_failure_count
    )
    final_transport_up_event_count = (
        ladder_samples.transport_up_event_counts[-1]
        if ladder_samples.transport_up_event_counts
        else baseline_ladder.transport_up_event_count
    )
    final_recovery_attempt_count = (
        ladder_samples.attempt_counts[-1]
        if ladder_samples.attempt_counts
        else baseline_ladder.attempt_count
    )
    final_recovery_recovered_count = (
        ladder_samples.recovered_counts[-1]
        if ladder_samples.recovered_counts
        else baseline_ladder.recovered_count
    )
    # #184's NO-GO/verdict contract for this driver is bootCount/resetReason/
    # wifiConnected+hostedIsInitialized/SSE-resume/heap (all above and
    # unchanged) -- it does not name 'recoveryLadderState reached degraded'
    # as a FAIL condition of C6-RESET RECOVERY specifically. run_sse_soak()
    # treats it as a FAIL because a ladder that goes terminal *during an
    # otherwise-idle soak* is itself the anomaly; here the ladder is
    # deliberately provoked, and a degraded outcome is already caught by
    # the "did not observe an established Hosted link again" reason below
    # (a ladder that reaches degraded cannot also have restored the
    # transport). Recorded per #197 for evidence, and
    # intentionally not added as a second, redundant FAIL path -- flagged
    # on the issue rather than decided here.
    ladder_reached_degraded = "degraded" in ladder_samples.states

    if not recovery_reasons and not recovered:
        # The last readiness check's own words rather than a fixed field
        # list: which fields prove an established link is a property of the
        # image, and this message is what an operator reads first.
        recovery_reasons.append(
            f"did not observe an established Hosted link again within "
            f"{recovery_timeout_s}s of the scheduled reset"
            + (f" -- last readiness check: {link_why_not}" if link_why_not else "")
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

        # Straight through stream_sse(), not stream_sse_with_continuity():
        # this check asks only "did a fresh stream advance at all after the
        # rejoin", which is a two-frame question, not a continuity one.
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

    # The last marker actually read, kept as None when no poll produced a
    # usable one -- an unread marker must not report as "unchanged".
    final_restart_marker = (last_status or {}).get(schema.restart_field)
    if _type_mismatch(final_restart_marker, int):
        final_restart_marker = None
    restart_observed = (
        final_restart_marker is not None
        and schema.restart_detected(baseline_restart_marker, [final_restart_marker])
    )

    post_heap = (last_status or {}).get(schema.heap_field)
    heap_recovered: Optional[bool] = None
    if not _type_mismatch(post_heap, int):
        heap_floor = baseline_heap * (1 - heap_tolerance_pct / 100.0)
        heap_recovered = post_heap >= heap_floor
        if not heap_recovered:
            recovery_reasons.append(
                f"{schema.heap_field} did not recover within tolerance after reset+rejoin "
                f"({post_heap} < floor {heap_floor:.0f}, baseline {baseline_heap})"
            )

    verdict = "PASS" if recovered and sse_resumed and not recovery_reasons else "FAIL"

    report = {
        "driver": "c6_reset_recovery",
        "verdict": verdict,
        "image": schema.name,
        "statusFieldsRead": schema.fields_read(),
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
        "baselineLargestFree8bitBlock": baseline_heap,
        "finalLargestFree8bitBlock": post_heap,
        "heapTolerancePct": heap_tolerance_pct,
        "heapRecoveredWithinTolerance": heap_recovered,
        "baselineRecoveryLadderState": baseline_ladder.state,
        "finalRecoveryLadderState": final_recovery_ladder_state,
        "recoveryLadderStatesObserved": sorted(set(ladder_samples.states)),
        "recoveryLadderReachedDegraded": ladder_reached_degraded,
        "baselineHostedTransportFailureCount": baseline_ladder.transport_failure_count,
        "finalHostedTransportFailureCount": final_transport_failure_count,
        "hostedTransportFailureCountAdvancedBy":
            final_transport_failure_count - baseline_ladder.transport_failure_count,
        "baselineHostedTransportUpEventCount": baseline_ladder.transport_up_event_count,
        "finalHostedTransportUpEventCount": final_transport_up_event_count,
        "hostedTransportUpEventCountAdvancedBy":
            final_transport_up_event_count - baseline_ladder.transport_up_event_count,
        "baselineRecoveryAttemptCount": baseline_ladder.attempt_count,
        "finalRecoveryAttemptCount": final_recovery_attempt_count,
        "recoveryAttemptCountAdvancedBy": final_recovery_attempt_count - baseline_ladder.attempt_count,
        "baselineRecoveryRecoveredCount": baseline_ladder.recovered_count,
        "finalRecoveryRecoveredCount": final_recovery_recovered_count,
        "recoveryRecoveredCountAdvancedBy":
            final_recovery_recovered_count - baseline_ladder.recovered_count,
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
    report.update(
        schema.restart_report(baseline_restart_marker, final_restart_marker, restart_observed)
    )
    return report


# ---------------------------------------------------------------------------
# Orchestration and the #184 verdict vocabulary.
# ---------------------------------------------------------------------------


# A response that is not JSON at all is the same class of problem as one
# missing a field: the device answered with something this harness cannot
# read. json.JSONDecodeError is a ValueError, so it is named explicitly
# rather than caught as one -- an ordinary ValueError from anywhere else is a
# bug in this harness and must still propagate.
CONTRACT_ERRORS = (KeyError, TypeError, json.JSONDecodeError)


def _run_driver_safely(name: str, fn: Callable[..., dict], *args: Any) -> dict:
    """Bounds the blast radius of a response-contract violation to "this
    driver is INVALID" rather than crashing the whole (possibly
    multi-hour) run with a bare traceback. Only contract-shape errors are
    caught here (CONTRACT_ERRORS: _require_field's KeyError/TypeError, plus a
    body that is not JSON -- an HTML error page from a proxy, or a route that
    does not exist on this image) -- anything else is a bug in this harness
    and must propagate, per AGENTS.md "never swallow an error to keep
    moving"."""
    try:
        return fn(*args)
    except CONTRACT_ERRORS as contract_error:
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
    # UNAVAILABLE is ranked BELOW FAIL and above pass: a driver that could
    # not run on this image did not cover its part of #184's contract, which
    # is that row's "required evidence is missing" -- but a driver that
    # actually failed is the more actionable answer, so a real NO-GO is never
    # masked by a coverage gap. An operator who wants an exit code covering
    # only what this image can measure names the drivers explicitly.
    if any(v == "UNAVAILABLE" for v in verdicts):
        return "INVALID / UNKNOWN", EXIT_INVALID_UNKNOWN
    # PASS and OBSERVATION_ONLY (num_clients > production's cap) both fall
    # through here -- #184: "Higher counts may be run and logged, but carry
    # no verdict."
    return "NO IMMEDIATE BLOCKER", EXIT_NO_IMMEDIATE_BLOCKER


def run(args: argparse.Namespace) -> tuple[dict, int]:
    client = BenchClient(args.device, args.port, connect_timeout_s=args.connect_timeout_s)
    schema = SCHEMAS[args.image]
    header = {
        "schemaVersion": 2, "issue": 197, "device": args.device, "port": args.port,
        "image": schema.name, "statusFieldsRead": schema.fields_read(),
    }

    # Preflight: an unreachable device, or a C6 that never came up, cannot
    # produce any of the required evidence -- #184's INVALID/UNKNOWN row
    # ("required evidence is missing"), not NO-GO (NO-GO is a live link
    # that then fails; this is "there was never a link to test").
    try:
        preflight_status = capture_status(client)
    except TRANSPORT_EXCEPTIONS as error:
        return dict(header, **{
            "verdict": "INVALID / UNKNOWN",
            "reasons": [f"device unreachable at {args.device}:{args.port}: {error}"],
            "drivers": {},
        }), EXIT_INVALID_UNKNOWN
    except json.JSONDecodeError as error:
        # Answered, but not with JSON. Same INVALID/UNKNOWN row as
        # unreachable -- the required evidence never arrived -- and reported
        # rather than raised, so a run started overnight leaves a verdict
        # instead of a traceback.
        return dict(header, **{
            "verdict": "INVALID / UNKNOWN",
            "reasons": [
                f"{args.device}:{args.port}{DEFAULT_STATUS_PATH} answered with a body that is "
                f"not JSON: {error}"
            ],
            "drivers": {},
        }), EXIT_INVALID_UNKNOWN

    # The declared --image is checked against the payload before anything is
    # measured. Without this every subsequent read would silently re-label
    # fields: a bench payload read as shipping finds no heapLargest8bit and
    # fails obscurely deep inside a driver, and a shipping payload read as
    # bench would be worse still if the two ever shared enough names.
    mismatches = schema.structural_mismatches(preflight_status)
    if mismatches:
        looks_like = identify_schema(preflight_status)
        hint = (
            f" This payload matches the {looks_like.name!r} schema -- did you mean "
            f"--image {looks_like.name}?"
            if looks_like is not None and looks_like.name != schema.name
            else ""
        )
        return dict(header, **{
            "verdict": "INVALID / UNKNOWN",
            "reasons": [
                f"--image {schema.name} was declared, but /api/status does not match that "
                f"image's payload: {mismatches}.{hint}"
            ],
            "drivers": {},
        }), EXIT_INVALID_UNKNOWN

    link_ready, why_not = schema.link_readiness(preflight_status)
    if not link_ready:
        return dict(header, **{
            "verdict": "INVALID / UNKNOWN",
            "reasons": [why_not],
            "drivers": {},
        }), EXIT_INVALID_UNKNOWN

    drivers_to_run = (
        ["sse_soak", "reconnect_storm", "c6_reset_recovery"] if args.driver == "all" else [args.driver]
    )
    driver_results: dict[str, dict] = {}

    if "sse_soak" in drivers_to_run:
        driver_results["sse_soak"] = _run_driver_safely(
            "sse_soak", run_sse_soak, client, schema, args.num_clients, args.duration,
            args.status_poll_interval_s, args.heap_recovery_tolerance_pct, args.early_stall_check_s,
            args.sse_max_silence_s,
        )
    if "reconnect_storm" in drivers_to_run:
        driver_results["reconnect_storm"] = _run_driver_safely(
            "reconnect_storm", run_reconnect_storm, client, schema, args.storm_clients,
            args.storm_duration, args.storm_cycle_min_s, args.storm_cycle_max_s,
            args.storm_settle_s, args.heap_recovery_tolerance_pct,
        )
    if "c6_reset_recovery" in drivers_to_run:
        driver_results["c6_reset_recovery"] = _run_driver_safely(
            "c6_reset_recovery", run_c6_reset_recovery, client, schema,
            args.reset_recovery_timeout_s, args.reset_poll_interval_s,
            args.heap_recovery_tolerance_pct, args.sse_resume_timeout_s,
        )

    verdict, exit_code = _compose_overall_verdict(driver_results)
    return dict(header, **{
        "verdict": verdict,
        "driversUnavailableOnThisImage": [
            name for name, result in driver_results.items() if result["verdict"] == "UNAVAILABLE"
        ],
        "drivers": driver_results,
    }), exit_code


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


def _shipping_fixture_frame_bytes(event: Optional[str], data: str, frame_id: int) -> bytes:
    """Byte-exact reproduction of what the shipping image puts on the wire:
    webEventStreamFormatPrefix() (src/web/web_event_stream.cpp:106) followed
    by the payload and kWebEventStreamTerminator (:19, "\\r\\n\\r\\n"), sent
    as three segments by webEventStreamBroadcast()
    (src/web/web_request_psychic.cpp:456). An id of 0 and a null/empty event
    name are omitted rather than sent empty, exactly as that function does --
    which is why a nameless frame is reachable as a fixture at all, and worth
    a scenario: it is what a bench stream looks like to a shipping reader."""
    prefix = ""
    if frame_id:
        prefix += f"id: {frame_id}\r\n"
    if event:
        prefix += f"event: {event}\r\n"
    return f"{prefix}data: {data}\r\n\r\n".encode("utf-8")


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

# A representative slice of buildStatusJson() (src/web/web_server.cpp:393),
# with the neighbouring fields kept so the fixture is shaped like the real
# payload rather than only carrying what the reader happens to want: no
# bootCount, resetReason as resetReasonName()'s string (:401), heapLargest8bit
# and sseClients rather than the bench's names, and the hostedLink block from
# :724-742 / include/hosted_link_status.h.
FIXTURE_SHIPPING_STATUS_BODY = {
    "estop": False,
    "uptimeMs": 3600000,
    "firmwareVersion": "self-test",
    "fsVersion": "self-test",
    "resetReason": "POWERON",
    "heapFree": 260000,
    "heapMin": 240000,
    "heapLargestBlock": 150000,
    "heapLargest8bit": 123456,
    "sseClients": 1,
    "sseClientsPeak": 3,
    "wifiRssi": -55,
    "wifiConnected": True,
    "wifiClientConnected": True,
    "littleFsReady": True,
    "hostedLink": {
        "phase": "idle",
        "terminal": False,
        "transportFailureCount": 0,
        "transportUpEventCount": 1,
        "attemptCount": 0,
        "totalAttemptCount": 0,
        "recoveredCount": 0,
        "lastFailureAtMs": 0,
        "lastAttemptAtMs": 0,
        "degradedAtMs": 0,
    },
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
            self._serve_json(200, self.server.status_body)
        else:
            self.send_error(404)

    def do_POST(self) -> None:
        # Counted so a scenario can assert that a driver which claims to have
        # refused really did refuse -- i.e. that it never sent the request at
        # all, rather than sending it and discarding the answer.
        self.server.post_count += 1
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

    def _serve_live(self, frame_for: Callable[[int], bytes], count: int, interval_s: float) -> None:
        """A stream that keeps going, for the scenarios that run a whole
        driver rather than one read. Ends on the client's own disconnect: a
        broken pipe here IS the expected end of a fixture stream (the soak
        stopping, or the storm's deliberate RST), not an error to report."""
        for index in range(count):
            try:
                self.wfile.write(frame_for(index))
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                break
            time.sleep(interval_s)
        self.close_connection = True

    def _serve_sse(self) -> None:
        query = parse_qs(urlsplit(self.path).query)
        mode = query.get("mode", [self.server.default_sse_mode])[0]

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
        elif mode == "shipping_normal":
            # One tick's worth of what eventStreamTask() broadcasts, twice:
            # "rc" every tick, "status" on demand, "log" every other tick,
            # all three sharing that tick's millis() as their id.
            for frame_id, event, payload in (
                (100000, "rc", '{"ch":[1500,1500]}'),
                (100000, "status", '{"estop":false}'),
                (101000, "rc", '{"ch":[1500,1500]}'),
                (101000, "log", "boot line one"),
                (102000, "rc", '{"ch":[1500,1500]}'),
            ):
                self.wfile.write(_shipping_fixture_frame_bytes(event, payload, frame_id))
            self.wfile.flush()
            self.close_connection = True
        elif mode == "shipping_silence":
            # A real gap on the wire, not a synthesised timestamp: the
            # continuity tracker is fed by the live read loop, so the only
            # honest way to test the silence limit is to make the fixture go
            # quiet. 0.35s is long enough to exceed the 0.2s limit the
            # scenario sets and short enough to keep the self-test quick.
            self.wfile.write(_shipping_fixture_frame_bytes("rc", '{"ch":[1500]}', 200000))
            self.wfile.flush()
            time.sleep(0.35)
            self.wfile.write(_shipping_fixture_frame_bytes("rc", '{"ch":[1500]}', 200400))
            self.wfile.flush()
            self.close_connection = True
        elif mode == "shipping_id_regression":
            self.wfile.write(_shipping_fixture_frame_bytes("rc", '{"ch":[1500]}', 300000))
            self.wfile.write(_shipping_fixture_frame_bytes("rc", '{"ch":[1500]}', 299000))
            self.wfile.flush()
            self.close_connection = True
        elif mode == "bench_live":
            self._serve_live(_fixture_frame_bytes, count=25, interval_s=0.2)
        elif mode == "shipping_live":
            self._serve_live(
                lambda index: _shipping_fixture_frame_bytes(
                    "rc", '{"ch":[1500,1500]}', 400000 + index * 200),
                count=25, interval_s=0.2,
            )
        elif mode == "shipping_nameless":
            # Exactly what the bench stream looks like: no id:, no event:,
            # a bare counter payload. A shipping reader pointed at it must
            # say so rather than count it as a delivered event.
            self.wfile.write(_fixture_frame_bytes(7))
            self.wfile.flush()
            self.close_connection = True
        else:
            self.close_connection = True


def _start_fixture_server(
    status_body: Optional[dict] = None, sse_mode: str = "normal",
) -> tuple[http.server.ThreadingHTTPServer, threading.Thread]:
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _FixtureHandler)
    # Per-server, not per-handler: BaseHTTPRequestHandler is instantiated
    # once per request, so anything a scenario configures or counts has to
    # live on the server the handler can reach through self.server.
    server.status_body = FIXTURE_STATUS_BODY if status_body is None else status_body
    # The mode used when the request carries no ?mode= -- which is how the
    # drivers themselves fetch /api/events, since they use the real path.
    server.default_sse_mode = sse_mode
    server.post_count = 0
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread


def _stop_fixture_server(server: http.server.ThreadingHTTPServer, thread: threading.Thread) -> None:
    server.shutdown()
    server.server_close()
    thread.join(timeout=5.0)


def _record_scenario(name: str, body: Callable[[], None], failures: list[str]) -> None:
    """Run one scenario body and record its outcome.

    CONTRACT_ERRORS are recorded as failures alongside assertion failures
    rather than escaping as a traceback. A schema reader that looks for a
    field the payload does not carry raises KeyError/TypeError by design
    (_require_field), so a mutation that mis-names a field would otherwise
    kill the suite mid-run and skip every scenario after it -- the exit code
    would still be non-zero, but the output would say less about which
    measurement broke."""
    try:
        body()
        print(f"  PASS  {name}")
    except (AssertionError,) + CONTRACT_ERRORS as failure:
        print(f"  FAIL  {name}: {failure}")
        failures.append(f"{name}: {failure}")


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
        _record_scenario(name, lambda: check(result, frames_seen), failures)
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


def _run_continuity_scenario(
    name: str, mode: str, schema: StatusSchema, max_silence_s: float,
    check: Callable[[SseStreamResult, SseContinuityTracker, dict, list[str]], None],
    failures: list[str], stop_after_frames: Optional[int] = None,
) -> None:
    """Drive stream_sse_with_continuity() -- the exact wiring
    _sse_soak_worker() uses -- against a fixture, then assert on the tracker
    and on the run-level summary the schema produces from it. Nothing here
    parses SSE or re-derives continuity; both come back from production
    code.

    `stop_after_frames` stops the stream from the harness side once that many
    frames have arrived, which is what a real soak does when its duration
    expires. Without it the fixture's own close is what ends the stream, and
    the tracker correctly reports that as an early end."""
    server, thread = _start_fixture_server()
    try:
        port = server.server_address[1]
        client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)
        stop = threading.Event()
        seen = [0]

        def on_frame(_frame: SseFrame, _ts: float) -> None:
            seen[0] += 1
            if stop_after_frames is not None and seen[0] >= stop_after_frames:
                stop.set()

        result, tracker = stream_sse_with_continuity(
            client, schema, stop, read_chunk_timeout_s=2.0, path=f"/api/events?mode={mode}",
            on_frame=on_frame,
        )
        fields, reasons = schema.summarize_continuity([tracker], max_silence_s)
        _record_scenario(name, lambda: check(result, tracker, fields, reasons), failures)
    finally:
        _stop_fixture_server(server, thread)


def _check_shipping_normal(
    result: SseStreamResult, tracker: SseContinuityTracker, fields: dict, reasons: list[str],
) -> None:
    assert result.error is None, f"unexpected error: {result.error}"
    assert tracker.model == "heartbeat", f"shipping must use the heartbeat model, got {tracker.model!r}"
    assert tracker.frame_count == 5, f"expected 5 frames, got {tracker.frame_count}"
    assert tracker.event_counts == {"rc": 3, "status": 1, "log": 1}, (
        f"event names must come off the wire, got {tracker.event_counts}"
    )
    assert tracker.heartbeat_frame_count == 3, (
        f"expected 3 'rc' heartbeats, got {tracker.heartbeat_frame_count}"
    )
    assert tracker.frames_without_event == 0, "every fixture frame carried an event: name"
    assert tracker.id_regressions == 0, "the fixture ids only move forward"
    assert fields["totalIdRegressions"] == 0 and fields["totalFramesWithoutEventName"] == 0
    assert fields["eventNamesObserved"] == ["log", "rc", "status"], fields["eventNamesObserved"]
    assert fields["unexpectedEventNames"] == [], fields["unexpectedEventNames"]
    assert not tracker.ended_early, "the harness stopped this stream, so it did not end early"
    assert reasons == [], f"a healthy shipping stream must produce no FAIL reason: {reasons}"


def _check_shipping_ended_early(
    result: SseStreamResult, tracker: SseContinuityTracker, fields: dict, reasons: list[str],
) -> None:
    assert tracker.frame_count == 5, f"the frames were delivered, got {tracker.frame_count}"
    assert tracker.ended_early, (
        "the fixture closed the stream on its own, which the harness never asked for"
    )
    assert any("ended before the harness stopped it" in reason for reason in reasons), (
        "a stream that ends early must be a FAIL reason -- otherwise a mid-run clean EOF "
        f"only shows up as a smaller frame count nothing objects to: {reasons}"
    )


def _check_shipping_silence(
    result: SseStreamResult, tracker: SseContinuityTracker, fields: dict, reasons: list[str],
) -> None:
    assert tracker.frame_count == 2, f"expected both frames, got {tracker.frame_count}"
    assert tracker.max_silence_s >= 0.35, (
        f"the fixture went quiet for 0.35s; tracker measured {tracker.max_silence_s:.3f}s"
    )
    assert any("no SSE frame" in reason for reason in reasons), (
        f"a silence past the limit must be a FAIL reason, got {reasons}"
    )
    assert fields["maxSilenceSObserved"] >= 0.35, fields["maxSilenceSObserved"]


def _check_shipping_id_regression(
    result: SseStreamResult, tracker: SseContinuityTracker, fields: dict, reasons: list[str],
) -> None:
    assert tracker.id_regressions == 1, (
        f"id 300000 -> 299000 is one regression, tracker saw {tracker.id_regressions}"
    )
    assert any("go backwards" in reason for reason in reasons), (
        f"an id regression must be a FAIL reason, got {reasons}"
    )


def _check_shipping_nameless(
    result: SseStreamResult, tracker: SseContinuityTracker, fields: dict, reasons: list[str],
) -> None:
    assert tracker.frame_count == 1, f"the frame was delivered, got {tracker.frame_count}"
    assert tracker.frames_without_event == 1, (
        "a bench-style frame has no event: name and must be counted as such, got "
        f"{tracker.frames_without_event}"
    )
    assert tracker.heartbeat_frame_count == 0, "a nameless frame is not an 'rc' heartbeat"
    assert any("no event: name" in reason for reason in reasons), (
        f"a nameless frame must be a FAIL reason, not absorbed: {reasons}"
    )


def _run_shipping_status_scenarios(failures: list[str]) -> None:
    """Read the shipping fixture payload through the production schema
    readers, reached the same way a driver reaches them: capture_status()
    over real HTTP, then ShippingStatusSchema's own accessors."""
    schema = SCHEMAS["shipping"]
    bench_schema = SCHEMAS["bench"]
    server, thread = _start_fixture_server(status_body=FIXTURE_SHIPPING_STATUS_BODY)
    try:
        port = server.server_address[1]
        client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)

        def check(name: str, body: Callable[[], None]) -> None:
            _record_scenario(name, body, failures)

        status = capture_status(client)

        def field_map() -> None:
            assert schema.heap_largest_free(status, "self-test") == 123456, "heapLargest8bit"
            assert schema.sse_clients(status, "self-test") == 1, "sseClients"
            assert schema.restart_marker(status, "self-test") == 3600000, "uptimeMs"
            ladder = schema.ladder(status, "self-test")
            assert ladder.state == "idle", ladder
            assert ladder.transport_up_event_count == 1, ladder
            assert ladder.transport_failure_count == 0, ladder
            assert ladder.attempt_count == 0 and ladder.recovered_count == 0, ladder
            reset = schema.reset_reason(status, "self-test")
            assert reset.display == "POWERON" and reset.crash_shaped is False, reset
            ready, why_not = schema.link_readiness(status)
            assert ready, f"the fixture is a healthy shipping payload: {why_not}"

        check("shipping field map reads nested hostedLink, heapLargest8bit, sseClients", field_map)

        def boot_count_absent() -> None:
            assert schema.publishes_boot_count is False, (
                "the shipping image publishes no bootCount; claiming otherwise makes an "
                "absent field readable as a value"
            )
            assert "bootCount" not in status, "fixture sanity: the shipping payload has no bootCount"
            report = schema.restart_report(3600000, 3601000, False)
            assert "bootCount" not in json.dumps(report), (
                f"a shipping restart report must not carry a bootCount key at all: {report}"
            )
            assert report == {
                "baselineUptimeMs": 3600000, "finalUptimeMs": 3601000,
                "uptimeMsWentBackwards": False,
            }, report

        check("absent bootCount reads as absent, never as zero", boot_count_absent)

        def declared_image_is_checked() -> None:
            assert schema.structural_mismatches(status) == [], (
                "the shipping fixture must satisfy the shipping schema"
            )
            bench_mismatches = bench_schema.structural_mismatches(status)
            assert bench_mismatches, (
                "reading a shipping payload as --image bench must be refused, not accepted"
            )
            assert any("bootCount" in m for m in bench_mismatches), bench_mismatches
            shipping_vs_bench = schema.structural_mismatches(FIXTURE_STATUS_BODY)
            assert shipping_vs_bench, (
                "reading a bench payload as --image shipping must be refused, not accepted"
            )
            assert any("bootCount is present" in m for m in shipping_vs_bench), shipping_vs_bench
            assert identify_schema(status) is schema, "the shipping payload identifies as shipping"
            assert identify_schema(FIXTURE_STATUS_BODY) is bench_schema, (
                "the bench payload identifies as bench"
            )

        check("a wrong --image is refused by the payload itself", declared_image_is_checked)

        def reset_reason_tri_state() -> None:
            crash = schema.reset_reason(dict(status, resetReason="TASK_WDT"), "self-test")
            assert crash.crash_shaped is True, crash
            ambiguous = schema.reset_reason(dict(status, resetReason="OTHER"), "self-test")
            assert ambiguous.crash_shaped is None, (
                "resetReasonName() collapses CPU_LOCKUP/PWR_GLITCH/USB/JTAG/EFUSE into "
                f"'OTHER'; that must read as unknown, never as clean: {ambiguous}"
            )
            assert ambiguous.caveat and "CPU_LOCKUP" in ambiguous.caveat, ambiguous
            unmapped = schema.reset_reason(dict(status, resetReason="NOPE"), "self-test")
            assert unmapped.crash_shaped is None, unmapped

        check("shipping resetReason is a string and its unknowns stay unknown", reset_reason_tri_state)

        def link_readiness_needs_the_ladder() -> None:
            no_block = {k: v for k, v in status.items() if k != "hostedLink"}
            ready, why_not = schema.link_readiness(no_block)
            assert not ready and "PA_CAP_HOSTED_WIFI" in why_not, why_not
            degraded = dict(status, hostedLink=dict(status["hostedLink"], phase="degraded"))
            ready, why_not = schema.link_readiness(degraded)
            assert not ready, "a degraded ladder is terminal for the boot; not a ready link"

        check("wifiConnected alone is not readiness on shipping", link_readiness_needs_the_ladder)

        def restart_semantics() -> None:
            assert schema.restart_detected(3600000, [3601000, 3602000]) is False, (
                "rising uptime is the device still running"
            )
            assert schema.restart_detected(3600000, [3601000, 1200]) is True, (
                "uptimeMs stepping backwards is the reboot evidence this image has"
            )
            assert schema.restart_detected(3600000, [1200, 5000]) is True, (
                "compared against the previous sample, so a reboot that then runs on is "
                "still caught"
            )
            assert bench_schema.restart_detected(4, [4, 4]) is False
            assert bench_schema.restart_detected(4, [4, 5]) is True
            assert bench_schema.restart_detected(4, [3]) is True, (
                "a power cycle resets bootCount downward; any change is a restart"
            )

        check("restart evidence: uptime regression on shipping, any bootCount change on bench",
              restart_semantics)

        def reset_driver_refuses() -> None:
            posts_before = server.post_count
            result = run_c6_reset_recovery(
                client, schema, recovery_timeout_s=1.0, poll_interval_s=0.1,
                heap_tolerance_pct=20.0, sse_resume_timeout_s=1.0,
            )
            assert result["verdict"] == "UNAVAILABLE", result
            assert any("#243" in reason for reason in result["reasons"]), result["reasons"]
            assert server.post_count == posts_before, (
                "the driver must refuse BEFORE sending anything -- a request that went out "
                "and was discarded is a stub, not a refusal"
            )
            verdict, exit_code = _compose_overall_verdict({"c6_reset_recovery": result})
            assert (verdict, exit_code) == ("INVALID / UNKNOWN", EXIT_INVALID_UNKNOWN), (
                f"an unavailable driver must never reach a passing exit code: {verdict} {exit_code}"
            )

        check("c6_reset_recovery refuses on shipping without touching the network",
              reset_driver_refuses)
    finally:
        _stop_fixture_server(server, thread)


def _run_end_to_end_driver_scenarios(failures: list[str]) -> None:
    """Run the SSE-soak and reconnect-storm drivers themselves, end to end,
    against a fixture serving each image's stream and status payload. The
    scenarios above prove the pieces; these prove the drivers are wired to
    them -- that the bench driver still reports gaps and bootCount after the
    schema split, and that the shipping driver reports the heartbeat model
    and never grows a bootCount reading it does not have."""
    for image, status_body, sse_mode in (
        ("bench", FIXTURE_STATUS_BODY, "bench_live"),
        ("shipping", FIXTURE_SHIPPING_STATUS_BODY, "shipping_live"),
    ):
        schema = SCHEMAS[image]
        server, thread = _start_fixture_server(status_body=status_body, sse_mode=sse_mode)
        try:
            port = server.server_address[1]
            client = BenchClient("127.0.0.1", port, connect_timeout_s=5.0)

            # The driver call is inside the scenario body, not before it: a
            # mutated reader raises out of the driver itself, and that has to
            # land as this scenario's red line rather than as a traceback.
            def soak_report(image: str = image, schema: StatusSchema = schema) -> None:
                soak = run_sse_soak(
                    client, schema, num_clients=1, duration_s=1.0, status_poll_interval_s=0.3,
                    heap_tolerance_pct=20.0, early_stall_check_s=1.0, max_silence_s=5.0,
                )
                assert soak["verdict"] == "PASS", soak["reasons"]
                assert soak["image"] == image, soak["image"]
                assert soak["totalFramesReceived"] > 0, soak["totalFramesReceived"]
                assert soak["statusFieldsRead"] == schema.fields_read(), soak["statusFieldsRead"]
                assert soak["baselineResetReasonAssessment"] == "notCrashShaped", soak
                if image == "bench":
                    assert soak["sseContinuityModel"] == "counter", soak["sseContinuityModel"]
                    assert soak["totalFrameGaps"] == 0, soak["totalFrameGaps"]
                    assert soak["baselineBootCount"] == 1 and soak["bootCountAdvanced"] is False, soak
                    assert soak["perClient"][0]["firstCounter"] == 0, soak["perClient"]
                else:
                    assert soak["sseContinuityModel"] == "heartbeat", soak["sseContinuityModel"]
                    assert soak["totalHeartbeatFrames"] > 0, soak["totalHeartbeatFrames"]
                    assert "totalFrameGaps" not in soak, (
                        "a counter gap count against a stream with no counter would read 0 "
                        "however badly the stream stalled -- the vacuous pass this schema "
                        "mode exists to avoid"
                    )
                    assert "baselineBootCount" not in soak, (
                        "the shipping image publishes no bootCount; a report carrying one "
                        "would be an invented reading"
                    )
                    assert soak["baselineUptimeMs"] == 3600000, soak
                    assert soak["uptimeMsWentBackwards"] is False, soak

            _record_scenario(
                f"{image}: sse_soak runs end to end and reports its own image's numbers",
                soak_report, failures)

            def storm_report(image: str = image, schema: StatusSchema = schema) -> None:
                storm = run_reconnect_storm(
                    client, schema, storm_clients=1, duration_s=0.6, cycle_min_s=0.1,
                    cycle_max_s=0.2, settle_s=0.1, heap_tolerance_pct=20.0,
                )
                assert storm["verdict"] == "PASS", storm["reasons"]
                assert storm["image"] == image, storm["image"]
                assert storm["cycleCount"] > 0 and storm["streamsOpened"] > 0, storm
                assert storm["capacityRefusals"] == 0, storm
                if image == "bench":
                    assert storm["baselineBootCount"] == 1, storm
                else:
                    assert "baselineBootCount" not in storm, storm
                    assert storm["baselineUptimeMs"] == 3600000, storm

            _record_scenario(
                f"{image}: reconnect_storm runs end to end and reports its own image's numbers",
                storm_report, failures)
        finally:
            _stop_fixture_server(server, thread)


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
        "post_json(), stream_sse_with_continuity() and the status-schema readers, "
        "against a local http.server fixture serving byte-exact PsychicEventSource "
        "(bench) and webEventStreamFormatPrefix (shipping) framing. No device "
        "required, no inline parse loop.\n"
    )
    failures: list[str] = []
    shipping = SCHEMAS["shipping"]

    print("bench image:")
    _run_sse_scenario("normal frames parse with zero gaps", "normal", _check_normal, failures)
    _run_sse_scenario("gapped fixture reports exactly 2 gaps", "gapped", _check_gapped, failures)
    _run_sse_scenario(
        "truncated fixture is reported, not silently dropped", "truncated", _check_truncated, failures,
    )
    _run_sse_scenario(
        "mid-stream reset is recorded on the result, never raised", "connreset", _check_connreset, failures,
    )
    _run_json_scenarios(failures)

    print("\nshipping image:")
    _run_continuity_scenario(
        "a healthy shipping stream reports its event census and no FAIL reason",
        "shipping_normal", shipping, 5.0, _check_shipping_normal, failures,
        stop_after_frames=5,
    )
    _run_continuity_scenario(
        "a stream the peer ends early is a FAIL reason",
        "shipping_normal", shipping, 5.0, _check_shipping_ended_early, failures,
    )
    _run_continuity_scenario(
        "a silence past the limit is a FAIL reason",
        "shipping_silence", shipping, 0.2, _check_shipping_silence, failures,
    )
    _run_continuity_scenario(
        "a frame id going backwards is a FAIL reason",
        "shipping_id_regression", shipping, 5.0, _check_shipping_id_regression, failures,
    )
    _run_continuity_scenario(
        "a bench-style nameless frame is reported, not counted as an event",
        "shipping_nameless", shipping, 5.0, _check_shipping_nameless, failures,
    )
    _run_shipping_status_scenarios(failures)

    print("\nboth images, drivers end to end:")
    _run_end_to_end_driver_scenarios(failures)

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
            "protoArtoo soak harness implementing the #184 verdict contract "
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
        "--image", choices=sorted(SCHEMAS), default="bench",
        help="which firmware image is on the board, and therefore which /api/status "
             "schema to read: 'bench' (bringup/p4_hosted_bench.cpp, env "
             "firebeetle2_hosted_bench) or 'shipping' (the firebeetle2 product image). "
             "Declared, never sniffed -- the declaration is checked against the payload "
             "at preflight and a mismatch is INVALID. The shipping image has no C6 reset "
             "route, so --driver c6_reset_recovery is refused there (#243)",
    )
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
    parser.add_argument(
        "--sse-max-silence-s", type=float, default=5.0,
        help="shipping-image continuity limit: the longest interval a client may go "
             "with no SSE frame before it is a FAIL. eventStreamTask() "
             "(src/web/web_server.cpp) ticks once a second while any client is "
             "connected, so 5s allows several missed ticks of scheduling jitter without "
             "accepting a stall. Ignored for --image bench, whose stream carries a "
             "monotonic counter and is judged arithmetically instead",
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
