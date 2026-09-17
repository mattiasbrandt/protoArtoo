// =============================================================================
// src/tasks/audio_catalog_gate.cpp
//
// See include/audio_catalog_gate.h for what this coordinates and why the three
// facts live in one place.
// =============================================================================

#include "audio_catalog_gate.h"

#include <freertos/FreeRTOS.h>

namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// 1. The reader gate.
uint8_t s_readersInside = 0;
bool s_gateClosed = false;

// 2. The refresh ledger.
AudioCatalogRefreshLedger s_ledger{};

// 3. What the last discovery observed.
AudioCatalogObservation s_observation{};

// 4. The saved-bindings warning.
AudioBindingWarning s_warning{};

// 5. The interruption watch.
uint32_t s_stopRequests = 0;

bool isTerminal(AudioCatalogRefreshState state) {
    return state == AudioCatalogRefreshState::Completed ||
           state == AudioCatalogRefreshState::Blocked ||
           state == AudioCatalogRefreshState::Failed ||
           state == AudioCatalogRefreshState::Interrupted;
}

}  // namespace

// -----------------------------------------------------------------------------
// 1. The reader gate
// -----------------------------------------------------------------------------

bool audioCatalogReaderAcquire() {
    bool admitted = false;
    taskENTER_CRITICAL(&s_mux);
    // 255 readers is not a real state on a server that serialises its handlers;
    // the ceiling is here so a leaked release can never wrap the count back to
    // zero and let a refresh delete storage a reader is still walking.
    if (!s_gateClosed && s_readersInside < 255u) {
        ++s_readersInside;
        admitted = true;
    }
    taskEXIT_CRITICAL(&s_mux);
    return admitted;
}

void audioCatalogReaderRelease() {
    taskENTER_CRITICAL(&s_mux);
    if (s_readersInside > 0) {
        --s_readersInside;
    }
    taskEXIT_CRITICAL(&s_mux);
}

void audioCatalogGateClose() {
    taskENTER_CRITICAL(&s_mux);
    s_gateClosed = true;
    taskEXIT_CRITICAL(&s_mux);
}

uint8_t audioCatalogReadersInside() {
    taskENTER_CRITICAL(&s_mux);
    const uint8_t inside = s_readersInside;
    taskEXIT_CRITICAL(&s_mux);
    return inside;
}

void audioCatalogGateOpen() {
    taskENTER_CRITICAL(&s_mux);
    s_gateClosed = false;
    taskEXIT_CRITICAL(&s_mux);
}

// -----------------------------------------------------------------------------
// 2. The refresh ledger
// -----------------------------------------------------------------------------

uint32_t audioCatalogRefreshRequested() {
    taskENTER_CRITICAL(&s_mux);
    ++s_ledger.requestId;
    const uint32_t id = s_ledger.requestId;
    taskEXIT_CRITICAL(&s_mux);
    return id;
}

uint32_t audioCatalogRefreshBegin() {
    taskENTER_CRITICAL(&s_mux);
    // An outstanding request is one that was handed out and has not settled.
    // Anything else is a refresh nobody asked for through the API -- the
    // Console can reach the same command -- and it gets a number of its own so
    // its outcome is on the same ledger rather than nowhere.
    if (s_ledger.requestId == s_ledger.settledId) {
        ++s_ledger.requestId;
    }
    s_ledger.activeId = s_ledger.requestId;
    const uint32_t id = s_ledger.activeId;
    taskEXIT_CRITICAL(&s_mux);
    return id;
}

void audioCatalogRefreshSettled(uint32_t requestId, AudioCatalogRefreshState state) {
    if (!isTerminal(state)) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    if (requestId >= s_ledger.settledId) {
        s_ledger.settledId = requestId;
        s_ledger.settledState = state;
    }
    if (s_ledger.activeId == requestId) {
        s_ledger.activeId = 0;
    }
    taskEXIT_CRITICAL(&s_mux);
}

void audioCatalogRefreshSettleOutstanding(AudioCatalogRefreshState state) {
    if (!isTerminal(state)) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    const bool outstanding = s_ledger.requestId > s_ledger.settledId;
    const uint32_t id = s_ledger.requestId;
    taskEXIT_CRITICAL(&s_mux);
    if (outstanding) {
        audioCatalogRefreshSettled(id, state);
    }
}

void audioCatalogRefreshLedgerRead(AudioCatalogRefreshLedger* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    *out = s_ledger;
    taskEXIT_CRITICAL(&s_mux);
}

// -----------------------------------------------------------------------------
// 3. What the last discovery observed
// -----------------------------------------------------------------------------

void audioCatalogObservationPublish(const AudioCatalogObservation& observation) {
    taskENTER_CRITICAL(&s_mux);
    s_observation = observation;
    taskEXIT_CRITICAL(&s_mux);
}

void audioCatalogObservationRead(AudioCatalogObservation* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    *out = s_observation;
    taskEXIT_CRITICAL(&s_mux);
}

// -----------------------------------------------------------------------------
// 4. The saved-bindings warning
// -----------------------------------------------------------------------------

void audioBindingWarningEvaluate(const AudioSoundListIdentity& saved,
                                 const AudioSoundListIdentity& observed) {
    taskENTER_CRITICAL(&s_mux);
    if (!observed.observed) {
        // Nothing was seen, so nothing was learned. Whatever the warning said
        // before still stands, and soundListChecked stays where it was so the
        // page can say the check could not be made.
        taskEXIT_CRITICAL(&s_mux);
        return;
    }
    s_warning.soundListChecked = true;
    if (!saved.observed) {
        // No baseline: the builder has not saved an assignment against this
        // card yet, so there is nothing for the card to have drifted from.
        s_warning.soundListChanged = false;
    } else {
        s_warning.soundListChanged = (saved.checksum != observed.checksum);
    }
    taskEXIT_CRITICAL(&s_mux);
}

void audioBindingWarningClear() {
    taskENTER_CRITICAL(&s_mux);
    s_warning.soundListChanged = false;
    taskEXIT_CRITICAL(&s_mux);
}

void audioBindingWarningRead(AudioBindingWarning* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    *out = s_warning;
    taskEXIT_CRITICAL(&s_mux);
}

// -----------------------------------------------------------------------------
// 5. The interruption watch
// -----------------------------------------------------------------------------

void audioCatalogInterruptNoteStop() {
    taskENTER_CRITICAL(&s_mux);
    ++s_stopRequests;
    taskEXIT_CRITICAL(&s_mux);
}

uint32_t audioCatalogInterruptStopCount() {
    taskENTER_CRITICAL(&s_mux);
    const uint32_t count = s_stopRequests;
    taskEXIT_CRITICAL(&s_mux);
    return count;
}

bool audioCatalogInterruptFired(uint32_t stopCountAtStart, bool sleepMode) {
    return sleepMode || audioCatalogInterruptStopCount() != stopCountAtStart;
}

// -----------------------------------------------------------------------------

void audioCatalogGateResetForTest() {
    taskENTER_CRITICAL(&s_mux);
    s_readersInside = 0;
    s_gateClosed = false;
    s_ledger = AudioCatalogRefreshLedger{};
    s_observation = AudioCatalogObservation{};
    s_warning = AudioBindingWarning{};
    s_stopRequests = 0;
    taskEXIT_CRITICAL(&s_mux);
}
