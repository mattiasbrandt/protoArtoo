// =============================================================================
// include/seq_json.h
//
// Learned Sequence JSON format v1 — parse and serialize (issue #2 slice 3,
// ADR 0006). Maps 1:1 onto the SeqStep / SeqStepParams model so the slice-2
// engine is the single interpreter; no separate runtime representation.
//
//   parse:     JSON text -> SeqDraft (into caller-owned SeqStep buffers).
//              Returns parse-level errors in the ProtocolCheckResult shape so
//              the API layer reports parse and Protocol Check failures the same
//              way. Semantic bounds are NOT checked here — callers run
//              protocolCheck() on the resulting draft.
//   serialize: SequenceEntry -> JSON text (powers GET /api/seq/builtins and the
//              editor's clone-to-retrain).
//
// Format v1:
//   { "format":1, "name":"DM:X", "suppressMs":8000, "toggleGroup":"none",
//     "meta":{...},
//     "steps":[ {"t":0,"type":"audio","cmd":"$H"},
//               {"t":0,"type":"dome","cmd":":SM0,2200,150"},
//               {"t":0,"type":"loop","body":2,"periodMs":1846,"durationMs":14000},
//               {"t":0,"type":"random","set":"ring","pulseMin":1150,"pulseMax":1500,
//                "moveMs":300,"jitterMs":500,"distinct":true},
//               {"t":0,"type":"audioCat","category":"alert","fallback":"scream"},
//               {"t":500,"type":"end"} ],
//     "closeSteps":[] }
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <ArduinoJson.h>

#include "protocol_check.h"   // SeqDraft, ProtocolCheckResult
#include "sequence_engine.h"  // SeqStep, SeqToggleGroup, SeqSlotSet

// Current on-disk format version.
static const int SEQ_JSON_FORMAT = 1;

// String <-> enum helpers (shared with the store/API). Return false on unknown.
bool seqToggleGroupFromString(const char* s, SeqToggleGroup& out);
const char* seqToggleGroupToString(SeqToggleGroup g);
bool seqSlotSetFromString(const char* s, SeqSlotSet& out);
const char* seqSlotSetToString(SeqSlotSet s);

// Parse JSON v1 into a draft, filling the caller-owned step buffers. On success
// `out.steps`/`out.closeSteps` point at the provided buffers. effectClass is
// left FX_NONE — protocolCheck() stamps it. Returns a field-level error on any
// structural/type failure (ok == true on success).
ProtocolCheckResult seqJsonParse(const char* json,
                                 SeqStep* stepBuf, uint8_t stepCap,
                                 SeqStep* closeBuf, uint8_t closeCap,
                                 SeqDraft& out);

// Same, from an already-deserialized JSON root — lets the runtime store parse
// straight from a LittleFS File stream (deserializeJson(doc, file)) without a
// large intermediate text buffer.
ProtocolCheckResult seqJsonParseVariant(JsonVariantConst root,
                                        SeqStep* stepBuf, uint8_t stepCap,
                                        SeqStep* closeBuf, uint8_t closeCap,
                                        SeqDraft& out);

// Serialize an entry into an existing JsonObject (for building arrays such as
// GET /api/seq/builtins without an intermediate text buffer).
void seqJsonSerializeObject(JsonObject obj, const SequenceEntry& entry,
                            const char* source);

// Serialize an entry to JSON v1 text. `source` populates meta.source
// ("factory" for builtins, "user"/"guild" for runtime). Returns the number of
// bytes written (excluding NUL), or 0 on buffer overflow.
size_t seqJsonSerialize(const SequenceEntry& entry, const char* source,
                        char* outBuf, size_t outCap);
