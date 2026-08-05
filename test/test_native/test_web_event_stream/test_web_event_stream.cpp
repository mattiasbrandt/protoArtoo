// =============================================================================
// test/test_native/test_web_event_stream/test_web_event_stream.cpp
//
// Native unit tests for the live update stream's decision core and its
// /api/events handler.
//
// What is worth pinning here is the behaviour that made issue #83 necessary:
// that a client which stops accepting bytes is dropped rather than waited on,
// that the client cap turns a fourth tab away before any upgrade happens, and
// that the registry cannot leak capacity across a reconnect. The deadline value
// itself is calibration and is passed in, so a retune does not rewrite these.
// The wire behaviour under a genuinely stalled socket is device evidence
// (tools/webload_sse_stall.py) and is recorded on the issue instead.
// =============================================================================
#include <unity.h>

#include <cstring>

#include "api_events.h"
#include "web_event_stream.h"
#include "web_request_test_backend.h"

// A legible deadline, not the calibrated device value.
static constexpr uint32_t kDeadlineMs = 200;

// Set by src/native_test_stubs.cpp's event-stream transport stub, so a test can
// tell the handler how many streams are already open.
extern size_t g_test_event_stream_clients;

void setUp() {
    g_test_event_stream_clients = 0;
    g_webRefusedSseCap = 0;
}

void tearDown() {
}

// -----------------------------------------------------------------------------
// Registry
// -----------------------------------------------------------------------------

void test_registry_starts_empty() {
    WebEventStreamRegistry registry;
    webEventStreamRegistryInit(&registry);

    TEST_ASSERT_EQUAL_size_t(0, registry.count);
    // Descriptor 0 is a legitimate socket, so an empty registry must not be
    // able to report it as a member.
    TEST_ASSERT_FALSE(webEventStreamRegistryHas(&registry, 0));
}

void test_registry_accepts_clients_up_to_the_cap() {
    WebEventStreamRegistry registry;
    webEventStreamRegistryInit(&registry);

    for (int i = 0; i < PA_ADMISSION_MAX_SSE_CLIENTS; i++) {
        TEST_ASSERT_TRUE(webEventStreamRegistryAdd(&registry, 40 + i));
    }
    TEST_ASSERT_EQUAL_size_t(PA_ADMISSION_MAX_SSE_CLIENTS, registry.count);
}

void test_registry_refuses_the_client_past_the_cap() {
    WebEventStreamRegistry registry;
    webEventStreamRegistryInit(&registry);
    for (int i = 0; i < PA_ADMISSION_MAX_SSE_CLIENTS; i++) {
        webEventStreamRegistryAdd(&registry, 40 + i);
    }

    TEST_ASSERT_FALSE(webEventStreamRegistryAdd(&registry, 99));
    TEST_ASSERT_EQUAL_size_t(PA_ADMISSION_MAX_SSE_CLIENTS, registry.count);
}

void test_re_adding_a_client_does_not_consume_a_second_slot() {
    WebEventStreamRegistry registry;
    webEventStreamRegistryInit(&registry);
    webEventStreamRegistryAdd(&registry, 42);

    // A descriptor can arrive twice when a close callback is still in flight as
    // its replacement connects. A duplicate entry would double-send and would
    // permanently eat one of only three slots.
    TEST_ASSERT_TRUE(webEventStreamRegistryAdd(&registry, 42));
    TEST_ASSERT_EQUAL_size_t(1, registry.count);
}

void test_removing_frees_the_slot_for_a_reconnect() {
    WebEventStreamRegistry registry;
    webEventStreamRegistryInit(&registry);
    for (int i = 0; i < PA_ADMISSION_MAX_SSE_CLIENTS; i++) {
        webEventStreamRegistryAdd(&registry, 40 + i);
    }

    TEST_ASSERT_TRUE(webEventStreamRegistryRemove(&registry, 41));
    TEST_ASSERT_FALSE(webEventStreamRegistryHas(&registry, 41));
    // The whole reconnect story: an evicted client comes back through the
    // frontend's backoff and must find room waiting for it.
    TEST_ASSERT_TRUE(webEventStreamRegistryAdd(&registry, 77));
    TEST_ASSERT_EQUAL_size_t(PA_ADMISSION_MAX_SSE_CLIENTS, registry.count);
}

void test_removing_an_unknown_socket_leaves_the_count_alone() {
    WebEventStreamRegistry registry;
    webEventStreamRegistryInit(&registry);
    webEventStreamRegistryAdd(&registry, 42);

    // Every closing connection runs through the close callback, streams and
    // ordinary requests alike -- and the broadcaster may already have evicted
    // the socket the callback is reporting. Both must be free.
    TEST_ASSERT_FALSE(webEventStreamRegistryRemove(&registry, 43));
    TEST_ASSERT_FALSE(webEventStreamRegistryRemove(&registry, 43));
    TEST_ASSERT_EQUAL_size_t(1, registry.count);
}

void test_snapshot_reports_only_registered_sockets() {
    WebEventStreamRegistry registry;
    webEventStreamRegistryInit(&registry);
    webEventStreamRegistryAdd(&registry, 40);
    webEventStreamRegistryAdd(&registry, 41);
    webEventStreamRegistryRemove(&registry, 40);

    int sockets[PA_ADMISSION_MAX_SSE_CLIENTS] = {};
    const size_t count =
        webEventStreamRegistrySnapshot(&registry, sockets, PA_ADMISSION_MAX_SSE_CLIENTS);

    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_EQUAL_INT(41, sockets[0]);
}

// -----------------------------------------------------------------------------
// Framing
// -----------------------------------------------------------------------------

void test_prefix_carries_the_event_name_the_frontend_listens_for() {
    char out[kWebEventStreamPrefixMax];
    const size_t len = webEventStreamFormatPrefix(out, sizeof(out), "rc", 1234);

    // data/status_stream.js attaches addEventListener("rc", ...), so the name
    // has to travel in the frame rather than being implied by the payload.
    TEST_ASSERT_EQUAL_STRING("id: 1234\r\nevent: rc\r\ndata: ", out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), len);
}

void test_prefix_omits_a_zero_id() {
    char out[kWebEventStreamPrefixMax];
    webEventStreamFormatPrefix(out, sizeof(out), "status", 0);

    // "id: 0" would be stored by the browser and replayed in Last-Event-ID on
    // reconnect, promising a replay semantic this stream does not have.
    TEST_ASSERT_EQUAL_STRING("event: status\r\ndata: ", out);
}

void test_prefix_always_introduces_the_payload() {
    char out[kWebEventStreamPrefixMax];
    webEventStreamFormatPrefix(out, sizeof(out), nullptr, 0);

    // Without a "data:" field no EventSource listener ever sees the payload,
    // however well-formed the rest of the frame is.
    TEST_ASSERT_EQUAL_STRING("data: ", out);
}

void test_prefix_refuses_a_buffer_too_small_to_be_useful() {
    char out[4] = "xxx";
    TEST_ASSERT_EQUAL_size_t(0, webEventStreamFormatPrefix(out, sizeof(out), "status", 1));
}

void test_frame_terminator_ends_the_event() {
    // A blank line is what separates one event from the next; without it the
    // browser holds the payload waiting for more fields.
    TEST_ASSERT_EQUAL_STRING("\r\n\r\n", kWebEventStreamTerminator);
    TEST_ASSERT_EQUAL_size_t(4, kWebEventStreamTerminatorLength);
}

// -----------------------------------------------------------------------------
// Bounded send
// -----------------------------------------------------------------------------

void test_a_progressing_send_continues() {
    TEST_ASSERT_TRUE(WebEventSendVerdict::kContinue ==
                     webEventSendDecide(WebEventWriteResult::kWrote, 10, kDeadlineMs));
}

void test_a_briefly_blocked_send_is_retried() {
    // A full receive window is normal for a moment; the fix is not to give up
    // on the first EAGAIN, it is to stop waiting eventually.
    TEST_ASSERT_TRUE(WebEventSendVerdict::kContinue ==
                     webEventSendDecide(WebEventWriteResult::kWouldBlock, 10, kDeadlineMs));
}

void test_a_blocked_send_is_evicted_at_the_deadline() {
    TEST_ASSERT_TRUE(WebEventSendVerdict::kEvictDeadline ==
                     webEventSendDecide(WebEventWriteResult::kWouldBlock, kDeadlineMs,
                                        kDeadlineMs));
}

void test_a_trickling_client_is_evicted_at_the_deadline_too() {
    // The defect this replaces is a client holding the broadcaster, and one
    // that accepts a handful of bytes per attempt holds it just as effectively
    // as one that accepts none. "Still moving" is not an exemption.
    TEST_ASSERT_TRUE(WebEventSendVerdict::kEvictDeadline ==
                     webEventSendDecide(WebEventWriteResult::kWrote, kDeadlineMs + 50,
                                        kDeadlineMs));
}

void test_a_failed_write_is_evicted_as_an_error_not_a_deadline() {
    // An ordinary tab close arrives here. Counting it as a deadline breach
    // would fill the eviction counter with normal traffic and destroy the one
    // number that says whether the guard is doing anything.
    TEST_ASSERT_TRUE(WebEventSendVerdict::kEvictError ==
                     webEventSendDecide(WebEventWriteResult::kFailed, 1, kDeadlineMs));
}

void test_a_dead_connection_is_an_error_even_past_the_deadline() {
    TEST_ASSERT_TRUE(WebEventSendVerdict::kEvictError ==
                     webEventSendDecide(WebEventWriteResult::kFailed, kDeadlineMs * 10,
                                        kDeadlineMs));
}

// -----------------------------------------------------------------------------
// GET /api/events
// -----------------------------------------------------------------------------

void test_a_client_below_the_cap_gets_a_stream() {
    g_test_event_stream_clients = PA_ADMISSION_MAX_SSE_CLIENTS - 1;
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleEventsGet(req);

    TEST_ASSERT_TRUE(backend.eventStreamStarted);
    // The response is open-ended from here; anything else sent would land in
    // the middle of the stream the broadcaster now owns.
    TEST_ASSERT_EQUAL_UINT(0, backend.sendCalls);
}

void test_a_client_past_the_cap_is_refused_before_the_upgrade() {
    g_test_event_stream_clients = PA_ADMISSION_MAX_SSE_CLIENTS;
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleEventsGet(req);

    // Refused *before* the upgrade is the acceptance criterion: no stream head
    // is written and no connection is registered, rather than a fourth client
    // being admitted and then reaped.
    TEST_ASSERT_FALSE(backend.eventStreamStarted);
    TEST_ASSERT_EQUAL_INT(503, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT32(1, g_webRefusedSseCap);
}

void test_a_refusal_is_answered_so_the_frontend_can_back_off() {
    g_test_event_stream_clients = PA_ADMISSION_MAX_SSE_CLIENTS;
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleEventsGet(req);

    // data/status_stream.js reconnects from EventSource.onerror, which a
    // non-2xx triggers. The body only has to exist and be legible in a log.
    TEST_ASSERT_EQUAL_UINT(1, backend.sendCalls);
    TEST_ASSERT_TRUE(backend.sentBodyLength > 0);
}

void test_a_transport_that_cannot_start_a_stream_still_answers() {
    WebRequestTestBackend backend;
    backend.eventStreamFails = true;
    WebRequest req(&backend);

    handleEventsGet(req);

    // A handler that neither upgrades nor replies leaves the browser waiting on
    // a request that will never be answered.
    TEST_ASSERT_FALSE(backend.eventStreamStarted);
    TEST_ASSERT_EQUAL_INT(503, backend.sentCode);
    // Not a cap refusal: the counter has to keep meaning "too many tabs".
    TEST_ASSERT_EQUAL_UINT32(0, g_webRefusedSseCap);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_registry_starts_empty);
    RUN_TEST(test_registry_accepts_clients_up_to_the_cap);
    RUN_TEST(test_registry_refuses_the_client_past_the_cap);
    RUN_TEST(test_re_adding_a_client_does_not_consume_a_second_slot);
    RUN_TEST(test_removing_frees_the_slot_for_a_reconnect);
    RUN_TEST(test_removing_an_unknown_socket_leaves_the_count_alone);
    RUN_TEST(test_snapshot_reports_only_registered_sockets);

    RUN_TEST(test_prefix_carries_the_event_name_the_frontend_listens_for);
    RUN_TEST(test_prefix_omits_a_zero_id);
    RUN_TEST(test_prefix_always_introduces_the_payload);
    RUN_TEST(test_prefix_refuses_a_buffer_too_small_to_be_useful);
    RUN_TEST(test_frame_terminator_ends_the_event);

    RUN_TEST(test_a_progressing_send_continues);
    RUN_TEST(test_a_briefly_blocked_send_is_retried);
    RUN_TEST(test_a_blocked_send_is_evicted_at_the_deadline);
    RUN_TEST(test_a_trickling_client_is_evicted_at_the_deadline_too);
    RUN_TEST(test_a_failed_write_is_evicted_as_an_error_not_a_deadline);
    RUN_TEST(test_a_dead_connection_is_an_error_even_past_the_deadline);

    RUN_TEST(test_a_client_below_the_cap_gets_a_stream);
    RUN_TEST(test_a_client_past_the_cap_is_refused_before_the_upgrade);
    RUN_TEST(test_a_refusal_is_answered_so_the_frontend_can_back_off);
    RUN_TEST(test_a_transport_that_cannot_start_a_stream_still_answers);

    return UNITY_END();
}
