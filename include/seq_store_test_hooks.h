// =============================================================================
// include/seq_store_test_hooks.h
//
// Native-test-only observation hooks for seqStoreDelete()'s stub
// (src/native_test_stubs.cpp). seq_store.cpp's real LittleFS I/O is not in
// [env:native]'s build_src_filter (platformio.ini is fenced), so no native
// test can drive a real file removal; the stub records the call and answers
// a test-set verdict instead (native_test_stubs.cpp's own header comment).
//
// Declared once here rather than as an inline `extern` in each consumer
// (test_console_module.cpp's dome.action.delete-sequence executor, #259) -
// matches the precedent include/aux_led_test_hooks.h and
// include/audio_test_hooks.h already set for this shape of native stub
// observation hook.
// =============================================================================
#pragma once

// seqStoreDelete()'s own stub: a test sets the verdict, calls the executor,
// and asserts on both the outcome and whether the real lookup-then-delete
// core actually ran.
extern bool g_test_seq_delete_ok;
extern unsigned g_test_seq_delete_calls;
