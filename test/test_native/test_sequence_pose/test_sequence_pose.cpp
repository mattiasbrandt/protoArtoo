// =============================================================================
// test/test_native/test_sequence_pose/test_sequence_pose.cpp
//
// Send the droid to one moment of a saved routine (#440, include/sequence_pose.h).
//
// What these hold is what the browser must never be able to reach: which
// commands the pose at an instant is made of -- one per Part, never a group --
// and how far apart the Coordinator sends them. The pose is built from real
// steps with the catalog's own macros, so the loop arithmetic under test is
// the engine's.
// =============================================================================
#include <unity.h>

#include <string.h>

#include "sequence_pose.h"

void setUp() {}
void tearDown() {}

static SeqPosePlan plan;

static const SeqPoseCmd* find(const char* payload) {
    for (uint8_t i = 0; i < plan.count; ++i) {
        if (strcmp(plan.cmds[i].act.payload, payload) == 0) return &plan.cmds[i];
    }
    return nullptr;
}

static uint8_t countOf(uint8_t cls) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < plan.count; ++i) {
        if (plan.cmds[i].cls == cls) ++n;
    }
    return n;
}

// A loop opening ring panel 1 at the start of each pass and closing it half way
// through: pass three opens it at 2100 ms and closes it at 2600 ms. Then a pie
// opened and never closed, and the end at 3600 ms.
static const SeqStep kPasses[] = {
    SEQ_LOOP(100, 2, 1000, 3000),
    SEQ_DOME(0, FX_PANEL, ":OP01"),
    SEQ_DOME(500, FX_PANEL, ":CL01"),
    SEQ_DOME(3200, FX_PANEL, ":OPP1"),
    SEQ_TERM(3600),
};
static const uint8_t kPassesCount = sizeof(kPasses) / sizeof(kPasses[0]);

// -----------------------------------------------------------------------------
// What the pose is made of
// -----------------------------------------------------------------------------

// The engine's loop arithmetic: a pass's steps are timed from the pass start.
void test_a_pose_inside_a_loop_takes_the_pass_it_falls_in() {
    sequencePosePlan(kPasses, kPassesCount, false, 2300, &plan);
    TEST_ASSERT_NOT_NULL(find(":OP01"));
    TEST_ASSERT_NULL(find(":CL01"));

    sequencePosePlan(kPasses, kPassesCount, false, 2800, &plan);
    TEST_ASSERT_NOT_NULL(find(":CL01"));
    TEST_ASSERT_NULL(find(":OP01"));
}

// A Part the routine has not yet moved by the instant is not commanded.
void test_nothing_the_routine_has_not_yet_moved_is_commanded() {
    sequencePosePlan(kPasses, kPassesCount, false, 50, &plan);
    TEST_ASSERT_EQUAL_UINT8(0, plan.count);

    sequencePosePlan(kPasses, kPassesCount, false, 2300, &plan);
    TEST_ASSERT_NULL(find(":OPP1"));  // the pie opens at 3200
}

// Never a group command: :OP00 is every panel, one command each.
void test_a_group_target_becomes_one_command_per_panel() {
    static const SeqStep steps[] = {
        SEQ_DOME(0, FX_PANEL, ":OP00"),
        SEQ_DOME(100, FX_PANEL, ":CL15"),
        SEQ_TERM(1000),
    };
    sequencePosePlan(steps, 3, false, 500, &plan);

    TEST_ASSERT_EQUAL_UINT8(seqEnginePanelTargetCount(), countOf(SEQ_POSE_PANEL));
    for (uint8_t i = 0; i < plan.count; ++i) {
        const char* target = plan.cmds[i].act.payload + 3;
        TEST_ASSERT_TRUE_MESSAGE(strcmp(target, "00") != 0 && strcmp(target, "14") != 0 &&
                                     strcmp(target, "15") != 0,
                                 plan.cmds[i].act.payload);
    }
    // The ring close came later and wins for the ring; the pies stay open.
    TEST_ASSERT_NOT_NULL(find(":CL01"));
    TEST_ASSERT_NOT_NULL(find(":CL13"));
    TEST_ASSERT_NOT_NULL(find(":OPP1"));
    TEST_ASSERT_NOT_NULL(find(":OPP6"));
}

// Past the end the engine closes ring panels a run left open and leaves pies
// where they are; a toggle's open half keeps its ring panels open.
void test_past_the_end_the_ring_is_closed_and_the_pies_stay() {
    static const SeqStep steps[] = {
        SEQ_DOME(0, FX_PANEL, ":OP02"),
        SEQ_DOME(0, FX_PANEL, ":OPP2"),
        SEQ_DOME(0, FX_LOGIC_PSI, "DL:FLD:ALARM"),
        SEQ_AUDIO(0, "$C"),
        SEQ_TERM(1000),
    };
    sequencePosePlan(steps, 5, false, 900, &plan);
    TEST_ASSERT_NOT_NULL(find(":OP02"));
    TEST_ASSERT_NOT_NULL(find("$C"));

    sequencePosePlan(steps, 5, false, 1500, &plan);
    TEST_ASSERT_NOT_NULL(find(":CL02"));
    TEST_ASSERT_NOT_NULL(find(":OPP2"));
    TEST_ASSERT_EQUAL_UINT8(0, countOf(SEQ_POSE_INSTANT));  // lights reset, the show is over

    sequencePosePlan(steps, 5, true, 1500, &plan);
    TEST_ASSERT_NOT_NULL(find(":OP02"));
}

// Everything at that instant: the sound that would be playing, from its start,
// and each light in its mode then -- a light whose own time ran out is not.
void test_sound_and_lights_are_the_ones_running_at_the_instant() {
    static const SeqStep steps[] = {
        SEQ_AUDIO(0, "$A"),
        SEQ_DOME(0, FX_LOGIC_PSI, "DL:LOGIC:ALARM"),
        SEQ_DOME(200, FX_LOGIC_PSI, "DL:RLD:LEIA"),
        SEQ_DOME(300, FX_LOGIC_PSI, "DL:FPSI:MARCH:RED:1"),
        SEQ_AUDIO(400, "$C"),
        SEQ_TERM(5000),
    };
    sequencePosePlan(steps, 6, false, 1000, &plan);
    TEST_ASSERT_NULL(find("$A"));
    TEST_ASSERT_NOT_NULL(find("$C"));
    TEST_ASSERT_NOT_NULL(find("DL:LOGIC:ALARM"));   // still the front logic's
    TEST_ASSERT_NOT_NULL(find("DL:RLD:LEIA"));      // the rear's own
    TEST_ASSERT_NOT_NULL(find("DL:FPSI:MARCH:RED:1"));
    // DL:LOGIC goes before DL:RLD, or the rear logic would end on ALARM.
    const SeqPoseCmd* logic = find("DL:LOGIC:ALARM");
    const SeqPoseCmd* rear = find("DL:RLD:LEIA");
    TEST_ASSERT_TRUE(logic < rear);

    sequencePosePlan(steps, 6, false, 1400, &plan);
    TEST_ASSERT_NULL(find("DL:FPSI:MARCH:RED:1"));  // its one second is over
}

// What the routine does not say is not sent: a flutter's end position, a
// random pick, a quiet command.
void test_a_flutter_a_random_pick_and_quiet_are_not_sent() {
    static const SeqStep steps[] = {
        SEQ_DOME(0, FX_PANEL, ":OP03"),
        SEQ_DOME(100, FX_PANEL, ":OF03"),
        SEQ_RAND(200, SLOTSET_RING, RAND_OPEN, 0, 300, 0, 0),
        SEQ_AUDIO(0, "$C"),
        SEQ_AUDIO(300, "$s"),
        SEQ_TERM(1000),
    };
    sequencePosePlan(steps, 6, false, 500, &plan);
    TEST_ASSERT_EQUAL_UINT8(0, plan.count);
}

// A body Part goes to where its last step left it, in the step's own shape.
void test_a_body_part_takes_its_last_step() {
    static const SeqStep steps[] = {
        SEQ_BODY(0, "doorFL", BODY_SHAPE_OPEN, 0, 0),
        SEQ_BODY(600, "doorFL", BODY_SHAPE_CLOSE, 40, 0),
        SEQ_BODY(600, "utilUp", BODY_SHAPE_OPEN, 0, 0),
        SEQ_TERM(1000),
    };
    sequencePosePlan(steps, 4, false, 700, &plan);
    TEST_ASSERT_EQUAL_UINT8(2, countOf(SEQ_POSE_BODY));
    const SeqPoseCmd* door = find("doorFL");
    TEST_ASSERT_NOT_NULL(door);
    TEST_ASSERT_EQUAL_UINT8(BODY_SHAPE_CLOSE, door->act.bodyShape);
    TEST_ASSERT_EQUAL_UINT8(40, door->act.bodyHowFar);
}

// -----------------------------------------------------------------------------
// How it is paced
// -----------------------------------------------------------------------------

// Every panel open and a body Part with a slow throw: the Coordinator ticks
// every 10 ms and every command it may send goes. No two motions start inside
// the Cadence Floor, the body Output holds the next one off for its own throw,
// and the sound and lights go first, unspaced.
void test_a_pose_never_starts_two_motions_inside_the_cadence_floor() {
    static const SeqStep steps[] = {
        SEQ_AUDIO(0, "$C"),
        SEQ_DOME(0, FX_LOGIC_PSI, "DL:ALL:ALARM"),
        SEQ_DOME(0, FX_PANEL, ":OP00"),
        SEQ_BODY(0, "doorFL", BODY_SHAPE_OPEN, 0, 0),
        SEQ_BODY(0, "doorFR", BODY_SHAPE_OPEN, 0, 0),
        SEQ_TERM(5000),
    };
    sequencePosePlan(steps, 6, false, 100, &plan);
    const uint16_t slowThrowMs = 800;

    SeqPoseRun run = {};
    TEST_ASSERT_TRUE(sequencePoseStart(&run, 1000, false, false, plan.count, 0, nullptr));

    uint32_t lastMotion = 0;
    uint16_t lastSpacing = 0;
    bool anyMotion = false;
    uint8_t instantsBeforeMotion = 0;
    for (uint32_t now = 1000; run.active && now < 60000; now += 10) {
        if (!sequencePoseDue(run, now)) continue;
        if (sequencePoseFinished(run)) {
            sequencePoseEnd(&run);
            break;
        }
        const SeqPoseCmd& cmd = plan.cmds[run.next];
        const bool motion = cmd.cls != SEQ_POSE_INSTANT;
        if (motion) {
            if (anyMotion) {
                TEST_ASSERT_GREATER_OR_EQUAL_UINT32(lastSpacing, now - lastMotion);
                TEST_ASSERT_GREATER_OR_EQUAL_UINT32(SEQ_CADENCE_FLOOR_MS, now - lastMotion);
            }
            anyMotion = true;
            lastMotion = now;
            lastSpacing = cmd.cls == SEQ_POSE_BODY ? slowThrowMs : SEQ_CADENCE_FLOOR_MS;
        } else {
            TEST_ASSERT_FALSE_MESSAGE(anyMotion, "a sound or light waited behind a motion");
            ++instantsBeforeMotion;
        }
        sequencePoseAdvance(&run, now, cmd.cls, true, slowThrowMs, boardOutputAddress(0), SEQ_CADENCE_FLOOR_MS);
        sequencePoseAwaitDone(&run, false);
    }
    TEST_ASSERT_FALSE(run.active);
    TEST_ASSERT_EQUAL_UINT8(plan.count, run.sent);
    TEST_ASSERT_EQUAL_UINT8(1 + 4, instantsBeforeMotion);  // the sound, four lights
}

// The next command waits while the Output the last body command moved is still
// moving, past its throw time if need be.
void test_the_next_command_waits_for_the_moving_output() {
    SeqPoseRun run = {};
    TEST_ASSERT_TRUE(sequencePoseStart(&run, 0, false, false, 3, 0, nullptr));
    sequencePoseAdvance(&run, 0, SEQ_POSE_BODY, true, 300, boardOutputAddress(2), SEQ_CADENCE_FLOOR_MS);
    TEST_ASSERT_TRUE(sequencePoseDue(run, SEQ_CADENCE_FLOOR_MS));
    TEST_ASSERT_FALSE(sequencePoseAwaitDone(&run, true));
    TEST_ASSERT_TRUE(sequencePoseAwaitDone(&run, false));
}

// A second press while the first pose is still being reached -- here, just
// after the first sent its last motion -- keeps that motion's spacing: the new
// pose's first command waits for the Floor and for the Output to stop moving,
// exactly as the first pose's next command would have.
void test_a_pose_replacing_a_pose_keeps_the_spacing() {
    SeqPoseRun run = {};
    TEST_ASSERT_TRUE(sequencePoseStart(&run, 0, false, false, 1, 0, nullptr));
    sequencePoseAdvance(&run, 0, SEQ_POSE_BODY, true, 300, boardOutputAddress(2), SEQ_CADENCE_FLOOR_MS);  // its last command: a body Output
    TEST_ASSERT_TRUE(sequencePoseFinished(run));

    TEST_ASSERT_TRUE(sequencePoseStart(&run, 10, false, false, 3, 0, nullptr));
    TEST_ASSERT_FALSE(sequencePoseDue(run, 10));
    TEST_ASSERT_FALSE(sequencePoseDue(run, SEQ_CADENCE_FLOOR_MS - 1));
    TEST_ASSERT_TRUE(sequencePoseDue(run, SEQ_CADENCE_FLOOR_MS));
    TEST_ASSERT_FALSE(sequencePoseAwaitDone(&run, true));  // Output 2 still moving
    TEST_ASSERT_TRUE(sequencePoseAwaitDone(&run, false));

    // A pose that has run out its spacing and ended hands nothing on.
    sequencePoseEnd(&run);
    TEST_ASSERT_TRUE(sequencePoseStart(&run, 20, false, false, 3, 0, nullptr));
    TEST_ASSERT_TRUE(sequencePoseDue(run, 20));
    TEST_ASSERT_TRUE(sequencePoseAwaitDone(&run, true));
}

// A resync's staged ring close and a pose never share the dome. Staging a
// resync ends a pose being reached; a pose that starts supersedes a staged
// close. A pose that moves nothing -- refused, or empty -- leaves it.
void test_a_pose_and_a_resync_close_never_share_the_dome() {
    SeqPoseRun run = {};
    uint8_t closeIdx = SEQ_RESYNC_CLOSE_NONE;
    uint32_t closeDueMs = 0;

    TEST_ASSERT_TRUE(sequencePoseStart(&run, 0, false, false, 3, 0, &closeIdx));
    TEST_ASSERT_TRUE(sequenceResyncCloseStage(&run, &closeIdx, &closeDueMs, 100));
    TEST_ASSERT_FALSE(run.active);
    TEST_ASSERT_FALSE(sequencePoseDue(run, 1000));
    TEST_ASSERT_EQUAL_UINT8(0, closeIdx);
    TEST_ASSERT_EQUAL_UINT32(100, closeDueMs);

    TEST_ASSERT_FALSE(sequencePoseStart(&run, 200, true, false, 3, 0, &closeIdx));  // estop
    TEST_ASSERT_FALSE(sequencePoseStart(&run, 200, false, false, 0, 0, &closeIdx));  // nothing to send
    TEST_ASSERT_EQUAL_UINT8(0, closeIdx);

    TEST_ASSERT_TRUE(sequencePoseStart(&run, 200, false, false, 3, 0, &closeIdx));
    TEST_ASSERT_EQUAL_UINT8(SEQ_RESYNC_CLOSE_NONE, closeIdx);
}

// A pose is refused outright under a latched estop and in Sleep Mode, with
// words for the surface, and nothing starts.
void test_a_pose_is_refused_under_the_estop_and_in_sleep_mode() {
    SeqPoseRun run = {};
    TEST_ASSERT_FALSE(sequencePoseStart(&run, 0, true, false, 5, 0, nullptr));
    TEST_ASSERT_FALSE(run.active);
    TEST_ASSERT_FALSE(sequencePoseDue(run, 0));
    TEST_ASSERT_FALSE(sequencePoseStart(&run, 0, false, true, 5, 0, nullptr));
    TEST_ASSERT_FALSE(run.active);

    TEST_ASSERT_NOT_NULL(sequencePoseRefusal(true, false));
    TEST_ASSERT_NOT_NULL(sequencePoseRefusal(false, true));
    TEST_ASSERT_NULL(sequencePoseRefusal(false, false));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_pose_inside_a_loop_takes_the_pass_it_falls_in);
    RUN_TEST(test_nothing_the_routine_has_not_yet_moved_is_commanded);
    RUN_TEST(test_a_group_target_becomes_one_command_per_panel);
    RUN_TEST(test_past_the_end_the_ring_is_closed_and_the_pies_stay);
    RUN_TEST(test_sound_and_lights_are_the_ones_running_at_the_instant);
    RUN_TEST(test_a_flutter_a_random_pick_and_quiet_are_not_sent);
    RUN_TEST(test_a_body_part_takes_its_last_step);
    RUN_TEST(test_a_pose_never_starts_two_motions_inside_the_cadence_floor);
    RUN_TEST(test_the_next_command_waits_for_the_moving_output);
    RUN_TEST(test_a_pose_replacing_a_pose_keeps_the_spacing);
    RUN_TEST(test_a_pose_and_a_resync_close_never_share_the_dome);
    RUN_TEST(test_a_pose_is_refused_under_the_estop_and_in_sleep_mode);
    return UNITY_END();
}
