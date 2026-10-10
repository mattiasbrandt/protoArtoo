// =============================================================================
// test/test_native/test_reaction_evaluator/test_reaction_evaluator.cpp
//
// When a Reaction fires (include/reaction_evaluator.h), through
// reactionEvaluatorTick() only: its quiet period, the droid-wide floor, a held
// press released when its binding is no longer read, and a Reaction's state
// kept by its place's change stamp (ADR 0070, amended 2026-10-10, #490).
//
// The condition is RC_BINDING_DROID_SPEED - the resolved drive speed at or
// over the threshold - because it needs nothing but ReactionInputs.driveSpeed
// and the gate open. No firing here opens a body Part.
// =============================================================================

#include <unity.h>

#include <string.h>

#include "rc_binding_types.h"  // disabledRcTriggerBinding()
#include "reaction_evaluator.h"

namespace {

constexpr uint16_t kThreshold = 100;

struct Rig {
    ReactionEvaluator ev;
    RcTriggerBinding bindings[REACTION_SLOT_MAX];
    uint16_t stamps[REACTION_SLOT_MAX];
    ReactionOutput out;
    uint32_t nowMs;

    void begin() {
        reactionEvaluatorInit(&ev);
        for (size_t i = 0; i < REACTION_SLOT_MAX; ++i) {
            bindings[i] = disabledRcTriggerBinding();
            stamps[i] = 1;
        }
        out = {};
        nowMs = 1000;
    }

    // One tick at this drive speed, REACTION_TICK_MS after the last. Every
    // press the action door is handed counts as carried out.
    void tick(int16_t speed) {
        ReactionInputs in = {};
        in.nowMs = nowMs;
        in.driveSpeed = speed;
        in.audioPlayState = 0;
        reactionEvaluatorTick(
            &ev, bindings, stamps, REACTION_SLOT_MAX, in,
            [](RobotActionId, const char*) { return false; }, &out);
        for (uint8_t i = 0; i < out.count; ++i) {
            reactionEvaluatorFired(&ev, out.firings[i]);
        }
        nowMs += REACTION_TICK_MS;
    }

    // Ticks at this speed until `ms` have passed.
    void hold(int16_t speed, uint32_t ms) {
        for (uint32_t spent = 0; spent < ms; spent += REACTION_TICK_MS) {
            tick(speed);
        }
    }

    bool pressed(size_t slot) const {
        for (uint8_t i = 0; i < out.count; ++i) {
            if (out.firings[i].slot == slot && out.firings[i].pressed) {
                return true;
            }
        }
        return false;
    }
};

RcTriggerBinding speedReaction(RobotActionId target, uint16_t threshold, uint16_t quietS) {
    return makeRcReactionBinding(RC_BINDING_DROID_SPEED, 1, target, nullptr, threshold, quietS);
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

// A Reaction fires once on its condition's rise, and again only after its own
// quiet period: a rise inside it - past the droid-wide floor - fires nothing.
void test_a_reaction_waits_out_its_quiet_period(void) {
    Rig rig;
    rig.begin();
    rig.bindings[0] = speedReaction(SOUND_ACTION_RANDOM_HAPPY, kThreshold, 2);
    rig.tick(0);  // armed, at rest
    rig.tick(200);
    TEST_ASSERT_TRUE_MESSAGE(rig.pressed(0), "the first rise fires");

    rig.hold(0, 500);  // past the floor, inside the 2 s quiet period
    rig.tick(200);
    TEST_ASSERT_FALSE_MESSAGE(rig.pressed(0), "a rise inside the quiet period fired");

    rig.hold(0, 2000);
    rig.tick(200);
    TEST_ASSERT_TRUE_MESSAGE(rig.pressed(0), "a rise after the quiet period fires");
    TEST_ASSERT_EQUAL_UINT16(2, rig.ev.slots[0].fires);
}

// Any two Reactions are REACTION_FLOOR_MS apart, whichever two: a second
// Reaction rising inside the floor fires nothing, and rising after it fires.
void test_two_reactions_keep_the_droid_wide_floor(void) {
    Rig rig;
    rig.begin();
    rig.bindings[0] = speedReaction(SOUND_ACTION_RANDOM_HAPPY, kThreshold, 1);
    rig.bindings[1] = speedReaction(SOUND_ACTION_RANDOM_SAD, 3 * kThreshold, 1);
    rig.tick(0);
    rig.tick(2 * kThreshold);
    TEST_ASSERT_TRUE(rig.pressed(0));
    rig.tick(4 * kThreshold);  // REACTION_TICK_MS later: inside the floor
    TEST_ASSERT_FALSE_MESSAGE(rig.pressed(1), "a second Reaction fired inside the floor");

    rig.hold(2 * kThreshold, REACTION_FLOOR_MS);  // the second's condition clears
    rig.tick(4 * kThreshold);
    TEST_ASSERT_TRUE_MESSAGE(rig.pressed(1), "the second Reaction fires once the floor passed");
}

// A switch action holds its press while the condition holds. When the RC Map's
// rules leave the binding still - its stamp unchanged - the press is released,
// for the action it pressed.
void test_a_held_press_is_released_when_its_binding_is_no_longer_read(void) {
    Rig rig;
    rig.begin();
    rig.bindings[0] = speedReaction(SERVO_ACTION_ARM1_TOGGLE, kThreshold, 1);
    rig.tick(0);
    rig.tick(200);
    TEST_ASSERT_TRUE(rig.pressed(0));
    TEST_ASSERT_TRUE(rig.ev.slots[0].pressHeld);

    rig.bindings[0] = disabledRcTriggerBinding();
    rig.tick(200);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, rig.out.count, "the held press is released");
    TEST_ASSERT_FALSE(rig.out.firings[0].pressed);
    TEST_ASSERT_EQUAL_UINT8(0, rig.out.firings[0].slot);
    TEST_ASSERT_EQUAL(SERVO_ACTION_ARM1_TOGGLE, rig.out.firings[0].target);
}

// A Reaction whose place's stamp moved was edited - here saved again with
// nothing different to see, so only the stamp says so - and starts afresh: its
// quiet period and its counts go, and its next rise fires.
void test_an_edited_reaction_starts_afresh(void) {
    Rig rig;
    rig.begin();
    rig.bindings[0] = speedReaction(SOUND_ACTION_RANDOM_HAPPY, kThreshold, 60);
    rig.tick(0);
    rig.tick(200);
    TEST_ASSERT_TRUE(rig.pressed(0));
    rig.hold(0, 500);

    rig.stamps[0]++;
    rig.tick(0);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, rig.ev.slots[0].fires, "an edited Reaction kept its counts");
    rig.tick(200);
    TEST_ASSERT_TRUE_MESSAGE(rig.pressed(0), "an edited Reaction is still in its quiet period");
}

// A save that leaves a Reaction unchanged keeps it in its place with its stamp,
// while another place's stamp moves: it keeps its quiet period and its counts.
void test_an_unchanged_reaction_keeps_its_quiet_period_across_a_save(void) {
    Rig rig;
    rig.begin();
    rig.bindings[0] = speedReaction(SOUND_ACTION_RANDOM_HAPPY, kThreshold, 60);
    rig.tick(0);
    rig.tick(200);
    TEST_ASSERT_TRUE(rig.pressed(0));
    rig.hold(0, 500);

    // The save: a radio cue added in the next place.
    rig.bindings[1] = makeRcTriggerBinding(RC_BINDING_SBUS1, 5, SOUND_ACTION_RANDOM_SAD, nullptr,
                                           RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                                           RC_SBUS_DEFAULT_MAX, 0, false);
    rig.stamps[1]++;
    rig.hold(0, 500);
    rig.tick(200);
    TEST_ASSERT_FALSE_MESSAGE(rig.pressed(0), "an unchanged Reaction lost its quiet period");
    TEST_ASSERT_EQUAL_UINT16(1, rig.ev.slots[0].fires);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_reaction_waits_out_its_quiet_period);
    RUN_TEST(test_two_reactions_keep_the_droid_wide_floor);
    RUN_TEST(test_a_held_press_is_released_when_its_binding_is_no_longer_read);
    RUN_TEST(test_an_edited_reaction_starts_afresh);
    RUN_TEST(test_an_unchanged_reaction_keeps_its_quiet_period_across_a_save);
    return UNITY_END();
}
