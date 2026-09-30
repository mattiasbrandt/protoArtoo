// =============================================================================
// test/test_native/test_sequence_dome_how_far/test_sequence_dome_how_far.cpp
//
// How far a dome panel goes (include/sequence_dome_how_far.h, #438): a panel
// open or close with a howFar goes to the dome as the fork's `:MV` fraction of
// that panel's own throw, never as a pulse, and a ring panel left part open is
// closed by the engine's staggered terminal cleanup like one left fully open.
// =============================================================================

#include <string.h>

#include <unity.h>

#include "protocol_check.h"
#include "seq_json.h"
#include "sequence_dome_how_far.h"
#include "sequence_engine.h"

void setUp() {}
void tearDown() {}

static SeqStep gSteps[96];
static SeqStep gClose[96];

// 0 is the panel's open end and 180 its closed end (the fork's scaleToPos):
// 60% open stops 40% short of closed; 25% closed is 25% along from open. A
// pie addresses the fork's own number, and PP3 has none, so it goes as written.
static void test_how_far_goes_to_the_dome_as_a_fraction_of_the_panels_own_throw() {
    char out[12];
    TEST_ASSERT_TRUE(seqDomeHowFarCommand(":OP01", 60, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(":MV010072", out);
    TEST_ASSERT_TRUE(seqDomeHowFarCommand(":CLP1", 25, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(":MV080045", out);
    TEST_ASSERT_TRUE(seqDomeHowFarCommand(":OP15", 50, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(":MV150090", out);
    TEST_ASSERT_FALSE(seqDomeHowFarCommand(":OPP3", 50, out, sizeof(out)));
    TEST_ASSERT_FALSE(seqDomeHowFarCommand(":OP01", 100, out, sizeof(out)));
    TEST_ASSERT_FALSE(seqDomeHowFarCommand(":OF01", 50, out, sizeof(out)));
}

static ProtocolCheckResult parseAndCheck(const char* json, SeqDraft& d) {
    ProtocolCheckResult r = seqJsonParse(json, gSteps, 96, gClose, 96, d);
    return r.ok ? protocolCheck(d) : r;
}

// How far is said on an open or a close, and nowhere else: a flutter and a
// light have no throw to be a fraction of. A saved step never carries `:MV`.
static void test_how_far_is_refused_where_there_is_no_throw() {
    SeqDraft d;
    ProtocolCheckResult r = parseAndCheck(
        "{\"format\":1,\"name\":\"DM:HFOF\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"dome\",\"cmd\":\":OF01\",\"howFar\":40},"
        "{\"t\":100,\"type\":\"dome\",\"cmd\":\":CL01\"},"
        "{\"t\":300,\"type\":\"end\"}]}",
        d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[0].howFar", r.field);
    r = parseAndCheck(
        "{\"format\":1,\"name\":\"DM:HFMV\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"dome\",\"cmd\":\":MV010090\"},"
        "{\"t\":300,\"type\":\"end\"}]}",
        d);
    TEST_ASSERT_FALSE(r.ok);
}

static uint32_t stubRand() { return 0; }

// SAFETY: a ring panel left part open is still open, and terminal cleanup
// closes it one panel at a time, never with a group close.
static void test_a_ring_panel_left_part_open_is_closed_at_the_end() {
    SeqDraft d;
    ProtocolCheckResult r = parseAndCheck(
        "{\"format\":1,\"name\":\"DM:HFRING\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"dome\",\"cmd\":\":OP02\",\"howFar\":50},"
        "{\"t\":300,\"type\":\"end\"}]}",
        d);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    SequenceEntry e = { d.name, d.steps, d.stepCount, d.suppressMs, d.toggleGroup, nullptr, 0, nullptr };
    static SeqEngineState st;
    seqEngineInit(st);
    seqEngineStart(st, &e, 0);
    bool sentMove = false;
    bool closed = false;
    for (uint32_t now = 0; now <= 3000 && seqEngineActive(st); now += 10) {
        SeqAction act;
        while (seqEnginePeek(st, now, stubRand, act)) {
            if (strcmp(act.payload, ":MV020090") == 0) sentMove = true;
            if (strcmp(act.payload, ":CL02") == 0) closed = true;
            TEST_ASSERT_NOT_EQUAL(0, strcmp(act.payload, ":CL15"));
            seqEngineCommit(st);
        }
    }
    TEST_ASSERT_TRUE(sentMove);
    TEST_ASSERT_TRUE(closed);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_how_far_goes_to_the_dome_as_a_fraction_of_the_panels_own_throw);
    RUN_TEST(test_how_far_is_refused_where_there_is_no_throw);
    RUN_TEST(test_a_ring_panel_left_part_open_is_closed_at_the_end);
    return UNITY_END();
}
