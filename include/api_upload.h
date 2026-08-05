// =============================================================================
// include/api_upload.h
//
// Streaming OTA upload endpoints, written against the project-owned WebRequest
// seam (ADR 0021) and bound by the seam route table:
//   POST /upload/firmware    — firmware image (U_FLASH)
//   POST /upload/filesystem  — LittleFS image (U_SPIFFS)
//
// The size guard and the outcome-to-response mapping live here as pure
// functions so the host tests can drive them; the handlers themselves are
// unavoidably about Update, which only exists on the device.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "web_request.h"

// Which flash partition an upload is destined for.
enum class UploadTarget : uint8_t { kFirmware, kFilesystem };

// What happened to an upload, recorded during the streamed body and answered
// once the body has been consumed.
enum class UploadOutcome : uint8_t {
    kInProgress,       // no problem seen yet, and not finished either
    kRejectedOversize, // cannot fit the destination partition
    kFailed,           // Update refused to begin, write, or finalize
    kNoImage,          // the request carried no image to write
    kComplete,         // written and finalized
};

// Multipart framing -- boundary lines, the Content-Disposition part header, the
// closing boundary -- that Content-Length counts but the image does not.
// Around 200-300 bytes for the dashboard's upload form; 4 KB is deliberate
// slack so framing overhead alone can never reject an image that genuinely
// fits its partition.
static constexpr size_t kUploadMultipartOverheadAllowance = 4096;

// Ceiling handed to the backend's own transport-level upload limit. It exists
// only to sit above every partition on this board so the per-target guard
// below is what rejects an oversize image, in the JSON shape the dashboard
// parses, rather than the backend's generic error page.
static constexpr size_t kUploadTransportCeiling = 4 * 1024 * 1024;

// Would a multipart request of this Content-Length fit the destination
// partition?
//
// This is a fail-fast check, not the last line of defence: the real bound is
// the flash write itself, which fails once it runs past the partition end. Its
// value is that it fails at the first chunk with a clear message instead of
// after the operator has waited out a whole upload. Content-Length is compared
// rather than image size because the image size is not knowable until the
// multipart body has been fully processed.
inline bool uploadContentLengthFits(size_t contentLength, size_t partitionSize) {
    if (partitionSize == 0) {
        // Partition lookup failed. Refusing every upload would strand the
        // device with no way back except a physical reflash, so let it through
        // and leave the write to enforce the real bound.
        return true;
    }
    return contentLength <= partitionSize + kUploadMultipartOverheadAllowance;
}

// The response a failed upload earns. Bodies are JSON because
// data/firmware.js reads its operator-facing message out of the "error" field.
// Only defined for the failure outcomes; success is formatted below, since it
// carries measurements a constant cannot.
struct UploadResponse {
    int code;
    const char* body;
};

inline UploadResponse uploadFailureResponse(UploadTarget target, UploadOutcome outcome) {
    if (outcome == UploadOutcome::kNoImage) {
        // A POST that delivered no image must not read as a successful flash.
        // It reached this endpoint, so answering 200 would reboot the
        // controller to re-run the image it is already running -- an outage
        // with nothing to show for it.
        return UploadResponse{400, "{\"ok\":false,\"error\":\"no image received\"}"};
    }
    if (outcome == UploadOutcome::kRejectedOversize) {
        return target == UploadTarget::kFirmware
                   ? UploadResponse{413,
                                    "{\"ok\":false,\"error\":\"firmware image is larger than the "
                                    "app partition\"}"}
                   : UploadResponse{413,
                                    "{\"ok\":false,\"error\":\"filesystem image is larger than the "
                                    "filesystem partition\"}"};
    }
    return target == UploadTarget::kFirmware
               ? UploadResponse{500, "{\"ok\":false,\"error\":\"update failed\"}"}
               : UploadResponse{500, "{\"ok\":false,\"error\":\"filesystem update failed\"}"};
}

// Success body, carrying the transfer's own evidence: bytes written, the
// smallest free heap seen across the upload, and how long it took.
//
// These travel in the response because the device reboots a second later and
// takes the in-memory log ring with it -- and because polling /api/status from
// the host during a transfer samples far too coarsely to catch a transient
// dip. This is the only report that sees every chunk. Returns false if the
// body would not fit, which the caller must treat as a failed response rather
// than sending a truncated one.
inline bool formatUploadSuccessJson(char* out, size_t outSize, size_t bytesWritten,
                                    uint32_t minHeapFree, uint32_t durationMs) {
    if (out == nullptr || outSize == 0) {
        return false;
    }
    const int written =
        snprintf(out, outSize,
                 "{\"ok\":true,\"bytes\":%u,\"minHeapFree\":%u,\"durationMs\":%u}",
                 (unsigned)bytesWritten, (unsigned)minHeapFree, (unsigned)durationMs);
    return written > 0 && (size_t)written < outSize;
}

void handleFirmwareUploadChunk(WebRequest& req, const char* filename, size_t index,
                               const uint8_t* data, size_t len, bool final);
void handleFirmwareUploadDone(WebRequest& req);

void handleFilesystemUploadChunk(WebRequest& req, const char* filename, size_t index,
                                 const uint8_t* data, size_t len, bool final);
void handleFilesystemUploadDone(WebRequest& req);
