// =============================================================================
// src/web/api_upload.cpp
//
// Streaming OTA upload endpoints, ported to the WebRequest seam (ADR 0021):
//   POST /upload/firmware    — firmware image (U_FLASH)
//   POST /upload/filesystem  — LittleFS image (U_SPIFFS)
//
// Split out of api_system.cpp when ported. These land early in the migration
// on purpose: without a working HTTP upload path, later work needs a physical
// serial reflash rather than an over-the-air one.
//
// The body is written to flash chunk by chunk as it arrives, so the image is
// never accumulated in heap -- only whatever the backend hands over for the
// current chunk is ever in memory. Peak heap pressure across an upload is
// recorded per session and logged on completion, because the alternative
// (polling /api/status from the host during the transfer) samples too coarsely
// to catch a transient dip.
// =============================================================================

#include "api_upload.h"

#include <Arduino.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "logging.h"
#include "web_server.h"

static const char* TAG = "Upload";

namespace {

// One in-flight upload's state. File-scope rather than per-request, which adds
// no constraint that is not already there: Update is a global singleton, so two
// concurrent uploads are impossible however this state is stored.
struct UploadSession {
    UploadOutcome outcome;
    size_t bytesWritten;
    uint32_t minHeapFree;  // smallest free heap seen across the transfer
    uint32_t startMs;
    // Whether the backend handed over a single chunk for this request. Not
    // derivable from the fields above: a request whose body the multipart
    // parser abandoned leaves every one of them untouched, which is also what
    // an empty POST leaves. Kept separately so the two can be told apart.
    bool sawChunk;
};

UploadSession s_firmwareSession = {};
UploadSession s_filesystemSession = {};

size_t firmwarePartitionSize() {
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    return part != nullptr ? (size_t)part->size : 0;
}

size_t filesystemPartitionSize() {
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    return part != nullptr ? (size_t)part->size : 0;
}

void beginSession(UploadSession& session) {
    session.outcome = UploadOutcome::kInProgress;
    session.bytesWritten = 0;
    session.minHeapFree = (uint32_t)ESP.getFreeHeap();
    session.startMs = millis();
}

void observeHeap(UploadSession& session) {
    const uint32_t freeHeap = (uint32_t)ESP.getFreeHeap();
    if (freeHeap < session.minHeapFree) {
        session.minHeapFree = freeHeap;
    }
}

// Shared body of both chunk handlers. The only per-target differences are the
// Update command, the partition the image has to fit, and what it is called in
// the log.
void handleUploadChunk(UploadSession& session, UploadTarget target, int updateCommand,
                       size_t partitionSize, const char* label, WebRequest& req,
                       const char* filename, size_t index, const uint8_t* data, size_t len,
                       bool final) {
    session.sawChunk = true;

    if (index == 0) {
        beginSession(session);

        const size_t contentLength = req.contentLength();
        if (!uploadContentLengthFits(contentLength, partitionSize)) {
            PA_LOG_ERROR(TAG, "%s upload rejected: %u bytes exceeds the %u byte partition", label,
                         (unsigned)contentLength, (unsigned)partitionSize);
            session.outcome = UploadOutcome::kRejectedOversize;
            return;
        }

        PA_LOG_INFO(TAG, "OTA %s upload started: %s (%u bytes incl. multipart framing)", label,
                    filename != nullptr ? filename : "", (unsigned)contentLength);

        // UPDATE_SIZE_UNKNOWN, not the content length: the content length is
        // the whole multipart body including framing, and handing that to
        // Update.begin() makes Update.end() fail its size check and silently
        // roll back to the previous image.
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, updateCommand)) {
            Update.printError(Serial);
            session.outcome = UploadOutcome::kFailed;
            return;
        }
    }

    // A rejected upload still has its body drained: the transfer is already in
    // flight and the client is answered once, from the completion handler.
    if (session.outcome != UploadOutcome::kInProgress) {
        return;
    }

    if (len > 0) {
        // const_cast because Update.write() takes a non-const pointer even
        // though it only reads the buffer. The seam keeps the chunk const so a
        // handler cannot scribble on a buffer the backend still owns.
        if (Update.write(const_cast<uint8_t*>(data), len) != len) {
            Update.printError(Serial);
            session.outcome = UploadOutcome::kFailed;
            return;
        }
        session.bytesWritten += len;
        observeHeap(session);
    }

    if (final) {
        if (session.bytesWritten == 0) {
            Update.abort();
            session.outcome = UploadOutcome::kNoImage;
            return;
        }
        if (!Update.end(true)) {
            Update.printError(Serial);
            session.outcome = UploadOutcome::kFailed;
            return;
        }
        session.outcome = UploadOutcome::kComplete;
        PA_LOG_INFO(TAG, "OTA %s upload complete: %u bytes in %u ms, min free heap %u", label,
                    (unsigned)session.bytesWritten, (unsigned)(millis() - session.startMs),
                    (unsigned)session.minHeapFree);
    }
}

// Shared body of both completion handlers.
void handleUploadDone(UploadSession& session, UploadTarget target, const char* label,
                      WebRequest& req) {
    // Only an upload that actually wrote and finalized an image counts as
    // success. Treating "nothing went wrong" as success made an empty POST to
    // this endpoint answer 200 and reboot the controller -- proven on the
    // device before this check existed.
    const UploadOutcome effective = uploadEffectiveOutcome(
        session.outcome, session.sawChunk, req.contentLength(), Update.hasError());

    // Rearm here rather than at the start of the next upload: the chunk handler
    // is the only thing that runs at the start of one, and the case that has to
    // be detected is precisely the one where it never runs at all.
    session.outcome = UploadOutcome::kInProgress;
    session.sawChunk = false;

    if (effective == UploadOutcome::kBodyNotParsed) {
        // The backend consumed a whole body and handed over nothing. Its
        // multipart parser abandons a part silently when it cannot allocate its
        // buffer, so what an investigation needs is the heap at this instant --
        // and the device is about to answer and drop the request that produced
        // it. Recorded here because nothing else in the system sees this moment.
        PA_LOG_ERROR(TAG,
                     "%s upload: %u byte body consumed, no chunk delivered; "
                     "free heap %u, largest block %u",
                     label, (unsigned)req.contentLength(), (unsigned)ESP.getFreeHeap(),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }

    if (effective != UploadOutcome::kComplete) {
        const UploadResponse response = uploadFailureResponse(target, effective);
        PA_LOG_ERROR(TAG, "POST /upload/%s failed with %d", label, response.code);
        req.send(response.code, "application/json", response.body);
        return;
    }

    char body[96] = {};
    if (!formatUploadSuccessJson(body, sizeof(body), session.bytesWritten, session.minHeapFree,
                                 millis() - session.startMs)) {
        // Never reboot on the strength of a response we could not build: an
        // operator who sees an error has a device still running the old image,
        // which is the recoverable half of the two outcomes.
        const UploadResponse response = uploadFailureResponse(target, UploadOutcome::kFailed);
        PA_LOG_ERROR(TAG, "POST /upload/%s response overflow", label);
        req.send(response.code, "application/json", response.body);
        return;
    }

    PA_LOG_INFO(TAG, "[WEB] POST /upload/%s - update complete, reboot scheduled", label);
    req.send(200, "application/json", body);
    requestSystemRestart(1000);
}

}  // namespace

void handleFirmwareUploadChunk(WebRequest& req, const char* filename, size_t index,
                               const uint8_t* data, size_t len, bool final) {
    handleUploadChunk(s_firmwareSession, UploadTarget::kFirmware, U_FLASH, firmwarePartitionSize(),
                      "firmware", req, filename, index, data, len, final);
}

void handleFirmwareUploadDone(WebRequest& req) {
    handleUploadDone(s_firmwareSession, UploadTarget::kFirmware, "firmware", req);
}

// U_SPIFFS targets the spiffs/littlefs partition; the Update library unmounts
// LittleFS for the duration of the write.
void handleFilesystemUploadChunk(WebRequest& req, const char* filename, size_t index,
                                 const uint8_t* data, size_t len, bool final) {
    handleUploadChunk(s_filesystemSession, UploadTarget::kFilesystem, U_SPIFFS,
                      filesystemPartitionSize(), "filesystem", req, filename, index, data, len,
                      final);
}

void handleFilesystemUploadDone(WebRequest& req) {
    handleUploadDone(s_filesystemSession, UploadTarget::kFilesystem, "filesystem", req);
}
