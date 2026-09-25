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
#include "../../include/audio_sound_member.h"
#include "../../include/reset_reason.h"
#include "../../include/config.h"
#include "../../include/config_cache.h"
#include "../../include/failed_alloc_tracker.h"
#include "../../include/api_aux_led.h"  // LitWireReading, formatLitWiresJson()
#include "../../include/aux_led.h"
#include "../../include/board_outputs.h"
#include "../../include/rc_diagnostics_snapshot.h"
#include "../../include/robot_state.h"
#include "../../include/status_json.h"
#include "../../include/web_event_stream.h"
#include "../../include/web_request.h"
#include "../../include/web_server_psychic.h"
#include "../../include/web_network_bootstrap.h"
#include "../../include/web_network_manager.h"
#include "../../include/wifi_recovery_gesture.h"

// hosted_link_status.h is only meaningful (and only defined, by
// web_network_manager_hosted.cpp) on boards with the ESP-Hosted backend; the
// call site in captureStatusJsonInputs() below is guarded by the same
// capability gate, so a board without it never references the undefined symbol.
#if PA_CAP_HOSTED_WIFI
#include "../../include/hosted_link_status.h"
#endif

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
// in include/web_event_stream.h; formatStatusJson() (src/web/status_json.cpp)
// reads them for /api/status.

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

// Sized for the longest stamp the version scheme composes (include/status_json.h),
// so the copy in loadFsVersion() never truncates the identity acceptance runs
// verify.
static char s_fsVersion[STATUS_VERSION_STAMP_MAX] = "unknown";
bool serverStarted = false;
bool eventTaskStarted = false;
static bool otaTaskStarted = false;
static bool mdnsStarted = false;
static constexpr int OTA_RECEIVE_TIMEOUT_MS = 15000;
static volatile bool s_otaActive = false;
static volatile uint8_t s_otaProgressPct = 255;
static uint8_t s_lastOtaLoggedPct = 255;
static char s_otaLastError[64] = "none";

// Network Recovery Mode local entry gesture (ADR 0015). The count is
// persisted under NVS_NAMESPACE so it survives the reboot(s) the gesture itself
// requires, and is cleared once uptime confirms the boot was not part of a
// rapid power-cycle sequence. See wifi_recovery_gesture.cpp for
// kWifiRecoveryCycleKey and the decision rule in include/wifi_recovery_gesture.h.
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
        // Error, not warning: a stamp that outgrows STATUS_VERSION_STAMP_MAX means the
        // version scheme itself changed, and a truncated stamp blinds the
        // flashed-build identity check acceptance runs rely on.
        PA_LOG_ERROR(TAG, "fsVersion truncated to %u chars; version scheme outgrew the buffer",
                     (unsigned)(sizeof(s_fsVersion) - 1));
    }
#endif
}

}  // namespace

// The capture half of the status document: everything formatStatusJson()
// (src/web/status_json.cpp) writes that is not a web admission counter, read
// once, in the order this builder has always read it.
static void captureStatusJsonInputs(StatusJsonInputs* in) {
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    RcInputActiveConfig activeRc = {};
    configCacheReadActiveRcInput(&activeRc);

    // dome.status.current's two console-queryable fields (ADR 0036) come from
    // captureDomeStatusSnapshot() (api_status_serializers.cpp) instead of the
    // inline reads this block used before #223, so the Console module and this
    // JSON builder can never disagree about what "domeTargetSpeed"/"domeEnabled"
    // mean. It opens its own short critical section rather than being folded
    // into the block below: RobotState's spinlock is reentrant on this target,
    // but nesting would tie this function's one big lock to that helper's
    // internals, and the two extra fields are read microseconds apart from the
    // other ~60 either way - no consumer of this payload depends on a single
    // atomic instant across all fields (see the independent buildHealthJson/
    // buildWifiJson/buildSerialJson reads elsewhere in the same file/pair).
    DomeStatusSnapshot domeSnap = {};
    captureDomeStatusSnapshot(&domeSnap);
    in->domeTargetSpeed = domeSnap.domeTargetSpeed;
    in->enableDome = domeSnap.domeEnabled;

    taskENTER_CRITICAL(&robotStateMux);
    copyFailsafeDiagnosticsLocked(&in->diag);
    in->webControlEnabled = robotState.webControlEnabled;
    in->sbus2SignalLost = robotState.sbus2SignalLost;
    in->driveSpeed = robotState.driveOutputSpeed;
    in->driveSteer = robotState.driveOutputSteer;
    in->speedLimitMax = cfg.drive.speedLimitMax;
    in->speedPresetActive = normalizeSpeedPresetId((uint8_t)cfg.drive.speedPresetActive);
    in->stationary = robotState.stationary;
    // The width on the pin, which is what the "Target" detail below has always
    // reported; the commanded target of a move in progress is the Parts
    // table's to show (captureServoOutputCommanded(), #362).
    in->arm1TargetUs = robotState.servoCommanded[0].nowUs;
    in->arm2TargetUs = robotState.servoCommanded[1].nowUs;
    in->lastSbus1Ms = robotState.lastSbus1Ms;
    in->lastSbus2Ms = robotState.lastSbus2Ms;
    in->sbus1LostFrameCount = robotState.sbus1LostFrameCount;
    in->sbus2LostFrameCount = robotState.sbus2LostFrameCount;
    in->queueOverflowCount = robotState.queueOverflowCount;
    in->domeHbRx = robotState.domeHbRx;
    in->bodyHbTx = robotState.bodyHbTx;
    in->domeLastSeenMs = robotState.domeLastSeenMs;
    in->domeRxOverflowCount = robotState.domeRxOverflowCount;
    in->domeRxUnknownCount = robotState.domeRxUnknownCount;
    in->domeActiveTransport = robotState.domeActiveTransport;
    in->domeUartOwner = robotState.domeUartOwner;
    in->fbBatteryRaw = robotState.driveFeedbackBatteryRaw;
    in->fbBoardTempRaw = robotState.driveFeedbackBoardTempRaw;
    in->fbSpeedR = robotState.driveFeedbackSpeedR;
    in->fbSpeedL = robotState.driveFeedbackSpeedL;
    in->fbCurrentL = robotState.driveFeedbackCurrentL;
    in->fbCurrentR = robotState.driveFeedbackCurrentR;
    in->fbValid = robotState.driveFeedbackValid;
    in->enableArm1 = cfg.system.enable_arm1;
    in->enableArm2 = cfg.system.enable_arm2;
    in->enableAux1 = cfg.system.enable_aux1;
    in->enableAux2 = cfg.system.enable_aux2;
    in->enableAux3 = cfg.system.enable_aux3;
    in->enableRcCh1 = activeRc.enableRc[0];
    in->enableRcCh2 = activeRc.enableRc[1];
    in->enableRcCh3 = activeRc.enableRc[2];
    in->enableRcCh4 = activeRc.enableRc[3];
    in->enableRcCh5 = activeRc.enableRc[4];
    in->enableRcCh6 = activeRc.enableRc[5];
    in->rcInputMode = static_cast<RcInputMode>(activeRc.mode);
    in->singleSbusUseCh2 = activeRc.useCh2;
    in->enableS1Hoverboard = cfg.system.enable_drive;
    in->enableS2Sound = cfg.system.enable_audio;
    in->enableS3DomeCtrl = cfg.system.enable_protor2link;
    in->audioActive = robotState.audioActive;
    in->audioLinkOk = robotState.audio_module_link_ok;
    in->audioRxStatus = robotState.audio_module_rx_status;
    in->activeMood = robotState.activeMood;
    in->sleepMode = robotState.sleepMode;
    in->sleepSinceMs = robotState.sleepSinceMs;
    // The droid's lit wires, read inside the same critical section as
    // everything else here so one frame is one consistent reading (#413).
    in->litWireCount = 0;
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        if (!robotState.auxLed[i].lit || in->litWireCount >= BOARD_OUTPUT_LIGHT_CAPABLE_COUNT) {
            continue;
        }
        LitWireReading& wire = in->litWires[in->litWireCount];
        wire.id = BOARD_OUTPUTS[i].id;
        wire.r = robotState.auxLed[i].r;
        wire.g = robotState.auxLed[i].g;
        wire.b = robotState.auxLed[i].b;
        wire.effect = auxLedEffectToString(robotState.auxLed[i].effect);
        wire.available = robotState.auxLed[i].available;
        ++in->litWireCount;
    }
    taskEXIT_CRITICAL(&robotStateMux);
    in->uptimeMs = millis();
    in->heapFree = ESP.getFreeHeap();
    in->heapMin = ESP.getMinFreeHeap();
    in->heapLargestBlock = webHeapMaxAlloc();
    // Why each of the next three is published is said where it is written
    // (src/web/status_json.cpp).
    in->heapLargest8bit = (uint32_t)largestFreeBlock8Bit();
    in->failedAllocs = failedAllocTrackerCount();
    in->sseClients = (unsigned)webEventStreamClientCount();
    in->firmwareVersion = PA_FIRMWARE_VERSION;
    in->fsVersion = s_fsVersion;
    in->resetReason = resetReasonName(esp_reset_reason());
    in->otaActive = s_otaActive;
    in->otaProgressPct = s_otaProgressPct;
    snprintf(in->otaLastError, sizeof(in->otaLastError), "%s", s_otaLastError);
    // Query WiFi connectivity status through the seam
    WifiConnectivityStatus connectivity = networkManagerQueryConnectivity();
    in->wifiConnected = connectivity.wifiConnected;
    in->wifiClientConnected = connectivity.wifiClientConnected;
    in->wifiRssi = connectivity.wifiRssi;
    in->littleFsReady = littleFsReady;
    in->sound = audioSoundStatusIdentity();
#if PA_CAP_HOSTED_WIFI
    in->hostedLink = hostedLinkQueryStatus();
#endif
}

bool buildStatusJson(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return false;
    }
    StatusJsonInputs in = {};
    captureStatusJsonInputs(&in);
    return formatStatusJson(buffer, bufferSize, in);
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

// A log event's batch: up to eight lines, each with its separator.
static constexpr size_t kSseLogBatchBytes = 8 * (LOG_LINE_MAX + 8) + 1;

// The WebEvents task's one event body (#428). The status, rc and log events
// are built into it one after another, all on this task, and
// webEventStreamBroadcast() has put each on the wire before it returns
// (src/web/web_request_psychic.cpp), so it never holds two at once. Sized by
// the largest of the three, the status document (STATUS_JSON_BUFFER_BYTES,
// include/status_json.h). The rc event needs at most 2,518 B (#381) and is
// still measured against this buffer before it is written; a log batch always
// fits whole.
static char s_sseBody[STATUS_JSON_BUFFER_BYTES];
static_assert(kSseLogBatchBytes <= sizeof(s_sseBody),
              "a whole log batch must fit the WebEvents event body");
static JsonDocument s_sseRcDoc;
static bool s_rcSseBuildWarned = false;
static bool s_rcSseSizeWarned = false;
static bool s_statusSseOverflowWarned = false;
static portMUX_TYPE s_broadcastMux = portMUX_INITIALIZER_UNLOCKED;
static bool s_broadcastRequested = false;
static uint32_t s_lastLogSent = 0;
static char s_sseLogLines[8][LOG_LINE_MAX];
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
            PA_LOG_DEBUG("WebEvents", "stack HWM: %u bytes free",
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
                if (!buildStatusJson(s_sseBody, sizeof(s_sseBody))) {
                    if (!s_statusSseOverflowWarned) {
                        PA_LOG_WARN("WebEvents",
                                    "status SSE payload overflowed; sending fallback payload");
                        s_statusSseOverflowWarned = true;
                    }
                } else {
                    s_statusSseOverflowWarned = false;
                }
                webEventStreamBroadcast("status", s_sseBody, nowMs);
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
                if (rcBytes >= sizeof(s_sseBody)) {
                    if (!s_rcSseSizeWarned) {
                        PA_LOG_WARN("WebEvents",
                                    "rc SSE payload too large (%u bytes >= %u); event dropped",
                                    (unsigned)rcBytes, (unsigned)sizeof(s_sseBody));
                        s_rcSseSizeWarned = true;
                    }
                } else {
                    s_rcSseSizeWarned = false;
                    serializeJson(s_sseRcDoc, s_sseBody, sizeof(s_sseBody));
                    webEventStreamBroadcast("rc", s_sseBody, nowMs);
                }
            }
            if (!hwmUnderLoadLogged) {
                PA_LOG_DEBUG("WebEvents", "stack HWM under SSE load: %u bytes free",
                             (unsigned)uxTaskGetStackHighWaterMark(NULL));
                hwmUnderLoadLogged = true;
            }

            if (++s_logSendTick >= 2) {
                s_logSendTick = 0;
                size_t linesCopied = 0;
                s_lastLogSent = copyNewLogLinesSince(s_lastLogSent, s_sseLogLines, 8, &linesCopied);
                if (linesCopied > 0) {
                    size_t pos = 0;
                    for (size_t i = 0; i < linesCopied && pos < kSseLogBatchBytes - 1; ++i) {
                        if (i > 0) {
                            s_sseBody[pos++] = '\x01';
                        }
                        size_t lineLen = strnlen(s_sseLogLines[i], LOG_LINE_MAX);
                        size_t room = kSseLogBatchBytes - 1 - pos;
                        size_t copy = lineLen < room ? lineLen : room;
                        memcpy(s_sseBody + pos, s_sseLogLines[i], copy);
                        pos += copy;
                    }
                    s_sseBody[pos] = '\0';
                    webEventStreamBroadcast("log", s_sseBody, nowMs);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ArduinoOTA task entry. Named rather than the lambda it used to be, because
// two things in this project read task entry points by name and neither can
// see an anonymous one (#271):
//
//  - tools/task_stack_recipes.json walks this chain from a root symbol, and a
//    lambda's is `startHttpServerOnce()::{lambda(void*)#1}::_FUN(void*)` --
//    which renumbers if another lambda is added above it in this function.
//  - test/test_tools/test_profiler_task_list.py extracts the registered task
//    name from the xTaskCreatePinnedToCore() call site. Its second argument is
//    the name; with a multi-line lambda as the first, no name-extracting scan
//    can reach past the lambda body's commas, so this task was invisible to
//    the guard that exists to stop /api/profiler silently omitting a task.
static void otaServiceTask(void*) {
    // Delay OTA init to let WiFi event handler complete first
    vTaskDelay(pdMS_TO_TICKS(500));

    char hostname[DROID_NAME_MAX_LEN + 1] = {};
    configCacheResolvedMdnsHostname(hostname, sizeof(hostname));
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setMdnsEnabled(false);
    ArduinoOTA.setTimeout(OTA_RECEIVE_TIMEOUT_MS);
    ArduinoOTA.onStart([]() {
        const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
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
            PA_LOG_INFO("ArduinoOTA", "progress %u%% heap free=%lu min=%lu largest8bit=%lu",
                        (unsigned)pct, (unsigned long)ESP.getFreeHeap(),
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
        snprintf(s_otaLastError, sizeof(s_otaLastError), "arduino:%d update:%u", (int)error,
                 updateError);
        s_otaActive = false;
        s_lastOtaLoggedPct = 255;
        PA_LOG_ERROR(TAG, "ArduinoOTA error: %d update=%u %s", (int)error, updateError,
                     updateErrorText);
    });
    ArduinoOTA.begin();
    PA_LOG_INFO(TAG, "ArduinoOTA ready on port 3232 as %s", hostname);

    for (;;) {
        ArduinoOTA.handle();
        vTaskDelay(pdMS_TO_TICKS(10));
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

    // Query WiFi connectivity to check if STA is connected to upstream AP
    WifiConnectivityStatus connectivityForMdns = networkManagerQueryConnectivity();
    if (!mdnsStarted && connectivityForMdns.staConnected) {
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
    //
    // Size is chip-target specific; OTA_TASK_STACK_BYTES in include/config.h carries the
    // measured chain and the sizing rule. This task had no static measurement at all
    // until #271 walked it, and the 4096 it used to hard-code covers its artoo-esp32
    // chain by 400 B -- on a walk that is a lower bound. On the ESP32-P4 that same
    // 4096 had 96 B to spare, thinner than one interrupt entry, and was raised.
    if (!otaTaskStarted) {
        xTaskCreatePinnedToCore(otaServiceTask, "ArduinoOTA", OTA_TASK_STACK_BYTES, nullptr, 1,
                                nullptr, 0);
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

    // Initialize network manager: register WiFi event handler.
    // The backend (web_network_manager_native.cpp or native_test_stubs.cpp)
    // handles the actual registration and event translation.
    networkManagerInitialize();

    if (!eventTaskStarted) {
        // Size is chip-target specific; WEB_EVENTS_TASK_STACK_BYTES in include/config.h
        // carries the measured chain. The 6144 this used to hard-code was sized from
        // an ESP32 DoubleException in _dtoa_r after a 4096 overflow, once
        // requestStatusBroadcastNow() call sites grew from rare hardware edges to
        // every web write handler. On ESP32-P4 _dtoa_r is 416 B not 160 B and the
        // static chain through buildStatusJson is 5808 B -- 336 B past 6144 -- so
        // 5808 * 1.25 = 7260 -> 7680 (#256).
        xTaskCreatePinnedToCore(eventStreamTask, "WebEvents", WEB_EVENTS_TASK_STACK_BYTES,
                                nullptr, 1, nullptr, 0);
        eventTaskStarted = true;
    }

    // Network bootstrap: evaluate recovery gesture, build developer shortcut,
    // decide boot posture, and apply it (defined in web_network_bootstrap.cpp).
    webNetworkBootstrap();
}
