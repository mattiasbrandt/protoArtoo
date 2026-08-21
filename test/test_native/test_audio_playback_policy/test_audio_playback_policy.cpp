#include <unity.h>

#include "audio_playback_policy.h"

void setUp() {}
void tearDown() {}

static AudioPlaybackConfig baseConfig() {
    AudioPlaybackConfig cfg{};
    cfg.slotTracks[AUDIO_SLOT_NAMED_SCREAM] = 42;
    cfg.slotTracks[AUDIO_SLOT_NAMED_FAINT] = 43;
    cfg.randMin = 100;
    cfg.randMax = 105;
    cfg.intervalQuietS = 10;
    cfg.intervalMidS = 20;
    cfg.intervalFullS = 30;
    cfg.intervalAwakeS = 40;
    cfg.moodMasks = {0, 0, (uint16_t)(1U << AUDIO_CATEGORY_HAPPY), 0};
    cfg.categoryRanges[AUDIO_CATEGORY_HAPPY] = {7, 9};
    cfg.categoryRanges[AUDIO_CATEGORY_SCREAM] = {0, 0};
    return cfg;
}

void test_slot_prefers_valid_chirp_binding_when_catalog_capable() {
    AudioPlaybackConfig cfg = baseConfig();
    AudioBindingCache bindings{};
    bindings.slots[AUDIO_SLOT_NAMED_SCREAM] = {true, 3, 'b', 12};

    AudioPlaybackContext ctx{&cfg, &bindings, true, 1000, 0};
    AudioPlaybackRequest req{};
    req.kind = AUDIO_PLAYBACK_REQ_SLOT;
    req.slot = AUDIO_SLOT_NAMED_SCREAM;

    AudioPlaybackIntent intent = audioPlaybackResolveRequest(ctx, req);

    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_PLAY_BANKED, intent.kind);
    TEST_ASSERT_EQUAL_UINT16(12, intent.index);
    TEST_ASSERT_EQUAL_UINT8(3, intent.bank);
    TEST_ASSERT_EQUAL('B', intent.page);
    TEST_ASSERT_TRUE(intent.markAudioActive);
    TEST_ASSERT_TRUE(intent.updateLastPlayMs);
    TEST_ASSERT_TRUE(intent.updateLastRandMs);
}

void test_track_stop_resolves_with_clear_audio_active_and_cadence_bump() {
    AudioPlaybackContext ctx{nullptr, nullptr, false, 1000, 0};
    AudioPlaybackRequest req{};
    req.kind = AUDIO_PLAYBACK_REQ_TRACK_STOP;

    AudioPlaybackIntent intent = audioPlaybackResolveRequest(ctx, req);

    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_TRACK_STOP, intent.kind);
    TEST_ASSERT_TRUE(intent.clearAudioActive);
    TEST_ASSERT_TRUE(intent.updateLastPlayMs);
}

void test_slot_invalid_chirp_binding_falls_back_to_flat_track() {
    AudioPlaybackConfig cfg = baseConfig();
    AudioBindingCache bindings{};
    bindings.slots[AUDIO_SLOT_NAMED_SCREAM] = {true, 0, 'A', 12};

    AudioPlaybackContext ctx{&cfg, &bindings, true, 1000, 0};
    AudioPlaybackRequest req{};
    req.kind = AUDIO_PLAYBACK_REQ_SLOT;
    req.slot = AUDIO_SLOT_NAMED_SCREAM;

    AudioPlaybackIntent intent = audioPlaybackResolveRequest(ctx, req);

    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_PLAY_FLAT, intent.kind);
    TEST_ASSERT_EQUAL_UINT16(42, intent.track);
    TEST_ASSERT_EQUAL(AUDIO_SLOT_NAMED_SCREAM, intent.slot);
}

void test_category_empty_uses_explicit_fallback_slot_not_global_random_range() {
    AudioPlaybackConfig cfg = baseConfig();
    AudioBindingCache bindings{};

    AudioPlaybackContext ctx{&cfg, &bindings, false, 1000, 0};
    AudioPlaybackRequest req{};
    req.kind = AUDIO_PLAYBACK_REQ_CATEGORY;
    req.categoryRequest.category = AUDIO_CATEGORY_SCREAM;
    req.categoryRequest.fallbackSlot = AUDIO_SLOT_NAMED_FAINT;
    req.categoryRequest.randomValue = 0;

    AudioPlaybackIntent intent = audioPlaybackResolveRequest(ctx, req);

    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_PLAY_FLAT, intent.kind);
    TEST_ASSERT_EQUAL_UINT16(43, intent.track);
    TEST_ASSERT_TRUE(intent.fallbackSlotUsed);
    TEST_ASSERT_EQUAL(AUDIO_CATEGORY_SCREAM, intent.category);
}

void test_category_empty_without_fallback_is_explicit_none() {
    AudioPlaybackConfig cfg = baseConfig();

    AudioPlaybackContext ctx{&cfg, nullptr, false, 1000, 0};
    AudioPlaybackRequest req{};
    req.kind = AUDIO_PLAYBACK_REQ_CATEGORY;
    req.categoryRequest.category = AUDIO_CATEGORY_SCREAM;
    req.categoryRequest.fallbackSlot = AUDIO_SLOT_NONE;

    AudioPlaybackIntent intent = audioPlaybackResolveRequest(ctx, req);

    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_NONE, intent.kind);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_NONE_CATEGORY_EMPTY, intent.reason);
    TEST_ASSERT_FALSE(intent.updateLastRandMs);
}

void test_direct_banked_normalizes_lowercase_page() {
    AudioPlaybackContext ctx{nullptr, nullptr, true, 1000, 0};
    AudioPlaybackRequest req{};
    req.kind = AUDIO_PLAYBACK_REQ_DIRECT_BANKED;
    req.banked.index = 55;
    req.banked.bank = 2;
    req.banked.page = 'c';

    AudioPlaybackIntent intent = audioPlaybackResolveRequest(ctx, req);

    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_PLAY_BANKED, intent.kind);
    TEST_ASSERT_EQUAL_UINT16(55, intent.index);
    TEST_ASSERT_EQUAL_UINT8(2, intent.bank);
    TEST_ASSERT_EQUAL('C', intent.page);
}

void test_command_play_antispam_has_no_timing_updates() {
    AudioPlaybackConfig cfg = baseConfig();
    AudioPlaybackContext ctx{&cfg, nullptr, false, 1200, 1000};
    AudioPlaybackRequest req{};
    req.kind = AUDIO_PLAYBACK_REQ_DIRECT_TRACK;
    req.track = 9;

    AudioPlaybackIntent intent = audioPlaybackResolveRequest(ctx, req);

    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_NONE, intent.kind);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_NONE_ANTI_SPAM, intent.reason);
    TEST_ASSERT_FALSE(intent.updateLastPlayMs);
    TEST_ASSERT_FALSE(intent.updateLastRandMs);
}

void test_random_tick_suppressed_during_dome_sequence_updates_random_timer_only() {
    AudioPlaybackConfig cfg = baseConfig();
    AudioPlaybackRandomContext ctx{&cfg, nullptr, false, true, true, 31000, 0, 11, 0};

    AudioPlaybackIntent intent = audioPlaybackResolveRandomTick(ctx);

    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_NONE, intent.kind);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_NONE_DOME_SEQUENCE_ACTIVE, intent.reason);
    TEST_ASSERT_TRUE(intent.updateLastRandMs);
    TEST_ASSERT_FALSE(intent.updateLastPlayMs);
    TEST_ASSERT_FALSE(intent.markAudioActive);
}

void test_random_tick_uses_category_chirp_binding_when_mood_mapping_selected_category() {
    AudioPlaybackConfig cfg = baseConfig();
    AudioBindingCache bindings{};
    bindings.categories[AUDIO_CATEGORY_HAPPY] = {true, 4, 'd'};
    AudioPlaybackRandomContext ctx{&cfg, &bindings, true, true, false, 31000, 0, 11, 1};

    AudioPlaybackIntent intent = audioPlaybackResolveRandomTick(ctx);

    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_PLAY_BANKED, intent.kind);
    TEST_ASSERT_EQUAL_UINT16(8, intent.index);
    TEST_ASSERT_EQUAL_UINT8(4, intent.bank);
    TEST_ASSERT_EQUAL('D', intent.page);
    TEST_ASSERT_EQUAL(AUDIO_CATEGORY_HAPPY, intent.category);
}

void test_random_tick_interval_prevents_rapid_reentry_when_lastrandms_advances() {
    // This test simulates the state update flow between consecutive random ticks.
    // Issue #51 suspected a tight loop where identical bank/page/index values repeat.
    // This would only happen if lastRandMs never advances, so the interval check always passes.
    // This test verifies that doesn't happen when the state is properly updated.

    AudioPlaybackConfig cfg = baseConfig();
    cfg.intervalFullS = 30;  // 30 second interval for full-awake mood
    AudioBindingCache bindings{};
    bindings.categories[AUDIO_CATEGORY_HAPPY] = {true, 4, 'd'};

    // First call: lastRandMs = 0, nowMs = 1000 (interval has elapsed: 1000 - 0 >= 30000 is false)
    // Actually, let me set up so the interval HAS elapsed
    uint32_t nowMs = 40000;  // 40 seconds into boot
    uint32_t lastRandMs = 0;  // First time, lastRandMs is 0

    AudioPlaybackRandomContext ctx1{&cfg, &bindings, true, true, false, nowMs, lastRandMs, 11, 1};
    AudioPlaybackIntent intent1 = audioPlaybackResolveRandomTick(ctx1);

    // First call should succeed with 30s interval: nowMs(40000) - lastRandMs(0) >= 30000 is true
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_PLAY_BANKED, intent1.kind);
    TEST_ASSERT_TRUE(intent1.updateLastRandMs);  // Should mark to update the timer

    // Simulate state update: lastRandMs becomes nowMs (40000)
    lastRandMs = nowMs;

    // Second call: 1ms later, same randomValue, should FAIL interval check
    nowMs = 40001;
    AudioPlaybackRandomContext ctx2{&cfg, &bindings, true, true, false, nowMs, lastRandMs, 11, 1};
    AudioPlaybackIntent intent2 = audioPlaybackResolveRandomTick(ctx2);

    // Second call should fail interval check: nowMs(40001) - lastRandMs(40000) < 30000
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_INTENT_NONE, intent2.kind);
    TEST_ASSERT_EQUAL(AUDIO_PLAYBACK_NONE_INTERVAL_NOT_READY, intent2.reason);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_slot_prefers_valid_chirp_binding_when_catalog_capable);
    RUN_TEST(test_track_stop_resolves_with_clear_audio_active_and_cadence_bump);
    RUN_TEST(test_slot_invalid_chirp_binding_falls_back_to_flat_track);
    RUN_TEST(test_category_empty_uses_explicit_fallback_slot_not_global_random_range);
    RUN_TEST(test_category_empty_without_fallback_is_explicit_none);
    RUN_TEST(test_direct_banked_normalizes_lowercase_page);
    RUN_TEST(test_command_play_antispam_has_no_timing_updates);
    RUN_TEST(test_random_tick_suppressed_during_dome_sequence_updates_random_timer_only);
    RUN_TEST(test_random_tick_uses_category_chirp_binding_when_mood_mapping_selected_category);
    RUN_TEST(test_random_tick_interval_prevents_rapid_reentry_when_lastrandms_advances);
    return UNITY_END();
}
