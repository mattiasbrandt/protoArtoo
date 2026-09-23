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

// One lit wire, as a reading to serialise. `id` is the Output's components{}
// key - a stored identifier, never a name a builder reads - which is what a
// surface iterating GET /api/config's Outputs already holds. There is no pin
// here and there is no Output label: where a wire plugs in is Wiring's answer,
// and a status frame that carried a board's pin number would be the third
// place it is written down.
struct LitWireReading {
    const char* id;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    const char* effect;
    bool available;  // false when this wire's driver did not start
};

// The droid's lit wires as a JSON object, keyed by Output id:
//   {"aux1":{"r":<u8>,"g":<u8>,"b":<u8>,"effect":"...","available":true}}
// An empty list writes {} - a droid with no light says so rather than leaving
// a surface to tell "no lights" from "no answer".
//
// The OBJECT ONLY, with no field name around it, so the status frame can put it
// under "lights" and the aux-LED endpoints can wrap it in an "ok" envelope
// without two spellings of one shape existing.
//
// Returns false if the payload does not fit in buf; *written, when given, is
// the length it wrote.
bool formatLitWiresJson(char* buf, size_t bufSize, const LitWireReading* wires, size_t count,
                        size_t* written);

void handleAuxLedColorPost(WebRequest& req);
void handleAuxLedEffectPost(WebRequest& req);
