// =============================================================================
// include/api_aux_led.h
//
// AUX LED API routes, written against the project-owned WebRequest seam
// (ADR 0021) and bound by the seam route table. Exposed so native tests can
// drive them directly through the host-test backend.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "web_request.h"

// Format JSON response for AUX LED endpoints.
// Output: {"ok":true,"auxLed":{"pin":<u8>,"r":<u8>,"g":<u8>,"b":<u8>,"effect":"..."}}
// Returns false if the payload does not fit in buf.
bool formatAuxLedStateJson(char* buf, size_t bufSize, uint8_t pin, uint8_t r, uint8_t g, uint8_t b,
                           const char* effect);

void handleAuxLedColorPost(WebRequest& req);
void handleAuxLedEffectPost(WebRequest& req);
