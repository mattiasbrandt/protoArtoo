// =============================================================================
// src/web/api_admission_trace.cpp
//
// GET /api/admission/trace -- the admission decision ring, served through
// WebRequest::sendChunked() (ADR 0021) so a whole page load's profile costs one
// chunk buffer rather than a buffer sized for the ring.
// =============================================================================

#include "../../include/api_admission_trace.h"

#if PA_ADMISSION_TRACE

#include "../../include/logging.h"

static const char* TAG = "WebServer";

namespace {

// File scope, zero-initialised, which is already the empty ring: total and the
// write cursor both start at 0 and no entry is read before it is written.
WebAdmissionTrace s_trace;

size_t fillAdmissionTraceResponse(uint8_t* out, size_t capacity, size_t offset) {
    JsonSliceWriter writer(out, capacity, offset);
    webAdmissionTraceWrite(&s_trace, writer);
    return writer.written();
}

}  // namespace

WebAdmissionTrace* webAdmissionTraceInstance() {
    return &s_trace;
}

void handleAdmissionTraceGet(WebRequest& req) {
    // The slice writer re-walks the whole body once per chunk, so the ring has
    // to be stable for the duration of the send. It is: every write to it comes
    // from an admission callback on the single server task, which is the task
    // running this handler, so no decision can land mid-body. Same argument
    // src/web/api_dome.cpp makes for its layout cache, without needing that
    // one's generation pin because there is no background writer here.
    const bool clear = req.hasParam("clear");

    if (!req.sendChunked("application/json", fillAdmissionTraceResponse)) {
        req.send(503, "application/json", "{\"error\":\"admission trace unavailable\"}");
        return;
    }

    // After the send, never before: clearing first would serve an empty profile
    // and discard the run that asked for it. A read that fails therefore leaves
    // the ring intact, which is the right way round for evidence.
    if (clear) {
        webAdmissionTraceInit(&s_trace);
        PA_LOG_DEBUG(TAG, "admission trace cleared");
    }
}

#endif  // PA_ADMISSION_TRACE
