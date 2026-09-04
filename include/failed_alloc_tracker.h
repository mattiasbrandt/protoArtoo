// =============================================================================
// include/failed_alloc_tracker.h
//
// Heap failed-allocation counter, compiled into EVERY build.
//
// The count answers one question -- "did anything fail to allocate?" -- and
// ADR 0017's heap acceptance rule reads it on a PRODUCTION image, where the
// profiler (PA_HEAP_PROFILE) is compiled out. So the counter, the IDF hook
// that feeds it, and its accessors live here rather than inside
// src/web/api_profiler.cpp's #if: /api/status reports the count on every
// build, and /api/profiler reports the same number plus the last failure's
// detail when the profiler is compiled in. One counter, two readers.
//
// IDF keeps exactly ONE failed-alloc hook: heap_caps_register_failed_alloc_callback()
// stores into a single `alloc_failed_callback` pointer, so a second registration
// silently replaces the first (framework-espidf components/heap/heap_caps.c:27,62-71).
// That is why this module owns the hook outright instead of the profiler keeping
// a second one for its backtrace.
//
// Architecture (ADR 0005/0014 -- pure step core + thin adapter, the shape
// include/queue_drop_tracker.h already uses):
//   - failedAllocTrackerBegin()/End() are the pure counting core: state in,
//     decision out. No I/O, no IDF, natively testable.
//   - src/failed_alloc_tracker.cpp is the thin adapter: it registers the IDF
//     hook, captures the backtrace (Xtensa only) and publishes the readings.
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Backtrace frames captured for the most recent failed allocation. Sizes both
// the tracker's own array and ProfilerReading::lastFailBt, which copies it --
// one constant, so the two arrays cannot drift apart.
#define FAILED_ALLOC_BT_MAX 12

// The counting state itself (pure data, no I/O).
//
// `inCallback` is a re-entry guard, not a cross-core lock, and it is
// deliberately not an atomic test-and-set: it defends against the recursive
// shape described in the adapter (a failing allocation whose handler allocates
// and fails again), which re-enters on the SAME task's stack.
struct FailedAllocTrackerState {
    uint32_t count;             // total failed allocations since registration
    volatile bool inCallback;   // re-entry guard for the detail capture below
    volatile uint32_t lastSize; // bytes requested by the most recent failure
    volatile uint32_t lastCaps; // MALLOC_CAP_* mask of that request
};

// Count one failed allocation and claim the detail capture.
//
// The count is raised for EVERY failure, including a re-entrant one - it is the
// number ADR 0017 reads, and a dropped increment would under-report the very
// condition under test. The detail (size/caps, and the caller's backtrace) is
// captured only by the outermost call; a re-entrant call returns false and
// leaves the outer call's detail intact.
//
// Returns true when the caller owns the capture and must pair the call with
// failedAllocTrackerEnd().
inline bool failedAllocTrackerBegin(FailedAllocTrackerState& state, uint32_t requestedSize,
                                    uint32_t caps) {
    __atomic_fetch_add(&state.count, 1U, __ATOMIC_RELAXED);

    if (state.inCallback) {
        return false;
    }
    state.inCallback = true;
    state.lastSize = requestedSize;
    state.lastCaps = caps;
    return true;
}

// Release the detail capture claimed by failedAllocTrackerBegin(). Called only
// after the caller has finished writing the backtrace, so a nested failure
// during that walk still cannot overwrite the record being built.
inline void failedAllocTrackerEnd(FailedAllocTrackerState& state) {
    state.inCallback = false;
}

// One consistent-enough read of the tracker for /api/profiler. The fields are
// copied one at a time without a critical section, exactly as the profiler read
// them when it owned them: this is a diagnostic record about an event that has
// already happened, not a correctness-bearing structure.
struct FailedAllocReading {
    uint32_t count;
    uint32_t lastSize;
    uint32_t lastCaps;
    uint8_t btDepth;                 // 0 on RISC-V - esp_backtrace_* is Xtensa-only
    uint32_t bt[FAILED_ALLOC_BT_MAX];
};

// Register the IDF failed-alloc hook. Call once at boot, on every build.
void failedAllocTrackerInit();

// Total failed allocations since registration. The one number /api/status
// publishes as "failedAllocs".
uint32_t failedAllocTrackerCount();

// Count plus the most recent failure's detail, for /api/profiler. lastSize,
// lastCaps and the backtrace are meaningful only when count > 0.
void failedAllocTrackerRead(FailedAllocReading* out);
