// =============================================================================
// test/test_native/test_sequence_gesture/test_sequence_gesture.cpp
//
// A Gesture (include/sequence_gesture.h, ADR 0046, #438): its Parts resolved
// and ordered by where they sit when it runs, the spread's timing, the dome
// command a dome Gesture becomes, Protocol Check's form rules, and the safety
// invariant that a dome Gesture leaving ring panels open is closed by the
// engine's staggered terminal cleanup, one panel at a time.
// =============================================================================

#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "protocol_check.h"
#include "seq_json.h"
#include "sequence_engine.h"
#include "sequence_gesture.h"
#include "sequence_pose.h"

void setUp() {}
void tearDown() {}

static SeqStep gSteps[96];
static SeqStep gClose[96];

static SeqStep gestureStep(const char* payload, uint8_t shape, uint8_t spread, uint8_t start,
                           uint8_t direction) {
    SeqStep s;
    memset(&s, 0, sizeof(s));
    s.type = STEP_GESTURE;
    strncpy(s.payload, payload, sizeof(s.payload) - 1);
    s.params.shape = shape;
    seqGestureSetSpread(s.params, spread);
    seqGestureSetStart(s.params, start);
    seqGestureSetDirection(s.params, direction);
    return s;
}

static const char* idAt(const uint8_t* members, uint8_t i) { return droidPartIdAt(members[i]); }

// "The ring, clockwise, from the front": the order is where the panels sit,
// not the order the catalog lists them in. Bearings read 0 dead astern and 180
// dead ahead (operator decision, 2026-09-30).
static void test_ring_orders_by_bearing_from_the_front() {
    uint8_t m[SEQ_GESTURE_MEMBERS_MAX];
    SeqStep cw = gestureStep("ring", BODY_SHAPE_OPEN, GESTURE_SPREAD_WAVE, GESTURE_START_FRONT,
                             GESTURE_DIR_CLOCKWISE);
    const uint8_t n = seqGestureMembers(cw, m, SEQ_GESTURE_MEMBERS_MAX);
    TEST_ASSERT_EQUAL_UINT8(14, n);
    TEST_ASSERT_EQUAL_STRING("panel14", idAt(m, 0));  // 184 degrees
    TEST_ASSERT_EQUAL_STRING("panel13", idAt(m, 1));  // 194
    TEST_ASSERT_EQUAL_STRING("panel12", idAt(m, 2));  // 204
    TEST_ASSERT_EQUAL_STRING("panel1", idAt(m, 13));  // 142.5, the last before the front again

    SeqStep ccw = gestureStep("ring", BODY_SHAPE_OPEN, GESTURE_SPREAD_WAVE, GESTURE_START_FRONT,
                              GESTURE_DIR_COUNTERCLOCKWISE);
    seqGestureMembers(ccw, m, SEQ_GESTURE_MEMBERS_MAX);
    TEST_ASSERT_EQUAL_STRING("panel1", idAt(m, 0));
    TEST_ASSERT_EQUAL_STRING("panel2", idAt(m, 1));
}

// A Part with no bearing orders last and is never refused: every body Part
// today, so a body set keeps catalog order.
static void test_parts_with_no_bearing_keep_their_place_last() {
    uint8_t m[SEQ_GESTURE_MEMBERS_MAX];
    SeqStep s = gestureStep("breadpan", BODY_SHAPE_OPEN, GESTURE_SPREAD_WAVE, GESTURE_START_FRONT,
                            GESTURE_DIR_CLOCKWISE);
    TEST_ASSERT_EQUAL_UINT8(4, seqGestureMembers(s, m, SEQ_GESTURE_MEMBERS_MAX));
    TEST_ASSERT_EQUAL_STRING("doorFL", idAt(m, 0));
    TEST_ASSERT_EQUAL_STRING("doorRR", idAt(m, 3));
}

// A chase: one member per step, and the one before goes back as the next goes,
// the going-back first so two are never out at once.
static void test_chase_moves_one_member_per_step_and_returns_the_one_before() {
    TEST_ASSERT_EQUAL_UINT16(6, seqGesturePassMoves(GESTURE_SPREAD_CHASE, 3));
    const SeqGestureMove m1 = seqGesturePassMove(GESTURE_SPREAD_CHASE, 3, 400, 1);
    const SeqGestureMove m2 = seqGesturePassMove(GESTURE_SPREAD_CHASE, 3, 400, 2);
    const SeqGestureMove last = seqGesturePassMove(GESTURE_SPREAD_CHASE, 3, 400, 5);
    TEST_ASSERT_EQUAL_UINT8(0, m1.member);
    TEST_ASSERT_TRUE(m1.undo);
    TEST_ASSERT_EQUAL_UINT32(400, m1.atMs);
    TEST_ASSERT_EQUAL_UINT8(1, m2.member);
    TEST_ASSERT_FALSE(m2.undo);
    TEST_ASSERT_EQUAL_UINT32(400, m2.atMs);
    TEST_ASSERT_EQUAL_UINT8(2, last.member);
    TEST_ASSERT_TRUE(last.undo);
    TEST_ASSERT_EQUAL_UINT32(1200, last.atMs);
}

// A dome Gesture is one `$` command whose mask is the members the fork has an
// address for (AstroPixelsPlus.ino:359-371): the ring is bits 14..20.
static void test_dome_gesture_is_one_command_over_the_members_mask() {
    char cmd[24];
    SeqStep chase = gestureStep("ring", BODY_SHAPE_OPEN, GESTURE_SPREAD_CHASE, GESTURE_START_FRONT,
                                GESTURE_DIR_CLOCKWISE);
    TEST_ASSERT_TRUE(seqGestureDomeCommand(chase, cmd, sizeof(cmd)));
    TEST_ASSERT_EQUAL_STRING(":OW$1FC000", cmd);
    SeqStep pies = gestureStep("pies", BODY_SHAPE_CLOSE, GESTURE_SPREAD_TOGETHER, GESTURE_START_FRONT,
                               GESTURE_DIR_CLOCKWISE);
    TEST_ASSERT_TRUE(seqGestureDomeCommand(pies, cmd, sizeof(cmd)));
    TEST_ASSERT_EQUAL_STRING(":CL$7E00000", cmd);
    // A pair the dome has no command for resolves to nothing.
    SeqStep none = gestureStep("ring", BODY_SHAPE_CLOSE, GESTURE_SPREAD_WAVE, GESTURE_START_FRONT,
                               GESTURE_DIR_CLOCKWISE);
    TEST_ASSERT_FALSE(seqGestureDomeCommand(none, cmd, sizeof(cmd)));
}

static ProtocolCheckResult parseAndCheck(const char* json, SeqDraft& d) {
    ProtocolCheckResult r = seqJsonParse(json, gSteps, 96, gClose, 96, d);
    return r.ok ? protocolCheck(d) : r;
}

// Form, not capability: a pair the dome cannot perform still saves.
static void test_a_pair_the_dome_cannot_perform_still_saves() {
    SeqDraft d;
    ProtocolCheckResult r = parseAndCheck(
        "{\"format\":1,\"name\":\"DM:GWAVE\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"gesture\",\"set\":\"ring\",\"shape\":\"close\",\"spread\":\"wave\"},"
        "{\"t\":3000,\"type\":\"end\"}]}",
        d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_UINT8(FX_PANEL, d.steps[0].effectClass);
}

static void test_a_flutter_gesture_still_owes_a_close() {
    SeqDraft d;
    ProtocolCheckResult r = parseAndCheck(
        "{\"format\":1,\"name\":\"DM:GFLUT\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"gesture\",\"set\":\"breadpan\",\"shape\":\"flutter\"},"
        "{\"t\":3000,\"type\":\"end\"}]}",
        d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].shape", r.field);
    r = parseAndCheck(
        "{\"format\":1,\"name\":\"DM:GFLUT\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"gesture\",\"set\":\"breadpan\",\"shape\":\"flutter\"},"
        "{\"t\":2000,\"type\":\"gesture\",\"set\":\"breadpan\",\"shape\":\"close\"},"
        "{\"t\":3000,\"type\":\"end\"}]}",
        d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
}

static void test_a_list_across_both_halves_is_refused() {
    SeqDraft d;
    ProtocolCheckResult r = parseAndCheck(
        "{\"format\":1,\"name\":\"DM:GMIX\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"gesture\",\"parts\":[\"doorFL\",\"panel1\"]},"
        "{\"t\":3000,\"type\":\"end\"}]}",
        d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].parts", r.field);
}

// With a tempo, a Gesture's pace defaults to one beat, and a repeat with no
// extent runs to the end step (ADR 0060).
static void test_a_gesture_on_a_tempo_paces_by_the_beat_and_repeats_to_the_end() {
    SeqDraft d;
    ProtocolCheckResult r = parseAndCheck(
        "{\"format\":1,\"name\":\"DM:GBEAT\",\"suppressMs\":9000,"
        "\"tempo\":{\"bpm\":120,\"source\":\"typed\",\"confidence\":1},\"steps\":["
        "{\"t\":0,\"beat\":2,\"type\":\"gesture\",\"set\":\"ring\",\"spread\":\"chase\",\"repeatBeats\":8},"
        "{\"t\":0,\"beat\":16,\"type\":\"end\"}]}",
        d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_UINT32(1000, d.steps[0].tMs);
    TEST_ASSERT_EQUAL_UINT16(500, seqGestureStepMs(d.steps[0].params));
    TEST_ASSERT_EQUAL_UINT16(4000, seqGestureRepeatMs(d.steps[0].params));
    TEST_ASSERT_EQUAL_UINT32(7000, seqGestureExtentMs(d.steps[0].params));
}

static uint32_t stubRand() { return 0; }

// SAFETY: a dome Gesture that leaves ring panels open is closed by terminal
// cleanup one panel at a time, never by a group close - the 2026-06-17
// brownout rule. Without the engine marking the Gesture's ring panels, the
// `$` command it sends would leave them open when the sequence ends.
static void test_a_dome_gesture_that_leaves_the_ring_open_is_closed_one_panel_at_a_time() {
    SeqDraft d;
    ProtocolCheckResult r = parseAndCheck(
        "{\"format\":1,\"name\":\"DM:GOPEN\",\"suppressMs\":9000,\"steps\":["
        "{\"t\":0,\"type\":\"gesture\",\"set\":\"ring\"},"
        "{\"t\":500,\"type\":\"end\"}]}",
        d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    SequenceEntry e = { d.name, d.steps, d.stepCount, d.suppressMs, d.toggleGroup, nullptr, 0, nullptr };
    static SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &e, 0);
    uint8_t gestures = 0;
    uint8_t ringCloses = 0;
    for (uint32_t now = 0; now <= 8000 && seqEngineActive(st); now += 10) {
        SeqAction act;
        while (seqEnginePeek(st, now, stubRand, act)) {
            if (act.kind == SEQ_ACT_GESTURE) gestures++;
            if (act.kind == SEQ_ACT_DOME_CMD) {
                TEST_ASSERT_NOT_EQUAL(0, strcmp(act.payload, ":CL15"));
                TEST_ASSERT_NOT_EQUAL(0, strcmp(act.payload, ":CL00"));
                if (strncmp(act.payload, ":CL", 3) == 0) ringCloses++;
            }
            seqEngineCommit(st);
        }
    }
    TEST_ASSERT_EQUAL_UINT8(1, gestures);
    TEST_ASSERT_EQUAL_UINT8(seqEngineRingPanelCount(), ringCloses);
}

// SAFETY: a body Gesture's moves are paced by the Coordinator, never closer
// than the Cadence Floor, whatever the authored spread asks - "together" asks
// for all four doors at once. And the Gesture never holds the engine: a step
// written after it still fires on time.
static void test_a_body_gesture_is_paced_by_the_floor_and_never_holds_the_engine() {
    SeqStep g = gestureStep("breadpan", BODY_SHAPE_OPEN, GESTURE_SPREAD_TOGETHER, GESTURE_START_FRONT,
                            GESTURE_DIR_CLOCKWISE);
    static SeqGestureRun run;
    run = SeqGestureRun{};
    run.awaitArm = SEQ_BULK_CENTRE_NO_AWAIT;
    TEST_ASSERT_TRUE(sequenceGestureStart(&run, g, 0));
    uint32_t starts[8];
    uint8_t n = 0;
    for (uint32_t now = 0; now <= 3000 && n < 8; now += 10) {
        SeqGestureNext next;
        if (!sequenceGestureNext(&run, now, /*awaitedMoving=*/false, &next)) continue;
        TEST_ASSERT_FALSE(next.dome);
        starts[n++] = now;
        // A driven Output with a short throw: the Floor is what spaces them.
        sequenceGestureDone(&run, next, now, /*started=*/true, 100, 0);
    }
    TEST_ASSERT_EQUAL_UINT8(4, n);
    for (uint8_t i = 1; i < n; ++i) {
        TEST_ASSERT_TRUE(starts[i] - starts[i - 1] >= SEQ_CADENCE_FLOOR_MS);
    }
    TEST_ASSERT_FALSE(sequenceGestureActive(run));

    SeqDraft d;
    ProtocolCheckResult r = parseAndCheck(
        "{\"format\":1,\"name\":\"DM:GHOLD\",\"suppressMs\":9000,\"steps\":["
        "{\"t\":0,\"type\":\"gesture\",\"set\":\"breadpan\",\"spread\":\"wave\",\"stepMs\":2000},"
        "{\"t\":100,\"type\":\"audio\",\"cmd\":\"$H\"},"
        "{\"t\":500,\"type\":\"end\"}]}",
        d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    SequenceEntry e = { d.name, d.steps, d.stepCount, d.suppressMs, d.toggleGroup, nullptr, 0, nullptr };
    static SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &e, 0);
    uint32_t audioAt = 0;
    for (uint32_t now = 0; now <= 1000 && seqEngineActive(st); now += 10) {
        SeqAction act;
        while (seqEnginePeek(st, now, stubRand, act)) {
            if (act.kind == SEQ_ACT_AUDIO_DOLLAR) audioAt = now;
            seqEngineCommit(st);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(100, audioAt);
}

// A pose sends the droid to where a run of the Gesture has it at that
// instant: a wave one door per second is two doors open at 1.5 s. The pose
// planner's default case ignores a step it does not know, which would make a
// pose differ from the run.
static void test_a_pose_puts_the_gesture_where_the_run_has_it() {
    SeqStep steps[2];
    steps[0] = gestureStep("breadpan", BODY_SHAPE_OPEN, GESTURE_SPREAD_WAVE, GESTURE_START_FRONT,
                           GESTURE_DIR_CLOCKWISE);
    seqGestureSetStepMs(steps[0].params, 1000);
    memset(&steps[1], 0, sizeof(steps[1]));
    steps[1].type = STEP_END;
    steps[1].tMs = 9000;
    static SeqPosePlan plan;
    sequencePosePlan(steps, 2, false, 1500, &plan);
    TEST_ASSERT_EQUAL_UINT8(2, plan.count);
    TEST_ASSERT_EQUAL_STRING("doorFL", plan.cmds[0].act.payload);
    TEST_ASSERT_EQUAL_STRING("doorFR", plan.cmds[1].act.payload);
    TEST_ASSERT_EQUAL_UINT8(SEQ_POSE_BODY, plan.cmds[0].cls);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ring_orders_by_bearing_from_the_front);
    RUN_TEST(test_parts_with_no_bearing_keep_their_place_last);
    RUN_TEST(test_chase_moves_one_member_per_step_and_returns_the_one_before);
    RUN_TEST(test_dome_gesture_is_one_command_over_the_members_mask);
    RUN_TEST(test_a_pair_the_dome_cannot_perform_still_saves);
    RUN_TEST(test_a_flutter_gesture_still_owes_a_close);
    RUN_TEST(test_a_list_across_both_halves_is_refused);
    RUN_TEST(test_a_gesture_on_a_tempo_paces_by_the_beat_and_repeats_to_the_end);
    RUN_TEST(test_a_dome_gesture_that_leaves_the_ring_open_is_closed_one_panel_at_a_time);
    RUN_TEST(test_a_body_gesture_is_paced_by_the_floor_and_never_holds_the_engine);
    RUN_TEST(test_a_pose_puts_the_gesture_where_the_run_has_it);
    return UNITY_END();
}
