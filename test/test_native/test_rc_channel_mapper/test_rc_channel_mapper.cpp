// =============================================================================
// test/test_native/test_rc_channel_mapper/test_rc_channel_mapper.cpp
//
// Native unit tests for RC channel mapper pure function.
// Tests: dead-zone, inversion, mode routing, clamping.
// =============================================================================

#include <unity.h>

#include "rc_channel_mapper.h"
#include "rc_mapping.h"

void setUp() {
}

void tearDown() {
}

// =============================================================================
// Helper Functions
// =============================================================================

static RcChannelSnapshot makePwmSnapshot(uint32_t ch1_us, uint32_t ch2_us) {
    RcChannelSnapshot snap = {};
    snap.valid = true;
    snap.mode = RC_INPUT_STANDARD_PWM;
    snap.channels[0] = ch1_us;  // CH1 at index 0
    snap.channels[1] = ch2_us;  // CH2 at index 1
    return snap;
}

static RcChannelSnapshot makeSbusSnapshot(int ch1_raw, int ch2_raw) {
    RcChannelSnapshot snap = {};
    snap.valid = true;
    snap.mode = RC_INPUT_SINGLE_SBUS;
    snap.channels[0] = ch1_raw;  // CH1 at index 0
    snap.channels[1] = ch2_raw;  // CH2 at index 1
    return snap;
}

static RcMappingConfig makeDefaultPwmConfig() {
    RcMappingConfig cfg = {};
    cfg.enableRc[0] = true;
    cfg.enableRc[1] = true;
    cfg.enableRc[2] = true;
    cfg.enableRc[3] = true;
    cfg.enableRc[4] = true;
    cfg.enableRc[5] = true;
    cfg.enableDome = false;
    cfg.enableArm1 = false;
    cfg.enableArm2 = false;
    cfg.enableSound = false;
    cfg.maxOut = 1000;
    cfg.prevSoundPressed = false;

    // Drive speed on CH1, steer on CH2
    cfg.driveSpeed = defaultPwmBinding(1);
    cfg.driveSteer = defaultPwmBinding(2);
    cfg.domeSpeed = disabledRcBinding();
    cfg.arm1 = disabledRcBinding();
    cfg.arm2 = disabledRcBinding();
    cfg.sound = disabledRcBinding();
    return cfg;
}

static RcMappingConfig makeDefaultSbusConfig() {
    RcMappingConfig cfg = {};
    cfg.enableRc[0] = true;  // SBUS1 receiver
    cfg.enableRc[1] = true;  // SBUS2 receiver (not used for single_sbus)
    cfg.enableRc[2] = false;
    cfg.enableRc[3] = false;
    cfg.enableRc[4] = false;
    cfg.enableRc[5] = false;
    cfg.enableDome = false;
    cfg.enableArm1 = false;
    cfg.enableArm2 = false;
    cfg.enableSound = false;
    cfg.maxOut = 1000;
    cfg.prevSoundPressed = false;

    // Drive speed on SBUS CH1, steer on CH2
    cfg.driveSpeed = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    cfg.driveSteer = defaultSbusBinding(RC_BINDING_SBUS1, 2);
    cfg.domeSpeed = disabledRcBinding();
    cfg.arm1 = disabledRcBinding();
    cfg.arm2 = disabledRcBinding();
    cfg.sound = disabledRcBinding();
    return cfg;
}

static RcChannelSnapshot makeDualSbusSnapshot(int ch1_raw, int ch2_raw, int dome_ch1_raw,
                                               int dome_ch2_raw) {
    RcChannelSnapshot snap = {};
    snap.valid = true;
    snap.mode = RC_INPUT_DUAL_SBUS;
    // SBUS1 (drive): channels 0-15
    snap.channels[0] = ch1_raw;   // Drive speed
    snap.channels[1] = ch2_raw;   // Drive steer
    // SBUS2 (dome): channels 16-17 map to logical 16-17 in the snapshot
    snap.channels[16] = dome_ch1_raw;
    snap.channels[17] = dome_ch2_raw;
    return snap;
}

static RcMappingConfig makeDefaultDualSbusConfig() {
    RcMappingConfig cfg = {};
    cfg.enableRc[0] = true;  // SBUS1 (drive) enabled
    cfg.enableRc[1] = true;  // SBUS2 (dome) enabled
    cfg.enableRc[2] = false;
    cfg.enableRc[3] = false;
    cfg.enableRc[4] = false;
    cfg.enableRc[5] = false;
    cfg.enableDome = true;
    cfg.enableArm1 = false;
    cfg.enableArm2 = false;
    cfg.enableSound = false;
    cfg.maxOut = 1000;
    cfg.prevSoundPressed = false;

    // Drive: SBUS1 CH1 (speed), CH2 (steer)
    cfg.driveSpeed = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    cfg.driveSteer = defaultSbusBinding(RC_BINDING_SBUS1, 2);
    // Dome: SBUS2 CH1 (speed)
    cfg.domeSpeed = defaultSbusBinding(RC_BINDING_SBUS2, 1);
    cfg.arm1 = disabledRcBinding();
    cfg.arm2 = disabledRcBinding();
    cfg.sound = disabledRcBinding();
    return cfg;
}

// =============================================================================
// Test: PWM Mode — Basic Mapping (Center Sticks)
// =============================================================================

void test_pwm_mode_center_sticks() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1500);  // Both centered
    RcMappingConfig cfg = makeDefaultPwmConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);   // Center = 0
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);   // Center = 0
    TEST_ASSERT_EQUAL_INT16(0, intent.domeSpeed);
    TEST_ASSERT_NULL(intent.audioTrigger);
}

void test_pwm_mode_full_forward() {
    RcChannelSnapshot snap = makePwmSnapshot(2000, 1500);  // Full forward, centered steer
    RcMappingConfig cfg = makeDefaultPwmConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(1000, intent.driveSpeed);   // Full forward = +1000
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
}

void test_pwm_mode_full_reverse() {
    RcChannelSnapshot snap = makePwmSnapshot(1000, 1500);  // Full reverse, centered steer
    RcMappingConfig cfg = makeDefaultPwmConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(-1000, intent.driveSpeed);   // Full reverse = -1000
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
}

void test_pwm_mode_left_steer() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1000);  // Centered speed, full left
    RcMappingConfig cfg = makeDefaultPwmConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(-1000, intent.driveSteer);   // Full left = -1000
}

void test_pwm_mode_right_steer() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 2000);  // Centered speed, full right
    RcMappingConfig cfg = makeDefaultPwmConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(1000, intent.driveSteer);   // Full right = +1000
}

// =============================================================================
// Test: PWM Mode — Invalid Snapshot
// =============================================================================

void test_invalid_snapshot() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1500);
    snap.valid = false;
    RcMappingConfig cfg = makeDefaultPwmConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_FALSE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
}

// =============================================================================
// Test: Dead-zone Application
// =============================================================================

void test_pwm_deadzone_application() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1500);
    RcMappingConfig cfg = makeDefaultPwmConfig();

    // Add 50 us deadband to speed binding (±50 from center 1500)
    cfg.driveSpeed.deadband = 50;
    cfg.driveSteer.deadband = 50;

    // Values within deadzone should map to 0
    RcControlIntent intent1 = rcMapChannels(snap, cfg);
    TEST_ASSERT_EQUAL_INT16(0, intent1.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, intent1.driveSteer);

    // Values just outside deadzone should be non-zero
    snap.channels[0] = 1551;  // 51 µs from center
    RcControlIntent intent2 = rcMapChannels(snap, cfg);
    TEST_ASSERT_TRUE(intent2.driveSpeed > 0);
}

// =============================================================================
// Test: Inversion
// =============================================================================

void test_pwm_inversion() {
    RcChannelSnapshot snap = makePwmSnapshot(2000, 1500);  // Full forward
    RcMappingConfig cfg = makeDefaultPwmConfig();

    // Enable inversion on speed
    cfg.driveSpeed.reverse = true;

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(-1000, intent.driveSpeed);   // Inverted: forward becomes reverse
}

// =============================================================================
// Test: Speed Limit (maxOut)
// =============================================================================

void test_speed_limit_half() {
    RcChannelSnapshot snap = makePwmSnapshot(2000, 1500);  // Full forward
    RcMappingConfig cfg = makeDefaultPwmConfig();

    // Set max output to 500 (half normal)
    cfg.maxOut = 500;

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(500, intent.driveSpeed);   // Scaled to 500, not 1000
}

// =============================================================================
// Test: SBUS Mode — Basic Mapping
// =============================================================================

void test_sbus_mode_center_sticks() {
    RcChannelSnapshot snap = makeSbusSnapshot(992, 992);  // SBUS center
    RcMappingConfig cfg = makeDefaultSbusConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
}

void test_sbus_mode_full_forward() {
    RcChannelSnapshot snap = makeSbusSnapshot(1811, 992);  // SBUS max
    RcMappingConfig cfg = makeDefaultSbusConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(1000, intent.driveSpeed);
}

void test_sbus_mode_full_reverse() {
    RcChannelSnapshot snap = makeSbusSnapshot(172, 992);  // SBUS min
    RcMappingConfig cfg = makeDefaultSbusConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(-1000, intent.driveSpeed);
}

// =============================================================================
// Test: Mode Mismatch (snapshot mode != binding source)
// =============================================================================

void test_mode_mismatch_pwm_binding_in_sbus_mode() {
    RcChannelSnapshot snap = makeSbusSnapshot(992, 992);  // SBUS mode snapshot
    RcMappingConfig cfg = makeDefaultPwmConfig();          // PWM bindings
    // Snapshot is SBUS mode, but config expects PWM

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_FALSE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
}

void test_mode_mismatch_sbus_binding_in_pwm_mode() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1500);  // PWM mode snapshot
    RcMappingConfig cfg = makeDefaultSbusConfig();          // SBUS bindings
    // Snapshot is PWM mode, but config expects SBUS

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_FALSE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
}

// =============================================================================
// Test: Clamping (Out-of-range values)
// =============================================================================

void test_pwm_out_of_range_clamping() {
    RcChannelSnapshot snap = makePwmSnapshot(2050, 1500);  // Just beyond valid range (2100 max)
    RcMappingConfig cfg = makeDefaultPwmConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    // Value is within valid range but above max (2000), so should clamp during calibration
    TEST_ASSERT_EQUAL_INT16(1000, intent.driveSpeed);
}

// =============================================================================
// Test: Disabled Binding
// =============================================================================

void test_disabled_binding_returns_zero() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1500);
    RcMappingConfig cfg = makeDefaultPwmConfig();

    // Disable drive binding
    cfg.driveSpeed = disabledRcBinding();
    cfg.driveSteer = defaultPwmBinding(2);

    RcControlIntent intent = rcMapChannels(snap, cfg);

    // With speed disabled, intent is not valid
    TEST_ASSERT_FALSE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
}

// =============================================================================
// Test: Dome Speed
// =============================================================================

void test_dome_speed_mapping() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1500);
    snap.channels[2] = 2000;  // CH3 = full forward on dome
    RcMappingConfig cfg = makeDefaultPwmConfig();

    // Enable dome on CH3
    cfg.enableDome = true;
    cfg.domeSpeed = defaultPwmBinding(3);

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_EQUAL_INT16(1000, intent.domeSpeed);   // Dome at full forward
}

void test_dome_disabled() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1500);
    snap.channels[2] = 2000;
    RcMappingConfig cfg = makeDefaultPwmConfig();

    // Dome disabled (default)
    cfg.enableDome = false;

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_EQUAL_INT16(0, intent.domeSpeed);
}

// =============================================================================
// Test: Partial Input (only one of speed/steer available)
// =============================================================================

void test_partial_input_missing_speed() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1500);
    RcMappingConfig cfg = makeDefaultPwmConfig();

    // Make speed binding invalid (NONE source)
    cfg.driveSpeed = disabledRcBinding();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    // Both speed and steer must be available for drive intent
    TEST_ASSERT_FALSE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
}

void test_partial_input_missing_steer() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1500);
    RcMappingConfig cfg = makeDefaultPwmConfig();

    // Make steer binding invalid (NONE source)
    cfg.driveSteer = disabledRcBinding();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    // Both speed and steer must be available for drive intent
    TEST_ASSERT_FALSE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
}

// =============================================================================
// Test: Dual-SBUS Mode
// =============================================================================

void test_dual_sbus_center_sticks() {
    RcChannelSnapshot snap = makeDualSbusSnapshot(992, 992, 992, 992);
    RcMappingConfig cfg = makeDefaultDualSbusConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
    TEST_ASSERT_EQUAL_INT16(0, intent.domeSpeed);
}

void test_dual_sbus_drive_forward_dome_speed() {
    RcChannelSnapshot snap = makeDualSbusSnapshot(1811, 992, 1811, 992);
    RcMappingConfig cfg = makeDefaultDualSbusConfig();

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_TRUE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(1000, intent.driveSpeed);    // SBUS1 CH1 full forward
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);       // SBUS1 CH2 centered
    TEST_ASSERT_EQUAL_INT16(1000, intent.domeSpeed);     // SBUS2 CH1 full forward
}

void test_dual_sbus_mode_mismatch() {
    RcChannelSnapshot snap = makePwmSnapshot(1500, 1500);  // PWM mode, not dual-SBUS
    RcMappingConfig cfg = makeDefaultDualSbusConfig();     // Config expects dual-SBUS

    RcControlIntent intent = rcMapChannels(snap, cfg);

    TEST_ASSERT_FALSE(intent.valid);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, intent.driveSteer);
    TEST_ASSERT_EQUAL_INT16(0, intent.domeSpeed);
}

// =============================================================================
// Run All Tests
// =============================================================================

int main() {
    UNITY_BEGIN();

    // PWM basic mapping
    RUN_TEST(test_pwm_mode_center_sticks);
    RUN_TEST(test_pwm_mode_full_forward);
    RUN_TEST(test_pwm_mode_full_reverse);
    RUN_TEST(test_pwm_mode_left_steer);
    RUN_TEST(test_pwm_mode_right_steer);

    // Invalid snapshot
    RUN_TEST(test_invalid_snapshot);

    // Dead-zone
    RUN_TEST(test_pwm_deadzone_application);

    // Inversion
    RUN_TEST(test_pwm_inversion);

    // Speed limit
    RUN_TEST(test_speed_limit_half);

    // SBUS mode
    RUN_TEST(test_sbus_mode_center_sticks);
    RUN_TEST(test_sbus_mode_full_forward);
    RUN_TEST(test_sbus_mode_full_reverse);

    // Mode mismatch
    RUN_TEST(test_mode_mismatch_pwm_binding_in_sbus_mode);
    RUN_TEST(test_mode_mismatch_sbus_binding_in_pwm_mode);

    // Clamping
    RUN_TEST(test_pwm_out_of_range_clamping);

    // Disabled binding
    RUN_TEST(test_disabled_binding_returns_zero);

    // Dome speed
    RUN_TEST(test_dome_speed_mapping);
    RUN_TEST(test_dome_disabled);

    // Partial input
    RUN_TEST(test_partial_input_missing_speed);
    RUN_TEST(test_partial_input_missing_steer);

    // Dual-SBUS mode
    RUN_TEST(test_dual_sbus_center_sticks);
    RUN_TEST(test_dual_sbus_drive_forward_dome_speed);
    RUN_TEST(test_dual_sbus_mode_mismatch);

    return UNITY_END();
}
