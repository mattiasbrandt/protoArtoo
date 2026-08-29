// =============================================================================
// include/api_config.h
//
// Config, RC-map and WiFi API endpoints, written against the project-owned
// WebRequest seam (ADR 0021) and bound by the seam route table. Exposed so
// native tests can drive them directly through the host-test backend.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "api_config_apply.h"
#include "config_store.h"
#include "robot_state.h"  // CommandSource
#include "web_request.h"

// Write a JSON config object into a caller-supplied buffer.
// Pure function - no globals, no Arduino, no FreeRTOS.
// params: buf               - output buffer (must not be null)
//         bufSize           - size of buf in bytes
//         speedLimitMax     - current speed limit cap
//         webDriveTimeoutMs - current web drive timeout in ms
// thread-safe: yes (pure function, no globals)
void formatConfigJson(char* buf, size_t bufSize, int16_t speedLimitMax, uint32_t webDriveTimeoutMs);

// Commit Step for the POST /api/config Apply Core (ADR 0034): the complete
// transport-independent tail of a config write - replay the core's applied-
// field log lines, sync the config cache, resync stationary mode (with its
// existing edge-detect/drive-on-cue logic, ADR 0012), fire the dome-on-cue
// action, persist to NVS, and (on success) broadcast the new status. This is
// "persistSystemConfig(WebRequest&, ...)"'s sibling named in ADR 0034's
// Consequences as the first extraction target: the HTTP handler
// (handleConfigPost) and the Controller Console (src/console/
// console_module.cpp) both call this instead of each carrying their own copy
// of the sequence. `working` must already hold configApply()'s output;
// `source` is forwarded to commandedSetStationary() for its Command Source
// provenance (#221) - the web route always passes SRC_WEB_API, the Console
// passes SRC_SERIAL_CONSOLE/SRC_WEB_CONSOLE.
//
// ConfigCommitOutcome is small (one bool + one ConfigSnapshot, no log-record
// payload) - safe as a stack local, unlike ConfigApplyResult.
struct ConfigCommitOutcome {
    bool persisted = false;  // false -> caller reports "failed to persist config"
    ConfigSnapshot snap;     // the post-apply, post-cache-resync snapshot
};
ConfigCommitOutcome configCommitApplied(ConfigSnapshot* working, const ConfigApplyResult& result,
                                         CommandSource source);

void handleConfigGet(WebRequest& req);
void handleConfigPost(WebRequest& req);
void handleRcMapGet(WebRequest& req);
void handleRcMapPost(WebRequest& req);
void handleWifiPost(WebRequest& req);
