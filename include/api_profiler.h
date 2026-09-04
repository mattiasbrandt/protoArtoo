// =============================================================================
// include/api_profiler.h
//
// PA_HEAP_PROFILE=1 gated heap profiling interface.
//
// Instrumentation call sites use this interface unconditionally. When the
// Build Feature Flag is 0, inline stubs compile away; route and translation-unit
// boundaries remain the only compile-time gates. Gating only the implementation
// (.cpp) while leaving the route registered recreates the header defect: the
// endpoint becomes a false presence. Unconditional no-op stubs here keep call
// sites always-reachable even in builds without the profiler.
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// FAILED_ALLOC_BT_MAX and the counter the reading below copies. The tracker is
// compiled into every build; this header only renders what it already counted.
#include "failed_alloc_tracker.h"

// =============================================================================
// Profiler reading - the shared core beneath every profiler adapter (ADR 0034)
//
// GET /api/profiler and the Controller Console's system.api.get-profiler
// answer from ONE read of the profiler's counters. The HTTP handler renders it
// as JSON, the Console renders it as key=value records; neither owns a second
// way of reading the state, and the muxes stay private to api_profiler.cpp.
//
// These types are declared UNCONDITIONALLY, outside the PA_HEAP_PROFILE guard
// below, even though only a profiler build can fill them. They are plain data,
// and the Console's rendering of them - the half a host test can actually
// exercise - would otherwise sit behind a flag whose whole purpose is to
// remove an ESP32-only measurement from the image
// (include/console_profiler_view.h).
// =============================================================================

#define PROF_LABEL_MAX 20
#define PROF_SNAPSHOT_MAX 8
// Thirteen project-created tasks plus loopTask. Raised from 11 at #271, when
// the three created outside src/main.cpp joined the list (api_profiler.cpp).
#define PROF_TASK_MAX 14
#define PROF_REQUEST_PATH_MAX 28
#define PROF_REQUEST_TRACE_MAX 32

// One closed mode window: the heap low-water mark measured while it was open.
typedef struct {
    char label[PROF_LABEL_MAX];
    uint32_t heapMinDuring;        // minimum_free_bytes at window close
    uint32_t largestBlockAtClose;  // largest free block at window close
    uint32_t windowOpenTs;         // millis() when the window opened
} ProfilerWindowSnapshot;

// One task's stack high-water mark. `found` is false when the task is not
// running in this image, which is why it is reported rather than assumed:
// a task that is never listed reads exactly like a task that is disabled.
typedef struct {
    const char* name;
    uint32_t hwmBytes;
    bool found;
} ProfilerTaskStack;

// One admitted HTTP request's lifecycle. handlerDoneMs is "response written",
// not "response ready" - see the ring's definition in api_profiler.cpp.
typedef struct {
    char path[PROF_REQUEST_PATH_MAX];
    uint32_t startMs;
    uint32_t handlerDoneMs;
} ProfilerRequestTrace;

typedef struct {
    // Tier 1 globals, read straight from the IDF heap APIs
    uint32_t heapFree;
    uint32_t heapMin;
    uint32_t heapLargest;
    float fragRatio;
    uint32_t allocBlocks;
    uint32_t freeBlocks;
    uint32_t totalBlocks;
    uint32_t windowMinFree;  // multi_heap_info_t.minimum_free_bytes - the
                             // running low-water mark of the open window

    // Failed allocations. lastFail* are meaningful only when failedAllocs > 0,
    // and the backtrace is empty on RISC-V (esp_backtrace_* is Xtensa-only).
    uint32_t failedAllocs;
    uint32_t lastFailSize;
    uint32_t lastFailCaps;
    uint8_t lastFailBtDepth;
    uint32_t lastFailBt[FAILED_ALLOC_BT_MAX];

    // The currently open mode window, if any
    bool currentWindowOpen;
    char currentWindowLabel[PROF_LABEL_MAX];

    // Closed mode windows, OLDEST FIRST - the ring's own head/count arithmetic
    // is resolved here so no adapter repeats it.
    ProfilerWindowSnapshot windows[PROF_SNAPSHOT_MAX];
    uint8_t windowCount;

    ProfilerTaskStack taskStacks[PROF_TASK_MAX];
} ProfilerReading;

// How much stack headroom a task has left, as the operator-facing word both
// adapters print. Defined once here rather than per adapter: two renderings of
// one measurement disagreeing about where "warn" starts is exactly the kind of
// drift the shared reading exists to prevent.
static inline const char* profilerHwmStatus(uint32_t hwmBytes) {
    if (hwmBytes > 2048U) return "ok";
    if (hwmBytes > 1024U) return "warn";
    return "crit";
}

#if PA_HEAP_PROFILE

#include "web_request.h"

// Call once at task start (SafetyMonitorTask) to open the initial "boot"
// monitoring window. Registering the failed-alloc hook is NOT part of this:
// that counter runs on every build and is registered by
// failedAllocTrackerInit() beside this call.
void profilerInit();

// Close the current monitoring window (reading its local low-water mark via
// heap_caps_get_info before stopping) and open a new window with newLabel.
// Each closed window is stored as a snapshot entry in the ring.
// Call from SafetyMonitorTask on mode transitions (RC link, dome connect, etc.).
void profilerModeTransition(const char* newLabel);

// Collect per-task stack HWM for all known tasks via xTaskGetHandle().
// Should be called at ~1 Hz from SafetyMonitorTask.
void profilerCollectHwm();

// Collect per-task heap allocation stats via heap_caps_alloc_all_task_stat_arrays
// + heap_caps_get_all_task_stat + heap_caps_free_all_task_stat_arrays.
// This is a no-op inside profiler builds without CONFIG_HEAP_TASK_TRACKING.
// Should be called at ~1 Hz from SafetyMonitorTask.
void profilerCollectTaskHeap();

// Observe profiler-only audio/SSE transitions without making normal builds
// read state or call the web server merely to feed a compiled-out feature.
void profilerObserveOptionalSubsystems();
void profilerPeriodicCollect();

// Bounded request lifecycle trace. The token is an opaque ring index used only
// to finish the same entry after the handler returns. The profiler owns its
// timestamp reads so a disabled hook cannot leave millis() work at the caller.
uint8_t profilerRequestStarted(const char* path);
void profilerRequestFinished(uint8_t token);

// One consistent read of every counter above, for whichever adapter is
// rendering it. Each block is internally consistent; the blocks are captured
// in a fixed mux order but are NOT atomic with respect to each other - see the
// definition for why holding all of them across the copy would be worse.
void profilerRead(ProfilerReading* out);

// The bounded request-lifecycle ring, indexed oldest-first. Streamed one entry
// at a time rather than copied whole (the copyLogLineAt() shape #239
// established for the log ring) so a caller on the Console task - 5120 B of
// stack, src/main.cpp - does not need a kilobyte of it for a diagnostic list.
size_t profilerRequestTraceCount(void);
bool profilerRequestTraceAt(size_t index, ProfilerRequestTrace* out);

// Profiler endpoints, bound by the seam route table (ADR 0021). All are absent
// (404) when PA_HEAP_PROFILE=0, which is why both this block and
// their registration sit behind the same guard.
//
//   GET  /api/profiler               - live heap/HWM/snapshot JSON
void handleProfilerGet(WebRequest& req);

#if PA_HEAP_TRACING
// Tier 3 leak-trace cores, shared by the two POST routes below and by the
// Console's system.action.profiler-trace-start|stop (#224, ADR 0034). The
// running/not-running distinction lives here rather than in each adapter,
// because it is one piece of device state and two opinions of it is how a
// trace gets started twice.
//
// NOTE: no environment in platformio.ini sets PA_HEAP_TRACING=1 - it is a
// documented manual escalation (the artoo_esp32_profiler section there) that
// also needs CONFIG_HEAP_TRACING=y in a custom sdkconfig. So this block, like
// the two handlers it has always guarded, is compiled by no image the repo
// currently builds; on every image that does exist both operations answer
// unavailable reason=not-in-this-build from the Console's build guard.
typedef enum {
    PROFILER_TRACE_STARTED = 0,
    PROFILER_TRACE_STOPPED = 1,
    PROFILER_TRACE_ALREADY_RUNNING = 2,
    PROFILER_TRACE_NOT_RUNNING = 3,
    PROFILER_TRACE_FAILED = 4,
} ProfilerTraceOutcome;

ProfilerTraceOutcome profilerTraceStart(void);
ProfilerTraceOutcome profilerTraceStop(void);

//   POST /api/profiler/trace/start   - start Tier 3 leak trace
//   POST /api/profiler/trace/stop    - stop + dump Tier 3 leak trace to serial
void handleProfilerTraceStartPost(WebRequest& req);
void handleProfilerTraceStopPost(WebRequest& req);
#endif

#else

inline void profilerInit() {}
inline void profilerModeTransition(const char*) {}
inline void profilerCollectHwm() {}
inline void profilerCollectTaskHeap() {}
inline void profilerObserveOptionalSubsystems() {}
inline void profilerPeriodicCollect() {}
inline uint8_t profilerRequestStarted(const char*) { return 0; }
inline void profilerRequestFinished(uint8_t) {}

#endif  // PA_HEAP_PROFILE
