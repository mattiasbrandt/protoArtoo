// =============================================================================
// test/test_native/test_guided_setup/test_guided_setup.cpp
//
// Guided Setup's record (#351): where the run stands, and which of its steps the
// builder has actually been shown.
//
// The defect this guards is the one the record exists for. Every component
// toggle on this controller defaults false, so a fresh flash boots inert and a
// category nobody was ever asked about is indistinguishable, in the config
// alone, from one the builder looked at and declined. The tests below are about
// the three readings that difference depends on surviving a round trip through
// NVS: never drawn on, drawn and nothing shown yet, and drawn with steps behind
// it.
//
// The step KEYS are deliberately not checked against any vocabulary here,
// because firmware has none: the run is drawn in the browser and its step list
// grows. What is checked is form, and that a record which came back shorter than
// it went in says so rather than quietly shortening a builder's answer.
// =============================================================================
#include <unity.h>

#include <string.h>

#include <string>

#include "config_serializer.h"
#include "guided_setup.h"
#include "../../../test/stubs/config/map_config_io.h"

void setUp() {
}
void tearDown() {
}

// Round trip through the serializer, the way a save and the next cold boot do.
static void roundTrip(const GuidedSetupConfig& in, GuidedSetupConfig* out,
                      GuidedSetupRepairReport* report) {
    MapWriter writer;
    TEST_ASSERT_TRUE(configSerializeGuidedSetup(in, writer));

    MapReader reader;
    for (const auto& pair : writer.data()) {
        reader.set(pair.first.c_str(), pair.second);
    }
    configDeserializeGuidedSetup(reader, out, report);
}

// A controller guided Setup has never been drawn on. `recorded` false is the
// whole point: it is what lets the browser tell this case from a run that has
// shown nothing yet, and only this case may be read as "whatever is configured
// here was answered before the record existed".
void test_a_controller_with_no_record_reads_as_never_drawn_on() {
    MapReader reader;  // nothing stored at all
    GuidedSetupConfig loaded = {};
    GuidedSetupRepairReport report = {};
    configDeserializeGuidedSetup(reader, &loaded, &report);

    TEST_ASSERT_EQUAL_UINT8(GUIDED_SETUP_NOT_RUN, loaded.run);
    TEST_ASSERT_FALSE(loaded.recorded);
    TEST_ASSERT_EQUAL_STRING("", loaded.visited);
    TEST_ASSERT_TRUE(guidedSetupRepairReportIsClean(report));
}

// A run that has been drawn and has shown nothing yet. This is the case an empty
// string cannot carry, so the writer stores a sentinel and the reader must bring
// back `recorded` true with an empty list.
void test_a_drawn_run_that_has_shown_nothing_is_not_an_absent_record() {
    GuidedSetupConfig saved = {};
    guidedSetupDefaults(&saved);
    saved.recorded = true;

    GuidedSetupConfig loaded = {};
    GuidedSetupRepairReport report = {};
    roundTrip(saved, &loaded, &report);

    TEST_ASSERT_TRUE(loaded.recorded);
    TEST_ASSERT_EQUAL_STRING("", loaded.visited);
    TEST_ASSERT_TRUE(guidedSetupRepairReportIsClean(report));
}

// The steps a builder was shown survive the round trip, in order, and each one
// answers for itself afterwards.
void test_visited_steps_survive_a_save_and_load() {
    GuidedSetupConfig saved = {};
    guidedSetupDefaults(&saved);
    TEST_ASSERT_EQUAL_UINT32(0, guidedSetupVisitedSet(&saved, "wifi,_board,drive"));
    saved.recorded = true;
    saved.run = GUIDED_SETUP_SKIPPED;

    GuidedSetupConfig loaded = {};
    GuidedSetupRepairReport report = {};
    roundTrip(saved, &loaded, &report);

    TEST_ASSERT_EQUAL_UINT8(GUIDED_SETUP_SKIPPED, loaded.run);
    TEST_ASSERT_TRUE(loaded.recorded);
    TEST_ASSERT_EQUAL_STRING("wifi,_board,drive", loaded.visited);
    TEST_ASSERT_TRUE(guidedSetupVisitedHas(loaded, "wifi"));
    TEST_ASSERT_TRUE(guidedSetupVisitedHas(loaded, "_board"));
    TEST_ASSERT_TRUE(guidedSetupVisitedHas(loaded, "drive"));
    TEST_ASSERT_TRUE(guidedSetupRepairReportIsClean(report));
}

// "Skipping is finishing", but the two are not the same fact about a droid: both
// end the run and the surfaces that report on it afterwards must still be able
// to say which happened.
void test_skipped_and_completed_stay_distinguishable() {
    const GuidedSetupRun ends[] = {GUIDED_SETUP_SKIPPED, GUIDED_SETUP_COMPLETED};
    for (size_t i = 0; i < sizeof(ends) / sizeof(ends[0]); ++i) {
        GuidedSetupConfig saved = {};
        guidedSetupDefaults(&saved);
        saved.run = ends[i];
        saved.recorded = true;

        GuidedSetupConfig loaded = {};
        GuidedSetupRepairReport report = {};
        roundTrip(saved, &loaded, &report);

        TEST_ASSERT_EQUAL_UINT8(ends[i], loaded.run);
    }
    TEST_ASSERT_EQUAL_STRING("skipped", guidedSetupRunId(GUIDED_SETUP_SKIPPED));
    TEST_ASSERT_EQUAL_STRING("completed", guidedSetupRunId(GUIDED_SETUP_COMPLETED));
    TEST_ASSERT_EQUAL_STRING("not-run", guidedSetupRunId(GUIDED_SETUP_NOT_RUN));
}

// The summary's dismissal is config (#371): Done survives the next cold boot,
// and a controller that ended its run before the key existed shows the summary
// rather than reading as dismissed.
void test_a_dismissed_summary_stays_dismissed_and_an_absent_key_does_not_dismiss() {
    GuidedSetupConfig saved = {};
    guidedSetupDefaults(&saved);
    saved.run = GUIDED_SETUP_COMPLETED;
    saved.recorded = true;
    saved.summaryDone = true;

    GuidedSetupConfig loaded = {};
    GuidedSetupRepairReport report = {};
    roundTrip(saved, &loaded, &report);
    TEST_ASSERT_TRUE(loaded.summaryDone);

    MapReader older;
    older.set("gsetup_run", (uint32_t)GUIDED_SETUP_COMPLETED);
    // std::string, not a literal: a const char* would pick the bool overload.
    older.set("gsetup_visited", std::string("wifi"));
    configDeserializeGuidedSetup(older, &loaded, &report);
    TEST_ASSERT_EQUAL_UINT8(GUIDED_SETUP_COMPLETED, loaded.run);
    TEST_ASSERT_FALSE(loaded.summaryDone);
}

// A run state this image cannot name reads as a run that has NOT ended, and says
// it repaired something. Erring the other way would lock a builder out of the
// only guided pass over a damaged byte.
void test_an_unknown_stored_run_reopens_the_run_and_is_reported() {
    MapReader reader;
    // std::string, not a bare literal: MapReader::set overloads on
    // std::string/uint32_t/float/bool, and a `const char*` picks the BOOL one -
    // a pointer-to-bool conversion outranks the user-defined one to std::string
    // - so `set("gsetup_run", "7")` silently stores "1". Measured here, not
    // assumed: the assertion below came back "Expected 0 Was 1".
    reader.set("gsetup_run", std::string("7"));
    reader.set("gsetup_visited", std::string(GUIDED_SETUP_VISITED_NONE));

    GuidedSetupConfig loaded = {};
    GuidedSetupRepairReport report = {};
    configDeserializeGuidedSetup(reader, &loaded, &report);

    TEST_ASSERT_EQUAL_UINT8(GUIDED_SETUP_NOT_RUN, loaded.run);
    TEST_ASSERT_TRUE(report.runRepaired);
    TEST_ASSERT_FALSE(guidedSetupRepairReportIsClean(report));
}

// Form, not vocabulary. A key made of characters a key may not contain is
// dropped and counted; one that is merely unfamiliar is kept, because which
// steps exist is the browser's question and its list grows.
void test_a_malformed_key_is_dropped_and_counted_and_an_unfamiliar_one_is_kept() {
    GuidedSetupConfig cfg = {};
    guidedSetupDefaults(&cfg);

    // Two malformed keys: an uppercase letter, and one longer than a key may be.
    TEST_ASSERT_EQUAL_UINT32(2, guidedSetupVisitedSet(&cfg, "wifi,Drive,a_step_nobody_has"));
    TEST_ASSERT_EQUAL_STRING("wifi", cfg.visited);

    // A key this firmware has never heard of, correctly formed: kept.
    TEST_ASSERT_EQUAL_UINT32(0, guidedSetupVisitedSet(&cfg, "wifi,build"));
    TEST_ASSERT_EQUAL_STRING("wifi,build", cfg.visited);
    TEST_ASSERT_TRUE(guidedSetupVisitedHas(cfg, "build"));
}

// The browser marks a step visited every time it draws it, so the list has to
// collapse repeats or the record fills up with one run's redraws.
void test_a_step_drawn_twice_is_carried_once() {
    GuidedSetupConfig cfg = {};
    guidedSetupDefaults(&cfg);
    TEST_ASSERT_EQUAL_UINT32(0, guidedSetupVisitedSet(&cfg, "drive,wifi,drive,drive"));
    TEST_ASSERT_EQUAL_STRING("drive,wifi", cfg.visited);
}

// Whole-token match. A record holding "domectl" must not answer yes for "dome",
// or a step nobody was shown would render as confirmed.
void test_a_prefix_is_not_a_visit() {
    GuidedSetupConfig cfg = {};
    guidedSetupDefaults(&cfg);
    TEST_ASSERT_EQUAL_UINT32(0, guidedSetupVisitedSet(&cfg, "domectl"));
    TEST_ASSERT_FALSE(guidedSetupVisitedHas(cfg, "dome"));
    TEST_ASSERT_FALSE(guidedSetupVisitedHas(cfg, "domectld"));
    TEST_ASSERT_TRUE(guidedSetupVisitedHas(cfg, "domectl"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_controller_with_no_record_reads_as_never_drawn_on);
    RUN_TEST(test_a_drawn_run_that_has_shown_nothing_is_not_an_absent_record);
    RUN_TEST(test_visited_steps_survive_a_save_and_load);
    RUN_TEST(test_skipped_and_completed_stay_distinguishable);
    RUN_TEST(test_an_unknown_stored_run_reopens_the_run_and_is_reported);
    RUN_TEST(test_a_malformed_key_is_dropped_and_counted_and_an_unfamiliar_one_is_kept);
    RUN_TEST(test_a_step_drawn_twice_is_carried_once);
    RUN_TEST(test_a_prefix_is_not_a_visit);
    RUN_TEST(test_a_dismissed_summary_stays_dismissed_and_an_absent_key_does_not_dismiss);
    return UNITY_END();
}
