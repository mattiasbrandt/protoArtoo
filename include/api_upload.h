// =============================================================================
// include/api_upload.h
//
// Streaming OTA upload endpoints, written against the project-owned WebRequest
// seam (ADR 0021) and bound by the seam route table:
//   POST /upload/firmware    - firmware image (U_FLASH)
//   POST /upload/filesystem  - LittleFS image (U_SPIFFS)
//   POST /upload/wifi-module - WiFi Module image (hosted boards only)
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
    kBodyNotParsed,    // a body arrived, and none of it reached the updater
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

// What the client is actually told, from what the streamed body recorded.
//
// Separate from the handler because the handler is unavoidably about Update and
// only exists on the device, while this is the decision -- and a decision that
// is only reachable through a flash write is a decision nothing can test
// (ADR 0011).
//
//   recorded         what the chunk handler last wrote down
//   sawChunk         whether the backend handed over a single chunk at all
//   contentLength    the request's Content-Length
//   updaterHasError  Update.hasError() once the body has been consumed
//
// The kInProgress case is the interesting one: the body has been consumed and
// nothing finalized. Splitting it on sawChunk separates two failures that used
// to answer identically. "No image received" is true of an empty POST, and it is
// what an operator can act on. It is a lie when a 1.5 MB image was transferred
// and the backend's multipart parser dropped it on the floor without saying so
// -- the parser gives up silently on an allocation failure, so the request looks
// from here exactly like one that carried nothing. That misreport is what made
// this cost an evening; naming it separately is what stops the next one.
inline UploadOutcome uploadEffectiveOutcome(UploadOutcome recorded, bool sawChunk,
                                            size_t contentLength, bool updaterHasError) {
    if (recorded == UploadOutcome::kComplete) {
        // Consulted even on the success path: an error the library latched
        // without failing a call the handler checked would otherwise be
        // reported to the operator as a successful flash.
        return updaterHasError ? UploadOutcome::kFailed : UploadOutcome::kComplete;
    }
    if (recorded != UploadOutcome::kInProgress) {
        // Already decided during the body: oversize, or a failed write.
        return recorded;
    }
    if (!sawChunk && contentLength > 0) {
        return UploadOutcome::kBodyNotParsed;
    }
    return UploadOutcome::kNoImage;
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
    if (outcome == UploadOutcome::kBodyNotParsed) {
        // 503 rather than 400: the body was transferred in full and the
        // controller failed to read it, so this is the controller's fault and
        // retrying is the right thing for the operator to do. A malformed body
        // would land here too and earn a slightly generous 503, which is the
        // cheaper mistake -- the dashboard is the only client that builds these
        // requests, and it always builds them well-formed.
        return UploadResponse{503,
                              "{\"ok\":false,\"error\":\"the controller could not read the upload "
                              "body; no image data reached the updater. retry\"}"};
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

// Hosted boards only. The seam registers this route under PA_CAP_HOSTED_WIFI;
// artoo-esp32 does not register it and answers the generic 404.
void handleWifiModuleUploadChunk(WebRequest& req, const char* filename, size_t index,
                                 const uint8_t* data, size_t len, bool final);
void handleWifiModuleUploadDone(WebRequest& req);
