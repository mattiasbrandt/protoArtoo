// =============================================================================
// seq_last_run_json.h
//
// Pure JSON builder for GET /api/seq/last-run. Keeping this separate from the
// AsyncWebServer handler makes the evidence contract native-testable.
// =============================================================================
#pragma once

#include <ArduinoJson.h>

#include "sequence_run_evidence.h"

// Populates doc with the same payload returned by GET /api/seq/last-run.
// Returns false only if the JsonDocument overflowed.
bool populateSeqLastRunJson(JsonDocument& doc, const SeqRunEvidence& ev, bool have);
