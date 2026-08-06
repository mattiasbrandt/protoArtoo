// =============================================================================
// include/api_profiler.h
//
// PA_HEAP_PROFILE=1 gated heap profiling interface.
//
// All functions are no-ops when PA_HEAP_PROFILE is not defined — callers may
// include this header unconditionally and guard call sites with #if PA_HEAP_PROFILE.
// =============================================================================
#pragma once

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

#ifdef CONFIG_HEAP_TASK_TRACKING
// Collect per-task heap allocation stats via heap_caps_alloc_all_task_stat_arrays
// + heap_caps_get_all_task_stat + heap_caps_free_all_task_stat_arrays.
// Only available when CONFIG_HEAP_TASK_TRACKING is enabled in sdkconfig.
// Should be called at ~1 Hz from SafetyMonitorTask.
void profilerCollectTaskHeap();
#endif

// Profiler endpoints, bound by the seam route table (ADR 0021). All are absent
// (404) when PA_HEAP_PROFILE is not defined, which is why both this block and
// their registration sit behind the same guard.
//
//   GET  /api/profiler               — live heap/HWM/snapshot JSON
void handleProfilerGet(WebRequest& req);

#ifdef CONFIG_HEAP_TRACING
//   POST /api/profiler/trace/start   — start Tier 3 leak trace
//   POST /api/profiler/trace/stop    — stop + dump Tier 3 leak trace to serial
void handleProfilerTraceStartPost(WebRequest& req);
void handleProfilerTraceStopPost(WebRequest& req);
#endif

#endif  // PA_HEAP_PROFILE
