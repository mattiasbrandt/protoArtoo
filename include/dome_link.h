// =============================================================================
// include/dome_link.h
//
// DomeLinkTask  --  bidirectional Marcduino serial link to the dome controller.
//
// Physical link: UART_PORT_DOME (Serial2), 9600 baud 8N1, on PIN_DOME_TX /
// PIN_DOME_RX. Both are per Board Variant (include/config.h); on artoo-esp32
// that is PCB header S3 ("Dome Control"), GPIO 33 TX / GPIO 34 RX. Connected
// over slip ring to AstroPixelsPlus.
//
// Responsibilities:
//   TX  --  drain domeTxQueue and write ASCII commands to the dome UART; send
//        #PAHB\r heartbeat to dome at 1 Hz.
//   RX  --  read CR-terminated lines from the dome UART; intercept #APHB
//        heartbeat; route remaining commands through the Marcduino body parser
//        (parseMarcduinoCommand) which dispatches to AudioTask / ServoTask.
//   DomeLinkTask is the sole writer for dome-bound Marcduino TX; callers must
//   enqueue via domeQueueTx() and never write the dome UART directly.
//
// UART ownership contract -- board-dependent, keyed on PA_CAP_DEDICATED_AUDIO_UART:
//   - Capability 0 (artoo-esp32, three HP UARTs): the audio module's RX shares
//     this controller. DomeLinkTask owns it while the active transport is
//     UART, and releases it to audio RX on PIN_AUDIO_RX while the transport is
//     WiFi fallback. Arbitrated through domeUartAcquire()/domeUartRelease().
//   - Capability 1 (firebeetle2, five HP UARTs): audio has UART_PORT_AUDIO to
//     itself, so there is nothing to hand over. DomeLinkTask owns
//     UART_PORT_DOME for the whole boot and the handoff does not run (#254).
//
// Queue sends from real-time tasks must use domeQueueTx() (timeout 0).
//
// Dome Layout Cache:
//   DomeLinkTask maintains a cache of the dome's /api/dome/layout JSON (~24KB).
//   Web handlers call domeLayoutCacheReadChunk() to stream cached bytes out in
//   small pieces (non-blocking, no per-request buffer).
//   Call domeLayoutCacheRefreshRequested() to trigger an on-demand refresh.
// =============================================================================
#pragma once

#include <stdint.h>
#include <string.h>

// config.h is reached transitively through robot_state.h, but the audio-claim
// helpers below are gated on PA_CAP_DEDICATED_AUDIO_UART, so name the
// dependency directly rather than letting a header reshuffle silently turn a
// #if into "capability absent".
#include "config.h"
#include "robot_state.h"

// Status of the dome layout cache
struct DomeLayoutCacheStatus {
    bool has_data;       // Cache is valid and contains data
    size_t length;       // Size of cached JSON in bytes
    uint32_t fetched_at_ms;  // Timestamp when cache was last populated
    int last_http_status;    // HTTP status from last fetch attempt (0 = no attempt yet)
};

// -----------------------------------------------------------------------------
// DomeTxCmd  --  message placed on domeTxQueue.
// buf holds the Marcduino command WITHOUT the trailing \r.
// DomeLinkTask appends \r before writing to UART2.
// 64 bytes comfortably covers all Marcduino command lengths.
// -----------------------------------------------------------------------------
struct DomeTxCmd {
    char buf[64];
};

// Queue handle  --  defined in main.cpp alongside other queues.
extern QueueHandle_t domeTxQueue;

// -----------------------------------------------------------------------------
// domeLinkTask()  --  FreeRTOS task entry point.
// Pinned to Core 1, priority 3 (below ServoTask/DomeTask; above SafetyMonitor).
// Stack: 3072 bytes.
// -----------------------------------------------------------------------------
void domeLinkTask(void* pvParameters);

// -----------------------------------------------------------------------------
// domeQueueTx()
// Non-blocking enqueue of a Marcduino command for TX to the dome.
// cmd should NOT include the trailing \r  --  DomeLinkTask appends it.
// Returns true if enqueued, false if queue was full.
// Safe to call from any task with timeout 0.
// -----------------------------------------------------------------------------
bool domeQueueTx(const char* cmd);

// True when a dome heartbeat has been seen within the timeout window.
bool domeConnected();

// Dome layout cache accessors (thread-safe, non-blocking).

// Get current cache status (has_data, length, fetched_at_ms, last_http_status).
DomeLayoutCacheStatus domeLayoutCacheGetStatus();

// Copy up to maxLen bytes starting at offset from the cached layout into outBuf.
// fetchedAtMs pins the read to the cache generation observed via
// domeLayoutCacheGetStatus(): if the cache has since been refreshed (a
// different fetched_at_ms), this returns 0 rather than mixing bytes from two
// different fetches. Returns 0 if offset is out of range or the cache is empty.
// Intended for chunked response fillers (small maxLen per call) so no
// per-request buffer is needed. Thread-safe.
size_t domeLayoutCacheReadChunk(uint8_t* outBuf, size_t maxLen, size_t offset, uint32_t fetchedAtMs);

// Request an on-demand cache refresh. Returns false if throttled (within 30s of last fetch).
// DomeLinkTask will fetch on next WiFi-connected loop iteration if refresh is pending.
bool domeLayoutCacheRefreshRequested();

bool domeUartAcquire(DomeUartOwner requester);
void domeUartRelease(DomeUartOwner requester);
bool domeUartOwnedBy(DomeUartOwner owner);

// -----------------------------------------------------------------------------
// Audio-side claim on the UART controller an audio query needs.
//
// The arbiter above exists because two consumers share one controller. Whether
// they do is a Board Variant fact, so the audio backends ask through these two
// helpers rather than each calling domeUartAcquire() directly:
//
//   - Capability 0: borrow the dome link's controller. The claim can be
//     DENIED, and a denied claim is what AUDIO_RX_BLOCKED_BY_DOME_UART reports.
//   - Capability 1: audio owns UART_PORT_AUDIO outright, so the claim always
//     succeeds and no shared state is touched. Without this, the P4 in the
//     posture epic #182 calls primary -- dome link on its own serial UART --
//     would have every audio status query denied by an arbiter guarding a
//     controller audio no longer needs (#254).
//
// Paired: every audioUartClaim() that returns true owes an audioUartRelease().
// -----------------------------------------------------------------------------
inline bool audioUartClaim() {
#if PA_CAP_DEDICATED_AUDIO_UART
    return true;
#else
    return domeUartAcquire(DOME_UART_AUDIO);
#endif
}

inline void audioUartRelease() {
#if PA_CAP_DEDICATED_AUDIO_UART
    // Nothing was borrowed, so there is nothing to give back. Releasing here
    // would clear robotState.domeUartOwner out from under the dome link, which
    // holds the controller permanently on this board.
#else
    domeUartRelease(DOME_UART_AUDIO);
#endif
}
