// =============================================================================
// include/web_admission.h
//
// Connection and request admission, owned by the project rather than by any
// web stack. Two layers, deliberately:
//
//   Connection Admission  -- runs before any HTTP byte is parsed, from the
//                            server's socket-open callback. Blind to the URL
//                            by construction, so it cannot exempt any path.
//   Request admission     -- runs once a request has been read, before route
//                            matching and before a static file is opened.
//                            This is the layer that knows the URL, so this is
//                            where the estop bypass lives.
//
// The decision functions here are pure: no Arduino, no heap, no clock of their
// own. Thresholds arrive as parameters instead of being baked in, so a host
// test pins the behaviour without pinning the calibration -- the calibrated
// values live in platformio.ini [flags_base] under their rationale, and the
// device hookup passes them in.
//
// See docs/adr/0018-early-admission-seam-feasibility.md for why admission has
// to gate the costly work rather than run beside it, and CONTEXT.md for the
// Connection Admission / Immediate Request Refusal distinction.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Connection Admission
// -----------------------------------------------------------------------------

// Token bucket over accepted connections, in milli-tokens so a refill rate of
// a few per second still accumulates smoothly between millisecond ticks.
//
// Rate pacing exists because a heap threshold alone cannot stop a dense burst:
// connections already admitted keep allocating while the heap falls, so the
// next sample arrives too late. Bounding the admission rate bounds how much
// allocation pressure can pile up regardless of what the heap looked like.
struct WebAcceptRateLimiter {
    uint32_t tokensMilli;
    uint32_t lastRefillMs;
};

// Start the bucket full, as of nowMs. A server that has just begun listening
// should be able to absorb a whole burst before pacing bites.
void webAcceptRateLimiterInit(WebAcceptRateLimiter* limiter, uint32_t nowMs, uint32_t burst);

// Refill for elapsed time, then take one token. Returns false when the bucket
// is empty, meaning this connection is being paced out. Wraparound of the
// millisecond clock is handled by unsigned subtraction, so a 49-day uptime
// does not produce a spurious refill or stall.
bool webAcceptRateLimiterTake(WebAcceptRateLimiter* limiter, uint32_t nowMs, uint32_t burst,
                              uint32_t perSecond);

enum class WebAcceptDecision : unsigned char {
    kAdmit,
    kRejectRate,
    kRejectHeap,
};

// Produces the current largest free block. Passed in rather than called
// directly so the decision stays host-testable, and so the decision itself
// controls whether the sample is taken at all.
using WebHeapSampler = size_t (*)(void* ctx);

// The whole Connection Admission decision. Rate is checked before heap, and
// the sampler is invoked only if the rate check passes: the rate check is
// arithmetic on state already in cache, while sampling the heap may cost a
// heap walk, so a paced-out connection must never trigger one.
WebAcceptDecision webAcceptDecide(WebAcceptRateLimiter* limiter, uint32_t nowMs, uint32_t burst,
                                  uint32_t perSecond, WebHeapSampler sampler, void* samplerCtx,
                                  size_t minLargestFreeBlock);

// -----------------------------------------------------------------------------
// Cached heap sample
// -----------------------------------------------------------------------------

// heap_caps_get_largest_free_block() walks the heap, and both admission layers
// run on the single server task that also services every other connection. The
// sample is therefore cached and refreshed at most once per interval, which
// bounds the walk cost to a rate rather than a per-connection charge.
struct WebHeapSampleCache {
    size_t value;
    uint32_t lastSampleMs;
    bool primed;
};

// True when the cache is unprimed or older than minIntervalMs. An unprimed
// cache reads as due, never as "plenty of heap" -- an optimistic default would
// admit a whole burst on a value nobody has measured yet.
bool webHeapSampleDue(const WebHeapSampleCache* cache, uint32_t nowMs, uint32_t minIntervalMs);

// Record a fresh sample taken at nowMs.
void webHeapSampleStore(WebHeapSampleCache* cache, uint32_t nowMs, size_t value);

// -----------------------------------------------------------------------------
// Socket census
// -----------------------------------------------------------------------------

// How many admitted sockets the census can name at once. Sized above the
// server's max_open_sockets so the tracked set never fills before the server's
// own budget does -- if it did, the overflow would be an artefact of this
// bookkeeping rather than a property of the stack being measured.
#ifndef WEB_SOCKET_CENSUS_CAPACITY
#define WEB_SOCKET_CENSUS_CAPACITY 16
#endif

// Connection lifetime evidence: how many sockets were opened, how many are open
// now, and how many requests were served across them.
//
// The ratio of requests to sockets is what actually answers "is this stack
// reusing connections", and it cannot be inferred from either number alone. A
// stack that closes per response serves one request per socket; a keep-alive
// stack serves many, and pays for it in occupancy against a fixed
// max_open_sockets budget.
//
// The set of admitted descriptors is held rather than a bare counter because
// the server calls its close callback for sockets Connection Admission refused
// as well as for ones it admitted (httpd_sess_new() routes an open_fn failure
// through httpd_sess_delete(), which calls close_fn). A bare decrement would
// therefore underflow by exactly the refusal count -- and refusals cluster in
// precisely the pressure windows where the occupancy reading matters most.
struct WebSocketCensus {
    // Descriptors of currently-open admitted sockets. A free slot holds -1.
    int admittedFds[WEB_SOCKET_CENSUS_CAPACITY];
    int open;
    int openPeak;
    // Cumulative admitted opens. Connection churn is this against uptime.
    uint32_t accepted;
    // Requests seen by the request layer, across all sockets.
    uint32_t requests;
    // Admitted sockets the set had no room to name. Published rather than
    // silently dropped: a non-zero value means `open` is an undercount and the
    // capacity above is what needs raising, not the reading that needs
    // explaining.
    uint32_t untracked;
};

// Clear the census and mark every slot free.
void webSocketCensusInit(WebSocketCensus* census);

// Record an admitted socket. Returns false when the set was full, in which case
// the open is still counted in `accepted` and `untracked` but the descriptor is
// not tracked, so its later close cannot be attributed.
bool webSocketCensusOpen(WebSocketCensus* census, int fd);

// Release a socket. Returns false for a descriptor the census never admitted --
// a refused connection, or one lost to the capacity above -- and leaves the
// occupancy count untouched in that case.
bool webSocketCensusClose(WebSocketCensus* census, int fd);

// Count one request. Deliberately not keyed on the socket: what is wanted is
// the total, and pinning requests to descriptors would make this a per-request
// scan of the set on the one task that services every connection.
void webSocketCensusRequest(WebSocketCensus* census);

// -----------------------------------------------------------------------------
// Request admission
// -----------------------------------------------------------------------------

enum class WebRequestAdmission : unsigned char {
    kAdmit,
    kRejectInflightCap,
    kRejectHeapFloor,
};

struct WebRequestAdmissionInputs {
    // True for the estop path. Never rejected, never counted -- the safety
    // path must not be shed by a memory policy.
    bool estop;
    // Read-only diagnostics and the live update stream. They stay reachable
    // deeper into heap pressure because they are what an operator needs in
    // order to see a rejection window at all, but they are not exempt: their
    // own response construction still allocates.
    bool diagnostic;
    // The live update stream is not counted against the in-flight cap; it
    // would pin a slot for the connection's whole lifetime.
    bool longLived;
    int inflightRequests;
    int maxInflightRequests;
    size_t largestFreeBlock;
    size_t minLargestFreeBlock;
    size_t minLargestFreeBlockDiagnostic;
    // Bench-only floor override. When non-zero it replaces the ordinary-class
    // floor above, so a build carrying it refuses ordinary work on a healthy
    // heap -- the only way to exercise the ADR 0016 Busy Recovery Page against
    // a controller that real load can no longer degrade. Diagnostics keep
    // their own floor so the induced session stays observable, and the estop
    // bypass is unaffected because it is decided before any floor is
    // consulted. Zero in every shipping build.
    size_t minLargestFreeBlockOverride;
};

// The whole request-level decision, in the same order the async stack applied
// it: estop bypass, then the in-flight cap, then the heap floor for this
// request's class.
WebRequestAdmission webRequestAdmissionDecide(const WebRequestAdmissionInputs& in);

// Path classification. Substring-free and allocation-free: these run on the
// server task for every request, including every static asset on a page load.
bool webPathIsEstop(const char* path);
bool webPathIsDiagnostic(const char* path);
bool webPathIsLongLived(const char* path);

// True when this request is the browser navigating to a page, rather than a
// page fetching one of its own resources.
//
// The distinction decides what a refusal looks like on the wire. A refused
// navigation must carry the Busy Recovery Page, because a bodyless non-2xx
// response to a main-frame navigation is what the browser renders as its own
// error page -- the operator sees a dead tab rather than a controller saying
// it is busy. A refused asset gets the cheap close instead: dressing up every
// shed asset would cost bytes at the moment there are none, and no asset
// caller renders a body anyway.
//
// Both header values are taken as already-copied strings so the decision stays
// host-testable and so the caller controls where they came from -- reading
// them through the vendor request object would allocate.
//
// Sec-Fetch-Mode is authoritative when present: every browser this UI targets
// sends it, and "navigate" means exactly this. Accept is the fallback for
// clients that omit it, where a preference for text/html is the best available
// signal. Absent both, the request is treated as an asset, so an unknown
// client gets the cheap path rather than the expensive one.
bool webIsMainFrameNavigation(const char* secFetchMode, const char* accept);

// -----------------------------------------------------------------------------
// Counters
// -----------------------------------------------------------------------------
//
// Project-owned. They took over from globals that lived inside a patched copy
// of a vendor TCP library, and outliving that library is the point: these are
// the only accept/admission counters now. The JSON field names they are
// published under are a separate contract -- see buildStatusJson() in
// src/web/web_server.cpp, where they keep their historical spelling so run
// evidence stays comparable against the recorded baseline without hand-diffing.
extern volatile uint32_t g_webAcceptRejectHeap;
extern volatile uint32_t g_webAcceptRejectRate;
extern volatile uint32_t g_webAcceptRejectLastMs;

// The largest-free-block reading that caused the most recent heap rejection,
// and the lowest one ever seen by the guard. A refusal count alone says the
// floor was crossed but not by how far, which is exactly the evidence needed
// before anyone is allowed to argue about where the floor belongs.
extern volatile uint32_t g_webAcceptRejectLargestBlock;
extern volatile uint32_t g_webAcceptMinLargestBlockSeen;

// Cost of the Connection Admission guard itself, in microseconds. Always on:
// the guard's affordability is an acceptance condition of the stack it runs
// on, not a one-off measurement, so it stays visible in every run.
extern volatile uint32_t g_webAcceptGuardLastUs;
extern volatile uint32_t g_webAcceptGuardMaxUs;

// Request-level admission evidence. Same broad classes the decision gates on.
extern volatile int g_webInflightRequests;
extern volatile int g_webInflightRequestsPeak;
extern volatile uint32_t g_webRefusedInflightCap;
extern volatile uint32_t g_webRefusedHeapFloor;
extern volatile uint32_t g_webRefusedHeapFloorDiag;

// Refusals that were answered with the Busy Recovery Page rather than a bare
// close. Published because this path is rare by design and would otherwise be
// indistinguishable from a broken one: an aggregate run that never triggers it
// looks exactly like a run where it silently stopped working.
extern volatile uint32_t g_webBusyRecoveryPagesServed;

// Connection lifetime, published from the census above. Kept as plain globals
// alongside the rest rather than exposing the census struct, so the status
// builder stays a formatter and never reaches into admission state.
//
// These have no historical spelling to preserve: nothing measured connection
// reuse before, because on the async stack there was none to measure.
extern volatile uint32_t g_webSocketsAccepted;
extern volatile int g_webSocketsOpen;
extern volatile int g_webSocketsOpenPeak;
extern volatile uint32_t g_webSocketsUntracked;
extern volatile uint32_t g_webRequestsServed;
