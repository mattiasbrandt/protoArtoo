/**
 * Test: the Console module seam is serialized across its two adapters (#262,
 * criterion 1; #206 "Serialized at the module seam - browser and serial
 * cannot race configuration persistence or shared result state").
 *
 * WHAT A PREEMPTION LOOKS LIKE HERE. The serial adapter runs in the Console
 * task and the browser adapter in the web server's task, both pinned to
 * Core 0 (src/main.cpp; src/web/web_request_psychic.cpp's
 * `config.core_id = 0`), so one can be scheduled out in the middle of a
 * response and the other run to completion before it resumes. The observable
 * boundary where that can happen, and the only one either adapter can be
 * interrupted at without the module noticing, is a record sink callback: the
 * module hands a record out, the adapter writes it to a transport, and the
 * scheduler may run the other adapter there.
 *
 * So these tests re-enter consoleExecuteCommand() from inside a sink
 * callback, with the other source. That is not a simulation of a preemption -
 * it IS one, deterministically placed: the outer request is genuinely
 * suspended mid-response with all of its state live while the inner one runs
 * a complete request through the same module, the same catalog and the same
 * shared statics.
 *
 * Everything runs through consoleExecuteCommand() with the two real
 * ConsoleCommandSource values, never through an HTTP stack: the seam under
 * test is below both adapters, and putting a web server in front of it would
 * only add a component that cannot be driven natively.
 *
 * TWO SHARED SURFACES ARE UNDER TEST, and they fail differently:
 *  - The per-request scratch the module splits a command line in
 *    (consoleExecuteCommand()'s `lineBuf`, which opName/rawArgs and every
 *    parsed argument point INTO). It is a stack local, so each nested request
 *    gets its own; were it static, the inner request would overwrite the
 *    outer's still-live argument pointers mid-response. The listing tests
 *    below read the outer's answer after the interruption precisely so that
 *    would show.
 *  - The config-write critical section (`s_consoleConfigApplyResult` under
 *    the config write lock, ConfigWriteLock in include/api_config.h - the one
 *    every config writer takes since #269, this module's two adapters and the
 *    REST routes alike). The lock tests below drive it from BOTH Console
 *    sources, which the existing coverage in test_console_module.cpp does only
 *    from the serial one; test_config_write_lock.cpp drives the REST side.
 */

#include <unity.h>

#include <cstdio>
#include <cstring>

#include <freertos/semphr.h>  // paStubMutexReset()/paStubMutexStorage() - the native
                              // mutex stub's exposed singleton, which
                              // xSemaphoreCreateMutexStatic() always returns, so a
                              // test can inspect the real take/give accounting of
                              // the config write lock (src/web/api_config.cpp)

#include "config_cache.h"
#include "console_catalog.h"
#include "console_module.h"
#include "console_record.h"

// =============================================================================
// Capture: one per concurrent request, so the two answers can be compared
// against each other rather than merged into one stream.
// =============================================================================

static const int kMaxRecords = 320;
static const int kRecordTextMax = 192;

struct Capture {
    char lines[kMaxRecords][kRecordTextMax];
    uint32_t ids[kMaxRecords];
    int count;
    int overflowed;
    uint32_t requestId;
};

static Capture g_outer;
static Capture g_inner;

static void captureReset(Capture* c) {
    memset(c, 0, sizeof(*c));
}

static void captureAdd(Capture* c, uint32_t requestId, const char* text) {
    if (c->count >= kMaxRecords) {
        c->overflowed = 1;
        return;
    }
    snprintf(c->lines[c->count], kRecordTextMax, "%s", text);
    c->ids[c->count] = requestId;
    c->count++;
}

// The nesting plan: when the outer request emits its Nth record, run the
// named command from the other source. Zero disables nesting.
struct NestPlan {
    int fireAfterOuterRecord;
    const char* command;
    ConsoleCommandSource source;
    int fired;
};

static NestPlan g_plan;

static void runInto(Capture* cap, ConsoleCommandSource source, const char* command);

// Called from every outer-sink callback, after the record is captured.
static void maybeNest(void) {
    if (g_plan.command == nullptr || g_plan.fired) {
        return;
    }
    if (g_outer.count != g_plan.fireAfterOuterRecord) {
        return;
    }
    g_plan.fired = 1;
    runInto(&g_inner, g_plan.source, g_plan.command);
}

static Capture* g_active = nullptr;  // which capture the sink below writes to

// One sink pair. Which capture a callback writes to is decided by g_active,
// saved and restored around a nested call (runInto) exactly the way a real
// scheduler would leave each task's own locals intact.
static void sinkBegin(uint32_t requestId, const char* operationType) {
    char text[kRecordTextMax];
    snprintf(text, sizeof(text), "begin operation=%s", operationType);
    Capture* c = g_active;
    captureAdd(c, requestId, text);
    if (c == &g_outer) maybeNest();
}

static void sinkField(uint32_t requestId, const char* name, const char* value) {
    char text[kRecordTextMax];
    snprintf(text, sizeof(text), "field name=%s value=%s", name, value);
    Capture* c = g_active;
    captureAdd(c, requestId, text);
    if (c == &g_outer) maybeNest();
}

static void sinkItem(uint32_t requestId, const char* value) {
    char text[kRecordTextMax];
    snprintf(text, sizeof(text), "item value=%s", value);
    Capture* c = g_active;
    captureAdd(c, requestId, text);
    if (c == &g_outer) maybeNest();
}

static void sinkResult(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                       ConsoleReason reason) {
    char text[kRecordTextMax];
    snprintf(text, sizeof(text), "result status=%s outcome=%s%s%s", consoleStatusString(status),
             consoleOutcomeString(outcome), consoleReasonIsPresent(reason) ? " reason=" : "",
             consoleReasonIsPresent(reason) ? consoleReasonString(reason) : "");
    Capture* c = g_active;
    captureAdd(c, requestId, text);
    if (c == &g_outer) maybeNest();
}

static void sinkEnd(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                    ConsoleReason reason) {
    char text[kRecordTextMax];
    snprintf(text, sizeof(text), "end status=%s outcome=%s%s%s", consoleStatusString(status),
             consoleOutcomeString(outcome), consoleReasonIsPresent(reason) ? " reason=" : "",
             consoleReasonIsPresent(reason) ? consoleReasonString(reason) : "");
    Capture* c = g_active;
    captureAdd(c, requestId, text);
    if (c == &g_outer) maybeNest();
}

static void runInto(Capture* cap, ConsoleCommandSource source, const char* command) {
    Capture* saved = g_active;
    g_active = cap;

    ConsoleRecordSink sink = {};
    sink.onRecordBegin = sinkBegin;
    sink.onRecordField = sinkField;
    sink.onRecordItem = sinkItem;
    sink.onRecordResult = sinkResult;
    sink.onRecordEnd = sinkEnd;

    ConsoleRequest req = {};
    req.requestId = consoleGetNextRequestId();
    req.source = source;
    req.operationName = command;
    cap->requestId = req.requestId;

    consoleExecuteCommand(&req, &sink);

    g_active = saved;
}

// =============================================================================

void setUp(void) {
    ConfigSnapshot snap = {};
    configCacheApply(snap);
    consoleModuleInit();  // idempotent
    paStubMutexReset();
    captureReset(&g_outer);
    captureReset(&g_inner);
    memset(&g_plan, 0, sizeof(g_plan));
    g_active = nullptr;
}

void tearDown(void) {
    paStubMutexReset();
}

// Every record a capture holds must carry that capture's own request ID.
static void assertAllRecordsCarryOwnId(const Capture* c, const char* what) {
    char msg[128];
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, c->overflowed, "capture overflowed - raise kMaxRecords");
    TEST_ASSERT_TRUE_MESSAGE(c->count > 0, "capture is empty");
    for (int i = 0; i < c->count; ++i) {
        snprintf(msg, sizeof(msg), "%s: record %d carries id %lu, not %lu (\"%s\")", what, i,
                 (unsigned long)c->ids[i], (unsigned long)c->requestId, c->lines[i]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(c->requestId, c->ids[i], msg);
    }
}

// =============================================================================
// Records
// =============================================================================

/**
 * The load-bearing test. A serial listing is interrupted, two items in, by a
 * complete browser query; the listing must then finish with exactly the
 * answer it would have given had nothing interrupted it.
 *
 * `operations type=config` is chosen because its filter argument lives in the
 * per-request line scratch and is dereferenced on every loop iteration - so
 * the inner request running with that scratch shared would not merely add
 * stray records, it would change what the outer one enumerates. The
 * comparison is against a clean, uninterrupted run of the same command taken
 * in the same test, so the expectation is the module's own answer rather than
 * a count copied into the test and going stale with the registry.
 *
 * Making consoleExecuteCommand()'s `lineBuf` static turns this red: the outer
 * listing's item set changes once the inner request overwrites the buffer its
 * `type=config` filter points into.
 */
void test_a_nested_web_query_does_not_change_a_serial_listings_answer(void) {
    // Clean reference run, no interruption.
    runInto(&g_outer, CONSOLE_SOURCE_SERIAL, "operations type=config");
    const int cleanCount = g_outer.count;
    static char cleanLines[kMaxRecords][kRecordTextMax];
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_outer.overflowed, "capture overflowed - raise kMaxRecords");
    TEST_ASSERT_TRUE_MESSAGE(cleanCount > 4,
                             "operations type=config listed almost nothing - fixture is wrong");
    for (int i = 0; i < cleanCount; ++i) {
        snprintf(cleanLines[i], kRecordTextMax, "%s", g_outer.lines[i]);
    }

    // Same command, interrupted after its second record by a browser query.
    captureReset(&g_outer);
    captureReset(&g_inner);
    g_plan.fireAfterOuterRecord = 2;
    g_plan.command = "system.status.health";
    g_plan.source = CONSOLE_SOURCE_WEB;
    g_plan.fired = 0;

    runInto(&g_outer, CONSOLE_SOURCE_SERIAL, "operations type=config");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_plan.fired, "the nested browser query never ran");
    TEST_ASSERT_TRUE_MESSAGE(g_inner.count > 0, "the nested browser query produced no records");

    TEST_ASSERT_EQUAL_INT_MESSAGE(cleanCount, g_outer.count,
                                  "the interrupted listing returned a different number of records");
    char msg[256];
    for (int i = 0; i < cleanCount; ++i) {
        snprintf(msg, sizeof(msg), "record %d differs: clean \"%s\" vs interrupted \"%s\"", i,
                 cleanLines[i], g_outer.lines[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cleanLines[i], g_outer.lines[i], msg);
    }
}

/**
 * Neither request's records can be mistaken for the other's. Every record
 * carries the ID of the request that produced it, and the two IDs differ -
 * which is what lets the operator (and docs/console-protocol.md s.3.1's
 * "records of one request may be separated by other lines") reassemble two
 * interleaved responses on one wire.
 */
void test_interleaved_responses_stay_attributable_to_their_own_request(void) {
    g_plan.fireAfterOuterRecord = 1;
    g_plan.command = "system.status.health";
    g_plan.source = CONSOLE_SOURCE_WEB;

    runInto(&g_outer, CONSOLE_SOURCE_SERIAL, "operations type=status");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_plan.fired, "the nested query never ran");
    assertAllRecordsCarryOwnId(&g_outer, "serial listing");
    assertAllRecordsCarryOwnId(&g_inner, "browser query");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(g_outer.requestId, g_inner.requestId,
                                  "both adapters were handed the same request ID");
    TEST_ASSERT_TRUE_MESSAGE(g_inner.requestId > g_outer.requestId,
                             "the later request did not get a later ID from the shared counter");
}

/**
 * The reverse direction, and with a field-emitting query on the outside: a
 * browser status query interrupted by a serial one keeps its own field
 * values. A shared response buffer between the two adapters would show up
 * here as the outer's fields carrying the inner's values.
 */
void test_a_nested_serial_query_does_not_overwrite_a_browser_querys_fields(void) {
    runInto(&g_outer, CONSOLE_SOURCE_WEB, "system.status.health");
    const int cleanCount = g_outer.count;
    static char cleanLines[kMaxRecords][kRecordTextMax];
    for (int i = 0; i < cleanCount; ++i) {
        snprintf(cleanLines[i], kRecordTextMax, "%s", g_outer.lines[i]);
    }
    TEST_ASSERT_TRUE_MESSAGE(cleanCount > 2, "system.status.health emitted almost nothing");

    captureReset(&g_outer);
    captureReset(&g_inner);
    g_plan.fireAfterOuterRecord = 2;
    g_plan.command = "system.status.wifi";
    g_plan.source = CONSOLE_SOURCE_SERIAL;
    g_plan.fired = 0;

    runInto(&g_outer, CONSOLE_SOURCE_WEB, "system.status.health");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_plan.fired, "the nested serial query never ran");
    TEST_ASSERT_EQUAL_INT_MESSAGE(cleanCount, g_outer.count,
                                  "the interrupted query returned a different number of records");
    char msg[256];
    for (int i = 0; i < cleanCount; ++i) {
        snprintf(msg, sizeof(msg), "record %d differs: clean \"%s\" vs interrupted \"%s\"", i,
                 cleanLines[i], g_outer.lines[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cleanLines[i], g_outer.lines[i], msg);
    }
}

// =============================================================================
// The config-write critical section, from both sources
// =============================================================================

// The last record a capture holds, which for a config write is its result.
static const char* lastRecord(const Capture* c) {
    return c->count > 0 ? c->lines[c->count - 1] : "";
}

/**
 * A browser write while the serial adapter holds the window is refused, and
 * nothing of it reaches the config cache.
 *
 * test_console_module.cpp proves this from the serial source; the guarantee
 * is symmetric and the browser side is the one an operator can trigger from
 * a page they left open, so it is proven rather than assumed.
 */
void test_a_browser_write_is_refused_while_the_other_adapter_holds_the_window(void) {
    struct PaStubMutex* m = paStubMutexStorage();
    m->held = 1;  // the serial adapter, mid-write

    runInto(&g_outer, CONSOLE_SOURCE_WEB, "system.config.enable_arm1 value=true");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "result status=err outcome=unavailable reason=temporarily-unavailable",
        lastRecord(&g_outer), "a contended browser write was not refused");

    ConfigSnapshot after = {};
    configCacheRead(&after);
    TEST_ASSERT_FALSE_MESSAGE(after.system.enable_arm1,
                              "a refused browser write still reached the config cache");
}

/**
 * The same for the grouped WiFi write, which is a second entry into the same
 * window through a different Apply Core (wifiApply/wifiCommitApplied) and had
 * been covered from the serial source only.
 */
void test_a_browser_wifi_write_is_refused_while_the_other_adapter_holds_the_window(void) {
    struct PaStubMutex* m = paStubMutexStorage();
    m->held = 1;

    runInto(&g_outer, CONSOLE_SOURCE_WEB, "wifi.config.settings ap-ssid=protoArtoo-test");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "result status=err outcome=unavailable reason=temporarily-unavailable",
        lastRecord(&g_outer), "a contended browser WiFi write was not refused");
}

/**
 * Writes from the two adapters in turn both land, in order, with no lost
 * update and with the lock handed back cleanly each time. A guard leaked on
 * any exit path would strand the second write on `temporarily-unavailable`
 * forever - a worse failure than the race it closes, and one a single-write
 * test cannot see.
 */
void test_alternating_writes_from_both_adapters_all_apply_with_balanced_locking(void) {
    runInto(&g_outer, CONSOLE_SOURCE_SERIAL, "system.config.enable_arm1 value=true");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("result status=ok outcome=staged-until-reboot",
                                     lastRecord(&g_outer), "the serial write did not apply");

    captureReset(&g_inner);
    runInto(&g_inner, CONSOLE_SOURCE_WEB, "system.config.enable_arm2 value=true");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("result status=ok outcome=staged-until-reboot",
                                     lastRecord(&g_inner), "the browser write did not apply");

    ConfigSnapshot after = {};
    configCacheRead(&after);
    TEST_ASSERT_TRUE_MESSAGE(after.system.enable_arm1, "the serial write was lost");
    TEST_ASSERT_TRUE_MESSAGE(after.system.enable_arm2, "the browser write was lost");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "the config-write window was left held");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->unmatchedGives, "a give with nothing held");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->failedTakes, "an uncontended write still failed to take");
    TEST_ASSERT_EQUAL_INT_MESSAGE(m->takeCount, m->giveCount, "takes and gives are not balanced");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, m->takeCount, "the two writes did not take the window twice");
}

/**
 * The window is scoped to the shared state, not held across the answer.
 *
 * A browser write started from inside a serial write's own result record -
 * the exact instant the first adapter is handing its answer to a transport -
 * succeeds. It has to: on the serial adapter that callback writes to the
 * wire under the serial mutex, so a config window still held there would put
 * two locks in a fixed order around a blocking device write. Widening the
 * guard to cover the sink callback turns this red with
 * `temporarily-unavailable`.
 */
void test_the_write_window_is_released_before_the_answer_is_emitted(void) {
    g_plan.fireAfterOuterRecord = 1;
    g_plan.command = "system.config.enable_arm2 value=true";
    g_plan.source = CONSOLE_SOURCE_WEB;

    runInto(&g_outer, CONSOLE_SOURCE_SERIAL, "system.config.enable_arm1 value=true");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_plan.fired, "the nested browser write never ran");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("result status=ok outcome=staged-until-reboot",
                                     lastRecord(&g_outer), "the serial write did not apply");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "result status=ok outcome=staged-until-reboot", lastRecord(&g_inner),
        "a write started from inside the other adapter's answer was blocked");

    ConfigSnapshot after = {};
    configCacheRead(&after);
    TEST_ASSERT_TRUE_MESSAGE(after.system.enable_arm1, "the outer write was lost");
    TEST_ASSERT_TRUE_MESSAGE(after.system.enable_arm2, "the nested write was lost");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "the config-write window was left held");
    TEST_ASSERT_EQUAL_INT_MESSAGE(m->takeCount, m->giveCount, "takes and gives are not balanced");
}

/**
 * A query interrupting a write, and a write interrupting a query, both leave
 * the window free. Queries never take it (they touch no shared write state),
 * so a query that somehow did would show as an extra take here.
 */
void test_a_query_nested_in_a_write_never_touches_the_write_window(void) {
    g_plan.fireAfterOuterRecord = 1;
    g_plan.command = "system.status.health";
    g_plan.source = CONSOLE_SOURCE_WEB;

    runInto(&g_outer, CONSOLE_SOURCE_SERIAL, "system.config.enable_aux1 value=true");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_plan.fired, "the nested query never ran");
    assertAllRecordsCarryOwnId(&g_inner, "nested browser query");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, m->takeCount, "the query took the config-write window");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "the config-write window was left held");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->unmatchedGives, "a give with nothing held");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_nested_web_query_does_not_change_a_serial_listings_answer);
    RUN_TEST(test_interleaved_responses_stay_attributable_to_their_own_request);
    RUN_TEST(test_a_nested_serial_query_does_not_overwrite_a_browser_querys_fields);
    RUN_TEST(test_a_browser_write_is_refused_while_the_other_adapter_holds_the_window);
    RUN_TEST(test_a_browser_wifi_write_is_refused_while_the_other_adapter_holds_the_window);
    RUN_TEST(test_alternating_writes_from_both_adapters_all_apply_with_balanced_locking);
    RUN_TEST(test_the_write_window_is_released_before_the_answer_is_emitted);
    RUN_TEST(test_a_query_nested_in_a_write_never_touches_the_write_window);
    return UNITY_END();
}
