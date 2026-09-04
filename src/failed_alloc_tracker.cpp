// =============================================================================
// src/failed_alloc_tracker.cpp
//
// Thin adapter for the always-compiled failed-allocation counter
// (include/failed_alloc_tracker.h): registers the one IDF failed-alloc hook,
// counts through the pure core, and publishes the readings /api/status and
// /api/profiler render.
//
// Real-time safety: the hook allocates nothing and takes no lock, because it
// runs on the stack of whichever task hit the failure - including a Core 1
// real-time task.
// =============================================================================

#include "failed_alloc_tracker.h"

#include <esp_debug_helpers.h>  // esp_backtrace_get_start/next_frame
#include <esp_heap_caps.h>

#include "logging.h"

static const char* TAG = "FailedAlloc";

static FailedAllocTrackerState s_state = {};

// Backtrace of the most recent failed allocation. Kept beside the state rather
// than inside it so the pure core stays free of the target-specific half.
static uint32_t s_lastFailBt[FAILED_ALLOC_BT_MAX];
static volatile uint8_t s_lastFailBtDepth = 0;

// The failed-allocation hook. Captured ALLOCATION-FREE for /api/profiler to
// report.
//
// This hook runs IN the context of the failing allocation, on the stack of
// whichever task hit it (IDF heap_caps.c: heap_caps_alloc_failed calls the hook
// inline, then optionally aborts). It MUST NOT allocate: a previous version
// logged via Arduino Print::printf, which mallocs its own buffer - so under heap
// exhaustion that malloc ALSO failed and re-entered this hook, recursing until a
// task stack overflowed (coredump analysis found a 64-byte mDNS alloc crash on the
// lwIP 'tiT' task). IDF's own abort path
// (fmt_abort_str/hex_to_str) likewise formats with manual hex + memcpy, never
// printf. So: only count, capture raw values + backtrace PCs (esp_backtrace_*
// walks the stack and does not allocate), guard against re-entry, and let the
// /api/profiler handler format them where allocation is safe.
static void failedAllocCb(size_t requested_size, uint32_t caps, const char* function_name) {
    (void)function_name;

    if (!failedAllocTrackerBegin(s_state, (uint32_t)requested_size, caps)) {
        // Re-entrant call: counted, but the outer call owns the detail.
        return;
    }

    // Backtrace capture is Xtensa-only. ESP-IDF DECLARES esp_backtrace_get_start()
    // and esp_backtrace_get_next_frame() in esp_debug_helpers.h for every target, but
    // only IMPLEMENTS them for Xtensa -- they are hand-written assembly that walks
    // register windows, which RISC-V does not have. So on the ESP32-P4 the calls
    // compile cleanly and the link fails with "undefined reference"; verified against
    // the framework archives, where the symbol is defined once for esp32 and zero
    // times for esp32p4_es.
    //
    // The rest of the record -- the failure count, requested size and caps -- is
    // architecture-neutral and still the most useful part, so RISC-V keeps it and
    // simply reports a zero-depth backtrace. /api/profiler already renders a depth of
    // 0 as an empty list, so no consumer changes.
#if CONFIG_IDF_TARGET_ARCH_XTENSA
    esp_backtrace_frame_t frame;
    esp_backtrace_get_start(&frame.pc, &frame.sp, &frame.next_pc);
    uint8_t depth = 0;
    for (; depth < FAILED_ALLOC_BT_MAX; ++depth) {
        s_lastFailBt[depth] = frame.pc;
        if (!esp_backtrace_get_next_frame(&frame)) {
            ++depth;
            break;
        }
    }
    s_lastFailBtDepth = depth;
#else
    s_lastFailBtDepth = 0;
#endif

    failedAllocTrackerEnd(s_state);
}

void failedAllocTrackerInit() {
    // The only documented failure is a NULL callback
    // (framework-espidf components/heap/heap_caps.c:62-66), so this cannot fail
    // here - but a silent registration failure would leave failedAllocs flat
    // forever and read exactly like a healthy board, which is the one reading
    // ADR 0017's rule cannot afford to get wrong.
    esp_err_t err = heap_caps_register_failed_alloc_callback(failedAllocCb);
    if (err != ESP_OK) {
        PA_LOG_ERROR(TAG, "failed-alloc hook not registered (err %d); failedAllocs stays 0",
                     (int)err);
    }
}

uint32_t failedAllocTrackerCount() {
    return __atomic_load_n(&s_state.count, __ATOMIC_RELAXED);
}

void failedAllocTrackerRead(FailedAllocReading* out) {
    if (out == nullptr) {
        return;
    }
    out->count = __atomic_load_n(&s_state.count, __ATOMIC_RELAXED);
    out->lastSize = s_state.lastSize;
    out->lastCaps = s_state.lastCaps;
    out->btDepth = s_lastFailBtDepth;
    for (uint8_t i = 0; i < FAILED_ALLOC_BT_MAX; ++i) {
        out->bt[i] = s_lastFailBt[i];
    }
}
