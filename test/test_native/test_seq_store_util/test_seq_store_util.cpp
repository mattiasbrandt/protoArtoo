// =============================================================================
// test/test_native/test_seq_store_util/test_seq_store_util.cpp
//
// Native tests for the pure Learned Sequence store decision logic
// (seq_store_util.cpp, issue #2 slice 3): name->file mapping and the save
// capacity policy (16-file cap, per-file size cap, free-space floor). These
// run the real production helpers, so the boundaries are proven, not emulated.
//
// The per-file cap and the free-space floor became chip-target specific in #256.
// This binary always builds PA_BOARD_ARTOO_ESP32 (platformio.ini env:native), so
// what it pins here is the artoo-esp32 half; the cross-board values are proven
// by test/test_tools/test_board_chip_sized_constants.py.
// =============================================================================

#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "seq_store_index.h"  // SEQ_STORE_MAX
#include "seq_store_util.h"

void setUp()    {}
void tearDown() {}

// -----------------------------------------------------------------------------
// Name -> file
// -----------------------------------------------------------------------------
static void test_name_to_file_basic() {
    char f[40];
    TEST_ASSERT_TRUE(seqStoreNameToFile("DM:VADER", f, sizeof(f)));
    TEST_ASSERT_EQUAL_STRING("DM_VADER.json", f);
}

static void test_name_to_file_keeps_underscores_digits() {
    char f[40];
    TEST_ASSERT_TRUE(seqStoreNameToFile("DM:MY_SEQ1", f, sizeof(f)));
    TEST_ASSERT_EQUAL_STRING("DM_MY_SEQ1.json", f);
}

static void test_name_to_file_rejects_non_dm() {
    char f[40];
    TEST_ASSERT_FALSE(seqStoreNameToFile("BD:NOPE", f, sizeof(f)));
    TEST_ASSERT_FALSE(seqStoreNameToFile(nullptr, f, sizeof(f)));
}

static void test_name_to_file_rejects_overflow() {
    char f[8];  // too small for "DM_VADER.json"
    TEST_ASSERT_FALSE(seqStoreNameToFile("DM:VADER", f, sizeof(f)));
}

// -----------------------------------------------------------------------------
// Capacity policy
// -----------------------------------------------------------------------------
static const size_t BIG_FREE = 200 * 1024;

static void test_capacity_ok_when_room() {
    ProtocolCheckResult r = seqStoreCapacityCheck(true, 0, 2000, BIG_FREE);
    TEST_ASSERT_TRUE(r.ok);
}

static void test_capacity_new_when_full_rejected() {
    ProtocolCheckResult r =
        seqStoreCapacityCheck(true, SEQ_STORE_MAX, 2000, BIG_FREE);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("name", r.field);
}

static void test_capacity_update_when_full_allowed() {
    // Overwriting an existing name does not consume a new slot.
    ProtocolCheckResult r =
        seqStoreCapacityCheck(false, SEQ_STORE_MAX, 2000, BIG_FREE);
    TEST_ASSERT_TRUE(r.ok);
}

static void test_capacity_last_slot_allowed() {
    ProtocolCheckResult r =
        seqStoreCapacityCheck(true, (uint8_t)(SEQ_STORE_MAX - 1), 2000, BIG_FREE);
    TEST_ASSERT_TRUE(r.ok);
}

static void test_capacity_file_size_boundary() {
    // Exactly at the cap is allowed; one over is rejected.
    ProtocolCheckResult ok =
        seqStoreCapacityCheck(true, 0, SEQ_FILE_MAX_BYTES, BIG_FREE);
    TEST_ASSERT_TRUE(ok.ok);
    ProtocolCheckResult bad =
        seqStoreCapacityCheck(true, 0, SEQ_FILE_MAX_BYTES + 1, BIG_FREE);
    TEST_ASSERT_FALSE(bad.ok);
    TEST_ASSERT_EQUAL_STRING("json", bad.field);
}

static void test_capacity_free_floor_boundary() {
    const size_t fileLen = 1000;
    // freeBytes exactly floor+fileLen -> allowed; one less -> rejected.
    ProtocolCheckResult ok = seqStoreCapacityCheck(
        true, 0, fileLen, SEQ_FS_FREE_FLOOR + fileLen);
    TEST_ASSERT_TRUE(ok.ok);
    ProtocolCheckResult bad = seqStoreCapacityCheck(
        true, 0, fileLen, SEQ_FS_FREE_FLOOR + fileLen - 1);
    TEST_ASSERT_FALSE(bad.ok);
    TEST_ASSERT_EQUAL_STRING("json", bad.field);
}

static void test_artoo_esp32_keeps_its_own_capacity_numbers() {
    // #256 made these per chip target. The artoo-esp32 values must not move
    // when the ESP32-P4 arm changes: a save that fits today has to keep fitting,
    // and the rejection boundary has to stay where every deployed controller
    // and every stored sequence already expects it.
    TEST_ASSERT_EQUAL_size_t(12u * 1024u, SEQ_FILE_MAX_BYTES);
    TEST_ASSERT_EQUAL_size_t(24u * 1024u, SEQ_FS_FREE_FLOOR);
}

static void test_over_cap_message_names_the_enforced_cap() {
    // The size in the message and the size actually enforced come from one
    // macro, so a per-chip cap cannot report one number while rejecting at
    // another. Derive the expectation from the constant rather than repeating
    // a literal, which is what makes this a coupling test and not a copy.
    ProtocolCheckResult r =
        seqStoreCapacityCheck(true, 0, SEQ_FILE_MAX_BYTES + 1, BIG_FREE);
    TEST_ASSERT_FALSE(r.ok);
    char expected[64];
    snprintf(expected, sizeof(expected), "file too large (%u KB max)",
             (unsigned)(SEQ_FILE_MAX_BYTES / 1024u));
    TEST_ASSERT_EQUAL_STRING(expected, r.message);
}

static void test_free_floor_leaves_room_for_the_outgoing_copy() {
    // seqStoreSave writes /seq/.tmp.json while the outgoing copy of the same
    // sequence is still on disk -- it is removed only just before the rename --
    // so the space held back beyond the incoming file must cover one more
    // full-size file. That mechanism is what sets the floor on both chips.
    const size_t maxFile = SEQ_FILE_MAX_BYTES;
    ProtocolCheckResult atFloor =
        seqStoreCapacityCheck(false, 1, maxFile, SEQ_FS_FREE_FLOOR + maxFile);
    TEST_ASSERT_TRUE(atFloor.ok);
    // What is left once the incoming file is accounted for is the floor itself.
    TEST_ASSERT_TRUE(SEQ_FS_FREE_FLOOR >= maxFile);
    // One byte short of floor + fileLen is refused, so the reserve is real and
    // not merely nominal.
    ProtocolCheckResult below =
        seqStoreCapacityCheck(false, 1, maxFile, SEQ_FS_FREE_FLOOR + maxFile - 1);
    TEST_ASSERT_FALSE(below.ok);
    TEST_ASSERT_EQUAL_STRING("json", below.field);
}

static void test_capacity_count_takes_precedence_over_size() {
    // A new, oversized save into a full store reports the slot error first.
    ProtocolCheckResult r = seqStoreCapacityCheck(
        true, SEQ_STORE_MAX, SEQ_FILE_MAX_BYTES + 1, 0);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("name", r.field);
}

// -----------------------------------------------------------------------------
int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_name_to_file_basic);
    RUN_TEST(test_name_to_file_keeps_underscores_digits);
    RUN_TEST(test_name_to_file_rejects_non_dm);
    RUN_TEST(test_name_to_file_rejects_overflow);

    RUN_TEST(test_capacity_ok_when_room);
    RUN_TEST(test_capacity_new_when_full_rejected);
    RUN_TEST(test_capacity_update_when_full_allowed);
    RUN_TEST(test_capacity_last_slot_allowed);
    RUN_TEST(test_capacity_file_size_boundary);
    RUN_TEST(test_capacity_free_floor_boundary);
    RUN_TEST(test_artoo_esp32_keeps_its_own_capacity_numbers);
    RUN_TEST(test_over_cap_message_names_the_enforced_cap);
    RUN_TEST(test_free_floor_leaves_room_for_the_outgoing_copy);
    RUN_TEST(test_capacity_count_takes_precedence_over_size);
    return UNITY_END();
}
