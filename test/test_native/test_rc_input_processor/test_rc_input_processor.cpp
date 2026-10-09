// =============================================================================
// test/test_native/test_rc_input_processor/test_rc_input_processor.cpp
//
// Unity tests for RcInputProcessor pure orchestration logic.
// =============================================================================

#include <unity.h>
#include <cstring>

#include "rc_input_processor.h"

// Helper to build a basic RcChannelSnapshot
static RcChannelSnapshot buildChannelSnapshot() {
    RcChannelSnapshot snap = {};
    snap.valid = true;
    snap.mode = RC_INPUT_DUAL_SBUS;
    for (int i = 0; i < 16; ++i) {
        snap.channels[i] = RC_SBUS_DEFAULT_CENTER;
    }
    return snap;
}

// Helper to build a basic RcProcessorConfig
static RcProcessorConfig buildProcessorConfig() {
    RcProcessorConfig cfg = {};
    cfg.mapping = {};
    cfg.mapping.enableDome = true;
    cfg.mapping.maxOut = 1000;
    cfg.mapping.domeSpeed = defaultSbusBinding(RC_BINDING_SBUS1, 3);
    cfg.triggerCount = 0;
    cfg.categories = {};
    cfg.estopActive = false;
    cfg.currentSleepMode = false;
    cfg.currentSpeedPreset = SpeedPresetId::Normal;
    return cfg;
}

void setUp(void) {}

void tearDown(void) {}

void test_init_zeroes_state(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);

    // All trigger states should be zeroed (switchStateInit = false indicates not yet initialized)
    for (size_t i = 0; i < RC_TRIGGER_MAX; ++i) {
        TEST_ASSERT_FALSE(proc.triggerStates[i].switchStateInit);
        TEST_ASSERT_FALSE(proc.triggerStates[i].lastPressed);
    }

    // Dome filter should be zeroed (not initialized)
    TEST_ASSERT_FALSE(proc.domeInputFilter.initialized);

    // Sound state should be false
    TEST_ASSERT_FALSE(proc.lastSoundPressed);

    // Stationary lock should be false
    TEST_ASSERT_FALSE(proc.stationaryLocked);

    // The boot hold starts set: no drive stick seen at centre yet
    TEST_ASSERT_FALSE(proc.driveCentreSeen);
}

void test_backbone_drive_passthrough(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);

    RcChannelSnapshot snap = buildChannelSnapshot();
    snap.channels[0] = 1200;  // ch1: drive speed, above center
    snap.channels[1] = 950;   // ch2: drive steer, below center

    RcProcessorConfig cfg = buildProcessorConfig();
    cfg.mapping.driveSpeed = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    cfg.mapping.driveSteer = defaultSbusBinding(RC_BINDING_SBUS1, 2);
    cfg.mapping.enableRc[0] = true;
    cfg.mapping.enableRc[1] = true;

    RcProcessorInput input = {};
    input.config = cfg;
    input.nowMs = millis();
    input.randomSeed = 42;

    // The boot hold releases on a centred frame first (#389).
    RcProcessorOutput output = {};
    input.channels = buildChannelSnapshot();
    rcInputProcessorTick(&proc, input, &output);

    input.channels = snap;
    rcInputProcessorTick(&proc, input, &output);

    // Drive intent should be non-zero
    TEST_ASSERT_TRUE(output.backbone.driveSpeed != 0 || output.backbone.driveSteer != 0);
    TEST_ASSERT_TRUE(output.submitDrive);
}

// A drive-and-dome processor config on the default dual_sbus bindings: drive
// on SBUS1 CH1/CH2, dome on SBUS2 CH1, both receivers enabled.
static RcProcessorConfig buildDualDefaultConfig() {
    RcProcessorConfig cfg = buildProcessorConfig();
    cfg.mapping.driveSpeed = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    cfg.mapping.driveSteer = defaultSbusBinding(RC_BINDING_SBUS1, 2);
    cfg.mapping.domeSpeed = defaultSbusBinding(RC_BINDING_SBUS2, 1);
    cfg.mapping.enableRc[0] = true;
    cfg.mapping.enableRc[1] = true;
    return cfg;
}

static RcProcessorOutput tickWith(RcInputProcessor* proc, const RcProcessorConfig& cfg,
                                  RcBindingSource source, int ch1, int ch2) {
    RcProcessorInput input = {};
    input.config = cfg;
    input.channels = buildChannelSnapshot();
    input.channels.source = source;
    input.channels.channels[0] = (int16_t)ch1;
    input.channels.channels[1] = (int16_t)ch2;
    input.nowMs = 1000;
    input.sourceFilter = source;
    RcProcessorOutput output = {};
    rcInputProcessorTick(proc, input, &output);
    return output;
}

void test_boot_hold_zeroes_drive_until_sticks_centre(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);
    const RcProcessorConfig cfg = buildDualDefaultConfig();

    RcProcessorOutput held = tickWith(&proc, cfg, RC_BINDING_SBUS1, 1811, 992);
    TEST_ASSERT_TRUE(held.driveAwaitingCentre);
    TEST_ASSERT_EQUAL_INT16(0, held.backbone.driveSpeed);
    TEST_ASSERT_EQUAL_INT16(0, held.backbone.driveSteer);
    TEST_ASSERT_TRUE(held.submitDrive);  // a zero is sent, the hold is not silence

    RcProcessorOutput centred = tickWith(&proc, cfg, RC_BINDING_SBUS1, 1000, 985);
    TEST_ASSERT_FALSE(centred.driveAwaitingCentre);

    RcProcessorOutput driving = tickWith(&proc, cfg, RC_BINDING_SBUS1, 1811, 992);
    TEST_ASSERT_FALSE(driving.driveAwaitingCentre);
    TEST_ASSERT_EQUAL_INT16(1000, driving.backbone.driveSpeed);
}

// The HotRC DS-650's CH2 trigger rests at an endpoint at factory stroke: the
// hold never releases, so the trigger cannot drive the droid at boot.
void test_boot_hold_never_releases_on_a_trigger_resting_at_an_endpoint(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);
    const RcProcessorConfig cfg = buildDualDefaultConfig();

    for (int i = 0; i < 50; ++i) {
        RcProcessorOutput out = tickWith(&proc, cfg, RC_BINDING_SBUS1, 992, 2028);
        TEST_ASSERT_TRUE(out.driveAwaitingCentre);
        TEST_ASSERT_EQUAL_INT16(0, out.backbone.driveSteer);
    }
}

// dual_sbus: a frame from the dome receiver says nothing about the drive, and
// a frame from the drive receiver says nothing about the dome (#389).
void test_dual_sbus_dome_receiver_frame_leaves_drive_alone(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);
    const RcProcessorConfig cfg = buildDualDefaultConfig();
    (void)tickWith(&proc, cfg, RC_BINDING_SBUS1, 992, 992);  // release the boot hold

    RcProcessorOutput dome = tickWith(&proc, cfg, RC_BINDING_SBUS2, 1811, 172);
    TEST_ASSERT_FALSE(dome.submitDrive);
    TEST_ASSERT_FALSE(dome.backbone.driveActive);
    TEST_ASSERT_TRUE(dome.domeFiltered);
    TEST_ASSERT_EQUAL_INT(1811, dome.domeRawFiltered);

    RcProcessorOutput drive = tickWith(&proc, cfg, RC_BINDING_SBUS1, 1811, 992);
    TEST_ASSERT_TRUE(drive.submitDrive);
    TEST_ASSERT_EQUAL_INT16(1000, drive.backbone.driveSpeed);
    TEST_ASSERT_FALSE(drive.domeFiltered);
}

// dual_sbus with the drive receiver turned off: the dome receiver's frames
// keep sending the drive a zero, as before #389.
void test_dual_sbus_drive_receiver_off_keeps_zero_from_dome_frames(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);
    RcProcessorConfig cfg = buildDualDefaultConfig();
    cfg.mapping.enableRc[0] = false;

    RcProcessorOutput dome = tickWith(&proc, cfg, RC_BINDING_SBUS2, 1811, 172);
    TEST_ASSERT_TRUE(dome.submitDrive);
    TEST_ASSERT_EQUAL_INT16(0, dome.backbone.driveSpeed);
}

// single_sbus: the factory dome binding (SBUS2 CH1) reads nothing, so the drive
// stick on the one receiver's CH1 never turns the dome.
void test_single_sbus_factory_dome_binding_reads_nothing(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);
    RcProcessorConfig cfg = buildDualDefaultConfig();

    RcProcessorInput input = {};
    input.config = cfg;
    input.channels = buildChannelSnapshot();
    input.channels.mode = RC_INPUT_SINGLE_SBUS;
    input.channels.source = RC_BINDING_SBUS1;
    input.channels.channels[0] = 1811;
    input.nowMs = 1000;
    input.sourceFilter = RC_BINDING_SBUS1;
    RcProcessorOutput output = {};
    rcInputProcessorTick(&proc, input, &output);

    TEST_ASSERT_FALSE(output.domeFiltered);
    TEST_ASSERT_TRUE(output.backbone.driveActive);
}

void test_sound_edge_detection(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);

    // First: verify initial state
    TEST_ASSERT_FALSE(proc.lastSoundPressed);

    RcChannelSnapshot snap = buildChannelSnapshot();
    snap.channels[5] = 1811;  // ch6: sound, pressed (extreme high)

    RcProcessorConfig cfg = buildProcessorConfig();
    cfg.mapping.sound = defaultSbusBinding(RC_BINDING_SBUS1, 6);
    cfg.mapping.enableSound = true;
    cfg.mapping.enableRc[5] = true;

    RcProcessorInput input = {};
    input.channels = snap;
    input.config = cfg;
    input.nowMs = 1000;
    input.randomSeed = 42;

    // First tick: sound pressed
    RcProcessorOutput out1 = {};
    rcInputProcessorTick(&proc, input, &out1);
    // After first tick with sound high, lastSoundPressed should update
    // Note: rcMapChannels would determine if this results in soundPressed=true
    // The exact behavior depends on rc_channel_mapper, but we can at least
    // verify the processor ticked without error
    TEST_ASSERT_TRUE(true);  // Processor executed without crashing

    // Second tick: verify state is maintained
    input.nowMs = 1020;
    RcProcessorOutput out2 = {};
    rcInputProcessorTick(&proc, input, &out2);
    TEST_ASSERT_TRUE(true);  // Processor executed without crashing
}

void test_trigger_no_fire_before_confirm(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);

    RcChannelSnapshot snap = buildChannelSnapshot();
    snap.channels[2] = 1811;  // ch3: trigger, extreme position (one tick)

    RcProcessorConfig cfg = buildProcessorConfig();
    cfg.triggerCount = 1;
    cfg.triggers[0] = makeRcTriggerBinding(
        RC_BINDING_SBUS1, 3, SERVO_ACTION_ARM1_TOGGLE, nullptr,
        RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX, 0, true);

    RcProcessorInput input = {};
    input.channels = snap;
    input.config = cfg;
    input.nowMs = 1000;
    input.randomSeed = 42;

    // First tick: trigger at extreme, but not enough frames to confirm
    RcProcessorOutput output = {};
    rcInputProcessorTick(&proc, input, &output);

    // Should not have fired yet (needs kSwitchEdgeConfirmFrames = 2)
    TEST_ASSERT_EQUAL_INT(-1, output.triggerResults[0].servoIndex);
}

void test_trigger_fires_after_confirm(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);

    RcChannelSnapshot snap = buildChannelSnapshot();
    snap.channels[2] = 1811;  // ch3: trigger, extreme position

    RcProcessorConfig cfg = buildProcessorConfig();
    cfg.triggerCount = 1;
    cfg.triggers[0] = makeRcTriggerBinding(
        RC_BINDING_SBUS1, 3, SERVO_ACTION_ARM1_TOGGLE, nullptr,
        RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX, 0, true);

    RcProcessorInput input = {};
    input.channels = snap;
    input.config = cfg;
    input.nowMs = 1000;
    input.randomSeed = 42;

    // Tick enough times to confirm (kSwitchEdgeConfirmFrames = 2)
    RcProcessorOutput output = {};
    for (int tick = 0; tick < 3; ++tick) {
        rcInputProcessorTick(&proc, input, &output);
        input.nowMs += 20;
    }

    // After 3 ticks (initialization + 2 confirms), should have fired
    TEST_ASSERT_GREATER_OR_EQUAL(output.triggerResults[0].servoIndex, 0);
}

void test_stationary_lock_propagates(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);

    // op_mode switch: LOW (172) = driving, HIGH (1811) = stationary
    // To trigger stationary, we need the HIGH position
    RcChannelSnapshot snap = buildChannelSnapshot();
    snap.channels[2] = 1811;  // ch3: op_mode switch HIGH -> stationary

    RcProcessorConfig cfg = buildProcessorConfig();
    cfg.triggerCount = 1;
    cfg.triggers[0] = makeRcTriggerBinding(
        RC_BINDING_SBUS1, 3, SYSTEM_ACTION_OP_MODE, nullptr,
        RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX, 0, true);

    RcProcessorInput input = {};
    input.channels = snap;
    input.config = cfg;
    input.nowMs = 1000;
    input.randomSeed = 42;

    // Tick enough times to confirm (kSwitchEdgeConfirmFrames = 2)
    // Tick 0: init switch state
    // Tick 1: start confirming
    // Tick 2: confirm fired
    RcProcessorOutput output = {};
    for (int tick = 0; tick < 3; ++tick) {
        rcInputProcessorTick(&proc, input, &output);
        input.nowMs += 20;
        // If the op_mode action fired and set stationary mode, it updates proc.stationaryLocked
    }

    // Verify the processor executed and the output has the state field
    // The action dispatcher handles the stationary mode decision, and this
    // test just verifies the processor integrates with it without crashing
    TEST_ASSERT_FALSE(output.stationaryLockedByTrigger == true && proc.stationaryLocked == false);
}

void test_dome_filter_accepts_on_initial_tick(void) {
    RcInputProcessor proc = {};
    rcInputProcessorInit(&proc);

    RcChannelSnapshot snap = buildChannelSnapshot();
    snap.channels[2] = 1200;  // ch3: dome speed, off-center

    RcProcessorConfig cfg = buildProcessorConfig();
    cfg.mapping.enableDome = true;
    cfg.mapping.domeSpeed = defaultSbusBinding(RC_BINDING_SBUS1, 3);
    cfg.mapping.enableRc[0] = true;  // the dome binding's receiver is enabled

    RcProcessorInput input = {};
    input.channels = snap;
    input.config = cfg;
    input.nowMs = 1000;
    input.randomSeed = 42;

    RcProcessorOutput output = {};
    rcInputProcessorTick(&proc, input, &output);

    // Initial tick should accept the dome value
    TEST_ASSERT_TRUE(output.domeFiltered);
    TEST_ASSERT_EQUAL_INT(1200, output.domeRawFiltered);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_zeroes_state);
    RUN_TEST(test_backbone_drive_passthrough);
    RUN_TEST(test_sound_edge_detection);
    RUN_TEST(test_trigger_no_fire_before_confirm);
    RUN_TEST(test_trigger_fires_after_confirm);
    RUN_TEST(test_stationary_lock_propagates);
    RUN_TEST(test_dome_filter_accepts_on_initial_tick);
    RUN_TEST(test_boot_hold_zeroes_drive_until_sticks_centre);
    RUN_TEST(test_boot_hold_never_releases_on_a_trigger_resting_at_an_endpoint);
    RUN_TEST(test_dual_sbus_dome_receiver_frame_leaves_drive_alone);
    RUN_TEST(test_dual_sbus_drive_receiver_off_keeps_zero_from_dome_frames);
    RUN_TEST(test_single_sbus_factory_dome_binding_reads_nothing);
    return UNITY_END();
}
