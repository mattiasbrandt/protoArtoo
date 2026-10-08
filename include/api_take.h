// =============================================================================
// include/api_take.h
//
// Takes: performing on the sticks and keeping it (#442, ADR 0061), on the
// project-owned WebRequest seam (ADR 0021).
//
//   GET  /api/take          - the take in hand, and the store's figures
//   POST /api/take/arm      - arm a take for a saved sequence {seq}
//   POST /api/take/keep     - stop the take and keep it; answers the receipt
//   GET  /api/take/file     - one take file, as stored ?owner=&take=
//   POST /api/take/file     - put one back (a backup's restore), as an upload
//
// None asks for Non-RC Control consent: a take is RC motion (ADR 0064).
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "web_request.h"

void handleTakeGet(WebRequest& req);
void handleTakeArmPost(WebRequest& req);
void handleTakeKeepPost(WebRequest& req);
void handleTakeFileGet(WebRequest& req);
void handleTakeFileUploadChunk(WebRequest& req, const char* filename, size_t index,
                               const uint8_t* data, size_t len, bool final);
void handleTakeFileUploadDone(WebRequest& req);
