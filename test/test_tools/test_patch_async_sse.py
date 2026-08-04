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

    def test_abstract_response_tcp_zero_add_state_is_per_response(self):
        source = (
            "prefix\n"
            + PATCH.ABSTRACT_RESPONSE_ZERO_READ_STATE_BEFORE
            + "suffix\n"
        )

        patched = PATCH.patch_abstract_response_zero_read_state(source)

        self.assertIn(
            "static constexpr uint8_t MAX_TCP_ADD_ZERO_RETRIES = 10;",
            patched,
        )
        self.assertIn("uint8_t _tcpAddZeroRetries{0};", patched)
        self.assertEqual(PATCH.patch_abstract_response_zero_read_state(patched), patched)

    def test_abstract_response_tcp_zero_add_state_upgrades_short_retry_window(self):
        patched = PATCH.patch_abstract_response_zero_read_state(
            PATCH.ABSTRACT_RESPONSE_ZERO_READ_STATE_TCP_RETRY_PREVIOUS_AFTER
        )

        self.assertIn(
            "static constexpr uint8_t MAX_TCP_ADD_ZERO_RETRIES = 10;",
            patched,
        )
        self.assertNotIn(
            "static constexpr uint8_t MAX_TCP_ADD_ZERO_RETRIES = 2;",
            patched,
        )

    def test_abstract_response_zero_read_state_patch_rejects_vendor_drift(self):
        with self.assertRaisesRegex(
            RuntimeError,
            "WebResponseImpl.h response buffer state changed",
        ):
            PATCH.patch_abstract_response_zero_read_state("changed vendor state")

    def test_abstract_response_zero_read_patch_distinguishes_eof_retry_and_exhaustion(self):
        source = (
            "prefix\n"
            + PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_BEFORE
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_ZERO_READ_BEFORE
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_INFLIGHT_CREDIT_BEFORE
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_BUFFER_RELEASE_BEFORE
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
        self.assertIn(
            "if (_send_buffer_len == 0 && _prematureZeroReadRetries == 0)",
            patched,
        )
        retry_index = patched.index("++_prematureZeroReadRetries;")
        self.assertLess(
            retry_index,
            patched.index("_state = RESPONSE_FAILED;", retry_index),
        )
        self.assertEqual(PATCH.patch_abstract_response_zero_read(patched), patched)

    def test_abstract_response_keeps_final_buffer_retryable_until_tcp_accepts_it(self):
        """Issue #60: unittest locks completion after TCP acceptance at the patcher seam."""
        source_complete_source = """\
          if (_sendContentLength && (_sentLength == _contentLength)) {
            // it was last piece of content
            _state = RESPONSE_END;
          }
"""
        source = (
            "prefix\n"
            + PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_BEFORE
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_ZERO_READ_BEFORE
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_INFLIGHT_CREDIT_BEFORE
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_BUFFER_RELEASE_BEFORE
            + "suffix\n"
        )

        patched = PATCH.patch_abstract_response_zero_read(source)

        self.assertIn(
            "_send_buffer_len = _send_buffer_offset = 0;  // consider buffer empty\n"
            "          if (_sendContentLength && (_sentLength == _contentLength)) {\n"
            "            // The final buffered bytes have been accepted by TCP.\n"
            "            _state = RESPONSE_END;\n"
            "          }",
            patched,
        )
        self.assertNotIn(source_complete_source, patched)
        self.assertEqual(PATCH.patch_abstract_response_zero_read(patched), patched)

    def test_abstract_response_patch_upgrades_previous_issue60_output(self):
        """Issue #60: unittest verifies the pre-build caller migrates its prior output."""
        current_progress_tail = """\
          _sentLength += readLen;       // data is not sent yet, but we need it to understand that it would be last block
        }
"""
        previous_progress_tail = """\
          _sentLength += readLen;       // data is not sent yet, but we need it to understand that it would be last block
          if (_sendContentLength && (_sentLength == _contentLength)) {
            // it was last piece of content
            _state = RESPONSE_END;
          }
        }
"""
        previous_zero_read_patch = PATCH.ABSTRACT_RESPONSE_ZERO_READ_AFTER.replace(
            current_progress_tail,
            previous_progress_tail,
        )
        source = (
            "prefix\n"
            + PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_BEFORE
            + "middle\n"
            + previous_zero_read_patch
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_INFLIGHT_CREDIT_BEFORE
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_BUFFER_RELEASE_AFTER
            + "suffix\n"
        )

        patched = PATCH.patch_abstract_response_zero_read(source)

        self.assertIn(PATCH.ABSTRACT_RESPONSE_ZERO_READ_AFTER, patched)
        self.assertNotIn(previous_zero_read_patch, patched)
        self.assertIn(PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_AFTER, patched)
        self.assertEqual(PATCH.patch_abstract_response_zero_read(patched), patched)

    def test_abstract_response_patch_upgrades_legacy_fill_failure_names(self):
        """Issue #60: unittest verifies exact legacy migration keeps drift checks."""
        legacy_state = """\
  // buffer data size specifiers
  size_t _send_buffer_offset{0}, _send_buffer_len{0};
  static constexpr uint8_t MAX_CONSECUTIVE_FILL_BUFFER_FAILURES = 2;
  uint8_t _consecutiveFillBufferFailures{0};
  size_t _readDataFromCacheOrContent(uint8_t *data, const size_t len);
"""
        legacy_zero_read = PATCH.ABSTRACT_RESPONSE_ZERO_READ_PREVIOUS_AFTER.replace(
            "MAX_PREMATURE_ZERO_READ_RETRIES",
            "MAX_CONSECUTIVE_FILL_BUFFER_FAILURES",
        ).replace(
            "_prematureZeroReadRetries",
            "_consecutiveFillBufferFailures",
        )
        legacy_tcp_add = PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_BEFORE.replace(
            PATCH.ABSTRACT_RESPONSE_PENDING_FINAL_BUFFER_AFTER,
            PATCH.ABSTRACT_RESPONSE_PENDING_FINAL_BUFFER_BEFORE,
        )

        patched_state = PATCH.patch_abstract_response_zero_read_state(legacy_state)
        patched_response = PATCH.patch_abstract_response_zero_read(
            legacy_tcp_add
            + legacy_zero_read
            + PATCH.ABSTRACT_RESPONSE_INFLIGHT_CREDIT_BEFORE
            + PATCH.ABSTRACT_RESPONSE_BUFFER_RELEASE_BEFORE
        )

        self.assertEqual(patched_state, PATCH.ABSTRACT_RESPONSE_ZERO_READ_STATE_AFTER)
        self.assertIn(PATCH.ABSTRACT_RESPONSE_ZERO_READ_AFTER, patched_response)
        self.assertIn(PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_AFTER, patched_response)
        self.assertEqual(
            PATCH.patch_abstract_response_zero_read(patched_response),
            patched_response,
        )

    def test_abstract_response_zero_read_recognizes_mss_cap_pipeline_output(self):
        """Issue #64: a tree already patched through patch_abstract_response_tcp_mss_cap()
        must not make patch_abstract_response_zero_read() raise on re-run.

        patch_abstract_response_tcp_mss_cap() runs later in the pipeline and
        further transforms the exact region patch_abstract_response_zero_read()'s
        PENDING_FINAL_BUFFER stage also touches (its own BEFORE state IS
        ABSTRACT_RESPONSE_TCP_DIAGNOSTICS_AFTER). A cached PlatformIO libdeps
        tree that already has the full pipeline applied -- as happens whenever
        one PlatformIO environment's cache is older than another's while the
        patcher itself is under active iteration -- contains none of the
        forms patch_abstract_response_zero_read() previously recognized as
        "already handled," so it fell through to the drift-detection raise
        despite the tree already being correctly and fully patched.
        """
        source = (
            "prefix\n"
            + PATCH.ABSTRACT_RESPONSE_ZERO_READ_AFTER
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_TCP_MSS_CAP_AFTER
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_INFLIGHT_CREDIT_AFTER
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_BUFFER_RELEASE_AFTER
            + "suffix\n"
        )

        patched = PATCH.patch_abstract_response_zero_read(source)

        self.assertEqual(patched, source)

    def test_abstract_response_bounds_tcp_zero_add_and_counts_partial_progress(self):
        """Issue #60: unittest locks bounded stalls and partial progress accounting."""
        source = (
            "prefix\n"
            + PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_BEFORE
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_ZERO_READ_AFTER
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_INFLIGHT_CREDIT_BEFORE
            + PATCH.ABSTRACT_RESPONSE_BUFFER_RELEASE_AFTER
            + "suffix\n"
        )

        patched = PATCH.patch_abstract_response_zero_read(source)

        self.assertIn(
            "if (added_len == 0 && !_chunked && _sendContentLength) {\n"
            "            if (_tcpAddZeroRetries < MAX_TCP_ADD_ZERO_RETRIES) {\n"
            "              ++_tcpAddZeroRetries;",
            patched,
        )
        self.assertIn(
            "_state = RESPONSE_FAILED;\n"
            "              request->client()->close();",
            patched,
        )
        self.assertIn(
            "} else if (added_len > 0) {\n"
            "            if (!_chunked && _sendContentLength) {\n"
            "              _tcpAddZeroRetries = 0;\n"
            "            }\n"
            "            payloadlen += added_len;\n"
            "          }\n"
            "          _send_buffer_offset += added_len;",
            patched,
        )
        self.assertEqual(patched.count("_tcpAddZeroRetries = 0;"), 2)
        self.assertIn(
            "if (payloadlen || _chunked || !_sendContentLength) {\n"
            "      _in_flight += payloadlen;\n"
            "      --_in_flight_credit;  // take a credit\n"
            "    }",
            patched,
        )
        self.assertEqual(PATCH.patch_abstract_response_zero_read(patched), patched)

    def test_abstract_response_preserves_other_tcp_add_and_credit_behavior(self):
        """Issue #60: unittest locks vendor behavior for out-of-scope responses."""
        source = (
            PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_BEFORE
            + PATCH.ABSTRACT_RESPONSE_ZERO_READ_AFTER
            + PATCH.ABSTRACT_RESPONSE_INFLIGHT_CREDIT_BEFORE
            + PATCH.ABSTRACT_RESPONSE_BUFFER_RELEASE_AFTER
        )

        patched = PATCH.patch_abstract_response_zero_read(source)

        self.assertIn(
            "if (added_len == 0 && !_chunked && _sendContentLength)",
            patched,
        )
        self.assertIn(
            "if (payloadlen || _chunked || !_sendContentLength) {\n"
            "      _in_flight += payloadlen;",
            patched,
        )

    def test_abstract_response_patch_upgrades_unscoped_tcp_stall_policy(self):
        """Issue #60: unittest verifies migration to declared-length TCP scope."""
        source = (
            PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_PREVIOUS_AFTER
            + PATCH.ABSTRACT_RESPONSE_ZERO_READ_AFTER
            + PATCH.ABSTRACT_RESPONSE_INFLIGHT_CREDIT_PREVIOUS_AFTER
            + PATCH.ABSTRACT_RESPONSE_BUFFER_RELEASE_AFTER
        )

        patched = PATCH.patch_abstract_response_zero_read(source)

        self.assertIn(PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_AFTER, patched)
        self.assertIn(PATCH.ABSTRACT_RESPONSE_INFLIGHT_CREDIT_AFTER, patched)
        self.assertNotIn(
            PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_PREVIOUS_AFTER,
            patched,
        )
        self.assertEqual(PATCH.patch_abstract_response_zero_read(patched), patched)

    def test_abstract_response_tcp_diagnostics_cover_attempt_recovery_and_exhaustion(self):
        """Issue #60: unittest locks allocation-free TCP outcome instrumentation."""
        source = (
            "prefix\n"
            + PATCH.ABSTRACT_RESPONSE_TCP_DIAGNOSTICS_DECLARATIONS_BEFORE
            + "middle\n"
            + PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_AFTER
            + "suffix\n"
        )

        patched = PATCH.patch_abstract_response_tcp_diagnostics(source)

        self.assertIn(
            "void webResponseTcpRecordZeroProgress(bool hadSendSpace);",
            patched,
        )
        self.assertIn(
            "bool const tcp_had_send_space =\n"
            "          !_chunked && _sendContentLength && request->client()->space() > 0;",
            patched,
        )
        self.assertIn(
            "if (added_len == 0 && !_chunked && _sendContentLength) {\n"
            "            webResponseTcpRecordZeroProgress(tcp_had_send_space);",
            patched,
        )
        self.assertIn(
            "webResponseTcpRecordExhaustion();\n"
            "              _state = RESPONSE_FAILED;",
            patched,
        )
        self.assertEqual(patched.count("if (_tcpAddZeroRetries > 0) {"), 2)
        self.assertEqual(patched.count("webResponseTcpRecordRecovery();"), 3)
        self.assertEqual(
            PATCH.patch_abstract_response_tcp_diagnostics(patched),
            patched,
        )

    def test_abstract_response_tcp_diagnostics_upgrade_previous_output(self):
        source = (
            PATCH.ABSTRACT_RESPONSE_TCP_DIAGNOSTICS_DECLARATIONS_PREVIOUS_AFTER
            + PATCH.ABSTRACT_RESPONSE_TCP_DIAGNOSTICS_PREVIOUS_AFTER
        )

        patched = PATCH.patch_abstract_response_tcp_diagnostics(source)

        self.assertIn(PATCH.ABSTRACT_RESPONSE_TCP_DIAGNOSTICS_DECLARATIONS_AFTER, patched)
        self.assertIn(PATCH.ABSTRACT_RESPONSE_TCP_DIAGNOSTICS_AFTER, patched)
        self.assertNotIn("void webResponseTcpRecordZeroProgress();", patched)
        self.assertEqual(PATCH.patch_abstract_response_tcp_diagnostics(patched), patched)

    def test_abstract_response_tcp_diagnostics_recognizes_mss_cap_pipeline_output(self):
        """A repeated pre-build pass must accept the later MSS-cap output."""
        source = (
            PATCH.ABSTRACT_RESPONSE_TCP_DIAGNOSTICS_DECLARATIONS_AFTER
            + PATCH.ABSTRACT_RESPONSE_TCP_MSS_CAP_AFTER
        )

        self.assertEqual(
            PATCH.patch_abstract_response_tcp_diagnostics(source),
            source,
        )

    def test_abstract_response_zero_read_patch_rejects_vendor_drift(self):
        source = (
            PATCH.ABSTRACT_RESPONSE_ZERO_READ_BEFORE
            + PATCH.ABSTRACT_RESPONSE_TCP_ADD_PROGRESS_BEFORE
            + PATCH.ABSTRACT_RESPONSE_INFLIGHT_CREDIT_BEFORE
            + "changed vendor buffer release"
        )

        with self.assertRaisesRegex(
            RuntimeError,
            "WebResponses.cpp response buffer release changed",
        ):
            PATCH.patch_abstract_response_zero_read(source)

    def test_abstract_response_tcp_mss_cap_bounds_single_add_and_is_idempotent(self):
        """Issue #60 Slice 12: bound each declared-length TCP enqueue to one MSS."""
        source = "prefix\n" + PATCH.ABSTRACT_RESPONSE_TCP_DIAGNOSTICS_AFTER + "suffix\n"

        patched = PATCH.patch_abstract_response_tcp_mss_cap(source)

        self.assertIn(
            "size_t const add_request_len =\n"
            "          (!_chunked && _sendContentLength)\n"
            "            ? std::min<size_t>(remaining_len, (size_t)CONFIG_LWIP_TCP_MSS)\n"
            "            : remaining_len;",
            patched,
        )
        self.assertIn(
            "request->client()->add(reinterpret_cast<char *>(_send_buffer->data() + _send_buffer_offset), add_request_len);",
            patched,
        )
        self.assertIn(
            "if (added_len != add_request_len || add_request_len != remaining_len) {",
            patched,
        )
        # A capped chunk that fully drains the buffer must still finalize
        # RESPONSE_END, so that check now appears in both branches.
        self.assertEqual(patched.count("_state = RESPONSE_END;"), 2)
        self.assertEqual(
            PATCH.patch_abstract_response_tcp_mss_cap(patched),
            patched,
        )

    def test_abstract_response_tcp_mss_cap_rejects_vendor_drift(self):
        with self.assertRaisesRegex(
            RuntimeError,
            "TCP diagnostics seam changed for MSS cap",
        ):
            PATCH.patch_abstract_response_tcp_mss_cap("changed vendor tcp add path")

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

    def test_asyncclient_add_diagnostics_capture_error_and_queue_state(self):
        source = (
            PATCH.ASYNC_CLIENT_ADD_DIAGNOSTICS_DECLARATION_BEFORE
            + PATCH.ASYNC_CLIENT_ADD_DIAGNOSTICS_BEFORE
        )

        patched = PATCH.patch_asyncclient_add_diagnostics(source)

        self.assertIn("void webResponseTcpRecordAsyncClientAddFailure(", patched)
        self.assertIn("uint16_t const queue_length = tcp_sndqueuelen(_pcb);", patched)
        self.assertIn("err, err == ERR_MEM, queue_length, TCP_SND_QUEUELEN", patched)
        self.assertEqual(PATCH.patch_asyncclient_add_diagnostics(patched), patched)

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
