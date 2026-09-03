// =============================================================================
// src/web/api_upload.cpp
//
// Streaming OTA upload endpoints, ported to the WebRequest seam (ADR 0021):
//   POST /upload/firmware    - firmware image (U_FLASH)
//   POST /upload/filesystem  - LittleFS image (U_SPIFFS)
//   POST /upload/wifi-module - WiFi Module image (PA_CAP_HOSTED_WIFI)
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

#if PA_CAP_HOSTED_WIFI
#include "esp32-hal-hosted.h"
#include "wifi_module_status.h"
#include "wifi_module_update_support.h"
#endif

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
            PA_LOG_WARN(TAG, "%s upload rejected: %u bytes exceeds the %u byte partition", label,
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

    // A begun update that did not finalize must be torn down before the next
    // upload: Update is a global singleton, and begin() against a transaction a
    // failed upload left open refuses until the device reboots. Covers every
    // failure path at once -- write failure, end() failure, and a body the
    // parser abandoned after begin() -- and is a no-op when begin() never ran
    // or the failure path already aborted.
    if (effective != UploadOutcome::kComplete && Update.isRunning()) {
        Update.abort();
    }

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

#if PA_CAP_HOSTED_WIFI

// WiFi Module Update (#241). Streams the body into hostedWriteUpdate() the
// same way firmware streams into Update.write() -- never accumulated in heap.
//
// hostedDeinitWiFi() is NOT used as a pause: when BLE is inactive it calls
// hostedDeinit() and tears down the SDIO stack (esp32-hal-hosted.c), which
// would drop both this HTTP upload and the OTA RPCs. The radio traffic for
// the transfer is the upload itself. Activate runs after the HTTP response
// so a module reboot cannot swallow the 200. A failed begin/write/end does
// not call activate, so the running slot stays selected.
//
// The controller is not restarted (ADR 0032).
//
// Bin source order (#241): (a) this uploaded file -- implemented;
// (b) HTTP(S) to a protoArtoo release asset -- UNKNOWN, no such URL in
// the tree; (c) hostedGetUpdateURL() builds
// https://espressif.github.io/arduino-esp32/hosted/<target>-v2.12.11.bin
// (read from the HAL, not fetched here). No LittleFS recovery partition.

enum class WifiModuleUploadOutcome : uint8_t {
    kInProgress,
    kGated,
    kFailedBegin,
    kFailedWrite,
    kFailedEnd,
    kNoImage,
    kBodyNotParsed,
    kComplete,
};

struct WifiModuleUploadSession {
    WifiModuleUploadOutcome outcome = WifiModuleUploadOutcome::kInProgress;
    WifiModuleUploadDecision gateDecision = WifiModuleUploadDecision::Allow;
    size_t bytesWritten = 0;
    uint32_t minHeapFree = 0;
    uint32_t startMs = 0;
    bool sawChunk = false;
    bool began = false;
};

static WifiModuleUploadSession s_wifiModuleSession = {};

static const char* wifiModuleGateErrorJson(WifiModuleUploadDecision decision) {
    switch (decision) {
        case WifiModuleUploadDecision::LinkNotReady:
            return "{\"ok\":false,\"error\":\"wifi-module-link-not-ready\"}";
        case WifiModuleUploadDecision::Unknown:
            return "{\"ok\":false,\"error\":\"wifi-module-unknown\"}";
        case WifiModuleUploadDecision::NotSupported:
            return "{\"ok\":false,\"error\":\"wifi-module-not-supported\"}";
        case WifiModuleUploadDecision::AlreadyCurrent:
            return "{\"ok\":false,\"error\":\"wifi-module-already-current\"}";
        case WifiModuleUploadDecision::Allow:
            break;
    }
    return "{\"ok\":false,\"error\":\"wifi-module-unknown\"}";
}

static const char* wifiModuleTransferErrorJson(WifiModuleUploadOutcome outcome) {
    switch (outcome) {
        case WifiModuleUploadOutcome::kFailedBegin:
            return "{\"ok\":false,\"error\":\"wifi-module-begin-failed\"}";
        case WifiModuleUploadOutcome::kFailedWrite:
            return "{\"ok\":false,\"error\":\"wifi-module-write-failed\"}";
        case WifiModuleUploadOutcome::kFailedEnd:
            return "{\"ok\":false,\"error\":\"wifi-module-end-failed\"}";
        case WifiModuleUploadOutcome::kNoImage:
            return "{\"ok\":false,\"error\":\"no image received\"}";
        case WifiModuleUploadOutcome::kBodyNotParsed:
            return "{\"ok\":false,\"error\":\"the controller could not read the upload "
                   "body; no image data reached the updater. retry\"}";
        default:
            return "{\"ok\":false,\"error\":\"wifi-module-update-failed\"}";
    }
}

void handleWifiModuleUploadChunk(WebRequest& req, const char* filename, size_t index,
                                 const uint8_t* data, size_t len, bool final) {
    (void)filename;
    s_wifiModuleSession.sawChunk = true;

    if (index == 0) {
        s_wifiModuleSession.outcome = WifiModuleUploadOutcome::kInProgress;
        s_wifiModuleSession.bytesWritten = 0;
        s_wifiModuleSession.began = false;
        s_wifiModuleSession.minHeapFree = (uint32_t)ESP.getFreeHeap();
        s_wifiModuleSession.startMs = millis();

        const WifiModuleStatusSnapshot snap = wifiModuleQueryUpdateSupport();
        WifiModuleUploadGateInput gateIn{};
        gateIn.linkReady = snap.linkReady;
        gateIn.support = snap.classification.support;
        gateIn.versionPresent = snap.classification.versionPresent;
        gateIn.versionMajor = snap.classification.versionMajor;
        gateIn.versionMinor = snap.classification.versionMinor;
        gateIn.versionPatch = snap.classification.versionPatch;
        gateIn.hostMajor = snap.hostMajor;
        gateIn.hostMinor = snap.hostMinor;
        gateIn.hostPatch = snap.hostPatch;

        const WifiModuleUploadDecision decision = wifiModuleClassifyUploadGate(gateIn);
        if (decision != WifiModuleUploadDecision::Allow) {
            s_wifiModuleSession.outcome = WifiModuleUploadOutcome::kGated;
            s_wifiModuleSession.gateDecision = decision;
            return;
        }

        if (!hostedBeginUpdate()) {
            s_wifiModuleSession.outcome = WifiModuleUploadOutcome::kFailedBegin;
            return;
        }
        s_wifiModuleSession.began = true;
    }

    if (s_wifiModuleSession.outcome != WifiModuleUploadOutcome::kInProgress) {
        return;
    }

    if (len > 0) {
        if (!hostedWriteUpdate(const_cast<uint8_t*>(data), (uint32_t)len)) {
            s_wifiModuleSession.outcome = WifiModuleUploadOutcome::kFailedWrite;
            return;
        }
        s_wifiModuleSession.bytesWritten += len;
        const uint32_t freeHeap = (uint32_t)ESP.getFreeHeap();
        if (freeHeap < s_wifiModuleSession.minHeapFree) {
            s_wifiModuleSession.minHeapFree = freeHeap;
        }
    }

    if (final) {
        if (s_wifiModuleSession.bytesWritten == 0) {
            s_wifiModuleSession.outcome = WifiModuleUploadOutcome::kNoImage;
            return;
        }
        if (!hostedEndUpdate()) {
            s_wifiModuleSession.outcome = WifiModuleUploadOutcome::kFailedEnd;
            return;
        }
        s_wifiModuleSession.outcome = WifiModuleUploadOutcome::kComplete;
        PA_LOG_INFO(TAG, "WiFi Module upload complete: %u bytes in %u ms, min free heap %u",
                    (unsigned)s_wifiModuleSession.bytesWritten,
                    (unsigned)(millis() - s_wifiModuleSession.startMs),
                    (unsigned)s_wifiModuleSession.minHeapFree);
    }
}

void handleWifiModuleUploadDone(WebRequest& req) {
    WifiModuleUploadOutcome effective = s_wifiModuleSession.outcome;
    if (effective == WifiModuleUploadOutcome::kInProgress) {
        if (!s_wifiModuleSession.sawChunk && req.contentLength() > 0) {
            effective = WifiModuleUploadOutcome::kBodyNotParsed;
        } else {
            effective = WifiModuleUploadOutcome::kNoImage;
        }
    }

    const bool complete = (effective == WifiModuleUploadOutcome::kComplete);
    const size_t bytesWritten = s_wifiModuleSession.bytesWritten;
    const uint32_t minHeapFree = s_wifiModuleSession.minHeapFree;
    const uint32_t durationMs = millis() - s_wifiModuleSession.startMs;
    const WifiModuleUploadDecision gateDecision = s_wifiModuleSession.gateDecision;

    s_wifiModuleSession = WifiModuleUploadSession{};

    if (!complete) {
        if (effective == WifiModuleUploadOutcome::kGated) {
            PA_LOG_ERROR(TAG, "POST /upload/wifi-module gated: %s",
                         wifiModuleUploadGateErrorToken(gateDecision));
            req.send(409, "application/json", wifiModuleGateErrorJson(gateDecision));
            return;
        }
        const int code = (effective == WifiModuleUploadOutcome::kNoImage) ? 400
                         : (effective == WifiModuleUploadOutcome::kBodyNotParsed) ? 503
                                                                                  : 500;
        PA_LOG_ERROR(TAG, "POST /upload/wifi-module failed with %d", code);
        req.send(code, "application/json", wifiModuleTransferErrorJson(effective));
        return;
    }

    char body[96] = {};
    if (!formatUploadSuccessJson(body, sizeof(body), bytesWritten, minHeapFree, durationMs)) {
        PA_LOG_ERROR(TAG, "POST /upload/wifi-module response overflow");
        req.send(500, "application/json", "{\"ok\":false,\"error\":\"wifi-module-update-failed\"}");
        return;
    }

    PA_LOG_INFO(TAG, "[WEB] POST /upload/wifi-module - update written, activating module");
    req.send(200, "application/json", body);
    // Activate after the response so a module reboot cannot swallow the 200.
    // Failure here leaves the new image in the inactive slot; the running
    // slot stays selected (fail closed).
    if (!hostedActivateUpdate()) {
        PA_LOG_ERROR(TAG, "WiFi Module activate failed after a successful write");
    }
}

#else  // !PA_CAP_HOSTED_WIFI

void handleWifiModuleUploadChunk(WebRequest& req, const char* filename, size_t index,
                                 const uint8_t* data, size_t len, bool final) {
    (void)req;
    (void)filename;
    (void)index;
    (void)data;
    (void)len;
    (void)final;
}

void handleWifiModuleUploadDone(WebRequest& req) {
    req.send(404, "application/json", "{\"ok\":false,\"error\":\"not on this board\"}");
}

#endif  // PA_CAP_HOSTED_WIFI
