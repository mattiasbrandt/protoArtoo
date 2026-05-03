// =============================================================================
// test/test_native/test_rc_input_state_machines/test_rc_input_state_machines.cpp
//
// Native tests for pure RC input state machines extracted from RcInputTask.
// =============================================================================
#include <unity.h>

#include "dome_input_filter.h"
#include "rc_mapping_cache.h"
#include "trigger_debounce.h"

void setUp() {
}

void tearDown() {
}

static RcMappingConfig makeMappingConfig(int16_t maxOut) {
    RcMappingConfig cfg = {};
    cfg.maxOut = maxOut;
    cfg.enableRc[0] = true;
    cfg.driveSpeed = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    return cfg;
}

void test_mapping_cache_misses_when_empty() {
    RcMappingCache cache = {};
    RcMappingConfig out = {};

    TEST_ASSERT_FALSE(rcMappingCacheGet(cache, RC_INPUT_SINGLE_SBUS, &out));
    TEST_ASSERT_FALSE(rcMappingCacheIsDirty(cache));
}

void test_mapping_cache_hits_after_set() {
    RcMappingCache cache = {};
    RcMappingConfig cfg = makeMappingConfig(700);

    rcMappingCacheSet(&cache, RC_INPUT_SINGLE_SBUS, cfg);

    RcMappingConfig out = {};
    TEST_ASSERT_TRUE(rcMappingCacheGet(cache, RC_INPUT_SINGLE_SBUS, &out));
    TEST_ASSERT_EQUAL_INT16(700, out.maxOut);
    TEST_ASSERT_FALSE(rcMappingCacheIsDirty(cache));
}

void test_mapping_cache_invalidate_blocks_hit_until_reset() {
    RcMappingCache cache = {};
    rcMappingCacheSet(&cache, RC_INPUT_SINGLE_SBUS, makeMappingConfig(700));
    rcMappingCacheInvalidate(&cache);

    RcMappingConfig out = {};
    TEST_ASSERT_TRUE(rcMappingCacheIsDirty(cache));
    TEST_ASSERT_FALSE(rcMappingCacheGet(cache, RC_INPUT_SINGLE_SBUS, &out));

    rcMappingCacheSet(&cache, RC_INPUT_SINGLE_SBUS, makeMappingConfig(900));
    TEST_ASSERT_TRUE(rcMappingCacheGet(cache, RC_INPUT_SINGLE_SBUS, &out));
    TEST_ASSERT_EQUAL_INT16(900, out.maxOut);
}

static RcTriggerBinding makeToggleBinding() {
    return makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                                RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX,
                                0, false);
}

static RcTriggerBinding makeOneShotBinding() {
    return makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SOUND_ACTION_RANDOM_GENERAL, nullptr,
                                RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX,
                                0, false);
}

void test_trigger_debounce_holds_first_changed_frame() {
    TriggerDebounceState state = {};
    RcTriggerBinding binding = makeToggleBinding();

    TEST_ASSERT_FALSE(triggerDebounceAnalog(&state, binding, 992, 100, 2, 120).fired);
    TriggerDebounceResult result = triggerDebounceAnalog(&state, binding, 1811, 120, 2, 120);

    TEST_ASSERT_FALSE(result.fired);
    TEST_ASSERT_EQUAL_UINT8(1, state.pendingCount);
}

void test_trigger_debounce_fires_after_confirmed_change() {
    TriggerDebounceState state = {};
    RcTriggerBinding binding = makeToggleBinding();

    triggerDebounceAnalog(&state, binding, 992, 100, 2, 120);
    triggerDebounceAnalog(&state, binding, 1811, 120, 2, 120);
    TriggerDebounceResult result = triggerDebounceAnalog(&state, binding, 1811, 140, 2, 120);

    TEST_ASSERT_TRUE(result.fired);
    TEST_ASSERT_TRUE(result.pressed);
    TEST_ASSERT_EQUAL(RC_SWITCH_HIGH, state.lastSwitchState);
}

void test_trigger_debounce_repeat_guard_suppresses_fast_one_shot() {
    TriggerDebounceState state = {};
    RcTriggerBinding binding = makeOneShotBinding();

    triggerDebounceAnalog(&state, binding, 992, 100, 2, 120);
    triggerDebounceAnalog(&state, binding, 1811, 180, 2, 120);
    TEST_ASSERT_TRUE(triggerDebounceAnalog(&state, binding, 1811, 200, 2, 120).fired);

    triggerDebounceAnalog(&state, binding, 172, 230, 2, 120);
    TriggerDebounceResult result = triggerDebounceAnalog(&state, binding, 172, 250, 2, 120);

    TEST_ASSERT_FALSE(result.fired);
    TEST_ASSERT_EQUAL_UINT32(200, state.lastEdgeMs);
}

void test_trigger_debounce_digital_fires_once_per_edge() {
    TriggerDebounceState state = {};

    TEST_ASSERT_TRUE(triggerDebounceDigital(&state, true).fired);
    TEST_ASSERT_FALSE(triggerDebounceDigital(&state, true).fired);
    TriggerDebounceResult release = triggerDebounceDigital(&state, false);

    TEST_ASSERT_TRUE(release.fired);
    TEST_ASSERT_FALSE(release.pressed);
}

void test_dome_filter_accepts_initial_stable_sample() {
    DomeInputFilter filter = {};

    DomeInputFilterResult result = domeInputFilterUpdate(&filter, 992, 992, 140, 90, 2);

    TEST_ASSERT_TRUE(result.accepted);
    TEST_ASSERT_EQUAL_INT(992, filter.lastAcceptedRaw);
}

void test_dome_filter_holds_first_exit_from_neutral() {
    DomeInputFilter filter = {};
    domeInputFilterUpdate(&filter, 992, 992, 140, 90, 2);

    DomeInputFilterResult result = domeInputFilterUpdate(&filter, 1811, 992, 140, 90, 2);

    TEST_ASSERT_FALSE(result.accepted);
    TEST_ASSERT_EQUAL_UINT8(1, filter.pendingCount);
    TEST_ASSERT_EQUAL_INT(992, filter.lastAcceptedRaw);
}

void test_dome_filter_accepts_after_confirmed_exit() {
    DomeInputFilter filter = {};
    domeInputFilterUpdate(&filter, 992, 992, 140, 90, 2);
    domeInputFilterUpdate(&filter, 1811, 992, 140, 90, 2);

    DomeInputFilterResult result = domeInputFilterUpdate(&filter, 1811, 992, 140, 90, 2);

    TEST_ASSERT_TRUE(result.accepted);
    TEST_ASSERT_EQUAL_INT(1811, filter.lastAcceptedRaw);
}

void test_dome_filter_resets_pending_when_change_is_not_stable() {
    DomeInputFilter filter = {};
    domeInputFilterUpdate(&filter, 992, 992, 140, 90, 2);
    domeInputFilterUpdate(&filter, 1500, 992, 140, 90, 2);

    DomeInputFilterResult result = domeInputFilterUpdate(&filter, 1811, 992, 140, 90, 2);

    TEST_ASSERT_FALSE(result.accepted);
    TEST_ASSERT_EQUAL_UINT8(1, filter.pendingCount);
    TEST_ASSERT_EQUAL_INT(1811, filter.pendingRaw);
    TEST_ASSERT_EQUAL_INT(992, filter.lastAcceptedRaw);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_mapping_cache_misses_when_empty);
    RUN_TEST(test_mapping_cache_hits_after_set);
    RUN_TEST(test_mapping_cache_invalidate_blocks_hit_until_reset);
    RUN_TEST(test_trigger_debounce_holds_first_changed_frame);
    RUN_TEST(test_trigger_debounce_fires_after_confirmed_change);
    RUN_TEST(test_trigger_debounce_repeat_guard_suppresses_fast_one_shot);
    RUN_TEST(test_trigger_debounce_digital_fires_once_per_edge);
    RUN_TEST(test_dome_filter_accepts_initial_stable_sample);
    RUN_TEST(test_dome_filter_holds_first_exit_from_neutral);
    RUN_TEST(test_dome_filter_accepts_after_confirmed_exit);
    RUN_TEST(test_dome_filter_resets_pending_when_change_is_not_stable);
    return UNITY_END();
}
