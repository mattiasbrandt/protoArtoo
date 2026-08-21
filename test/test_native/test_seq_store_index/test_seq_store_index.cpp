// =============================================================================
// test/test_native/test_seq_store_index/test_seq_store_index.cpp
//
// Native tests for the pure Learned Sequence name index (seq_store_index.cpp)
// and the runtime-first routing precedence it gives sequenceLookup()
// (issue #2 slice 3c).
// =============================================================================

#include <string.h>

#include <unity.h>

#include "seq_store_index.h"
#include "sequence_dispatcher.h"

void setUp()    { seqStoreIndexClear(); }
void tearDown() { seqStoreIndexClear(); }

static SeqIndexEntry mk(const char* name, SeqToggleGroup g) {
    SeqIndexEntry e = {};
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.toggleGroup = g;
    e.suppressMs = 4000;
    strcpy(e.source, "user");
    e.modified = false;
    strncpy(e.file, "seq000.json", sizeof(e.file) - 1);
    return e;
}

// -----------------------------------------------------------------------------
// Index mechanics
// -----------------------------------------------------------------------------
static void test_add_find_count() {
    TEST_ASSERT_TRUE(seqStoreIndexAdd(mk("DM:ALPHA", TOGGLE_NONE)));
    TEST_ASSERT_TRUE(seqStoreIndexAdd(mk("DM:BETA", TOGGLE_USER1)));
    TEST_ASSERT_EQUAL_UINT8(2, seqStoreIndexCount());

    const SeqIndexEntry* a = seqStoreIndexFind("DM:ALPHA");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_UINT8(TOGGLE_NONE, a->toggleGroup);
    const SeqIndexEntry* b = seqStoreIndexFind("DM:BETA");
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT8(TOGGLE_USER1, b->toggleGroup);
    TEST_ASSERT_NULL(seqStoreIndexFind("DM:NOPE"));
}

static void test_add_updates_in_place() {
    seqStoreIndexAdd(mk("DM:ALPHA", TOGGLE_NONE));
    SeqIndexEntry upd = mk("DM:ALPHA", TOGGLE_PIES);
    upd.suppressMs = 9999;
    TEST_ASSERT_TRUE(seqStoreIndexAdd(upd));
    TEST_ASSERT_EQUAL_UINT8(1, seqStoreIndexCount());  // not duplicated
    const SeqIndexEntry* a = seqStoreIndexFind("DM:ALPHA");
    TEST_ASSERT_EQUAL_UINT8(TOGGLE_PIES, a->toggleGroup);
    TEST_ASSERT_EQUAL_UINT32(9999, a->suppressMs);
}

static void test_full_table_rejects_new_accepts_update() {
    char name[24];
    for (uint8_t i = 0; i < SEQ_STORE_MAX; ++i) {
        snprintf(name, sizeof(name), "DM:S%u", (unsigned)i);
        TEST_ASSERT_TRUE(seqStoreIndexAdd(mk(name, TOGGLE_NONE)));
    }
    TEST_ASSERT_EQUAL_UINT8(SEQ_STORE_MAX, seqStoreIndexCount());
    // New name when full -> rejected.
    TEST_ASSERT_FALSE(seqStoreIndexAdd(mk("DM:OVERFLOW", TOGGLE_NONE)));
    // Existing name when full -> still updates.
    TEST_ASSERT_TRUE(seqStoreIndexAdd(mk("DM:S0", TOGGLE_ALL)));
    TEST_ASSERT_EQUAL_UINT8(TOGGLE_ALL, seqStoreIndexFind("DM:S0")->toggleGroup);
}

static void test_remove_compacts() {
    seqStoreIndexAdd(mk("DM:A", TOGGLE_NONE));
    seqStoreIndexAdd(mk("DM:B", TOGGLE_NONE));
    seqStoreIndexAdd(mk("DM:C", TOGGLE_NONE));
    TEST_ASSERT_TRUE(seqStoreIndexRemove("DM:B"));
    TEST_ASSERT_EQUAL_UINT8(2, seqStoreIndexCount());
    TEST_ASSERT_NULL(seqStoreIndexFind("DM:B"));
    TEST_ASSERT_NOT_NULL(seqStoreIndexFind("DM:A"));
    TEST_ASSERT_NOT_NULL(seqStoreIndexFind("DM:C"));
    TEST_ASSERT_FALSE(seqStoreIndexRemove("DM:B"));  // already gone
}

// -----------------------------------------------------------------------------
// Routing precedence — runtime -> catalog -> alias -> fallback
// -----------------------------------------------------------------------------
static void test_lookup_runtime_first() {
    // DM:VADER is a Factory Sequence; index a Learned one of the same name.
    SequenceLookupResult before = sequenceLookup("DM:VADER");
    TEST_ASSERT_EQUAL_UINT8(SEQ_CATALOG, before.kind);

    seqStoreIndexAdd(mk("DM:VADER", TOGGLE_NONE));
    SequenceLookupResult shadowed = sequenceLookup("DM:VADER");
    TEST_ASSERT_EQUAL_UINT8(SEQ_RUNTIME, shadowed.kind);  // Retrained shadows

    // Memory Wipe — factory resurfaces.
    seqStoreIndexRemove("DM:VADER");
    SequenceLookupResult after = sequenceLookup("DM:VADER");
    TEST_ASSERT_EQUAL_UINT8(SEQ_CATALOG, after.kind);
}

static void test_lookup_runtime_new_name() {
    SequenceLookupResult miss = sequenceLookup("DM:HOMEBREW");
    TEST_ASSERT_EQUAL_UINT8(SEQ_FALLBACK, miss.kind);  // unknown -> dome fallback

    seqStoreIndexAdd(mk("DM:HOMEBREW", TOGGLE_NONE));
    SequenceLookupResult hit = sequenceLookup("DM:HOMEBREW");
    TEST_ASSERT_EQUAL_UINT8(SEQ_RUNTIME, hit.kind);
}

static void test_lookup_alias_still_works() {
    SequenceLookupResult r = sequenceLookup("DM:WAVE");  // alias -> :SE02
    TEST_ASSERT_EQUAL_UINT8(SEQ_ALIAS, r.kind);
    TEST_ASSERT_EQUAL_STRING(":SE02", r.aliasTarget);
}

// -----------------------------------------------------------------------------
int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_add_find_count);
    RUN_TEST(test_add_updates_in_place);
    RUN_TEST(test_full_table_rejects_new_accepts_update);
    RUN_TEST(test_remove_compacts);
    RUN_TEST(test_lookup_runtime_first);
    RUN_TEST(test_lookup_runtime_new_name);
    RUN_TEST(test_lookup_alias_still_works);
    return UNITY_END();
}
