// =============================================================================
// include/web_request_scratch.h
//
// One scratch for the web handlers' per-request buffers (#428).
//
// On artoo-esp32 every static byte is a heap byte: with Bluetooth compiled
// out, static data starts at the bottom of DRAM and the heap gets what is
// left (#381). A handler's request-scoped buffer that is `static` holds its
// bytes for the whole boot although it is used for one request at a time,
// and several of them are kilobytes. They share this one store instead.
//
// Sharing is safe because of how requests are served, and the accessor
// checks both halves of that rather than trusting them:
//
//  - One server task. esp_http_server runs every handler on its one "httpd"
//    task, pinned to core 0 (src/web/web_request_psychic.cpp), so two
//    handlers never run at once. The store is bound to that task once it
//    exists, and a claim from any other task is refused.
//  - A handler is done with its buffer when it returns. send() and
//    sendChunked() finish on the wire before they return, so nothing reads a
//    request's buffer after its handler has. A second claim while one is
//    held - a handler reaching another scratch user mid-request - is
//    refused, so no handler can ever write over a buffer another is using.
//
// A refused claim is a programming error, not a load condition. The handler
// answers it as an error (never silently reuses the store), and the refusal
// is logged.
//
// Usage, in a handler:
//
//     WebRequestScratch<ConfigApplyResult> scratch;
//     if (!scratch) {
//         webSendJsonError(req, 500, "request scratch unavailable");
//         return;
//     }
//     ConfigApplyResult& result = *scratch;
//
// Every claim value-initialises its type, so each request starts from the
// same state a fresh `static` did at boot - which the old per-handler
// statics only ever got once.
// =============================================================================
#pragma once

#include <stddef.h>

#include <new>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "api_audio_category_range_apply.h"
#include "api_audio_mood_map_apply.h"
#include "api_audio_tracks_apply.h"
#include "api_config_apply.h"
#include "api_console.h"
#include "api_rc_map_apply.h"
#include "api_wifi_apply.h"
#include "sequence_run_evidence.h"
#include "status_json.h"

// A handler's text body, for a type the union below can hold.
template <size_t N>
struct WebScratchText {
    char text[N];
};

// GET /api/rc/map's body. The map holds at most kRcMapMaxEntries entries of
// source/channel/action plus an optional Marcduino payload; 2 KB clears a full
// map with headroom.
constexpr size_t RC_MAP_JSON_BODY_BYTES = 2048;

// Every type a handler keeps in the scratch. The store is as large as the
// largest of them on the chip being built, so a type that grows past the
// others grows the store rather than overrunning it; one that is not listed
// here fails to compile at its WebRequestScratch<> below.
union WebRequestScratchLayout {
    ConsoleWebScratch console;                                 // POST /api/console
    ConfigApplyResult configApply;                             // POST /api/config
    WebScratchText<STATUS_JSON_BUFFER_BYTES> statusBody;       // GET /api/status
    SeqRunEvidence seqLastRun;                                 // GET /api/seq/last-run
    WebScratchText<RC_MAP_JSON_BODY_BYTES> rcMapBody;          // GET /api/rc/map
    RcMapApplyResult rcMapApply;                               // POST /api/rc/map
    WifiApplyResult wifiApply;                                 // POST /api/wifi
    AudioMoodMapApplyResult audioMoodMapApply;                 // POST /api/audio/mood-map
    AudioTracksApplyResult audioTracksApply;                   // POST /api/audio/tracks
    AudioCategoryRangeApplyResult audioCategoryRangeApply;     // POST /api/audio/category-range

    // Never constructed: the union only sizes and aligns the store. Its
    // members have constructors of their own, which would otherwise delete
    // the union's.
    WebRequestScratchLayout() = delete;
};

// Binds the store to the task that serves requests. Called once, on that task
// (src/web/web_request_psychic.cpp). Until it is, every claim is refused.
void webRequestScratchBindOwner(TaskHandle_t owner);

// The raw claim behind WebRequestScratch<>. Null, logged, when the caller is
// not the bound task or the store is already held. Handlers use the class.
void* webRequestScratchClaim();
void webRequestScratchRelease();

// Holds the store for one handler's request, as a T. Released when it goes
// out of scope.
template <typename T>
class WebRequestScratch {
public:
    WebRequestScratch() {
        static_assert(sizeof(T) <= sizeof(WebRequestScratchLayout),
                      "a type kept in the web request scratch must be a member of "
                      "WebRequestScratchLayout");
        static_assert(alignof(T) <= alignof(WebRequestScratchLayout),
                      "a type kept in the web request scratch must be a member of "
                      "WebRequestScratchLayout");
        void* storage = webRequestScratchClaim();
        if (storage != nullptr) {
            value_ = new (storage) T();
        }
    }

    ~WebRequestScratch() {
        if (value_ != nullptr) {
            value_->~T();
            webRequestScratchRelease();
        }
    }

    WebRequestScratch(const WebRequestScratch&) = delete;
    WebRequestScratch& operator=(const WebRequestScratch&) = delete;

    explicit operator bool() const { return value_ != nullptr; }
    T& operator*() const { return *value_; }
    T* operator->() const { return value_; }

private:
    T* value_ = nullptr;
};
