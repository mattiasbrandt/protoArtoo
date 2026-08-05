// =============================================================================
// include/api_aux_led.h
//
// AUX LED API routes, written against the project-owned WebRequest seam
// (ADR 0021) and bound by the seam route table. Exposed so native tests can
// drive them directly through the host-test backend.
// =============================================================================
#pragma once

#include "web_request.h"

void handleAuxLedColorPost(WebRequest& req);
void handleAuxLedEffectPost(WebRequest& req);
