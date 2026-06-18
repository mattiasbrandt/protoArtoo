// =============================================================================
// test/test_native/test_seq_run_evidence/test_seq_run_evidence.cpp
//
// Native tests for the body-side last-run evidence record (issue #2 task #6,
// src/sequence_run_evidence.cpp). Verifies the record derives net-open/touched
// ring masks and effect scopes from the recorded command stream, separates
// cleanup from the general TX stream, bounds the ring with truncation flags,
// counts drops/retries, and lets an abort outcome win over the COMPLETED
// fallback.
// =============================================================================
#include <string.h>

#include <unity.h>

#include "sequence_run_evidence.h"

void setUp()    {}
void tearDown() {}

static SeqAction dome(const char* cmd) {
    SeqAction a;
    memset(&a, 0, sizeof(a));
    a.kind = SEQ_ACT_DOME_CMD;
    strncpy(a.payload, cmd, sizeof(a.payload) - 1);
    return a;
}

static SeqAction audioDollar(const char* cmd) {
    SeqAction a;
    memset(&a, 0, sizeof(a));
    a.kind = SEQ_ACT_AUDIO_DOLLAR;
    strncpy(a.payload, cmd, sizeof(a.payload) - 1);
    return a;
}

static int ringBit(int panel) {
    const uint8_t c = seqEngineRingPanelCount();
    for (uint8_t i = 0; i < c; ++i) {
        if (seqEngineRingPanelNumber(i) == panel) return (int)i;
    }
    return -1;
}

// Must run first: a pristine record has not recorded any run.
static void test_initial_snapshot_is_invalid() {
    SeqRunEvidence ev;
    TEST_ASSERT_FALSE(seqEvidenceSnapshot(ev));
    TEST_ASSERT_FALSE(ev.valid);
}

static void test_begin_sets_running_and_name() {
    seqEvidenceBegin("DM:ROCKMARCH", 2, 1000, 0);
    SeqRunEvidence ev;
    TEST_ASSERT_TRUE(seqEvidenceSnapshot(ev));
    TEST_ASSERT_TRUE(ev.valid);
    TEST_ASSERT_EQUAL_STRING("DM:ROCKMARCH", ev.name);
    TEST_ASSERT_EQUAL_UINT8(2, ev.source);
    TEST_ASSERT_EQUAL_INT(SEQ_RUN_RUNNING, (int)ev.outcome);
    TEST_ASSERT_EQUAL_UINT32(1000, ev.startMs);
}

static void test_scopes_inferred_from_stream() {
    seqEvidenceBegin("DM:VADER", 1, 0, 0);
    seqEvidenceRecordTx(audioDollar("$M"), false);   // audio
    seqEvidenceRecordTx(dome("@0T11"), false);       // logic/psi
    seqEvidenceRecordTx(dome("@HPA0021|47"), false); // holo
    seqEvidenceRecordTx(dome(":OP01"), false);       // panel
    seqEvidenceRecordTx(dome("@0T1"), false);        // reset -> no scope
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_TRUE(ev.fxScopes & SEQ_EVID_FX_AUDIO);
    TEST_ASSERT_TRUE(ev.fxScopes & SEQ_EVID_FX_LOGIC_PSI);
    TEST_ASSERT_TRUE(ev.fxScopes & SEQ_EVID_FX_HOLO);
    TEST_ASSERT_TRUE(ev.fxScopes & SEQ_EVID_FX_PANEL);
    TEST_ASSERT_FALSE(ev.fxScopes & SEQ_EVID_FX_DOME_SEQ);
}

static void test_ring_masks_open_close_flutter() {
    seqEvidenceBegin("X", 0, 0, 0);
    seqEvidenceRecordTx(dome(":OP01"), false);  // open P1
    seqEvidenceRecordTx(dome(":OP03"), false);  // open P3
    seqEvidenceRecordTx(dome(":CL01"), false);  // close P1 -> net-open {3}
    seqEvidenceRecordTx(dome(":OF07"), false);  // flutter P7 -> touched only
    seqEvidenceRecordTx(dome(":OPP1"), false);  // pie -> ignored for ring
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);

    const uint16_t net = ev.netOpenRingMask;
    const uint16_t touched = ev.touchedRingMask;
    TEST_ASSERT_FALSE(net & (1u << ringBit(1)));     // P1 closed
    TEST_ASSERT_TRUE(net & (1u << ringBit(3)));      // P3 open
    TEST_ASSERT_FALSE(net & (1u << ringBit(7)));     // P7 fluttered, not open
    TEST_ASSERT_TRUE(touched & (1u << ringBit(1)));  // P1 touched
    TEST_ASSERT_TRUE(touched & (1u << ringBit(3)));  // P3 touched
    TEST_ASSERT_TRUE(touched & (1u << ringBit(7)));  // P7 touched (flutter)
}

static void test_group_close_clears_net_open() {
    seqEvidenceBegin("X", 0, 0, 0);
    seqEvidenceRecordTx(dome(":OP15"), false);  // open ring group -> all open
    seqEvidenceRecordTx(dome(":CL15"), false);  // close ring group -> none open
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_UINT16(0, ev.netOpenRingMask);
    // All ring panels touched by the group commands.
    const uint16_t all = (uint16_t)((1u << seqEngineRingPanelCount()) - 1u);
    TEST_ASSERT_EQUAL_UINT16(all, ev.touchedRingMask);
}

static void test_cleanup_separate_from_general_stream() {
    seqEvidenceBegin("X", 0, 0, 0);
    seqEvidenceRecordTx(dome(":OP01"), false);     // normal
    seqEvidenceRecordTx(dome(":CL01"), true);      // cleanup
    seqEvidenceRecordTx(dome("@0T1"), true);       // cleanup
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_UINT16(3, ev.txTotalCount);     // all in general stream
    TEST_ASSERT_EQUAL_UINT8(2, ev.cleanupCount);      // only the cleanup ones
    TEST_ASSERT_EQUAL_UINT8(2, ev.cleanupTotalCount);
    TEST_ASSERT_FALSE(ev.cleanupTruncated);
    TEST_ASSERT_EQUAL_STRING(":CL01", ev.cleanup[0]);
    TEST_ASSERT_EQUAL_STRING("@0T1", ev.cleanup[1]);
}

static void test_tx_ring_overflow_and_truncation() {
    seqEvidenceBegin("X", 0, 0, 0);
    const uint16_t n = SEQ_EVID_TX_CAP + 5;
    for (uint16_t i = 0; i < n; ++i) seqEvidenceRecordTx(dome(":OP01"), false);
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_UINT16(n, ev.txTotalCount);
    TEST_ASSERT_EQUAL_UINT16(5, ev.txOverflowCount);  // 5 dropped from the ring
}

static void test_cleanup_overflow_truncation() {
    seqEvidenceBegin("X", 0, 0, 0);
    const uint8_t n = SEQ_EVID_CLEANUP_CAP + 3;
    for (uint8_t i = 0; i < n; ++i) seqEvidenceRecordTx(dome(":CL01"), true);
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_UINT8(SEQ_EVID_CLEANUP_CAP, ev.cleanupCount);
    TEST_ASSERT_EQUAL_UINT8(n, ev.cleanupTotalCount);
    TEST_ASSERT_TRUE(ev.cleanupTruncated);
}

static void test_end_completed_and_drop_delta() {
    seqEvidenceBegin("X", 0, 100, 7);          // drop baseline = 7
    seqEvidenceRecordTx(dome(":OP01"), false);
    seqEvidenceEnd(SEQ_RUN_COMPLETED, "", 200, 10);  // now = 10 -> delta 3
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_INT(SEQ_RUN_COMPLETED, (int)ev.outcome);
    TEST_ASSERT_EQUAL_UINT32(200, ev.endMs);
    TEST_ASSERT_EQUAL_UINT32(3, ev.domeQueueDropDelta);
}

static void test_retry_count() {
    seqEvidenceBegin("X", 0, 0, 0);
    seqEvidenceNoteRetry();
    seqEvidenceNoteRetry();
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_UINT32(2, ev.dispatchRetryCount);
}

static void test_abort_outcome_wins_over_completed_fallback() {
    seqEvidenceBegin("X", 0, 0, 0);
    seqEvidenceEnd(SEQ_RUN_ESTOP, "estop", 50, 0);     // abort finalizes first
    seqEvidenceEnd(SEQ_RUN_COMPLETED, "", 60, 0);      // later idle transition: no-op
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_INT(SEQ_RUN_ESTOP, (int)ev.outcome);
    TEST_ASSERT_EQUAL_STRING("estop", ev.reason);
    TEST_ASSERT_EQUAL_UINT32(50, ev.endMs);
}

static void test_recordtx_ignored_when_not_running() {
    seqEvidenceBegin("X", 0, 0, 0);
    seqEvidenceEnd(SEQ_RUN_COMPLETED, "", 10, 0);
    seqEvidenceRecordTx(dome(":OP01"), false);  // after end -> ignored
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_UINT16(0, ev.txTotalCount);
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_snapshot_is_invalid);  // first: pristine record
    RUN_TEST(test_begin_sets_running_and_name);
    RUN_TEST(test_scopes_inferred_from_stream);
    RUN_TEST(test_ring_masks_open_close_flutter);
    RUN_TEST(test_group_close_clears_net_open);
    RUN_TEST(test_cleanup_separate_from_general_stream);
    RUN_TEST(test_tx_ring_overflow_and_truncation);
    RUN_TEST(test_cleanup_overflow_truncation);
    RUN_TEST(test_end_completed_and_drop_delta);
    RUN_TEST(test_retry_count);
    RUN_TEST(test_abort_outcome_wins_over_completed_fallback);
    RUN_TEST(test_recordtx_ignored_when_not_running);
    return UNITY_END();
}
