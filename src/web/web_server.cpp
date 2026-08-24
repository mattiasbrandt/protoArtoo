// =============================================================================
// src/web/web_server.cpp
//
// WiFi and HTTP server bootstrap for protoArtoo.
// =============================================================================

#include "../../include/web_server.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
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
#include "../../include/api_seq.h"
#include "../../include/api_status.h"
#include "../../include/api_system.h"
#include "../../include/audio_task.h"
#include "../../include/reset_reason.h"
#include "../../include/config.h"
#include "../../include/config_cache.h"
#include "../../include/aux_led.h"
#include "../../include/rc_diagnostics_snapshot.h"
#include "../../include/robot_state.h"
#include "../../include/web_admission.h"
#include "../../include/web_event_stream.h"
#include "../../include/web_response_deadline.h"
#include "../../include/web_request.h"
#include "../../include/web_server_psychic.h"
#include "../../include/web_network_bootstrap.h"

// src/secrets.h is the Developer WiFi Shortcut (ADR 0015): local/self-build-only
// compile-time WiFi defaults. It is never required to compile or boot - public
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

static const char* TAG = "WebServer";
bool littleFsReady = false;

// Admission control lives entirely on the serving backend now
// (src/web/web_request_psychic.cpp, against the pure decision core in
// include/web_admission.h). Its counters -- inflight depth, refusals by class,
// accept-guard rejections -- are the project-owned globals declared there and
// in include/web_event_stream.h; this file only reads them for /api/status.

// Profiler-only request lifecycle storage is owned by api_profiler.cpp. The
// admission middleware reaches it through the opaque api_profiler.h interface,
// which compiles away in ordinary images.

#ifdef ARDUINO
static size_t largestFreeBlock8Bit() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}
#else
static size_t largestFreeBlock8Bit() {
    return SIZE_MAX;
}
#endif

// Sized for the longest stamp the version scheme composes:
// fs-v<release-tag>-<count>-g<sha>[-dirty][+<branch-suffix>], e.g.
// fs-v1.0.0-alpha.1-837-g401530a+phase-v1.0.0 (43 chars). 128 leaves room for
// longer branch names on both ends without the copy in loadFsVersion() ever
// truncating the identity acceptance runs verify.
static constexpr size_t kVersionStampMax = 128;
static char s_fsVersion[kVersionStampMax] = "unknown";
bool serverStarted = false;
bool eventTaskStarted = false;
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
const char* kWifiRecoveryCycleKey = "wifiRecovN";
const uint32_t WIFI_RECOVERY_GESTURE_STABLE_MS = 20000;

namespace {

// MALLOC_CAP_INTERNAL - dominated by a constant ~36 KB leftover-IRAM block
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
        // Error, not warning: a stamp that outgrows kVersionStampMax means the
        // version scheme itself changed, and a truncated stamp blinds the
        // flashed-build identity check acceptance runs rely on.
        PA_LOG_ERROR(TAG, "fsVersion truncated to %u chars; version scheme outgrew the buffer",
                     (unsigned)(sizeof(s_fsVersion) - 1));
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
    RcInputActiveConfig activeRc = {};
    configCacheReadActiveRcInput(&activeRc);
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
    enableRcCh1 = activeRc.enableRc[0];
    enableRcCh2 = activeRc.enableRc[1];
    enableRcCh3 = activeRc.enableRc[2];
    enableRcCh4 = activeRc.enableRc[3];
    enableRcCh5 = activeRc.enableRc[4];
    enableRcCh6 = activeRc.enableRc[5];
    rcInputMode = static_cast<RcInputMode>(activeRc.mode);
    singleSbusUseCh2 = activeRc.useCh2;
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

    // Admission evidence, read from the project-owned counters the serving
    // backend writes (include/web_admission.h). The JSON field names below are
    // a comparability contract with the recorded baseline and the load
    // harness, not a description of which implementation produced them --
    // which is why they are unchanged by the cutover that removed the other
    // implementation.
    const uint32_t acceptRejectHeap = g_webAcceptRejectHeap;
    const uint32_t acceptRejectRate = g_webAcceptRejectRate;
    const uint32_t acceptRejectLastMs = g_webAcceptRejectLastMs;
    const int inflightRequests = g_webInflightRequests;
    const int inflightRequestsPeak = g_webInflightRequestsPeak;
    const uint32_t refusedInflightCap = g_webRefusedInflightCap;
    const uint32_t refusedHeapFloor = g_webRefusedHeapFloor;
    const uint32_t refusedHeapFloorDiag = g_webRefusedHeapFloorDiag;

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

    // Connection lifetime. httpRequestsServed against httpSocketsAccepted is
    // the measurement: their ratio is requests per connection, which is what
    // "does this stack reuse connections" actually means (ADR 0023).
    // httpSocketsOpenPeak is the other half -- reuse is only affordable if
    // occupancy stays inside max_open_sockets.
    //
    // responseMaxMs is the reading the response-phase deadline is calibrated
    // against: the longest response phase seen this boot. It is published on
    // every run rather than measured once, because "does the deadline still
    // clear the slowest legitimate response" is a standing property of the
    // system, not a past result (ADR 0024). responseDeadlineAgeMs follows
    // sseEvictAgeMs: -1 until one has fired.
    //
    // sendRetriesMemory is the one to watch: it counts writes that had to wait
    // because the stack could not allocate a segment, which is the condition
    // that used to abandon a response mid-body and hand the browser a
    // well-formed but truncated file (prior async backend failure mode). It is
    // now retried rather than fatal, so the failure is invisible from the
    // outside -- this counter is the only place the pressure still shows.
    if (written > 0 && written < (int)bufferSize - 1) {
        const uint32_t responseDeadlineLastMs = g_webResponseDeadlineLastMs;
        const long responseDeadlineAgeMs =
            g_webResponseDeadlineClosures == 0 ? -1L : (long)(millis() - responseDeadlineLastMs);
        const int extra =
            snprintf(buffer + written, bufferSize - (size_t)written,
                     ",\"httpSocketsAccepted\":%lu,\"httpSocketsOpen\":%d,"
                     "\"httpSocketsOpenPeak\":%d,\"httpSocketsUntracked\":%lu,"
                     "\"httpRequestsServed\":%lu,\"responseDeadlineClosures\":%lu,"
                     "\"responseDeadlineAgeMs\":%ld,\"responseLastMs\":%lu,"
                     "\"responseMaxMs\":%lu,\"sendRetriesWindow\":%lu,"
                     "\"sendRetriesMemory\":%lu,\"sendRetryMaxMs\":%lu",
                     (unsigned long)g_webSocketsAccepted, (int)g_webSocketsOpen,
                     (int)g_webSocketsOpenPeak, (unsigned long)g_webSocketsUntracked,
                     (unsigned long)g_webRequestsServed,
                     (unsigned long)g_webResponseDeadlineClosures, responseDeadlineAgeMs,
                     (unsigned long)g_webResponseLastMs, (unsigned long)g_webResponseMaxMs,
                     (unsigned long)g_webSendRetriesWindow,
                     (unsigned long)g_webSendRetriesMemory,
                     (unsigned long)g_webSendRetryMaxMs);
        if (extra > 0) {
            // Truncation leaves written past the buffer, which the bound check
            // below reads as a failed build -- the same way the fixed section
            // above reports its own overflow.
            written += extra;
        }
    }

    // Conditionally append enabled-component keys - disabled components are absent,
    // not emitted as false placeholders (status/dashboard contract).
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

        // Top-level dome_link block - always present for external tooling,
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

// Shared SSE JSON buffers - file-scope so every producer in this file uses the
// same allocation rather than each having their own.
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

    // The HTTP server starts here, on the WiFi event callback path, never
    // directly from setup(); mDNS and ArduinoOTA below start alongside it and
    // are not part of the HTTP server's own bring-up.
    initPsychicWebServer();

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
            PA_LOG_ERROR(TAG, "mDNS init failed for host %s", hostname);
        }
    }

    // Start OTA task in background - MUST NOT block WiFi event handler (causes TWDT)
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

void webServerInit() {
    if (serverStarted) {
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

    // Set up the WiFi event handler (defined in web_network_bootstrap.cpp)
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

    // Network bootstrap: evaluate recovery gesture, build developer shortcut,
    // decide boot posture, and apply it (defined in web_network_bootstrap.cpp).
    webNetworkBootstrap();
}
