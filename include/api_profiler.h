// =============================================================================
// include/api_profiler.h
//
// PA_HEAP_PROFILE=1 gated heap profiling interface.
//
// Instrumentation call sites use this interface unconditionally. When the
// Build Feature Flag is 0, inline stubs compile away; route and translation-unit
// boundaries remain the only compile-time gates.
// =============================================================================
#pragma once

#include <stdint.h>

#if PA_HEAP_PROFILE

#include "web_request.h"

// Call once at task start (SafetyMonitorTask) to register the failed-alloc
// callback and open the initial "boot" monitoring window.
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
// to finish the same entry after the handler returns.
uint8_t profilerRequestStarted(const char* path, uint32_t startMs);
void profilerRequestFinished(uint8_t token, uint32_t handlerDoneMs);

// Profiler endpoints, bound by the seam route table (ADR 0021). All are absent
// (404) when PA_HEAP_PROFILE=0, which is why both this block and
// their registration sit behind the same guard.
//
//   GET  /api/profiler               - live heap/HWM/snapshot JSON
void handleProfilerGet(WebRequest& req);

#if PA_HEAP_TRACING
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
inline uint8_t profilerRequestStarted(const char*, uint32_t) { return 0; }
inline void profilerRequestFinished(uint8_t, uint32_t) {}

#endif  // PA_HEAP_PROFILE
