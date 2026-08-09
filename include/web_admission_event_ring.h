// =============================================================================
// include/web_admission_event_ring.h
//
// EVENT RING: A ring buffer (circular FIFO) of admission decision events,
// independent of the admission policy (include/web_admission.h). This module
// stores evidence about what the admission layers decided and why; it does not
// participate in those decisions.
//
// The ring exists so the largest-free-block reading through a full page load
// can be read back as a curve rather than argued about from a spot value. Both
// admission layers (include/web_admission.h) refuse on a number. The counters
// they publish say how often each floor was crossed, but not what the reading
// was doing between crossings, and not how old the reading was when it was used
// -- both layers share one cached sample, so a decision may be taken on a value
// measured up to an interval earlier. A refusal count cannot distinguish "the
// heap really was that low" from "the heap was that low once, and everything
// that arrived in the next interval inherited the reading".
//
// Recording happens strictly AFTER the decision it describes, so nothing here
// can change what the guard did. The cost is time and BSS, never a different
// outcome.
//
// Gated on PA_ADMISSION_TRACE and off by default: this is evidence-gathering
// for the outstanding re-derivation of the admission floors, not a permanent
// surface. The floors in force were calibrated against the async stack's
// page-load transient and have not been re-derived for this one -- see the heap
// floor rationale in platformio.ini [flags_base] and
// docs/adr/0022-connection-admission-on-esp-http-server.md. The final
// verification of whatever replaces them has to run with this off, or it
// verifies a build nobody ships.
//
// Pure data and pure functions: no Arduino, no vendor type, no clock of its
// own. The device hookup lives in src/web/web_admission_psychic.cpp and the
// readback route in src/web/api_admission_trace.cpp.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef PA_ADMISSION_TRACE
#define PA_ADMISSION_TRACE 0
#endif

// -----------------------------------------------------------------------------
// Vocabulary
//
// Unconditional, unlike the ring below. The device hookup names the layer and
// the outcome of every decision it takes whether or not this build stores
// them, so a build with the trace off differs from one with it on by an empty
// inline function -- not by a second set of #if blocks threaded through the
// admission callbacks, which is where a divergence between the measured build
// and the shipped one would hide.
// -----------------------------------------------------------------------------

enum class WebAdmissionTraceLayer : unsigned char {
    kConnection = 0,
    kRequest = 1,
};

enum class WebAdmissionTraceOutcome : unsigned char {
    kAdmit = 0,
    kRejectRate = 1,
    kRejectHeap = 2,
    kRejectInflight = 3,
};

// Whether the request was a main-frame navigation. Three-valued on purpose:
// the classification costs two header reads, and the guard only pays for them
// on the path already committed to refusing. An admitted request is therefore
// genuinely unknown rather than assumed to be an asset, and a trace that
// pretended otherwise would invent the very distinction this ticket turns on.
enum class WebAdmissionTraceNavigation : unsigned char {
    kAsset = 0,
    kNavigation = 1,
    kUnknown = 2,
};

// Passed as the sample age when there is no sample for an age to be measured
// from -- an unprimed cache. Recorded at the ceiling rather than at 0, because
// 0 reads as "just measured", which is the opposite of what it means.
constexpr uint32_t kWebAdmissionTraceAgeUnknown = UINT32_MAX;

// -----------------------------------------------------------------------------
// The ring
// -----------------------------------------------------------------------------

#if PA_ADMISSION_TRACE

#include "web_json_slice_writer.h"

// -----------------------------------------------------------------------------
// Why a row is eight bytes
//
// This ring lives in static DRAM, and static DRAM is the same memory the heap
// is carved out of: every byte the ring takes is a byte the heap does not have.
// A trace large enough to be comfortable would therefore describe a controller
// with measurably less heap than the one that ships -- it would move the very
// reading it exists to record.
//
// It is also simply not available. Measured on this build: dram0_0_seg has
// 596 bytes of headroom, so an unpacked row of 44 bytes buys thirteen
// decisions.
//
// So a row is packed to eight bytes and carries no path. Three consequences,
// all deliberate:
//
//   - Readings are stored in 4-byte units. The heap allocator aligns to four,
//     so nothing is lost that was ever distinguishable.
//   - Timestamps are 16-bit offsets from the first row after a clear. A
//     capture is seconds long; the absolute base travels once, in the document.
//   - Paths are gone. What each row was serving is recoverable by position
//     from the capturing harness's own network log
//     (tools/webload_browser_capture.js records every request in order), which
//     is where that information already exists and costs nothing.
//
// Sixty-four rows at eight bytes is 512, which fits with margin left over.
// Raise it once the #91 cutover deletes the async scaffold's statics.
// -----------------------------------------------------------------------------

#ifndef WEB_ADMISSION_TRACE_CAPACITY
#define WEB_ADMISSION_TRACE_CAPACITY 64
#endif

// Readings are stored in 4-byte units, matching the heap allocator's own
// alignment, so the shift discards nothing that was ever measurable.
constexpr unsigned kWebAdmissionTraceBlockShift = 2;

// Ceilings for the packed fields. Each saturates rather than wraps: a wrapped
// value reads as a small, plausible number, and every field here is one where a
// plausible wrong number would be believed.
constexpr uint16_t kWebAdmissionTraceAgeSaturatedRaw = UINT8_MAX;
constexpr uint32_t kWebAdmissionTraceBlockMax =
    (uint32_t)UINT16_MAX << kWebAdmissionTraceBlockShift;
constexpr uint32_t kWebAdmissionTraceOffsetMax = UINT16_MAX;

struct WebAdmissionTraceEntry {
    // Milliseconds since the first decision recorded after the last clear.
    uint16_t msOffset;
    // The largest-free-block reading the decision actually used, in 4-byte
    // units. This is the shared cached sample, and may be up to one sample
    // interval old.
    uint16_t blockQuads;
    // A reading taken fresh immediately after the decision, in the same units,
    // or 0 when the build did not ask for one. The gap between this and
    // `blockQuads` is the measurement of how much a stale sample distorts the
    // guard: a large, one-directional gap means refusals are being taken on
    // heap states that no longer exist.
    uint16_t freshQuads;
    // Age of the used reading, in milliseconds, saturating at 255. The sample
    // interval is two orders of magnitude below that, so anything at the
    // ceiling is a cache nothing has refreshed rather than a long interval.
    uint8_t sampleAgeMs;
    // layer:1, outcome:2, navigation:2, in-flight depth:3. All four are small
    // enumerations or single digits, so one byte holds the lot; the accessors
    // below are the only thing that should know that.
    uint8_t flags;
};

// The calibration in force when the rows were recorded. Carried in the
// document rather than supplied by whoever reads it: every question this trace
// exists to answer is a question about a reading relative to a floor, and a
// profile labelled with the wrong floors is worse than none. It is also what
// makes two runs comparable after a floor moves.
struct WebAdmissionTraceConfig {
    uint32_t connectionFloor;
    uint32_t requestFloor;
    uint32_t requestFloorDiagnostic;
    uint32_t sampleIntervalMs;
    uint32_t maxInflightRequests;
};

// Written and read from the single server task, like the socket census, so it
// carries no lock. The readback route runs on that same task.
struct WebAdmissionTrace {
    WebAdmissionTraceEntry entries[WEB_ADMISSION_TRACE_CAPACITY];
    // Decisions ever recorded. Rows held is min(total, capacity); the
    // difference is what the ring overwrote, published rather than hidden so a
    // profile read after too long is visibly partial instead of quietly so.
    uint32_t total;
    uint32_t next;
    // Device milliseconds of the first row since the last clear. Row offsets
    // are measured from here, and it travels in the document so a profile can
    // still be lined up against /api/status uptime or a serial log.
    uint32_t baseMs;
    WebAdmissionTraceConfig config;
};

// Clears the rows. Deliberately leaves the calibration alone: clearing arms the
// ring for the next run, and a run that came back unlabelled because it was
// armed rather than rebooted would be evidence nobody could use.
void webAdmissionTraceInit(WebAdmissionTrace* trace);

void webAdmissionTraceConfigure(WebAdmissionTrace* trace, const WebAdmissionTraceConfig& config);

// Records one decision. Every value is given in its natural unit -- bytes,
// milliseconds, a real request depth -- and packed here, so no caller has to
// know the row layout. Values beyond what a packed field can hold saturate.
//
// `sampleAgeMs` is the age of the reading in `block`; pass
// kWebAdmissionTraceAgeUnknown when there is no sample to measure an age from.
void webAdmissionTraceRecord(WebAdmissionTrace* trace, uint32_t ms,
                             WebAdmissionTraceLayer layer, WebAdmissionTraceOutcome outcome,
                             uint32_t block, uint32_t fresh, uint32_t sampleAgeMs,
                             int inflight, WebAdmissionTraceNavigation navigation);

// Row accessors. The packing is an artefact of the DRAM budget above, so
// nothing outside this module reads the bits directly.
WebAdmissionTraceLayer webAdmissionTraceEntryLayer(const WebAdmissionTraceEntry& entry);
WebAdmissionTraceOutcome webAdmissionTraceEntryOutcome(const WebAdmissionTraceEntry& entry);
WebAdmissionTraceNavigation webAdmissionTraceEntryNavigation(const WebAdmissionTraceEntry& entry);
unsigned webAdmissionTraceEntryInflight(const WebAdmissionTraceEntry& entry);
uint32_t webAdmissionTraceEntryBlock(const WebAdmissionTraceEntry& entry);
uint32_t webAdmissionTraceEntryFresh(const WebAdmissionTraceEntry& entry);

// Writes the whole ring, oldest row first, as one JSON document through the
// ADR 0021 slice writer -- so a profile larger than any chunk buffer costs one
// chunk buffer and nothing is ever assembled whole. Called once per chunk with
// a different window; the ring must not change across those calls, which holds
// because every caller runs on the single server task.
void webAdmissionTraceWrite(const WebAdmissionTrace* trace, JsonSliceWriter& writer);

#endif  // PA_ADMISSION_TRACE
