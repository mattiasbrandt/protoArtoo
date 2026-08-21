// =============================================================================
// include/web_event_stream.h
//
// The live update stream (/api/events), owned by the project rather than by any
// web stack's EventSource class.
//
// PsychicEventSource is not used, and neither was the stack before it: both
// carry the same defect, an unbounded send.
// PsychicEventSourceClient::sendEvent() retries httpd_socket_send() in a
// `while (result == HTTPD_SOCK_ERR_TIMEOUT)` loop with no exit, against a socket
// whose send timeout defaults to five seconds. A client that stops reading fills
// its receive window and the broadcaster stops broadcasting -- a diagnostics
// blackout for every viewer, which is exactly when an operator needs the stream.
//
// The fix is to evict, not to buffer. A bounded queue would protect other
// clients from one bad one, but under this project's single-operator profile the
// stalled client IS the operator; dropping the socket and letting
// data/status_stream.js's existing exponential backoff reconnect returns them to
// a live stream, while a queue would only delay the same blackout.
//
// This header holds the decisions that make that possible in pure form: which
// connections are registered, how one event is framed, and when one client's
// send has run out of time. The transport lives behind the two backend-provided
// functions at the bottom. See docs/adr/0021-project-owned-web-request-seam.md.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

// Concurrent /api/events clients. Real operator use is one or two tabs; this
// leaves room for a couple of legitimate viewers without letting an unbounded
// number of tabs and reloads pile up long-lived connections.
#ifndef PA_ADMISSION_MAX_SSE_CLIENTS
#define PA_ADMISSION_MAX_SSE_CLIENTS 3
#endif

// How long one client may take to accept one whole event before it is dropped.
// Calibration lives in platformio.ini [flags_base] alongside the other admission
// thresholds; this is only the fallback for a build that does not set it.
#ifndef PA_SSE_SEND_DEADLINE_MS
#define PA_SSE_SEND_DEADLINE_MS 250
#endif

// -----------------------------------------------------------------------------
// Registry
// -----------------------------------------------------------------------------

// Which connections are currently subscribed, identified by the transport's own
// connection identity (a socket descriptor on both stacks). Fixed capacity and
// no heap: this is mutated from the server task when a stream opens or closes
// and read from the broadcaster task on every tick, so it must not be able to
// fail an allocation at either point.
//
// A free slot is kWebEventStreamNoSocket rather than 0, because 0 is a valid
// descriptor.
struct WebEventStreamRegistry {
    int sockets[PA_ADMISSION_MAX_SSE_CLIENTS];
    size_t count;
};

extern const int kWebEventStreamNoSocket;

void webEventStreamRegistryInit(WebEventStreamRegistry* registry);

// Registers a connection. Returns false when the registry is full, which is the
// client cap doing its job. Re-adding an already-registered connection succeeds
// without consuming a second slot: a reconnect that reuses a descriptor must not
// be able to leak capacity.
bool webEventStreamRegistryAdd(WebEventStreamRegistry* registry, int socket);

// Unregisters a connection. Returns false when it was not registered, so a close
// callback for an ordinary request costs nothing and a double removal -- the
// broadcaster evicting a client whose close callback has already fired -- is
// harmless rather than corrupting the count.
bool webEventStreamRegistryRemove(WebEventStreamRegistry* registry, int socket);

bool webEventStreamRegistryHas(const WebEventStreamRegistry* registry, int socket);

// Copies the registered connections into out, oldest slot first, and returns how
// many were written. The broadcaster works from a snapshot so it can send
// without holding a lock across a bounded-but-not-instant write.
size_t webEventStreamRegistrySnapshot(const WebEventStreamRegistry* registry, int* out,
                                      size_t outCapacity);

// -----------------------------------------------------------------------------
// Framing
// -----------------------------------------------------------------------------

// An event goes out as three pieces: this prefix, the caller's payload buffer,
// and the terminator. Nothing concatenates them, so a 3 KB rc payload is sent
// straight out of the buffer that already holds it -- no second buffer sized for
// the whole frame, and no allocation, which is what both vendors' std::string
// message builders cost per client per event.
extern const char kWebEventStreamTerminator[];
extern const size_t kWebEventStreamTerminatorLength;

// Longest prefix this can produce: "id: 4294967295\r\n" plus an event name plus
// "data: ". 64 bytes leaves room for any name this project sends.
static const size_t kWebEventStreamPrefixMax = 64;

// Writes "id: <id>\r\nevent: <event>\r\ndata: " into out and returns its length,
// excluding the terminating null. An id of 0 and a null event name are omitted
// rather than sent empty, matching what both vendor implementations emit. An
// over-long event name is truncated rather than overflowing; returns 0 if out is
// too small to hold anything useful.
size_t webEventStreamFormatPrefix(char* out, size_t outSize, const char* event, uint32_t id);

// -----------------------------------------------------------------------------
// Bounded send
// -----------------------------------------------------------------------------

// One non-blocking write attempt's outcome, normalised by the backend so this
// decision names no transport constant.
enum class WebEventWriteResult : unsigned char {
    kWrote,       // some bytes went out; the caller has advanced its cursor
    kWouldBlock,  // the client's receive window is full right now
    kFailed,      // the connection is gone
};

enum class WebEventSendVerdict : unsigned char {
    kContinue,
    kEvictDeadline,
    kEvictError,
};

// Whether to keep pushing this event at this client. Called after every attempt,
// with the time since the event's first attempt.
//
// The deadline applies to attempts that made progress as well as to blocked
// ones. A client that accepts a handful of bytes per attempt is stalling the
// broadcaster just as effectively as one that accepts none, and "it is still
// moving" is precisely the excuse that lets a slow reader hold the stream open
// indefinitely.
WebEventSendVerdict webEventSendDecide(WebEventWriteResult result, uint32_t elapsedMs,
                                       uint32_t deadlineMs);

// -----------------------------------------------------------------------------
// Counters
// -----------------------------------------------------------------------------
//
// Published through /api/status. ADR 0017 scores sseClients directly, so the
// count has to be the number of streams actually open rather than a proxy for it.

// Highest concurrent stream count seen this boot.
extern volatile uint32_t g_webSseClientsPeak;

// Connections refused because the cap was already met.
extern volatile uint32_t g_webRefusedSseCap;

// Streams dropped by the deadline, and when the last one went. Eviction is rare
// by design, which is exactly why it needs a counter: a run that never triggers
// it is otherwise indistinguishable from one where it silently stopped working.
extern volatile uint32_t g_webSseEvicted;
extern volatile uint32_t g_webSseEvictLastMs;

// -----------------------------------------------------------------------------
// Backend-provided transport
// -----------------------------------------------------------------------------
//
// eventStreamTask() in src/web/web_server.cpp owns scheduling -- the on-demand
// status flag, the 1 Hz rc snapshot, the every-other-tick log batch -- and
// reaches the wire only through these two. Defined once per build, next to that
// build's request seam implementation.

// Streams currently open.
size_t webEventStreamClientCount();

// Sends one event to every open stream. data must be free of bare newlines; the
// log batch already joins its lines with \x01 for this reason.
void webEventStreamBroadcast(const char* event, const char* data, uint32_t id);
