// =============================================================================
// src/web/web_server.cpp
//
// WiFi and AsyncWebServer bootstrap for protoArtoo.
// =============================================================================

#include "../../include/web_server.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#ifdef ARDUINO
#include <Update.h>
#endif
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <stddef.h>
#include <stdio.h>

#include "../../include/api_actions.h"
#include "../../include/api_profiler.h"
#include "../../include/api_config.h"
#include "../../include/drive_speed_preset.h"
#include "../../include/api_helpers.h"
#include "../../include/api_seq.h"
#include "../../include/api_status.h"
#include "../../include/api_system.h"
#include "../../include/audio_task.h"
#include "../../include/reset_reason.h"
#include "../../include/config.h"
#include "../../include/config_store.h"
#include "../../include/aux_led.h"
#include "../../include/rc_diagnostics_snapshot.h"
#include "../../include/robot_state.h"
#include "../../include/web_admission.h"
#include "../../include/web_event_stream.h"
#include "../../include/web_request.h"
#include "../../include/web_request_async.h"
#include "../../include/wifi_boot_decision.h"
#include "../../include/wifi_recovery_gesture.h"
#ifdef PA_USE_PSYCHICHTTP_PROTOTYPE
#include "../../include/psychic_adapter.h"
#endif
#ifdef PA_WEB_BACKEND_PSYCHIC
#include "../../include/web_server_psychic.h"
#endif

// src/secrets.h is the Developer WiFi Shortcut (ADR 0015): local/self-build-only
// compile-time WiFi defaults. It is never required to compile or boot — public
// release binaries (protoArtoo_chirp, protoArtoo_mp3trigger) ship without it and
// boot into WiFi Provisioning via wifiDecideBootPosture() instead.
#if __has_include("secrets.h")
#include "secrets.h"
#define PA_HAS_SECRETS_HEADER 1
#else
#define PA_HAS_SECRETS_HEADER 0
#endif

// PA_ENABLE_STA_WIFI selects which posture the Developer WiFi Shortcut resolves to
// when secrets.h is present: 1 (default) = WiFi Client Mode, 0 = Standalone AP Mode.
// It has no effect once Device WiFi Settings are provisioned (runtime settings win).
#ifndef PA_ENABLE_STA_WIFI
#define PA_ENABLE_STA_WIFI 1
#endif

#ifndef ARDUINO
class String {
   public:
    const char* c_str() const {
        return "";
    }
    bool startsWith(const char*) const {
        return false;
    }
    bool operator==(const char*) const {
        return false;
    }
};

class IPAddress {
   public:
    String toString() const {
        return String();
    }
};

using wl_status_t = int;
using WiFiEvent_t = int;

static const wl_status_t WL_CONNECTED = 3;
static const int WIFI_AP = 1;
static const int WIFI_AP_STA = 2;
static const WiFiEvent_t ARDUINO_EVENT_WIFI_AP_START = 0;
static const WiFiEvent_t ARDUINO_EVENT_WIFI_STA_START = 1;
static const WiFiEvent_t ARDUINO_EVENT_WIFI_STA_GOT_IP = 2;
static const WiFiEvent_t ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 3;

class AsyncEventSourceClient {
   public:
    void send(const char*, const char*, unsigned long) {
    }
};

class AsyncEventSource {
   public:
    explicit AsyncEventSource(const char*) {
    }

    template <typename Callback>
    void onConnect(Callback) {
    }

    size_t count() const {
        return 0;
    }
    void send(const char*, const char*, unsigned long) {
    }
};

class AsyncStaticWebHandler {
   public:
    AsyncStaticWebHandler& setDefaultFile(const char*) { return *this; }
    AsyncStaticWebHandler& setCacheControl(const char*) { return *this; }
};

class AsyncWebServerRequest {
   public:
    const String& url() const {
        static String s;
        return s;
    }
    void abort() {
    }
    template <typename Callback>
    void onDisconnect(Callback) {
    }
};

using ArMiddlewareNext = void (*)();

class AsyncWebServer {
   public:
    explicit AsyncWebServer(int) {
    }
    void addHandler(AsyncEventSource*) {
    }
    template <typename Middleware>
    void addMiddleware(Middleware) {
    }
    template <typename FsType>
    AsyncStaticWebHandler& serveStatic(const char*, FsType&, const char*) {
        static AsyncStaticWebHandler handler;
        return handler;
    }
    void begin() {
    }
};

class LittleFSClass {
   public:
    bool begin(bool) {
        return false;
    }
};

class WiFiClass {
   public:
    wl_status_t status() const {
        return 0;
    }
    long RSSI() const {
        return 0;
    }
    IPAddress softAPIP() const {
        return IPAddress();
    }
    IPAddress localIP() const {
        return IPAddress();
    }
    int getMode() const {
        return WIFI_AP;
    }
    int softAPgetStationNum() const {
        return 0;
    }
    void onEvent(void (*)(WiFiEvent_t)) {
    }
    void mode(int) {
    }
    void softAP(const char*, const char* = nullptr) {
    }
    void begin(const char*, const char*) {
    }
};

class ESPClass {
   public:
    unsigned long getFreeHeap() const {
        return 0;
    }
    unsigned long getMinFreeHeap() const {
        return 0;
    }
    unsigned long getMaxAllocHeap() const {
        return 0;
    }
};

static WiFiClass WiFi;
static LittleFSClass LittleFS;
static ESPClass ESP;

inline unsigned long millis() {
    return 0;
}
inline unsigned long pdMS_TO_TICKS(unsigned long ms) {
    return ms;
}
inline void vTaskDelay(unsigned long) {
}
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, unsigned int, void*, unsigned int,
                                   void*, int) {
    return 0;
}
#endif

static const char* TAG = "WebServer";
static AsyncWebServer server(80);
static AsyncEventSource events("/api/events");
static bool littleFsReady = false;

// Admission control against heap exhaustion under bursty concurrent load. A
// burst of long-lived SSE connections plus overlapping page-reload traffic
// can drop the largest contiguous free block low enough that
// ESPAsyncWebServer's own response-construction allocations fail (see
// tools/patch_async_sse.py for the vendor-side hardening applied to that
// failure path). These two gates keep heap out of that danger zone in the
// first place instead of only reacting to it after the fact.

// Concurrent /api/events (SSE) clients. The cap itself now lives with the rest
// of the stream's policy in include/web_event_stream.h, so both stacks enforce
// one number.
static constexpr size_t kMaxSseClients = PA_ADMISSION_MAX_SSE_CLIENTS;

// Below this, prefer rejecting new non-essential requests over constructing
// more response objects. Chosen with wide margin above the ~2-10 KB
// largest-block range where allocation failures were actually observed
// while reproducing the crash under load, so a rejection response can still
// be built safely. Matches the threshold used by the vendor-side guards in
// tools/patch_async_sse.py (LittleFS static-file open, AsyncTCP accept).
#ifndef PA_ADMISSION_MIN_LARGEST_FREE_BLOCK
#define PA_ADMISSION_MIN_LARGEST_FREE_BLOCK 20000
#endif
static constexpr size_t kMinLargestFreeBlockForNewWork = PA_ADMISSION_MIN_LARGEST_FREE_BLOCK;

// Read-only diagnostics (status/profiler/coredump) stay reachable far deeper
// into heap pressure than normal work -- they are what an operator needs to
// see a rejection window -- but they must not be exempt entirely: response
// construction itself allocates (headers, body copy), and letting it run
// with a critically depressed heap aborts the firmware on an unpatched
// std::list node allocation inside addHeader (coredump-proven, three times).
// Floor sits just above the 2-10 KB largest-block range where those
// allocations were observed to fail.
#ifndef PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG
#define PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG 10000
#endif
static constexpr size_t kMinLargestFreeBlockForDiagnostics =
    PA_ADMISSION_MIN_LARGEST_FREE_BLOCK_DIAG;

// Deterministic bound on parked-request memory. Every accepted request holds
// its parsed request object and header list while it waits for the single
// async_tcp task to serve it; a dense connection burst can park enough of
// them to exhaust the heap before any of them respond, no matter what the
// heap looked like when each was admitted. Browsers open at most 6 parallel
// connections per host, so a cap of 6 is invisible to legitimate clients.
// Single-writer: touched only from the async_tcp task.
#ifndef PA_ADMISSION_MAX_INFLIGHT_REQUESTS
#define PA_ADMISSION_MAX_INFLIGHT_REQUESTS 6
#endif
static constexpr int kMaxInflightRequests = PA_ADMISSION_MAX_INFLIGHT_REQUESTS;

// Lifecycle evidence: admission counters by the same broad classes the
// middleware already gates on (inflight, SSE, heap-floor diagnostic and
// non-diagnostic). Cheap int increments only -- safe to keep in the always-on
// /api/status snapshot. Single-writer (async_tcp task), read cross-task
// (eventTask SSE broadcast, status handler) without a mutex.
//
// The inflight and heap-floor counters belong to this file's own admission
// middleware, which only exists on the async stack. On the PsychicHttp build
// the equivalents are the project-owned globals in include/web_admission.h,
// so these would be permanently zero -- and a counter that is always zero
// reads as evidence of no refusals rather than of no implementation.
#ifndef PA_WEB_BACKEND_PSYCHIC
static int s_inflightRequests = 0;
static int s_peakInflightRequests = 0;
static uint32_t s_refusedInflightCap = 0;
static uint32_t s_refusedHeapFloor = 0;
static uint32_t s_refusedHeapFloorDiag = 0;
#endif

// The event stream's own counters (peak clients, refused-at-cap, evicted) are
// project-owned globals in include/web_event_stream.h, written by whichever
// backend is serving the stream and read here for /api/status.

#if PA_HEAP_PROFILE
// Bounded request-lifecycle trace (issue #54 evidence, profiler-gated so it
// costs nothing in normal builds). Read after an experiment via
// /api/profiler, not polled during the workload. Covers only admitted,
// non-SSE requests -- SSE's own lifetime is already visible via the
// sseClients/sseClientsPeak counters above, and its "disconnect" is a tab
// closing, not a per-request event this ring is meant to capture.
//
// handlerDoneMs marks when next() returned, i.e. when the matched handler's
// call into send() returned control to the middleware. Under this stack's
// synchronous per-request dispatch (single async_tcp task, matching the
// single-writer property already relied on for s_inflightRequests above),
// that is the best available proxy for "response ready" -- actual socket
// write/flush still happens later via AsyncTCP polling, up to disconnectMs.
//
// Single-writer (async_tcp task) for both the initial record and the two
// updates below; read from the same task via /api/profiler. A slot may be
// overwritten by a newer entry before a very long-lived request's
// disconnectMs update reaches it -- acceptable for a bounded evidence trace,
// not a correctness-bearing structure.
//
// RequestLifecycleEntry and PA_REQUEST_TRACE_MAX are declared in
// web_server.h so api_profiler.cpp can size its copy buffer identically.
static RequestLifecycleEntry s_requestTrace[PA_REQUEST_TRACE_MAX];
static uint8_t s_requestTraceHead = 0;
static uint8_t s_requestTraceCount = 0;

// Opens a new lifecycle-trace entry for one admitted request, called from
// the admission middleware below at the moment a non-SSE request is counted
// against the inflight cap. Overwrites the oldest ring slot once full; the
// returned index is captured by the request's onDisconnect closure so
// disconnectMs can be filled in later without a second lookup.
static uint8_t pushRequestTraceEntry(const char* path, uint32_t startMs) {
    uint8_t idx = s_requestTraceHead;
    RequestLifecycleEntry& e = s_requestTrace[idx];
    strncpy(e.requestPath, path, sizeof(e.requestPath) - 1);
    e.requestPath[sizeof(e.requestPath) - 1] = '\0';
    e.startMs = startMs;
    e.handlerDoneMs = 0;
    e.disconnectMs = 0;
    s_requestTraceHead = (uint8_t)((s_requestTraceHead + 1U) % PA_REQUEST_TRACE_MAX);
    if (s_requestTraceCount < PA_REQUEST_TRACE_MAX) {
        s_requestTraceCount++;
    }
    return idx;
}

// Copies the trace ring oldest-first into out, for the /api/profiler handler
// (api_profiler.cpp) to read once after an experiment. Read-only; does not
// clear or rotate the ring, so repeated reads during a warm-up are safe.
size_t copyRequestLifecycleTrace(RequestLifecycleEntry* out, size_t maxEntries) {
    uint8_t count = s_requestTraceCount;
    if (count > maxEntries) {
        count = (uint8_t)maxEntries;
    }
    uint8_t oldest = (uint8_t)((s_requestTraceHead + PA_REQUEST_TRACE_MAX - s_requestTraceCount) %
                                PA_REQUEST_TRACE_MAX);
    for (uint8_t i = 0; i < count; i++) {
        out[i] = s_requestTrace[(uint8_t)((oldest + i) % PA_REQUEST_TRACE_MAX)];
    }
    return count;
}
#endif  // PA_HEAP_PROFILE

#ifdef ARDUINO
static size_t largestFreeBlock8Bit() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}
#else
static size_t largestFreeBlock8Bit() {
    return SIZE_MAX;
}
#endif

// Accept-guard telemetry, defined inside the patched AsyncTCP tcp_accept
// path (tools/patch_async_sse.py). Always-on visibility for rejected
// accepts: the guard's own debug log is compiled out in normal builds, and
// its invisibility previously cost two diagnosis rounds.
extern "C" {
extern volatile uint32_t g_asyncTcpAcceptRejectHeap;
extern volatile uint32_t g_asyncTcpAcceptRejectRate;
extern volatile uint32_t g_asyncTcpAcceptRejectLastMs;
}

static char s_fsVersion[48] = "unknown";
static bool routesRegistered = false;
static bool serverStarted = false;
static bool eventTaskStarted = false;
static bool otaTaskStarted = false;
static bool mdnsStarted = false;
static constexpr int OTA_RECEIVE_TIMEOUT_MS = 15000;
static volatile bool s_otaActive = false;
static volatile uint8_t s_otaProgressPct = 255;
static uint8_t s_lastOtaLoggedPct = 255;
static char s_otaLastError[64] = "none";

// Network Recovery Mode local entry gesture (ADR 0015). See
// include/wifi_recovery_gesture.h for the pure decision rule. The count is
// persisted under NVS_NAMESPACE so it survives the reboot(s) the gesture
// itself requires; it is cleared once uptime confirms the boot was not part
// of a rapid power-cycle sequence.
static const char* kWifiRecoveryCycleKey = "wifiRecovN";
static constexpr uint32_t WIFI_RECOVERY_GESTURE_STABLE_MS = 20000;

namespace {

// MALLOC_CAP_INTERNAL — dominated by a constant ~36 KB leftover-IRAM block
// that malloc can never allocate. Kept ONLY to keep the legacy
// heapLargestBlock status field stable for existing consumers; never use it
// for heap-health decisions. The real pool is largestFreeBlock8Bit().
static uint32_t webHeapMaxAlloc() {
    return (uint32_t)ESP.getMaxAllocHeap();
}

static void logOtaHeapCheckpoint(const char* label) {
    PA_LOG_INFO("ArduinoOTA", "%s heap free=%lu min=%lu largest8bit=%lu",
                label,
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMinFreeHeap(),
                (unsigned long)largestFreeBlock8Bit());
}

const char* rcInputModeLabel(RcInputMode mode) {
    switch (mode) {
        case RC_INPUT_STANDARD_PWM:
            return "standard_pwm";
        case RC_INPUT_SINGLE_SBUS:
            return "single_sbus";
        case RC_INPUT_DUAL_SBUS:
        default:
            return "dual_sbus";
    }
}

const char* domeTransportLabel(DomeLinkTransport transport) {
    switch (transport) {
        case DOME_LINK_TRANSPORT_UART:
            return "uart";
        case DOME_LINK_TRANSPORT_WIFI:
            return "wifi";
        case DOME_LINK_TRANSPORT_DISCONNECTED:
        default:
            return "disconnected";
    }
}

void loadFsVersion() {
    snprintf(s_fsVersion, sizeof(s_fsVersion), "%s", "unknown");
#ifdef ARDUINO
    if (!littleFsReady) {
        return;
    }

    File versionFile = LittleFS.open("/fs-version.json", "r");
    if (!versionFile) {
        PA_LOG_WARN(TAG, "fs-version.json missing; using unknown fsVersion");
        return;
    }

    JsonDocument versionDoc;
    DeserializationError parseError = deserializeJson(versionDoc, versionFile);
    versionFile.close();
    if (parseError) {
        PA_LOG_WARN(TAG, "fs-version.json parse failed: %s", parseError.c_str());
        return;
    }

    const char* loadedVersion = versionDoc["fsVersion"] | "";
    if (loadedVersion[0] == '\0') {
        PA_LOG_WARN(TAG, "fs-version.json missing fsVersion key");
        return;
    }

    int n = snprintf(s_fsVersion, sizeof(s_fsVersion), "%s", loadedVersion);
    if (n <= 0 || n >= (int)sizeof(s_fsVersion)) {
        PA_LOG_WARN(TAG, "fsVersion truncated to %u chars", (unsigned)(sizeof(s_fsVersion) - 1));
    }
#endif
}

bool appendJsonChunk(char*& pos, size_t& remaining, const char* chunk) {
    if (remaining == 0) {
        return false;
    }

    int n = snprintf(pos, remaining, "%s", chunk);
    if (n <= 0 || n >= (int)remaining) {
        return false;
    }

    pos += n;
    remaining -= (size_t)n;
    return true;
}

bool appendPeripheralStatus(char*& pos, size_t& remaining, const char* key, const char* state,
                            const char* detail) {
    if (remaining == 0) {
        return false;
    }

    int n = snprintf(pos, remaining, ",\"%s\":{\"state\":\"%s\",\"detail\":\"%s\"}", key, state,
                     detail);
    if (n <= 0 || n >= (int)remaining) {
        return false;
    }

    pos += n;
    remaining -= (size_t)n;
    return true;
}

}  // namespace

bool buildStatusJson(char* buffer, size_t bufferSize) {
    FailsafeDiagnostics diag = {};
    bool webControlEnabled;
    bool sbusSignalLost;
    bool sbus2SignalLost;
    bool wifiConnected;
    bool wifiClientConnected;
    int driveSpeed;
    int driveSteer;
    float domeTargetSpeed;
    int speedLimitMax;
    SpeedPresetId speedPresetActive;
    bool stationary;
    unsigned long uptimeMs;
    unsigned long heapFree;
    unsigned long heapMin;
    uint32_t heapLargestBlock;
    bool otaActive;
    uint8_t otaProgressPct;
    char otaLastError[64];
    long wifiRssi;
    bool enableArm1, enableArm2, enableAux1, enableAux2, enableAux3, enableDome;
    bool enableRcCh1, enableRcCh2, enableRcCh3, enableRcCh4, enableRcCh5, enableRcCh6;
    bool enableS1Hoverboard, enableS2Sound, enableS3DomeCtrl;
    bool audioActive;
    bool audioLinkOk;
    AudioRxStatus audioRxStatus;
    bool sleepMode;
    uint8_t activeMood;
    uint32_t sleepSinceMs;
    uint8_t auxLedPin;
    uint8_t auxLedR;
    uint8_t auxLedG;
    uint8_t auxLedB;
    AuxLedEffect auxLedEffect;
    bool auxLedAvailable;
    RcInputMode rcInputMode;
    bool singleSbusUseCh2;
    uint16_t arm1TargetUs;
    uint16_t arm2TargetUs;
    uint32_t lastSbus1Ms;
    uint32_t lastSbus2Ms;
    uint32_t sbus1LostFrameCount;
    uint32_t sbus2LostFrameCount;
    uint32_t domeHbRx;
    uint32_t bodyHbTx;
    uint32_t domeLastSeenMs;
    uint32_t domeRxOverflowCount;
    uint32_t domeRxUnknownCount;
    DomeLinkTransport domeActiveTransport;
    DomeUartOwner domeUartOwner;
    int16_t hbBatteryRaw;
    int16_t hbBoardTempRaw;
    int16_t hbSpeedR;
    int16_t hbSpeedL;
    int16_t hbCurrentL;
    int16_t hbCurrentR;
    bool hbFeedbackValid;

    if (buffer == nullptr || bufferSize == 0) {
        return false;
    }

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    taskENTER_CRITICAL(&robotStateMux);
    copyFailsafeDiagnosticsLocked(&diag);
    sbusSignalLost = diag.sbusSignalLost;
    webControlEnabled = robotState.webControlEnabled;
    sbus2SignalLost = robotState.sbus2SignalLost;
    driveSpeed = robotState.driveOutputSpeed;
    driveSteer = robotState.driveOutputSteer;
    domeTargetSpeed = robotState.domeTargetSpeed;
    speedLimitMax = cfg.drive.speedLimitMax;
    speedPresetActive = normalizeSpeedPresetId((uint8_t)cfg.drive.speedPresetActive);
    stationary = robotState.stationary;
    arm1TargetUs = robotState.arm1TargetUs;
    arm2TargetUs = robotState.arm2TargetUs;
    lastSbus1Ms = robotState.lastSbus1Ms;
    lastSbus2Ms = robotState.lastSbus2Ms;
    sbus1LostFrameCount = robotState.sbus1LostFrameCount;
    sbus2LostFrameCount = robotState.sbus2LostFrameCount;
    domeHbRx = robotState.domeHbRx;
    bodyHbTx = robotState.bodyHbTx;
    domeLastSeenMs = robotState.domeLastSeenMs;
    domeRxOverflowCount = robotState.domeRxOverflowCount;
    domeRxUnknownCount = robotState.domeRxUnknownCount;
    domeActiveTransport = robotState.domeActiveTransport;
    domeUartOwner = robotState.domeUartOwner;
    hbBatteryRaw = robotState.hb_batteryRaw;
    hbBoardTempRaw = robotState.hb_boardTempRaw;
    hbSpeedR = robotState.hb_speedR;
    hbSpeedL = robotState.hb_speedL;
    hbCurrentL = robotState.hb_currentL;
    hbCurrentR = robotState.hb_currentR;
    hbFeedbackValid = robotState.hb_feedbackValid;
    enableArm1 = cfg.system.enable_arm1;
    enableArm2 = cfg.system.enable_arm2;
    enableAux1 = cfg.system.enable_aux1;
    enableAux2 = cfg.system.enable_aux2;
    enableAux3 = cfg.system.enable_aux3;
    enableDome = cfg.system.enable_dome;
    enableRcCh1 = cfg.system.enable_rc_ch1;
    enableRcCh2 = cfg.system.enable_rc_ch2;
    enableRcCh3 = cfg.system.enable_rc_ch3;
    enableRcCh4 = cfg.system.enable_rc_ch4;
    enableRcCh5 = cfg.system.enable_rc_ch5;
    enableRcCh6 = cfg.system.enable_rc_ch6;
    rcInputMode = cfg.system.rc_input_mode;
    singleSbusUseCh2 = cfg.system.single_sbus_use_ch2;
    enableS1Hoverboard = cfg.system.enable_s1_hoverboard;
    enableS2Sound = cfg.system.enable_s2_sound;
    enableS3DomeCtrl = cfg.system.enable_s3_dome_ctrl;
    audioActive = robotState.audioActive;
    audioLinkOk = robotState.audio_module_link_ok;
    audioRxStatus = robotState.audio_module_rx_status;
    activeMood = robotState.activeMood;
    sleepMode = robotState.sleepMode;
    sleepSinceMs = robotState.sleepSinceMs;
    auxLedPin = robotState.auxLed.pin;
    auxLedR = robotState.auxLed.r;
    auxLedG = robotState.auxLed.g;
    auxLedB = robotState.auxLed.b;
    auxLedEffect = robotState.auxLed.effect;
    auxLedAvailable = robotState.auxLed.available;
    taskEXIT_CRITICAL(&robotStateMux);
    uptimeMs = millis();
    heapFree = ESP.getFreeHeap();
    heapMin = ESP.getMinFreeHeap();
    heapLargestBlock = webHeapMaxAlloc();
    otaActive = s_otaActive;
    otaProgressPct = s_otaProgressPct;
    snprintf(otaLastError, sizeof(otaLastError), "%s", s_otaLastError);
    int wifiMode = WiFi.getMode();
    bool apEnabled = wifiMode == WIFI_AP || wifiMode == WIFI_AP_STA;
    bool staConnected = WiFi.status() == WL_CONNECTED;
    unsigned int apStationCount = apEnabled ? (unsigned int)WiFi.softAPgetStationNum() : 0U;
    WiFiConnectivityFields wifi =
        deriveWiFiConnectivityFields(apEnabled, staConnected, apStationCount, WiFi.RSSI());
    wifiConnected = wifi.wifiConnected;
    wifiClientConnected = wifi.wifiClientConnected;
    wifiRssi = wifi.wifiRssi;

    const char* auxLedEffectLabel = auxLedEffectToString(auxLedEffect);

    // Admission evidence comes from whichever stack this build actually runs
    // its admission on. The JSON field names below stay as they are either
    // way: they are a comparability contract with the recorded baseline and
    // the load harness, not a description of which implementation produced
    // them. The names on the left are the project-owned counters that survive
    // the cutover; the async values feeding them here disappear with the
    // vendor patch that defines them.
#ifdef PA_WEB_BACKEND_PSYCHIC
    const uint32_t acceptRejectHeap = g_webAcceptRejectHeap;
    const uint32_t acceptRejectRate = g_webAcceptRejectRate;
    const uint32_t acceptRejectLastMs = g_webAcceptRejectLastMs;
    const int inflightRequests = g_webInflightRequests;
    const int inflightRequestsPeak = g_webInflightRequestsPeak;
    const uint32_t refusedInflightCap = g_webRefusedInflightCap;
    const uint32_t refusedHeapFloor = g_webRefusedHeapFloor;
    const uint32_t refusedHeapFloorDiag = g_webRefusedHeapFloorDiag;
#else
    const uint32_t acceptRejectHeap = g_asyncTcpAcceptRejectHeap;
    const uint32_t acceptRejectRate = g_asyncTcpAcceptRejectRate;
    const uint32_t acceptRejectLastMs = g_asyncTcpAcceptRejectLastMs;
    const int inflightRequests = s_inflightRequests;
    const int inflightRequestsPeak = s_peakInflightRequests;
    const uint32_t refusedInflightCap = s_refusedInflightCap;
    const uint32_t refusedHeapFloor = s_refusedHeapFloor;
    const uint32_t refusedHeapFloorDiag = s_refusedHeapFloorDiag;
#endif

    // Build the fixed system-health fields first.
    int written = snprintf(
        buffer, bufferSize,
        "{\"estop\":%s,\"webControlEnabled\":%s,\"sbusSignalLost\":%s,\"sbusHwFailsafe\":%s,\"webDriveExpired\":%s,\"failsafeSource\":%d,\"driveSpeed\":%d,\"driveSteer\":%d,\"domeTargetSpeed\":%.3f,\"domeEnabled\":%s,\"speedLimitMax\":%d,\"speedPreset\":\"%s\",\"stationary\":%s,\"failsafeCount\":%lu,\"failsafeTriggerMs\":%lu,\"failsafeZeroMs\":%lu,\"failsafeTriggerToZeroMs\":%lu,\"failsafeWatchdogMs\":%lu,\"failsafeTriggerSource\":%d,\"uptimeMs\":%lu,\"firmwareVersion\":\"%s\",\"fsVersion\":\"%s\",\"resetReason\":\"%s\",\"heapFree\":%lu,\"heapMin\":%lu,\"heapLargestBlock\":%lu,\"heapLargest8bit\":%lu,\"sseClients\":%u,\"sseClientsPeak\":%lu,\"tcpAcceptRejectHeap\":%lu,\"tcpAcceptRejectRate\":%lu,\"tcpAcceptRejectAgeMs\":%ld,\"acceptGuardLastUs\":%lu,\"acceptGuardMaxUs\":%lu,\"acceptRejectLargestBlock\":%lu,\"acceptMinLargestBlockSeen\":%ld,\"inflightRequests\":%d,\"inflightRequestsPeak\":%d,\"refusedInflightCap\":%lu,\"refusedSseCap\":%lu,\"sseEvicted\":%lu,\"sseEvictAgeMs\":%ld,\"refusedHeapFloor\":%lu,\"refusedHeapFloorDiag\":%lu,\"busyRecoveryPagesServed\":%lu,\"otaActive\":%s,\"otaProgress\":%u,\"otaLastError\":\"%s\",\"wifiRssi\":%ld,\"wifiConnected\":%s,\"wifiClientConnected\":%s,\"littleFsReady\":%s,\"sleepMode\":%s,\"sleepSinceMs\":%lu,\"activeMood\":%u,\"auxLed\":{\"pin\":%u,\"r\":%u,\"g\":%u,\"b\":%u,\"effect\":\"%s\",\"available\":%s}",
        diag.estop ? "true" : "false", webControlEnabled ? "true" : "false",
        diag.sbusSignalLost ? "true" : "false", diag.sbusHwFailsafe ? "true" : "false",
        diag.webDriveExpired ? "true" : "false", (int)diag.failsafeSource, driveSpeed, driveSteer,
        (double)domeTargetSpeed, enableDome ? "true" : "false",
        speedLimitMax, speedPresetIdToString(speedPresetActive), stationary ? "true" : "false",
        (unsigned long)diag.failsafeTriggerCount, (unsigned long)diag.failsafeLastTriggerMs, (unsigned long)diag.failsafeLastZeroOutputMs, (unsigned long)diag.failsafeLastTriggerToZeroMs,
        (unsigned long)diag.failsafeLastWatchdogMs, (int)diag.failsafeLastTriggerSource, uptimeMs, PA_FIRMWARE_VERSION, s_fsVersion,
        resetReasonName(esp_reset_reason()),
        heapFree, heapMin, (unsigned long)heapLargestBlock,
        // Same capability mask as every admission guard (MALLOC_CAP_8BIT).
        // heapLargestBlock above uses MALLOC_CAP_INTERNAL and can diverge
        // wildly from what the guards actually see; both are emitted so the
        // divergence itself is observable.
        (unsigned long)largestFreeBlock8Bit(),
        // Open event streams; the client cap keys on this, so stuck or leaked
        // entries become visible instead of silently denying new streams.
        (unsigned)webEventStreamClientCount(), (unsigned long)g_webSseClientsPeak,
        (unsigned long)acceptRejectHeap, (unsigned long)acceptRejectRate,
        acceptRejectLastMs == 0 ? -1L
                                : (long)(uint32_t)((uint32_t)uptimeMs - acceptRejectLastMs),
        // Cost of the connection guard itself. Kept always-on rather than
        // measured once: whether the guard is affordable on a stack that
        // services every connection from one task is a standing property, not
        // a one-off result.
        (unsigned long)g_webAcceptGuardLastUs, (unsigned long)g_webAcceptGuardMaxUs,
        // Depth evidence for the floor. The resting largest-block reading in
        // this same payload is by definition never the one that caused a
        // refusal, so a bare refusal count cannot say how far the floor was
        // crossed -- and crossing depth is what the out-of-scope rule demands
        // before any floor is argued about.
        (unsigned long)g_webAcceptRejectLargestBlock,
        g_webAcceptMinLargestBlockSeen == UINT32_MAX
            ? -1L
            : (long)g_webAcceptMinLargestBlockSeen,
        // Live + peak inflight depth and refusal counts by the same broad
        // classes the admission layer gates on -- current/peak/refused
        // evidence needed before any cap, floor, or weight is retuned.
        inflightRequests, inflightRequestsPeak,
        (unsigned long)refusedInflightCap, (unsigned long)g_webRefusedSseCap,
        // Stalled-client evictions. Rare by design, which is exactly why they
        // are published: a run that never trips the deadline is otherwise
        // indistinguishable from one where the guard silently stopped working.
        // The age separates a boot-time blip from an ongoing problem.
        (unsigned long)g_webSseEvicted,
        g_webSseEvictLastMs == 0 ? -1L
                                 : (long)(uint32_t)((uint32_t)uptimeMs - g_webSseEvictLastMs),
        (unsigned long)refusedHeapFloor, (unsigned long)refusedHeapFloorDiag,
        (unsigned long)g_webBusyRecoveryPagesServed,
        otaActive ? "true" : "false", (unsigned)otaProgressPct, otaLastError, wifiRssi,
        wifiConnected ? "true" : "false",
        wifiClientConnected ? "true" : "false", littleFsReady ? "true" : "false",
        sleepMode ? "true" : "false", (unsigned long)sleepSinceMs, (unsigned)activeMood,
        (unsigned)auxLedPin, (unsigned)auxLedR, (unsigned)auxLedG, (unsigned)auxLedB,
        auxLedEffectLabel, auxLedAvailable ? "true" : "false");

#if PA_WEB_BACKEND_PSYCHIC
    // Connection lifetime, emitted only on the stack that has one to report.
    // The async backend closes every response by construction, so a socket
    // count there would only restate the request count -- and adding a field
    // the recorded baseline scorecard never carried is exactly what the
    // preserved counter names above exist to avoid.
    //
    // httpRequestsServed against httpSocketsAccepted is the measurement: their
    // ratio is requests per connection, which is what "does this stack reuse
    // connections" actually means. httpSocketsOpenPeak is the other half --
    // reuse is only affordable if occupancy stays inside max_open_sockets.
    if (written > 0 && written < (int)bufferSize - 1) {
        const int extra =
            snprintf(buffer + written, bufferSize - (size_t)written,
                     ",\"httpSocketsAccepted\":%lu,\"httpSocketsOpen\":%d,"
                     "\"httpSocketsOpenPeak\":%d,\"httpSocketsUntracked\":%lu,"
                     "\"httpRequestsServed\":%lu",
                     (unsigned long)g_webSocketsAccepted, (int)g_webSocketsOpen,
                     (int)g_webSocketsOpenPeak, (unsigned long)g_webSocketsUntracked,
                     (unsigned long)g_webRequestsServed);
        if (extra > 0) {
            // Truncation leaves written past the buffer, which the bound check
            // below reads as a failed build -- the same way the fixed section
            // above reports its own overflow.
            written += extra;
        }
    }
#endif

    // Conditionally append enabled-component keys — disabled components are absent,
    // not emitted as false placeholders (Phase 3 status/dashboard contract).
    bool ok = written > 0 && written < (int)bufferSize - 1;
    if (ok) {
        char* pos = buffer + written;
        size_t remaining = bufferSize - (size_t)written;
        char detail[96];

        if (enableArm1) {
            snprintf(detail, sizeof(detail), "Target %u us", (unsigned)arm1TargetUs);
            ok = appendPeripheralStatus(pos, remaining, "arm1", "ready", detail) && ok;
        }
        if (enableArm2) {
            snprintf(detail, sizeof(detail), "Target %u us", (unsigned)arm2TargetUs);
            ok = appendPeripheralStatus(pos, remaining, "arm2", "ready", detail) && ok;
        }
        if (enableAux1) {
            ok = appendPeripheralStatus(pos, remaining, "aux1", "ready", "Servo channel enabled") &&
                 ok;
        }
        if (enableAux2) {
            ok = appendPeripheralStatus(pos, remaining, "aux2", "ready", "Servo channel enabled") &&
                 ok;
        }
        if (enableAux3) {
            ok = appendPeripheralStatus(pos, remaining, "aux3", "ready", "Servo channel enabled") &&
                 ok;
        }
        if (enableDome) {
            if (domeTargetSpeed > 0.001f || domeTargetSpeed < -0.001f) {
                snprintf(detail, sizeof(detail), "Target %.0f%%",
                         (double)(domeTargetSpeed * 100.0f));
                ok = appendPeripheralStatus(pos, remaining, "dome", "spinning", detail) && ok;
            } else {
                ok = appendPeripheralStatus(pos, remaining, "dome", "idle", "Target 0%") && ok;
            }
        }
        if (enableRcCh1 && !(rcInputMode == RC_INPUT_SINGLE_SBUS && singleSbusUseCh2)) {
            if (rcInputMode == RC_INPUT_STANDARD_PWM) {
                ok = appendPeripheralStatus(
                         pos, remaining, "rcCh1", "ready",
                         "Standard PWM input enabled; routing configurable via /api/config") &&
                     ok;
            } else if (lastSbus1Ms == 0) {
                ok = appendPeripheralStatus(pos, remaining, "rcCh1", "not_seen",
                                            "Drive SBUS input waiting for first frame") &&
                     ok;
            } else if (sbusSignalLost) {
                snprintf(detail, sizeof(detail),
                         "Drive SBUS lost, last %lu ms ago, lost frames %lu",
                         uptimeMs - lastSbus1Ms, (unsigned long)sbus1LostFrameCount);
                ok = appendPeripheralStatus(pos, remaining, "rcCh1", "signal_lost", detail) && ok;
            } else {
                snprintf(detail, sizeof(detail),
                         "Drive SBUS active, last %lu ms ago, lost frames %lu",
                         uptimeMs - lastSbus1Ms, (unsigned long)sbus1LostFrameCount);
                ok = appendPeripheralStatus(pos, remaining, "rcCh1", "active", detail) && ok;
            }
        }
        if (enableRcCh2) {
            if (rcInputMode == RC_INPUT_STANDARD_PWM) {
                ok = appendPeripheralStatus(
                         pos, remaining, "rcCh2", "ready",
                         "Standard PWM input enabled; routing configurable via /api/config") &&
                     ok;
            } else if (rcInputMode == RC_INPUT_SINGLE_SBUS && !singleSbusUseCh2) {
                ok = appendPeripheralStatus(
                         pos, remaining, "rcCh2", "standby",
                         "SBUS2 not selected; using SBUS1 (CH1) in single_sbus mode") &&
                     ok;
            } else if (lastSbus2Ms == 0) {
                ok = appendPeripheralStatus(pos, remaining, "rcCh2", "not_seen",
                                            "SBUS2 input waiting for first frame") &&
                     ok;
            } else if (sbus2SignalLost) {
                snprintf(detail, sizeof(detail), "SBUS2 lost, last %lu ms ago, lost frames %lu",
                         uptimeMs - lastSbus2Ms, (unsigned long)sbus2LostFrameCount);
                ok = appendPeripheralStatus(pos, remaining, "rcCh2", "signal_lost", detail) && ok;
            } else {
                snprintf(detail, sizeof(detail), "SBUS2 active, last %lu ms ago, lost frames %lu",
                         uptimeMs - lastSbus2Ms, (unsigned long)sbus2LostFrameCount);
                ok = appendPeripheralStatus(pos, remaining, "rcCh2", "active", detail) && ok;
            }
        }
        if (enableRcCh3) {
            snprintf(detail, sizeof(detail),
                     "CH3 enabled; %s routing is configurable via /api/config",
                     rcInputModeLabel(rcInputMode));
            ok = appendPeripheralStatus(pos, remaining, "rcCh3",
                                        rcInputMode == RC_INPUT_STANDARD_PWM ? "ready" : "standby",
                                        detail) &&
                 ok;
        }
        if (enableRcCh4) {
            snprintf(detail, sizeof(detail),
                     "CH4 enabled; %s routing is configurable via /api/config",
                     rcInputModeLabel(rcInputMode));
            ok = appendPeripheralStatus(pos, remaining, "rcCh4",
                                        rcInputMode == RC_INPUT_STANDARD_PWM ? "ready" : "standby",
                                        detail) &&
                 ok;
        }
        if (enableRcCh5) {
            snprintf(detail, sizeof(detail),
                     "CH5 enabled; %s routing is configurable via /api/config",
                     rcInputModeLabel(rcInputMode));
            ok = appendPeripheralStatus(pos, remaining, "rcCh5",
                                        rcInputMode == RC_INPUT_STANDARD_PWM ? "ready" : "standby",
                                        detail) &&
                 ok;
        }
        if (enableRcCh6) {
            snprintf(detail, sizeof(detail),
                     "CH6 enabled; %s routing is configurable via /api/config",
                     rcInputModeLabel(rcInputMode));
            ok = appendPeripheralStatus(pos, remaining, "rcCh6",
                                        rcInputMode == RC_INPUT_STANDARD_PWM ? "ready" : "standby",
                                        detail) &&
                 ok;
        }
        if (enableS1Hoverboard) {
            if (driveSpeed != 0 || driveSteer != 0) {
                snprintf(detail, sizeof(detail), "Command %d/%d", driveSpeed, driveSteer);
                ok = appendPeripheralStatus(pos, remaining, "s1Hoverboard", "commanding", detail) &&
                     ok;
            } else {
                ok = appendPeripheralStatus(pos, remaining, "s1Hoverboard", "idle",
                                            "No drive command requested") &&
                     ok;
            }
        }
        if (enableS2Sound) {
            const char* rxStatusText = audioRxStatusToken(audioRxStatus);
            const char* rxDetail = audioRxStatusDetail(audioRxStatus);
            int _n = snprintf(pos, remaining,
                              ",\"s2Sound\":{\"state\":\"%s\",\"detail\":\"%s\",\"driver\":\"%s\",\"link_ok\":%s,\"rx_status\":\"%s\",\"rx_detail\":\"%s\"}",
                              audioActive ? "playing" : "idle",
                              audioRxStatus == AUDIO_RX_BLOCKED_BY_DOME_UART ? rxDetail :
                                  (audioActive ? "Playback active" : "Ready, no active playback"),
                              audioGetDriverName(),
                              audioLinkOk ? "true" : "false", rxStatusText, rxDetail);
            if (_n > 0 && _n < (int)remaining) {
                pos += _n;
                remaining -= (size_t)_n;
            } else {
                ok = false;
            }
        }
        if (enableS3DomeCtrl) {
            const char* transportLabel = domeTransportLabel(domeActiveTransport);
            if (domeLastSeenMs == 0) {
                snprintf(detail, sizeof(detail),
                         "Heartbeat tx %lu, no protoR2link heartbeat seen yet (transport %s)",
                         (unsigned long)bodyHbTx, transportLabel);
                ok = appendPeripheralStatus(pos, remaining, "s3DomeCtrl", "not_seen", detail) && ok;
            } else if ((uptimeMs - domeLastSeenMs) < 5000UL) {
                snprintf(detail, sizeof(detail),
                         "Heartbeat rx %lu / tx %lu, last %lu ms ago (transport %s)",
                         (unsigned long)domeHbRx, (unsigned long)bodyHbTx,
                         uptimeMs - domeLastSeenMs, transportLabel);
                ok =
                    appendPeripheralStatus(pos, remaining, "s3DomeCtrl", "connected", detail) && ok;
            } else {
                snprintf(detail, sizeof(detail),
                         "Heartbeat rx %lu / tx %lu, last %lu ms ago (transport %s)",
                         (unsigned long)domeHbRx, (unsigned long)bodyHbTx,
                         uptimeMs - domeLastSeenMs, transportLabel);
                ok = appendPeripheralStatus(pos, remaining, "s3DomeCtrl", "lost", detail) && ok;
            }
        }

        // Top-level dome_link block — always present for external tooling,
        // regardless of whether the s3DomeCtrl component is enabled.
        // three states: connected (hb seen < 5s), lost (was seen, now > 5s), not_seen (never).
        {
            const char* dlState;
            const char* dlTransport = domeTransportLabel(domeActiveTransport);
            const char* dlUartOwner = "none";
            int32_t lastRxMs = -1;
            char dlDetail[96];
            switch (domeUartOwner) {
                case DOME_UART_DOME:
                    dlUartOwner = "dome";
                    break;
                case DOME_UART_AUDIO:
                    dlUartOwner = "audio";
                    break;
                case DOME_UART_NONE:
                default:
                    break;
            }
            if (!enableS3DomeCtrl) {
                dlState = "disabled";
                dlTransport = "none";
            } else if (domeLastSeenMs == 0) {
                dlState = "not_seen";
            } else if ((uptimeMs - domeLastSeenMs) < 5000UL) {
                dlState = "connected";
                lastRxMs = (int32_t)(uptimeMs - domeLastSeenMs);
            } else {
                dlState = "lost";
                lastRxMs = (int32_t)(uptimeMs - domeLastSeenMs);
            }
            snprintf(dlDetail, sizeof(dlDetail), "transport=%s, uart_owned=%s", dlTransport,
                     domeUartOwner == DOME_UART_DOME ? "true" : "false");
            char dlBuf[384];
            snprintf(dlBuf, sizeof(dlBuf),
                     ",\"dome_link\":{\"state\":\"%s\",\"transport\":\"%s\",\"detail\":\"%s\",\"hb_tx\":%lu,\"hb_rx\":%lu"
                     ",\"rx_overflow\":%lu,\"rx_unknown\":%lu,\"last_rx_ms\":%ld,\"uart_owner\":\"%s\",\"uart_owned_by_dome\":%s}",
                     dlState, dlTransport, dlDetail, (unsigned long)bodyHbTx,
                     (unsigned long)domeHbRx, (unsigned long)domeRxOverflowCount,
                     (unsigned long)domeRxUnknownCount, (long)lastRxMs, dlUartOwner,
                     domeUartOwner == DOME_UART_DOME ? "true" : "false");
            ok = appendJsonChunk(pos, remaining, dlBuf) && ok;
        }

        if (hbFeedbackValid) {
            char hbBuf[128];
            snprintf(hbBuf, sizeof(hbBuf),
                     ",\"hoverboard\":{\"batteryV\":%.2f,\"boardTempC\":%.1f"
                     ",\"speedR\":%d,\"speedL\":%d"
                     ",\"currentL\":%.2f,\"currentR\":%.2f}",
                     (double)(hbBatteryRaw / 100.0f), (double)(hbBoardTempRaw / 10.0f),
                     (int)hbSpeedR, (int)hbSpeedL, (double)(hbCurrentL / 100.0f),
                     (double)(hbCurrentR / 100.0f));
            ok = appendJsonChunk(pos, remaining, hbBuf) && ok;
        }

        ok = appendJsonChunk(pos, remaining, "}") && ok;
    }

    if (!ok) {
        snprintf(buffer, bufferSize, "{\"ok\":false,\"error\":\"status payload overflow\"}");
        return false;
    }
    return true;
}

bool webLittleFsMounted() {
    return littleFsReady;
}

bool webOtaActive() {
    return s_otaActive;
}

bool webServerHasSSEClients() {
    return webEventStreamClientCount() > 0;
}

#ifndef PA_WEB_BACKEND_PSYCHIC
// Event stream transport for the async scaffold (include/web_event_stream.h).
// AsyncEventSource owns the client list and the send here, so these are pure
// forwarding -- the bounded send and the eviction live on the psychic backend,
// which is where an unbounded one can actually be fixed (ADR 0020 found no safe
// cross-task close on AsyncTCP). The #91 cutover deletes this block with the
// rest of the scaffold.
// Called from eventStreamTask and, for the count, from any handler building the
// status payload on the async_tcp task. AsyncEventSource does its own locking.
size_t webEventStreamClientCount() {
    return events.count();
}

// eventStreamTask only, same as the psychic backend's.
void webEventStreamBroadcast(const char* event, const char* data, uint32_t id) {
    events.send(data, event, id);
}
#endif

// Shared SSE JSON buffers — file-scope so both eventStreamTask and the
// onConnect handler use the same allocation rather than each having their own.
// eventStreamTask runs at 1 Hz on Core 0; onConnect fires on the AsyncTCP
// task also on Core 0. They cannot run truly concurrently on the same core,
// so sharing these buffers is safe without additional locking.
// Combined saving vs previous approach (two sets of statics): 3 KB BSS.
// Status JSON can exceed 1 KB when many components are enabled; keep headroom.
static char s_sseStatusBody[3072];
// RC diagnostics JSON reaches ~2570 bytes in dual_sbus mode (2 sources + 7 analog
// channels + digital section + mapping profile + raw channel arrays).
// Keep a 3072-byte margin to avoid truncating SSE rc events.
static char s_sseRcBody[3072];
static JsonDocument s_sseRcDoc;
static bool s_rcSseBuildWarned = false;
static bool s_rcSseSizeWarned = false;
static bool s_statusSseOverflowWarned = false;
static portMUX_TYPE s_broadcastMux = portMUX_INITIALIZER_UNLOCKED;
static bool s_broadcastRequested = false;
static uint32_t s_lastLogSent = 0;
static char s_sseLogLines[8][LOG_LINE_MAX];
static char s_sseLogBatch[8 * (LOG_LINE_MAX + 8) + 1];
static int s_logSendTick = 0;

void requestStatusBroadcastNow() {
    taskENTER_CRITICAL(&s_broadcastMux);
    s_broadcastRequested = true;
    taskEXIT_CRITICAL(&s_broadcastMux);
}

void eventStreamTask(void*) {
    bool hwmLogged = false;
    bool hwmUnderLoadLogged = false;
    bool recoveryGestureCleared = false;
    for (;;) {
        if (!hwmLogged) {
            PA_LOG_DEBUG("WebEvents", "stack HWM: %u words free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Network Recovery Mode gesture: once this boot has run
        // stably past the gesture window, clear the persisted power-cycle
        // count so a single ordinary power cycle days from now does not
        // silently accumulate toward the next rapid-cycle gesture.
        if (!recoveryGestureCleared && millis() >= WIFI_RECOVERY_GESTURE_STABLE_MS) {
            recoveryGestureCleared = true;
            Preferences recoveryPrefs;
            if (recoveryPrefs.begin(NVS_NAMESPACE, false)) {
                if (recoveryPrefs.getUChar(kWifiRecoveryCycleKey, 0) != 0) {
                    recoveryPrefs.putUChar(kWifiRecoveryCycleKey, 0);
                    PA_LOG_DEBUG("WebEvents", "recovery gesture cycle count cleared after stable uptime");
                }
                recoveryPrefs.end();
            }
        }

        if (s_otaActive) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (serverStarted && webEventStreamClientCount() > 0) {
            uint32_t nowMs = millis();

            taskENTER_CRITICAL(&s_broadcastMux);
            bool broadcastRequested = s_broadcastRequested;
            if (broadcastRequested) {
                s_broadcastRequested = false;
            }
            taskEXIT_CRITICAL(&s_broadcastMux);

            if (broadcastRequested) {
                if (!buildStatusJson(s_sseStatusBody, sizeof(s_sseStatusBody))) {
                    if (!s_statusSseOverflowWarned) {
                        PA_LOG_WARN("WebEvents",
                                    "status SSE payload overflowed; sending fallback payload");
                        s_statusSseOverflowWarned = true;
                    }
                } else {
                    s_statusSseOverflowWarned = false;
                }
                webEventStreamBroadcast("status", s_sseStatusBody, nowMs);
            }

            RcDiagnosticsSnapshot rcSnap;
            captureRcDiagnosticsSnapshot(&rcSnap);
            s_sseRcDoc.clear();
            if (!populateRcDiagnosticsJson(s_sseRcDoc, rcSnap)) {
                if (!s_rcSseBuildWarned) {
                    PA_LOG_WARN("WebEvents", "rc SSE JSON build failed; event dropped");
                    s_rcSseBuildWarned = true;
                }
            } else {
                s_rcSseBuildWarned = false;
                size_t rcBytes = measureJson(s_sseRcDoc);
                if (rcBytes >= sizeof(s_sseRcBody)) {
                    if (!s_rcSseSizeWarned) {
                        PA_LOG_WARN("WebEvents",
                                    "rc SSE payload too large (%u bytes >= %u); event dropped",
                                    (unsigned)rcBytes, (unsigned)sizeof(s_sseRcBody));
                        s_rcSseSizeWarned = true;
                    }
                } else {
                    s_rcSseSizeWarned = false;
                    serializeJson(s_sseRcDoc, s_sseRcBody, sizeof(s_sseRcBody));
                    webEventStreamBroadcast("rc", s_sseRcBody, nowMs);
                }
            }
            if (!hwmUnderLoadLogged) {
                PA_LOG_DEBUG("WebEvents", "stack HWM under SSE load: %u words free",
                             (unsigned)uxTaskGetStackHighWaterMark(NULL));
                hwmUnderLoadLogged = true;
            }

            if (++s_logSendTick >= 2) {
                s_logSendTick = 0;
                size_t linesCopied = 0;
                s_lastLogSent = copyNewLogLinesSince(s_lastLogSent, s_sseLogLines, 8, &linesCopied);
                if (linesCopied > 0) {
                    size_t pos = 0;
                    for (size_t i = 0; i < linesCopied && pos < sizeof(s_sseLogBatch) - 1; ++i) {
                        if (i > 0) {
                            s_sseLogBatch[pos++] = '\x01';
                        }
                        size_t lineLen = strnlen(s_sseLogLines[i], LOG_LINE_MAX);
                        size_t room = sizeof(s_sseLogBatch) - 1 - pos;
                        size_t copy = lineLen < room ? lineLen : room;
                        memcpy(s_sseLogBatch + pos, s_sseLogLines[i], copy);
                        pos += copy;
                    }
                    s_sseLogBatch[pos] = '\0';
                    webEventStreamBroadcast("log", s_sseLogBatch, nowMs);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void startHttpServerOnce() {
    if (serverStarted) {
        return;
    }

#ifdef PA_USE_PSYCHICHTTP_PROTOTYPE
    // issue #72 prototype: PsychicHttp replaces ESPAsyncWebServer's HTTP
    // server entirely (both would try to bind port 80), swapped in at the
    // exact same trigger point production uses -- once WiFi has genuinely
    // come up, per handleWiFiEvent()'s two call sites below. WiFi bring-up
    // itself (webServerInit(), executeWifiBootPosture()) is untouched and
    // always runs normally; only the HTTP server started below differs.
    //
    // This used to be an early `return` right here, which also skipped
    // mDNS and ArduinoOTA startup further down in this same function --
    // the exact same "skip too much of a multi-purpose function" mistake
    // already made once for WiFi bring-up (see #72's findings doc). That
    // left nothing listening on port 3232, so every OTA "Sending invitation"
    // hung until timeout; only caught by actually trying to OTA-reflash,
    // not by any earlier compile or source-level check. Fixed by only
    // branching the HTTP-server-specific part; mDNS/OTA below now run
    // unconditionally for both backends.
    initPsychicHttpServer();
#elif defined(PA_WEB_BACKEND_PSYCHIC)
    // ADR 0021 dual-backend scaffold: the PsychicHttp backend replaces the
    // async HTTP server at the same WiFi-event trigger point (both would
    // bind port 80); mDNS/OTA below run unconditionally for both backends.
    initPsychicWebServer();
#else
    if (!routesRegistered) {
        events.onConnect([](AsyncEventSourceClient* client) {
            (void)client;
            // AsyncEventSource::_addClient() invokes this connect callback
            // before appending the new client to its internal list (see
            // _addClient() in AsyncEventSource.cpp: _connectcb(client) runs,
            // then _clients.emplace_back(client)) -- events.count() here is
            // therefore the count BEFORE this client is registered, so the
            // peak must account for the one being added now.
            if (events.count() + 1 > g_webSseClientsPeak) {
                g_webSseClientsPeak = events.count() + 1;
            }
            // MUST NOT call client->close() here: this callback runs inside
            // AsyncEventSourceClient's constructor (via _addClient), and a
            // synchronous close runs the disconnect path that nulls the
            // connection the constructor is still using -- panic, proven by
            // coredump twice -- and then leaks the half-dead client as a
            // permanent zombie entry in the client list, silently consuming
            // the SSE cap. Cap and heap admission for /api/events happen in
            // the middleware below, before the upgrade machinery ever runs.
            // Over-cap is still possible in a narrow race (a second upgrade
            // completing before the first registers); it is bounded to +1
            // and transient, which is preferable to a crash.
            if (events.count() >= kMaxSseClients) {
                PA_LOG_WARN(TAG, "SSE clients above cap (%u) after race; tolerating extra client",
                            (unsigned)kMaxSseClients);
            }
        });
        server.addHandler(&events);

        // Reject non-essential requests while heap is critically fragmented,
        // instead of letting every handler construct response objects that
        // may fail deep inside ESPAsyncWebServer. Estop and read-only
        // diagnostics (status/profiler) must always go through regardless of
        // heap state -- those are exactly what's needed to see what's
        // happening during a rejection window, and they're cheap enough not
        // to meaningfully add to the pressure.
        server.addMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
            const String& url = request->url();
            // Estop is the safety path: never rejected, never counted.
            if (url.startsWith("/api/estop")) {
                next();
                return;
            }
            // Long-lived SSE stream: not counted against the in-flight cap
            // (it would pin a slot for the connection's whole lifetime), but
            // its client cap is enforced here -- rejecting pre-upgrade with
            // abort() is safe, whereas closing the client from
            // events.onConnect crashes mid-constructor (see the onConnect
            // comment above).
            const bool sse = url == "/api/events";
            if (sse && events.count() >= kMaxSseClients) {
                PA_LOG_WARN(TAG, "SSE client cap (%u) reached; rejecting new connection",
                            (unsigned)kMaxSseClients);
                g_webRefusedSseCap = g_webRefusedSseCap + 1u;
                request->abort();
                return;
            }
            if (!sse && s_inflightRequests >= kMaxInflightRequests) {
                // abort() is the only rejection that is safe under pressure:
                // a 503 with a body still constructs a full response, whose
                // constructor unconditionally adds a Connection header via a
                // std::list<AsyncWebHeader> node allocation -- a separate
                // allocation site from the response object itself, not
                // covered by the vendor's nothrow fixes, and the proven
                // abort() site of the burst crashes. Closing the socket
                // allocates nothing.
                s_refusedInflightCap++;
                request->abort();
                return;
            }
            // SSE counts as diagnostic: it is the operator's primary
            // liveness/telemetry channel, one long-lived connection bounded
            // by its own client cap, and shedding it during warm-heap dips
            // left dashboards without live data while plain status polls
            // still worked.
            const bool diagnostic = sse || url == "/api/status" || url == "/api/profiler" ||
                                    url == "/api/coredump";
            size_t largestBlock = largestFreeBlock8Bit();
            const size_t floor =
                diagnostic ? kMinLargestFreeBlockForDiagnostics : kMinLargestFreeBlockForNewWork;
            if (largestBlock < floor) {
                if (diagnostic) {
                    s_refusedHeapFloorDiag++;
                } else {
                    // Diagnostic rejections stay silent: they only occur
                    // during a pressure storm, exactly when log volume
                    // itself is unwelcome.
                    PA_LOG_WARN(TAG, "rejecting %s: largest free block %u < %u",
                                url.c_str(), (unsigned)largestBlock, (unsigned)floor);
                    s_refusedHeapFloor++;
                }
                request->abort();
                return;
            }
#if PA_HEAP_PROFILE
            uint32_t traceStartMs = 0;
            bool traced = false;
            uint8_t traceIdx = 0;
#endif
            if (!sse) {
                s_inflightRequests++;
                if (s_inflightRequests > s_peakInflightRequests) {
                    s_peakInflightRequests = s_inflightRequests;
                }
#if PA_HEAP_PROFILE
                traceStartMs = millis();
                // Full path (not just a broad class) so a specific slow/hung
                // request (issue #67) can be matched against the browser's
                // own per-request timestamps after the fact.
                traceIdx = pushRequestTraceEntry(url.c_str(), traceStartMs);
                traced = true;
#endif
                request->onDisconnect([
#if PA_HEAP_PROFILE
                                           traced, traceIdx
#endif
                ]() {
                    if (s_inflightRequests > 0) {
                        s_inflightRequests--;
                    }
#if PA_HEAP_PROFILE
                    if (traced) {
                        s_requestTrace[traceIdx].disconnectMs = millis();
                    }
#endif
                });
            }
            next();
#if PA_HEAP_PROFILE
            if (traced) {
                s_requestTrace[traceIdx].handlerDoneMs = millis();
            }
#endif
        });

        // ADR 0021: route groups ported to the WebRequest seam register
        // through webRegisterRoute() against this server instance. Their
        // paths live in the one seam route table both backends call, so this
        // block only registers what is still async-only.
        webRequestAsyncAttach(server);
        webRegisterSeamRoutes();

        registerStatusRoutes(server);
#if PA_HEAP_PROFILE
        registerProfilerRoutes(server);
#endif
        registerActionsRoutes(server);
        registerSeqRoutes(server);

        if (littleFsReady) {
            server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
        }

        routesRegistered = true;
    }

    server.begin();
#endif
    serverStarted = true;
    PA_LOG_INFO(TAG, "HTTP server started on port 80");

    if (!mdnsStarted && WiFi.status() == WL_CONNECTED) {
        char hostname[DROID_NAME_MAX_LEN + 1] = {};
        configCacheResolvedMdnsHostname(hostname, sizeof(hostname));
        mdnsStarted = MDNS.begin(hostname);
        if (mdnsStarted) {
            MDNS.enableArduino(3232, false);
            PA_LOG_INFO(TAG, "mDNS ready as %s.local", hostname);
        } else {
            PA_LOG_WARN(TAG, "mDNS init failed for host %s", hostname);
        }
    }

    // Start OTA task in background — MUST NOT block WiFi event handler (causes TWDT)
    if (!otaTaskStarted) {
        xTaskCreatePinnedToCore(
            [](void*) {
                // Delay OTA init to let WiFi event handler complete first
                vTaskDelay(pdMS_TO_TICKS(500));

                char hostname[DROID_NAME_MAX_LEN + 1] = {};
                configCacheResolvedMdnsHostname(hostname, sizeof(hostname));
                ArduinoOTA.setHostname(hostname);
                ArduinoOTA.setMdnsEnabled(false);
                ArduinoOTA.setTimeout(OTA_RECEIVE_TIMEOUT_MS);
                ArduinoOTA.onStart([]() {
                    const char* type =
                        (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
                    s_otaActive = true;
                    s_otaProgressPct = 0;
                    s_lastOtaLoggedPct = 255;
                    snprintf(s_otaLastError, sizeof(s_otaLastError), "%s", "none");
                    PA_LOG_INFO(TAG, "ArduinoOTA start: %s", type);
                    logOtaHeapCheckpoint("start");
                });
                ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
                    if (total == 0) {
                        return;
                    }
                    uint8_t pct = (uint8_t)((progress * 100U) / total);
                    if (pct > 100U) {
                        pct = 100U;
                    }
                    s_otaProgressPct = pct;
                    if (s_lastOtaLoggedPct == 255 || pct == 100U ||
                        pct >= (uint8_t)(s_lastOtaLoggedPct + 10U)) {
                        s_lastOtaLoggedPct = pct;
                        PA_LOG_INFO("ArduinoOTA",
                                    "progress %u%% heap free=%lu min=%lu largest8bit=%lu",
                                    (unsigned)pct,
                                    (unsigned long)ESP.getFreeHeap(),
                                    (unsigned long)ESP.getMinFreeHeap(),
                                    (unsigned long)largestFreeBlock8Bit());
                    }
                });
                ArduinoOTA.onEnd([]() {
                    logOtaHeapCheckpoint("complete");
                    s_otaProgressPct = 100;
                    s_otaActive = false;
                    s_lastOtaLoggedPct = 255;
                    snprintf(s_otaLastError, sizeof(s_otaLastError), "%s", "none");
                    PA_LOG_INFO(TAG, "ArduinoOTA complete");
                });
                ArduinoOTA.onError([](ota_error_t error) {
                    unsigned int updateError = 0;
                    const char* updateErrorText = "unavailable";
#ifdef ARDUINO
                    updateError = Update.getError();
                    updateErrorText = Update.errorString();
#endif
                    logOtaHeapCheckpoint("error");
                    snprintf(s_otaLastError, sizeof(s_otaLastError), "arduino:%d update:%u",
                             (int)error, updateError);
                    s_otaActive = false;
                    s_lastOtaLoggedPct = 255;
                    PA_LOG_ERROR(TAG, "ArduinoOTA error: %d update=%u %s", (int)error,
                                 updateError, updateErrorText);
                });
                ArduinoOTA.begin();
                PA_LOG_INFO(TAG, "ArduinoOTA ready on port 3232 as %s", hostname);

                for (;;) {
                    ArduinoOTA.handle();
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            },
            "ArduinoOTA", 4096, nullptr, 1, nullptr, 0);
        otaTaskStarted = true;
    }
}

void handleWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_START:
            PA_LOG_INFO(TAG, "Hotspot started - SSID: %s  IP: %s", WiFi.softAPSSID().c_str(),
                        WiFi.softAPIP().toString().c_str());
            startHttpServerOnce();
            break;
        case ARDUINO_EVENT_WIFI_STA_START:
            PA_LOG_INFO(TAG, "Connecting to WiFi network...");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            PA_LOG_INFO(TAG, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
            startHttpServerOnce();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            // Ordinary WiFi Client Mode connection trouble stays visible as a
            // client-mode problem (ADR 0015). It must never trigger automatic
            // AP fallback here — wifiDecideBootPosture() has no connectivity
            // input, so there is nothing to re-decide on disconnect.
            PA_LOG_INFO(TAG, "WiFi connection lost");
            break;
        default:
            break;
    }
}

// buildDeveloperShortcut: the Developer WiFi Shortcut (ADR 0015) resolved from
// src/secrets.h, source-build-only. Never populated in public release binaries —
// `available` stays false whenever secrets.h is absent or leaves its expected
// macros undefined, which is always true for protoArtoo_chirp/protoArtoo_mp3trigger.
static WifiDeveloperShortcut buildDeveloperShortcut() {
    WifiDeveloperShortcut shortcut;
#if PA_HAS_SECRETS_HEADER
#if PA_ENABLE_STA_WIFI
#if defined(PA_STA_SSID) && defined(PA_STA_PASSWORD)
    shortcut.available = true;
    shortcut.mode = WifiMode::CLIENT;
#endif
#else
#if defined(PA_AP_PASSWORD)
    shortcut.available = true;
    shortcut.mode = WifiMode::STANDALONE_AP;
#endif
#endif  // PA_ENABLE_STA_WIFI
#endif  // PA_HAS_SECRETS_HEADER
    return shortcut;
}

// executeWifiBootPosture: enters the posture wifiDecideBootPosture() returned.
// This function decides HOW to enter a posture; it never re-derives WHICH
// posture to enter (that decision already happened, and stays pure/testable).
static void executeWifiBootPosture(WifiBootPosture posture, const WifiConfig& settings) {
    switch (posture) {
        case WifiBootPosture::PROVISIONING:
        case WifiBootPosture::NETWORK_RECOVERY:
            // Both postures expose WiFi Provisioning with the documented Default AP
            // Credential — recovery must stay reachable even if the operator no
            // longer remembers a custom Standalone AP Mode password.
            WiFi.mode(WIFI_AP);
            WiFi.softAP(WIFI_AP_SSID, WIFI_DEFAULT_AP_PASSWORD);
            PA_LOG_INFO(TAG, "WiFi bootstrap: %s (AP %s)",
                        posture == WifiBootPosture::NETWORK_RECOVERY ? "network recovery"
                                                                      : "provisioning",
                        WIFI_AP_SSID);
            break;
        case WifiBootPosture::CLIENT_MODE: {
            const char* ssid = settings.sta_ssid;
            const char* password = settings.sta_password;
#if PA_HAS_SECRETS_HEADER && defined(PA_STA_SSID) && defined(PA_STA_PASSWORD)
            // Developer WiFi Shortcut: an unprovisioned controller has no saved
            // STA credentials, so a self-build falls back to secrets.h defaults.
            if (ssid[0] == '\0') {
                ssid = PA_STA_SSID;
                password = PA_STA_PASSWORD;
            }
#endif
            WiFi.mode(WIFI_STA);
            WiFi.begin(ssid, password);
            PA_LOG_INFO(TAG, "WiFi bootstrap: client mode (SSID %s)", ssid);
            break;
        }
        case WifiBootPosture::STANDALONE_AP_MODE:
            WiFi.mode(WIFI_AP);
            WiFi.softAP(settings.ap_ssid, settings.ap_password);
            PA_LOG_INFO(TAG, "WiFi bootstrap: standalone AP mode (SSID %s)", settings.ap_ssid);
            break;
    }
}

// evaluateNetworkRecoveryGesture: reads the persisted power-cycle count,
// runs it through the pure gesture rule (wifi_recovery_gesture.h), and
// persists the updated count. Only a true power-on reset advances the
// gesture; watchdog/panic/brownout/software resets fall through to
// wifiEvaluateRecoveryGesture()'s "reset the count" branch. Returns true
// only on the boot that latches Network Recovery Mode.
static bool evaluateNetworkRecoveryGesture() {
    Preferences recoveryPrefs;
    if (!recoveryPrefs.begin(NVS_NAMESPACE, false)) {
        PA_LOG_WARN(TAG, "recovery gesture NVS open failed; gesture unavailable this boot");
        return false;
    }

    WifiRecoveryGestureInput gestureInput;
    gestureInput.wasPowerOnReset = (esp_reset_reason() == ESP_RST_POWERON);
    gestureInput.priorCycleCount = recoveryPrefs.getUChar(kWifiRecoveryCycleKey, 0);

    WifiRecoveryGestureResult gestureResult = wifiEvaluateRecoveryGesture(gestureInput);
    recoveryPrefs.putUChar(kWifiRecoveryCycleKey, gestureResult.nextCycleCount);
    recoveryPrefs.end();

    if (gestureResult.recoveryRequested) {
        PA_LOG_WARN(TAG,
                    "Network Recovery Mode gesture detected (%u power cycles) - "
                    "starting WiFi Provisioning",
                    (unsigned)WIFI_RECOVERY_GESTURE_THRESHOLD);
    } else if (gestureInput.wasPowerOnReset && gestureResult.nextCycleCount > 0) {
        PA_LOG_DEBUG(TAG, "power-on reset cycle count = %u/%u",
                     (unsigned)gestureResult.nextCycleCount,
                     (unsigned)WIFI_RECOVERY_GESTURE_THRESHOLD);
    }
    return gestureResult.recoveryRequested;
}

void webServerInit() {
    if (routesRegistered || serverStarted) {
        PA_LOG_DEBUG(TAG, "web bootstrap already initialised");
        return;
    }

    littleFsReady = LittleFS.begin(true);
    if (littleFsReady) {
        PA_LOG_INFO(TAG, "filesystem ready");
    } else {
        PA_LOG_ERROR(TAG, "LittleFS mount failed - API only mode");
    }

    loadFsVersion();

    WiFi.onEvent(handleWiFiEvent);

    if (!eventTaskStarted) {
        // Keep 6144 bytes for status/rc/log SSE work and JSON serialization headroom.
        // A previous 2048-byte reduction overflowed on client connect; 4096 also
        // overflowed (DoubleException in _dtoa_r float formatting) once
        // requestStatusBroadcastNow() call sites grew from rare hardware edges to
        // every web write handler, raising buildStatusJson() call frequency here.
        xTaskCreatePinnedToCore(eventStreamTask, "WebEvents", 6144, nullptr, 1, nullptr, 0);
        eventTaskStarted = true;
    }

    // ADR 0015: boot posture is decided once from Device WiFi Settings plus the
    // Developer WiFi Shortcut, through the same pure decision layer the native
    // tests exercise (test_wifi_boot_decision). networkRecoveryRequested comes
    // from the explicit local power-cycle gesture (wifi_recovery_gesture.h)
    // — this shell classifies the reset reason and
    // owns the persisted counter, but never infers recovery from ordinary STA
    // connection trouble.
    WifiConfig wifiSettings = {};
    configCacheReadWifi(&wifiSettings);

    WifiBootDecisionInput decisionInput;
    decisionInput.settings = wifiSettings;
    decisionInput.networkRecoveryRequested = evaluateNetworkRecoveryGesture();
    decisionInput.developerShortcut = buildDeveloperShortcut();

    WifiBootPosture posture = wifiDecideBootPosture(decisionInput);
    executeWifiBootPosture(posture, wifiSettings);

    // Record what was actually applied so the read surface can distinguish
    // active settings from any pending (saved-but-not-yet-applied) settings
    // for a Staged Network Switch (ADR 0015).
    configCacheSetActiveWifi(wifiSettings);
    configCacheSetActiveWifiRecovery(posture == WifiBootPosture::NETWORK_RECOVERY);
}
