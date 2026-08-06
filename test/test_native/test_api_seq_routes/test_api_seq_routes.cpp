// =============================================================================
// test/test_native/test_api_seq_routes/test_api_seq_routes.cpp
//
// Native unit tests for the Learned Sequence handlers through the WebRequest
// seam's host-test backend (ADR 0021). Each handler is driven exactly as a
// device backend would drive it, and the assertions are on the captured
// response -- no vendor web-server type appears anywhere here, which is the
// property the port exists to establish.
//
// The weight is on the request-body path, because that is the part with a
// history: POST /api/seq has broken on body handling before, and the port
// moved it from a vendor body callback to contentLength()/body() on the seam.
// The three body outcomes (absent, over cap, declared-but-lost) carry distinct
// status codes on the async stack, so they are asserted individually rather
// than as "some 4xx".
//
// What is deliberately not covered: anything needing real LittleFS. The store's
// filesystem half is stubbed (src/native_test_stubs.cpp); its index, JSON codec
// and Protocol Check are the real code.
// =============================================================================
#include <unity.h>

#include <cstring>
#include <string>

#include "api_seq.h"
#include "protocol_check.h"
#include "seq_store_index.h"
#include "seq_store_util.h"  // SEQ_FILE_MAX_BYTES
#include "sequence_dispatcher.h"
#include "web_request_test_backend.h"

// Recorded side effects from src/native_test_stubs.cpp.
extern ProtocolCheckResult g_test_seq_save_result;
extern std::string g_test_seq_saved_body;
extern unsigned g_test_seq_save_calls;
extern bool g_test_seq_delete_ok;
extern unsigned g_test_seq_delete_calls;
extern std::string g_test_seq_file_body;

namespace {

// A POST as a device backend delivers it: the declared Content-Length and the
// buffered body are separate inputs, because the whole point of the parity
// rules is what happens when they disagree.
WebRequestTestBackend postBackend(const char* body, size_t declaredLength) {
    WebRequestTestBackend b;
    b.body = body;
    b.contentLength = declaredLength;
    return b;
}

WebRequestTestBackend postBackend(const char* body) {
    return postBackend(body, body != nullptr ? std::strlen(body) : 0);
}

WebRequestTestBackend paramBackend(const WebRequestTestParam* params, size_t count) {
    WebRequestTestBackend b;
    b.params = params;
    b.paramCount = count;
    return b;
}

void seedIndex(const char* name) {
    SeqIndexEntry e = {};
    std::snprintf(e.name, sizeof(e.name), "%s", name);
    std::snprintf(e.file, sizeof(e.file), "%s", "seq1.json");
    e.valid = true;
    seqStoreIndexAdd(e);
}

bool bodyContains(const WebRequestTestBackend& b, const char* needle) {
    return std::strstr(b.sentBody, needle) != nullptr;
}

}  // namespace

void setUp() {
    // The test route hands the name to the dispatcher, so its queue has to
    // exist or every accepted name comes back 503 "sequence queue full" and
    // the handler's own decisions become unobservable.
    sequenceDispatcherInit();
    seqStoreIndexClear();
    g_test_seq_save_result = pcOk();
    g_test_seq_saved_body.clear();
    g_test_seq_save_calls = 0;
    g_test_seq_delete_ok = true;
    g_test_seq_delete_calls = 0;
    g_test_seq_file_body.clear();
}

void tearDown() {
}

// -----------------------------------------------------------------------------
// POST /api/seq -- the save path and its three distinct body failures
// -----------------------------------------------------------------------------

void test_save_passes_body_through_verbatim() {
    const char* json = "{\"v\":1,\"name\":\"DM:TEST\",\"steps\":[]}";
    WebRequestTestBackend b = postBackend(json);
    WebRequest req(&b);
    handleSeqPost(req);

    TEST_ASSERT_EQUAL_INT(200, b.sentCode);
    TEST_ASSERT_EQUAL_UINT(1, g_test_seq_save_calls);
    // The bytes the store received are the bytes the client sent: no
    // truncation at the seam, which is the regression this route has had.
    TEST_ASSERT_EQUAL_STRING(json, g_test_seq_saved_body.c_str());
}

void test_save_with_no_body_is_400() {
    WebRequestTestBackend b = postBackend(nullptr, 0);
    WebRequest req(&b);
    handleSeqPost(req);

    TEST_ASSERT_EQUAL_INT(400, b.sentCode);
    TEST_ASSERT_TRUE(bodyContains(b, "missing JSON body"));
    TEST_ASSERT_EQUAL_UINT(0, g_test_seq_save_calls);
}

void test_save_over_the_file_cap_is_413() {
    // Declared over SEQ_FILE_MAX_BYTES. The backend would not have buffered it,
    // so the handler has to answer from the declared length alone.
    WebRequestTestBackend b = postBackend(nullptr, SEQ_FILE_MAX_BYTES + 1);
    WebRequest req(&b);
    handleSeqPost(req);

    TEST_ASSERT_EQUAL_INT(413, b.sentCode);
    TEST_ASSERT_TRUE(bodyContains(b, "payload too large"));
    TEST_ASSERT_EQUAL_UINT(0, g_test_seq_save_calls);
}

void test_save_with_lost_body_is_500_not_400() {
    // In range but the buffer never survived: our allocation failure, not the
    // client's malformed request. Collapsing this into the 400 would report the
    // wrong party at fault -- and would silently change a status code the
    // migration's parity criterion is measured on.
    WebRequestTestBackend b = postBackend(nullptr, 128);
    WebRequest req(&b);
    handleSeqPost(req);

    TEST_ASSERT_EQUAL_INT(500, b.sentCode);
    TEST_ASSERT_TRUE(bodyContains(b, "request buffer alloc failed"));
    TEST_ASSERT_EQUAL_UINT(0, g_test_seq_save_calls);
}

void test_save_at_the_cap_is_accepted() {
    // The boundary belongs to the client: exactly SEQ_FILE_MAX_BYTES is what
    // the store itself accepts, so the route must not reject it one byte early.
    std::string json(SEQ_FILE_MAX_BYTES, 'x');
    WebRequestTestBackend b = postBackend(json.c_str(), SEQ_FILE_MAX_BYTES);
    WebRequest req(&b);
    handleSeqPost(req);

    TEST_ASSERT_EQUAL_UINT(1, g_test_seq_save_calls);
}

void test_save_rejection_reports_the_failing_field() {
    g_test_seq_save_result = pcFail("steps[3].cmd", "unknown command");
    WebRequestTestBackend b = postBackend("{\"v\":1}");
    WebRequest req(&b);
    handleSeqPost(req);

    TEST_ASSERT_EQUAL_INT(400, b.sentCode);
    TEST_ASSERT_TRUE(bodyContains(b, "steps[3].cmd"));
    TEST_ASSERT_TRUE(bodyContains(b, "unknown command"));
}

// -----------------------------------------------------------------------------
// POST /api/seq/test -- one handler, two client shapes
// -----------------------------------------------------------------------------

void test_test_accepts_a_json_body_name() {
    WebRequestTestBackend b = postBackend("{\"name\":\"DM:ROCKMARCH\"}");
    WebRequest req(&b);
    handleSeqTestPost(req);

    TEST_ASSERT_EQUAL_INT(200, b.sentCode);
}

void test_test_accepts_a_form_parameter_name() {
    // The form shape the older clients use. Both backends parse a form body
    // into parameters, so the handler sees it here and not in body().
    const WebRequestTestParam params[] = {{"name", "DM:ROCKMARCH"}};
    WebRequestTestBackend b = paramBackend(params, 1);
    WebRequest req(&b);
    handleSeqTestPost(req);

    TEST_ASSERT_EQUAL_INT(200, b.sentCode);
}

void test_test_trims_whitespace_around_the_name() {
    const WebRequestTestParam params[] = {{"name", "  DM:ROCKMARCH  "}};
    WebRequestTestBackend b = paramBackend(params, 1);
    WebRequest req(&b);
    handleSeqTestPost(req);

    TEST_ASSERT_EQUAL_INT(200, b.sentCode);
}

void test_test_rejects_a_name_without_the_dm_prefix() {
    const WebRequestTestParam params[] = {{"name", "ROCKMARCH"}};
    WebRequestTestBackend b = paramBackend(params, 1);
    WebRequest req(&b);
    handleSeqTestPost(req);

    TEST_ASSERT_EQUAL_INT(400, b.sentCode);
    TEST_ASSERT_TRUE(bodyContains(b, "DM:"));
}

void test_test_rejects_a_malformed_json_body() {
    WebRequestTestBackend b = postBackend("{not json");
    WebRequest req(&b);
    handleSeqTestPost(req);

    TEST_ASSERT_EQUAL_INT(400, b.sentCode);
    TEST_ASSERT_TRUE(bodyContains(b, "invalid json body"));
}

// -----------------------------------------------------------------------------
// GET /api/seq -- chunked read out of the store
// -----------------------------------------------------------------------------

void test_get_streams_the_stored_file() {
    seedIndex("DM:MYSEQ");
    g_test_seq_file_body = "{\"v\":1,\"name\":\"DM:MYSEQ\"}";
    const WebRequestTestParam params[] = {{"name", "DM:MYSEQ"}};
    WebRequestTestBackend b = paramBackend(params, 1);
    WebRequest req(&b);
    handleSeqGet(req);

    TEST_ASSERT_TRUE(b.sentChunked);
    TEST_ASSERT_EQUAL_STRING(g_test_seq_file_body.c_str(), b.sentBody);
    TEST_ASSERT_EQUAL_UINT(g_test_seq_file_body.size(), b.sentBodyLength);
}

void test_get_streams_a_file_larger_than_one_chunk() {
    // The whole reason the body is offset-addressed: it has to reassemble
    // correctly across more than one filler call.
    seedIndex("DM:BIG");
    g_test_seq_file_body.assign(5000, 'a');
    const WebRequestTestParam params[] = {{"name", "DM:BIG"}};
    WebRequestTestBackend b = paramBackend(params, 1);
    WebRequest req(&b);
    handleSeqGet(req);

    TEST_ASSERT_EQUAL_UINT(5000, b.sentBodyLength);
}

void test_get_of_an_unknown_name_is_404_before_any_body() {
    const WebRequestTestParam params[] = {{"name", "DM:NOPE"}};
    WebRequestTestBackend b = paramBackend(params, 1);
    WebRequest req(&b);
    handleSeqGet(req);

    TEST_ASSERT_EQUAL_INT(404, b.sentCode);
    TEST_ASSERT_FALSE(b.sentChunked);
}

void test_get_without_a_name_is_400() {
    WebRequestTestBackend b;
    WebRequest req(&b);
    handleSeqGet(req);

    TEST_ASSERT_EQUAL_INT(400, b.sentCode);
    TEST_ASSERT_TRUE(bodyContains(b, "missing name parameter"));
}

// -----------------------------------------------------------------------------
// DELETE /api/seq -- the method the seam gained for this route group
// -----------------------------------------------------------------------------

void test_delete_removes_an_indexed_sequence() {
    seedIndex("DM:MYSEQ");
    const WebRequestTestParam params[] = {{"name", "DM:MYSEQ"}};
    WebRequestTestBackend b = paramBackend(params, 1);
    WebRequest req(&b);
    handleSeqDelete(req);

    TEST_ASSERT_EQUAL_INT(200, b.sentCode);
    TEST_ASSERT_EQUAL_UINT(1, g_test_seq_delete_calls);
    TEST_ASSERT_TRUE(bodyContains(b, "\"ok\":true"));
}

void test_delete_of_an_unknown_name_is_404() {
    const WebRequestTestParam params[] = {{"name", "DM:NOPE"}};
    WebRequestTestBackend b = paramBackend(params, 1);
    WebRequest req(&b);
    handleSeqDelete(req);

    TEST_ASSERT_EQUAL_INT(404, b.sentCode);
    TEST_ASSERT_EQUAL_UINT(0, g_test_seq_delete_calls);
}

void test_delete_reports_a_store_failure_as_500() {
    seedIndex("DM:MYSEQ");
    g_test_seq_delete_ok = false;
    const WebRequestTestParam params[] = {{"name", "DM:MYSEQ"}};
    WebRequestTestBackend b = paramBackend(params, 1);
    WebRequest req(&b);
    handleSeqDelete(req);

    TEST_ASSERT_EQUAL_INT(500, b.sentCode);
    TEST_ASSERT_TRUE(bodyContains(b, "delete failed"));
}

// -----------------------------------------------------------------------------
// Read-only listings and the idempotent stop
// -----------------------------------------------------------------------------

void test_list_serializes_the_index() {
    seedIndex("DM:MYSEQ");
    WebRequestTestBackend b;
    WebRequest req(&b);
    handleSeqListGet(req);

    TEST_ASSERT_EQUAL_INT(200, b.sentCode);
    TEST_ASSERT_TRUE(bodyContains(b, "DM:MYSEQ"));
    TEST_ASSERT_TRUE(bodyContains(b, "retrained"));
}

void test_list_of_an_empty_store_is_an_empty_array() {
    WebRequestTestBackend b;
    WebRequest req(&b);
    handleSeqListGet(req);

    TEST_ASSERT_EQUAL_INT(200, b.sentCode);
    TEST_ASSERT_EQUAL_STRING("[]", b.sentBody);
}

void test_builtins_lists_the_factory_catalog_without_steps() {
    WebRequestTestBackend b;
    WebRequest req(&b);
    handleSeqBuiltinsGet(req);

    TEST_ASSERT_EQUAL_INT(200, b.sentCode);
    TEST_ASSERT_TRUE(bodyContains(b, "stepCount"));
    // The list form is metadata only; step data is what made this payload
    // large enough to exhaust fragmented heap mid-send.
    TEST_ASSERT_FALSE(bodyContains(b, "\"steps\""));
}

void test_builtins_of_an_unknown_name_is_404() {
    const WebRequestTestParam params[] = {{"name", "DM:NOT_A_FACTORY_SEQ"}};
    WebRequestTestBackend b = paramBackend(params, 1);
    WebRequest req(&b);
    handleSeqBuiltinsGet(req);

    TEST_ASSERT_EQUAL_INT(404, b.sentCode);
}

void test_stop_is_idempotent_and_always_ok() {
    WebRequestTestBackend b;
    WebRequest req(&b);
    handleSeqStopPost(req);
    TEST_ASSERT_EQUAL_INT(200, b.sentCode);

    WebRequestTestBackend again;
    WebRequest req2(&again);
    handleSeqStopPost(req2);
    TEST_ASSERT_EQUAL_INT(200, again.sentCode);
}

void test_last_run_answers_even_with_no_recorded_run() {
    WebRequestTestBackend b;
    WebRequest req(&b);
    handleSeqLastRunGet(req);

    TEST_ASSERT_EQUAL_INT(200, b.sentCode);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_save_passes_body_through_verbatim);
    RUN_TEST(test_save_with_no_body_is_400);
    RUN_TEST(test_save_over_the_file_cap_is_413);
    RUN_TEST(test_save_with_lost_body_is_500_not_400);
    RUN_TEST(test_save_at_the_cap_is_accepted);
    RUN_TEST(test_save_rejection_reports_the_failing_field);

    RUN_TEST(test_test_accepts_a_json_body_name);
    RUN_TEST(test_test_accepts_a_form_parameter_name);
    RUN_TEST(test_test_trims_whitespace_around_the_name);
    RUN_TEST(test_test_rejects_a_name_without_the_dm_prefix);
    RUN_TEST(test_test_rejects_a_malformed_json_body);

    RUN_TEST(test_get_streams_the_stored_file);
    RUN_TEST(test_get_streams_a_file_larger_than_one_chunk);
    RUN_TEST(test_get_of_an_unknown_name_is_404_before_any_body);
    RUN_TEST(test_get_without_a_name_is_400);

    RUN_TEST(test_delete_removes_an_indexed_sequence);
    RUN_TEST(test_delete_of_an_unknown_name_is_404);
    RUN_TEST(test_delete_reports_a_store_failure_as_500);

    RUN_TEST(test_list_serializes_the_index);
    RUN_TEST(test_list_of_an_empty_store_is_an_empty_array);
    RUN_TEST(test_builtins_lists_the_factory_catalog_without_steps);
    RUN_TEST(test_builtins_of_an_unknown_name_is_404);
    RUN_TEST(test_stop_is_idempotent_and_always_ok);
    RUN_TEST(test_last_run_answers_even_with_no_recorded_run);

    return UNITY_END();
}
