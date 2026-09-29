"""tools/bench_auto.py: what each step is charged with, what the scripts declare,
and the one estop rule the runner must never break.

The memory samples below are shaped on the artoo round of 2026-09-29 (#435):
1 Hz polls that never saw the dip, while failedAllocs and heapMin, which are
cumulative within a boot, did. The flag logic is judged on those counters.
"""

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import bench_auto  # noqa: E402
import soak  # noqa: E402

ARTOO = soak.SCHEMAS["artoo"]
FLOOR = soak.resolve_admission_floor("artoo_esp32")

BASE = {"heapFree": 45004, "heapMin": 39960, "heapLargestBlock": 42996, "heapLargest8bit": 42996,
        "failedAllocs": 0, "refusedHeapFloor": 0, "refusedHeapFloorDiag": 0, "uptimeMs": 20000}


def sample(step, kind="poll", **reading):
    values = dict(BASE)
    values.update(reading)
    return {"t": "-", "step": step, "kind": kind, "status": "ok", "reading": values,
            "estop": False, "resetReason": "POWERON"}


def unanswered(step):
    return {"t": "-", "step": step, "kind": "poll", "status": "unanswered", "error": "timed out"}


def flags_by_step(flags):
    out = {}
    for flag in flags:
        out.setdefault(flag.step, []).append(flag.kind)
    return out


def serve(testcase, answers):
    """A droid that answers each path as `answers` says, counting paths and
    client sockets; stopped at the test's cleanup."""
    import http.server
    import threading

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        protocol_version = "HTTP/1.1"

        def do_GET(self):
            self.server.asked.append(self.path)
            self.server.peers.add(self.client_address)
            code, body = answers(self.path)
            data = body.encode()
            self.send_response(code)
            self.send_header("Content-Length", str(len(data)))
            if self.server.close_after:
                self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(data)
            # A droid that drops the session without a Connection: close.
            self.close_connection = self.server.close_after or self.server.drop_silently

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.asked = []
    server.peers = set()
    server.close_after = False
    server.drop_silently = False
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    testcase.addCleanup(lambda: (server.shutdown(), server.server_close()))
    return server


class WhatEachStepIsChargedWith(unittest.TestCase):
    STEPS = ["idle", "console-sweep.js", "configuration/component-picker-lineup.js",
             "wifi/wifi-standalone-ap-page.js", "shell/nav-has-no-setup-entry.js"]

    def morning(self):
        return [
            sample("idle", uptimeMs=20000),
            sample("idle", "boundary", uptimeMs=21000),
            sample("console-sweep.js", heapFree=37272, heapLargestBlock=21492, uptimeMs=22000),
            # The dip itself is never sampled; the counters moved in the last
            # second, and only the boundary poll carries it.
            sample("console-sweep.js", "boundary", heapMin=540, failedAllocs=7, uptimeMs=23000),
            sample("configuration/component-picker-lineup.js", heapMin=540, failedAllocs=7,
                   heapFree=40988, heapLargestBlock=28660, uptimeMs=24000),
            sample("configuration/component-picker-lineup.js", "boundary", heapMin=280,
                   failedAllocs=10, uptimeMs=25000),
            sample("wifi/wifi-standalone-ap-page.js", heapMin=280, failedAllocs=10,
                   heapFree=36972, heapLargestBlock=15860, uptimeMs=26000),
            sample("wifi/wifi-standalone-ap-page.js", "boundary", heapMin=280, failedAllocs=11,
                   uptimeMs=27000),
            sample("shell/nav-has-no-setup-entry.js", heapMin=280, failedAllocs=11, uptimeMs=28000),
            sample("shell/nav-has-no-setup-entry.js", "boundary", heapMin=280, failedAllocs=11,
                   uptimeMs=29000),
        ]

    def test_the_step_whose_segment_the_counter_moved_in_is_the_one_flagged(self):
        rows, flags = bench_auto.summarize_memory(self.morning(), self.STEPS, ARTOO, FLOOR)
        advanced = {row.step: row.advanced.get("failedAllocs", 0) for row in rows}
        self.assertEqual(advanced, {"idle": 0, "console-sweep.js": 7,
                                    "configuration/component-picker-lineup.js": 3,
                                    "wifi/wifi-standalone-ap-page.js": 1,
                                    "shell/nav-has-no-setup-entry.js": 0})
        self.assertEqual(flags_by_step(flags), {"console-sweep.js": ["failedAllocs"],
                                                "configuration/component-picker-lineup.js": ["failedAllocs"],
                                                "wifi/wifi-standalone-ap-page.js": ["failedAllocs"]})

    def test_the_table_carries_the_mornings_figures(self):
        rows, _ = bench_auto.summarize_memory(self.morning(), self.STEPS, ARTOO, FLOOR)
        sweep, picker, wifi = rows[1], rows[2], rows[3]
        self.assertEqual((sweep.min_heap_free, sweep.min_heap_largest_block, sweep.heap_min_end,
                          sweep.failed_allocs_end), (37272, 21492, 540, 7))
        self.assertEqual((picker.heap_min_end, picker.failed_allocs_end), (280, 10))
        self.assertEqual(wifi.min_heap_largest_block, 15860)

    def test_new_heap_min_lows_are_reported_per_step_and_never_flagged(self):
        rows, flags = bench_auto.summarize_memory(self.morning(), self.STEPS, ARTOO, FLOOR)
        self.assertEqual({row.step: row.new_heap_min_low for row in rows if row.new_heap_min_low},
                         {"console-sweep.js": 540, "configuration/component-picker-lineup.js": 280})
        self.assertNotIn("heapMin", {flag.kind for flag in flags})

    def test_a_reset_is_flagged_and_the_new_boots_count_is_not_a_negative_move(self):
        samples = [
            sample("a", failedAllocs=11, uptimeMs=900000),
            sample("b", failedAllocs=11, uptimeMs=901000),
            {**sample("b", failedAllocs=2, uptimeMs=3000), "resetReason": "TASK_WDT"},
        ]
        rows, flags = bench_auto.summarize_memory(samples, ["a", "b"], ARTOO, FLOOR)
        self.assertEqual(rows[1].advanced, {"failedAllocs": 2})
        self.assertEqual(sorted(flags_by_step(flags)["b"]), ["failedAllocs", "reset"])
        self.assertIn("TASK_WDT", next(f.message for f in flags if f.kind == "reset"))

    def test_a_droid_that_stops_answering_is_flagged_on_its_step(self):
        samples = [sample("a"), unanswered("b"), unanswered("b"), sample("b")]
        rows, flags = bench_auto.summarize_memory(samples, ["a", "b"], ARTOO, FLOOR)
        self.assertEqual((rows[1].polls, rows[1].unanswered), (3, 2))
        self.assertEqual(flags_by_step(flags), {"b": ["unanswered"]})

    def test_the_buffer_reading_is_judged_against_the_compiled_floor_only(self):
        floor = FLOOR.ordinary_bytes
        at_floor = [sample("a", heapLargest8bit=floor)]
        below = [sample("a", heapLargest8bit=floor - 1)]
        self.assertEqual(bench_auto.summarize_memory(at_floor, ["a"], ARTOO, FLOOR)[1], [])
        _, flags = bench_auto.summarize_memory(below, ["a"], ARTOO, FLOOR)
        self.assertEqual([f.kind for f in flags], ["floor"])

    def test_a_counter_missing_from_a_poll_is_an_evidence_flag_not_a_flat_line(self):
        gap = sample("b")
        del gap["reading"]["failedAllocs"]
        samples = [sample("a", failedAllocs=5), gap, sample("b", failedAllocs=8)]
        rows, flags = bench_auto.summarize_memory(samples, ["a", "b"], ARTOO, FLOOR)
        self.assertEqual(rows[1].missing, {"failedAllocs": 1})
        # The hole does not hide the move across it.
        self.assertEqual(rows[1].advanced, {"failedAllocs": 3})
        self.assertEqual(sorted(flags_by_step(flags)["b"]), ["evidence", "failedAllocs"])

    def test_a_payload_without_a_field_the_log_reads_cannot_start_a_run(self):
        whole = dict(soak.FIXTURE_ARTOO_STATUS_BODY)
        self.assertIsNone(bench_auto.payload_refusal(ARTOO, whole))
        for field in ARTOO.memory_fields():
            with self.subTest(field=field):
                body = dict(whole)
                body.pop(field)
                self.assertIn(field, bench_auto.payload_refusal(ARTOO, body))
        self.assertIn("failedAllocs", bench_auto.payload_refusal(ARTOO, dict(whole, failedAllocs="7")))

    def test_a_refusal_at_the_heap_floor_is_flagged(self):
        samples = [sample("a"), sample("b", refusedHeapFloorDiag=1)]
        _, flags = bench_auto.summarize_memory(samples, ["a", "b"], ARTOO, FLOOR)
        self.assertEqual(flags_by_step(flags), {"b": ["refusedHeapFloorDiag"]})


class WhatTheScriptsDeclare(unittest.TestCase):
    def test_every_script_in_the_tree_declares_a_target_the_runner_can_read(self):
        declarations = bench_auto.discover_declarations()
        scripts = sorted(p for p in (REPO_ROOT / "test" / "playwright").glob("*/*.js")
                         if not p.parent.name.startswith("_"))
        self.assertEqual(len(declarations), len(scripts))

    def test_a_word_the_runner_cannot_read_is_refused_not_ignored(self):
        for line in ("droid estop=latchd", "droid quickly", "fixture", "fixture seq.html x", "board"):
            with self.subTest(line=line):
                with self.assertRaises(bench_auto.DeclarationError):
                    bench_auto.parse_declaration("s.js", f"// bench-auto: {line}\n")
        with self.assertRaises(bench_auto.DeclarationError):
            bench_auto.parse_declaration("s.js", "// bench-auto: droid\n// bench-auto: droid\n")

    def test_latched_estop_scripts_run_after_the_ones_that_need_it_clear(self):
        parse = bench_auto.parse_declaration
        steps = bench_auto.droid_steps([
            parse("a/latched.js", "// bench-auto: droid estop=latched"),
            parse("b/clear.js", "// bench-auto: droid estop=clear parts=1,2"),
            parse("c/any.js", "// bench-auto: droid"),
        ])
        self.assertEqual([s.name for s in steps],
                         ["c/any.js", "b/clear.js PART=1", "b/clear.js PART=2", "a/latched.js"])
        self.assertEqual(steps[1].env, (("PART", "1"),))


class TheLastFailedAllocation(unittest.TestCase):
    """GET /api/profiler at each step's end: lastFail kept when a profiler
    build serves it (src/web/api_profiler.cpp:518-529), and a 404 recorded
    once as a property of the image, never as a failure, never re-asked."""

    def log_for(self, server, run_dir):
        connection = bench_auto.DroidConnection(f"http://127.0.0.1:{server.server_address[1]}", 5)
        self.addCleanup(connection.close)
        return bench_auto.MemoryLog(connection, ARTOO, run_dir, interval_s=60)

    def test_a_profiler_build_s_last_failed_allocation_is_kept_per_step(self):
        import json
        import tempfile
        status = json.dumps(dict(soak.FIXTURE_ARTOO_STATUS_BODY, failedAllocs=7))
        profiler = '{"heapFree":1,"failedAllocs":7,"lastFail":{"size":4096,"caps":6144,"bt":["0x400d1234","0x400d5678"]}}'
        server = serve(self, lambda path: (200, profiler if path == "/api/profiler" else status))
        with tempfile.TemporaryDirectory() as run_dir:
            log = self.log_for(server, Path(run_dir))
            log.close_step("console-sweep.js", "next")
            log.stop()
            kept = (Path(run_dir) / "profiler.jsonl").read_text()
        self.assertIsNone(log.profiler_absent_note)
        self.assertEqual(log.profiler[0]["lastFail"], {"size": 4096, "caps": 6144,
                                                       "bt": ["0x400d1234", "0x400d5678"]})
        self.assertIn('"step": "console-sweep.js"', kept)

    def test_no_profiler_is_said_once_and_the_route_is_not_asked_again(self):
        import json
        import tempfile
        status = json.dumps(soak.FIXTURE_ARTOO_STATUS_BODY)
        server = serve(self, lambda path: (404, "Not Found") if path == "/api/profiler" else (200, status))
        with tempfile.TemporaryDirectory() as run_dir:
            log = self.log_for(server, Path(run_dir))
            log.close_step("a", "b")
            log.close_step("b", "c")
            log.stop()
        self.assertIn("no profiler", log.profiler_absent_note)
        self.assertEqual(server.asked.count("/api/profiler"), 1)
        self.assertEqual([s["status"] for s in log.samples], ["ok", "ok"])


class ThePollerDoesNotChurnConnections(unittest.TestCase):
    """A socket per poll is the heap pressure ADR 0023 measured and refreshes
    the admission guard's heap sample on every accept; the log must hold one."""

    def test_every_poll_and_profiler_read_share_one_connection(self):
        import json
        import tempfile
        status = json.dumps(soak.FIXTURE_ARTOO_STATUS_BODY)
        server = serve(self, lambda path: (200, status) if path == "/api/status" else (404, "no"))
        with tempfile.TemporaryDirectory() as run_dir:
            connection = bench_auto.DroidConnection(f"http://127.0.0.1:{server.server_address[1]}", 5)
            self.addCleanup(connection.close)
            log = bench_auto.MemoryLog(connection, ARTOO, Path(run_dir), interval_s=60)
            log.start("a")
            for step, following in (("a", "b"), ("b", "c"), ("c", "c")):
                log.close_step(step, following)
            log.stop()
        self.assertEqual(len(server.asked), 5)
        self.assertEqual(len(server.peers), 1)
        self.assertEqual((log.connection.requests, log.connection.opened), (5, 1))

    def test_a_connection_the_droid_closed_is_opened_again_and_counted(self):
        import json
        status = json.dumps(soak.FIXTURE_ARTOO_STATUS_BODY)
        server = serve(self, lambda path: (200, status))
        server.close_after = True
        connection = bench_auto.DroidConnection(f"http://127.0.0.1:{server.server_address[1]}", 5)
        for _ in range(3):
            self.assertEqual(connection.request("GET", "/api/status")[0], 200)
        connection.close()
        self.assertEqual((connection.requests, connection.opened), (3, 3))


    def test_a_session_dropped_without_notice_is_retried_once_on_a_new_connection(self):
        import json
        import time
        status = json.dumps(soak.FIXTURE_ARTOO_STATUS_BODY)
        server = serve(self, lambda path: (200, status))
        server.drop_silently = True
        connection = bench_auto.DroidConnection(f"http://127.0.0.1:{server.server_address[1]}", 5)
        self.addCleanup(connection.close)
        for _ in range(3):
            self.assertEqual(connection.request("GET", "/api/status")[0], 200)
            time.sleep(0.05)
        self.assertEqual((connection.requests, connection.opened), (3, 3))


class TheBoardAsLeft(unittest.TestCase):
    def test_the_report_names_the_state_and_the_step_it_was_first_read_in(self):
        samples = [dict(sample("sweep"), estop=False), dict(sample("stop-every-surface.js"), estop=True),
                   dict(sample("estop-latched-refusals.js"), estop=True)]
        self.assertEqual(bench_auto.estop_story(samples, True),
                         "Estop at end: latched (first read latched during stop-every-surface.js)")
        self.assertIn("UNKNOWN", bench_auto.estop_story(samples, None, "timed out"))


class ARunThatDidNotBeginClearClearsNothing(unittest.TestCase):
    def test_a_latch_standing_when_the_run_began_is_left_alone(self):
        server, thread = soak._start_fixture_server(dict(soak.FIXTURE_ARTOO_STATUS_BODY, estop=True))
        try:
            connection = bench_auto.DroidConnection(f"http://127.0.0.1:{server.server_address[1]}", 5)
            said = bench_auto.ensure_estop(connection, "clear", "shell/stop-every-surface.js", run_began_clear=False)
            connection.close()
            self.assertEqual(server.post_count, 0)
            self.assertIn("did not begin clear", said)
        finally:
            soak._stop_fixture_server(server, thread)


if __name__ == "__main__":
    unittest.main()
