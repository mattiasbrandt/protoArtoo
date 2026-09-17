// =============================================================================
// test_audio_catalog_gate
//
// The coordination between a catalog refresh and everything that reads what it
// produced (#397 work items 4, 9, 10, 13).
//
// The exposure this exists for: GET /api/audio/catalog borrows the driver's
// bank and entry arrays and re-walks them once per HTTP chunk while the body
// goes out, and refreshCatalog() deletes and replaces the entry allocation when
// it grows. Zeroing the driver's own count does not help -- the reader holds a
// pointer and a count of its own -- so the two have to be kept apart, which is
// what the gate does.
// =============================================================================

#include <unity.h>

#include <stdint.h>

#include "../../../include/audio_catalog_gate.h"

void setUp() {
    audioCatalogGateResetForTest();
}

void tearDown() {
    audioCatalogGateResetForTest();
}

// -----------------------------------------------------------------------------
// The reader gate
// -----------------------------------------------------------------------------

void test_a_reader_is_counted_while_it_is_inside() {
    TEST_ASSERT_EQUAL_UINT8(0, audioCatalogReadersInside());
    TEST_ASSERT_TRUE(audioCatalogReaderAcquire());
    TEST_ASSERT_EQUAL_UINT8(1, audioCatalogReadersInside());
    audioCatalogReaderRelease();
    TEST_ASSERT_EQUAL_UINT8(0, audioCatalogReadersInside());
}

void test_a_closed_gate_refuses_a_new_reader() {
    audioCatalogGateClose();
    TEST_ASSERT_FALSE_MESSAGE(
        audioCatalogReaderAcquire(),
        "a reader admitted while a refresh holds the gate would be handed a pointer whose "
        "lifetime nobody controls");
    audioCatalogGateOpen();
    TEST_ASSERT_TRUE(audioCatalogReaderAcquire());
    audioCatalogReaderRelease();
}

// The refresh side's whole question: is anyone still walking the storage I am
// about to replace?
void test_closing_the_gate_does_not_evict_a_reader_already_inside() {
    TEST_ASSERT_TRUE(audioCatalogReaderAcquire());
    audioCatalogGateClose();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1, audioCatalogReadersInside(),
        "the reader is mid-send; the refresh has to wait for it, not pretend it left");
    audioCatalogReaderRelease();
    TEST_ASSERT_EQUAL_UINT8(0, audioCatalogReadersInside());
}

void test_two_readers_are_both_waited_for() {
    TEST_ASSERT_TRUE(audioCatalogReaderAcquire());
    TEST_ASSERT_TRUE(audioCatalogReaderAcquire());
    TEST_ASSERT_EQUAL_UINT8(2, audioCatalogReadersInside());
    audioCatalogReaderRelease();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, audioCatalogReadersInside(),
                                    "one reader leaving does not mean the storage is free");
    audioCatalogReaderRelease();
    TEST_ASSERT_EQUAL_UINT8(0, audioCatalogReadersInside());
}

void test_an_unbalanced_release_cannot_wrap_the_count() {
    audioCatalogReaderRelease();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, audioCatalogReadersInside(),
        "wrapping to 255 would read as readers that do not exist, which never drains");
}

// -----------------------------------------------------------------------------
// The refresh ledger
// -----------------------------------------------------------------------------

void test_each_request_gets_its_own_number() {
    TEST_ASSERT_EQUAL_UINT32(1u, audioCatalogRefreshRequested());
    TEST_ASSERT_EQUAL_UINT32(2u, audioCatalogRefreshRequested());

    AudioCatalogRefreshLedger ledger{};
    audioCatalogRefreshLedgerRead(&ledger);
    TEST_ASSERT_EQUAL_UINT32(2u, ledger.requestId);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, ledger.settledId,
                                     "asking for a refresh settles nothing on its own");
}

void test_a_run_claims_the_outstanding_request() {
    const uint32_t asked = audioCatalogRefreshRequested();
    const uint32_t claimed = audioCatalogRefreshBegin();
    TEST_ASSERT_EQUAL_UINT32(asked, claimed);

    AudioCatalogRefreshLedger ledger{};
    audioCatalogRefreshLedgerRead(&ledger);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(asked, ledger.activeId,
                                     "a caller watching this request has to be able to see it run");

    audioCatalogRefreshSettled(claimed, AudioCatalogRefreshState::Completed);
    audioCatalogRefreshLedgerRead(&ledger);
    TEST_ASSERT_EQUAL_UINT32(0u, ledger.activeId);
    TEST_ASSERT_EQUAL_UINT32(asked, ledger.settledId);
    TEST_ASSERT_EQUAL_INT(AudioCatalogRefreshState::Completed, ledger.settledState);
}

// The Console can reach the same command without going through the API, and an
// outcome nobody can find is the defect this ledger exists to remove.
void test_a_refresh_nobody_asked_for_still_gets_a_number() {
    const uint32_t minted = audioCatalogRefreshBegin();
    TEST_ASSERT_EQUAL_UINT32(1u, minted);
    audioCatalogRefreshSettled(minted, AudioCatalogRefreshState::Failed);

    AudioCatalogRefreshLedger ledger{};
    audioCatalogRefreshLedgerRead(&ledger);
    TEST_ASSERT_EQUAL_UINT32(1u, ledger.settledId);
    TEST_ASSERT_EQUAL_INT(AudioCatalogRefreshState::Failed, ledger.settledState);
}

void test_an_older_outcome_cannot_overwrite_a_newer_one() {
    const uint32_t first = audioCatalogRefreshRequested();
    const uint32_t second = audioCatalogRefreshRequested();
    audioCatalogRefreshSettled(second, AudioCatalogRefreshState::Completed);
    audioCatalogRefreshSettled(first, AudioCatalogRefreshState::Failed);

    AudioCatalogRefreshLedger ledger{};
    audioCatalogRefreshLedgerRead(&ledger);
    TEST_ASSERT_EQUAL_UINT32(second, ledger.settledId);
    TEST_ASSERT_EQUAL_INT_MESSAGE(AudioCatalogRefreshState::Completed, ledger.settledState,
                                  "a slow loser must not report its outcome over a newer one");
}

// The bug this whole item is about: an older catalog that is still ready is not
// the refresh a caller just asked for.
void test_an_old_completed_refresh_does_not_settle_a_new_request() {
    const uint32_t first = audioCatalogRefreshRequested();
    audioCatalogRefreshSettled(audioCatalogRefreshBegin(), AudioCatalogRefreshState::Completed);

    const uint32_t second = audioCatalogRefreshRequested();
    AudioCatalogRefreshLedger ledger{};
    audioCatalogRefreshLedgerRead(&ledger);

    TEST_ASSERT_EQUAL_UINT32(second, ledger.requestId);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        first, ledger.settledId,
        "the completed refresh is the OLD one; a caller watching the new request must still be "
        "waiting, not reading this as its own success");
    TEST_ASSERT_TRUE(ledger.settledId < ledger.requestId);
}

void test_a_non_terminal_state_never_settles_anything() {
    const uint32_t id = audioCatalogRefreshRequested();
    audioCatalogRefreshSettled(id, AudioCatalogRefreshState::Running);

    AudioCatalogRefreshLedger ledger{};
    audioCatalogRefreshLedgerRead(&ledger);
    TEST_ASSERT_EQUAL_UINT32(0u, ledger.settledId);
    TEST_ASSERT_EQUAL_INT(AudioCatalogRefreshState::None, ledger.settledState);
}

// -----------------------------------------------------------------------------
// The saved-bindings warning
// -----------------------------------------------------------------------------

static AudioSoundListIdentity identity(bool observed, uint32_t checksum) {
    AudioSoundListIdentity id{};
    id.observed = observed;
    id.checksum = checksum;
    return id;
}

void test_a_changed_sound_list_raises_the_warning() {
    audioBindingWarningEvaluate(identity(true, 111u), identity(true, 222u));

    AudioBindingWarning warning{};
    audioBindingWarningRead(&warning);
    TEST_ASSERT_TRUE(warning.soundListChanged);
    TEST_ASSERT_TRUE(warning.soundListChecked);
}

void test_an_unchanged_sound_list_leaves_the_warning_down() {
    audioBindingWarningEvaluate(identity(true, 111u), identity(true, 111u));

    AudioBindingWarning warning{};
    audioBindingWarningRead(&warning);
    TEST_ASSERT_FALSE(warning.soundListChanged);
    TEST_ASSERT_TRUE(warning.soundListChecked);
}

// Zero is a checksum the module can really send, so it is a value like any
// other -- not a stand-in for "nothing saved".
void test_a_checksum_of_zero_is_a_value() {
    audioBindingWarningEvaluate(identity(true, 0u), identity(true, 7u));

    AudioBindingWarning warning{};
    audioBindingWarningRead(&warning);
    TEST_ASSERT_TRUE_MESSAGE(warning.soundListChanged,
                             "a saved zero is a baseline, and the card no longer matches it");
}

void test_no_saved_baseline_is_nothing_to_warn_about() {
    audioBindingWarningEvaluate(identity(false, 0u), identity(true, 7u));

    AudioBindingWarning warning{};
    audioBindingWarningRead(&warning);
    TEST_ASSERT_FALSE_MESSAGE(warning.soundListChanged,
                              "the builder has not saved an assignment against this card yet");
    TEST_ASSERT_TRUE(warning.soundListChecked);
}

// The latch. Not being able to look is not the same as having looked and found
// nothing, and a truncated manifest arrives exactly when a card has changed.
void test_a_manifest_that_carried_no_checksum_does_not_clear_a_warning() {
    audioBindingWarningEvaluate(identity(true, 111u), identity(true, 222u));
    audioBindingWarningEvaluate(identity(true, 111u), identity(false, 0u));

    AudioBindingWarning warning{};
    audioBindingWarningRead(&warning);
    TEST_ASSERT_TRUE_MESSAGE(warning.soundListChanged,
                             "a manifest with no checksum in it has not shown the card unchanged");
}

void test_saving_assignments_clears_the_warning() {
    audioBindingWarningEvaluate(identity(true, 111u), identity(true, 222u));
    audioBindingWarningClear();

    AudioBindingWarning warning{};
    audioBindingWarningRead(&warning);
    TEST_ASSERT_FALSE(warning.soundListChanged);
}

void test_nothing_observed_yet_reports_an_unchecked_sound_list() {
    audioBindingWarningEvaluate(identity(true, 111u), identity(false, 0u));

    AudioBindingWarning warning{};
    audioBindingWarningRead(&warning);
    TEST_ASSERT_FALSE_MESSAGE(warning.soundListChecked,
                              "the page has to be able to say the check could not be made");
    TEST_ASSERT_FALSE(warning.soundListChanged);
}

// -----------------------------------------------------------------------------
// The interruption watch
// -----------------------------------------------------------------------------

void test_a_walk_with_nothing_asked_of_it_keeps_going() {
    const uint32_t atStart = audioCatalogInterruptStopCount();
    TEST_ASSERT_FALSE(audioCatalogInterruptFired(atStart, false));
}

void test_a_stop_enqueued_during_a_walk_interrupts_it() {
    const uint32_t atStart = audioCatalogInterruptStopCount();
    audioCatalogInterruptNoteStop();
    TEST_ASSERT_TRUE_MESSAGE(
        audioCatalogInterruptFired(atStart, false),
        "a stop counted at enqueue time is seen whatever else is in the queue ahead of it");
}

void test_a_stop_from_before_the_walk_does_not_interrupt_it() {
    audioCatalogInterruptNoteStop();
    const uint32_t atStart = audioCatalogInterruptStopCount();
    TEST_ASSERT_FALSE_MESSAGE(audioCatalogInterruptFired(atStart, false),
                              "the walk started after that stop; it is not what this refresh is for");
}

void test_entering_sleep_interrupts_a_walk() {
    const uint32_t atStart = audioCatalogInterruptStopCount();
    TEST_ASSERT_TRUE(audioCatalogInterruptFired(atStart, true));
}

// -----------------------------------------------------------------------------
// What the last discovery observed
// -----------------------------------------------------------------------------

void test_the_last_observation_is_readable_by_the_web_side() {
    AudioCatalogObservation published{};
    published.completeness.manifestComplete = false;
    published.completeness.missingNameCount = 5;
    published.completeness.entryCapReached = true;
    published.identity.observed = true;
    published.identity.checksum = 4242u;
    audioCatalogObservationPublish(published);

    AudioCatalogObservation read{};
    audioCatalogObservationRead(&read);
    TEST_ASSERT_FALSE(read.completeness.manifestComplete);
    TEST_ASSERT_EQUAL_UINT16(5, read.completeness.missingNameCount);
    TEST_ASSERT_TRUE(read.completeness.entryCapReached);
    TEST_ASSERT_TRUE(read.identity.observed);
    TEST_ASSERT_EQUAL_UINT32(4242u, read.identity.checksum);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_a_reader_is_counted_while_it_is_inside);
    RUN_TEST(test_a_closed_gate_refuses_a_new_reader);
    RUN_TEST(test_closing_the_gate_does_not_evict_a_reader_already_inside);
    RUN_TEST(test_two_readers_are_both_waited_for);
    RUN_TEST(test_an_unbalanced_release_cannot_wrap_the_count);
    RUN_TEST(test_each_request_gets_its_own_number);
    RUN_TEST(test_a_run_claims_the_outstanding_request);
    RUN_TEST(test_a_refresh_nobody_asked_for_still_gets_a_number);
    RUN_TEST(test_an_older_outcome_cannot_overwrite_a_newer_one);
    RUN_TEST(test_an_old_completed_refresh_does_not_settle_a_new_request);
    RUN_TEST(test_a_non_terminal_state_never_settles_anything);
    RUN_TEST(test_a_changed_sound_list_raises_the_warning);
    RUN_TEST(test_an_unchanged_sound_list_leaves_the_warning_down);
    RUN_TEST(test_a_checksum_of_zero_is_a_value);
    RUN_TEST(test_no_saved_baseline_is_nothing_to_warn_about);
    RUN_TEST(test_a_manifest_that_carried_no_checksum_does_not_clear_a_warning);
    RUN_TEST(test_saving_assignments_clears_the_warning);
    RUN_TEST(test_nothing_observed_yet_reports_an_unchecked_sound_list);
    RUN_TEST(test_a_walk_with_nothing_asked_of_it_keeps_going);
    RUN_TEST(test_a_stop_enqueued_during_a_walk_interrupts_it);
    RUN_TEST(test_a_stop_from_before_the_walk_does_not_interrupt_it);
    RUN_TEST(test_entering_sleep_interrupts_a_walk);
    RUN_TEST(test_the_last_observation_is_readable_by_the_web_side);
    return UNITY_END();
}
