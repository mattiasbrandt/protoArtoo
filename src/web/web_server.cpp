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
#include <LittleFS.h>
#include <WiFi.h>
#include <stddef.h>
#include <stdio.h>

#include "../../include/api_actions.h"
#include "../../include/api_profiler.h"
#include "../../include/api_audio.h"
#include "../../include/api_aux_led.h"
#include "../../include/api_config.h"
#include "../../include/api_drive.h"
#include "../../include/drive_speed_preset.h"
#include "../../include/api_estop.h"
#include "../../include/api_helpers.h"
#include "../../include/api_rc.h"
#include "../../include/api_servo.h"
#include "../../include/api_status.h"
#include "../../include/api_system.h"
#include "../../include/api_validation.h"
#include "../../include/audio_task.h"
#include "../../include/config.h"
#include "../../include/config_store.h"
#include "../../include/aux_led.h"
#include "../../include/rc_diagnostics_snapshot.h"
#include "../../include/robot_state.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// PA_ENABLE_STA_WIFI=1 (default): WiFi client mode — connects to an existing network.
//   Credentials (PA_STA_SSID, PA_STA_PASSWORD) must be in src/secrets.h.
//   Server starts when the connection is established.
// PA_ENABLE_STA_WIFI=0 (protoArtoo_prod): hotspot mode — device creates its own
//   access point. No external network needed.
// These are mutually exclusive — never both active simultaneously.
#ifndef PA_ENABLE_STA_WIFI
#define PA_ENABLE_STA_WIFI 1
#endif

#ifndef ARDUINO
class String {
   public:
    const char* c_str() const {
        return "";
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
    AsyncStaticWebHandler& setDefaultFile(const char*) {
        return *this;
    }
};

class AsyncWebServer {
   public:
    explicit AsyncWebServer(int) {
    }
    void addHandler(AsyncEventSource*) {
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
inline size_t heap_caps_get_largest_free_block(uint32_t) {
    return 0;
}
static const uint32_t MALLOC_CAP_8BIT = 4;
#endif
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

static const char* TAG = "WebServer";
static AsyncWebServer server(80);
static AsyncEventSource events("/api/events");
static bool littleFsReady = false;
static char s_fsVersion[48] = "unknown";
static bool routesRegistered = false;
static bool serverStarted = false;
static bool eventTaskStarted = false;
static bool otaTaskStarted = false;

namespace {

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
    bool estop;
    bool webControlEnabled;
    bool sbusSignalLost;
    bool sbus2SignalLost;
    bool sbusHwFailsafe;
    bool webDriveExpired;
    bool wifiConnected;
    bool wifiClientConnected;
    int failsafeSource;
    int driveSpeed;
    int driveSteer;
    float domeTargetSpeed;
    int speedLimitMax;
    SpeedPresetId speedPresetActive;
    bool stationary;
    unsigned long failsafeCount;
    unsigned long failsafeTriggerMs;
    unsigned long failsafeZeroMs;
    unsigned long failsafeTriggerToZeroMs;
    unsigned long failsafeWatchdogMs;
    int failsafeTriggerSource;
    unsigned long uptimeMs;
    unsigned long heapFree;
    unsigned long heapMin;
    uint32_t heapLargestBlock;
    long wifiRssi;
    bool enableArm1, enableArm2, enableAux1, enableAux2, enableAux3, enableDome;
    bool enableRcCh1, enableRcCh2, enableRcCh3, enableRcCh4, enableRcCh5, enableRcCh6;
    bool enableS1Hoverboard, enableS2Sound, enableS3DomeCtrl;
    bool audioActive;
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
    estop = robotState.estop;
    webControlEnabled = robotState.webControlEnabled;
    sbusSignalLost = robotState.sbusSignalLost;
    sbus2SignalLost = robotState.sbus2SignalLost;
    sbusHwFailsafe = robotState.sbusHwFailsafe;
    webDriveExpired = robotState.webDriveExpired;
    failsafeSource = (int)robotState.failsafeSource;
    driveSpeed = robotState.driveOutputSpeed;
    driveSteer = robotState.driveOutputSteer;
    domeTargetSpeed = robotState.domeTargetSpeed;
    speedLimitMax = cfg.drive.speedLimitMax;
    speedPresetActive = normalizeSpeedPresetId((uint8_t)cfg.drive.speedPresetActive);
    stationary = robotState.stationary;
    failsafeCount = robotState.failsafeTriggerCount;
    failsafeTriggerMs = robotState.failsafeLastTriggerMs;
    failsafeZeroMs = robotState.failsafeLastZeroOutputMs;
    failsafeTriggerToZeroMs = robotState.failsafeLastTriggerToZeroMs;
    failsafeWatchdogMs = robotState.failsafeLastWatchdogMs;
    failsafeTriggerSource = (int)robotState.failsafeLastTriggerSource;
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
    heapLargestBlock = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
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
    // Build the fixed system-health fields first.
    int written = snprintf(
        buffer, bufferSize,
        "{\"estop\":%s,\"webControlEnabled\":%s,\"sbusSignalLost\":%s,\"sbusHwFailsafe\":%s,\"webDriveExpired\":%s,\"failsafeSource\":%d,\"driveSpeed\":%d,\"driveSteer\":%d,\"domeTargetSpeed\":%.3f,\"domeEnabled\":%s,\"speedLimitMax\":%d,\"speedPreset\":\"%s\",\"stationary\":%s,\"failsafeCount\":%lu,\"failsafeTriggerMs\":%lu,\"failsafeZeroMs\":%lu,\"failsafeTriggerToZeroMs\":%lu,\"failsafeWatchdogMs\":%lu,\"failsafeTriggerSource\":%d,\"uptimeMs\":%lu,\"firmwareVersion\":\"%s\",\"fsVersion\":\"%s\",\"heapFree\":%lu,\"heapMin\":%lu,\"heapLargestBlock\":%lu,\"wifiRssi\":%ld,\"wifiConnected\":%s,\"wifiClientConnected\":%s,\"littleFsReady\":%s,\"sleepMode\":%s,\"sleepSinceMs\":%lu,\"activeMood\":%u,\"auxLed\":{\"pin\":%u,\"r\":%u,\"g\":%u,\"b\":%u,\"effect\":\"%s\",\"available\":%s}",
        estop ? "true" : "false", webControlEnabled ? "true" : "false",
        sbusSignalLost ? "true" : "false", sbusHwFailsafe ? "true" : "false",
        webDriveExpired ? "true" : "false", failsafeSource, driveSpeed, driveSteer,
        (double)domeTargetSpeed, enableDome ? "true" : "false",
        speedLimitMax, speedPresetIdToString(speedPresetActive), stationary ? "true" : "false",
        failsafeCount, failsafeTriggerMs, failsafeZeroMs, failsafeTriggerToZeroMs,
        failsafeWatchdogMs, failsafeTriggerSource, uptimeMs, PA_FIRMWARE_VERSION, s_fsVersion,
        heapFree, heapMin, (unsigned long)heapLargestBlock, wifiRssi,
        wifiConnected ? "true" : "false",
        wifiClientConnected ? "true" : "false", littleFsReady ? "true" : "false",
        sleepMode ? "true" : "false", (unsigned long)sleepSinceMs, (unsigned)activeMood,
        (unsigned)auxLedPin, (unsigned)auxLedR, (unsigned)auxLedG, (unsigned)auxLedB,
        auxLedEffectLabel, auxLedAvailable ? "true" : "false");

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
            int _n = snprintf(pos, remaining,
                              ",\"s2Sound\":{\"state\":\"%s\",\"detail\":\"%s\",\"driver\":\"%s\"}",
                              audioActive ? "playing" : "idle",
                              audioActive ? "Playback active" : "Ready, no active playback",
                              audioGetDriverName());
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
            int32_t lastRxMs = -1;
            char dlDetail[96];
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
            char dlBuf[320];
            snprintf(dlBuf, sizeof(dlBuf),
                     ",\"dome_link\":{\"state\":\"%s\",\"transport\":\"%s\",\"detail\":\"%s\",\"hb_tx\":%lu,\"hb_rx\":%lu"
                     ",\"rx_overflow\":%lu,\"rx_unknown\":%lu,\"last_rx_ms\":%ld}",
                     dlState, dlTransport, dlDetail, (unsigned long)bodyHbTx,
                     (unsigned long)domeHbRx, (unsigned long)domeRxOverflowCount,
                     (unsigned long)domeRxUnknownCount, (long)lastRxMs);
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

bool webServerHasSSEClients() {
    return events.count() > 0;
}

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

void requestStatusBroadcastNow() {
    taskENTER_CRITICAL(&s_broadcastMux);
    s_broadcastRequested = true;
    taskEXIT_CRITICAL(&s_broadcastMux);
}

void eventStreamTask(void*) {
    bool hwmLogged = false;
    bool hwmUnderLoadLogged = false;
    for (;;) {
        if (!hwmLogged) {
            PA_LOG_DEBUG("WebEvents", "stack HWM: %u words free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        if (serverStarted && events.count() > 0) {
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
                events.send(s_sseStatusBody, "status", nowMs);
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
                    events.send(s_sseRcBody, "rc", nowMs);
                }
            }
            if (!hwmUnderLoadLogged) {
                PA_LOG_DEBUG("WebEvents", "stack HWM under SSE load: %u words free",
                             (unsigned)uxTaskGetStackHighWaterMark(NULL));
                hwmUnderLoadLogged = true;
            }

            size_t linesCopied = 0;
            s_lastLogSent = copyNewLogLinesSince(s_lastLogSent, s_sseLogLines, 8, &linesCopied);
            for (size_t i = 0; i < linesCopied; ++i) {
                events.send(s_sseLogLines[i], "log", nowMs);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void startHttpServerOnce() {
    if (serverStarted) {
        return;
    }

    if (!routesRegistered) {
        events.onConnect([](AsyncEventSourceClient* client) {
            // Keep AsyncTCP callback light. Heavy JSON/log formatting runs in
            // eventStreamTask on Core 0 to avoid async_tcp stack pressure.
            (void)client;
        });
        server.addHandler(&events);

        registerEstopRoutes(server);
        registerDriveRoutes(server);
        registerMoodMapRoutes(server);
        registerAudioRoutes(server);
        registerConfigRoutes(server);
        registerAuxLedRoutes(server);
        registerRcRoutes(server);
        registerServoRoutes(server);
        registerStatusRoutes(server);
        registerValidationRoutes(server);
        registerSystemRoutes(server);
#if PA_HEAP_PROFILE
        registerProfilerRoutes(server);
#endif
        registerActionsRoutes(server);

        if (littleFsReady) {
            server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
        }

        routesRegistered = true;
    }

    server.begin();
    serverStarted = true;
    PA_LOG_INFO(TAG, "HTTP server started on port 80");

    // Start OTA task in background — MUST NOT block WiFi event handler (causes TWDT)
    if (!otaTaskStarted) {
        xTaskCreatePinnedToCore(
            [](void*) {
                // Delay OTA init to let WiFi event handler complete first
                vTaskDelay(pdMS_TO_TICKS(500));

                ArduinoOTA.setHostname("protoArtoo");
                ArduinoOTA.onStart([]() {
                    const char* type =
                        (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
                    PA_LOG_INFO(TAG, "ArduinoOTA start: %s", type);
                });
                ArduinoOTA.onEnd([]() { PA_LOG_INFO(TAG, "ArduinoOTA complete"); });
                ArduinoOTA.onError([](ota_error_t error) {
                    PA_LOG_ERROR(TAG, "ArduinoOTA error: %d", (int)error);
                });
                ArduinoOTA.begin();
                PA_LOG_INFO(TAG, "ArduinoOTA ready on port 3232");

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
            PA_LOG_INFO(TAG, "Hotspot started - SSID: %s  IP: %s", WIFI_AP_SSID,
                        WiFi.softAPIP().toString().c_str());
            startHttpServerOnce();
            break;
#if PA_ENABLE_STA_WIFI
        case ARDUINO_EVENT_WIFI_STA_START:
            PA_LOG_INFO(TAG, "Connecting to WiFi network...");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            PA_LOG_INFO(TAG, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
            startHttpServerOnce();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            PA_LOG_INFO(TAG, "WiFi connection lost");
            break;
#endif  // PA_ENABLE_STA_WIFI
        default:
            break;
    }
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
        // Keep 4096 bytes for status/rc/log SSE work and JSON serialization headroom.
        // A previous 2048-byte reduction overflowed on client connect.
        xTaskCreatePinnedToCore(eventStreamTask, "WebEvents", 4096, nullptr, 1, nullptr, 0);
        eventTaskStarted = true;
    }

#if PA_ENABLE_STA_WIFI
    // Compile-time guard: credentials must be present when WiFi client mode is enabled.
#if !__has_include("secrets.h")
#error "PA_ENABLE_STA_WIFI=1 requires src/secrets.h defining PA_STA_SSID and PA_STA_PASSWORD"
#endif
#ifndef PA_STA_SSID
#error "PA_ENABLE_STA_WIFI=1: PA_STA_SSID not defined in secrets.h"
#endif
#ifndef PA_STA_PASSWORD
#error "PA_ENABLE_STA_WIFI=1: PA_STA_PASSWORD not defined in secrets.h"
#endif
    WiFi.mode(WIFI_STA);
    WiFi.begin(PA_STA_SSID, PA_STA_PASSWORD);
    PA_LOG_INFO(TAG, "WiFi bootstrap: client mode");
#else
#if !__has_include("secrets.h")
#error "PA_ENABLE_STA_WIFI=0 requires src/secrets.h defining PA_AP_PASSWORD"
#endif
#ifndef PA_AP_PASSWORD
#error "PA_ENABLE_STA_WIFI=0: PA_AP_PASSWORD not defined in secrets.h"
#endif
    static_assert(sizeof(PA_AP_PASSWORD) >= 9, "PA_AP_PASSWORD must be at least 8 characters");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, PA_AP_PASSWORD);
    PA_LOG_INFO(TAG, "WiFi bootstrap: hotspot mode (secured)");
#endif  // PA_ENABLE_STA_WIFI
}
