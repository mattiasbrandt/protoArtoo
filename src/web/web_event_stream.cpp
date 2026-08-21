// =============================================================================
// src/web/web_event_stream.cpp
//
// Decision core for the live update stream, plus the counters it publishes. No
// Arduino, no vendor type, no clock of its own -- elapsed time and write
// outcomes arrive as parameters, so a host test pins the behaviour without
// pinning the calibration or needing a socket.
//
// See include/web_event_stream.h for why the stream evicts rather than queues.
// =============================================================================

#include "../../include/web_event_stream.h"

#include <stdio.h>
#include <string.h>

const int kWebEventStreamNoSocket = -1;

const char kWebEventStreamTerminator[] = "\r\n\r\n";
const size_t kWebEventStreamTerminatorLength = sizeof(kWebEventStreamTerminator) - 1;

volatile uint32_t g_webSseClientsPeak = 0;
volatile uint32_t g_webRefusedSseCap = 0;
volatile uint32_t g_webSseEvicted = 0;
volatile uint32_t g_webSseEvictLastMs = 0;

// -----------------------------------------------------------------------------
// Registry
// -----------------------------------------------------------------------------

void webEventStreamRegistryInit(WebEventStreamRegistry* registry) {
    if (registry == nullptr) {
        return;
    }
    for (size_t i = 0; i < PA_ADMISSION_MAX_SSE_CLIENTS; i++) {
        registry->sockets[i] = kWebEventStreamNoSocket;
    }
    registry->count = 0;
}

bool webEventStreamRegistryHas(const WebEventStreamRegistry* registry, int socket) {
    if (registry == nullptr || socket == kWebEventStreamNoSocket) {
        return false;
    }
    for (size_t i = 0; i < PA_ADMISSION_MAX_SSE_CLIENTS; i++) {
        if (registry->sockets[i] == socket) {
            return true;
        }
    }
    return false;
}

bool webEventStreamRegistryAdd(WebEventStreamRegistry* registry, int socket) {
    if (registry == nullptr || socket == kWebEventStreamNoSocket) {
        return false;
    }
    // Already registered: succeed without taking a second slot. The same
    // descriptor can legitimately arrive twice if a close callback is still in
    // flight when its replacement connects, and a duplicate entry would both
    // double-send and permanently consume capacity.
    if (webEventStreamRegistryHas(registry, socket)) {
        return true;
    }
    for (size_t i = 0; i < PA_ADMISSION_MAX_SSE_CLIENTS; i++) {
        if (registry->sockets[i] == kWebEventStreamNoSocket) {
            registry->sockets[i] = socket;
            registry->count++;
            return true;
        }
    }
    return false;
}

bool webEventStreamRegistryRemove(WebEventStreamRegistry* registry, int socket) {
    if (registry == nullptr || socket == kWebEventStreamNoSocket) {
        return false;
    }
    for (size_t i = 0; i < PA_ADMISSION_MAX_SSE_CLIENTS; i++) {
        if (registry->sockets[i] == socket) {
            registry->sockets[i] = kWebEventStreamNoSocket;
            registry->count--;
            return true;
        }
    }
    return false;
}

size_t webEventStreamRegistrySnapshot(const WebEventStreamRegistry* registry, int* out,
                                      size_t outCapacity) {
    if (registry == nullptr || out == nullptr) {
        return 0;
    }
    size_t written = 0;
    for (size_t i = 0; i < PA_ADMISSION_MAX_SSE_CLIENTS && written < outCapacity; i++) {
        if (registry->sockets[i] != kWebEventStreamNoSocket) {
            out[written++] = registry->sockets[i];
        }
    }
    return written;
}

// -----------------------------------------------------------------------------
// Framing
// -----------------------------------------------------------------------------

size_t webEventStreamFormatPrefix(char* out, size_t outSize, const char* event, uint32_t id) {
    // "data: " alone still has to fit, otherwise the caller would emit a frame
    // whose payload is not introduced by any field name.
    if (out == nullptr || outSize < sizeof("data: ")) {
        return 0;
    }

    size_t pos = 0;
    // An id of 0 is omitted rather than sent as "id: 0": the browser stores the
    // last id and replays it in Last-Event-ID on reconnect, and this project
    // sends millis() as an advisory timestamp with no replay semantics behind
    // it. Both vendor implementations omit it the same way.
    if (id != 0) {
        const int written = snprintf(out + pos, outSize - pos, "id: %lu\r\n", (unsigned long)id);
        if (written > 0 && (size_t)written < outSize - pos) {
            pos += (size_t)written;
        }
    }
    if (event != nullptr && event[0] != '\0') {
        const int written = snprintf(out + pos, outSize - pos, "event: %s\r\n", event);
        if (written > 0 && (size_t)written < outSize - pos) {
            pos += (size_t)written;
        }
    }
    // Unconditional, and the reason for the outSize floor above: a frame with no
    // "data:" field carries nothing an EventSource listener will ever see.
    const int written = snprintf(out + pos, outSize - pos, "data: ");
    if (written > 0 && (size_t)written < outSize - pos) {
        pos += (size_t)written;
    }
    return pos;
}

// -----------------------------------------------------------------------------
// Bounded send
// -----------------------------------------------------------------------------

WebEventSendVerdict webEventSendDecide(WebEventWriteResult result, uint32_t elapsedMs,
                                       uint32_t deadlineMs) {
    // A dead connection is dead regardless of the clock, and reporting it as a
    // deadline breach would inflate the eviction counter with ordinary tab
    // closes -- the one number that has to stay meaningful for this to be
    // diagnosable at all.
    if (result == WebEventWriteResult::kFailed) {
        return WebEventSendVerdict::kEvictError;
    }
    if (elapsedMs >= deadlineMs) {
        return WebEventSendVerdict::kEvictDeadline;
    }
    (void)result;
    return WebEventSendVerdict::kContinue;
}
