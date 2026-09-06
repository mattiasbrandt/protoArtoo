// =============================================================================
// test/test_native/test_console_survival/test_console_survival.cpp
//
// Allocation proof for #225's survival set: the Console operations the
// operator can still reach when the heap is too low for HTTP admission -
// system.status.health (heap/uptime/reset-reason/estop/SBUS/WiFi/filesystem
// in one query), system.config.log-level (read), and system.action.reboot.
// Each is driven through consoleExecuteCommand() - the same entry point
// both adapters call - with a warm-then-count operator new/delete override,
// copying test_console_help_reader.cpp's own harness (that file's own
// header comment section 3) rather than designing a new one.
//
// A SEPARATE binary from test_console_module.cpp on purpose (#225's
// coordinator pin): that binary runs hundreds of other cases, and a global
// operator new override there is a needless blast radius. Each
// test_native/<dir> links its own executable, so the override here is
// contained to just these tests.
//
// Scope, read from the source, not assumed:
//
//   - system.status.health is entirely allocation-free on the call path
//     this binary can reach: captureHealthSnapshot() (src/web/
//     api_status_serializers.cpp) copies POD structs under a critical
//     section, networkManagerQueryConnectivity()'s native stub
//     (src/native_test_stubs.cpp) returns an aggregate by value, and
//     resetReasonName() (include/reset_reason.h) returns a static string
//     literal - no String, no JsonDocument, no std::map, anywhere in the
//     chain.
//
//   - system.config.log-level's READ is proven here (configCurrentLogLevel(),
//     src/config_store.cpp, reads a single lock-free static byte). The WRITE
//     is deliberately NOT covered by an allocation count in this file: on
//     the real device a config write commits through ESP-IDF NVS, a flash
//     write with no heap involvement, but the native test double for that
//     step (test/stubs/include/Preferences.h) backs every key with a real
//     std::map<std::string, std::string>, and a std::map insertion always
//     heap-allocates a tree node regardless of key/value size - a property
//     of the TEST DOUBLE, not of the firmware this ticket ships. Measured
//     directly (a throwaway probe, not shipped): warming then writing
//     "system.config.log-level value=4" allocates 174 times on this
//     binary's Preferences stub alone - every field configSave() persists,
//     not something this ticket's own code controls. Counting operator new
//     across that call would report a number this ticket's own production
//     code does not own, exactly the false-signal shape Trap 1 in the
//     coordinator's pin describes for src/tasks/console_task.cpp (a
//     boundary the host cannot see past, named as a residual rather than
//     invented a harness for). The ticket's own outcome text asks for
//     "current log level" as a survival READ, and lists read/write as a
//     separate criterion (3) with no allocation claim attached - this file
//     matches that scope. test/test_native/test_console_module/
//     test_console_module.cpp already proves the write's correctness
//     (value/word forms, range/unknown-argument rejection); it is not
//     re-proven here.
//
//   - system.action.reboot: consoleRejectAnyArgument() (include/
//     console_direct_action_system.h) is a plain loop, requestSystemRestart()'s
//     native stub (src/native_test_stubs.cpp) increments a counter, and
//     requestStatusBroadcastNow()'s native stub does the same - neither
//     touches Preferences/NVS, so this one IS provable end to end.
//
// The residual this binary cannot reach at all: src/tasks/console_task.cpp
// (the serial renderer) is not in [env:native]'s build_src_filter
// (platformio.ini is fenced on this ticket) - allocation-free there by
// construction (a file-scope static char recordBuffer[CONSOLE_RECORD_LINE_MAX],
// a stack char reasonStr[64], snprintf only, no String/new/malloc/
// JsonDocument anywhere in that file), not by a test in this binary.
// =============================================================================
#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "console_catalog.h"
#include "console_module.h"

// -----------------------------------------------------------------------------
// Allocation counter -- global operator new/delete for this test binary only.
// Copied from test_console_help_reader.cpp's own harness (its header
// comment, section 3): each test_native/<dir> links its own executable, so
// this is contained here and does not affect any other test binary.
// -----------------------------------------------------------------------------
static size_t g_allocCount = 0;

void* operator new(size_t size) {
    g_allocCount++;
    void* p = malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](size_t size) {
    g_allocCount++;
    void* p = malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

// -----------------------------------------------------------------------------
// Capturing sink. Fixed storage on purpose (test_console_help_reader.cpp's
// own reasoning): the allocation test counts every operator new during the
// call, so the harness itself must contribute none. Handles all three record
// shapes this file's three operations use - health answers begin/field.../end
// (a query), log-level's read answers begin/field/end too (a config read),
// reboot answers a single result record (an action) - one sink covers every
// case docs/console-protocol.md s.3.1 defines.
// -----------------------------------------------------------------------------
static const int MAX_FIELDS = 16;
static char g_names[MAX_FIELDS][64];
static char g_values[MAX_FIELDS][64];
static int g_fieldCount = 0;
static bool g_beginCalled = false;
static bool g_endCalled = false;
static bool g_resultCalled = false;
static ConsoleStatus g_status;
static ConsoleOutcome g_outcome;
static ConsoleReason g_reason;

static void capBegin(uint32_t, const char*) { g_beginCalled = true; }
static void capField(uint32_t, const char* name, const char* value) {
    if (g_fieldCount >= MAX_FIELDS) return;
    snprintf(g_names[g_fieldCount], sizeof(g_names[0]), "%s", name);
    snprintf(g_values[g_fieldCount], sizeof(g_values[0]), "%s", value);
    g_fieldCount++;
}
static void capEnd(uint32_t, ConsoleStatus s, ConsoleOutcome o, ConsoleReason r) {
    g_endCalled = true;
    g_status = s;
    g_outcome = o;
    g_reason = r;
}
static void capResult(uint32_t, ConsoleStatus s, ConsoleOutcome o, ConsoleReason r) {
    g_resultCalled = true;
    g_status = s;
    g_outcome = o;
    g_reason = r;
}

static ConsoleRecordSink makeSink() {
    ConsoleRecordSink sink = {};
    sink.onRecordBegin = capBegin;
    sink.onRecordField = capField;
    sink.onRecordResult = capResult;
    sink.onRecordEnd = capEnd;
    return sink;
}

static void runQuery(const char* opName) {
    g_fieldCount = 0;
    g_beginCalled = false;
    g_endCalled = false;
    g_resultCalled = false;
    ConsoleRecordSink sink = makeSink();
    ConsoleRequest req = {};
    req.requestId = 1;
    req.source = CONSOLE_SOURCE_SERIAL;
    req.operationName = opName;
    consoleExecuteCommand(&req, &sink);
}

static const char* fieldNamed(const char* name) {
    for (int i = 0; i < g_fieldCount; i++) {
        if (strcmp(g_names[i], name) == 0) return g_values[i];
    }
    return nullptr;
}

void setUp() {
    g_allocCount = 0;
}
void tearDown() {}

// -----------------------------------------------------------------------------
// system.status.health: the whole survival triple (heap free/min/largest
// block) plus uptime and reset reason, in one command.
// -----------------------------------------------------------------------------
void test_health_answers_the_survival_fields() {
    runQuery("system.status.health");

    TEST_ASSERT_TRUE(g_beginCalled);
    TEST_ASSERT_TRUE(g_endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_outcome);
    TEST_ASSERT_NOT_NULL(fieldNamed("heapFree"));
    TEST_ASSERT_NOT_NULL(fieldNamed("heapMin"));
    TEST_ASSERT_NOT_NULL(fieldNamed("heapLargestBlock"));
    TEST_ASSERT_NOT_NULL(fieldNamed("uptimeMs"));
    TEST_ASSERT_NOT_NULL(fieldNamed("resetReason"));
}

void test_health_allocates_nothing() {
    // Warm the path once so any one-time setup is not counted.
    runQuery("system.status.health");

    g_allocCount = 0;
    runQuery("system.status.health");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)g_allocCount,
        "system.status.health allocated - forbidden below the admission floor");
}

// -----------------------------------------------------------------------------
// system.config.log-level: read only (see the file header comment for why
// the write is out of this file's allocation scope).
// -----------------------------------------------------------------------------
void test_log_level_read_answers_the_live_value() {
    runQuery("system.config.log-level");

    TEST_ASSERT_TRUE(g_beginCalled);
    TEST_ASSERT_TRUE(g_endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_outcome);
    TEST_ASSERT_NOT_NULL(fieldNamed("value"));
}

void test_log_level_read_allocates_nothing() {
    runQuery("system.config.log-level");

    g_allocCount = 0;
    runQuery("system.config.log-level");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)g_allocCount,
        "system.config.log-level's read allocated - forbidden below the admission floor");
}

// -----------------------------------------------------------------------------
// system.action.reboot: an action, answered by a single result record - no
// begin/field/end. requestSystemRestart()'s own native stub is the
// observation hook (include/web_server_test_hooks.h).
// -----------------------------------------------------------------------------
void test_reboot_answers_a_single_result_record() {
    runQuery("system.action.reboot");

    TEST_ASSERT_FALSE(g_beginCalled);
    TEST_ASSERT_FALSE(g_endCalled);
    TEST_ASSERT_TRUE(g_resultCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_outcome);
}

void test_reboot_allocates_nothing() {
    runQuery("system.action.reboot");

    g_allocCount = 0;
    runQuery("system.action.reboot");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)g_allocCount,
        "system.action.reboot allocated - forbidden below the admission floor");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_health_answers_the_survival_fields);
    RUN_TEST(test_health_allocates_nothing);
    RUN_TEST(test_log_level_read_answers_the_live_value);
    RUN_TEST(test_log_level_read_allocates_nothing);
    RUN_TEST(test_reboot_answers_a_single_result_record);
    RUN_TEST(test_reboot_allocates_nothing);
    return UNITY_END();
}
