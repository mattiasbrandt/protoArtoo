// =============================================================================
// test/test_native/test_audio_task_step/test_audio_task_step.cpp
//
// Audio Step Core (ADR 0014): lifecycle transitions, init-retry ceiling,
// sleep gates, relative volume, Track Stop vs Quiet, and cadence gating.
// =============================================================================
#include <string.h>

#include <unity.h>

#include "audio_task_step.h"

void setUp() {}
void tearDown() {}

static AudioStepState initializedState() {
    AudioStepState s{};
    s.driverInitialized = true;
    return s;
}

static AudioPlaybackConfig playbackConfig() {
    AudioPlaybackConfig cfg{};
    cfg.randMin = 100;
    cfg.randMax = 105;
    // A zero interval suppresses random playback for that mood; set all four
    // so the tick fires regardless of which interval mood 0 resolves to.
    cfg.intervalQuietS = 10;
    cfg.intervalMidS = 10;
    cfg.intervalFullS = 10;
    cfg.intervalAwakeS = 10;
    return cfg;
}

static AudioStepCommandInputs commandInputs(const AudioPlaybackConfig* playback,
                                            const AudioNamedTracks* named,
                                            const AudioBindingCache* bindings,
                                            uint32_t nowMs, bool sleepMode) {
    AudioStepCommandInputs in{};
    in.nowMs = nowMs;
    in.sleepMode = sleepMode;
    in.playback = playback;
    in.named = named;
    in.bindings = bindings;
    return in;
}

// --- tick: enable/disable ----------------------------------------------------

void test_disable_transition_stops_driver_and_clears_random_once() {
    AudioStepState s = initializedState();
    s.randomMode = true;

    AudioStepTickActions a = audioStepTick(s, {false, false, 20});
    TEST_ASSERT_TRUE(a.stopDriver);
    TEST_ASSERT_EQUAL(AUDIO_STEP_STOP_DISABLED, a.stopReason);
    TEST_ASSERT_TRUE(a.clearAudioActive);
    TEST_ASSERT_TRUE(a.drainQueue);
    TEST_ASSERT_FALSE(s.randomMode);

    // Second disabled iteration: no repeated stop, no repeated clear.
    a = audioStepTick(s, {false, false, 20});
    TEST_ASSERT_FALSE(a.stopDriver);
    TEST_ASSERT_FALSE(a.clearAudioActive);
    TEST_ASSERT_TRUE(a.drainQueue);
}

void test_disabled_uninitialized_driver_is_never_stopped() {
    AudioStepState s{};
    AudioStepTickActions a = audioStepTick(s, {false, false, 20});
    TEST_ASSERT_FALSE(a.stopDriver);
    TEST_ASSERT_TRUE(a.drainQueue);
}

// --- tick: sleep entry ---------------------------------------------------------

void test_sleep_entry_transition_fires_once() {
    AudioStepState s = initializedState();
    s.randomMode = true;

    AudioStepTickActions a = audioStepTick(s, {true, true, 20});
    TEST_ASSERT_TRUE(a.stopDriver);
    TEST_ASSERT_EQUAL(AUDIO_STEP_STOP_SLEEP_ENTRY, a.stopReason);
    TEST_ASSERT_TRUE(a.clearAudioActive);
    TEST_ASSERT_FALSE(a.drainQueue);
    TEST_ASSERT_FALSE(s.randomMode);

    // Still sleeping: no repeated transition.
    a = audioStepTick(s, {true, true, 20});
    TEST_ASSERT_FALSE(a.stopDriver);
    TEST_ASSERT_FALSE(a.clearAudioActive);
}

// --- init retry ceiling --------------------------------------------------------

void test_init_requested_with_config_volume_until_success() {
    AudioStepState s{};
    AudioStepTickActions a = audioStepTick(s, {true, false, 25});
    TEST_ASSERT_TRUE(a.initDriver);
    TEST_ASSERT_EQUAL_UINT8(25, s.currentVol);

    AudioStepInitResultActions ir = audioStepInitResult(s, true, true);
    TEST_ASSERT_FALSE(ir.skipRestOfTick);
    TEST_ASSERT_TRUE(ir.refreshBindings);
    TEST_ASSERT_TRUE(ir.seedModuleState);
    TEST_ASSERT_TRUE(s.driverInitialized);

    // Next tick no longer requests init.
    a = audioStepTick(s, {true, false, 25});
    TEST_ASSERT_FALSE(a.initDriver);
}

void test_init_retry_ceiling_gives_up_inoperative() {
    AudioStepState s{};
    audioStepTick(s, {true, false, 20});
    for (uint8_t i = 1; i < AUDIO_STEP_INIT_MAX_RETRIES; ++i) {
        AudioStepInitResultActions ir = audioStepInitResult(s, false, false);
        TEST_ASSERT_TRUE(ir.skipRestOfTick);
        TEST_ASSERT_FALSE(ir.giveUp);
        TEST_ASSERT_FALSE(s.driverInitialized);
    }
    AudioStepInitResultActions ir = audioStepInitResult(s, false, false);
    TEST_ASSERT_TRUE(ir.skipRestOfTick);
    TEST_ASSERT_TRUE(ir.giveUp);
    TEST_ASSERT_TRUE(s.driverInitialized);
    TEST_ASSERT_EQUAL_UINT8(0, s.beginRetryCount);
}

// --- command: sleep gates -------------------------------------------------------

void test_play_commands_sleep_gated_stop_and_volume_are_not() {
    AudioStepState s = initializedState();
    AudioPlaybackConfig cfg = playbackConfig();
    AudioNamedTracks named{};
    AudioBindingCache bindings{};
    AudioStepCommandInputs in = commandInputs(&cfg, &named, &bindings, 1000, true);

    AudioCommand play{};
    play.type = AUDIO_CMD_PLAY_TRACK;
    play.track = 5;
    AudioStepCommandActions a = audioStepCommand(s, in, play);
    TEST_ASSERT_EQUAL(AUDIO_STEP_IGNORE_SLEEP, a.ignored);
    TEST_ASSERT_FALSE(a.hasIntent);

    AudioCommand vol{};
    vol.type = AUDIO_CMD_SET_VOLUME;
    vol.volume = 12;
    a = audioStepCommand(s, in, vol);
    TEST_ASSERT_EQUAL(AUDIO_STEP_IGNORE_NONE, a.ignored);
    TEST_ASSERT_TRUE(a.hasIntent);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_SET_VOLUME, a.intent.kind);
    TEST_ASSERT_EQUAL_UINT8(12, s.currentVol);
}

void test_direct_track_resolves_and_bumps_cadence() {
    AudioStepState s = initializedState();
    AudioPlaybackConfig cfg = playbackConfig();
    AudioNamedTracks named{};
    AudioBindingCache bindings{};
    AudioStepCommandInputs in = commandInputs(&cfg, &named, &bindings, 1000, false);

    AudioCommand play{};
    play.type = AUDIO_CMD_PLAY_TRACK;
    play.track = 5;
    AudioStepCommandActions a = audioStepCommand(s, in, play);
    TEST_ASSERT_TRUE(a.hasIntent);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_PLAY_FLAT, a.intent.kind);
    TEST_ASSERT_EQUAL_UINT16(5, a.intent.track);
    TEST_ASSERT_EQUAL_UINT32(1000, s.lastPlayMs);
}

// --- command: relative volume ----------------------------------------------------

void test_dollar_volume_up_clamps_at_max() {
    AudioStepState s = initializedState();
    s.currentVol = 30;
    AudioPlaybackConfig cfg = playbackConfig();
    AudioNamedTracks named{};
    AudioBindingCache bindings{};
    AudioStepCommandInputs in = commandInputs(&cfg, &named, &bindings, 1000, false);

    AudioCommand cmd{};
    cmd.type = AUDIO_CMD_DOLLAR;
    strcpy(cmd.dollar, "$+");
    AudioStepCommandActions a = audioStepCommand(s, in, cmd);
    TEST_ASSERT_TRUE(a.hasIntent);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_SET_VOLUME, a.intent.kind);
    TEST_ASSERT_EQUAL_UINT8(30, a.intent.volume);
    TEST_ASSERT_EQUAL_UINT8(30, s.currentVol);
}

void test_dollar_volume_down_clamps_at_min() {
    AudioStepState s = initializedState();
    s.currentVol = AUDIO_VOLUME_MIN;
    AudioPlaybackConfig cfg = playbackConfig();
    AudioNamedTracks named{};
    AudioBindingCache bindings{};
    AudioStepCommandInputs in = commandInputs(&cfg, &named, &bindings, 1000, false);

    AudioCommand cmd{};
    cmd.type = AUDIO_CMD_DOLLAR;
    strcpy(cmd.dollar, "$-");
    AudioStepCommandActions a = audioStepCommand(s, in, cmd);
    TEST_ASSERT_TRUE(a.hasIntent);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_SET_VOLUME, a.intent.kind);
    TEST_ASSERT_EQUAL_UINT8(AUDIO_VOLUME_MIN, a.intent.volume);
}

// --- command: Track Stop vs Quiet (ADR 0010) -------------------------------------

void test_track_stop_preserves_random_mode_and_bumps_cadence() {
    AudioStepState s = initializedState();
    s.randomMode = true;
    AudioNamedTracks named{};
    AudioStepCommandInputs in = commandInputs(nullptr, &named, nullptr, 2000, false);

    AudioCommand cmd{};
    cmd.type = AUDIO_CMD_TRACK_STOP;
    AudioStepCommandActions a = audioStepCommand(s, in, cmd);
    TEST_ASSERT_TRUE(a.hasIntent);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_TRACK_STOP, a.intent.kind);
    TEST_ASSERT_TRUE(s.randomMode);
    TEST_ASSERT_EQUAL_UINT32(2000, s.lastPlayMs);
}

void test_quiet_stop_disables_random_mode() {
    AudioStepState s = initializedState();
    s.randomMode = true;
    AudioNamedTracks named{};
    AudioStepCommandInputs in = commandInputs(nullptr, &named, nullptr, 2000, false);

    AudioCommand cmd{};
    cmd.type = AUDIO_CMD_STOP;
    AudioStepCommandActions a = audioStepCommand(s, in, cmd);
    TEST_ASSERT_TRUE(a.hasIntent);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_STOP, a.intent.kind);
    TEST_ASSERT_FALSE(s.randomMode);
}

// --- command: catalog gating ------------------------------------------------------

void test_catalog_commands_gated_on_capability() {
    AudioStepState s = initializedState();
    AudioNamedTracks named{};
    AudioStepCommandInputs in = commandInputs(nullptr, &named, nullptr, 1000, false);

    AudioCommand cmd{};
    cmd.type = AUDIO_CMD_REFRESH_CATALOG;
    AudioStepCommandActions a = audioStepCommand(s, in, cmd);
    TEST_ASSERT_EQUAL(AUDIO_STEP_IGNORE_UNSUPPORTED_BACKEND, a.ignored);
    TEST_ASSERT_FALSE(a.refreshCatalog);

    in.catalogCapable = true;
    a = audioStepCommand(s, in, cmd);
    TEST_ASSERT_TRUE(a.refreshCatalog);
    TEST_ASSERT_EQUAL(AUDIO_STEP_IGNORE_NONE, a.ignored);

    cmd.type = AUDIO_CMD_REFRESH_BINDINGS;
    a = audioStepCommand(s, in, cmd);
    TEST_ASSERT_TRUE(a.refreshBindings);
}

// --- idle: random tick --------------------------------------------------------------

void test_random_tick_gated_on_mode_and_sleep() {
    AudioStepState s = initializedState();
    AudioPlaybackConfig cfg = playbackConfig();
    AudioBindingCache bindings{};

    AudioStepIdleInputs in{};
    in.nowMs = 20000;  // beyond the 10 s awake interval
    in.playback = &cfg;
    in.bindings = &bindings;

    AudioStepIdleActions a = audioStepIdle(s, in);
    TEST_ASSERT_FALSE(a.hasIntent);  // randomMode off

    s.randomMode = true;
    in.sleepMode = true;
    a = audioStepIdle(s, in);
    TEST_ASSERT_FALSE(a.hasIntent);  // sleeping

    in.sleepMode = false;
    a = audioStepIdle(s, in);
    TEST_ASSERT_TRUE(a.hasIntent);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_PLAY_FLAT, a.intent.kind);
    TEST_ASSERT_TRUE(a.intent.track >= 100 && a.intent.track <= 105);
    TEST_ASSERT_EQUAL_UINT32(20000, s.lastRandMs);
}

void test_random_tick_does_not_replay_before_interval() {
    AudioStepState s = initializedState();
    s.randomMode = true;

    AudioPlaybackConfig cfg = playbackConfig();
    cfg.moodMasks.full = (uint16_t)(1U << AUDIO_CATEGORY_PROCESSING);
    cfg.categoryRanges[AUDIO_CATEGORY_PROCESSING] = {8, 8};

    AudioBindingCache bindings{};
    bindings.categories[AUDIO_CATEGORY_PROCESSING].valid = true;
    bindings.categories[AUDIO_CATEGORY_PROCESSING].bank = 2;
    bindings.categories[AUDIO_CATEGORY_PROCESSING].page = 'E';

    AudioStepIdleInputs in{};
    in.nowMs = 20000;
    in.catalogCapable = true;
    in.activeMood = 11;
    in.randomValue = 0;
    in.playback = &cfg;
    in.bindings = &bindings;

    AudioStepIdleActions a = audioStepIdle(s, in);
    TEST_ASSERT_TRUE(a.hasIntent);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_PLAY_BANKED, a.intent.kind);
    TEST_ASSERT_EQUAL_UINT16(8, a.intent.index);
    TEST_ASSERT_EQUAL_UINT8(2, a.intent.bank);
    TEST_ASSERT_EQUAL('E', a.intent.page);
    TEST_ASSERT_EQUAL_UINT32(20000, s.lastRandMs);

    in.nowMs = 20001;
    a = audioStepIdle(s, in);
    TEST_ASSERT_TRUE(a.hasIntent);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_NONE, a.intent.kind);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_NONE_INTERVAL_NOT_READY, a.intent.reason);
    TEST_ASSERT_EQUAL_UINT32(20000, s.lastRandMs);

    in.nowMs = 29999;
    a = audioStepIdle(s, in);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_NONE, a.intent.kind);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_NONE_INTERVAL_NOT_READY, a.intent.reason);
    TEST_ASSERT_EQUAL_UINT32(20000, s.lastRandMs);

    in.nowMs = 30000;
    a = audioStepIdle(s, in);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_PLAY_BANKED, a.intent.kind);
    TEST_ASSERT_EQUAL_UINT32(30000, s.lastRandMs);
}

// --- idle: auto-query cadence ---------------------------------------------------------

void test_auto_query_cadence_and_ota_gate() {
    AudioStepState s = initializedState();
    AudioStepIdleInputs in{};
    in.querySafePlayingCapable = true;
    in.nowMs = AUDIO_STEP_AUTO_QUERY_INTERVAL_MS;

    AudioStepIdleActions a = audioStepIdle(s, in);
    TEST_ASSERT_TRUE(a.autoQuery);
    TEST_ASSERT_EQUAL_UINT32(in.nowMs, s.lastAutoQueryMs);

    in.nowMs += AUDIO_STEP_AUTO_QUERY_INTERVAL_MS / 2;
    a = audioStepIdle(s, in);
    TEST_ASSERT_FALSE(a.autoQuery);  // interval not elapsed

    in.nowMs += AUDIO_STEP_AUTO_QUERY_INTERVAL_MS / 2;
    in.webOtaActive = true;
    a = audioStepIdle(s, in);
    TEST_ASSERT_FALSE(a.autoQuery);  // OTA in progress

    in.webOtaActive = false;
    a = audioStepIdle(s, in);
    TEST_ASSERT_TRUE(a.autoQuery);

    in.querySafePlayingCapable = false;
    in.nowMs += AUDIO_STEP_AUTO_QUERY_INTERVAL_MS;
    a = audioStepIdle(s, in);
    TEST_ASSERT_FALSE(a.autoQuery);  // unsafe-polling module
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_disable_transition_stops_driver_and_clears_random_once);
    RUN_TEST(test_disabled_uninitialized_driver_is_never_stopped);
    RUN_TEST(test_sleep_entry_transition_fires_once);
    RUN_TEST(test_init_requested_with_config_volume_until_success);
    RUN_TEST(test_init_retry_ceiling_gives_up_inoperative);
    RUN_TEST(test_play_commands_sleep_gated_stop_and_volume_are_not);
    RUN_TEST(test_direct_track_resolves_and_bumps_cadence);
    RUN_TEST(test_dollar_volume_up_clamps_at_max);
    RUN_TEST(test_dollar_volume_down_clamps_at_min);
    RUN_TEST(test_track_stop_preserves_random_mode_and_bumps_cadence);
    RUN_TEST(test_quiet_stop_disables_random_mode);
    RUN_TEST(test_catalog_commands_gated_on_capability);
    RUN_TEST(test_random_tick_gated_on_mode_and_sleep);
    RUN_TEST(test_random_tick_does_not_replay_before_interval);
    RUN_TEST(test_auto_query_cadence_and_ota_gate);
    return UNITY_END();
}
