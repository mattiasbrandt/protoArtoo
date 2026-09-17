// =============================================================================
// include/audio_catalog_gate.h
//
// The coordination point between the AudioTask-owned catalog refresh and
// everything that reads what it produced (#397 work items 4, 9, 10, 12, 13).
//
// Three facts live here, and they live together because a refresh is what
// changes all three at once:
//
//   1. WHO MAY READ the driver's catalog storage right now. The web handler
//      borrows the driver's bank and entry arrays and walks them once per HTTP
//      chunk while the body goes out; a refresh that lands mid-send deletes and
//      replaces the entry allocation under it (audio_chirp.cpp
//      ensureEntryStorage). Zeroing the driver's own count does not help -- the
//      reader is holding a pointer and a count of its own. So a refresh closes
//      this gate, waits for readers already inside to leave, and only then
//      touches storage; a reader that arrives while the gate is shut is told
//      the catalog is busy instead of being handed a pointer with a lifetime
//      nobody controls.
//
//   2. WHICH REFRESH a caller is watching. Accepting a command onto a queue is
//      not the same event as that command finishing, and the Sound page used to
//      read "an older catalog is still ready" as "the refresh I asked for
//      succeeded". Every request gets a number; the page watches its own.
//
//   3. WHAT THE LAST DISCOVERY OBSERVED -- how complete the catalog is, and the
//      checksum the module reported for its sound list. The second is compared
//      against the baseline saved beside the bindings, which is how a card
//      whose files moved under a saved assignment becomes visible.
//
// Threading: the reader side runs on the web server task, the refresh side on
// AudioTask. Every accessor takes a short critical section of its own and none
// is held across I/O of any kind -- the reader takes one to enter, one to
// leave, and none at all while the body is on the wire.
//
// Nothing here knows about FreeRTOS tasks or NVS: the waiting loop and the
// baseline read belong to their owners (src/tasks/audio_task.cpp), which keeps
// this compilable -- and testable -- on the host.
// =============================================================================
#pragma once

#include <stdint.h>

#include "audio_driver.h"

// -----------------------------------------------------------------------------
// 1. The reader gate
// -----------------------------------------------------------------------------

// Enter as a catalog reader. Returns false when a refresh holds the gate, in
// which case the caller must NOT touch the driver's catalog storage and must
// not call the release below. Every successful acquire is paired with exactly
// one release, including on error paths.
bool audioCatalogReaderAcquire();
void audioCatalogReaderRelease();

// Refresh side. Close() stops new readers immediately; readersInside() is what
// the owner waits on before it lets the driver replace anything. Open() lets
// readers back in and is called on every exit path, including a refresh that
// was never started.
void audioCatalogGateClose();
uint8_t audioCatalogReadersInside();
void audioCatalogGateOpen();

// -----------------------------------------------------------------------------
// 2. The refresh ledger
// -----------------------------------------------------------------------------

enum class AudioCatalogRefreshState : uint8_t {
    None = 0,     // nothing has been asked for since boot
    Queued,       // accepted onto the audio command queue, not started
    Running,      // the walk is under way
    Completed,    // the walk ran to the end
    Blocked,      // never started: the UART was busy, or a reader would not leave
    Failed,       // started and could not finish: no manifest, or no storage
    Interrupted,  // cut short by a stop or by entry into sleep
};

struct AudioCatalogRefreshLedger {
    uint32_t requestId = 0;    // the most recent request handed to a caller
    uint32_t activeId = 0;     // the request running right now, 0 when none is
    uint32_t settledId = 0;    // the last request that reached a terminal state
    AudioCatalogRefreshState settledState = AudioCatalogRefreshState::None;
};

// Web side: take the next request number. The caller enqueues the command and,
// if the queue refuses it, settles the number itself so nobody waits on a
// refresh that will never run.
uint32_t audioCatalogRefreshRequested();

// AudioTask side: claim the outstanding request, or mint one for a refresh
// nobody asked for through the API, and mark it running. Returns the id to pass
// back to audioCatalogRefreshSettled().
uint32_t audioCatalogRefreshBegin();

// Record how a request ended. A settle for a request older than one already
// settled is ignored, so a slow loser cannot overwrite a newer outcome.
void audioCatalogRefreshSettled(uint32_t requestId, AudioCatalogRefreshState state);

// Settle whatever request has been handed out and has not settled yet, for a
// caller that knows the refresh cannot run but does not know which number it
// was given. Does nothing when there is no outstanding request.
void audioCatalogRefreshSettleOutstanding(AudioCatalogRefreshState state);

void audioCatalogRefreshLedgerRead(AudioCatalogRefreshLedger* out);

// -----------------------------------------------------------------------------
// 3. What the last discovery observed
// -----------------------------------------------------------------------------

// The module's own checksum of its sound list, and whether one was seen at all.
// "Not observed" is a third state, distinct from any numeric value: a manifest
// whose checksum line was dropped has not shown the sound list unchanged.
struct AudioSoundListIdentity {
    bool observed = false;
    uint32_t checksum = 0;
};

struct AudioCatalogObservation {
    AudioCatalogCompleteness completeness{};
    AudioSoundListIdentity identity{};
};

void audioCatalogObservationPublish(const AudioCatalogObservation& observation);
void audioCatalogObservationRead(AudioCatalogObservation* out);

// -----------------------------------------------------------------------------
// 4. The saved-bindings warning
// -----------------------------------------------------------------------------

struct AudioBindingWarning {
    bool soundListChanged = false;  // the saved baseline and the card disagree
    bool soundListChecked = false;  // the last evaluation had something to compare
};

// Compare the baseline saved beside the bindings against what the module just
// reported. Deliberately LATCHING on the unobserved case: a truncated manifest
// does not clear a warning that is already up, because not being able to look
// is not the same as having looked and found nothing (#397 D4).
void audioBindingWarningEvaluate(const AudioSoundListIdentity& saved,
                                 const AudioSoundListIdentity& observed);

// The builder saved assignments against what the card says today, so whatever
// the warning was about is now the state they chose.
void audioBindingWarningClear();

void audioBindingWarningRead(AudioBindingWarning* out);

// -----------------------------------------------------------------------------
// 5. The interruption watch
// -----------------------------------------------------------------------------

// Noted by the stop-enqueue helpers, whatever else is in the queue ahead of the
// stop: a peek at the queue head would miss a stop queued behind a play, and a
// walk that only notices stops at the head is not interruptible in the case
// that matters. The walk captures the count when it starts and compares.
void audioCatalogInterruptNoteStop();
uint32_t audioCatalogInterruptStopCount();

// True when the walk that started at `stopCountAtStart` should give up now.
bool audioCatalogInterruptFired(uint32_t stopCountAtStart, bool sleepMode);

// Test seam: forget every request, observation, warning and stop. Production
// never calls this -- the state is a boot-to-reboot ledger.
void audioCatalogGateResetForTest();
