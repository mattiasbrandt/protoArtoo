// =============================================================================
// test/test_native/test_api_actions_json/test_api_actions_json.cpp
//
// Native unit tests for GET /api/actions through the WebRequest seam's
// host-test backend (ADR 0021). The registry payload is produced offset by
// offset across chunk boundaries, so these cover the one thing a whole-body
// handler cannot get wrong and this one can: a slice boundary landing in the
// middle of a field.
// =============================================================================
#include <unity.h>

#include <cstring>

#include "action_registry.h"
#include "api_actions.h"
#include "web_request_test_backend.h"

void setUp() {
}

void tearDown() {
}

void test_get_sends_chunked_json_array() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleActionsGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("application/json", backend.sentContentType);
    TEST_ASSERT_TRUE(backend.sentChunked);
    TEST_ASSERT_EQUAL_UINT(1, backend.sendCalls);
    TEST_ASSERT_EQUAL_CHAR('[', backend.sentBody[0]);
    TEST_ASSERT_EQUAL_CHAR(']', backend.sentBody[backend.sentBodyLength - 1]);
}

void test_body_contains_every_registry_entry() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleActionsGet(req);

    // One "id" field per entry, and every action's name present verbatim --
    // a chunk boundary that dropped or duplicated bytes would break both.
    size_t idFields = 0;
    for (const char* p = backend.sentBody; (p = strstr(p, "{\"id\":")) != nullptr; ++p) {
        idFields++;
    }
    TEST_ASSERT_EQUAL_UINT(ACTION_REGISTRY_SIZE, idFields);

    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, ACTION_REGISTRY[i].name));
    }
}

void test_body_has_no_gap_or_overlap_at_chunk_boundaries() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleActionsGet(req);

    // The host backend slices at 64 bytes, well inside a ~200-byte entry, so
    // every entry spans boundaries. Balanced braces and one separating comma
    // between entries is what a dropped or repeated slice would break.
    int depth = 0;
    size_t objects = 0;
    size_t topLevelCommas = 0;
    for (size_t i = 0; i < backend.sentBodyLength; ++i) {
        const char c = backend.sentBody[i];
        if (c == '{') {
            depth++;
            objects++;
        } else if (c == '}') {
            depth--;
            TEST_ASSERT_TRUE(depth >= 0);
        } else if (c == ',' && depth == 0) {
            topLevelCommas++;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, depth);
    TEST_ASSERT_EQUAL_UINT(ACTION_REGISTRY_SIZE, objects);
    TEST_ASSERT_EQUAL_UINT(ACTION_REGISTRY_SIZE - 1, topLevelCommas);
}

void test_body_carries_nullable_feature_requirements_for_every_entry() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleActionsGet(req);

    size_t boardFields = 0;
    for (const char* p = backend.sentBody;
         (p = strstr(p, "\"board_capability\":null")) != nullptr; ++p) {
        boardFields++;
    }
    size_t buildFields = 0;
    for (const char* p = backend.sentBody;
         (p = strstr(p, "\"build_flag\":null")) != nullptr; ++p) {
        buildFields++;
    }
    TEST_ASSERT_EQUAL_UINT(ACTION_REGISTRY_SIZE, boardFields);
    TEST_ASSERT_EQUAL_UINT(ACTION_REGISTRY_SIZE, buildFields);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_get_sends_chunked_json_array);
    RUN_TEST(test_body_contains_every_registry_entry);
    RUN_TEST(test_body_has_no_gap_or_overlap_at_chunk_boundaries);
    RUN_TEST(test_body_carries_nullable_feature_requirements_for_every_entry);
    return UNITY_END();
}
