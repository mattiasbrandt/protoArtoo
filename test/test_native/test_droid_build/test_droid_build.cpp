// =============================================================================
// test/test_native/test_droid_build/test_droid_build.cpp
//
// The Droid Build as firmware holds it (#343, ADR 0047).
//
// Three things are worth holding down here, and all three are ways the decision
// this file is named for could be undone without anything looking broken:
//
//   A design SEEDS and never FENCES. Whatever a builder answers, the Protocol
//   Check vocabulary is still the whole catalog - so a Part outside the Fitted
//   set is still nameable, still saveable, still wirable.
//
//   The two halves are independent. An MK4.1 dome on an MK4 Basic body is an
//   ordinary droid, so nothing may refuse a pairing.
//
//   A deliberately empty droid and a controller nobody has answered are
//   different answers, and only the second takes the pre-selected complement.
//   Collapse them and a builder who cleared their droid gets it re-fitted.
// =============================================================================
#include <unity.h>

#include <cstring>

#include "config_serializer.h"
#include "droid_build.h"
#include "droid_parts.h"
#include "../../stubs/config/map_config_io.h"

void setUp() {}
void tearDown() {}

namespace {

// Is the Part with this id among the fitted ones. Firmware only ever fits by
// id (droidFittedPartsFit()) and reads by index, so the by-id read is this
// test's own.
bool fitted(const DroidFittedParts& parts, const char* id) {
    return droidFittedPartsHasIndex(parts, droidPartIndexOf(id));
}

DroidBuildConfig defaults() {
    DroidBuildConfig build = {};
    droidBuildDefaults(&build);
    return build;
}

// What a writer laid down, read back through a reader - the same idiom
// test_servo_output_row.cpp uses for the storage door.
void copyInto(const MapWriter& writer, MapReader* reader) {
    for (const auto& pair : writer.data()) {
        reader->set(pair.first.c_str(), pair.second);
    }
}

}  // namespace

// ── The answer a fresh controller starts on ──────────────────────────────────

void test_a_fresh_controller_starts_on_the_pre_selected_design(void) {
    const DroidBuildConfig build = defaults();
    TEST_ASSERT_EQUAL_STRING("mk4", build.dome.design);
    TEST_ASSERT_EQUAL_STRING("complex", build.dome.variant);
    TEST_ASSERT_EQUAL_STRING("mk4", build.body.design);
    TEST_ASSERT_EQUAL_STRING("complex", build.body.variant);
    // It is an answer, not a blank: the complement that design carries is
    // already on the droid.
    TEST_ASSERT_EQUAL_UINT32(DROID_BUILD_DEFAULT_FITTED_COUNT,
                             (uint32_t)droidFittedPartsCount(build.fitted));
    TEST_ASSERT_TRUE(fitted(build.fitted, "pie1"));
    TEST_ASSERT_TRUE(fitted(build.fitted, "bodyPanel1"));
}

void test_a_common_addition_is_not_seeded_by_any_design(void) {
    const DroidBuildConfig build = defaults();
    // Every arm is optional and belongs to no design - the two utility arms
    // included since the operator's verified mapping (#409) - so a fresh
    // controller does not claim any is on the droid; the builder fits them.
    for (const char* id : {"gripArm", "gripClaw", "interArm", "interTool", "utilUp", "utilLo"}) {
        TEST_ASSERT_FALSE(fitted(build.fitted, id));
        // ... and they are still Parts this build can name.
        TEST_ASSERT_TRUE(droidPartIdIsKnown(id));
    }
    TEST_ASSERT_FALSE(fitted(build.fitted, "other1"));
}

// ── Seeds, never fences ──────────────────────────────────────────────────────

void test_the_part_vocabulary_is_the_whole_catalog_whatever_is_fitted(void) {
    DroidBuildConfig build = defaults();
    // Nothing fitted at all - the most fenced a Droid Build could possibly be.
    droidFittedPartsClear(&build.fitted);
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        TEST_ASSERT_TRUE(droidPartIdIsKnown(droidPartIdAt(i)));
    }
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)droidFittedPartsCount(build.fitted));
}

void test_a_part_no_design_carries_can_still_be_fitted(void) {
    DroidBuildConfig build = defaults();
    TEST_ASSERT_TRUE(droidFittedPartsFit(&build.fitted, "gripArm"));
    TEST_ASSERT_TRUE(fitted(build.fitted, "gripArm"));
    // and adding it took nothing away
    TEST_ASSERT_TRUE(fitted(build.fitted, "pie1"));
    TEST_ASSERT_EQUAL_UINT32(DROID_BUILD_DEFAULT_FITTED_COUNT + 1,
                             (uint32_t)droidFittedPartsCount(build.fitted));
}

void test_a_part_this_build_cannot_name_is_not_fitted(void) {
    DroidBuildConfig build = defaults();
    TEST_ASSERT_FALSE(droidFittedPartsFit(&build.fitted, "pie99"));
    TEST_ASSERT_FALSE(fitted(build.fitted, "pie99"));
    TEST_ASSERT_FALSE(fitted(build.fitted, ""));
    TEST_ASSERT_FALSE(fitted(build.fitted, nullptr));
}

// ── The vocabulary gate on a stated design ───────────────────────────────────

void test_a_design_the_catalog_declares_is_known_at_its_own_variants(void) {
    TEST_ASSERT_TRUE(droidDesignVariantIsKnown("mk4", "basic"));
    TEST_ASSERT_TRUE(droidDesignVariantIsKnown("mk4", "complex"));
    // `simple` is no longer a variant: it is the spelling `basic` was stored
    // under, and it is read as `basic` on the way in (test below), never kept.
    TEST_ASSERT_FALSE(droidDesignVariantIsKnown("mk4", "simple"));
    TEST_ASSERT_FALSE(droidDesignVariantIsKnown("mk4", "ornate"));
    // A design with no variant set is answered with no variant, and an empty
    // string is that answer rather than a missing one.
    TEST_ASSERT_TRUE(droidDesignVariantIsKnown("own", ""));
    TEST_ASSERT_FALSE(droidDesignVariantIsKnown("own", "complex"));
    TEST_ASSERT_FALSE(droidDesignVariantIsKnown("mk3", "complex"));
    TEST_ASSERT_FALSE(droidDesignVariantIsKnown("mk4", nullptr));
    TEST_ASSERT_NULL(droidDesignRow("mk3"));
    TEST_ASSERT_NULL(droidDesignRow(nullptr));
}

void test_a_mismatched_pairing_is_an_ordinary_droid(void) {
    DroidBuildConfig build = defaults();
    // A dome from one design under a body from another. Nothing here compares
    // the two halves, which is the whole point: a real droid is a mixture.
    TEST_ASSERT_TRUE(droidDesignChoiceSet(&build.dome, "mk4", "complex"));
    TEST_ASSERT_TRUE(droidDesignChoiceSet(&build.body, "own", ""));
    TEST_ASSERT_TRUE(droidDesignChoiceIsKnown(build.dome));
    TEST_ASSERT_TRUE(droidDesignChoiceIsKnown(build.body));
}

void test_an_id_too_long_for_the_field_is_refused_not_truncated(void) {
    DroidDesignChoice choice = {};
    TEST_ASSERT_FALSE(droidDesignChoiceSet(&choice, "a_design_id_far_past_the_field", ""));
    TEST_ASSERT_FALSE(droidDesignChoiceSet(&choice, "mk4", "a_variant_id_far_past_it"));
    // A truncated id would name a design the catalog never declared.
    TEST_ASSERT_EQUAL_STRING("", choice.design);
}

// ── The stored form ──────────────────────────────────────────────────────────

// A droid or a backup that answered before MK4's sparse variant was renamed
// stored `simple`. It reads back as `basic`, never reset to the default: that
// would quietly change what a builder said their droid is (#409).
void test_a_stored_legacy_variant_reads_as_the_variant_it_became(void) {
    DroidDesignChoice choice = {};
    TEST_ASSERT_TRUE(droidDesignChoiceSet(&choice, "mk4", "simple"));
    TEST_ASSERT_EQUAL_STRING("mk4", choice.design);
    TEST_ASSERT_EQUAL_STRING("basic", choice.variant);
    TEST_ASSERT_TRUE(droidDesignChoiceIsKnown(choice));
    // Only the design that renamed it: another design's `simple` is not
    // rewritten, and stays unknown.
    TEST_ASSERT_TRUE(droidDesignChoiceSet(&choice, "own", "simple"));
    TEST_ASSERT_EQUAL_STRING("simple", choice.variant);
    TEST_ASSERT_FALSE(droidDesignChoiceIsKnown(choice));
}

void test_every_field_round_trips_through_storage(void) {
    DroidBuildConfig saved = defaults();
    TEST_ASSERT_TRUE(droidDesignChoiceSet(&saved.dome, "mk4", "basic"));
    TEST_ASSERT_TRUE(droidDesignChoiceSet(&saved.body, "own", ""));
    TEST_ASSERT_TRUE(droidFittedPartsFit(&saved.fitted, "gripArm"));

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerializeDroidBuild(saved, writer));

    MapReader reader;
    copyInto(writer, &reader);
    DroidBuildConfig loaded = {};
    DroidBuildRepairReport report = {};
    configDeserializeDroidBuild(reader, &loaded, &report);

    TEST_ASSERT_TRUE(droidBuildRepairReportIsClean(report));
    TEST_ASSERT_EQUAL_STRING("mk4", loaded.dome.design);
    TEST_ASSERT_EQUAL_STRING("basic", loaded.dome.variant);
    TEST_ASSERT_EQUAL_STRING("own", loaded.body.design);
    TEST_ASSERT_EQUAL_STRING("", loaded.body.variant);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)droidFittedPartsCount(saved.fitted),
                             (uint32_t)droidFittedPartsCount(loaded.fitted));
    TEST_ASSERT_TRUE(fitted(loaded.fitted, "gripArm"));
    TEST_ASSERT_TRUE(fitted(loaded.fitted, "pie1"));
}

void test_a_controller_that_has_never_been_answered_takes_the_default(void) {
    MapReader reader;  // nothing stored at all
    DroidBuildConfig loaded = {};
    DroidBuildRepairReport report = {};
    configDeserializeDroidBuild(reader, &loaded, &report);

    TEST_ASSERT_TRUE(droidBuildRepairReportIsClean(report));
    TEST_ASSERT_EQUAL_STRING("mk4", loaded.dome.design);
    TEST_ASSERT_EQUAL_UINT32(DROID_BUILD_DEFAULT_FITTED_COUNT,
                             (uint32_t)droidFittedPartsCount(loaded.fitted));
}

void test_a_droid_with_nothing_fitted_is_an_answer_and_survives(void) {
    DroidBuildConfig saved = defaults();
    droidFittedPartsClear(&saved.fitted);

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerializeDroidBuild(saved, writer));

    MapReader reader;
    copyInto(writer, &reader);
    DroidBuildConfig loaded = {};
    DroidBuildRepairReport report = {};
    configDeserializeDroidBuild(reader, &loaded, &report);

    // The one that matters: an empty droid must not read back as the
    // pre-selected complement, or clearing a droid would be impossible.
    TEST_ASSERT_TRUE(droidBuildRepairReportIsClean(report));
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)droidFittedPartsCount(loaded.fitted));
}

void test_a_stored_design_this_build_cannot_name_is_repaired_and_counted(void) {
    MapWriter writer;
    writer.writeStr("dbuild_domed", "mk9");
    writer.writeStr("dbuild_domev", "baroque");
    writer.writeStr("dbuild_bodyd", "mk4");
    writer.writeStr("dbuild_bodyv", "complex");
    writer.writeStr("dbuild_parts", "utilUp,utilLo");

    MapReader reader;
    copyInto(writer, &reader);
    DroidBuildConfig loaded = {};
    DroidBuildRepairReport report = {};
    configDeserializeDroidBuild(reader, &loaded, &report);

    TEST_ASSERT_TRUE(report.domeRepaired);
    TEST_ASSERT_FALSE(report.bodyRepaired);
    TEST_ASSERT_EQUAL_STRING("mk4", loaded.dome.design);
    TEST_ASSERT_EQUAL_STRING("complex", loaded.dome.variant);
    // The half this build could still name is kept, and so are the Parts.
    TEST_ASSERT_EQUAL_STRING("mk4", loaded.body.design);
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)droidFittedPartsCount(loaded.fitted));
}

// A roadmap design is drawn on a card and can never be picked, so a stored one
// is not an answer anybody gave through this product - a hand-edited key, or a
// backup from an image that carried the design before it was withdrawn. The
// catalog declares MK3 as that kind of design (#368), and it restores to the
// default rather than surviving as an answer the controller cannot seed.
void test_a_stored_roadmap_design_restores_to_the_default(void) {
    MapWriter writer;
    writer.writeStr("dbuild_domed", "mk4");
    writer.writeStr("dbuild_domev", "complex");
    writer.writeStr("dbuild_bodyd", "mk3");
    writer.writeStr("dbuild_bodyv", "");
    writer.writeStr("dbuild_parts", "doorFL");

    MapReader reader;
    copyInto(writer, &reader);
    DroidBuildConfig loaded = {};
    DroidBuildRepairReport report = {};
    configDeserializeDroidBuild(reader, &loaded, &report);

    TEST_ASSERT_TRUE(report.bodyRepaired);
    TEST_ASSERT_FALSE(report.domeRepaired);
    TEST_ASSERT_EQUAL_STRING(DROID_BUILD_DEFAULT_DESIGN, loaded.body.design);
    TEST_ASSERT_EQUAL_STRING(DROID_BUILD_DEFAULT_VARIANT, loaded.body.variant);
    // Only the half that named it: the Parts stay as the builder left them.
    TEST_ASSERT_EQUAL_UINT32(1u, (uint32_t)droidFittedPartsCount(loaded.fitted));
}

void test_a_stored_variant_that_is_not_its_design_s_takes_the_default_pair(void) {
    MapWriter writer;
    writer.writeStr("dbuild_domed", "own");
    writer.writeStr("dbuild_domev", "complex");  // `own` declares no variants
    writer.writeStr("dbuild_parts", "pie1");

    MapReader reader;
    copyInto(writer, &reader);
    DroidBuildConfig loaded = {};
    DroidBuildRepairReport report = {};
    configDeserializeDroidBuild(reader, &loaded, &report);

    // Both fields move together: a defaulted design beside a stored variant
    // would be a pairing nobody ever stated.
    TEST_ASSERT_TRUE(report.domeRepaired);
    TEST_ASSERT_EQUAL_STRING("mk4", loaded.dome.design);
    TEST_ASSERT_EQUAL_STRING("complex", loaded.dome.variant);
}

void test_a_stored_part_this_build_no_longer_declares_is_dropped_and_counted(void) {
    MapWriter writer;
    writer.writeStr("dbuild_domed", "mk4");
    writer.writeStr("dbuild_domev", "complex");
    writer.writeStr("dbuild_bodyd", "mk4");
    writer.writeStr("dbuild_bodyv", "complex");
    writer.writeStr("dbuild_parts", "utilUp,periscope,utilLo");

    MapReader reader;
    copyInto(writer, &reader);
    DroidBuildConfig loaded = {};
    DroidBuildRepairReport report = {};
    configDeserializeDroidBuild(reader, &loaded, &report);

    TEST_ASSERT_EQUAL_UINT8(1, report.partsDropped);
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)droidFittedPartsCount(loaded.fitted));
    TEST_ASSERT_TRUE(fitted(loaded.fitted, "utilUp"));
    TEST_ASSERT_TRUE(fitted(loaded.fitted, "utilLo"));
}

void test_the_stored_part_list_tolerates_spacing_and_refuses_an_overlong_token(void) {
    DroidFittedParts parts = {};
    TEST_ASSERT_EQUAL_UINT8(0, droidFittedPartsParse(" utilUp , utilLo ", &parts));
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)droidFittedPartsCount(parts));

    // Longer than any declared id: counted as dropped rather than truncated
    // into a match with something it is not.
    TEST_ASSERT_EQUAL_UINT8(1, droidFittedPartsParse("upperPanelExtra", &parts));
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)droidFittedPartsCount(parts));
}

void test_every_part_in_the_catalog_can_be_stored_and_read_back(void) {
    // The bound on the stored list is arithmetic rather than measured, so the
    // maximal droid is the case that proves it: every Part fitted at once.
    DroidBuildConfig saved = defaults();
    for (size_t i = 0; i < DROID_PART_COUNT; ++i) {
        TEST_ASSERT_TRUE(droidFittedPartsFit(&saved.fitted, droidPartIdAt(i)));
    }

    MapWriter writer;
    TEST_ASSERT_TRUE(configSerializeDroidBuild(saved, writer));

    MapReader reader;
    copyInto(writer, &reader);
    DroidBuildConfig loaded = {};
    DroidBuildRepairReport report = {};
    configDeserializeDroidBuild(reader, &loaded, &report);

    TEST_ASSERT_TRUE(droidBuildRepairReportIsClean(report));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)DROID_PART_COUNT,
                             (uint32_t)droidFittedPartsCount(loaded.fitted));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_a_fresh_controller_starts_on_the_pre_selected_design);
    RUN_TEST(test_a_common_addition_is_not_seeded_by_any_design);

    RUN_TEST(test_the_part_vocabulary_is_the_whole_catalog_whatever_is_fitted);
    RUN_TEST(test_a_part_no_design_carries_can_still_be_fitted);
    RUN_TEST(test_a_part_this_build_cannot_name_is_not_fitted);

    RUN_TEST(test_a_design_the_catalog_declares_is_known_at_its_own_variants);
    RUN_TEST(test_a_mismatched_pairing_is_an_ordinary_droid);
    RUN_TEST(test_an_id_too_long_for_the_field_is_refused_not_truncated);

    RUN_TEST(test_a_stored_legacy_variant_reads_as_the_variant_it_became);
    RUN_TEST(test_every_field_round_trips_through_storage);
    RUN_TEST(test_a_controller_that_has_never_been_answered_takes_the_default);
    RUN_TEST(test_a_droid_with_nothing_fitted_is_an_answer_and_survives);
    RUN_TEST(test_a_stored_design_this_build_cannot_name_is_repaired_and_counted);
    RUN_TEST(test_a_stored_roadmap_design_restores_to_the_default);
    RUN_TEST(test_a_stored_variant_that_is_not_its_design_s_takes_the_default_pair);
    RUN_TEST(test_a_stored_part_this_build_no_longer_declares_is_dropped_and_counted);
    RUN_TEST(test_the_stored_part_list_tolerates_spacing_and_refuses_an_overlong_token);
    RUN_TEST(test_every_part_in_the_catalog_can_be_stored_and_read_back);

    return UNITY_END();
}
