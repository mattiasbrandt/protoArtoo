// =============================================================================
// src/web/api_profiler.cpp
//
// PA_HEAP_PROFILE=1 gated heap profiling backend.
//
// Implements:
//   GET /api/profiler — JSON snapshot of global heap, per-task stack HWM,
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
//     heap_caps_monitor_local_minimum_free_size_start/stop() — scoped low-water marks
//     heap_caps_register_failed_alloc_callback()
//   Tier 2 (CONFIG_HEAP_TASK_TRACKING only):
//     heap_caps_alloc_all_task_stat_arrays()
//     heap_caps_get_all_task_stat()         -> heap_all_tasks_stat_t
//     heap_caps_free_all_task_stat_arrays()
//   Tier 3 (CONFIG_HEAP_TRACING only — escalation path):
//     heap_trace_init_standalone(buf, 200)  — 200 records ~32B each = ~6 KB static
//     heap_trace_start(HEAP_TRACE_LEAKS)    — POST /api/profiler/trace/start
//     heap_trace_stop()                     — POST /api/profiler/trace/stop
//     heap_trace_dump()                     — dumps to serial at stop
//
// SAFETY: No interaction with Core 1 real-time loops.
// All state is written only from SafetyMonitorTask (Core 0).
// portMUX guards protect concurrent reads from web handlers (also Core 0).
// =============================================================================

#if PA_HEAP_PROFILE

#include "api_profiler.h"

#include <Arduino.h>
#include <esp_debug_helpers.h>   // esp_backtrace_get_start/next_frame (failed-alloc backtrace)
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>

#ifdef CONFIG_HEAP_TASK_TRACKING
#include <esp_heap_task_info.h>
#endif

#ifdef CONFIG_HEAP_TRACING
#include <esp_heap_trace.h>
#endif

#include "api_json_response.h"
#include "logging.h"
#include "web_server.h"

static const char* TAG = "Profiler";

// =============================================================================
// Mode-scoped snapshot ring (Tier 1 — heap_caps_monitor_local_minimum_free_size)
//
// Each entry records the low-water mark of a named mode window. Windows are
// opened and closed by profilerModeTransition(). The local monitoring API
// overrides minimum_free_bytes in heap_caps_get_info() for the window duration
// without affecting the global lifetime watermark.
// =============================================================================

#define PROF_SNAPSHOT_MAX 8

struct HeapSnapshot {
    char label[20];
    uint32_t heapMinDuring;     // minimum_free_bytes from heap_caps_get_info() at window close
    uint32_t largestBlockAtClose; // heap_caps_get_largest_free_block() at window close
    uint32_t windowOpenTs;      // millis() when window was opened
};

static HeapSnapshot s_snapshots[PROF_SNAPSHOT_MAX];
static uint8_t s_snapHead = 0;
static uint8_t s_snapCount = 0;
static portMUX_TYPE s_snapMux = portMUX_INITIALIZER_UNLOCKED;

// Active window state — written only by SafetyMonitorTask, read by web handler.
// s_windowMux guards concurrent reads of these three fields.
static portMUX_TYPE s_windowMux = portMUX_INITIALIZER_UNLOCKED;
static bool s_windowOpen = false;
static char s_windowLabel[20] = {};
static uint32_t s_windowOpenTs = 0;

// Push a completed snapshot into the ring (called with window already closed)
static void pushSnapshot(const char* label, uint32_t heapMin, uint32_t largestBlock, uint32_t openTs) {
    HeapSnapshot snap;
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
// Per-task stack HWM (Tier 1 — uxTaskGetStackHighWaterMark)
// =============================================================================

#define PROF_TASK_MAX 9

struct TaskHwmEntry {
    const char* name;
    uint32_t hwmBytes;
    bool found;
};

// Names must match xTaskCreatePinnedToCore() calls in main.cpp.
static const char* const s_taskNames[PROF_TASK_MAX] = {
    "DriveTask", "RCInputTask", "ServoTask", "DomeTask",
    "AudioTask", "AuxLedTask", "DomeLinkTask", "SafetyMonitor", "loopTask"
};

static TaskHwmEntry s_taskHwm[PROF_TASK_MAX];
static portMUX_TYPE s_hwmMux = portMUX_INITIALIZER_UNLOCKED;

// =============================================================================
// Failed-allocation counter (Tier 1 — heap_caps_register_failed_alloc_callback)
// =============================================================================

static uint32_t s_failedAllocCount = 0;

// Last failed allocation, captured ALLOCATION-FREE for /api/profiler to report.
//
// This hook runs IN the context of the failing allocation, on the stack of
// whichever task hit it (IDF heap_caps.c: heap_caps_alloc_failed calls the hook
// inline, then optionally aborts). It MUST NOT allocate: a previous version
// logged via Arduino Print::printf, which mallocs its own buffer — so under heap
// exhaustion that malloc ALSO failed and re-entered this hook, recursing until a
// task stack overflowed (a 64-byte mDNS alloc on the lwIP 'tiT' task crashed it;
// found by decoding the coredump, issue #8). IDF's own abort path
// (fmt_abort_str/hex_to_str) likewise formats with manual hex + memcpy, never
// printf. So: only count, capture raw values + backtrace PCs (esp_backtrace_*
// walks the stack and does not allocate), guard against re-entry, and let the
// /api/profiler handler format them where allocation is safe.
#define PROF_FAIL_BT_MAX 12
static volatile bool     s_inFailedAllocCb = false;
static volatile uint32_t s_lastFailSize    = 0;
static volatile uint32_t s_lastFailCaps    = 0;
static uint32_t          s_lastFailBt[PROF_FAIL_BT_MAX];
static volatile uint8_t  s_lastFailBtDepth = 0;

static void failedAllocCb(size_t requested_size, uint32_t caps, const char* function_name) {
    (void)function_name;
    __atomic_fetch_add(&s_failedAllocCount, 1U, __ATOMIC_RELAXED);

    // Reentrancy guard — belt-and-suspenders now that nothing below allocates.
    if (s_inFailedAllocCb) {
        return;
    }
    s_inFailedAllocCb = true;
    s_lastFailSize = (uint32_t)requested_size;
    s_lastFailCaps = caps;
    esp_backtrace_frame_t frame;
    esp_backtrace_get_start(&frame.pc, &frame.sp, &frame.next_pc);
    uint8_t depth = 0;
    for (; depth < PROF_FAIL_BT_MAX; ++depth) {
        s_lastFailBt[depth] = frame.pc;
        if (!esp_backtrace_get_next_frame(&frame)) {
            ++depth;
            break;
        }
    }
    s_lastFailBtDepth = depth;
    s_inFailedAllocCb = false;
}

// =============================================================================
// Tier 2 — per-task heap allocation stats (CONFIG_HEAP_TASK_TRACKING only)
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
// Tier 3 — heap leak tracing (CONFIG_HEAP_TRACING only — escalation path)
// Initialised at boot; started/stopped via POST /api/profiler/trace/start|stop.
// One-session-only diagnostic — disable after evidence collected.
// =============================================================================

#ifdef CONFIG_HEAP_TRACING
#define PROF_TRACE_RECORDS 200
static heap_trace_record_t s_traceRecords[PROF_TRACE_RECORDS];
static bool s_traceRunning = false;
#endif

// =============================================================================
// Public API
// =============================================================================

void profilerInit() {
    heap_caps_register_failed_alloc_callback(failedAllocCb);
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

#ifdef CONFIG_HEAP_TRACING
    if (heap_trace_init_standalone(s_traceRecords, PROF_TRACE_RECORDS) == ESP_OK) {
        PA_LOG_INFO(TAG, "Tier 3 heap trace buffer ready (%d records)", PROF_TRACE_RECORDS);
    } else {
        PA_LOG_WARN(TAG, "Tier 3 heap trace init failed");
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
    TaskHwmEntry tmp[PROF_TASK_MAX];
    int foundCount = 0;
    for (int i = 0; i < PROF_TASK_MAX; i++) {
        TaskHandle_t h = xTaskGetHandle(s_taskNames[i]);
        tmp[i].name = s_taskNames[i];
        tmp[i].found = (h != nullptr);
        // uxTaskGetStackHighWaterMark returns words; convert to bytes.
        tmp[i].hwmBytes = tmp[i].found
            ? (uint32_t)uxTaskGetStackHighWaterMark(h) * sizeof(StackType_t)
            : 0U;
        if (tmp[i].found) foundCount++;
    }
    if (foundCount == 0) {
        PA_LOG_WARN(TAG, "HWM: all xTaskGetHandle() calls returned NULL - check configUSE_TRACE_FACILITY=1 in sdkconfig");
    }
    taskENTER_CRITICAL(&s_hwmMux);
    for (int i = 0; i < PROF_TASK_MAX; i++) {
        s_taskHwm[i] = tmp[i];
    }
    taskEXIT_CRITICAL(&s_hwmMux);
}

#ifdef CONFIG_HEAP_TASK_TRACKING
void profilerCollectTaskHeap() {
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
}
#endif  // CONFIG_HEAP_TASK_TRACKING

// =============================================================================
// /api/profiler endpoint
// =============================================================================

static const char* hwmStatus(uint32_t hwmBytes) {
    if (hwmBytes > 2048U) return "ok";
    if (hwmBytes > 1024U) return "warn";
    return "crit";
}

static void buildProfilerJson(char* buf, size_t bufSize) {
    // Tier 1 global metrics — direct IDF 5.5 APIs
    uint32_t heapFree = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    uint32_t heapMin = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    uint32_t heapLargest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    float fragRatio = (heapFree > 0U) ? (1.0f - (float)heapLargest / (float)heapFree) : 0.0f;

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);

    uint32_t failedAllocs = __atomic_load_n(&s_failedAllocCount, __ATOMIC_RELAXED);

    // Read current open window state under mutex
    char currentLabel[20] = {};
    bool currentOpen = false;
    taskENTER_CRITICAL(&s_windowMux);
    currentOpen = s_windowOpen;
    if (currentOpen) {
        strncpy(currentLabel, s_windowLabel, sizeof(currentLabel) - 1);
        currentLabel[sizeof(currentLabel) - 1] = '\0';
    }
    taskEXIT_CRITICAL(&s_windowMux);
    // info.minimum_free_bytes already reflects the running local min while window open

    // Copy snapshot ring under mutex
    HeapSnapshot snapCopy[PROF_SNAPSHOT_MAX];
    uint8_t snapHead;
    uint8_t snapCount;
    taskENTER_CRITICAL(&s_snapMux);
    for (int i = 0; i < PROF_SNAPSHOT_MAX; i++) {
        snapCopy[i] = s_snapshots[i];
    }
    snapHead = s_snapHead;
    snapCount = s_snapCount;
    taskEXIT_CRITICAL(&s_snapMux);

    // Copy HWM entries under mutex
    TaskHwmEntry hwmCopy[PROF_TASK_MAX];
    taskENTER_CRITICAL(&s_hwmMux);
    for (int i = 0; i < PROF_TASK_MAX; i++) {
        hwmCopy[i] = s_taskHwm[i];
    }
    taskEXIT_CRITICAL(&s_hwmMux);

    size_t pos = 0;

#define APPEND(...) \
    do { \
        int _n = snprintf(buf + pos, bufSize - pos, __VA_ARGS__); \
        if (_n > 0) pos += (size_t)_n; \
        if (pos >= bufSize) { buf[bufSize - 1] = '\0'; return; } \
    } while (0)

    APPEND("{\"heapFree\":%lu", (unsigned long)heapFree);
    APPEND(",\"heapMin\":%lu", (unsigned long)heapMin);
    APPEND(",\"heapLargest\":%lu", (unsigned long)heapLargest);
    APPEND(",\"fragRatio\":%.3f", (double)fragRatio);
    APPEND(",\"allocBlocks\":%lu", (unsigned long)info.allocated_blocks);
    APPEND(",\"freeBlocks\":%lu", (unsigned long)info.free_blocks);
    APPEND(",\"totalBlocks\":%lu", (unsigned long)info.total_blocks);
    APPEND(",\"failedAllocs\":%lu", (unsigned long)failedAllocs);

    // Last failed allocation (raw values + backtrace PCs captured by the hook).
    // Decode the PCs against the matching firmware.elf:
    //   xtensa-esp32-elf-addr2line -e .pio/build/<env>/firmware.elf <pc...>
    if (failedAllocs > 0U) {
        APPEND(",\"lastFail\":{\"size\":%lu,\"caps\":%lu,\"bt\":[",
               (unsigned long)s_lastFailSize, (unsigned long)s_lastFailCaps);
        uint8_t btDepth = s_lastFailBtDepth;
        for (uint8_t i = 0; i < btDepth; ++i) {
            APPEND("%s\"0x%08x\"", (i == 0) ? "" : ",", (unsigned)s_lastFailBt[i]);
        }
        APPEND("]}");
    }

    // Task stack HWM array
    APPEND(",\"taskStacks\":[");
    bool firstTask = true;
    for (int i = 0; i < PROF_TASK_MAX; i++) {
        if (!hwmCopy[i].found) continue;
        if (!firstTask) APPEND(",");
        firstTask = false;
        APPEND("{\"name\":\"%s\",\"hwmBytes\":%lu,\"status\":\"%s\"}",
               hwmCopy[i].name,
               (unsigned long)hwmCopy[i].hwmBytes,
               hwmStatus(hwmCopy[i].hwmBytes));
    }
    APPEND("]");

#ifdef CONFIG_HEAP_TASK_TRACKING
    // Per-task heap allocation stats (Tier 2)
    TaskHeapEntry heapCopy[PROF_TASK_HEAP_MAX];
    uint8_t heapCount;
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
    if (currentOpen) {
        APPEND(",\"current\":{\"label\":\"%s\",\"heapFree\":%lu}",
               currentLabel, (unsigned long)info.minimum_free_bytes);
    }

    // Snapshot ring — oldest first; each entry = one closed mode window
    APPEND(",\"snapshots\":[");
    if (snapCount > 0) {
        uint8_t oldest = (uint8_t)((snapHead + PROF_SNAPSHOT_MAX - snapCount) % PROF_SNAPSHOT_MAX);
        bool firstSnap = true;
        for (uint8_t i = 0; i < snapCount; i++) {
            uint8_t idx = (uint8_t)((oldest + i) % PROF_SNAPSHOT_MAX);
            if (!firstSnap) APPEND(",");
            firstSnap = false;
            APPEND("{\"label\":\"%s\",\"heapFree\":%lu,\"largestBlock\":%lu,\"ts\":%lu}",
                   snapCopy[idx].label,
                   (unsigned long)snapCopy[idx].heapMinDuring,
                   (unsigned long)snapCopy[idx].largestBlockAtClose,
                   (unsigned long)snapCopy[idx].windowOpenTs);
        }
    }
    APPEND("]");

    // Bounded request-lifecycle trace (issue #54 evidence) -- oldest first.
    // Read here, once, after an experiment; never polled during the
    // workload. See web_request_psychic.cpp for field semantics.
    RequestLifecycleEntry traceCopy[PA_REQUEST_TRACE_MAX];
    size_t traceCount = copyRequestLifecycleTrace(traceCopy, PA_REQUEST_TRACE_MAX);
    APPEND(",\"requestTrace\":[");
    for (size_t i = 0; i < traceCount; i++) {
        if (i > 0) APPEND(",");
        APPEND("{\"path\":\"%s\",\"startMs\":%lu,\"handlerDoneMs\":%lu}",
               traceCopy[i].requestPath, (unsigned long)traceCopy[i].startMs,
               (unsigned long)traceCopy[i].handlerDoneMs);
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

#ifdef CONFIG_HEAP_TRACING
void handleProfilerTraceStartPost(WebRequest& req) {
    if (s_traceRunning) {
        webSendJsonError(req, 409, "trace already running");
        return;
    }
    esp_err_t err = heap_trace_start(HEAP_TRACE_LEAKS);
    if (err == ESP_OK) {
        s_traceRunning = true;
        PA_LOG_INFO(TAG, "Tier 3 heap trace started (LEAKS mode)");
        req.send(200, "application/json", "{\"ok\":true,\"mode\":\"LEAKS\"}");
    } else {
        PA_LOG_WARN(TAG, "Tier 3 heap trace start failed: %d", (int)err);
        webSendJsonError(req, 500, "start failed");
    }
}

void handleProfilerTraceStopPost(WebRequest& req) {
    if (!s_traceRunning) {
        webSendJsonError(req, 409, "trace not running");
        return;
    }
    heap_trace_stop();
    s_traceRunning = false;
    PA_LOG_INFO(TAG, "Tier 3 heap trace stopped - dumping to serial");
    heap_trace_dump();
    req.send(200, "application/json", "{\"ok\":true,\"note\":\"dump written to serial log\"}");
}
#endif

#endif  // PA_HEAP_PROFILE
