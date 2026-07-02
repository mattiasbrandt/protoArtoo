// =============================================================================
// include/dome_link.h
//
// DomeLinkTask — bidirectional Marcduino serial link to the dome controller.
//
// Physical link: UART2 (Serial2), 9600 baud 8N1, GPIO 33 TX / GPIO 34 RX.
// PCB header: S3 ("Dome Control"). Connected over slip ring to AstroPixelsPlus.
//
// Responsibilities:
//   TX — drain domeTxQueue and write ASCII commands to UART2; send #PAHB\r
//        heartbeat to dome at 1 Hz.
//   RX — read CR-terminated lines from UART2; intercept #APHB heartbeat;
//        route remaining commands through the Marcduino body parser
//        (parseMarcduinoCommand) which dispatches to AudioTask / ServoTask.
//
// UART2 ownership contract (S2 audio RX + S3 dome link share UART2 hardware):
//   - When active transport is UART, DomeLinkTask owns UART2 on GPIO33/34.
//   - When active transport is WiFi fallback, DomeLinkTask releases UART2 so
//     audio status/query paths can reclaim RX on GPIO35.
//   - DomeLinkTask is still the sole writer for dome-bound Marcduino TX; callers
//     must enqueue via domeQueueTx() and never write Serial2 directly.
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

#include "robot_state.h"

// Status of the dome layout cache
struct DomeLayoutCacheStatus {
    bool has_data;       // Cache is valid and contains data
    size_t length;       // Size of cached JSON in bytes
    uint32_t fetched_at_ms;  // Timestamp when cache was last populated
    int last_http_status;    // HTTP status from last fetch attempt (0 = no attempt yet)
};

// -----------------------------------------------------------------------------
// DomeTxCmd — message placed on domeTxQueue.
// buf holds the Marcduino command WITHOUT the trailing \r.
// DomeLinkTask appends \r before writing to UART2.
// 64 bytes comfortably covers all Marcduino command lengths.
// -----------------------------------------------------------------------------
struct DomeTxCmd {
    char buf[64];
};

// Queue handle — defined in main.cpp alongside other queues.
extern QueueHandle_t domeTxQueue;

// -----------------------------------------------------------------------------
// domeLinkTask() — FreeRTOS task entry point.
// Pinned to Core 1, priority 3 (below ServoTask/DomeTask; above SafetyMonitor).
// Stack: 3072 bytes.
// -----------------------------------------------------------------------------
void domeLinkTask(void* pvParameters);

// -----------------------------------------------------------------------------
// domeQueueTx()
// Non-blocking enqueue of a Marcduino command for TX to the dome.
// cmd should NOT include the trailing \r — DomeLinkTask appends it.
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
