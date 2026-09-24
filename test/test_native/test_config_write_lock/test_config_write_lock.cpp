/**
 * Test: the config write lock is the seam's, not an adapter's (#269, ADR
 * 0011's 2026-09-04 amendment).
 *
 * The race this closes is a lost update, not a corrupted read. Two writers
 * each read the config cache into a working snapshot, each applies its own
 * fields, and the second write-back reverts the first's fields before either
 * reaches NVS - so a write that answered "ok" is silently gone. Until this
 * ticket only the Console module locked, and it locked a mutex of its own, so
 * a dashboard form POST (handleConfigPost) and a serial Console write could
 * interleave exactly that way. #229's acceptance row "browser and serial
 * concurrent requests cannot race config/result state" was false on the
 * merged base; it becomes provable here.
 *
 * WHERE THE PREEMPTION GOES. The dangerous window opens at an adapter's
 * configCacheRead() and closes after configCommitApplied(), so a test that
 * only interleaves two answers proves nothing. The host harness is
 * single-threaded, so the interleaving is placed with the WebRequest test
 * backend's onFirstParamRead hook: a write handler's parameter reads happen
 * between its cache read and its commit, which is exactly where a real
 * scheduler could run the other adapter. The nested write is a complete
 * request through the real module, the real catalog and the same shared
 * config cache.
 *
 * The native FreeRTOS mutex stub models a NON-RECURSIVE mutex over one
 * singleton (test/stubs/include/freertos/semphr.h), so a contended take fails
 * here where a device would block up to the bound and then proceed. That is
 * the stronger case to assert against: the nested writer must report the
 * refusal, never report success and lose its write.
 */

#include <unity.h>

#include <cstring>

#include <freertos/semphr.h>  // paStubMutexReset()/paStubMutexStorage() - the
                              // singleton every xSemaphoreCreateMutexStatic()
                              // returns natively, so a test can inspect the
                              // real take/give accounting of the config write
                              // lock in src/config_write_lock.cpp

#include "api_audio.h"
#include "api_config.h"
#include "api_drive.h"
#include "api_identity.h"
#include "config_cache.h"
#include "console_module.h"
#include "console_record.h"
#include "web_request_test_backend.h"

// =============================================================================
// A minimal Console capture: only the last record matters here, which for a
// config write is its result.
// =============================================================================

static char g_consoleLastRecord[160];
static int g_consoleRecordCount;

static void sinkResult(uint32_t /*requestId*/, ConsoleStatus status, ConsoleOutcome outcome,
                       ConsoleReason reason) {
    snprintf(g_consoleLastRecord, sizeof(g_consoleLastRecord), "result status=%s outcome=%s%s%s",
             consoleStatusString(status), consoleOutcomeString(outcome),
             consoleReasonIsPresent(reason) ? " reason=" : "",
             consoleReasonIsPresent(reason) ? consoleReasonString(reason) : "");
    g_consoleRecordCount++;
}

static void runConsole(ConsoleCommandSource source, const char* command) {
    ConsoleRecordSink sink = {};
    sink.onRecordResult = sinkResult;

    ConsoleRequest req = {};
    req.requestId = consoleGetNextRequestId();
    req.source = source;
    req.operationName = command;

    consoleExecuteCommand(&req, &sink);
}

static const char* kConsoleWriteApplied = "result status=ok outcome=staged-until-reboot";

// The staged preemption for the interleaving test below.
static void nestedSerialConsoleWrite() {
    runConsole(CONSOLE_SOURCE_SERIAL, "system.config.enable_arm1 value=true");
}

static ConfigSnapshot readSnapshot() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    return snap;
}

void setUp(void) {
    ConfigSnapshot snap = {};
    snap.drive.speedLimitMax = 100;
    configCacheApply(snap);
    configCacheSetActiveWifi(snap.wifi);
    configCacheSetActiveWifiRecovery(false);
    consoleModuleInit();  // idempotent

    memset(g_consoleLastRecord, 0, sizeof(g_consoleLastRecord));
    g_consoleRecordCount = 0;
    paStubMutexReset();
}

void tearDown(void) {
    paStubMutexReset();
}

// =============================================================================
// The two-writer case the ticket opens with
// =============================================================================

/**
 * A serial Console write lands in the middle of a dashboard form POST's
 * read-modify-write window. Whichever of the two is refused, the one that
 * ANSWERED must be the one whose fields are in the cache: a write that
 * reported success and is not there is the silent revert.
 *
 * Take the lock out of handleConfigPost and this goes red exactly that way -
 * the Console write takes an uncontended lock, applies enable_arm1, answers
 * "ok", and the POST's write-back of a snapshot read before it puts
 * enable_arm1 straight back to false.
 */
void test_a_console_write_interleaved_into_a_rest_write_is_never_silently_reverted(void) {
    const WebRequestTestParam params[] = {{"speedLimitMax", "80"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    backend.onFirstParamRead = nestedSerialConsoleWrite;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_consoleRecordCount,
                                  "the nested Console write never ran (or ran twice)");

    const ConfigSnapshot after = readSnapshot();
    const bool consoleAnsweredOk = (strcmp(g_consoleLastRecord, kConsoleWriteApplied) == 0);

    TEST_ASSERT_EQUAL_MESSAGE(consoleAnsweredOk, after.system.enable_arm1,
                              "the Console write's answer and the config cache disagree - a write "
                              "that reported success was silently reverted, or one that reported "
                              "busy was applied anyway");

    // The REST write's own answer is held to the same rule.
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, backend.sentCode, "the REST write did not answer 200");
    TEST_ASSERT_EQUAL_INT_MESSAGE(80, after.drive.speedLimitMax,
                                  "the REST write answered 200 and is not in the cache");

    // And with a non-recursive lock the refusal is the Console's, reported.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "result status=err outcome=unavailable reason=temporarily-unavailable",
        g_consoleLastRecord, "the contended Console write was not refused");
}

/**
 * The REST route takes the seam's lock, so a write that arrives while another
 * adapter holds the window is refused with the additive busy body and touches
 * no config state. Without the lock the POST applies straight through and
 * answers 200.
 */
void test_a_rest_config_write_is_refused_while_the_window_is_held(void) {
    struct PaStubMutex* m = paStubMutexStorage();
    m->held = 1;  // another adapter, mid-write

    const WebRequestTestParam params[] = {{"speedLimitMax", "80"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(503, backend.sentCode, "a contended config POST was not refused");
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "config write busy"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(100, readSnapshot().drive.speedLimitMax,
                                  "a refused config POST still reached the config cache");
}

/**
 * The other two REST routes that read-modify-write the same config cache and
 * the same NVS namespace answer the same way.
 */
void test_the_rc_map_and_wifi_routes_are_refused_while_the_window_is_held(void) {
    struct PaStubMutex* m = paStubMutexStorage();
    m->held = 1;

    const WebRequestTestParam mapParams[] = {{"plain", "{\"map\":[]}"}};
    WebRequestTestBackend mapBackend;
    mapBackend.params = mapParams;
    mapBackend.paramCount = 1;
    WebRequest mapReq(&mapBackend);
    handleRcMapPost(mapReq);
    TEST_ASSERT_EQUAL_INT_MESSAGE(503, mapBackend.sentCode, "a contended rc map POST was not refused");
    TEST_ASSERT_NOT_NULL(strstr(mapBackend.sentBody, "config write busy"));

    const WebRequestTestParam wifiParams[] = {{"ap_ssid", "protoArtoo-test"}};
    WebRequestTestBackend wifiBackend;
    wifiBackend.params = wifiParams;
    wifiBackend.paramCount = 1;
    WebRequest wifiReq(&wifiBackend);
    handleWifiPost(wifiReq);
    TEST_ASSERT_EQUAL_INT_MESSAGE(503, wifiBackend.sentCode, "a contended wifi POST was not refused");
    TEST_ASSERT_NOT_NULL(strstr(wifiBackend.sentBody, "config write busy"));

    WifiConfig staged = {};
    configCacheReadWifi(&staged);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", staged.ap_ssid,
                                     "a refused wifi POST still reached the config cache");
}

/**
 * Uncontended writes from the two adapters in turn both land, and the window
 * is handed back cleanly each time. The take count is what pins the REST
 * route to the SAME lock the Console takes: a route that does not lock leaves
 * one take here instead of two, and a guard leaked on any exit path would
 * strand every later write on busy forever.
 */
void test_alternating_rest_and_console_writes_both_land_with_balanced_locking(void) {
    const WebRequestTestParam params[] = {{"speedLimitMax", "80"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleConfigPost(req);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, backend.sentCode, "the REST write did not apply");

    runConsole(CONSOLE_SOURCE_WEB, "system.config.enable_arm2 value=true");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kConsoleWriteApplied, g_consoleLastRecord,
                                     "the Console write did not apply");

    const ConfigSnapshot after = readSnapshot();
    TEST_ASSERT_EQUAL_INT_MESSAGE(80, after.drive.speedLimitMax, "the REST write was lost");
    TEST_ASSERT_TRUE_MESSAGE(after.system.enable_arm2, "the Console write was lost");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, m->takeCount,
                                  "the two writes did not take the same window twice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "the config write window was left held");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->unmatchedGives, "a give with nothing held");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->failedTakes, "an uncontended write still failed to take");
    TEST_ASSERT_EQUAL_INT_MESSAGE(m->takeCount, m->giveCount, "takes and gives are not balanced");
}

// =============================================================================
// Core 1's two fields, and the Core 0 writers that used to skip the lock (#417)
// =============================================================================

// RCInputTask sets the speed preset and stationary on Core 1, by field and
// without the lock - it must never block there. Staged at the point a real
// scheduler could run it: after the POST read the cache, before it commits.
static void rcLandsMidPost() {
    configCacheSelectSpeedPreset(SpeedPresetId::Turbo);
    configCacheSetStationary(true);
}

static void seedSpeedPresets() {
    ConfigSnapshot snap = readSnapshot();
    snap.drive.speedPresetSlow = 150;
    snap.drive.speedPresetNormal = 300;
    snap.drive.speedPresetTurbo = 600;
    snap.drive.speedPresetActive = SpeedPresetId::Normal;
    snap.drive.speedLimitMax = 300;
    snap.system.stationary = false;
    configCacheApply(snap);
}

/**
 * A config POST replaces the whole snapshot from the copy it read, and RC
 * input cannot take the lock the POST holds. Before the Commit Step kept the
 * live values, a POST about something else put the old speed limit and the
 * old mode straight back - the droid kept driving at Normal after the stick
 * said Turbo. What the request did state still wins.
 */
void test_an_rc_change_landing_mid_post_is_not_reverted(void) {
    seedSpeedPresets();

    const WebRequestTestParam unrelated[] = {{"webDriveTimeoutMs", "750"}};
    WebRequestTestBackend backend;
    backend.params = unrelated;
    backend.paramCount = 1;
    backend.onFirstParamRead = rcLandsMidPost;
    WebRequest req(&backend);
    handleConfigPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    ConfigSnapshot after = readSnapshot();
    TEST_ASSERT_EQUAL_UINT32(750, after.drive.webDriveTimeoutMs);
    TEST_ASSERT_EQUAL_INT_MESSAGE(600, after.drive.speedLimitMax, "the RC preset was reverted");
    TEST_ASSERT_EQUAL_UINT8((uint8_t)SpeedPresetId::Turbo, (uint8_t)after.drive.speedPresetActive);
    TEST_ASSERT_TRUE_MESSAGE(after.system.stationary, "the RC stationary toggle was reverted");

    seedSpeedPresets();
    const WebRequestTestParam stated[] = {{"speedLimitMax", "80"}, {"stationary", "false"}};
    WebRequestTestBackend statedBackend;
    statedBackend.params = stated;
    statedBackend.paramCount = 2;
    statedBackend.onFirstParamRead = rcLandsMidPost;
    WebRequest statedReq(&statedBackend);
    handleConfigPost(statedReq);

    TEST_ASSERT_EQUAL_INT(200, statedBackend.sentCode);
    after = readSnapshot();
    TEST_ASSERT_EQUAL_INT(80, after.drive.speedLimitMax);
    TEST_ASSERT_FALSE(after.system.stationary);
}

static int postWhileHeld(void (*handler)(WebRequest&), const WebRequestTestParam* params,
                         size_t count) {
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = count;
    WebRequest req(&backend);
    handler(req);
    return backend.sentCode;
}

/**
 * Every other REST route that writes the config cache or saves it whole takes
 * the same window, so it is refused rather than run beside a write that holds
 * it. Each of these used to run straight through.
 */
void test_the_other_rest_config_writers_wait_for_the_window(void) {
    configCacheSetActiveAudioEnabled(true);
    struct PaStubMutex* m = paStubMutexStorage();
    m->held = 1;

    const WebRequestTestParam volume[] = {{"action", "volume"}, {"level", "12"}};
    TEST_ASSERT_EQUAL_INT_MESSAGE(503, postWhileHeld(handleAudioPost, volume, 2), "volume");
    const WebRequestTestParam moodMap[] = {
        {"quiet", "1"}, {"mid", "2"}, {"full", "3"}, {"awakeplus", "4"}};
    TEST_ASSERT_EQUAL_INT_MESSAGE(503, postWhileHeld(handleAudioMoodMapPost, moodMap, 4),
                                  "mood map");
    const WebRequestTestParam tracks[] = {{"key", "scream"}, {"track", "3"}};
    TEST_ASSERT_EQUAL_INT_MESSAGE(503, postWhileHeld(handleAudioTracksPost, tracks, 2), "tracks");
    const WebRequestTestParam range[] = {
        {"lo_key", "snd_cat_gen_lo"}, {"hi_key", "snd_cat_gen_hi"}, {"lo", "10"}, {"hi", "20"}};
    TEST_ASSERT_EQUAL_INT_MESSAGE(503, postWhileHeld(handleAudioCategoryRangePost, range, 4),
                                  "category range");
    const WebRequestTestParam identity[] = {{"droidName", "artoo"}};
    TEST_ASSERT_EQUAL_INT_MESSAGE(503, postWhileHeld(handleIdentityPost, identity, 1), "identity");
    // The mode is a state the droid is already in, so it is applied and the
    // save is what is refused - answered as a save that did not happen.
    const WebRequestTestParam mode[] = {{"mode", "stationary"}};
    TEST_ASSERT_EQUAL_INT_MESSAGE(500, postWhileHeld(handleModePost, mode, 1), "mode");

    const ConfigSnapshot after = readSnapshot();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, after.audio.audioVolume,
                                    "a refused volume write still reached the config cache");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, after.audio.snd_scream,
                                     "a refused tracks write still reached the config cache");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, after.audio.snd_cat_gen_lo,
                                     "a refused category range still reached the config cache");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->takeCount, "a refused writer took the window anyway");
    configCacheSetActiveAudioEnabled(false);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_console_write_interleaved_into_a_rest_write_is_never_silently_reverted);
    RUN_TEST(test_a_rest_config_write_is_refused_while_the_window_is_held);
    RUN_TEST(test_the_rc_map_and_wifi_routes_are_refused_while_the_window_is_held);
    RUN_TEST(test_alternating_rest_and_console_writes_both_land_with_balanced_locking);
    RUN_TEST(test_an_rc_change_landing_mid_post_is_not_reverted);
    RUN_TEST(test_the_other_rest_config_writers_wait_for_the_window);
    return UNITY_END();
}
