// =============================================================================
// src/web/web_admission.cpp
//
// Decision core for Connection Admission and request admission, plus the
// counters both layers publish. No Arduino, no vendor type, no clock of its
// own: every input arrives as a parameter, so this whole file compiles and is
// exercised on the host. The device hookup lives with the backend that owns
// the sockets (src/web/web_request_psychic.cpp).
//
// See include/web_admission.h for the two-layer split and why the estop bypass
// can only exist at the request layer.
// =============================================================================

#include "../../include/web_admission.h"

#include <string.h>

namespace {

// One token, in the milli-token units the bucket counts in.
constexpr uint32_t kTokenMilli = 1000u;

// Refill for time elapsed since the last call, clamped to the burst size, and
// stamp the refill time. Unsigned subtraction makes the elapsed interval
// correct across the millisecond counter's 32-bit rollover; a signed or
// widened subtraction would read a rollover as roughly 49 days of credit.
void refill(WebAcceptRateLimiter* limiter, uint32_t nowMs, uint32_t burst, uint32_t perSecond) {
    const uint32_t elapsedMs = nowMs - limiter->lastRefillMs;
    limiter->lastRefillMs = nowMs;

    const uint32_t capMilli = burst * kTokenMilli;
    const uint64_t refilled =
        (uint64_t)limiter->tokensMilli + (uint64_t)elapsedMs * (uint64_t)perSecond;
    limiter->tokensMilli = refilled > capMilli ? capMilli : (uint32_t)refilled;
}

// True when path is exactly prefix, or prefix followed by a path separator.
// Substring matching would make /api/estopper a safety path.
bool pathMatches(const char* path, const char* prefix) {
    if (path == nullptr) {
        return false;
    }
    const size_t prefixLen = strlen(prefix);
    if (strncmp(path, prefix, prefixLen) != 0) {
        return false;
    }
    return path[prefixLen] == '\0' || path[prefixLen] == '/';
}

}  // namespace

// -----------------------------------------------------------------------------
// Counters
// -----------------------------------------------------------------------------

volatile uint32_t g_webAcceptRejectHeap = 0;
volatile uint32_t g_webAcceptRejectRate = 0;
volatile uint32_t g_webAcceptRejectLastMs = 0;
volatile uint32_t g_webAcceptRejectLargestBlock = 0;
volatile uint32_t g_webAcceptMinLargestBlockSeen = UINT32_MAX;

volatile uint32_t g_webAcceptGuardLastUs = 0;
volatile uint32_t g_webAcceptGuardMaxUs = 0;

volatile int g_webInflightRequests = 0;
volatile int g_webInflightRequestsPeak = 0;
volatile uint32_t g_webRefusedInflightCap = 0;
volatile uint32_t g_webRefusedHeapFloor = 0;
volatile uint32_t g_webRefusedHeapFloorDiag = 0;

// -----------------------------------------------------------------------------
// Connection Admission
// -----------------------------------------------------------------------------

void webAcceptRateLimiterInit(WebAcceptRateLimiter* limiter, uint32_t nowMs, uint32_t burst) {
    limiter->tokensMilli = burst * kTokenMilli;
    limiter->lastRefillMs = nowMs;
}

bool webAcceptRateLimiterTake(WebAcceptRateLimiter* limiter, uint32_t nowMs, uint32_t burst,
                              uint32_t perSecond) {
    refill(limiter, nowMs, burst, perSecond);
    if (limiter->tokensMilli < kTokenMilli) {
        return false;
    }
    limiter->tokensMilli -= kTokenMilli;
    return true;
}

WebAcceptDecision webAcceptDecide(WebAcceptRateLimiter* limiter, uint32_t nowMs, uint32_t burst,
                                  uint32_t perSecond, WebHeapSampler sampler, void* samplerCtx,
                                  size_t minLargestFreeBlock) {
    refill(limiter, nowMs, burst, perSecond);

    // Rate first, and the sampler is not called yet: this check is arithmetic
    // on state already in cache, whereas sampling the heap may walk it. A
    // connection that is going to be paced out must never trigger that walk.
    if (limiter->tokensMilli < kTokenMilli) {
        return WebAcceptDecision::kRejectRate;
    }

    // A connection refused for heap was never admitted, so it does not spend
    // admission budget. Charging it would let a heap-pressure window pace out
    // the connections that arrive after heap recovers.
    if (sampler(samplerCtx) < minLargestFreeBlock) {
        return WebAcceptDecision::kRejectHeap;
    }

    limiter->tokensMilli -= kTokenMilli;
    return WebAcceptDecision::kAdmit;
}

// -----------------------------------------------------------------------------
// Cached heap sample
// -----------------------------------------------------------------------------

bool webHeapSampleDue(const WebHeapSampleCache* cache, uint32_t nowMs, uint32_t minIntervalMs) {
    if (!cache->primed) {
        return true;
    }
    return (uint32_t)(nowMs - cache->lastSampleMs) >= minIntervalMs;
}

void webHeapSampleStore(WebHeapSampleCache* cache, uint32_t nowMs, size_t value) {
    cache->value = value;
    cache->lastSampleMs = nowMs;
    cache->primed = true;
}

// -----------------------------------------------------------------------------
// Request admission
// -----------------------------------------------------------------------------

WebRequestAdmission webRequestAdmissionDecide(const WebRequestAdmissionInputs& in) {
    // Estop is the safety path: never rejected, never counted. Shedding it to
    // protect memory would trade the failure this policy exists to prevent for
    // a worse one.
    if (in.estop) {
        return WebRequestAdmission::kAdmit;
    }

    if (!in.longLived && in.inflightRequests >= in.maxInflightRequests) {
        return WebRequestAdmission::kRejectInflightCap;
    }

    const size_t floor = in.diagnostic ? in.minLargestFreeBlockDiagnostic : in.minLargestFreeBlock;
    if (in.largestFreeBlock < floor) {
        return WebRequestAdmission::kRejectHeapFloor;
    }

    return WebRequestAdmission::kAdmit;
}

bool webPathIsEstop(const char* path) {
    return pathMatches(path, "/api/estop");
}

bool webPathIsDiagnostic(const char* path) {
    if (path == nullptr) {
        return false;
    }
    // Exact matches, matching what the async stack exempted. The coredump
    // subpaths are deliberately absent: /api/coredump/erase writes flash and
    // is not the read-only diagnostic this lower floor exists for.
    return strcmp(path, "/api/status") == 0 || strcmp(path, "/api/profiler") == 0 ||
           strcmp(path, "/api/coredump") == 0 || webPathIsLongLived(path);
}

bool webPathIsLongLived(const char* path) {
    if (path == nullptr) {
        return false;
    }
    return strcmp(path, "/api/events") == 0;
}

bool webIsMainFrameNavigation(const char* secFetchMode, const char* accept) {
    // Present and explicit: believe it, including when it says this is not a
    // navigation. A same-origin fetch may still advertise text/html, so
    // falling through to Accept here would misclassify it.
    if (secFetchMode != nullptr && secFetchMode[0] != '\0') {
        return strcmp(secFetchMode, "navigate") == 0;
    }

    // Fallback for clients that omit the mode. A navigating browser leads its
    // Accept with text/html; asset and API callers do not.
    if (accept == nullptr) {
        return false;
    }
    return strncmp(accept, "text/html", 9) == 0;
}
