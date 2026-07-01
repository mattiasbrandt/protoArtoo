// =============================================================================
// include/api_seq.h
//
// Learned Sequence REST API (issue #2 slice 3d, ADR 0006).
//   GET    /api/seq/list       — index of Learned Sequences
//   GET    /api/seq?name=      — raw JSON of one Learned Sequence
//   POST   /api/seq            — validate (Protocol Check) + persist
//   DELETE /api/seq?name=      — Memory Wipe
//   POST   /api/seq/test       — run a sequence by name (same path as dome/cmd)
//   POST   /api/seq/stop       — abort current sequence (non-latching, idempotent)
//   GET    /api/seq/builtins   — factory catalog serialized to JSON v1
// =============================================================================
#pragma once

#include <ESPAsyncWebServer.h>

void registerSeqRoutes(AsyncWebServer& server);
