// =============================================================================
// include/drive_capabilities.h
//
// The Foot Drive family's capability vocabulary (ADR 0042 as amended
// 2026-09-09: "The Component Family owns the vocabulary; each row declares its
// own bits"). Sound's words are AudioDriver::AUDIO_CAP_* (include/audio_driver.h);
// these are the Foot Drive's.
//
// A bit is declared once, on the product's Component Registry row
// (include/component_registry.inc). The drive backend profile reads its own
// row's word rather than restating it (include/drive_backend.h), and the Foot
// Drive page reads the same word off GET /api/identity/components, so the
// firmware, the lineup and the browser cannot disagree about what a Foot Drive
// reports.
//
// Its own header rather than a line in include/drive_backend.h because the
// dependency runs both ways: the registry needs these names to expand its
// rows, and the backend profile needs the registry to read its row.
//
// Every bit a supported row declares must have a consumer -
// tools/check_component_registry_drift.py reads this file for the names.
// =============================================================================
#pragma once

#include <stdint.h>

// The Foot Drive reports readings back over its wire (battery, board
// temperature, wheel speed, current). Firmware polls for them only when this is
// declared; the Foot Drive page shows the wheel controller's readings only when
// this is declared.
constexpr uint8_t DRIVE_CAP_REPORTS_FEEDBACK = 0x01;
