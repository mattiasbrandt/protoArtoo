import importlib.util
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[2] / "tools" / "patch_async_sse.py"
SPEC = importlib.util.spec_from_file_location("patch_async_sse", MODULE_PATH)
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class PatchAsyncWebServerTest(unittest.TestCase):
    def test_lib_source_dir_prefers_versioned_library_dir(self):
        with tempfile.TemporaryDirectory() as tmp:
            libdeps = Path(tmp)
            unversioned = libdeps / "AsyncTCP" / "src"
            versioned = libdeps / "AsyncTCP@3.4.10" / "src"
            unversioned.mkdir(parents=True)
            versioned.mkdir(parents=True)
            (unversioned / "AsyncTCP.cpp").write_text("// unversioned\n", encoding="utf-8")
            (versioned / "AsyncTCP.cpp").write_text("// versioned\n", encoding="utf-8")

            self.assertEqual(
                PATCH.lib_source_dir(libdeps, "AsyncTCP", "AsyncTCP.cpp"),
                versioned,
            )

    def test_lib_source_dir_uses_unversioned_when_no_versioned_dir_exists(self):
        with tempfile.TemporaryDirectory() as tmp:
            libdeps = Path(tmp)
            unversioned = libdeps / "ESPAsyncWebServer" / "src"
            unversioned.mkdir(parents=True)
            (unversioned / "AsyncEventSource.h").write_text("// unversioned\n", encoding="utf-8")

            self.assertEqual(
                PATCH.lib_source_dir(libdeps, "ESPAsyncWebServer", "AsyncEventSource.h"),
                unversioned,
            )

    def test_lib_source_dir_fails_when_required_source_is_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            libdeps = Path(tmp)
            (libdeps / "AsyncTCP@3.4.10" / "src").mkdir(parents=True)

            with self.assertRaisesRegex(RuntimeError, "AsyncTCP/src/AsyncTCP.cpp"):
                PATCH.lib_source_dir(libdeps, "AsyncTCP", "AsyncTCP.cpp")

    def test_lib_source_dir_fails_on_multiple_versioned_candidates(self):
        with tempfile.TemporaryDirectory() as tmp:
            libdeps = Path(tmp)
            first = libdeps / "AsyncTCP@3.4.10" / "src"
            second = libdeps / "AsyncTCP@3.4.11" / "src"
            first.mkdir(parents=True)
            second.mkdir(parents=True)
            (first / "AsyncTCP.cpp").write_text("// first\n", encoding="utf-8")
            (second / "AsyncTCP.cpp").write_text("// second\n", encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "ambiguous library dependency dirs"):
                PATCH.lib_source_dir(libdeps, "AsyncTCP", "AsyncTCP.cpp")


    def test_sse_patch_collapses_nested_guards_and_is_idempotent(self):
        source = (
            "header\n"
            + PATCH.SSE_PREAMBLE_START
            + "#ifndef SSE_MAX_INFLIGH\n"
            + "#ifndef SSE_MAX_INFLIGH\n"
            + "#define SSE_MAX_INFLIGH 16 * 1024"
            + "  // but no more than 16k, no need to blow it, since same data is kept in local Q\n"
            + "#endif\n"
            + "#endif\n"
            + "#ifndef SSE_MAX_INFLIGH\n"
            + "#define SSE_MAX_INFLIGH 8 * 1024"
            + "  // but no more than 8k, no need to blow it, since same data is kept in local Q\n"
            + "#endif\n"
            + "#define SSE_MAX_INFLIGH 16 * 1024"
            + "  // but no more than 16k, no need to blow it, since same data is kept in local Q\n"
            + PATCH.SSE_PREAMBLE_END
            + "body\n"
        )

        patched = PATCH.patch_sse_header(source)

        self.assertEqual(patched.count("#ifndef SSE_MAX_INFLIGH"), 3)
        self.assertEqual(PATCH.patch_sse_header(patched), patched)

    def test_static_handler_patch_removes_exists_and_is_idempotent(self):
        source = "prefix\n" + PATCH.STATIC_SEARCH_BEFORE + "suffix\n"

        patched = PATCH.patch_static_handler(source)

        self.assertNotIn("_fs.exists", patched)
        self.assertEqual(PATCH.patch_static_handler(patched), patched)

    def test_static_handler_alloc_patch_uses_nothrow_and_is_idempotent(self):
        source = (
            "prefix\n"
            + PATCH.STATIC_ALLOC_INCLUDES_BEFORE
            + "middle\n"
            + PATCH.STATIC_ALLOC_SEARCH_BEFORE
            + "suffix\n"
        )

        patched = PATCH.patch_static_handler_alloc(source)

        self.assertIn("#include <new>", patched)
        self.assertIn("new (std::nothrow) AsyncBasicResponse(304)", patched)
        self.assertIn("new (std::nothrow) AsyncFileResponse", patched)
        self.assertEqual(PATCH.patch_static_handler_alloc(patched), patched)

    def test_static_handler_open_guard_runs_before_file_open_and_is_idempotent(self):
        source = (
            "prefix\n"
            + PATCH.STATIC_OPEN_GUARD_INCLUDES_BEFORE
            + "middle\n"
            + PATCH.STATIC_OPEN_GUARD_BEFORE
            + "suffix\n"
        )

        patched = PATCH.patch_static_handler_open_guard(source)

        self.assertIn("#include <esp_heap_caps.h>", patched)
        self.assertIn("ASYNC_STATIC_MIN_LARGEST_FREE_BLOCK", patched)
        self.assertIn("heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)", patched)
        self.assertIn("request->abort();", patched)
        self.assertLess(
            patched.index("heap_caps_get_largest_free_block"),
            patched.index("String gzip = path + T__gz;"),
        )
        self.assertEqual(PATCH.patch_static_handler_open_guard(patched), patched)

    def test_abstract_response_zero_read_state_is_per_response_and_idempotent(self):
        source = (
            "prefix\n"
            + PATCH.ABSTRACT_RESPONSE_ZERO_READ_STATE_BEFORE
            + "suffix\n"
        )

        patched = PATCH.patch_abstract_response_zero_read_state(source)

        self.assertIn(
            "static constexpr uint8_t MAX_PREMATURE_ZERO_READ_RETRIES = 2;",
            patched,
        )
        self.assertIn("uint8_t _prematureZeroReadRetries{0};", patched)
        self.assertEqual(
            PATCH.patch_abstract_response_zero_read_state(patched),
            patched,
        )

    def test_abstract_response_zero_read_patch_distinguishes_eof_retry_and_exhaustion(self):
        source = (
            "prefix\n"
            + PATCH.ABSTRACT_RESPONSE_ZERO_READ_BEFORE
            + "suffix\n"
        )

        patched = PATCH.patch_abstract_response_zero_read(source)

        self.assertIn(
            "if (!_sendContentLength || (_sentLength == _contentLength))",
            patched,
        )
        self.assertIn(
            "_prematureZeroReadRetries < MAX_PREMATURE_ZERO_READ_RETRIES",
            patched,
        )
        self.assertIn("++_prematureZeroReadRetries;", patched)
        self.assertEqual(patched.count("_prematureZeroReadRetries = 0;"), 1)
        self.assertIn(
            "} else if (readLen != RESPONSE_TRY_AGAIN) {\n"
            "          _prematureZeroReadRetries = 0;",
            patched,
        )
        self.assertIn("_state = RESPONSE_FAILED;", patched)
        self.assertIn("request->client()->close();", patched)
        self.assertLess(
            patched.index("++_prematureZeroReadRetries;"),
            patched.index("_state = RESPONSE_FAILED;"),
        )
        self.assertEqual(PATCH.patch_abstract_response_zero_read(patched), patched)

    def test_async_event_dispatch_patch_catches_exceptions_and_is_idempotent(self):
        source = (
            "prefix\n"
            + PATCH.ASYNC_EVENT_INCLUDES_BEFORE
            + "middle\n"
            + PATCH.ASYNC_EVENT_DISPATCH_BEFORE
            + "suffix\n"
        )

        patched = PATCH.patch_async_event_dispatch(source)

        self.assertIn("#include <exception>", patched)
        self.assertIn("try {", patched)
        self.assertIn("catch (const std::exception &ex)", patched)
        self.assertEqual(PATCH.patch_async_event_dispatch(patched), patched)

    def test_tcp_accept_heap_guard_runs_before_client_alloc_and_is_idempotent(self):
        source = (
            "prefix\n"
            + PATCH.TCP_ACCEPT_HEAP_GUARD_INCLUDES_BEFORE
            + "middle\n"
            + PATCH.TCP_ACCEPT_HEAP_GUARD_BEFORE
            + "suffix\n"
        )

        patched = PATCH.patch_tcp_accept_heap_guard(source)

        self.assertIn("#if defined(ESP32)", patched)
        self.assertIn("#include <esp_heap_caps.h>", patched)
        self.assertIn("ASYNC_TCP_ACCEPT_MIN_LARGEST_FREE_BLOCK", patched)
        self.assertIn("heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)", patched)
        self.assertIn("tcp_abort(pcb);", patched)
        self.assertIn("return ERR_ABRT;", patched)
        self.assertLess(
            patched.index("heap_caps_get_largest_free_block"),
            patched.index("new (std::nothrow) AsyncClient(pcb)"),
        )
        self.assertEqual(PATCH.patch_tcp_accept_heap_guard(patched), patched)

    def test_webrequest_response_alloc_patch_uses_nothrow_and_is_idempotent(self):
        source = (
            "prefix\n"
            + PATCH.WEBREQUEST_ALLOC_INCLUDES_BEFORE
            + "middle\n"
            + PATCH.WEBREQUEST_FACTORY_ALLOC_BEFORE
            + "\n"
            + PATCH.WEBREQUEST_SEND_NULL_BEFORE
            + "  delete _response;\n"
            + "}\n"
            + "suffix\n"
        )

        patched = PATCH.patch_webrequest_response_alloc(source)

        self.assertIn("#include <new>", patched)
        self.assertIn("new (std::nothrow) AsyncBasicResponse", patched)
        self.assertIn("new (std::nothrow) AsyncResponseStream", patched)
        self.assertIn("if (response == nullptr)", patched)
        self.assertIn("abort();", patched)
        self.assertEqual(PATCH.patch_webrequest_response_alloc(patched), patched)

    def test_eventsource_response_alloc_patch_uses_nothrow_and_is_idempotent(self):
        source = (
            "prefix\n"
            + PATCH.EVENTSOURCE_ALLOC_INCLUDES_BEFORE
            + "middle\n"
            + PATCH.EVENTSOURCE_RESPONSE_ALLOC_BEFORE
            + "suffix\n"
        )

        patched = PATCH.patch_eventsource_response_alloc(source)

        self.assertIn("#include <new>", patched)
        self.assertIn("new (std::nothrow) AsyncEventSourceResponse", patched)
        self.assertEqual(PATCH.patch_eventsource_response_alloc(patched), patched)


if __name__ == "__main__":
    unittest.main()
