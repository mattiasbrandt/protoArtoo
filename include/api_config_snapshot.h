// =============================================================================
// include/api_config_snapshot.h
//
// ConfigSnapshot - a plain-data copy of all NVS-backed config fields from
// RobotState. Used by pure (FreeRTOS-free) JSON builders for the API layer.
//
// populateConfigJson(): pure function - no global state, no FreeRTOS.
//   Builds an ArduinoJson document from the snapshot.
//   Returns false if any RC binding format call fails (caller sends 500).
//   Defined in src/web/api_config.cpp.
//
// populateRcMapJson(): pure function - serializes RC map bindings to JSON.
//   Defined in src/web/api_config.cpp.
// =============================================================================
#pragma once

#include <stddef.h>
#include <ArduinoJson.h>

#include "api_apply_refusal.h"
#include "config_store.h"
#include "rc_mapping.h"
#include "rc_map_rules.h"

// ConfigSnapshot is canonically defined in config_store.h.
// (Included above for use by API layer JSON helpers.)

// The pure builder behind GET /api/rc/map, exposed for native regression
// tests. Where a save puts each binding is the RC Map Store's
// (rcMapStorePlace(), include/rc_map_store.h).

bool populateRcMapJson(JsonDocument& doc, const ConfigSnapshot& snap);

// Populates doc from snap. Pure: no globals, no FreeRTOS.
// Returns false if any binding string format fails.
// Defined in src/web/api_config.cpp.
bool populateConfigJson(JsonDocument& doc, const ConfigSnapshot& snap);
