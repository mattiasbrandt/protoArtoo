// =============================================================================
// test/test_native/test_console_help_reader/test_console_help_reader.cpp
//
// Native tests for the Console help path through the injected reader
// (ADR 0036). The module reads help text on demand via a caller-supplied
// seek/read pair, which is what makes these states reachable off-device at
// all -- a LittleFS File opened in setup() is not.
//
// Four things are covered here that a catalog-only test cannot reach:
//
//   1. The read is ADDRESSED, not scanned: exactly one seek, to the catalog
//      entry's own help_offset, and one read of its help_length.
//   2. Every failure state degrades EXPLICITLY. A missing, stale, truncated or
//      unreadable help file must still produce a stable, readable answer --
//      "the description is simply missing" is the defect, not the fallback.
//      The stale case is the quiet one: a firmware-only update leaves the
//      compiled offsets addressing an older file, so the read SUCCEEDS and
//      returns another operation's row (#281).
//   3. The rows the real catalog actually holds fit the read. Three rows in
//      data/console_help.txt are longer than the 512-byte buffer this module
//      used to read into, and their description and executor delimiters fell
//      past the cut, so those fields were absent from an otherwise successful
//      answer (#282). Those tests read data/console_help.txt itself: the row
//      lengths are the defect, so a fixture cannot stand in for them.
//   4. The request path performs NO dynamic allocation. The Console task runs
//      on Core 0 and AGENTS.md forbids allocation in task loops after setup();
//      a comment promising it is not evidence, so this counts real operator new
//      calls across the call.
// =============================================================================
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

#include "console_catalog.h"
#include "console_module.h"

// -----------------------------------------------------------------------------
// Allocation counter -- global operator new/delete for this test binary only.
// Each test_native/<dir> links its own executable, so this is contained here.
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
// Memory-backed help reader. Records what it was asked to do so the test can
// assert the ACCESS PATTERN, not just the returned bytes.
// -----------------------------------------------------------------------------
struct FakeHelpFile {
    const char* line;      // what a read returns
    uint32_t lastOffset;   // offset of the most recent seek
    int seekCalls;
    int readCalls;
    bool seekResult;       // false simulates a seek past EOF / unreadable file
    bool readReturnsZero;  // true simulates a truncated or unreadable file
};

static bool fakeSeek(void* ctx, uint32_t offset) {
    FakeHelpFile* f = (FakeHelpFile*)ctx;
    f->seekCalls++;
    f->lastOffset = offset;
    return f->seekResult;
}

static size_t fakeRead(void* ctx, char* out, size_t len) {
    FakeHelpFile* f = (FakeHelpFile*)ctx;
    f->readCalls++;
    if (f->readReturnsZero) return 0;
    size_t n = strlen(f->line);
    if (n > len) n = len;
    memcpy(out, f->line, n);
    return n;
}

static FakeHelpFile g_file;
static ConsoleHelpReader g_reader;

// -----------------------------------------------------------------------------
// File-backed help reader over the REAL data/console_help.txt.
//
// The three rows that #282 lost fields on are real rows -- 561, 562 and 585
// bytes, with the delimiter closing their executor at 560, 561 and 524 -- and a
// hand-written fixture is exactly what would not have caught that: the row
// lengths ARE the defect. This reader is the same shape as the LittleFS one in
// setup() (seek to the catalog's compiled offset, read help_length bytes), so
// what it exercises is the read the firmware actually performs.
// -----------------------------------------------------------------------------
static FILE* g_realFile = nullptr;
static ConsoleHelpReader g_realReader;

static bool realSeek(void* ctx, uint32_t offset) {
    FILE* f = (FILE*)ctx;
    return f != nullptr && fseek(f, (long)offset, SEEK_SET) == 0;
}

static size_t realRead(void* ctx, char* out, size_t len) {
    FILE* f = (FILE*)ctx;
    if (f == nullptr || out == nullptr || len == 0) return 0;
    return fread(out, 1, len, f);
}

// The test binary's working directory is the project root under `pio test`,
// but it is not worth depending on: try the plausible roots and FAIL loudly if
// the catalog is not among them. A test that quietly skips itself when it
// cannot find its input is the failure this whole suite is about.
static void useRealHelpFile() {
    static const char* candidates[] = {
        "data/console_help.txt",
        "../data/console_help.txt",
        "../../data/console_help.txt",
        "../../../data/console_help.txt",
        "../../../../data/console_help.txt",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        g_realFile = fopen(candidates[i], "rb");
        if (g_realFile != nullptr) break;
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(g_realFile,
        "data/console_help.txt not found from the test working directory");
    g_realReader.seek = realSeek;
    g_realReader.read = realRead;
    g_realReader.ctx = g_realFile;
    consoleModuleSetHelpReader(&g_realReader);
}

static void closeRealHelpFile() {
    if (g_realFile != nullptr) {
        fclose(g_realFile);
        g_realFile = nullptr;
    }
}

static void useReader(const char* line, bool seekOk, bool readZero) {
    g_file = {};
    g_file.line = line;
    g_file.seekResult = seekOk;
    g_file.readReturnsZero = readZero;
    g_reader.seek = fakeSeek;
    g_reader.read = fakeRead;
    g_reader.ctx = &g_file;
    consoleModuleSetHelpReader(&g_reader);
}

// -----------------------------------------------------------------------------
// Capturing sink. Fixed storage on purpose: the allocation test counts every
// operator new during the call, so the harness must not add any of its own.
// -----------------------------------------------------------------------------
static const int MAX_FIELDS = 16;
static char g_names[MAX_FIELDS][64];
static char g_values[MAX_FIELDS][256];
static int g_fieldCount = 0;
static bool g_ended = false;
static ConsoleOutcome g_endOutcome;
static ConsoleStatus g_endStatus;

static void capBegin(uint32_t, const char*) {}
static void capField(uint32_t, const char* name, const char* value) {
    if (g_fieldCount >= MAX_FIELDS) return;
    snprintf(g_names[g_fieldCount], sizeof(g_names[0]), "%s", name);
    snprintf(g_values[g_fieldCount], sizeof(g_values[0]), "%s", value);
    g_fieldCount++;
}
static void capEnd(uint32_t, ConsoleStatus s, ConsoleOutcome o, ConsoleReason) {
    g_ended = true;
    g_endStatus = s;
    g_endOutcome = o;
}

static ConsoleRecordSink makeSink() {
    ConsoleRecordSink sink = {};
    sink.onRecordBegin = capBegin;
    sink.onRecordField = capField;
    sink.onRecordEnd = capEnd;
    return sink;
}

static void runHelp(const char* opName) {
    g_fieldCount = 0;
    g_ended = false;
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
    consoleModuleSetHelpReader(nullptr);
    g_allocCount = 0;
}
void tearDown() {
    consoleModuleSetHelpReader(nullptr);
    closeRealHelpFile();
}

// -----------------------------------------------------------------------------
// 1. The read is addressed
// -----------------------------------------------------------------------------
void test_help_read_is_addressed_to_the_entry_offset() {
    const ConsoleCatalogEntry* e = consoleCatalogFindByName("drive.action.move");
    TEST_ASSERT_NOT_NULL(e);

    useReader("drive.action.move|Move|Set drive speed and steering|driveArbiterSubmit|", true, false);
    runHelp("help drive.action.move");

    // Exactly one seek and one read: an addressed lookup, not a file scan.
    TEST_ASSERT_EQUAL_INT(1, g_file.seekCalls);
    TEST_ASSERT_EQUAL_INT(1, g_file.readCalls);
    // And it sought to THIS entry's offset, which is what makes it addressed.
    TEST_ASSERT_EQUAL_UINT32(e->help_offset, g_file.lastOffset);
}

void test_help_fields_come_from_the_reader_bytes() {
    useReader("drive.action.move|Move|Set drive speed and steering|driveArbiterSubmit|", true, false);
    runHelp("help drive.action.move");

    TEST_ASSERT_EQUAL_STRING("Move", fieldNamed("display_name"));
    TEST_ASSERT_EQUAL_STRING("Set drive speed and steering", fieldNamed("description"));
    TEST_ASSERT_EQUAL_STRING("driveArbiterSubmit", fieldNamed("executor"));
}

// -----------------------------------------------------------------------------
// 2. Every failure state degrades explicitly
// -----------------------------------------------------------------------------
void test_absent_reader_degrades_explicitly() {
    consoleModuleSetHelpReader(nullptr);
    runHelp("help drive.action.move");

    TEST_ASSERT_TRUE(g_ended);
    const char* status = fieldNamed("help_file_status");
    TEST_ASSERT_NOT_NULL_MESSAGE(status, "no help_file_status emitted with a NULL reader");
    TEST_ASSERT_EQUAL_STRING("unavailable", status);
}

void test_failing_seek_degrades_explicitly() {
    // A stale or truncated help file: the reader exists, but the entry's offset
    // is past the end. The operator must be told the text is missing, not handed
    // a help answer with a silently absent description.
    useReader("irrelevant", /*seekOk=*/false, /*readZero=*/false);
    runHelp("help drive.action.move");

    TEST_ASSERT_TRUE(g_ended);
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("description"),
                             "a failed seek must not yield a description");
    TEST_ASSERT_NOT_NULL_MESSAGE(fieldNamed("help_file_status"),
        "a failed seek degraded SILENTLY: no help_file_status field was emitted");
}

void test_unreadable_file_degrades_explicitly() {
    // The reader is present and seeks fine, but returns no bytes.
    useReader("irrelevant", /*seekOk=*/true, /*readZero=*/true);
    runHelp("help drive.action.move");

    TEST_ASSERT_TRUE(g_ended);
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("description"),
                             "a zero-byte read must not yield a description");
    TEST_ASSERT_NOT_NULL_MESSAGE(fieldNamed("help_file_status"),
        "an unreadable help file degraded SILENTLY: no help_file_status field was emitted");
}

void test_row_for_another_operation_is_reported_unreadable() {
    // The stale help file after a firmware-only update: `make ota` ships the
    // image and `make uploadfs` is a separate step, so the catalog's compiled
    // offsets address a file whose rows have shifted (#243 inserted one row and
    // moved roughly a hundred). Seek and read both succeed and return a
    // perfectly well-formed row -- for the wrong operation. Field 0 is the only
    // thing that tells the two apart. The row below is a real one, copied
    // verbatim from data/console_help.txt, so the reader returns exactly the
    // shape a shifted offset would land on.
    useReader("dome.action.droid-sequence-scream|Scream|SE01 - scream audio and body "
              "sequence, then forward :SE01 to dome|sequenceStart|", true, false);
    runHelp("help drive.action.move");

    TEST_ASSERT_TRUE(g_ended);
    const char* status = fieldNamed("help_file_status");
    TEST_ASSERT_NOT_NULL_MESSAGE(status,
        "a row belonging to another operation degraded SILENTLY: no help_file_status");
    TEST_ASSERT_EQUAL_STRING("unreadable", status);

    // No partial prose: a record carrying both a degradation status and another
    // operation's description would look authoritative and be wrong
    // (docs/console-protocol.md s.3.4).
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("description"),
                             "another operation's description must not be emitted");
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("display_name"),
                             "another operation's display_name must not be emitted");
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("executor"),
                             "another operation's executor must not be emitted");
}

void test_matching_row_emits_no_help_file_status() {
    // The happy path is the other half of the same guarantee: help_file_status is
    // present exactly when the prose could not be retrieved in full.
    useReader("drive.action.move|Move|Set drive speed and steering|driveArbiterSubmit|", true, false);
    runHelp("help drive.action.move");

    TEST_ASSERT_EQUAL_STRING("Set drive speed and steering", fieldNamed("description"));
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("help_file_status"),
        "a matching row is not a degradation and must carry no help_file_status");
}

void test_alias_help_matches_on_the_canonical_name() {
    // `help drive_speed` resolves through the RC token alias, but the file's
    // field 0 is always the canonical name. The row check compares against the
    // catalog entry, so an alias lookup is a match, not a stale-file report.
    useReader("drive.action.speed|Speed|Forward/reverse drive speed (analog axis binding)|"
              "driveArbiterSubmit|", true, false);
    runHelp("help drive_speed");

    TEST_ASSERT_EQUAL_STRING("Forward/reverse drive speed (analog axis binding)",
                             fieldNamed("description"));
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("help_file_status"),
        "an alias resolves to the same row: help via an alias must not report a stale file");
}

// -----------------------------------------------------------------------------
// 2b. The rows that do not fit a naive read buffer (#282)
// -----------------------------------------------------------------------------
// Driven from the real data/console_help.txt, not a fixture: the defect was the
// length of three real rows against a 512-byte read buffer, so a fixture with
// convenient lengths is precisely the test that would have passed while the
// board lost fields. `help sound.config.mood-category-map` is the row that
// pins the boundary rather than merely crossing it - its description delimiter
// sits at 506 and its executor delimiter at 524, so a 512-byte buffer returned
// the description and dropped the executor, with `status=ok outcome=completed`
// and nothing to say a field was missing.
static void assertRealRowIsWholeAndClamped(const char* opName, const char* executor) {
    useRealHelpFile();
    runHelp(opName);

    TEST_ASSERT_TRUE(g_ended);
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("help_file_status"),
        "an over-long catalog row reported a degradation: its prose did not fit the read buffer");

    const char* description = fieldNamed("description");
    TEST_ASSERT_NOT_NULL_MESSAGE(description,
        "an over-long catalog row lost its description SILENTLY");
    // All three rows carry a description longer than the module's 256-byte
    // field buffer, so the emitted value is the clamped 255 - the same value
    // the browser adapter returns. Clamping is the documented behaviour;
    // vanishing is not.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(255, (uint32_t)strlen(description),
        "the description was not the clamped 255 bytes the browser adapter returns");

    const char* got = fieldNamed("executor");
    TEST_ASSERT_NOT_NULL_MESSAGE(got, "an over-long catalog row lost its executor SILENTLY");
    TEST_ASSERT_EQUAL_STRING(executor, got);
}

void test_over_long_row_keeps_description_and_executor() {
    // 561 bytes; both prose delimiters (546, 560) fell past a 512-byte read.
    assertRealRowIsWholeAndClamped("help dome.action.dome-sequence", "sequenceStart");
}

void test_over_long_row_keeps_executor_when_only_it_overflowed() {
    // 585 bytes; description delimiter at 506 (inside a 512-byte read),
    // executor delimiter at 524 (outside it). The asymmetric case.
    assertRealRowIsWholeAndClamped("help sound.config.mood-category-map", "audioMoodMapApply");
}

void test_over_long_row_with_no_params_tail_is_whole() {
    // 562 bytes; prose delimiters at 534 and 561, so the row is almost all
    // prose and needs nearly the whole buffer.
    assertRealRowIsWholeAndClamped("help system.action.reboot-wifi-module",
                                   "hostedLinkResetCoprocessor");
}

void test_clamped_description_is_marked_truncated() {
    // Ten of the 194 catalog descriptions are longer than the module's
    // 256-byte field buffer, and this is the longest at 506 bytes. The value
    // is clamped - the board has no static RAM to spend on carrying it whole -
    // so the operator gets a sentence that stops mid-word, and before this the
    // record said nothing about that at all.
    useRealHelpFile();
    runHelp("help dome.action.dome-sequence");

    const char* description = fieldNamed("description");
    TEST_ASSERT_NOT_NULL(description);
    TEST_ASSERT_EQUAL_UINT32(255, (uint32_t)strlen(description));

    const char* marker = fieldNamed("description_truncated");
    TEST_ASSERT_NOT_NULL_MESSAGE(marker,
        "a clamped description was emitted with nothing to say it had been cut");
    TEST_ASSERT_EQUAL_STRING("true", marker);

    // The marker is not the degradation status: the prose IS here, so
    // help_file_status must stay absent (docs/console-protocol.md s.3.4).
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("help_file_status"),
        "a clamped field is not an unreadable help file");
}

void test_whole_description_carries_no_truncation_marker() {
    // The other half of the same guarantee: the marker is present exactly when
    // something was cut, so its absence is meaningful.
    useReader("drive.action.move|Move|Set drive speed and steering|driveArbiterSubmit|", true,
              false);
    runHelp("help drive.action.move");

    TEST_ASSERT_EQUAL_STRING("Set drive speed and steering", fieldNamed("description"));
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("description_truncated"),
        "a whole description must not be marked truncated");
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("display_name_truncated"),
        "a whole display_name must not be marked truncated");
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("executor_truncated"),
        "a whole executor must not be marked truncated");
}

void test_row_cut_before_its_executor_degrades_explicitly() {
    // What a read that does not reach the executor's closing delimiter leaves
    // behind - a row longer than the read buffer, or a help file that ends
    // mid-row. The parser emits a field only when it reaches the `|` that
    // closes it, so before #282 this answered `status=ok outcome=completed`
    // with display_name, description and executor simply absent.
    useReader("drive.action.move|Move|Set drive speed and steer", true, false);
    runHelp("help drive.action.move");

    TEST_ASSERT_TRUE(g_ended);
    const char* status = fieldNamed("help_file_status");
    TEST_ASSERT_NOT_NULL_MESSAGE(status,
        "a row cut before its executor delimiter degraded SILENTLY: no help_file_status");
    TEST_ASSERT_EQUAL_STRING("unreadable", status);

    TEST_ASSERT_NULL_MESSAGE(fieldNamed("display_name"),
                             "a partial row must not yield a display_name");
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("description"),
                             "a partial row must not yield a description");
    TEST_ASSERT_NULL_MESSAGE(fieldNamed("executor"),
                             "a partial row must not yield an executor");
}

// -----------------------------------------------------------------------------
// 3. No dynamic allocation on the request path
// -----------------------------------------------------------------------------
void test_help_request_allocates_nothing() {
    useReader("drive.action.move|Move|Set drive speed and steering|driveArbiterSubmit|", true, false);

    // Warm the path once so any one-time setup is not counted.
    runHelp("help drive.action.move");

    g_allocCount = 0;
    runHelp("help drive.action.move");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)g_allocCount,
        "the help request path allocated -- forbidden in the Console task loop");
}

void test_status_query_allocates_nothing() {
    useReader("x|y|z|w|", true, false);
    runHelp("system.status.health");

    g_allocCount = 0;
    runHelp("system.status.health");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)g_allocCount,
        "the status query path allocated -- forbidden in the Console task loop");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_help_read_is_addressed_to_the_entry_offset);
    RUN_TEST(test_help_fields_come_from_the_reader_bytes);
    RUN_TEST(test_absent_reader_degrades_explicitly);
    RUN_TEST(test_failing_seek_degrades_explicitly);
    RUN_TEST(test_unreadable_file_degrades_explicitly);
    RUN_TEST(test_row_for_another_operation_is_reported_unreadable);
    RUN_TEST(test_matching_row_emits_no_help_file_status);
    RUN_TEST(test_alias_help_matches_on_the_canonical_name);
    RUN_TEST(test_over_long_row_keeps_description_and_executor);
    RUN_TEST(test_over_long_row_keeps_executor_when_only_it_overflowed);
    RUN_TEST(test_over_long_row_with_no_params_tail_is_whole);
    RUN_TEST(test_clamped_description_is_marked_truncated);
    RUN_TEST(test_whole_description_carries_no_truncation_marker);
    RUN_TEST(test_row_cut_before_its_executor_degrades_explicitly);
    RUN_TEST(test_help_request_allocates_nothing);
    RUN_TEST(test_status_query_allocates_nothing);
    return UNITY_END();
}
