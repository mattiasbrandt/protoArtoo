// =============================================================================
// test/test_native/test_api_motion_routes/test_api_motion_routes.cpp
//
// Native unit tests for the drive, servo, dome, estop and aux-LED handlers
// through the WebRequest seam's host-test backend (ADR 0021). Each handler is
// driven exactly as a device backend would drive it, and the assertions are on
// the captured response -- no vendor web-server type appears anywhere here,
// which is the property the port exists to establish.
//
// What is deliberately not covered: the queue-full branches, because the host
// freertos stub's xQueueSend() always succeeds, and anything that needs a real
// ServoTask/DomeTask. Those stay device behaviour.
// =============================================================================
#include <unity.h>

#include <cstring>

#include "api_aux_led.h"
#include "api_dome.h"
#include "api_drive.h"
#include "api_estop.h"
#include "api_servo.h"
#include "config_store.h"
#include "dome_link.h"
#include "dome_link_transport.h"
#include "drive_arbiter.h"
#include "failsafe_gate.h"
#include "robot_state.h"
#include "web_admission.h"
#include "web_request_test_backend.h"

// Recorded side effects from src/native_test_stubs.cpp.
extern bool g_test_commanded_stationary;
extern bool g_test_commanded_web_control;
extern unsigned g_test_web_control_calls;
extern unsigned g_test_status_broadcast_count;
extern unsigned g_test_marcduino_calls;
extern unsigned g_test_applied_mood;
extern unsigned g_test_restart_requests;
extern bool g_test_speed_preset_persist_ok;
extern bool g_test_aux_led_queue_ok;
extern DomeLayoutCacheStatus g_test_dome_layout_status;
extern const char* g_test_dome_layout_payload;
extern unsigned g_test_dome_layout_refresh_requests;
extern unsigned long g_test_millis;

namespace {

// A drive command reaches the motors only through the arbiter, so "did the
// handler submit, and with what" is the whole of what the web layer decides.
// Resolved with the same config DriveTask would use, so the value asserted on
// is the one the control loop would actually apply.
DriveOutput resolvedDriveOutput() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    DriveArbiterConfig cfg = {};
    cfg.speedLimitMax = snap.drive.speedLimitMax;
    cfg.webDriveTimeoutMs = 60000;
    return driveArbiterResolve(cfg, millis());
}

bool estopIsLatched() {
    return failsafeIsActive() && failsafeActiveReason() == FailsafeLayer::ESTOP;
}

void setDriveConfig(int16_t speedLimitMax) {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.drive.speedLimitMax = speedLimitMax;
    configCacheApply(snap);
}

void setDomeEnabled(bool enabled) {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.system.enable_dome = enabled;
    configCacheApply(snap);
}

}  // namespace

void setUp() {
    // Non-zero: the arbiter reads timestamp 0 as "never submitted", so a
    // frozen zero clock would make every handler submission invisible.
    g_test_millis = 1000;

    robotState = {};
    failsafeInit(&robotStateMux);
    driveArbiterInit(&robotStateMux);
    driveArbiterReset();
    setDriveConfig(300);
    setDomeEnabled(true);

    g_test_commanded_stationary = false;
    g_test_commanded_web_control = false;
    g_test_web_control_calls = 0;
    g_test_status_broadcast_count = 0;
    g_test_marcduino_calls = 0;
    g_test_applied_mood = 0;
    g_test_restart_requests = 0;
    g_test_speed_preset_persist_ok = true;
    g_test_aux_led_queue_ok = true;
    g_test_dome_layout_status = {};
    g_test_dome_layout_payload = "";
    g_test_dome_layout_refresh_requests = 0;
}

void tearDown() {
}

// -----------------------------------------------------------------------------
// Estop -- the safety path, and the reason this group was ported first
// -----------------------------------------------------------------------------

void test_estop_post_latches_and_stays_latched() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleEstopPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", backend.sentBody);
    TEST_ASSERT_TRUE(estopIsLatched());

    // Latching means exactly this: nothing short of an explicit clear releases
    // it, so an ordinary layer clear must not.
    failsafeClear(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(estopIsLatched());
}

void test_estop_clear_releases_the_latch() {
    WebRequestTestBackend latchBackend;
    WebRequest latchReq(&latchBackend);
    handleEstopPost(latchReq);
    TEST_ASSERT_TRUE(estopIsLatched());

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleEstopClearPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", backend.sentBody);
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_both_estop_paths_are_exempt_from_admission() {
    // The registered paths, checked against the classifier that gates them.
    // A route renamed on one side of that pair would silently lose the bypass.
    TEST_ASSERT_TRUE(webPathIsEstop("/api/estop"));
    TEST_ASSERT_TRUE(webPathIsEstop("/api/estop/clear"));

    WebRequestAdmissionInputs in = {};
    in.estop = true;
    in.inflightRequests = 99;
    in.maxInflightRequests = 1;
    in.largestFreeBlock = 0;
    in.minLargestFreeBlock = 9000;
    in.minLargestFreeBlockDiagnostic = 7500;

    // Over the in-flight cap and under the heap floor at once: still admitted.
    TEST_ASSERT_TRUE(webRequestAdmissionDecide(in) == WebRequestAdmission::kAdmit);
}

void test_dedicated_estop_clear_handler_broadcasts_status() {
    WebRequestTestBackend latchBackend;
    WebRequest latchReq(&latchBackend);
    handleEstopPost(latchReq);
    TEST_ASSERT_TRUE(estopIsLatched());
    g_test_status_broadcast_count = 0;

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleEstopClearPost(req);

    TEST_ASSERT_FALSE(failsafeIsActive());
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
}

void test_manual_command_clear_estop_clears_and_broadcasts() {
    WebRequestTestBackend latchBackend;
    WebRequest latchReq(&latchBackend);
    handleEstopPost(latchReq);
    TEST_ASSERT_TRUE(estopIsLatched());
    g_test_status_broadcast_count = 0;

    bool success = executeManualCommand("clear_estop");

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_FALSE(failsafeIsActive());
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
}

void test_both_clear_paths_are_behaviourally_identical() {
    // Latch via POST /api/estop
    WebRequestTestBackend latchBackend;
    WebRequest latchReq(&latchBackend);
    handleEstopPost(latchReq);
    TEST_ASSERT_TRUE(estopIsLatched());

    // Clear via /api/estop/clear and record broadcast count
    g_test_status_broadcast_count = 0;
    WebRequestTestBackend clearBackend1;
    WebRequest clearReq1(&clearBackend1);
    handleEstopClearPost(clearReq1);
    const unsigned broadcastsViaHandler = g_test_status_broadcast_count;
    TEST_ASSERT_EQUAL_UINT(1, broadcastsViaHandler);

    // Latch again for the manual-command test
    handleEstopPost(latchReq);
    TEST_ASSERT_TRUE(estopIsLatched());

    // Clear via manual-command and record broadcast count
    g_test_status_broadcast_count = 0;
    executeManualCommand("clear_estop");
    const unsigned broadcastsViaManualCommand = g_test_status_broadcast_count;

    // Both paths must broadcast exactly once on clear
    TEST_ASSERT_EQUAL_UINT(broadcastsViaHandler, broadcastsViaManualCommand);
    TEST_ASSERT_EQUAL_UINT(1, broadcastsViaManualCommand);
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_manual_command_clear_estop_is_case_insensitive() {
    WebRequestTestBackend latchBackend;
    WebRequest latchReq(&latchBackend);
    handleEstopPost(latchReq);
    TEST_ASSERT_TRUE(estopIsLatched());

    bool success = executeManualCommand("CLeAr_EsToP");

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_only_explicit_clear_estop_can_clear_the_latch() {
    WebRequestTestBackend latchBackend;
    WebRequest latchReq(&latchBackend);
    handleEstopPost(latchReq);
    TEST_ASSERT_TRUE(estopIsLatched());

    // Verify that other commands do not clear the latch
    bool disableResult = executeManualCommand("disable_web_control");
    TEST_ASSERT_TRUE(disableResult);
    TEST_ASSERT_TRUE(estopIsLatched());

    // Mode commands should not clear it either
    bool stationaryResult = executeManualCommand("#st");
    TEST_ASSERT_TRUE(stationaryResult);
    TEST_ASSERT_TRUE(estopIsLatched());

    // Only explicit clear_estop clears it
    bool clearResult = executeManualCommand("clear_estop");
    TEST_ASSERT_TRUE(clearResult);
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_drive_oversized_speed_is_rejected() {
    robotState.webControlEnabled = true;

    // A value wider than can fit in a signed 16-bit int should be rejected
    const WebRequestTestParam params[] = {{"speed", "32768"}, {"steer", "0"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleDrivePost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "speed and steer must be integers"));
}

void test_drive_is_rejected_while_sbus_lost_and_web_control_disabled() {
    robotState.webControlEnabled = false;
    robotState.sbusSignalLost = true;

    const WebRequestTestParam params[] = {{"speed", "100"}, {"steer", "0"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleDrivePost(req);

    TEST_ASSERT_EQUAL_INT(409, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "drive blocked by safety state"));
}

void test_drive_is_allowed_while_sbus_lost_but_web_control_enabled() {
    robotState.webControlEnabled = true;
    robotState.sbusSignalLost = true;

    const WebRequestTestParam params[] = {{"speed", "100"}, {"steer", "0"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleDrivePost(req);

    // Should succeed when web control is explicitly enabled despite SBUS loss
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
}

// -----------------------------------------------------------------------------
// Drive
// -----------------------------------------------------------------------------

void test_drive_clamps_to_the_configured_speed_cap() {
    setDriveConfig(200);
    robotState.webControlEnabled = true;

    const WebRequestTestParam params[] = {{"speed", "900"}, {"steer", "-900"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleDrivePost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    const DriveOutput resolved = resolvedDriveOutput();
    TEST_ASSERT_EQUAL_INT16(200, resolved.speed);
    TEST_ASSERT_EQUAL_INT16(-200, resolved.steer);
}

void test_drive_is_rejected_while_estopped() {
    robotState.webControlEnabled = true;
    robotState.estop = true;

    const WebRequestTestParam params[] = {{"speed", "100"}, {"steer", "0"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleDrivePost(req);

    TEST_ASSERT_EQUAL_INT(409, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "drive blocked by safety state"));
}

void test_drive_is_rejected_when_stationary() {
    robotState.webControlEnabled = true;
    robotState.stationary = true;

    const WebRequestTestParam params[] = {{"speed", "100"}, {"steer", "0"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleDrivePost(req);

    TEST_ASSERT_EQUAL_INT(409, backend.sentCode);
}

void test_drive_rejects_non_integer_values() {
    robotState.webControlEnabled = true;

    const WebRequestTestParam params[] = {{"speed", "fast"}, {"steer", "0"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleDrivePost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "speed and steer must be integers"));
}

void test_drive_without_parameters_is_rejected() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleDrivePost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "missing speed or steer"));
}

void test_mode_post_sets_stationary_and_broadcasts() {
    const WebRequestTestParam params[] = {{"mode", "STATIONARY"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleModePost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_TRUE(g_test_commanded_stationary);
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
}

void test_mode_post_rejects_an_unknown_mode() {
    const WebRequestTestParam params[] = {{"mode", "hyperdrive"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleModePost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "invalid mode"));
    TEST_ASSERT_EQUAL_UINT(0, g_test_status_broadcast_count);
}

void test_speed_preset_reports_the_applied_cap() {
    const WebRequestTestParam params[] = {{"preset", "TURBO"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleSpeedPresetPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"speedLimitMax\":"));
}

void test_speed_preset_reports_a_failed_persist() {
    g_test_speed_preset_persist_ok = false;
    const WebRequestTestParam params[] = {{"preset", "slow"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleSpeedPresetPost(req);

    TEST_ASSERT_EQUAL_INT(500, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "failed to persist speed preset"));
}

void test_web_control_disable_submits_a_zero_frame() {
    robotState.webControlEnabled = true;
    setDriveConfig(300);

    const WebRequestTestParam driveParams[] = {{"speed", "150"}, {"steer", "0"}};
    WebRequestTestBackend driveBackend;
    driveBackend.params = driveParams;
    driveBackend.paramCount = 2;
    WebRequest driveReq(&driveBackend);
    handleDrivePost(driveReq);
    TEST_ASSERT_EQUAL_INT16(150, resolvedDriveOutput().speed);

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleWebControlDisablePost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_FALSE(g_test_commanded_web_control);
    // Disabling browser control must not leave the last non-zero command
    // standing in the arbiter for the drive loop to keep applying.
    TEST_ASSERT_EQUAL_INT16(0, resolvedDriveOutput().speed);
}

// -----------------------------------------------------------------------------
// Manual command routing (shared with POST /api/manual-command)
// -----------------------------------------------------------------------------

void test_manual_command_keywords_are_case_insensitive() {
    TEST_ASSERT_TRUE(executeManualCommand("EnAbLe_Web_Control"));
    TEST_ASSERT_TRUE(g_test_commanded_web_control);
}

void test_manual_command_rejects_an_unknown_keyword() {
    TEST_ASSERT_FALSE(executeManualCommand("engage_hyperdrive"));
    TEST_ASSERT_EQUAL_UINT(0, g_test_web_control_calls);
}

void test_manual_command_rejects_an_empty_command() {
    TEST_ASSERT_FALSE(executeManualCommand(""));
}

void test_manual_command_routes_marcduino_by_prefix_without_case_folding() {
    TEST_ASSERT_TRUE(executeManualCommand("#SM"));
    TEST_ASSERT_EQUAL_UINT(1, g_test_marcduino_calls);
    // A Marcduino line is handed over verbatim -- lowercasing it here is what
    // the keyword path does, and doing it to these would change the command.
    TEST_ASSERT_EQUAL_UINT(0, g_test_web_control_calls);
}

void test_manual_command_intercepts_mood_commands() {
    TEST_ASSERT_TRUE(executeManualCommand(":SE11"));
    TEST_ASSERT_NOT_EQUAL(0, g_test_applied_mood);
    // Intercepted before the Marcduino router, which would discard it.
    TEST_ASSERT_EQUAL_UINT(0, g_test_marcduino_calls);
}

void test_manual_command_longer_than_any_keyword_is_unknown_not_truncated() {
    // "estop" plus padding: a keyword buffer that truncated instead of
    // rejecting would latch the estop on a command nobody sent.
    TEST_ASSERT_FALSE(executeManualCommand("estop_but_very_much_longer_than_the_buffer"));
    TEST_ASSERT_FALSE(failsafeIsActive());
}

// -----------------------------------------------------------------------------
// Dome
// -----------------------------------------------------------------------------

void test_dome_speed_rejects_out_of_range() {
    const WebRequestTestParam params[] = {{"speed", "2.5"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleDomeSpeedPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "-1.0..1.0"));
}

void test_dome_speed_is_refused_while_sleeping() {
    robotState.sleepMode = true;
    const WebRequestTestParam params[] = {{"speed", "0.5"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleDomeSpeedPost(req);

    TEST_ASSERT_EQUAL_INT(423, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "POST /api/wake"));
}

void test_dome_speed_is_refused_when_dome_output_is_disabled() {
    setDomeEnabled(false);
    const WebRequestTestParam params[] = {{"speed", "0.5"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleDomeSpeedPost(req);

    TEST_ASSERT_EQUAL_INT(409, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "dome output is disabled"));
}

void test_dome_cmd_rejects_an_overlong_command() {
    char longCmd[200];
    memset(longCmd, '*', sizeof(longCmd) - 1);
    longCmd[sizeof(longCmd) - 1] = '\0';

    const WebRequestTestParam params[] = {{"cmd", longCmd}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleDomeCmdPost(req);

    // The length limit is the endpoint's own contract, so it has to be what
    // rejects -- not a copy-out buffer quietly truncating into a valid command.
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "cmd too long (max 127)"));
}

void test_dome_cmd_without_a_command_is_rejected() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleDomeCmdPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "missing cmd parameter"));
}

void test_dome_layout_requests_a_refresh_on_a_cache_miss() {
    robotState.domeActiveTransport = DOME_LINK_TRANSPORT_WIFI;
    g_test_dome_layout_status.has_data = false;

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleDomeLayoutGet(req);

    TEST_ASSERT_EQUAL_INT(503, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"retry\":true"));
    TEST_ASSERT_EQUAL_UINT(1, g_test_dome_layout_refresh_requests);
}

void test_dome_layout_is_unavailable_off_wifi_transport() {
    robotState.domeActiveTransport = DOME_LINK_TRANSPORT_UART;

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleDomeLayoutGet(req);

    TEST_ASSERT_EQUAL_INT(503, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "not WiFi"));
    // No refresh: the cache is not the problem, the transport is.
    TEST_ASSERT_EQUAL_UINT(0, g_test_dome_layout_refresh_requests);
}

void test_dome_layout_streams_the_cache_with_its_age_header() {
    robotState.domeActiveTransport = DOME_LINK_TRANSPORT_WIFI;
    g_test_dome_layout_payload = "{\"schema\":1,\"panels\":[]}";
    g_test_dome_layout_status.has_data = true;
    g_test_dome_layout_status.length = strlen(g_test_dome_layout_payload);
    g_test_dome_layout_status.fetched_at_ms = 0;

    WebRequestTestBackend backend;
    WebRequest req(&backend);
    handleDomeLayoutGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_TRUE(backend.sentChunked);
    TEST_ASSERT_EQUAL_STRING(g_test_dome_layout_payload, backend.sentBody);
    TEST_ASSERT_EQUAL_UINT(strlen(g_test_dome_layout_payload), backend.sentBodyLength);
    // The age header survived the port to the seam rather than being dropped.
    TEST_ASSERT_EQUAL_size_t(1, backend.headerCount);
    TEST_ASSERT_EQUAL_STRING("X-Dome-Layout-Age-Ms", backend.headers[0].name);
}

// -----------------------------------------------------------------------------
// Servo
// -----------------------------------------------------------------------------

void test_servo_accepts_a_named_arm_action() {
    const WebRequestTestParam params[] = {{"arm", "arm1"}, {"action", "open"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleServoPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", backend.sentBody);
}

void test_servo_accepts_the_broadcast_arm() {
    // 255 is a real armId (robot_state.h) and this endpoint's own error message
    // offers "both" -- an int8_t return truncated it to the invalid sentinel.
    const WebRequestTestParam params[] = {{"arm", "both"}, {"action", "close"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleServoPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
}

void test_servo_rejects_an_unknown_arm() {
    const WebRequestTestParam params[] = {{"arm", "arm9"}, {"action", "open"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleServoPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "Invalid arm"));
}

void test_servo_rejects_an_out_of_range_position() {
    const WebRequestTestParam params[] = {
        {"arm", "arm1"}, {"action", "position"}, {"positionUs", "4000"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 3;
    WebRequest req(&backend);

    handleServoPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "positionUs must be between"));
}

void test_servo_position_without_a_value_is_rejected() {
    const WebRequestTestParam params[] = {{"arm", "arm1"}, {"action", "position"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 2;
    WebRequest req(&backend);

    handleServoPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "Missing positionUs parameter"));
}

// -----------------------------------------------------------------------------
// AUX LED
// -----------------------------------------------------------------------------

void test_aux_led_color_accepts_form_fields() {
    robotState.auxLed.available = true;
    robotState.auxLed.pin = 4;
    const WebRequestTestParam params[] = {{"r", "10"}, {"g", "20"}, {"b", "30"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 3;
    WebRequest req(&backend);

    handleAuxLedColorPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"r\":10"));
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"b\":30"));
}

void test_aux_led_color_accepts_a_json_body() {
    robotState.auxLed.available = true;
    robotState.auxLed.pin = 4;
    WebRequestTestBackend backend;
    backend.body = "{\"r\":1,\"g\":2,\"b\":3}";
    WebRequest req(&backend);

    handleAuxLedColorPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"g\":2"));
}

void test_aux_led_color_rejects_an_out_of_range_channel() {
    const WebRequestTestParam params[] = {{"r", "300"}, {"g", "0"}, {"b", "0"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 3;
    WebRequest req(&backend);

    handleAuxLedColorPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "0..255"));
}

void test_aux_led_effect_reports_the_new_effect() {
    robotState.auxLed.available = true;
    robotState.auxLed.pin = 4;
    const WebRequestTestParam params[] = {{"effect", "pulse"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleAuxLedEffectPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "\"effect\":\"pulse\""));
}

void test_aux_led_effect_rejects_an_unknown_effect() {
    const WebRequestTestParam params[] = {{"effect", "strobe"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleAuxLedEffectPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "off|solid|blink|pulse"));
}

void test_aux_led_reports_an_unavailable_strip_distinctly() {
    g_test_aux_led_queue_ok = false;
    robotState.auxLed.available = false;
    const WebRequestTestParam params[] = {{"effect", "solid"}};
    WebRequestTestBackend backend;
    backend.params = params;
    backend.paramCount = 1;
    WebRequest req(&backend);

    handleAuxLedEffectPost(req);

    TEST_ASSERT_EQUAL_INT(503, backend.sentCode);
    TEST_ASSERT_NOT_NULL(strstr(backend.sentBody, "aux LED unavailable"));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_estop_post_latches_and_stays_latched);
    RUN_TEST(test_estop_clear_releases_the_latch);
    RUN_TEST(test_both_estop_paths_are_exempt_from_admission);
    RUN_TEST(test_dedicated_estop_clear_handler_broadcasts_status);
    RUN_TEST(test_manual_command_clear_estop_clears_and_broadcasts);
    RUN_TEST(test_both_clear_paths_are_behaviourally_identical);
    RUN_TEST(test_manual_command_clear_estop_is_case_insensitive);
    RUN_TEST(test_only_explicit_clear_estop_can_clear_the_latch);

    RUN_TEST(test_drive_clamps_to_the_configured_speed_cap);
    RUN_TEST(test_drive_is_rejected_while_estopped);
    RUN_TEST(test_drive_is_rejected_when_stationary);
    RUN_TEST(test_drive_rejects_non_integer_values);
    RUN_TEST(test_drive_without_parameters_is_rejected);
    RUN_TEST(test_drive_oversized_speed_is_rejected);
    RUN_TEST(test_drive_is_rejected_while_sbus_lost_and_web_control_disabled);
    RUN_TEST(test_drive_is_allowed_while_sbus_lost_but_web_control_enabled);
    RUN_TEST(test_mode_post_sets_stationary_and_broadcasts);
    RUN_TEST(test_mode_post_rejects_an_unknown_mode);
    RUN_TEST(test_speed_preset_reports_the_applied_cap);
    RUN_TEST(test_speed_preset_reports_a_failed_persist);
    RUN_TEST(test_web_control_disable_submits_a_zero_frame);

    RUN_TEST(test_manual_command_keywords_are_case_insensitive);
    RUN_TEST(test_manual_command_rejects_an_unknown_keyword);
    RUN_TEST(test_manual_command_rejects_an_empty_command);
    RUN_TEST(test_manual_command_routes_marcduino_by_prefix_without_case_folding);
    RUN_TEST(test_manual_command_intercepts_mood_commands);
    RUN_TEST(test_manual_command_longer_than_any_keyword_is_unknown_not_truncated);

    RUN_TEST(test_dome_speed_rejects_out_of_range);
    RUN_TEST(test_dome_speed_is_refused_while_sleeping);
    RUN_TEST(test_dome_speed_is_refused_when_dome_output_is_disabled);
    RUN_TEST(test_dome_cmd_rejects_an_overlong_command);
    RUN_TEST(test_dome_cmd_without_a_command_is_rejected);
    RUN_TEST(test_dome_layout_requests_a_refresh_on_a_cache_miss);
    RUN_TEST(test_dome_layout_is_unavailable_off_wifi_transport);
    RUN_TEST(test_dome_layout_streams_the_cache_with_its_age_header);

    RUN_TEST(test_servo_accepts_a_named_arm_action);
    RUN_TEST(test_servo_accepts_the_broadcast_arm);
    RUN_TEST(test_servo_rejects_an_unknown_arm);
    RUN_TEST(test_servo_rejects_an_out_of_range_position);
    RUN_TEST(test_servo_position_without_a_value_is_rejected);

    RUN_TEST(test_aux_led_color_accepts_form_fields);
    RUN_TEST(test_aux_led_color_accepts_a_json_body);
    RUN_TEST(test_aux_led_color_rejects_an_out_of_range_channel);
    RUN_TEST(test_aux_led_effect_reports_the_new_effect);
    RUN_TEST(test_aux_led_effect_rejects_an_unknown_effect);
    RUN_TEST(test_aux_led_reports_an_unavailable_strip_distinctly);

    return UNITY_END();
}
