// =============================================================================
// include/api_seq.h
//
// Learned Sequence REST API (ADR 0006), ported to the
// project-owned WebRequest seam (ADR 0021) and bound by the seam route table.
//
//   GET    /api/seq/list       — index of Learned Sequences
//   GET    /api/seq?name=      — raw JSON of one Learned Sequence
//   POST   /api/seq            — validate (Protocol Check) + persist
//   DELETE /api/seq?name=      — Memory Wipe
//   POST   /api/seq/test       — run a sequence by name (same path as dome/cmd)
//   POST   /api/seq/stop       — abort current sequence (non-latching, idempotent)
//   GET    /api/seq/builtins   — factory catalog serialized to JSON v1
//   GET    /api/seq/last-run   — machine-readable evidence of the last run
// =============================================================================
#pragma once

#include "web_request.h"

void handleSeqListGet(WebRequest& req);
void handleSeqBuiltinsGet(WebRequest& req);
void handleSeqGet(WebRequest& req);
void handleSeqPost(WebRequest& req);
void handleSeqDelete(WebRequest& req);
void handleSeqTestPost(WebRequest& req);
void handleSeqStopPost(WebRequest& req);
void handleSeqLastRunGet(WebRequest& req);
