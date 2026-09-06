// =============================================================================
// src/web/api_profiler.cpp
//
// PA_HEAP_PROFILE=1 gated heap profiling backend.
//
// Implements:
//   GET /api/profiler - JSON snapshot of global heap, per-task stack HWM,
//                       mode-scoped low-water marks, and (if CONFIG_HEAP_TASK_TRACKING)
//                       per-task heap allocation stats.
//
// IDF 5.5 API usage (pioarduino 55.03.37 = IDF 5.5.2):
//   Tier 1:
//     heap_caps_get_free_size(MALLOC_CAP_8BIT)
//     heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)
//     heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)
//     heap_caps_get_info()    -> multi_heap_info_t
//     uxTaskGetStackHighWaterMark()
//     heap_caps_monitor_local_minimum_free_size_start/stop() - scoped low-water marks
//     heap_caps_register_failed_alloc_callback() - NOT here: the hook and its
//       counter are compiled into every build (src/failed_alloc_tracker.cpp),
//       because /api/status publishes the count on a production image too
//   Tier 2 (CONFIG_HEAP_TASK_TRACKING only):
//     heap_caps_alloc_all_task_stat_arrays()
//     heap_caps_get_all_task_stat()         -> heap_all_tasks_stat_t
//     heap_caps_free_all_task_stat_arrays()
//   Tier 3 (PA_HEAP_TRACING troubleshooting image):
//     heap_trace_init_standalone(buf, 200)  - 200 records ~32B each = ~6 KB static
//     heap_trace_start(HEAP_TRACE_LEAKS)    - POST /api/profiler/trace/start
//     heap_trace_stop()                     - POST /api/profiler/trace/stop
//     heap_trace_dump()                     - dumps to serial at stop
//
// SAFETY: No interaction with Core 1 real-time loops.
// All state is written only from SafetyMonitorTask (Core 0).
// portMUX guards protect concurrent reads from web handlers (also Core 0).
// =============================================================================

#if PA_HEAP_PROFILE

#include "api_profiler.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>

#ifdef CONFIG_HEAP_TASK_TRACKING
#include <esp_heap_task_info.h>
#endif

#if PA_HEAP_TRACING
#include <esp_heap_trace.h>
#endif

#include "api_json_response.h"
#include "failed_alloc_tracker.h"
#include "logging.h"
#include "robot_state.h"
#include "web_server.h"

static const char* TAG = "Profiler";

// =============================================================================
// Mode-scoped snapshot ring (Tier 1 - heap_caps_monitor_local_minimum_free_size)
//
// Each entry records the low-water mark of a named mode window. Windows are
// opened and closed by profilerModeTransition(). The local monitoring API
// overrides minimum_free_bytes in heap_caps_get_info() for the window duration
// without affecting the global lifetime watermark.
// =============================================================================

// PROF_SNAPSHOT_MAX and ProfilerWindowSnapshot are declared in api_profiler.h:
// the ring's shape is part of what an adapter reads, so it lives with the
// reading types rather than being redeclared here (ADR 0036).

static ProfilerWindowSnapshot s_snapshots[PROF_SNAPSHOT_MAX];
static uint8_t s_snapHead = 0;
static uint8_t s_snapCount = 0;
static portMUX_TYPE s_snapMux = portMUX_INITIALIZER_UNLOCKED;

// Active window state - written only by SafetyMonitorTask, read by web handler.
// s_windowMux guards concurrent reads of these three fields.
static portMUX_TYPE s_windowMux = portMUX_INITIALIZER_UNLOCKED;
static bool s_windowOpen = false;
static char s_windowLabel[PROF_LABEL_MAX] = {};
static uint32_t s_windowOpenTs = 0;

// Push a completed snapshot into the ring (called with window already closed)
static void pushSnapshot(const char* label, uint32_t heapMin, uint32_t largestBlock, uint32_t openTs) {
    ProfilerWindowSnapshot snap;
    strncpy(snap.label, label, sizeof(snap.label) - 1);
    snap.label[sizeof(snap.label) - 1] = '\0';
    snap.heapMinDuring = heapMin;
    snap.largestBlockAtClose = largestBlock;
    snap.windowOpenTs = openTs;

    taskENTER_CRITICAL(&s_snapMux);
    s_snapshots[s_snapHead] = snap;
    s_snapHead = (uint8_t)((s_snapHead + 1U) % PROF_SNAPSHOT_MAX);
    if (s_snapCount < PROF_SNAPSHOT_MAX) {
        s_snapCount++;
    }
    taskEXIT_CRITICAL(&s_snapMux);
}

// =============================================================================
// Per-task stack HWM (Tier 1 - uxTaskGetStackHighWaterMark)
// =============================================================================

// PROF_TASK_MAX and ProfilerTaskStack are declared in api_profiler.h, beside
// the rest of the reading's shape.

// Names must match the xTaskCreatePinnedToCore() calls in src/, wherever they
// are -- not only the ones in main.cpp.
//
// Nothing enforced that, and it drifted twice. "SeqDisp" was missing until
// #250, so /api/profiler silently reported nine of the ten tasks in main.cpp.
// The guard written then scanned main.cpp alone, so the three tasks created
// elsewhere stayed invisible to both the endpoint and the guard until #271:
// WebEvents and ArduinoOTA (web_server.cpp) and HostedRecovery
// (web_network_manager_hosted.cpp, ESP32-P4 only -- it reports not-found on
// artoo-esp32, like any task this image does not run).
//
// That is worse than an obviously absent endpoint, because the response looks
// complete -- a task that is never listed reads the same as a task that is
// disabled. If you create a task anywhere in src/, add it here.
// test/test_tools/test_profiler_task_list.py now scans the whole tree.
static const char* const s_taskNames[PROF_TASK_MAX] = {
    "DriveTask", "RCInputTask", "ServoTask", "DomeTask",
    "AudioTask", "AuxLedTask", "DomeLinkTask", "SafetyMonitor", "loopTask",
    "SeqDisp", "Console", "WebEvents", "ArduinoOTA", "HostedRecovery"
};

static ProfilerTaskStack s_taskHwm[PROF_TASK_MAX];
static portMUX_TYPE s_hwmMux = portMUX_INITIALIZER_UNLOCKED;

// =============================================================================
// Tier 2 - per-task heap allocation stats (CONFIG_HEAP_TASK_TRACKING only)
// Uses heap_caps_alloc_all_task_stat_arrays / get_all_task_stat / free.
// =============================================================================

#ifdef CONFIG_HEAP_TASK_TRACKING

#define PROF_TASK_HEAP_MAX 16

struct TaskHeapEntry {
    char name[configMAX_TASK_NAME_LEN + 1];
    uint32_t currentBytes;   // task_stat_t.overall_current_usage
    uint32_t peakBytes;      // task_stat_t.overall_peak_usage
    uint32_t heapCount;      // task_stat_t.heap_count
};

static TaskHeapEntry s_taskHeap[PROF_TASK_HEAP_MAX];
static uint8_t s_taskHeapCount = 0;
static portMUX_TYPE s_taskHeapMux = portMUX_INITIALIZER_UNLOCKED;

#endif  // CONFIG_HEAP_TASK_TRACKING

// =============================================================================
// Tier 3 - heap leak tracing (PA_HEAP_TRACING troubleshooting image)
// Initialised at boot; started/stopped via POST /api/profiler/trace/start|stop.
// One-session-only diagnostic - disable after evidence collected.
// =============================================================================

#if PA_HEAP_TRACING
#define PROF_TRACE_RECORDS 200
static heap_trace_record_t s_traceRecords[PROF_TRACE_RECORDS];
static bool s_traceRunning = false;
#endif

// Bounded request-lifecycle trace for profiler evidence. The profiler module
// owns its storage and exposes only an opaque start/finish interface, so the
// admission path has no Build Feature Flag conditionals or profiler structs.
// It covers only admitted requests that count against the inflight cap. A
// long-lived stream is already represented by the SSE client counters and is
// not the per-request event this ring describes.
//
// handlerDoneMs is captured when the matched handler's call into send() has
// returned through the middleware. esp_http_server writes synchronously from
// that call, so this means "response written", not merely "response ready".
// There is no disconnect timestamp because keep-alive sockets outlive their
// individual requests; socket lifetime belongs to the HTTP socket counters.
//
// The server task is the single writer and also serves /api/profiler. A slot
// may be overwritten before a very long request finishes; that is acceptable
// for a bounded diagnostic trace, not a correctness-bearing structure. The
// Console reads it from a different task, which is one more reader of the same
// deliberately-imprecise ring, not a new hazard class.
//
// PROF_REQUEST_TRACE_MAX and ProfilerRequestTrace are declared in
// api_profiler.h with the rest of the reading's shape.
static ProfilerRequestTrace s_requestTrace[PROF_REQUEST_TRACE_MAX];
static uint8_t s_requestTraceHead = 0;
static uint8_t s_requestTraceCount = 0;

static bool s_lastAudioActive = false;
static bool s_lastSseConnected = false;
static int s_profilerHwmTick = 0;

// =============================================================================
// Public API
// =============================================================================

void profilerInit() {
    // The failed-alloc hook is NOT registered here. It counts on every build,
    // not only a profiler one (/api/status publishes "failedAllocs"), and IDF
    // keeps a single hook slot - so src/failed_alloc_tracker.cpp owns it and
    // safetyMonitorTask registers it unconditionally, immediately before this
    // call.
    for (int i = 0; i < PROF_TASK_MAX; i++) {
        s_taskHwm[i].name = s_taskNames[i];
        s_taskHwm[i].hwmBytes = 0;
        s_taskHwm[i].found = false;
    }
    // Open the initial "boot" window to start capturing the startup low-water mark
    if (heap_caps_monitor_local_minimum_free_size_start() == ESP_OK) {
        taskENTER_CRITICAL(&s_windowMux);
        strncpy(s_windowLabel, "boot", sizeof(s_windowLabel) - 1);
        s_windowLabel[sizeof(s_windowLabel) - 1] = '\0';
        s_windowOpenTs = (uint32_t)millis();
        s_windowOpen = true;
        taskEXIT_CRITICAL(&s_windowMux);
    }
    PA_LOG_INFO(TAG, "PA_HEAP_PROFILE=1 active; boot window opened");

#if PA_HEAP_TRACING
    if (heap_trace_init_standalone(s_traceRecords, PROF_TRACE_RECORDS) == ESP_OK) {
        PA_LOG_INFO(TAG, "Tier 3 heap trace buffer ready (%d records)", PROF_TRACE_RECORDS);
    } else {
        PA_LOG_ERROR(TAG, "Tier 3 heap trace init failed");
    }
#endif
}

void profilerModeTransition(const char* newLabel) {
    // Close the current window: read local min, then stop monitoring.
    // Only SafetyMonitorTask calls this, so s_windowOpen/Label/Ts are safe to
    // read without the mutex here; we take it only to mark the window closed/open
    // so buildProfilerJson (web handler) sees a consistent state.
    if (s_windowOpen) {
        multi_heap_info_t info;
        heap_caps_get_info(&info, MALLOC_CAP_8BIT);
        uint32_t localMin = (uint32_t)info.minimum_free_bytes;
        uint32_t largestBlock = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        heap_caps_monitor_local_minimum_free_size_stop();
        pushSnapshot(s_windowLabel, localMin, largestBlock, s_windowOpenTs);
        taskENTER_CRITICAL(&s_windowMux);
        s_windowOpen = false;
        taskEXIT_CRITICAL(&s_windowMux);
    }
    // Open the new window
    if (heap_caps_monitor_local_minimum_free_size_start() == ESP_OK) {
        taskENTER_CRITICAL(&s_windowMux);
        strncpy(s_windowLabel, newLabel, sizeof(s_windowLabel) - 1);
        s_windowLabel[sizeof(s_windowLabel) - 1] = '\0';
        s_windowOpenTs = (uint32_t)millis();
        s_windowOpen = true;
        taskEXIT_CRITICAL(&s_windowMux);
    }
}

void profilerCollectHwm() {
    ProfilerTaskStack tmp[PROF_TASK_MAX];
    int foundCount = 0;
    for (int i = 0; i < PROF_TASK_MAX; i++) {
        TaskHandle_t h = xTaskGetHandle(s_taskNames[i]);
        tmp[i].name = s_taskNames[i];
        tmp[i].found = (h != nullptr);
        // uxTaskGetStackHighWaterMark returns WORDS; convert to bytes.
        //
        // Verified against the implementation rather than the header (#270):
        // the vendored task.h doc comment claims "in bytes (as opposed to
        // words in the standard FreeRTOS documentation)", but
        // prvTaskCheckFreeStackSpace() - the function both
        // uxTaskGetStackHighWaterMark() and its ...2() sibling return through
        // - divides the counted fill bytes by sizeof(StackType_t) before
        // returning (components/freertos/FreeRTOS-Kernel/tasks.c and the SMP
        // kernel's copy, ESP-IDF 5.5.5 in both toolchains). The doc comment
        // is a leftover from the older ESP-IDF FreeRTOS fork that did not
        // divide. Do not "correct" this multiply away on the strength of the
        // header: it would divide every reported HWM by 4.
        tmp[i].hwmBytes = tmp[i].found
            ? (uint32_t)uxTaskGetStackHighWaterMark(h) * sizeof(StackType_t)
            : 0U;
        if (tmp[i].found) foundCount++;
    }
    if (foundCount == 0) {
        PA_LOG_ERROR(TAG, "HWM: all xTaskGetHandle() calls returned NULL - check configUSE_TRACE_FACILITY=1 in sdkconfig");
    }
    taskENTER_CRITICAL(&s_hwmMux);
    for (int i = 0; i < PROF_TASK_MAX; i++) {
        s_taskHwm[i] = tmp[i];
    }
    taskEXIT_CRITICAL(&s_hwmMux);
}

void profilerCollectTaskHeap() {
#ifdef CONFIG_HEAP_TASK_TRACKING
    heap_all_tasks_stat_t tasks_stat = {};
    if (heap_caps_alloc_all_task_stat_arrays(&tasks_stat) != ESP_OK) {
        return;
    }
    if (heap_caps_get_all_task_stat(&tasks_stat) != ESP_OK) {
        heap_caps_free_all_task_stat_arrays(&tasks_stat);
        return;
    }

    size_t count = tasks_stat.task_count;
    if (count > PROF_TASK_HEAP_MAX) {
        count = PROF_TASK_HEAP_MAX;
    }

    TaskHeapEntry tmp[PROF_TASK_HEAP_MAX];
    for (size_t i = 0; i < count; i++) {
        const task_stat_t* t = &tasks_stat.stat_arr[i];
        strncpy(tmp[i].name, t->name, configMAX_TASK_NAME_LEN);
        tmp[i].name[configMAX_TASK_NAME_LEN] = '\0';
        tmp[i].currentBytes = (uint32_t)t->overall_current_usage;
        tmp[i].peakBytes = (uint32_t)t->overall_peak_usage;
        tmp[i].heapCount = (uint32_t)t->heap_count;
    }

    heap_caps_free_all_task_stat_arrays(&tasks_stat);

    taskENTER_CRITICAL(&s_taskHeapMux);
    s_taskHeapCount = (uint8_t)count;
    for (size_t i = 0; i < count; i++) {
        s_taskHeap[i] = tmp[i];
    }
    taskEXIT_CRITICAL(&s_taskHeapMux);
#endif  // CONFIG_HEAP_TASK_TRACKING
}

void profilerObserveOptionalSubsystems() {
    bool audioActive = false;
    taskENTER_CRITICAL(&robotStateMux);
    audioActive = robotState.audioActive;
    taskEXIT_CRITICAL(&robotStateMux);
    if (audioActive != s_lastAudioActive) {
        profilerModeTransition(audioActive ? "audio_play" : "audio_stop");
        s_lastAudioActive = audioActive;
    }

    const bool sseConnected = webServerHasSSEClients();
    if (sseConnected != s_lastSseConnected) {
        profilerModeTransition(sseConnected ? "sse_connect" : "sse_disconnect");
        s_lastSseConnected = sseConnected;
    }
}

void profilerPeriodicCollect() {
    if (++s_profilerHwmTick < 10) {
        return;
    }
    s_profilerHwmTick = 0;
    profilerCollectHwm();
    profilerCollectTaskHeap();
}

uint8_t profilerRequestStarted(const char* path) {
    const uint8_t idx = s_requestTraceHead;
    ProfilerRequestTrace& entry = s_requestTrace[idx];
    strncpy(entry.path, path, sizeof(entry.path) - 1);
    entry.path[sizeof(entry.path) - 1] = '\0';
    entry.startMs = millis();
    entry.handlerDoneMs = 0;
    s_requestTraceHead = (uint8_t)((s_requestTraceHead + 1U) % PROF_REQUEST_TRACE_MAX);
    if (s_requestTraceCount < PROF_REQUEST_TRACE_MAX) {
        s_requestTraceCount++;
    }
    return idx;
}

void profilerRequestFinished(uint8_t token) {
    if (token < PROF_REQUEST_TRACE_MAX) {
        s_requestTrace[token].handlerDoneMs = millis();
    }
}

size_t profilerRequestTraceCount(void) {
    return s_requestTraceCount;
}

// Oldest-first indexing over the ring, one entry per call. Both adapters read
// through this, so the head/count arithmetic exists once - it used to be a
// copy-the-whole-ring helper, which the Console cannot afford on its stack.
bool profilerRequestTraceAt(size_t index, ProfilerRequestTrace* out) {
    if (out == nullptr || index >= s_requestTraceCount) {
        return false;
    }
    const uint8_t oldest =
        (uint8_t)((s_requestTraceHead + PROF_REQUEST_TRACE_MAX - s_requestTraceCount) %
                  PROF_REQUEST_TRACE_MAX);
    *out = s_requestTrace[(uint8_t)((oldest + index) % PROF_REQUEST_TRACE_MAX)];
    return true;
}

// =============================================================================
// /api/profiler endpoint
// =============================================================================

// The shared read (api_profiler.h). Acquires the muxes in fixed order and
// returns one ProfilerReading.
// Lock order: s_windowMux -> s_snapMux -> s_hwmMux.
// Reason: one place owns the topology; consistent order keeps it that way if ever held together.
// Guarantee: each block is internally consistent; blocks are captured in fixed order but are
// NOT atomic with respect to each other (a writer can modify state between acquisitions).
// Non-atomicity is accepted: holding all of them across the copy would mean ~1KB of copying with
// interrupts disabled on a real-time core - unacceptably long critical section for a diagnostic.
//
// The Tier 1 globals are read here too, not by each adapter: fragRatio is a
// derived number, and two adapters deriving it from two separate reads of
// heap_caps_get_free_size() could print two different ratios for one snapshot.
//
// Tier 2 (CONFIG_HEAP_TASK_TRACKING per-task heap attribution) is deliberately
// NOT part of this reading and stays inside the JSON adapter below. No
// environment in platformio.ini sets that sdkconfig option - it is documented
// there (the artoo_esp32_profiler section) as a manual escalation that rebuilds
// the core libs - so it is absent from every image this repo builds, and
// carrying it across the seam would mean spelling configMAX_TASK_NAME_LEN into
// a build-independent header to size a name buffer no build fills.
void profilerRead(ProfilerReading* out) {
    if (out == nullptr) {
        return;
    }

    // Tier 1 global metrics - direct IDF 5.5 APIs (no mux needed)
    out->heapFree = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    out->heapMin = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    out->heapLargest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    out->fragRatio =
        (out->heapFree > 0U) ? (1.0f - (float)out->heapLargest / (float)out->heapFree) : 0.0f;

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    out->allocBlocks = (uint32_t)info.allocated_blocks;
    out->freeBlocks = (uint32_t)info.free_blocks;
    out->totalBlocks = (uint32_t)info.total_blocks;
    out->windowMinFree = (uint32_t)info.minimum_free_bytes;

    // Failed allocations come from the always-compiled tracker
    // (include/failed_alloc_tracker.h), which owns the IDF hook. The profiler
    // renders that one counter; it does not keep a second.
    FailedAllocReading failedAlloc = {};
    failedAllocTrackerRead(&failedAlloc);
    out->failedAllocs = failedAlloc.count;
    out->lastFailSize = failedAlloc.lastSize;
    out->lastFailCaps = failedAlloc.lastCaps;
    out->lastFailBtDepth = failedAlloc.btDepth;
    for (uint8_t i = 0; i < FAILED_ALLOC_BT_MAX; ++i) {
        out->lastFailBt[i] = failedAlloc.bt[i];
    }

    // Acquire s_windowMux first
    taskENTER_CRITICAL(&s_windowMux);
    out->currentWindowOpen = s_windowOpen;
    if (out->currentWindowOpen) {
        strncpy(out->currentWindowLabel, s_windowLabel, sizeof(out->currentWindowLabel) - 1);
        out->currentWindowLabel[sizeof(out->currentWindowLabel) - 1] = '\0';
    } else {
        out->currentWindowLabel[0] = '\0';
    }
    taskEXIT_CRITICAL(&s_windowMux);

    // Acquire s_snapMux. Resolved to oldest-first inside the critical section
    // so no adapter repeats the ring arithmetic and no caller pays 256 B of
    // stack for a temporary copy to reorder afterwards - the Console task has
    // 5120 B total. The critical section is no longer than the copy it
    // replaced: at most PROF_SNAPSHOT_MAX entries either way, and only
    // s_snapCount of them here.
    taskENTER_CRITICAL(&s_snapMux);
    const uint8_t count = s_snapCount;
    const uint8_t oldest = (uint8_t)((s_snapHead + PROF_SNAPSHOT_MAX - count) % PROF_SNAPSHOT_MAX);
    for (uint8_t i = 0; i < count; i++) {
        out->windows[i] = s_snapshots[(uint8_t)((oldest + i) % PROF_SNAPSHOT_MAX)];
    }
    taskEXIT_CRITICAL(&s_snapMux);
    out->windowCount = count;

    // Acquire s_hwmMux
    taskENTER_CRITICAL(&s_hwmMux);
    for (int i = 0; i < PROF_TASK_MAX; i++) {
        out->taskStacks[i] = s_taskHwm[i];
    }
    taskEXIT_CRITICAL(&s_hwmMux);
}

static void buildProfilerJson(char* buf, size_t bufSize) {
    // One shared read of every counter (api_profiler.h) - this handler is an
    // adapter over it, not a second reader of the muxed state. Static, not a
    // local: the reading is ~450 B and this runs on the web server task.
    static ProfilerReading reading = {};
    profilerRead(&reading);

    // Now use only the captured data; no direct mux acquisitions in this handler

    size_t pos = 0;

#define APPEND(...) \
    do { \
        int _n = snprintf(buf + pos, bufSize - pos, __VA_ARGS__); \
        if (_n > 0) pos += (size_t)_n; \
        if (pos >= bufSize) { buf[bufSize - 1] = '\0'; return; } \
    } while (0)

    APPEND("{\"heapFree\":%lu", (unsigned long)reading.heapFree);
    APPEND(",\"heapMin\":%lu", (unsigned long)reading.heapMin);
    APPEND(",\"heapLargest\":%lu", (unsigned long)reading.heapLargest);
    APPEND(",\"fragRatio\":%.3f", (double)reading.fragRatio);
    APPEND(",\"allocBlocks\":%lu", (unsigned long)reading.allocBlocks);
    APPEND(",\"freeBlocks\":%lu", (unsigned long)reading.freeBlocks);
    APPEND(",\"totalBlocks\":%lu", (unsigned long)reading.totalBlocks);
    APPEND(",\"failedAllocs\":%lu", (unsigned long)reading.failedAllocs);

    // Last failed allocation (raw values + backtrace PCs captured by the hook).
    // Decode the PCs against the matching firmware.elf:
    //   xtensa-esp32-elf-addr2line -e .pio/build/<env>/firmware.elf <pc...>
    if (reading.failedAllocs > 0U) {
        APPEND(",\"lastFail\":{\"size\":%lu,\"caps\":%lu,\"bt\":[",
               (unsigned long)reading.lastFailSize, (unsigned long)reading.lastFailCaps);
        for (uint8_t i = 0; i < reading.lastFailBtDepth; ++i) {
            APPEND("%s\"0x%08x\"", (i == 0) ? "" : ",", (unsigned)reading.lastFailBt[i]);
        }
        APPEND("]}");
    }

    // Task stack HWM array
    APPEND(",\"taskStacks\":[");
    bool firstTask = true;
    for (int i = 0; i < PROF_TASK_MAX; i++) {
        if (!reading.taskStacks[i].found) continue;
        if (!firstTask) APPEND(",");
        firstTask = false;
        APPEND("{\"name\":\"%s\",\"hwmBytes\":%lu,\"status\":\"%s\"}",
               reading.taskStacks[i].name,
               (unsigned long)reading.taskStacks[i].hwmBytes,
               profilerHwmStatus(reading.taskStacks[i].hwmBytes));
    }
    APPEND("]");

#ifdef CONFIG_HEAP_TASK_TRACKING
    // Per-task heap allocation stats (Tier 2). Captured here rather than in
    // profilerRead(): no environment enables CONFIG_HEAP_TASK_TRACKING, so
    // this tier exists in no image the repo builds and stays with its one
    // adapter - see profilerRead()'s own comment.
    static TaskHeapEntry heapCopy[PROF_TASK_HEAP_MAX];
    uint8_t heapCount = 0;
    taskENTER_CRITICAL(&s_taskHeapMux);
    heapCount = s_taskHeapCount;
    for (uint8_t i = 0; i < heapCount; i++) {
        heapCopy[i] = s_taskHeap[i];
    }
    taskEXIT_CRITICAL(&s_taskHeapMux);

    APPEND(",\"taskHeap\":[");
    for (uint8_t i = 0; i < heapCount; i++) {
        if (i > 0) APPEND(",");
        APPEND("{\"name\":\"%s\",\"current\":%lu,\"peak\":%lu,\"heapCount\":%lu}",
               heapCopy[i].name,
               (unsigned long)heapCopy[i].currentBytes,
               (unsigned long)heapCopy[i].peakBytes,
               (unsigned long)heapCopy[i].heapCount);
    }
    APPEND("]");
#endif

    // Active window: running local minimum of the currently open mode window
    if (reading.currentWindowOpen) {
        APPEND(",\"current\":{\"label\":\"%s\",\"heapFree\":%lu}",
               reading.currentWindowLabel, (unsigned long)reading.windowMinFree);
    }

    // Snapshot ring - oldest first; each entry = one closed mode window
    APPEND(",\"snapshots\":[");
    for (uint8_t i = 0; i < reading.windowCount; i++) {
        if (i > 0) APPEND(",");
        APPEND("{\"label\":\"%s\",\"heapFree\":%lu,\"largestBlock\":%lu,\"ts\":%lu}",
               reading.windows[i].label,
               (unsigned long)reading.windows[i].heapMinDuring,
               (unsigned long)reading.windows[i].largestBlockAtClose,
               (unsigned long)reading.windows[i].windowOpenTs);
    }
    APPEND("]");

    // Bounded request-lifecycle trace for profiler evidence -- oldest first.
    // Read here, once, after an experiment; never polled during the
    // workload. Field and lifetime semantics are documented with the ring.
    const size_t traceCount = profilerRequestTraceCount();
    APPEND(",\"requestTrace\":[");
    for (size_t i = 0; i < traceCount; i++) {
        ProfilerRequestTrace entry;
        if (!profilerRequestTraceAt(i, &entry)) break;
        if (i > 0) APPEND(",");
        APPEND("{\"path\":\"%s\",\"startMs\":%lu,\"handlerDoneMs\":%lu}",
               entry.path, (unsigned long)entry.startMs,
               (unsigned long)entry.handlerDoneMs);
    }
    APPEND("]}");

#undef APPEND
}

// GET /api/profiler
//
// The body buffer is allocated once, on the first request, and reused for
// every request after it -- so the per-request allocation cost is zero, which
// is what a diagnostic endpoint polled during a heap investigation has to be:
// a profiler that allocates per request measures its own footprint. It is not
// a BSS array either, because permanent DRAM is the scarcest budget on this
// target (api_json_response.h) and this route only exists on PA_HEAP_PROFILE
// builds.
void handleProfilerGet(WebRequest& req) {
    static constexpr size_t kProfilerBodySize = 4096;
    static char* body = nullptr;
    if (body == nullptr) {
        body = new (std::nothrow) char[kProfilerBodySize];
    }
    if (body == nullptr) {
        // The async route aborted the connection here. The seam has no abort,
        // and a 500 is the better answer regardless: setup.js reads the status
        // code to decide whether the profiler UI exists at all, and a dropped
        // connection is indistinguishable from the endpoint being absent.
        webSendJsonError(req, 500, "profiler buffer alloc failed");
        return;
    }
    buildProfilerJson(body, kProfilerBodySize);
    req.send(200, "application/json", body);
}

#if PA_HEAP_TRACING
// The Tier 3 trace cores, shared by POST /api/profiler/trace/start|stop and by
// the Console's system.action.profiler-trace-start|stop (#224). s_traceRunning
// is the whole reason these are functions rather than two lines in each
// adapter: it is the state that decides between "started" and "already
// running", and two adapters keeping their own opinion of it is how a trace
// gets started twice.
ProfilerTraceOutcome profilerTraceStart(void) {
    if (s_traceRunning) {
        return PROFILER_TRACE_ALREADY_RUNNING;
    }
    esp_err_t err = heap_trace_start(HEAP_TRACE_LEAKS);
    if (err != ESP_OK) {
        PA_LOG_WARN(TAG, "Tier 3 heap trace start failed: %d", (int)err);
        return PROFILER_TRACE_FAILED;
    }
    s_traceRunning = true;
    PA_LOG_INFO(TAG, "Tier 3 heap trace started (LEAKS mode)");
    return PROFILER_TRACE_STARTED;
}

ProfilerTraceOutcome profilerTraceStop(void) {
    if (!s_traceRunning) {
        return PROFILER_TRACE_NOT_RUNNING;
    }
    heap_trace_stop();
    s_traceRunning = false;
    PA_LOG_INFO(TAG, "Tier 3 heap trace stopped - dumping to serial");
    heap_trace_dump();
    return PROFILER_TRACE_STOPPED;
}

void handleProfilerTraceStartPost(WebRequest& req) {
    switch (profilerTraceStart()) {
        case PROFILER_TRACE_STARTED:
            req.send(200, "application/json", "{\"ok\":true,\"mode\":\"LEAKS\"}");
            return;
        case PROFILER_TRACE_ALREADY_RUNNING:
            webSendJsonError(req, 409, "trace already running");
            return;
        default:
            webSendJsonError(req, 500, "start failed");
            return;
    }
}

void handleProfilerTraceStopPost(WebRequest& req) {
    switch (profilerTraceStop()) {
        case PROFILER_TRACE_STOPPED:
            req.send(200, "application/json", "{\"ok\":true,\"note\":\"dump written to serial log\"}");
            return;
        case PROFILER_TRACE_NOT_RUNNING:
            webSendJsonError(req, 409, "trace not running");
            return;
        default:
            webSendJsonError(req, 500, "stop failed");
            return;
    }
}
#endif

#endif  // PA_HEAP_PROFILE
