// =============================================================================
// src/web/api_estop.cpp
//
// ESTOP API endpoints
//   POST /api/estop        - latch estop (requires explicit clear to resume)
//   POST /api/estop/clear  - clear estop (explicit human gate)
//
// Written against the project-owned WebRequest seam (ADR 0021) and bound by the
// seam route table. The latching behaviour itself lives in failsafe_gate.cpp;
// these handlers only carry the operator's intent across the web boundary, so
// a backend swap cannot change what latching means.
// =============================================================================

#include "api_estop.h"

#include "failsafe_gate.h"
#include "logging.h"
#include "robot_state.h"
#include "web_server.h"

static const char* TAG = "WebServer";

void handleEstopClearPost(WebRequest& req) {
    failsafeClearEstop();
    requestStatusBroadcastNow();
    PA_LOG_INFO(TAG, "[WEB] POST /api/estop/clear - estop cleared");
    req.send(200, "application/json", "{\"ok\":true}");
}

void handleEstopPost(WebRequest& req) {
    failsafeTrigger(FailsafeLayer::ESTOP);
    requestStatusBroadcastNow();
    PA_LOG_INFO(TAG, "[WEB] POST /api/estop - estop latched");
    req.send(200, "application/json", "{\"ok\":true}");
}
