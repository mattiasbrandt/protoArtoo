// =============================================================================
// include/api_servo.h
//
// Servo control API endpoint, written against the project-owned WebRequest
// seam (ADR 0021) and bound by the seam route table. Exposed so native tests
// can drive it directly through the host-test backend.
// =============================================================================
#pragma once

#include "web_request.h"

void handleServoPost(WebRequest& req);
