// =============================================================================
// include/api_config_snapshot.h
//
// ConfigSnapshot — a plain-data copy of all NVS-backed config fields from
// RobotState. Used by pure (FreeRTOS-free) JSON builders for the API layer.
//
// populateConfigJson(): pure function — no global state, no FreeRTOS.
//   Builds an ArduinoJson document from the snapshot.
//   Returns false if any RC binding format call fails (caller sends 500).
//   Defined in src/web/api_config.cpp.
//
// populateRcMapJson(): pure function — serializes RC map bindings to JSON.
//   Defined in src/web/api_config.cpp.
// =============================================================================
#pragma once

#include <stddef.h>
#include <ArduinoJson.h>

#include "config_store.h"
#include "rc_mapping.h"

// ConfigSnapshot is canonically defined in config_store.h.
// (Included above for use by API layer JSON helpers.)

// Pure helpers backing /api/rc/map serialization and slot assignment.
// Exposed for native regression tests.
static constexpr size_t kRcMapMaxEntries = 14;

struct RcMapEntry {
    RcBindingSource source;
    uint8_t channel;
    RobotActionId action;
    char payload[16];
};

bool populateRcMapJson(JsonDocument& doc, const ConfigSnapshot& snap);
void clearRcMapSlots(ConfigSnapshot* working);
bool assignRcMapEntryToSnapshot(const RcMapEntry& entry, const ConfigSnapshot& existing,
                                ConfigSnapshot* working, char* error, size_t errorSize);

// Populates doc from snap. Pure: no globals, no FreeRTOS.
// Returns false if any binding string format fails.
// Defined in src/web/api_config.cpp.
bool populateConfigJson(JsonDocument& doc, const ConfigSnapshot& snap);
