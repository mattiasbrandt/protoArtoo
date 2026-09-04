"""Route tests for tools/serve_editor_fixture.py (#261).

The fixture server is the harness this repo ships for data/ verification, so
what it answers an API path with is not cosmetic: before #261 it had no
/api/logs route, SimpleHTTPRequestHandler fell through to the SPA fallback, and
the dashboard's Live Logs panel filled with 156 lines of index.html served at
"200 text/html". These tests pin the two properties that keep that from coming
back - /api/logs answers as log text, and an API path with no fixture answers
404 instead of a page.

The real handler is served on an ephemeral port; nothing is reimplemented here.
"""

import importlib.util
import json
import threading
import unittest
import urllib.error
import urllib.parse
import urllib.request
from http.server import HTTPServer
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SERVER_PATH = REPO_ROOT / "tools" / "serve_editor_fixture.py"

_spec = importlib.util.spec_from_file_location("serve_editor_fixture", SERVER_PATH)
fixture_server = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fixture_server)


class FixtureRouteTest(unittest.TestCase):
    """Drives the shipped FixtureHandler over a real socket."""

    @classmethod
    def setUpClass(cls):
        # Port 0: the OS picks a free port, so this never collides with a
        # fixture server a developer already has running on 4173.
        cls.server = HTTPServer(("127.0.0.1", 0), fixture_server.FixtureHandler)
        cls.base = f"http://127.0.0.1:{cls.server.server_address[1]}"
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)

    def get(self, path):
        with urllib.request.urlopen(f"{self.base}{path}", timeout=5) as response:
            return response.status, response.headers.get("Content-type"), response.read()

    def post_console(self, command):
        request = urllib.request.Request(
            f"{self.base}/api/console",
            data=urllib.parse.urlencode({"command": command}).encode("utf-8"),
            headers={"Content-Type": "application/x-www-form-urlencoded"},
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=5) as response:
            return response.status, json.loads(response.read())

    def test_logs_route_answers_as_log_text(self):
        status, content_type, body = self.get("/api/logs")

        self.assertEqual(status, 200)
        # The dashboard refuses a log body whose media type is not text/plain,
        # so this header is the whole point of the route.
        self.assertTrue(
            content_type.split(";")[0].strip() == "text/plain",
            f"/api/logs must answer as text/plain, got {content_type!r}",
        )
        self.assertNotIn(b"<html", body.lower(), "a page must never be served as log text")

    def test_logs_body_is_newline_separated_device_style_lines(self):
        _, _, body = self.get("/api/logs")
        lines = body.decode("utf-8").split("\n")

        self.assertGreater(len(lines), 1, "the ring is served newline separated")
        for line in lines:
            # include/logging.h emits "[<millis>][<level>][<tag>] message"; a
            # body that does not look like that is not exercising the panel.
            self.assertRegex(line, r"^\[\d+\]\[[EWID]\]\[[a-z]+\] .+")

    def test_logs_route_survives_a_query_string(self):
        status, content_type, _ = self.get("/api/logs?ts=1")

        self.assertEqual(status, 200)
        self.assertIn("text/plain", content_type)

    def test_unknown_api_path_is_404_not_the_page(self):
        with self.assertRaises(urllib.error.HTTPError) as caught:
            self.get("/api/does-not-exist")

        self.assertEqual(
            caught.exception.code,
            404,
            "an API path with no fixture must announce itself, not fall through "
            "to index.html the way it did before #261",
        )

    def test_known_json_fixture_route_still_answers(self):
        status, content_type, _ = self.get("/api/dome/layout")

        self.assertEqual(status, 200)
        self.assertIn("application/json", content_type)

    def test_console_route_answers_records_for_a_known_command(self):
        status, payload = self.post_console("system.status.health")

        self.assertEqual(status, 200)
        # The dashboard reads the records off the envelope's "records" key and
        # reports "invalid response format" for anything else.
        self.assertIsInstance(payload.get("records"), list)
        types = [record["type"] for record in payload["records"]]
        self.assertEqual(types[0], "begin", "a record group opens with begin")
        self.assertEqual(types[-1], "end", "a record group is closed by end")

    def test_console_route_omits_truncated_on_an_answer_that_fit(self):
        _, payload = self.post_console("system.status.health")

        # Present exactly when the answer was cut - the dashboard reads its
        # absence as "this reply is whole" (#240).
        self.assertNotIn("truncated", payload)

    def test_console_logs_answer_is_cut_and_says_so(self):
        _, payload = self.post_console("system.status.logs")

        items = [r for r in payload["records"] if r["type"] == "item"]
        self.assertEqual(
            len(items),
            fixture_server.CONSOLE_LOG_ITEM_CAP,
            "the fixture must cut this answer where the adapter does, so the "
            "dashboard's truncated state is reachable offline",
        )
        self.assertTrue(
            payload.get("truncated"),
            "an answer this route cut must carry the envelope flag the "
            "dashboard reads",
        )
        # Cut like the adapter cuts: oldest lines discarded, newest kept.
        self.assertEqual(items[-1]["value"], fixture_server.FIXTURE_LOG_LINES[-1])
        self.assertEqual(payload["records"][-1]["type"], "end", "the group still closes")

    def test_console_route_refuses_a_body_with_no_command(self):
        request = urllib.request.Request(
            f"{self.base}/api/console", data=b"", method="POST"
        )
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(request, timeout=5)

        self.assertEqual(caught.exception.code, 400)

    def test_unknown_post_path_is_404_not_501(self):
        request = urllib.request.Request(
            f"{self.base}/api/nope", data=b"", method="POST"
        )
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(request, timeout=5)

        # 501 reaches the page as "Not supported by device", which reads like a
        # firmware answer rather than a missing fixture route.
        self.assertEqual(caught.exception.code, 404)

    def test_page_routes_still_fall_back_to_the_shell(self):
        status, content_type, body = self.get("/")

        self.assertEqual(status, 200)
        self.assertIn("text/html", content_type)
        self.assertIn(b"log-console", body, "the dashboard shell must still be served")


if __name__ == "__main__":
    unittest.main()
