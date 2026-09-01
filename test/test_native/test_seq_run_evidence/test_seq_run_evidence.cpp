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
#include <stdio.h>
#include <string.h>

#include <ArduinoJson.h>
#include <unity.h>

#include "protocol_check.h"  // PC_CMD_MAX -- the longest command the format holds
#include "seq_last_run_json.h"
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

static void test_tx_ring_omitted_recent_and_truncation() {
    seqEvidenceBegin("X", 0, 0, 0);
    const uint16_t n = SEQ_EVID_TX_CAP + 5;
    for (uint16_t i = 0; i < n; ++i) seqEvidenceRecordTx(dome(":OP01"), false);
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_UINT16(n, ev.txTotalCount);
    TEST_ASSERT_EQUAL_UINT16(5, ev.txOmittedRecentCount);
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

// The property the ESP32-P4 ring dimensions are sized to buy (#256): a run that
// fills the ring exactly is retained in full, with nothing overwritten and no
// omission reported. Written against the constants rather than a literal, so it
// states the rule on whichever board it is built for.
static void test_a_run_that_exactly_fills_the_ring_is_captured_whole() {
    seqEvidenceBegin("X", 0, 0, 0);
    char first[SEQ_EVID_CMD_LEN] = {};
    for (uint16_t i = 0; i < SEQ_EVID_TX_CAP; ++i) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), ":OP%02u", (unsigned)(i % 100u));
        if (i == 0) {
            strncpy(first, cmd, sizeof(first) - 1);
        }
        seqEvidenceRecordTx(dome(cmd), false);
    }
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_UINT16(SEQ_EVID_TX_CAP, ev.txTotalCount);
    TEST_ASSERT_EQUAL_UINT16(0, ev.txOmittedRecentCount);
    // The oldest slot still holds the first command emitted: nothing wrapped.
    TEST_ASSERT_EQUAL_STRING(first, ev.tx[0]);

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateSeqLastRunJson(doc, ev, true));
    TEST_ASSERT_FALSE(doc["tx"]["truncated"].as<bool>());
    TEST_ASSERT_EQUAL_UINT16(SEQ_EVID_TX_CAP, doc["tx"]["retained"].as<unsigned>());
}

// A TX entry retains a command up to its own width. PC_CMD_MAX is the longest
// command the sequence format can hold, so on a board whose entry is that wide
// nothing is clipped, and on a narrower one the clip lands exactly at the width
// -- which is the loss the ESP32-P4 entry width was raised to remove.
static void test_command_capture_is_bounded_by_the_entry_width() {
    char longCmd[PC_CMD_MAX + 1];
    memset(longCmd, 'A', PC_CMD_MAX);
    longCmd[0] = '@';
    longCmd[1] = '1';
    longCmd[2] = 'M';
    longCmd[PC_CMD_MAX] = '\0';

    seqEvidenceBegin("X", 0, 0, 0);
    seqEvidenceRecordTx(dome(longCmd), false);
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);

    const size_t expected = (size_t)PC_CMD_MAX < (size_t)SEQ_EVID_CMD_LEN
                                ? (size_t)PC_CMD_MAX
                                : (size_t)(SEQ_EVID_CMD_LEN - 1);
    TEST_ASSERT_EQUAL_size_t(expected, strlen(ev.tx[0]));
    TEST_ASSERT_EQUAL_STRING_LEN(longCmd, ev.tx[0], expected);
}

static void test_artoo_esp32_keeps_its_own_ring_dimensions() {
    // #256 made these per chip target, and this binary always builds
    // PA_BOARD_ARTOO_ESP32 (platformio.ini env:native). The artoo-esp32 record
    // must stay 2204 B per copy: that board has no static DRAM to give and the
    // two copies double every byte. The ESP32-P4 half is proven by
    // test/test_tools/test_board_chip_sized_constants.py.
    TEST_ASSERT_EQUAL_INT(48, SEQ_EVID_CMD_LEN);
    TEST_ASSERT_EQUAL_INT(32, SEQ_EVID_TX_CAP);
    TEST_ASSERT_EQUAL_INT(12, SEQ_EVID_CLEANUP_CAP);
    TEST_ASSERT_EQUAL_size_t(2204u, sizeof(SeqRunEvidence));
}

static void test_end_completed_and_drop_delta() {
    seqEvidenceBegin("X", 0, 100, 7);          // body queue-full baseline = 7
    seqEvidenceRecordTx(dome(":OP01"), false);
    seqEvidenceEnd(SEQ_RUN_COMPLETED, "", 200, 10);  // now = 10 -> delta 3
    SeqRunEvidence ev;
    seqEvidenceSnapshot(ev);
    TEST_ASSERT_EQUAL_INT(SEQ_RUN_COMPLETED, (int)ev.outcome);
    TEST_ASSERT_EQUAL_UINT32(200, ev.endMs);
    TEST_ASSERT_EQUAL_UINT32(3, ev.bodyQueueFullDelta);
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

static void test_last_run_json_uses_unambiguous_counter_names() {
    seqEvidenceBegin("DM:CANTINA", 4, 1000, 20);
    const uint16_t n = SEQ_EVID_TX_CAP + 3;
    for (uint16_t i = 0; i < n; ++i) seqEvidenceRecordTx(dome(":OP01"), false);
    seqEvidenceNoteRetry();
    seqEvidenceEnd(SEQ_RUN_COMPLETED, "", 2000, 37);

    SeqRunEvidence ev;
    TEST_ASSERT_TRUE(seqEvidenceSnapshot(ev));

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateSeqLastRunJson(doc, ev, true));
    TEST_ASSERT_TRUE(doc["valid"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("DM:CANTINA", doc["name"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT16(n, doc["tx"]["total"].as<unsigned>());
    TEST_ASSERT_EQUAL_UINT16(SEQ_EVID_TX_CAP, doc["tx"]["capacity"].as<unsigned>());
    TEST_ASSERT_EQUAL_UINT16(SEQ_EVID_TX_CAP, doc["tx"]["retained"].as<unsigned>());
    TEST_ASSERT_EQUAL_UINT16(3, doc["tx"]["omittedFromRecent"].as<unsigned>());
    TEST_ASSERT_TRUE(doc["tx"]["truncated"].as<bool>());
    TEST_ASSERT_EQUAL_UINT32(17, doc["warnings"]["bodyQueueFullDelta"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(1, doc["warnings"]["dispatchRetryCount"].as<uint32_t>());
    TEST_ASSERT_FALSE(doc["warnings"]["remoteDomeQueue"]["sampled"].as<bool>());
    TEST_ASSERT_TRUE(doc["warnings"]["remoteDomeQueue"]["queueFullDelta"].isNull());

    char out[1600];
    serializeJson(doc, out, sizeof(out));
    TEST_ASSERT_NULL(strstr(out, "\"overflow\""));
    TEST_ASSERT_NULL(strstr(out, "domeQueueDropDelta"));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_snapshot_is_invalid);  // first: pristine record
    RUN_TEST(test_begin_sets_running_and_name);
    RUN_TEST(test_scopes_inferred_from_stream);
    RUN_TEST(test_ring_masks_open_close_flutter);
    RUN_TEST(test_group_close_clears_net_open);
    RUN_TEST(test_cleanup_separate_from_general_stream);
    RUN_TEST(test_tx_ring_omitted_recent_and_truncation);
    RUN_TEST(test_cleanup_overflow_truncation);
    RUN_TEST(test_a_run_that_exactly_fills_the_ring_is_captured_whole);
    RUN_TEST(test_command_capture_is_bounded_by_the_entry_width);
    RUN_TEST(test_artoo_esp32_keeps_its_own_ring_dimensions);
    RUN_TEST(test_end_completed_and_drop_delta);
    RUN_TEST(test_retry_count);
    RUN_TEST(test_abort_outcome_wins_over_completed_fallback);
    RUN_TEST(test_recordtx_ignored_when_not_running);
    RUN_TEST(test_last_run_json_uses_unambiguous_counter_names);
    return UNITY_END();
}
