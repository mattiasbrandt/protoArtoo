// =============================================================================
// include/hosted_link_status.h
//
// Read-only snapshot accessor for the ESP-Hosted C6 link supervisor's phase
// and counters, exposed to /api/status (#189). Only defined on
// boards where PA_CAP_HOSTED_WIFI is set (src/web/web_network_manager_hosted.cpp);
// web_server.cpp's call site is itself guarded by the same capability gate,
// so this header carries no #if of its own -- it is unreachable, not
// undefined, on boards without the capability.
// =============================================================================
#pragma once

#include "hosted_link_supervisor.h"

struct HostedLinkStatusSnapshot {
    HostedLinkPhase phase = HostedLinkPhase::Idle;
    unsigned int transportFailureEventCount = 0;
    unsigned int transportUpEventCount = 0;
    unsigned int attemptCount = 0;
    unsigned int totalAttemptCount = 0;
    unsigned int recoveredCount = 0;
    uint32_t lastFailureAtMs = 0;
    uint32_t lastAttemptAtMs = 0;
    uint32_t degradedAtMs = 0;
};

// Thread-safe: copies the supervisor state under its own critical section.
// Defined in web_network_manager_hosted.cpp.
HostedLinkStatusSnapshot hostedLinkQueryStatus();
