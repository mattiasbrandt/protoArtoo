// =============================================================================
// src/web/web_admission_event_ring.cpp
//
// The admission event ring (ring buffer of decision events) and its JSON form.
// Pure: every input arrives as a parameter, so this compiles and is exercised
// on the host exactly like the decision core it observes.
//
// Rows are packed to eight bytes because the ring competes with the heap it
// measures; see include/web_admission_event_ring.h. The packing lives entirely
// between webAdmissionTraceRecord() and the accessors below -- callers give
// bytes and milliseconds and read bytes and milliseconds back.
// =============================================================================

#include "../../include/web_admission_event_ring.h"

#if PA_ADMISSION_TRACE

#include <string.h>

namespace {

// Field positions within WebAdmissionTraceEntry::flags.
constexpr uint8_t kLayerShift = 7;
constexpr uint8_t kOutcomeShift = 5;
constexpr uint8_t kNavigationShift = 3;
constexpr uint8_t kInflightShift = 0;

constexpr uint8_t kOutcomeMask = 0x03;
constexpr uint8_t kNavigationMask = 0x03;
constexpr uint8_t kInflightMask = 0x07;

uint16_t saturateU16(uint32_t value) {
    return value > UINT16_MAX ? (uint16_t)UINT16_MAX : (uint16_t)value;
}

// Bytes to the stored 4-byte units, saturating at the top of the field rather
// than wrapping to a small value that would read as a heap emergency.
uint16_t packBlock(uint32_t bytes) {
    return saturateU16(bytes >> kWebAdmissionTraceBlockShift);
}

}  // namespace

void webAdmissionTraceInit(WebAdmissionTrace* trace) {
    // Rows only. The calibration survives, so a ring armed between runs still
    // labels the run that follows.
    memset(trace->entries, 0, sizeof(trace->entries));
    trace->total = 0;
    trace->next = 0;
    trace->baseMs = 0;
}

void webAdmissionTraceConfigure(WebAdmissionTrace* trace,
                                const WebAdmissionTraceConfig& config) {
    trace->config = config;
}

void webAdmissionTraceRecord(WebAdmissionTrace* trace, uint32_t ms,
                             WebAdmissionTraceLayer layer, WebAdmissionTraceOutcome outcome,
                             uint32_t block, uint32_t fresh, uint32_t sampleAgeMs,
                             int inflight, WebAdmissionTraceNavigation navigation) {
    // The first row after a clear sets the origin every other offset is
    // measured from.
    if (trace->total == 0) {
        trace->baseMs = ms;
    }

    WebAdmissionTraceEntry& entry = trace->entries[trace->next];

    // Unsigned subtraction, so a capture that spans the millisecond counter's
    // rollover still produces the right interval rather than 49 days of it.
    const uint32_t offsetMs = ms - trace->baseMs;
    entry.msOffset = saturateU16(offsetMs);
    entry.blockQuads = packBlock(block);
    entry.freshQuads = packBlock(fresh);
    entry.sampleAgeMs = sampleAgeMs > kWebAdmissionTraceAgeSaturatedRaw
                            ? (uint8_t)kWebAdmissionTraceAgeSaturatedRaw
                            : (uint8_t)sampleAgeMs;

    // Clamped for width, not for meaning: the in-flight cap is single digits,
    // so a depth arriving above 7 is a bug in the accounting rather than a real
    // one, and it should read as an obviously pinned number.
    const unsigned depth = inflight < 0 ? 0u : (unsigned)inflight;
    entry.flags = (uint8_t)(((uint8_t)layer << kLayerShift) |
                            (((uint8_t)outcome & kOutcomeMask) << kOutcomeShift) |
                            (((uint8_t)navigation & kNavigationMask) << kNavigationShift) |
                            ((depth > kInflightMask ? kInflightMask : (uint8_t)depth)
                             << kInflightShift));

    trace->total = trace->total + 1u;
    trace->next = (trace->next + 1u) % WEB_ADMISSION_TRACE_CAPACITY;
}

WebAdmissionTraceLayer webAdmissionTraceEntryLayer(const WebAdmissionTraceEntry& entry) {
    return (WebAdmissionTraceLayer)((entry.flags >> kLayerShift) & 0x01);
}

WebAdmissionTraceOutcome webAdmissionTraceEntryOutcome(const WebAdmissionTraceEntry& entry) {
    return (WebAdmissionTraceOutcome)((entry.flags >> kOutcomeShift) & kOutcomeMask);
}

WebAdmissionTraceNavigation webAdmissionTraceEntryNavigation(
    const WebAdmissionTraceEntry& entry) {
    return (WebAdmissionTraceNavigation)((entry.flags >> kNavigationShift) & kNavigationMask);
}

unsigned webAdmissionTraceEntryInflight(const WebAdmissionTraceEntry& entry) {
    return (unsigned)((entry.flags >> kInflightShift) & kInflightMask);
}

uint32_t webAdmissionTraceEntryBlock(const WebAdmissionTraceEntry& entry) {
    return (uint32_t)entry.blockQuads << kWebAdmissionTraceBlockShift;
}

uint32_t webAdmissionTraceEntryFresh(const WebAdmissionTraceEntry& entry) {
    return (uint32_t)entry.freshQuads << kWebAdmissionTraceBlockShift;
}

void webAdmissionTraceWrite(const WebAdmissionTrace* trace, JsonSliceWriter& writer) {
    const uint32_t held = trace->total < WEB_ADMISSION_TRACE_CAPACITY
                              ? trace->total
                              : (uint32_t)WEB_ADMISSION_TRACE_CAPACITY;
    // Oldest first, so the document reads as a timeline. Once the ring has
    // wrapped the write cursor is the oldest row; before that it is row 0.
    const uint32_t first = trace->total < WEB_ADMISSION_TRACE_CAPACITY ? 0u : trace->next;

    // The legend travels with the rows. Rows are arrays rather than objects
    // because naming the columns once costs eighty bytes against roughly twenty
    // per row repeated, and a whole page load is meant to be read in one
    // response. Readings are expanded back to bytes here, so nothing outside
    // this file has to know a row is packed.
    writer.append(
        "{\"fields\":[\"msOffset\",\"layer\",\"block\",\"fresh\",\"ageMs\",\"outcome\","
        "\"inflight\",\"nav\"],"
        "\"layers\":[\"conn\",\"req\"],"
        "\"outcomes\":[\"admit\",\"rate\",\"heap\",\"inflight\"],"
        "\"nav\":[\"asset\",\"navigation\",\"unknown\"],"
        "\"units\":{\"block\":\"bytes\",\"msOffset\":\"ms since baseMs\"},"
        "\"ageSaturated\":");
    writer.appendUint(kWebAdmissionTraceAgeSaturatedRaw);
    // Rows carry no path: what each was serving is recovered by position from
    // the capturing harness's own request log, which already has it.
    writer.append(",\"pathsInRows\":false");
    writer.append(",\"baseMs\":");
    writer.appendUint(trace->baseMs);
    // The calibration these rows were taken under. Every reading below is only
    // meaningful against these numbers, so they travel with it.
    writer.append(",\"floors\":{\"connection\":");
    writer.appendUint(trace->config.connectionFloor);
    writer.append(",\"request\":");
    writer.appendUint(trace->config.requestFloor);
    writer.append(",\"requestDiagnostic\":");
    writer.appendUint(trace->config.requestFloorDiagnostic);
    writer.append(",\"sampleIntervalMs\":");
    writer.appendUint(trace->config.sampleIntervalMs);
    writer.append(",\"maxInflightRequests\":");
    writer.appendUint(trace->config.maxInflightRequests);
    writer.append("}");
    writer.append(",\"capacity\":");
    writer.appendUint(WEB_ADMISSION_TRACE_CAPACITY);
    writer.append(",\"total\":");
    writer.appendUint(trace->total);
    writer.append(",\"held\":");
    writer.appendUint(held);
    // What the ring dropped. Published rather than hidden: a profile read after
    // too long is then visibly partial instead of quietly so.
    writer.append(",\"overwritten\":");
    writer.appendUint(trace->total - held);
    writer.append(",\"rows\":[");

    for (uint32_t i = 0; i < held; ++i) {
        const WebAdmissionTraceEntry& e =
            trace->entries[(first + i) % WEB_ADMISSION_TRACE_CAPACITY];

        if (i != 0) {
            writer.append(',');
        }
        writer.append('[');
        writer.appendUint(e.msOffset);
        writer.append(',');
        writer.appendUint((unsigned long)webAdmissionTraceEntryLayer(e));
        writer.append(',');
        writer.appendUint(webAdmissionTraceEntryBlock(e));
        writer.append(',');
        writer.appendUint(webAdmissionTraceEntryFresh(e));
        writer.append(',');
        writer.appendUint(e.sampleAgeMs);
        writer.append(',');
        writer.appendUint((unsigned long)webAdmissionTraceEntryOutcome(e));
        writer.append(',');
        writer.appendUint(webAdmissionTraceEntryInflight(e));
        writer.append(',');
        writer.appendUint((unsigned long)webAdmissionTraceEntryNavigation(e));
        writer.append(']');
    }

    writer.append("]}");
}

#endif  // PA_ADMISSION_TRACE
