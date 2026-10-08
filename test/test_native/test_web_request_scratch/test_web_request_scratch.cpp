// =============================================================================
// test/test_native/test_web_request_scratch/test_web_request_scratch.cpp
//
// The web request scratch (include/web_request_scratch.h, #428): the one store
// every web handler's request-scoped buffer shares. What makes sharing safe is
// that no handler can ever hold it while another does, and that nothing but
// the server task can hold it at all - so those are what this proves, with the
// real accessor and a real handler.
// =============================================================================

#include <unity.h>

#include <string.h>

#include <freertos/task.h>

#include "api_config.h"
#include "api_config_apply.h"
#include "web_request.h"
#include "web_request_scratch.h"
#include "web_request_test_backend.h"

void setUp(void) {
    // The harness binds the host task at static initialisation
    // (src/native_test_stubs.cpp); rebind it in case a test rebound it.
    webRequestScratchBindOwner(xTaskGetCurrentTaskHandle());
}

void tearDown(void) {
    webRequestScratchBindOwner(xTaskGetCurrentTaskHandle());
}

// A handler reaching a second scratch user mid-request would write over the
// first one's buffer. The second claim is refused instead, and the store is
// usable again once the first lets go.
void test_a_second_claim_while_one_is_held_is_refused(void) {
    {
        WebRequestScratch<ConfigApplyResult> first;
        TEST_ASSERT_TRUE(static_cast<bool>(first));

        WebRequestScratch<RcMapApplyResult> nested;
        TEST_ASSERT_FALSE(static_cast<bool>(nested));
    }
    WebRequestScratch<RcMapApplyResult> after;
    TEST_ASSERT_TRUE(static_cast<bool>(after));
}

// Only the task that serves requests may hold the store: any other task's
// claim is refused rather than racing the server task for it.
void test_a_claim_from_a_task_other_than_the_server_task_is_refused(void) {
    static int s_someOtherTask = 0;
    webRequestScratchBindOwner(static_cast<TaskHandle_t>(&s_someOtherTask));

    WebRequestScratch<ConfigApplyResult> claim;
    TEST_ASSERT_FALSE(static_cast<bool>(claim));
}

// Every claim starts from the state a fresh static had at boot. The apply
// results carry non-zero member defaults, so a request that inherited the last
// one's result could report its refusal or its log lines.
void test_every_claim_starts_from_a_fresh_value(void) {
    {
        WebRequestScratch<ConfigApplyResult> first;
        TEST_ASSERT_TRUE(static_cast<bool>(first));
        first->error.hasError = true;
        snprintf(first->error.message, sizeof(first->error.message), "left behind");
        first->applied.count = 3;
    }
    WebRequestScratch<ConfigApplyResult> second;
    TEST_ASSERT_TRUE(static_cast<bool>(second));
    TEST_ASSERT_FALSE(second->error.hasError);
    TEST_ASSERT_EQUAL_STRING("", second->error.message);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)second->applied.count);
}

// A refused claim is answered as an error by the handler - never a silent
// reuse of a store somebody else holds, and never a body it did not build.
void test_a_handler_whose_claim_is_refused_answers_an_error(void) {
    WebRequestScratch<ConfigApplyResult> held;
    TEST_ASSERT_TRUE(static_cast<bool>(held));

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleRcMapGet(req);

    TEST_ASSERT_EQUAL_INT(500, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "request scratch unavailable"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_second_claim_while_one_is_held_is_refused);
    RUN_TEST(test_a_claim_from_a_task_other_than_the_server_task_is_refused);
    RUN_TEST(test_every_claim_starts_from_a_fresh_value);
    RUN_TEST(test_a_handler_whose_claim_is_refused_answers_an_error);
    return UNITY_END();
}
