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

#include "web_request.h"

// Write a JSON config object into a caller-supplied buffer.
// Pure function - no globals, no Arduino, no FreeRTOS.
// params: buf               - output buffer (must not be null)
//         bufSize           - size of buf in bytes
//         speedLimitMax     - current speed limit cap
//         webDriveTimeoutMs - current web drive timeout in ms
// thread-safe: yes (pure function, no globals)
void formatConfigJson(char* buf, size_t bufSize, int16_t speedLimitMax, uint32_t webDriveTimeoutMs);

void handleConfigGet(WebRequest& req);
void handleConfigPost(WebRequest& req);
void handleRcMapGet(WebRequest& req);
void handleRcMapPost(WebRequest& req);
void handleWifiPost(WebRequest& req);
