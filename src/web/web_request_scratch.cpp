// =============================================================================
// src/web/web_request_scratch.cpp
//
// The web request scratch's store and its claim check
// (include/web_request_scratch.h).
// =============================================================================

#include "web_request_scratch.h"

#include "logging.h"

static const char* TAG = "WebScratch";

namespace {

// Static, not heap: it is claimed on every request that uses it, and a
// per-request allocation of the largest handler's buffer on a fragmenting
// heap is the failure this store exists to make cheaper, not more likely.
alignas(WebRequestScratchLayout) unsigned char s_store[sizeof(WebRequestScratchLayout)];

// Written once, when the server task exists; read on every claim. Only the
// owner ever reads s_held or writes it, so it needs no lock.
volatile TaskHandle_t s_owner = nullptr;
bool s_held = false;

}  // namespace

void webRequestScratchBindOwner(TaskHandle_t owner) {
    s_owner = owner;
    s_held = false;
}

void* webRequestScratchClaim() {
    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (s_owner == nullptr || caller != s_owner) {
        PA_LOG_ERROR(TAG, "claim refused: the caller is not the web server task");
        return nullptr;
    }
    if (s_held) {
        PA_LOG_ERROR(TAG, "claim refused: another handler holds the scratch");
        return nullptr;
    }
    s_held = true;
    return s_store;
}

void webRequestScratchRelease() {
    s_held = false;
}
