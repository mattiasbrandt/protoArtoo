// =============================================================================
// src/web/api_rc.cpp
//
// RC diagnostics API endpoints
//   GET  /api/rc       — JSON RC diagnostics snapshot
//   POST /api/rc/debug — Toggle verbose RC logging on/off
//
// Written against the project-owned WebRequest seam (ADR 0021) and bound by the
// seam route table. The diagnostics snapshot core is reused unchanged: this
// file captures, serializes and answers, and decides nothing about the payload.
// tasks/rc_diagnostics_contract.md governs that payload's shape.
// =============================================================================

#include "api_rc.h"

#include <ArduinoJson.h>

#include "api_json_response.h"
#include "commanded_modes.h"
#include "logging.h"
#include "rc_diagnostics_snapshot.h"

static const char* TAG = "RC";

// The largest body POST /api/rc/debug will accept. Inherited from the async
// handler, which had to size a malloc for the whole body before reading it;
// kept because it is also a sane bound on a two-field JSON object.
static constexpr size_t RC_DEBUG_BODY_MAX = 128;

// Ceiling on the diagnostics payload, above which the response is refused
// rather than allocated for. Pinned by test_api_rc_routes against snapshots
// filled to capacity with the widest values: 2488 bytes for the largest payload
// the capture path can build, and 2931 for a deliberate over-bound that fills
// the analog and digital channel buckets both.
static constexpr size_t RC_PAYLOAD_MAX = 3072;

void handleRcGet(WebRequest& req) {
    RcDiagnosticsSnapshot snap;
    captureRcDiagnosticsSnapshot(&snap);

    JsonDocument doc;
    if (!populateRcDiagnosticsJson(doc, snap)) {
        webSendJsonError(req, 500, "rc json build failed");
        return;
    }

    webSendJsonDocument(req, doc, RC_PAYLOAD_MAX, TAG);
    PA_LOG_DEBUG(TAG, "GET /api/rc");
}

void handleRcDebugPost(WebRequest& req) {
    // The backend owns body accumulation now, so the length check happens once
    // against the declared length instead of on the first arriving chunk.
    // An absent body reaching this as "too large" is inherited behaviour, kept
    // deliberately: the async handler answered 413 for both an empty and an
    // over-long body, and payload parity is this port's correctness bar.
    const size_t declared = req.contentLength();
    if (declared == 0 || declared > RC_DEBUG_BODY_MAX) {
        webSendJsonError(req, 413, "payload too large");
        return;
    }

    // Borrowed for the life of the request, not copied: nothing here outlives
    // the parse below. Null with a non-zero declared length means the backend
    // consumed the body as form parameters instead, which is not JSON and gets
    // the same answer the async stack's parse would have given it.
    const char* body = req.body();
    if (body == nullptr) {
        webSendJsonError(req, 400, "invalid json");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        webSendJsonError(req, 400, "invalid json");
        return;
    }

    const bool enabled = doc["enabled"] | false;
    commandedSetRcDebug(enabled, SRC_WEB_API);

    PA_LOG_INFO(TAG, "RC debug mode %s", enabled ? "enabled" : "disabled");
    req.send(200, "application/json", "{\"ok\":true}");
}
