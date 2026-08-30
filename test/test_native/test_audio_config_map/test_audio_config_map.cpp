// =============================================================================
// test/test_native/test_audio_config_map/test_audio_config_map.cpp
//
// Native unit tests for audio_config_map (ADR 0013). Representative spot
// checks per the ADR's own decision (not a sentinel-golden full-field pin):
// a handful of mapping fields, chirp key table lookups, the $-command
// table, binding unpackers, and binding-refresh accept/reject cases through
// MapReader.
// =============================================================================
#include <unity.h>

#include "audio_config_map.h"
#include "../../../test/stubs/config/map_config_io.h"

void setUp(void) {
}
void tearDown(void) {
}

// --- audioConfigMapBuild: representative field spot checks ---
void test_audioConfigMapBuild_maps_slot_tracks_and_ranges(void) {
    ConfigSnapshot cfg = {};
    cfg.audio.snd_scream = 101;
    cfg.audio.snd_happy = 202;
    cfg.audio.snd_sys_boot = 303;
    cfg.audio.snd_sys_net_down = 404;
    cfg.audio.snd_rand_min = 5;
    cfg.audio.snd_rand_max = 50;
    cfg.audio.snd_cat_gen_lo = 10;
    cfg.audio.snd_cat_gen_hi = 20;

    AudioPlaybackConfig out;
    audioConfigMapBuild(cfg, &out);

    TEST_ASSERT_EQUAL_UINT16(101, out.slotTracks[AUDIO_SLOT_NAMED_SCREAM]);
    TEST_ASSERT_EQUAL_UINT16(202, out.slotTracks[AUDIO_SLOT_NAMED_HAPPY]);
    TEST_ASSERT_EQUAL_UINT16(303, out.slotTracks[AUDIO_SLOT_SYS_BOOT]);
    TEST_ASSERT_EQUAL_UINT16(404, out.slotTracks[AUDIO_SLOT_SYS_NET_DOWN]);
    TEST_ASSERT_EQUAL_UINT16(5, out.randMin);
    TEST_ASSERT_EQUAL_UINT16(50, out.randMax);
    TEST_ASSERT_EQUAL_UINT16(10, out.categoryRanges[AUDIO_CATEGORY_GENERAL].lo);
    TEST_ASSERT_EQUAL_UINT16(20, out.categoryRanges[AUDIO_CATEGORY_GENERAL].hi);
}

void test_audioConfigMapBuild_null_out_is_noop(void) {
    ConfigSnapshot cfg = {};
    audioConfigMapBuild(cfg, nullptr);  // must not crash
    TEST_ASSERT_TRUE(true);
}

// --- audioConfigMapNamedTracks: projection from slotTracks ---
void test_audioConfigMapNamedTracks_projects_from_playback_config(void) {
    AudioPlaybackConfig playback;
    playback.slotTracks[AUDIO_SLOT_NAMED_SCREAM] = 111;
    playback.slotTracks[AUDIO_SLOT_NAMED_DISCO] = 222;

    AudioNamedTracks named;
    audioConfigMapNamedTracks(playback, &named);

    TEST_ASSERT_EQUAL_UINT16(111, named.scream);
    TEST_ASSERT_EQUAL_UINT16(222, named.disco);
}

// --- chirp key tables ---
void test_audioChirpKeyForSlot_known_and_none(void) {
    TEST_ASSERT_EQUAL_STRING("chr_scream", audioChirpKeyForSlot(AUDIO_SLOT_NAMED_SCREAM));
    TEST_ASSERT_EQUAL_STRING("chr_sys_dome_on", audioChirpKeyForSlot(AUDIO_SLOT_SYS_DOME_ON));
    TEST_ASSERT_EQUAL_STRING("chr_sys_netdown", audioChirpKeyForSlot(AUDIO_SLOT_SYS_NET_DOWN));
    TEST_ASSERT_NULL(audioChirpKeyForSlot(AUDIO_SLOT_NONE));
}

void test_audioChirpKeyForCategory_known_and_out_of_range(void) {
    TEST_ASSERT_EQUAL_STRING("chr_cat_gen", audioChirpKeyForCategory(0));
    TEST_ASSERT_EQUAL_STRING("chr_cat_whis", audioChirpKeyForCategory(11));
    TEST_ASSERT_NULL(audioChirpKeyForCategory(99));
}

// --- $-command table ---
void test_audioSlotForDollar_known_and_unknown(void) {
    TEST_ASSERT_EQUAL(AUDIO_SLOT_NAMED_SCREAM, audioSlotForDollar("$S"));
    TEST_ASSERT_EQUAL(AUDIO_SLOT_NAMED_HAPPY, audioSlotForDollar("$H"));
    TEST_ASSERT_EQUAL(AUDIO_SLOT_NONE, audioSlotForDollar("$Z"));
    TEST_ASSERT_EQUAL(AUDIO_SLOT_NONE, audioSlotForDollar(nullptr));
    TEST_ASSERT_EQUAL(AUDIO_SLOT_NONE, audioSlotForDollar("no-dollar"));
}

// --- binding unpackers ---
void test_audioUnpackChirpBinding_round_trips(void) {
    uint32_t packed = ((uint32_t)3 << 24) | ((uint32_t)'B' << 16) | (uint32_t)42;
    uint8_t bank = 0;
    char page = 0;
    uint16_t index = 0;
    TEST_ASSERT_TRUE(audioUnpackChirpBinding(packed, &bank, &page, &index));
    TEST_ASSERT_EQUAL_UINT8(3, bank);
    TEST_ASSERT_EQUAL('B', page);
    TEST_ASSERT_EQUAL_UINT16(42, index);
}

void test_audioUnpackChirpBinding_rejects_zero_bank_or_index(void) {
    uint8_t bank = 0;
    char page = 0;
    uint16_t index = 0;
    uint32_t zeroBank = ((uint32_t)0 << 24) | ((uint32_t)'A' << 16) | (uint32_t)1;
    uint32_t zeroIndex = ((uint32_t)1 << 24) | ((uint32_t)'A' << 16) | (uint32_t)0;
    TEST_ASSERT_FALSE(audioUnpackChirpBinding(zeroBank, &bank, &page, &index));
    TEST_ASSERT_FALSE(audioUnpackChirpBinding(zeroIndex, &bank, &page, &index));
}

void test_audioUnpackChirpCategoryBinding_round_trips(void) {
    uint32_t packed = ((uint32_t)5 << 8) | (uint32_t)'c';  // lowercase normalizes to upper
    uint8_t bank = 0;
    char page = 0;
    TEST_ASSERT_TRUE(audioUnpackChirpCategoryBinding(packed, &bank, &page));
    TEST_ASSERT_EQUAL_UINT8(5, bank);
    TEST_ASSERT_EQUAL('C', page);
}

// --- binding refresh: accept/reject through MapReader ---
void test_audioBindingsRefresh_not_catalog_capable_clears_and_fails(void) {
    MapReader reader;
    reader.set("chr_scream", (uint32_t)(((uint32_t)1 << 24) | ((uint32_t)'A' << 16) | 5));
    AudioBindingCache out;
    bool ok = audioBindingsRefresh(reader, /*catalogCapable=*/false, &out);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_FALSE(out.slots[AUDIO_SLOT_NAMED_SCREAM].valid);
}

void test_audioBindingsRefresh_populates_valid_bindings(void) {
    MapReader reader;
    reader.set("chr_scream", (uint32_t)(((uint32_t)1 << 24) | ((uint32_t)'A' << 16) | 5));
    reader.set("chr_cat_gen", (uint32_t)(((uint32_t)2 << 8) | (uint32_t)'B'));
    AudioBindingCache out;
    bool ok = audioBindingsRefresh(reader, /*catalogCapable=*/true, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(out.slots[AUDIO_SLOT_NAMED_SCREAM].valid);
    TEST_ASSERT_EQUAL_UINT8(1, out.slots[AUDIO_SLOT_NAMED_SCREAM].bank);
    TEST_ASSERT_EQUAL('A', out.slots[AUDIO_SLOT_NAMED_SCREAM].page);
    TEST_ASSERT_EQUAL_UINT16(5, out.slots[AUDIO_SLOT_NAMED_SCREAM].index);
    TEST_ASSERT_TRUE(out.categories[AUDIO_CATEGORY_GENERAL].valid);
    TEST_ASSERT_EQUAL_UINT8(2, out.categories[AUDIO_CATEGORY_GENERAL].bank);
}

void test_audioBindingsRefresh_missing_key_leaves_slot_invalid(void) {
    MapReader reader;  // no keys set — all reads return default 0
    AudioBindingCache out;
    bool ok = audioBindingsRefresh(reader, /*catalogCapable=*/true, &out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(out.slots[AUDIO_SLOT_NAMED_SCREAM].valid);
    TEST_ASSERT_FALSE(out.categories[AUDIO_CATEGORY_GENERAL].valid);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_audioConfigMapBuild_maps_slot_tracks_and_ranges);
    RUN_TEST(test_audioConfigMapBuild_null_out_is_noop);
    RUN_TEST(test_audioConfigMapNamedTracks_projects_from_playback_config);
    RUN_TEST(test_audioChirpKeyForSlot_known_and_none);
    RUN_TEST(test_audioChirpKeyForCategory_known_and_out_of_range);
    RUN_TEST(test_audioSlotForDollar_known_and_unknown);
    RUN_TEST(test_audioUnpackChirpBinding_round_trips);
    RUN_TEST(test_audioUnpackChirpBinding_rejects_zero_bank_or_index);
    RUN_TEST(test_audioUnpackChirpCategoryBinding_round_trips);
    RUN_TEST(test_audioBindingsRefresh_not_catalog_capable_clears_and_fails);
    RUN_TEST(test_audioBindingsRefresh_populates_valid_bindings);
    RUN_TEST(test_audioBindingsRefresh_missing_key_leaves_slot_invalid);
    return UNITY_END();
}
