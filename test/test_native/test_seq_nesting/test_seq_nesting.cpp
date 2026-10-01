// =============================================================================
// test/test_native/test_seq_nesting/test_seq_nesting.cpp
//
// A sequence inside a sequence (ADR 0046, #438): the save-time walk that
// refuses a cycle, a path past the stated depth, a toggle phrase and a phrase
// that is not on the droid; and the load-time splice that puts a phrase's
// steps where the phrase step sits, in time order, a loop travelling whole.
// =============================================================================

#include <stdlib.h>
#include <string.h>

#include <unity.h>

#include "protocol_check.h"
#include "seq_json.h"
#include "seq_store_util.h"

void setUp() {}
void tearDown() {}

static SeqStep gSteps[96];
static SeqStep gClose[96];

// A fake droid: phrase id -> (toggle, step count, the phrases it names).
struct FakePhrase {
    const char* ref;
    bool toggle;
    uint8_t steps;
    const char* names[2];
};
static const FakePhrase kDroid[] = {
    {"aaaa", false, 4, {"bbbb", nullptr}},
    {"bbbb", false, 3, {"cccc", nullptr}},
    {"cccc", false, 3, {"dddd", nullptr}},
    {"dddd", false, 2, {nullptr, nullptr}},
    {"loop", false, 3, {"self", nullptr}},
    {"tggl", true, 3, {nullptr, nullptr}},
};

static void fakeLookup(const char* ref, SeqNestInfo* out, void*) {
    memset(out, 0, sizeof(*out));
    for (const FakePhrase& p : kDroid) {
        if (strcmp(p.ref, ref) != 0) continue;
        out->found = true;
        out->toggle = p.toggle;
        out->stepCount = p.steps;
        for (const char* n : p.names) {
            if (n != nullptr) strncpy(out->refs[out->refCount++], n, PC_SEQ_REF_MAX);
        }
    }
}

static ProtocolCheckResult nestCheck(const char* ref, const char* selfId) {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"format\":1,\"name\":\"DM:PARENT\",\"id\":\"%s\",\"suppressMs\":5000,\"steps\":["
             "{\"t\":0,\"type\":\"sequence\",\"ref\":\"%s\"},"
             "{\"t\":1000,\"type\":\"end\"}]}",
             selfId, ref);
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(json, gSteps, 96, gClose, 96, d);
    if (r.ok) r = protocolCheck(d);
    if (r.ok) r = protocolCheckNesting(d, selfId, d.name, fakeLookup, nullptr);
    return r;
}

static void test_the_nesting_walk_keeps_the_stated_rules() {
    TEST_ASSERT_TRUE(nestCheck("bbbb", "self").ok);          // bbbb > cccc > dddd: 3 deep
    TEST_ASSERT_FALSE(nestCheck("aaaa", "self").ok);         // aaaa > bbbb > cccc > dddd: 4 deep
    TEST_ASSERT_FALSE(nestCheck("loop", "self").ok);         // loop names the sequence being saved
    TEST_ASSERT_FALSE(nestCheck("tggl", "self").ok);         // a toggle is not a phrase
    TEST_ASSERT_FALSE(nestCheck("gone", "self").ok);         // not on this droid
    TEST_ASSERT_EQUAL_STRING("steps[0].ref", nestCheck("loop", "self").field);
}

// A phrase cannot sit in a loop body: splicing it would change the body's
// step count under the loop header.
static void test_a_phrase_inside_a_loop_is_refused() {
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParse(
        "{\"format\":1,\"name\":\"DM:LOOPED\",\"suppressMs\":5000,\"steps\":["
        "{\"t\":0,\"type\":\"loop\",\"body\":1,\"periodMs\":500,\"durationMs\":1000},"
        "{\"t\":0,\"type\":\"sequence\",\"ref\":\"bbbb\"},"
        "{\"t\":2000,\"type\":\"end\"}]}",
        gSteps, 96, gClose, 96, d);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("steps[1].type", r.field);
}

static SeqStep step(uint32_t t, SeqStepType type, const char* payload) {
    SeqStep s;
    memset(&s, 0, sizeof(s));
    s.tMs = t;
    s.type = type;
    strncpy(s.payload, payload, sizeof(s.payload) - 1);
    return s;
}

// The phrase's steps land where the phrase step sits, its end dropped, the
// branch back in time order with a loop kept whole, and a phrase step past
// the parent's end is cut as the engine would never run it.
static void test_a_phrase_is_spliced_in_time_order_where_it_sits() {
    SeqStep* buf = (SeqStep*)malloc(sizeof(SeqStep) * 4);
    buf[0] = step(0, STEP_DOME_CMD, ":OP01");
    buf[1] = step(500, STEP_SEQUENCE, "bbbb");
    buf[2] = step(800, STEP_DOME_CMD, ":CL01");
    buf[3] = step(5000, STEP_END, "");
    uint8_t count = 4;
    SeqStep child[5];
    child[0] = step(0, STEP_AUDIO, "$H");
    child[1] = step(100, STEP_LOOP, "");
    child[1].params.bodyCount = 1;
    child[2] = step(50, STEP_DOME_CMD, ":OF02");  // loop body: pass-relative
    child[3] = step(9000, STEP_AUDIO, "$S");      // past the parent's end
    child[4] = step(9500, STEP_END, "");
    TEST_ASSERT_TRUE(seqStoreSplicePhrase(&buf, &count, 1, child, 5));
    TEST_ASSERT_EQUAL_UINT8(6, count);
    TEST_ASSERT_EQUAL_STRING(":OP01", buf[0].payload);
    TEST_ASSERT_EQUAL_STRING("$H", buf[1].payload);
    TEST_ASSERT_EQUAL_UINT32(500, buf[1].tMs);
    TEST_ASSERT_EQUAL(STEP_LOOP, buf[2].type);
    TEST_ASSERT_EQUAL_UINT32(600, buf[2].tMs);
    TEST_ASSERT_EQUAL_STRING(":OF02", buf[3].payload);
    TEST_ASSERT_EQUAL_UINT32(50, buf[3].tMs);
    TEST_ASSERT_EQUAL_STRING(":CL01", buf[4].payload);
    TEST_ASSERT_EQUAL(STEP_END, buf[5].type);
    free(buf);
}

// A branching tree three levels deep: parent -> a; a -> b, c; b -> d, e;
// c -> f. Each pass splices exactly the phrases the branch held when the
// pass began, so b's own phrases (d, e: they sort before c) wait for the next
// pass rather than taking c's turn, and every leaf is in the run.
struct TreePhrase {
    const char* ref;
    SeqStep steps[3];
    uint8_t count;
};
static TreePhrase gTree[6];
static uint8_t gLeftOut = 0;

static void buildTree() {
    gTree[0] = {"a", {step(100, STEP_SEQUENCE, "b"), step(2000, STEP_SEQUENCE, "c"), step(3000, STEP_END, "")}, 3};
    gTree[1] = {"b", {step(0, STEP_SEQUENCE, "d"), step(50, STEP_SEQUENCE, "e"), step(500, STEP_END, "")}, 3};
    gTree[2] = {"c", {step(0, STEP_SEQUENCE, "f"), step(500, STEP_END, ""), step(0, STEP_END, "")}, 2};
    gTree[3] = {"d", {step(0, STEP_AUDIO, "$D"), step(100, STEP_END, ""), step(0, STEP_END, "")}, 2};
    gTree[4] = {"e", {step(0, STEP_AUDIO, "$E"), step(100, STEP_END, ""), step(0, STEP_END, "")}, 2};
    gTree[5] = {"f", {step(0, STEP_AUDIO, "$F"), step(100, STEP_END, ""), step(0, STEP_END, "")}, 2};
}

static void test_every_leaf_of_a_branching_tree_is_spliced_in() {
    buildTree();
    gLeftOut = 0;
    SeqStep* buf = (SeqStep*)malloc(sizeof(SeqStep) * 2);
    buf[0] = step(0, STEP_SEQUENCE, "a");
    buf[1] = step(9000, STEP_END, "");
    uint8_t count = 2;
    const bool fits = seqStoreSplicePhrases(
        &buf, &count,
        [](const char* ref, bool deep, const SeqStep** child, uint8_t* childCount) {
            for (const TreePhrase& p : gTree) {
                if (!deep && strcmp(p.ref, ref) == 0) {
                    *child = p.steps;
                    *childCount = p.count;
                    return;
                }
            }
            ++gLeftOut;
        },
        []() {});
    TEST_ASSERT_TRUE(fits);
    TEST_ASSERT_EQUAL_UINT8(0, gLeftOut);
    TEST_ASSERT_EQUAL_UINT8(4, count);
    TEST_ASSERT_EQUAL_STRING("$D", buf[0].payload);
    TEST_ASSERT_EQUAL_UINT32(100, buf[0].tMs);
    TEST_ASSERT_EQUAL_STRING("$E", buf[1].payload);
    TEST_ASSERT_EQUAL_UINT32(150, buf[1].tMs);
    TEST_ASSERT_EQUAL_STRING("$F", buf[2].payload);
    TEST_ASSERT_EQUAL_UINT32(2000, buf[2].tMs);
    TEST_ASSERT_EQUAL(STEP_END, buf[3].type);
    free(buf);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_the_nesting_walk_keeps_the_stated_rules);
    RUN_TEST(test_a_phrase_inside_a_loop_is_refused);
    RUN_TEST(test_a_phrase_is_spliced_in_time_order_where_it_sits);
    RUN_TEST(test_every_leaf_of_a_branching_tree_is_spliced_in);
    return UNITY_END();
}
