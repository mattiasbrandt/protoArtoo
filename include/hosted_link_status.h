// =============================================================================
// include/hosted_link_status.h
//
// Read-only snapshot accessor for the ESP-Hosted C6 link supervisor's phase
// and counters, exposed to /api/status (#189) and to the Console's
// system.status.hosted-link (#471). Only defined on boards where
// PA_CAP_HOSTED_WIFI is set (src/web/web_network_manager_hosted.cpp); both
// call sites, web_server.cpp's and console_module.cpp's, are themselves
// guarded by the same capability gate, so this header carries no #if of its
// own -- it is unreachable, not undefined, on boards without the capability.
// =============================================================================
#pragma once

#include "hosted_link_supervisor.h"

// How the last ladder attempt's re-init went (#471).
enum class HostedLinkInitOutcome : uint8_t {
    None,     // no attempt yet this boot
    Ok,       // hostedInitWiFi() returned true
    Refused,  // the fit check refused it: the heap would not hold the re-init
    Failed,   // hostedInitWiFi() ran and returned false
};

inline const char* hostedLinkInitOutcomeName(HostedLinkInitOutcome outcome) {
    switch (outcome) {
        case HostedLinkInitOutcome::None:
            return "none";
        case HostedLinkInitOutcome::Ok:
            return "ok";
        case HostedLinkInitOutcome::Refused:
            return "refused";
        case HostedLinkInitOutcome::Failed:
            return "failed";
    }
    return "unknown";
}

// The last ladder attempt's verdict inputs (#471): what
// hostedRunRecoveryLadder() decided "recovered" or "failed" from, kept after
// its attempt-result log line has rotated out of the log ring. The whole
// record is replaced on every attempt, so a refused attempt never carries an
// earlier attempt's liveness results.
//
// The one-byte members lead, here and in the snapshot below, so they share a
// word instead of each padding one out: the snapshot is a member of
// StatusJsonInputs, which buildStatusJson() holds on the WebEvents task's
// stack, a chain sized by the rule (tools/task_stack_recipes.json).
struct HostedLinkLastAttempt {
    HostedLinkInitOutcome init = HostedLinkInitOutcome::None;
    // false when the host was not initialised after the re-init, so the C6
    // was not asked; heartbeatSeen and the two results are then meaningless.
    bool livenessAsked = false;
    bool heartbeatSeen = false;  // a heartbeat newer than the re-init arrived
    // Refused only: the probe allocation that failed ("channel pool", "small
    // allocations", "SDIO pool"), or "free total short" when the free total
    // was already below the need and no probe ran. A string literal.
    const char* refusal = nullptr;
    // esp_err_t values, carried as plain integers: this header stays free of
    // ESP-IDF types like the step core it extends, and the number is the
    // lossless form -- esp_err_to_name() has no name for the host-side
    // RPC_ERR_* codes and answers "UNKNOWN ERROR" for them.
    int32_t heartbeatConfigResult = 0;
    int32_t wifiGetModeResult = 0;
};

struct HostedLinkStatusSnapshot {
    HostedLinkPhase phase = HostedLinkPhase::Idle;
    // The liveness watch (#471), copied from HostedLinkSupervisorState: its
    // source here beside phase, its counters below.
    HostedLinkLivenessSource livenessSource = HostedLinkLivenessSource::None;
    unsigned int transportFailureEventCount = 0;
    unsigned int transportUpEventCount = 0;
    unsigned int attemptCount = 0;
    unsigned int totalAttemptCount = 0;
    unsigned int recoveredCount = 0;
    uint32_t lastFailureAtMs = 0;
    uint32_t lastAttemptAtMs = 0;
    uint32_t degradedAtMs = 0;

    unsigned int livenessMissCount = 0;
    unsigned int heartbeatCount = 0;
    uint32_t lastHeartbeatNumber = 0;
    // Milliseconds since the point the miss check measures from: the active
    // source's last evidence, or the watch's start when none has arrived yet
    // (HostedLinkSupervisorState::lastLivenessAtMs). -1 while no watch runs
    // (livenessSource None: before the transport first comes up, during a
    // ladder run, and for good once Degraded).
    int32_t livenessAgeMs = -1;

    HostedLinkLastAttempt lastAttempt;
};

// Thread-safe: copies the supervisor state under its own critical section.
// Defined in web_network_manager_hosted.cpp. Fills *out rather than
// returning the snapshot, like captureWifiStatusSnapshot(): a returned struct
// assigned into StatusJsonInputs costs buildStatusJson() a second copy of it
// on the WebEvents task's stack.
void hostedLinkQueryStatus(HostedLinkStatusSnapshot* out);
