// =============================================================================
// test/test_native/test_api_json_response/test_api_json_response.cpp
//
// Native unit tests for webSendJsonError() wire shape and variants.
// Validates that the shared error-response helper produces consistent,
// predictable JSON with the correct HTTP status code and Content-Type.
// =============================================================================
#include <unity.h>

#include <cstring>

#include "api_json_response.h"
#include "web_request_test_backend.h"

void setUp() {
}

void tearDown() {
}

// =============================================================================
// webSendJsonError wire shape
// =============================================================================

void test_web_send_json_error_returns_correct_status_code() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 400, "test error");

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
}

void test_web_send_json_error_sets_json_content_type() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 400, "test error");

    TEST_ASSERT_EQUAL_STRING("application/json", backend.sentContentType);
}

void test_web_send_json_error_carries_ok_false() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 400, "test error");

    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"ok\":false"));
}

void test_web_send_json_error_carries_error_token() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 400, "invalid input");

    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"error\":\"invalid input\""));
}

void test_web_send_json_error_token_only() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 400, "missing parameter");

    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"error\":\"missing parameter\""));
    TEST_ASSERT_NULL(strstr(backend.sentBody, "\"hint\""));
    TEST_ASSERT_NULL(strstr(backend.sentBody, "\"field\""));
}

void test_web_send_json_error_with_hint() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 423, "sleeping", "POST /api/wake");

    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"error\":\"sleeping\""));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"hint\":\"POST /api/wake\""));
    TEST_ASSERT_NULL(strstr(backend.sentBody, "\"field\""));
}

void test_web_send_json_error_with_field() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 400, "invalid range", nullptr, "speed");

    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"error\":\"invalid range\""));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"field\":\"speed\""));
    TEST_ASSERT_NULL(strstr(backend.sentBody, "\"hint\""));
}

void test_web_send_json_error_with_hint_and_field() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 400, "param error", "use value 0-100", "level");

    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"error\":\"param error\""));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"hint\":\"use value 0-100\""));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"field\":\"level\""));
}

// =============================================================================
// Sleeping 423 unified shape (the fixed divergence)
// =============================================================================

void test_web_send_json_error_sleeping_423_has_ok_false() {
    // This was the divergence: sleeping responses lacked ok:false.
    // Now all error responses including 423 sleeping carry it.
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 423, "sleeping", "POST /api/wake");

    TEST_ASSERT_EQUAL_INT(423, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"error\":\"sleeping\""));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"hint\":\"POST /api/wake\""));
}

void test_web_send_json_error_409_conflict() {
    // Profiler trace state conflicts now return 409 (not 200).
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 409, "trace already running");

    TEST_ASSERT_EQUAL_INT(409, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"error\":\"trace already running\""));
}

void test_web_send_json_error_various_status_codes() {
    // Verify status codes pass through unchanged
    const int testCodes[] = {400, 403, 404, 409, 413, 423, 429, 500, 503};
    for (size_t i = 0; i < sizeof(testCodes) / sizeof(testCodes[0]); ++i) {
        WebRequestTestBackend backend;
        WebRequest req(&backend);
        webSendJsonError(req, testCodes[i], "test");
        TEST_ASSERT_EQUAL_INT(testCodes[i], backend.sentCode);
    }
}

void test_web_send_json_error_json_is_valid() {
    // Spot-check that the response is valid JSON by looking for balanced braces
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    webSendJsonError(req, 400, "test error", "hint text", "field name");

    const char* body = backend.sentBody;
    TEST_ASSERT_NOT_NULL(body);

    // Body should start with { and end with }
    TEST_ASSERT_EQUAL_CHAR('{', body[0]);

    // Find the last character in the body (ignoring null terminator)
    int lastIdx = strlen(body) - 1;
    TEST_ASSERT_EQUAL_CHAR('}', body[lastIdx]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_web_send_json_error_returns_correct_status_code);
    RUN_TEST(test_web_send_json_error_sets_json_content_type);
    RUN_TEST(test_web_send_json_error_carries_ok_false);
    RUN_TEST(test_web_send_json_error_carries_error_token);
    RUN_TEST(test_web_send_json_error_token_only);
    RUN_TEST(test_web_send_json_error_with_hint);
    RUN_TEST(test_web_send_json_error_with_field);
    RUN_TEST(test_web_send_json_error_with_hint_and_field);
    RUN_TEST(test_web_send_json_error_sleeping_423_has_ok_false);
    RUN_TEST(test_web_send_json_error_409_conflict);
    RUN_TEST(test_web_send_json_error_various_status_codes);
    RUN_TEST(test_web_send_json_error_json_is_valid);
    return UNITY_END();
}
