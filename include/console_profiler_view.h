// =============================================================================
// include/console_profiler_view.h
//
// Controller Console rendering of one ProfilerReading (include/api_profiler.h)
// as Console Records - the adapter half of system.api.get-profiler (#224,
// ADR 0034).
//
// The reading is produced only by a PA_HEAP_PROFILE=1 image; this rendering is
// compiled unconditionally. That split is the point: the profiler measurement
// is ESP32-only and cannot run on a host, but everything the operator actually
// reads - which fields appear, what they are called, what the item lines look
// like, and the measurement disclosure below - is ordinary formatting and is
// proven by test/test_native/test_console_profiler_view.
//
// HEADER-ONLY DELIBERATELY, the same constraint include/console_completion.h
// and include/console_direct_action_system.h already document: [env:native]'s
// build_src_filter in platformio.ini is an explicit allowlist of src/*.cpp
// translation units and platformio.ini is fenced, so a new src/console/*.cpp
// could not reach the native test binary. `inline` rather than `static`
// because this header genuinely has two includers in one binary -
// src/console/console_module.cpp and the native test.
//
// Field names are the /api/profiler JSON keys verbatim, per
// docs/console-protocol.md s.3.5, so a serial transcript and the REST response
// name the same measurement the same way.
// =============================================================================
#pragma once

#include <stdio.h>
#include <string.h>

#include "api_profiler.h"   // ProfilerReading, ProfilerRequestTrace, profilerHwmStatus()
#include "console_module.h"  // ConsoleRecordSink, ConsoleStatus/Outcome/Reason
#include "console_record.h"  // consoleQuoteValue()

// The one stable disclosure the ticket's own acceptance criterion asks for:
// profiling output says that serial and log traffic perturb the measurement.
//
// It is a fact about every number in this answer, not a caveat on one of them,
// so it is one field rather than a suffix repeated per value - and it is a
// field rather than prose in the help text because the operator reading a
// transcript later has the transcript, not the help file.
//
// Maker voice (docs/ui-copy-voice.md rule 7 - be honest about model vs reality
// in one clause): the console measuring the heap is itself using it.
#define CONSOLE_PROFILER_MEASUREMENT_NOTE \
    "reading this uses the heap it reports - a busy serial console or a chatty log level moves these numbers"

// Longest rendered item line. The request-trace line is the widest shape
// (PROF_REQUEST_PATH_MAX path plus two 10-digit millisecond values and their
// keys), and 160 is what dome.api.list-sequences already sizes its own
// fixed-field item lines at.
#define CONSOLE_PROFILER_ITEM_MAX 160

// Emit one ProfilerReading as Console Records.
//
// The caller has already emitted `begin`; this emits the scalar fields, then
// the item lines, then `end` - the shape every status executor uses
// (src/console/console_module.cpp).
//
// The request-lifecycle ring is passed as a count plus an indexed reader
// rather than by value: it is 32 entries the Console would otherwise have to
// hold on a 5120-byte task stack, and streaming it one entry at a time is the
// same shape system.status.logs uses for the log ring (#239). Pass a zero
// count and a null reader when there is no ring to render.
//
// Item lines follow the catalog's established item shape - a leading token
// naming what the line is, then `key:value` pairs (colon, so an item never
// looks like a record's own `key=value` pairs). The leading token is the JSON
// array or object the line comes from, so the mapping back to /api/profiler is
// mechanical.
inline void consoleEmitProfilerReading(uint32_t requestId, const ProfilerReading& reading,
                                       size_t traceCount,
                                       bool (*traceAt)(size_t, ProfilerRequestTrace*),
                                       const ConsoleRecordSink* sink) {
    if (sink == nullptr) {
        return;
    }

    char tempBuf[32] = {};

    if (sink->onRecordField) {
        snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)reading.heapFree);
        sink->onRecordField(requestId, "heapFree", tempBuf);
        snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)reading.heapMin);
        sink->onRecordField(requestId, "heapMin", tempBuf);
        snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)reading.heapLargest);
        sink->onRecordField(requestId, "heapLargest", tempBuf);
        // Three decimals, matching the JSON builder's own %.3f, so the two
        // renderings of one reading never disagree in the last digit.
        snprintf(tempBuf, sizeof(tempBuf), "%.3f", (double)reading.fragRatio);
        sink->onRecordField(requestId, "fragRatio", tempBuf);
        snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)reading.allocBlocks);
        sink->onRecordField(requestId, "allocBlocks", tempBuf);
        snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)reading.freeBlocks);
        sink->onRecordField(requestId, "freeBlocks", tempBuf);
        snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)reading.totalBlocks);
        sink->onRecordField(requestId, "totalBlocks", tempBuf);
        snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)reading.failedAllocs);
        sink->onRecordField(requestId, "failedAllocs", tempBuf);
    }

    // The JSON nests these under "lastFail" as size/caps/bt and omits the
    // object entirely when nothing has failed. Console Records are flat, so
    // the parent key prefixes each child key; the same "present only when
    // there is something to report" rule holds, for the same reason - a
    // lastFailSize of 0 would read as a zero-byte allocation failing.
    if (reading.failedAllocs > 0U && sink->onRecordField) {
        snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)reading.lastFailSize);
        sink->onRecordField(requestId, "lastFailSize", tempBuf);
        snprintf(tempBuf, sizeof(tempBuf), "%lu", (unsigned long)reading.lastFailCaps);
        sink->onRecordField(requestId, "lastFailCaps", tempBuf);

        // Backtrace PCs, comma-joined into one whitespace-free value (the
        // same one-token-per-value shape help's `aliases` field uses).
        // Decode against the matching firmware.elf:
        //   xtensa-esp32-elf-addr2line -e .pio/build/<env>/firmware.elf <pc...>
        // Empty on RISC-V, where esp_backtrace_* is not implemented - the
        // field is emitted anyway so "no backtrace" is visible rather than
        // indistinguishable from a dropped field.
        char btBuf[FAILED_ALLOC_BT_MAX * 12] = {};
        size_t used = 0;
        for (uint8_t i = 0; i < reading.lastFailBtDepth && i < FAILED_ALLOC_BT_MAX; ++i) {
            size_t remaining = sizeof(btBuf) - used;
            if (remaining <= 1) break;
            int n = snprintf(btBuf + used, remaining, "%s0x%08x", (used > 0) ? "," : "",
                             (unsigned)reading.lastFailBt[i]);
            if (n < 0) break;
            used += ((size_t)n < remaining) ? (size_t)n : (remaining - 1);
        }
        sink->onRecordField(requestId, "lastFailBt", btBuf);
    }

    // The disclosure sits with the numbers it qualifies, after them, so a
    // reader scrolling a transcript meets the values and the caveat together.
    if (sink->onRecordField) {
        char noteBuf[sizeof(CONSOLE_PROFILER_MEASUREMENT_NOTE) + 8] = {};
        sink->onRecordField(
            requestId, "measurement_note",
            consoleQuoteValue(CONSOLE_PROFILER_MEASUREMENT_NOTE, noteBuf, sizeof(noteBuf)));
    }

    if (sink->onRecordItem) {
        char itemBuf[CONSOLE_PROFILER_ITEM_MAX];

        // taskStacks[]: skip tasks this image is not running, exactly as the
        // JSON does. A missing row means the task does not exist here.
        for (int i = 0; i < PROF_TASK_MAX; ++i) {
            if (!reading.taskStacks[i].found) continue;
            snprintf(itemBuf, sizeof(itemBuf), "taskStack name:%s hwmBytes:%lu status:%s",
                     reading.taskStacks[i].name != nullptr ? reading.taskStacks[i].name : "",
                     (unsigned long)reading.taskStacks[i].hwmBytes,
                     profilerHwmStatus(reading.taskStacks[i].hwmBytes));
            sink->onRecordItem(requestId, itemBuf);
        }

        // current: the open mode window's running low-water mark.
        if (reading.currentWindowOpen) {
            snprintf(itemBuf, sizeof(itemBuf), "currentWindow label:%s heapFree:%lu",
                     reading.currentWindowLabel, (unsigned long)reading.windowMinFree);
            sink->onRecordItem(requestId, itemBuf);
        }

        // snapshots[]: closed mode windows, oldest first (profilerRead()
        // resolves the ring order, so this loop is a plain walk).
        for (uint8_t i = 0; i < reading.windowCount && i < PROF_SNAPSHOT_MAX; ++i) {
            snprintf(itemBuf, sizeof(itemBuf),
                     "window label:%s heapFree:%lu largestBlock:%lu ts:%lu",
                     reading.windows[i].label, (unsigned long)reading.windows[i].heapMinDuring,
                     (unsigned long)reading.windows[i].largestBlockAtClose,
                     (unsigned long)reading.windows[i].windowOpenTs);
            sink->onRecordItem(requestId, itemBuf);
        }

        // requestTrace[]: oldest first, streamed one entry at a time.
        if (traceAt != nullptr) {
            for (size_t i = 0; i < traceCount; ++i) {
                ProfilerRequestTrace entry;
                if (!traceAt(i, &entry)) break;
                snprintf(itemBuf, sizeof(itemBuf),
                         "requestTrace path:%s startMs:%lu handlerDoneMs:%lu", entry.path,
                         (unsigned long)entry.startMs, (unsigned long)entry.handlerDoneMs);
                sink->onRecordItem(requestId, itemBuf);
            }
        }
    }

    if (sink->onRecordEnd) {
        sink->onRecordEnd(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_COMPLETED,
                          CONSOLE_REASON_NONE);
    }
}
