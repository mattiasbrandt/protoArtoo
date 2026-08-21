// =============================================================================
// test/test_native/test_seq_store_util/test_seq_store_util.cpp
//
// Native tests for the pure Learned Sequence store decision logic
// (seq_store_util.cpp, issue #2 slice 3): name->file mapping and the save
// capacity policy (16-file cap, 12 KB per-file, 24 KB free-space floor). These
// run the real production helpers, so the boundaries are proven, not emulated.
// =============================================================================

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
    RUN_TEST(test_capacity_count_takes_precedence_over_size);
    return UNITY_END();
}
