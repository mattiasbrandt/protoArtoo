// =============================================================================
// include/api_json_response.h
//
// Sending an ArduinoJson document through the WebRequest seam (ADR 0021).
//
// The seam's send() takes a complete body, so a handler that built a
// JsonDocument needs the serialized bytes somewhere first. Two ways were tried
// and rejected before this one:
//
//   - a static per-route buffer, the shape api_status.cpp uses. Two of them
//     (3 KB + 2 KB) overflowed `.dram0.bss` by 1336 bytes on the psychic build,
//     which links with under 4 KB of DRAM to spare. Permanent BSS is the
//     scarcest budget on this target and a fixed buffer spends it on the
//     worst case at all times.
//   - streaming through sendChunked(), which asks for bytes by offset. That
//     needs random access into a serialization that has not happened yet, so
//     it does not remove the buffer -- only moves it.
//
// So the buffer is allocated per request at exactly the measured size and
// freed before the handler returns. Both device backends copy the body during
// send(), so the bytes are safe to release immediately after. Bounded
// per-request allocation in a Core 0 web handler, which is what the project
// permits; the real-time loops on Core 1 still allocate nothing.
// =============================================================================
#pragma once

#include <ArduinoJson.h>
#include <stddef.h>

#include "web_request.h"

// Serialize doc and answer 200 application/json with it.
//
// maxBytes is a sanity ceiling, not a buffer size: a payload measuring at or
// above it is refused with a 500 rather than attempted, so a runaway document
// cannot turn into a runaway allocation. Callers pass the largest payload their
// route can legitimately produce, with headroom.
//
// Answers 500 itself on both failure paths -- over-ceiling and allocation
// failure -- so a caller never has to decide what to do about them. tag names
// the route in the log line that accompanies either.
void webSendJsonDocument(WebRequest& req, const JsonDocument& doc, size_t maxBytes,
                         const char* tag);
