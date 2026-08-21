// =============================================================================
// test/test_native/test_api_identity/test_api_identity.cpp
//
// Native unit tests for the /api/identity handlers through the WebRequest
// seam's host-test backend (ADR 0021). Drives handleIdentityGet() /
// handleIdentityPost() exactly as a device backend would, asserting on the
// captured response -- no vendor web-server types anywhere.
// =============================================================================
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "api_identity.h"
#include "config_cache.h"
#include "web_request_test_backend.h"

namespace {

void applyIdentity(const char* name, bool mdnsUseName) {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snprintf(snap.system.droid_name, sizeof(snap.system.droid_name), "%s", name);
    snap.system.mdns_use_name = mdnsUseName;
    configCacheApply(snap);
}

}  // namespace

void setUp() {
    applyIdentity("artoo", false);
}

void tearDown() {
}

void test_get_returns_identity_json() {
    applyIdentity("r2-d2", true);
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleIdentityGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("application/json", backend.sentContentType);
    TEST_ASSERT_EQUAL_STRING("{\"droidName\":\"r2-d2\",\"mdnsUseName\":true}", backend.sentBody);
    TEST_ASSERT_EQUAL_UINT(1, backend.sendCalls);
}

void test_post_without_name_is_rejected() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleIdentityPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"droidName is required\"}", backend.sentBody);
}

void test_post_invalid_name_is_rejected() {
    const WebRequestTestParam params[] = {{"droidName", "R2 D2"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleIdentityPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "droidName must be 1..32"));
}

void test_post_overlong_name_is_rejected_not_truncated() {
    // 40 chars of otherwise-valid charset: must be rejected as over-long, not
    // silently truncated into a valid name by the seam's copy-out buffer.
    const char* longName = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const WebRequestTestParam params[] = {{"droidName", longName}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleIdentityPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
}

void test_post_valid_name_applies_and_echoes() {
    const WebRequestTestParam params[] = {{"droidName", "chopper"}, {"mdnsUseName", "true"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleIdentityPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"droidName\":\"chopper\",\"mdnsUseName\":true}", backend.sentBody);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_STRING("chopper", snap.system.droid_name);
    TEST_ASSERT_TRUE(snap.system.mdns_use_name);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_get_returns_identity_json);
    RUN_TEST(test_post_without_name_is_rejected);
    RUN_TEST(test_post_invalid_name_is_rejected);
    RUN_TEST(test_post_overlong_name_is_rejected_not_truncated);
    RUN_TEST(test_post_valid_name_applies_and_echoes);
    return UNITY_END();
}
