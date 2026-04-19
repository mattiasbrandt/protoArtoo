// =============================================================================
// src/web/api_config.cpp
//
// Config API endpoints
//   GET /api/config  — current persisted runtime config snapshot
//   POST /api/config — update config fields and persist to NVS
//
// Notes:
// - This route is the sole web entrypoint for config writes.
// - Hardware access is not performed here; values are validated, written to
//   RobotState cfg_* fields under robotStateMux, and persisted via saveConfigToNvs().
// - Compatibility aliases are accepted for in-flight RC mapping UX work.
// =============================================================================

#include "api_config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ctype.h>
#include <string.h>

#include "api_config_snapshot.h"
#include "api_helpers.h"
#include "audio_task.h"
#include "config.h"
#include "logging.h"
#include "robot_state.h"
#include "servo_component_helpers.h"

static const char* TAG = "WebServer";

extern bool saveConfigToNvs();

namespace {
constexpr uint16_t kServoPulseMinUs = 500;
constexpr uint16_t kServoPulseMaxUs = 2500;


const char* rcModeToString(RcInputMode mode) {
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

bool parseRcInputMode(const char* raw, RcInputMode* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }

    if (strcmp(raw, "standard_pwm") == 0) {
        *out = RC_INPUT_STANDARD_PWM;
        return true;
    }
    if (strcmp(raw, "single_sbus") == 0) {
        *out = RC_INPUT_SINGLE_SBUS;
        return true;
    }
    if (strcmp(raw, "dual_sbus") == 0) {
        *out = RC_INPUT_DUAL_SBUS;
        return true;
    }

    return false;
}

bool normalizeTriggerTargetAlias(const char* raw, char* out, size_t outSize) {
    if (raw == nullptr || out == nullptr || outSize == 0) {
        return false;
    }

    int copied = snprintf(out, outSize, "%s", raw);
    if (copied <= 0 || copied >= (int)outSize) {
        return false;
    }

    const char* oldToken = ":marcduino:";
    char* hit = strstr(out, oldToken);
    if (hit == nullptr) {
        return true;
    }

    char normalized[96] = {};
    size_t prefixLen = (size_t)(hit - out);
    const char* suffix = hit + strlen(oldToken);

    int n = snprintf(normalized, sizeof(normalized), "%.*s:cmd:%s", (int)prefixLen, out, suffix);
    if (n <= 0 || n >= (int)sizeof(normalized)) {
        return false;
    }

    copied = snprintf(out, outSize, "%s", normalized);
    return copied > 0 && copied < (int)outSize;
}

bool triggerBindingToUiString(char* out, size_t outSize, const RcTriggerBinding& binding) {
    char encoded[96] = {};
    if (!formatRcTriggerBinding(encoded, sizeof(encoded), binding)) {
        return false;
    }

    const char* oldToken = ":cmd:";
    char* hit = strstr(encoded, oldToken);
    if (hit == nullptr) {
        int n = snprintf(out, outSize, "%s", encoded);
        return n > 0 && n < (int)outSize;
    }

    char uiEncoded[112] = {};
    size_t prefixLen = (size_t)(hit - encoded);
    const char* suffix = hit + strlen(oldToken);
    int n = snprintf(uiEncoded, sizeof(uiEncoded), "%.*s:marcduino:%s", (int)prefixLen, encoded,
                     suffix);
    if (n <= 0 || n >= (int)sizeof(uiEncoded)) {
        return false;
    }

    n = snprintf(out, outSize, "%s", uiEncoded);
    return n > 0 && n < (int)outSize;
}

bool parseInt16Param(const AsyncWebServerRequest* req, const char* name, int16_t minValue,
                     int16_t maxValue, int16_t* out) {
    if (req == nullptr || out == nullptr || !req->hasParam(name, true)) {
        return false;
    }

    int16_t value = 0;
    if (!parseDriveValue(req->getParam(name, true)->value().c_str(), &value)) {
        return false;
    }

    if (value < minValue || value > maxValue) {
        return false;
    }

    *out = value;
    return true;
}

bool parseUint32Param(const AsyncWebServerRequest* req, const char* name, uint32_t minValue,
                      uint32_t maxValue, uint32_t* out) {
    if (req == nullptr || out == nullptr || !req->hasParam(name, true)) {
        return false;
    }

    uint32_t value = 0;
    if (!parseUint32Value(req->getParam(name, true)->value().c_str(), &value)) {
        return false;
    }

    if (value < minValue || value > maxValue) {
        return false;
    }

    *out = value;
    return true;
}

bool parseUint16Param(const AsyncWebServerRequest* req, const char* name, uint16_t minValue,
                      uint16_t maxValue, uint16_t* out) {
    uint32_t temp = 0;
    if (!parseUint32Param(req, name, minValue, maxValue, &temp)) {
        return false;
    }
    *out = (uint16_t)temp;
    return true;
}

bool parseUint8Param(const AsyncWebServerRequest* req, const char* name, uint8_t minValue,
                     uint8_t maxValue, uint8_t* out) {
    uint32_t temp = 0;
    if (!parseUint32Param(req, name, minValue, maxValue, &temp)) {
        return false;
    }
    *out = (uint8_t)temp;
    return true;
}

bool parseBoolParam(const AsyncWebServerRequest* req, const char* name, bool* out) {
    if (req == nullptr || out == nullptr || !req->hasParam(name, true)) {
        return false;
    }
    return parseBoolValue(req->getParam(name, true)->value().c_str(), out);
}

bool isValidIpv4Literal(const char* raw) {
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }

    const char* p = raw;
    int octetCount = 0;
    while (*p != '\0') {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }

        int value = 0;
        int digits = 0;
        while (*p != '\0' && *p != '.') {
            if (!isdigit((unsigned char)*p)) {
                return false;
            }
            value = (value * 10) + (*p - '0');
            digits++;
            if (digits > 3 || value > 255) {
                return false;
            }
            p++;
        }

        if (digits == 0) {
            return false;
        }

        octetCount++;
        if (octetCount > 4) {
            return false;
        }

        if (*p == '.') {
            p++;
            if (*p == '\0') {
                return false;
            }
        }
    }

    return octetCount == 4;
}

bool parseDomeWifiPeerIp(const char* raw, char* out, size_t outSize) {
    if (raw == nullptr || out == nullptr || outSize == 0) {
        return false;
    }
    if (raw[0] == '\0') {
        out[0] = '\0';
        return true;
    }
    if (strlen(raw) >= outSize) {
        return false;
    }
    if (!isValidIpv4Literal(raw)) {
        return false;
    }
    int n = snprintf(out, outSize, "%s", raw);
    return n > 0 && n < (int)outSize;
}

bool parseRcBindingParam(const AsyncWebServerRequest* req, const char* name, RcBindingConfig* out) {
    if (req == nullptr || out == nullptr || !req->hasParam(name, true)) {
        return false;
    }
    return parseRcBindingConfig(req->getParam(name, true)->value().c_str(), out);
}

bool parseRcTriggerParam(const AsyncWebServerRequest* req, const char* name,
                         RcTriggerBinding* out) {
    if (req == nullptr || out == nullptr || !req->hasParam(name, true)) {
        return false;
    }

    char normalized[96] = {};
    if (!normalizeTriggerTargetAlias(req->getParam(name, true)->value().c_str(), normalized,
                                     sizeof(normalized))) {
        return false;
    }
    return parseRcTriggerBinding(normalized, out);
}

bool sourceAllowedForMode(RcInputMode mode, RcBindingSource source) {
    switch (mode) {
        case RC_INPUT_STANDARD_PWM:
            return source == RC_BINDING_NONE || source == RC_BINDING_PWM;
        case RC_INPUT_SINGLE_SBUS:
            return source == RC_BINDING_NONE || source == RC_BINDING_SBUS1;
        case RC_INPUT_DUAL_SBUS:
        default:
            return source == RC_BINDING_NONE || source == RC_BINDING_SBUS1 ||
                   source == RC_BINDING_SBUS2;
    }
}

bool triggerTargetAllowedByRuntime(const RcTriggerBinding& binding) {
    // Phase 4 deferred: DOME_ACTION_SEQ is intentionally blocked at API level
    // until DomeLinkTask routing is implemented in runtime trigger handling.
    return binding.target != DOME_ACTION_SEQ;
}

}  // namespace

// -----------------------------------------------------------------------------
// captureConfigSnapshot()
//
// Copies all NVS-backed cfg_* fields out of robotState under portMUX.
// Call this once; pass the result to populateConfigJson().
// -----------------------------------------------------------------------------
void captureConfigSnapshot(ConfigSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&robotStateMux);
    out->speedLimitMax = robotState.cfg_speedLimitMax;
    out->speedPresetSlow = robotState.cfg_speedPresetSlow;
    out->speedPresetNormal = robotState.cfg_speedPresetNormal;
    out->speedPresetTurbo = robotState.cfg_speedPresetTurbo;
    out->speedPresetActive = normalizeSpeedPresetId((uint8_t)robotState.cfg_speedPresetActive);
    out->sbusTimeoutMs = robotState.cfg_sbusTimeoutMs;
    out->webDriveTimeoutMs = robotState.cfg_webDriveTimeoutMs;
    out->stationary = robotState.cfg_stationary;
    out->logLevel = robotState.cfg_logLevel;
    out->rcInputMode = robotState.cfg_rc_input_mode;
    out->sbusRecvCh2 = robotState.cfg_single_sbus_use_ch2;

    out->enableArm1 = robotState.cfg_enable_arm1;
    out->enableArm2 = robotState.cfg_enable_arm2;
    out->enableAux1 = robotState.cfg_enable_aux1;
    out->enableAux2 = robotState.cfg_enable_aux2;
    out->enableAux3 = robotState.cfg_enable_aux3;
    out->enableDome = robotState.cfg_enable_dome;
    out->enableRcCh1 = robotState.cfg_enable_rc_ch1;
    out->enableRcCh2 = robotState.cfg_enable_rc_ch2;
    out->enableRcCh3 = robotState.cfg_enable_rc_ch3;
    out->enableRcCh4 = robotState.cfg_enable_rc_ch4;
    out->enableRcCh5 = robotState.cfg_enable_rc_ch5;
    out->enableRcCh6 = robotState.cfg_enable_rc_ch6;
    out->enableS1Hoverboard = robotState.cfg_enable_s1_hoverboard;
    out->enableS2Sound = robotState.cfg_enable_s2_sound;
    out->enableS3DomeCtrl = robotState.cfg_enable_s3_dome_ctrl;

    out->domeNeutralUs = robotState.cfg_dome_neutral_us;
    out->domeMinPulseUs = robotState.cfg_dome_min_pulse_us;
    out->domeMaxPulseUs = robotState.cfg_dome_max_pulse_us;
    out->domeSpeedLimitPct = robotState.cfg_dome_speed_limit_pct;
    snprintf(out->domeWifiPeerIp, sizeof(out->domeWifiPeerIp), "%s",
             robotState.cfg_dome_wifi_peer_ip);

    out->arm1Type = robotState.cfg_arm1_type;
    out->arm2Type = robotState.cfg_arm2_type;
    out->aux1Type = robotState.cfg_aux1_type;
    out->aux2Type = robotState.cfg_aux2_type;
    out->aux3Type = robotState.cfg_aux3_type;
    out->arm1OpenUs = robotState.cfg_arm1_open_us;
    out->arm1CloseUs = robotState.cfg_arm1_close_us;
    out->arm2OpenUs = robotState.cfg_arm2_open_us;
    out->arm2CloseUs = robotState.cfg_arm2_close_us;
    out->aux1OpenUs = robotState.cfg_aux1_open_us;
    out->aux1CloseUs = robotState.cfg_aux1_close_us;
    out->aux2OpenUs = robotState.cfg_aux2_open_us;
    out->aux2CloseUs = robotState.cfg_aux2_close_us;
    out->aux3OpenUs = robotState.cfg_aux3_open_us;
    out->aux3CloseUs = robotState.cfg_aux3_close_us;
    out->auxLedPin = robotState.cfg_aux_led_pin;
    out->auxLedCount = robotState.cfg_aux_led_count;

    out->rcPwmDriveSpeed = robotState.cfg_rc_pwm_drive_speed;
    out->rcPwmDriveSteer = robotState.cfg_rc_pwm_drive_steer;
    out->rcPwmDomeSpeed = robotState.cfg_rc_pwm_dome_speed;
    out->rcPwmArm1 = robotState.cfg_rc_pwm_arm1;
    out->rcPwmArm2 = robotState.cfg_rc_pwm_arm2;
    out->rcPwmSound = robotState.cfg_rc_pwm_sound;

    out->rcSbusDriveSpeed = robotState.cfg_rc_sbus_drive_speed;
    out->rcSbusDriveSteer = robotState.cfg_rc_sbus_drive_steer;
    out->rcSbusDomeSpeed = robotState.cfg_rc_sbus_dome_speed;
    out->rcSbusArm1 = robotState.cfg_rc_sbus_arm1;
    out->rcSbusArm2 = robotState.cfg_rc_sbus_arm2;
    out->rcSbusSound = robotState.cfg_rc_sbus_sound;

    out->rcArm1 = robotState.cfg_rc_arm1;
    out->rcArm2 = robotState.cfg_rc_arm2;
    out->rcAux1 = robotState.cfg_rc_aux1;
    out->rcAux2 = robotState.cfg_rc_aux2;
    out->rcAux3 = robotState.cfg_rc_aux3;
    out->rcSound = robotState.cfg_rc_sound;
    out->rcOpmode = robotState.cfg_rc_opmode;
    out->rcFree0 = robotState.cfg_rc_free0;
    out->rcFree1 = robotState.cfg_rc_free1;
    out->rcFree2 = robotState.cfg_rc_free2;
    out->rcFree3 = robotState.cfg_rc_free3;
    taskEXIT_CRITICAL(&robotStateMux);
}

// -----------------------------------------------------------------------------
// populateConfigJson()
//
// Pure function — no global state, no FreeRTOS. Accepts a snapshot produced by
// captureConfigSnapshot() and builds the ArduinoJson document field by field.
// RC binding structs are serialised via formatRcBindingConfig /
// triggerBindingToUiString (both in the anonymous namespace, same TU).
// Returns false if any binding format call fails; caller should send HTTP 500.
// -----------------------------------------------------------------------------
bool populateConfigJson(JsonDocument& doc, const ConfigSnapshot& snap) {
    doc.clear();
    // --- RC binding config strings (12 channels, 48 bytes each) ---
    char rcPwmDriveSpeedStr[48] = {};
    char rcPwmDriveSteerStr[48] = {};
    char rcPwmDomeSpeedStr[48] = {};
    char rcPwmArm1Str[48] = {};
    char rcPwmArm2Str[48] = {};
    char rcPwmSoundStr[48] = {};
    char rcSbusDriveSpeedStr[48] = {};
    char rcSbusDriveSteerStr[48] = {};
    char rcSbusDomeSpeedStr[48] = {};
    char rcSbusArm1Str[48] = {};
    char rcSbusArm2Str[48] = {};
    char rcSbusSoundStr[48] = {};

    // --- Trigger binding strings (11 slots, 96 bytes each — wider for payload) ---
    char rcArm1Str[96] = {};
    char rcArm2Str[96] = {};
    char rcAux1Str[96] = {};
    char rcAux2Str[96] = {};
    char rcAux3Str[96] = {};
    char rcSoundStr[96] = {};
    char rcOpmodeStr[96] = {};
    char rcFree0Str[96] = {};
    char rcFree1Str[96] = {};
    char rcFree2Str[96] = {};
    char rcFree3Str[96] = {};

    if (!formatRcBindingConfig(rcPwmDriveSpeedStr, sizeof(rcPwmDriveSpeedStr),
                               snap.rcPwmDriveSpeed) ||
        !formatRcBindingConfig(rcPwmDriveSteerStr, sizeof(rcPwmDriveSteerStr),
                               snap.rcPwmDriveSteer) ||
        !formatRcBindingConfig(rcPwmDomeSpeedStr, sizeof(rcPwmDomeSpeedStr), snap.rcPwmDomeSpeed) ||
        !formatRcBindingConfig(rcPwmArm1Str, sizeof(rcPwmArm1Str), snap.rcPwmArm1) ||
        !formatRcBindingConfig(rcPwmArm2Str, sizeof(rcPwmArm2Str), snap.rcPwmArm2) ||
        !formatRcBindingConfig(rcPwmSoundStr, sizeof(rcPwmSoundStr), snap.rcPwmSound) ||
        !formatRcBindingConfig(rcSbusDriveSpeedStr, sizeof(rcSbusDriveSpeedStr),
                               snap.rcSbusDriveSpeed) ||
        !formatRcBindingConfig(rcSbusDriveSteerStr, sizeof(rcSbusDriveSteerStr),
                               snap.rcSbusDriveSteer) ||
        !formatRcBindingConfig(rcSbusDomeSpeedStr, sizeof(rcSbusDomeSpeedStr),
                               snap.rcSbusDomeSpeed) ||
        !formatRcBindingConfig(rcSbusArm1Str, sizeof(rcSbusArm1Str), snap.rcSbusArm1) ||
        !formatRcBindingConfig(rcSbusArm2Str, sizeof(rcSbusArm2Str), snap.rcSbusArm2) ||
        !formatRcBindingConfig(rcSbusSoundStr, sizeof(rcSbusSoundStr), snap.rcSbusSound) ||
        !triggerBindingToUiString(rcArm1Str, sizeof(rcArm1Str), snap.rcArm1) ||
        !triggerBindingToUiString(rcArm2Str, sizeof(rcArm2Str), snap.rcArm2) ||
        !triggerBindingToUiString(rcAux1Str, sizeof(rcAux1Str), snap.rcAux1) ||
        !triggerBindingToUiString(rcAux2Str, sizeof(rcAux2Str), snap.rcAux2) ||
        !triggerBindingToUiString(rcAux3Str, sizeof(rcAux3Str), snap.rcAux3) ||
        !triggerBindingToUiString(rcSoundStr, sizeof(rcSoundStr), snap.rcSound) ||
        !triggerBindingToUiString(rcOpmodeStr, sizeof(rcOpmodeStr), snap.rcOpmode) ||
        !triggerBindingToUiString(rcFree0Str, sizeof(rcFree0Str), snap.rcFree0) ||
        !triggerBindingToUiString(rcFree1Str, sizeof(rcFree1Str), snap.rcFree1) ||
        !triggerBindingToUiString(rcFree2Str, sizeof(rcFree2Str), snap.rcFree2) ||
        !triggerBindingToUiString(rcFree3Str, sizeof(rcFree3Str), snap.rcFree3)) {
        return false;
    }

    JsonObject drive = doc["drive"].to<JsonObject>();
    drive["speedLimitMax"] = snap.speedLimitMax;
    drive["speedPresetSlow"] = snap.speedPresetSlow;
    drive["speedPresetNormal"] = snap.speedPresetNormal;
    drive["speedPresetTurbo"] = snap.speedPresetTurbo;
    drive["speedPreset"] = speedPresetIdToString(snap.speedPresetActive);
    drive["webDriveTimeoutMs"] = snap.webDriveTimeoutMs;
    drive["stationary"] = snap.stationary;

    JsonObject rc = doc["rc"].to<JsonObject>();
    rc["inputMode"] = rcModeToString(snap.rcInputMode);
    rc["sbusTimeoutMs"] = snap.sbusTimeoutMs;

    JsonObject rcPwm = rc["pwm"].to<JsonObject>();
    rcPwm["driveSpeed"] = rcPwmDriveSpeedStr;
    rcPwm["driveSteer"] = rcPwmDriveSteerStr;
    rcPwm["domeSpeed"] = rcPwmDomeSpeedStr;
    rcPwm["arm1"] = rcPwmArm1Str;
    rcPwm["arm2"] = rcPwmArm2Str;
    rcPwm["sound"] = rcPwmSoundStr;

    JsonObject rcSbus = rc["sbus"].to<JsonObject>();
    rcSbus["driveSpeed"] = rcSbusDriveSpeedStr;
    rcSbus["driveSteer"] = rcSbusDriveSteerStr;
    rcSbus["domeSpeed"] = rcSbusDomeSpeedStr;
    rcSbus["arm1"] = rcSbusArm1Str;
    rcSbus["arm2"] = rcSbusArm2Str;
    rcSbus["sound"] = rcSbusSoundStr;
    rcSbus["recvCh2"] = snap.sbusRecvCh2;

    JsonObject rcTriggers = rc["triggers"].to<JsonObject>();
    rcTriggers["arm1"] = rcArm1Str;
    rcTriggers["arm2"] = rcArm2Str;
    rcTriggers["aux1"] = rcAux1Str;
    rcTriggers["aux2"] = rcAux2Str;
    rcTriggers["aux3"] = rcAux3Str;
    rcTriggers["sound"] = rcSoundStr;
    rcTriggers["opMode"] = rcOpmodeStr;
    rcTriggers["free0"] = rcFree0Str;
    rcTriggers["free1"] = rcFree1Str;
    rcTriggers["free2"] = rcFree2Str;
    rcTriggers["free3"] = rcFree3Str;

    JsonObject components = doc["components"].to<JsonObject>();
    components["arm1"]["enabled"] = snap.enableArm1;
    components["arm1"]["type"] = servoCompTypeToString(snap.arm1Type);
    components["arm2"]["enabled"] = snap.enableArm2;
    components["arm2"]["type"] = servoCompTypeToString(snap.arm2Type);
    components["aux1"]["enabled"] = snap.enableAux1;
    components["aux1"]["type"] = servoCompTypeToString(snap.aux1Type);
    components["aux2"]["enabled"] = snap.enableAux2;
    components["aux2"]["type"] = servoCompTypeToString(snap.aux2Type);
    components["aux3"]["enabled"] = snap.enableAux3;
    components["aux3"]["type"] = servoCompTypeToString(snap.aux3Type);
    components["dome"]["enabled"] = snap.enableDome;
    components["rcCh1"]["enabled"] = snap.enableRcCh1;
    components["rcCh2"]["enabled"] = snap.enableRcCh2;
    components["rcCh3"]["enabled"] = snap.enableRcCh3;
    components["rcCh4"]["enabled"] = snap.enableRcCh4;
    components["rcCh5"]["enabled"] = snap.enableRcCh5;
    components["rcCh6"]["enabled"] = snap.enableRcCh6;
    components["s1Hoverboard"]["enabled"] = snap.enableS1Hoverboard;
    components["s2Sound"]["enabled"] = snap.enableS2Sound;
    components["s3DomeCtrl"]["enabled"] = snap.enableS3DomeCtrl;

    // Legacy top-level calibration fields consumed by data/servo.js
    doc["arm1OpenUs"] = snap.arm1OpenUs;
    doc["arm1CloseUs"] = snap.arm1CloseUs;
    doc["arm2OpenUs"] = snap.arm2OpenUs;
    doc["arm2CloseUs"] = snap.arm2CloseUs;
    doc["aux1OpenUs"] = snap.aux1OpenUs;
    doc["aux1CloseUs"] = snap.aux1CloseUs;
    doc["aux2OpenUs"] = snap.aux2OpenUs;
    doc["aux2CloseUs"] = snap.aux2CloseUs;
    doc["aux3OpenUs"] = snap.aux3OpenUs;
    doc["aux3CloseUs"] = snap.aux3CloseUs;
    doc["aux_led_pin"] = snap.auxLedPin;
    doc["aux_led_count"] = snap.auxLedCount;

    JsonObject dome = doc["dome"].to<JsonObject>();
    dome["neutralUs"] = snap.domeNeutralUs;
    dome["minPulseUs"] = snap.domeMinPulseUs;
    dome["maxPulseUs"] = snap.domeMaxPulseUs;
    dome["speedLimitPct"] = snap.domeSpeedLimitPct;
    dome["wifiPeerIp"] = snap.domeWifiPeerIp;

    JsonObject system = doc["system"].to<JsonObject>();
    system["logLevel"] = snap.logLevel;

    return !doc.overflowed();
}

void registerConfigRoutes(AsyncWebServer& server) {
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        ConfigSnapshot snap;
        captureConfigSnapshot(&snap);
        JsonDocument doc;
        if (!populateConfigJson(doc, snap)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"config json build failed\"}");
            return;
        }
        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response stream alloc failed\"}");
            return;
        }
        serializeJson(doc, *stream);
        req->send(stream);
    });

    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* req) {
        ConfigSnapshot working;
        captureConfigSnapshot(&working);
        bool changed = false;
        bool speedLimitMaxProvided = false;
        bool speedPresetValuesProvided = false;
        SpeedPresetId activePresetBefore = SpeedPresetId::Normal;
        taskENTER_CRITICAL(&robotStateMux);
        activePresetBefore = normalizeSpeedPresetId((uint8_t)robotState.cfg_speedPresetActive);
        taskEXIT_CRITICAL(&robotStateMux);
        SpeedPresetId activePresetAfter = activePresetBefore;
        int16_t speedLimitMax;
        if (parseInt16Param(req, "speedLimitMax", 0, SPEED_LIMIT_MAX, &speedLimitMax)) {
            working.speedLimitMax = speedLimitMax;
            speedLimitMaxProvided = true;
            PA_LOG_INFO(TAG, "[CFG] speedLimitMax updated to %d", (int)speedLimitMax);
            changed = true;
        } else if (req->hasParam("speedLimitMax", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"speedLimitMax must be 0..600\"}");
            return;
        }

        int16_t speedPresetSlow;
        if (parseInt16Param(req, "speedPresetSlow", 0, SPEED_LIMIT_MAX, &speedPresetSlow)) {
            working.speedPresetSlow = speedPresetSlow;
            speedPresetValuesProvided = true;
            PA_LOG_INFO(TAG, "[CFG] speedPresetSlow updated to %d", (int)speedPresetSlow);
            changed = true;
        } else if (req->hasParam("speedPresetSlow", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"speedPresetSlow must be 0..600\"}");
            return;
        }

        int16_t speedPresetNormal;
        if (parseInt16Param(req, "speedPresetNormal", 0, SPEED_LIMIT_MAX, &speedPresetNormal)) {
            working.speedPresetNormal = speedPresetNormal;
            speedPresetValuesProvided = true;
            PA_LOG_INFO(TAG, "[CFG] speedPresetNormal updated to %d", (int)speedPresetNormal);
            changed = true;
        } else if (req->hasParam("speedPresetNormal", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"speedPresetNormal must be 0..600\"}");
            return;
        }

        int16_t speedPresetTurbo;
        if (parseInt16Param(req, "speedPresetTurbo", 0, SPEED_LIMIT_MAX, &speedPresetTurbo)) {
            working.speedPresetTurbo = speedPresetTurbo;
            speedPresetValuesProvided = true;
            PA_LOG_INFO(TAG, "[CFG] speedPresetTurbo updated to %d", (int)speedPresetTurbo);
            changed = true;
        } else if (req->hasParam("speedPresetTurbo", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"speedPresetTurbo must be 0..600\"}");
            return;
        }
        if (speedPresetValuesProvided &&
            !speedPresetValuesAreUnique(working.speedPresetSlow, working.speedPresetNormal,
                                        working.speedPresetTurbo)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"speed presets must be distinct values\"}");
            return;
        }
        if (speedPresetValuesProvided && !speedLimitMaxProvided) {
            working.speedLimitMax = speedPresetValueForId(
                activePresetBefore, working.speedPresetSlow, working.speedPresetNormal,
                working.speedPresetTurbo);
            PA_LOG_INFO(TAG, "[CFG] speedLimitMax derived from active preset %s -> %d",
                        speedPresetIdToString(activePresetBefore), (int)working.speedLimitMax);
        }
        if (speedLimitMaxProvided) {
            if (!resolveSpeedPresetForLimit(working.speedLimitMax, working.speedPresetSlow,
                                            working.speedPresetNormal, working.speedPresetTurbo,
                                            &activePresetAfter)) {
                activePresetAfter = SpeedPresetId::Normal;
            }
        }

        uint32_t webDriveTimeoutMs;
        if (parseUint32Param(req, "webDriveTimeoutMs", 100, 5000, &webDriveTimeoutMs)) {
            working.webDriveTimeoutMs = webDriveTimeoutMs;
            PA_LOG_INFO(TAG, "[CFG] webDriveTimeoutMs updated to %u", (unsigned)webDriveTimeoutMs);
            changed = true;
        } else if (req->hasParam("webDriveTimeoutMs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"webDriveTimeoutMs must be 100..5000\"}");
            return;
        }

        uint32_t sbusTimeoutMs;
        if (parseUint32Param(req, "sbusTimeoutMs", 50, 5000, &sbusTimeoutMs)) {
            working.sbusTimeoutMs = sbusTimeoutMs;
            PA_LOG_INFO(TAG, "[CFG] sbusTimeoutMs updated to %u", (unsigned)sbusTimeoutMs);
            changed = true;
        } else if (req->hasParam("sbusTimeoutMs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"sbusTimeoutMs must be 50..5000\"}");
            return;
        }

        bool boolValue;
        bool stationaryProvided = false;

        if (parseBoolParam(req, "stationary", &boolValue)) {
            stationaryProvided = true;
            working.stationary = boolValue;
            PA_LOG_INFO(TAG, "[CFG] stationary updated to %s", boolValue ? "true" : "false");
            changed = true;
        } else if (req->hasParam("stationary", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"stationary must be true/false or 1/0\"}");
            return;
        }

        if (req->hasParam("logLevel", true)) {
            int16_t lvl = 0;
            if (parseInt16Param(req, "logLevel", 1, 3, &lvl)) {
                working.logLevel = (uint8_t)lvl;
                PA_LOG_INFO(TAG, "[CFG] logLevel updated to %d", (int)lvl);
                changed = true;
            } else {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"logLevel must be 1 (Error), 2 (Info), or 3 "
                          "(Debug)\"}");
                return;
            }
        }

        if (req->hasParam("rcInputMode", true)) {
            RcInputMode mode;
            if (!parseRcInputMode(req->getParam("rcInputMode", true)->value().c_str(), &mode)) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"rcInputMode must be standard_pwm, "
                          "single_sbus, or dual_sbus\"}");
                return;
            }
            working.rcInputMode = mode;
            PA_LOG_INFO(TAG, "[CFG] rcInputMode updated to %s", rcModeToString(mode));
            changed = true;
        }

        if (parseBoolParam(req, "sbusRecvCh2", &boolValue)) {
            working.sbusRecvCh2 = boolValue;
            changed = true;
        } else if (req->hasParam("sbusRecvCh2", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"sbusRecvCh2 must be true/false or 1/0\"}");
            return;
        }

        uint8_t auxLedPin = 0;
        if (parseUint8Param(req, "aux_led_pin", AUX_LED_PIN_DISABLED, AUX_LED_PIN_MAX, &auxLedPin)) {
            working.auxLedPin = auxLedPin;
            changed = true;
        } else if (req->hasParam("aux_led_pin", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"aux_led_pin must be 0..3\"}");
            return;
        }

        uint8_t auxLedCount = 0;
        if (parseUint8Param(req, "aux_led_count", AUX_LED_COUNT_DEFAULT, AUX_LED_COUNT_MAX,
                            &auxLedCount)) {
            working.auxLedCount = auxLedCount;
            changed = true;
        } else if (req->hasParam("aux_led_count", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"aux_led_count must be 1..255\"}");
            return;
        }

        if (req->hasParam("plain", true)) {
            JsonDocument bodyDoc;
            const String rawBody = req->getParam("plain", true)->value();
            DeserializationError jsonErr = deserializeJson(bodyDoc, rawBody.c_str());
            if (jsonErr) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"invalid json body\"}");
                return;
            }

            JsonVariantConst rcBody = bodyDoc["rc"];
            if (!rcBody.isNull()) {
                if (rcBody["sbusTimeoutMs"].is<uint32_t>()) {
                    uint32_t parsedSbusTimeout = rcBody["sbusTimeoutMs"].as<uint32_t>();
                    if (parsedSbusTimeout < 50 || parsedSbusTimeout > 5000) {
                        req->send(400, "application/json",
                                  "{\"ok\":false,\"error\":\"rc.sbusTimeoutMs must be 50..5000\"}");
                        return;
                    }
                    working.sbusTimeoutMs = parsedSbusTimeout;
                    changed = true;
                } else if (!rcBody["sbusTimeoutMs"].isNull()) {
                    req->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"rc.sbusTimeoutMs must be integer\"}");
                    return;
                }
            }

            JsonVariantConst rcSbus = rcBody["sbus"];
            if (!rcSbus.isNull()) {
                if (rcSbus["recvCh2"].is<bool>()) {
                    working.sbusRecvCh2 = rcSbus["recvCh2"].as<bool>();
                    changed = true;
                } else if (!rcSbus["recvCh2"].isNull()) {
                    req->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"rc.sbus.recvCh2 must be boolean\"}");
                    return;
                }
            }

            if (bodyDoc["aux_led_pin"].is<uint8_t>()) {
                uint8_t parsed = bodyDoc["aux_led_pin"].as<uint8_t>();
                if (!auxLedPinSettingValid(parsed)) {
                    req->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"aux_led_pin must be 0..3\"}");
                    return;
                }
                working.auxLedPin = parsed;
                changed = true;
            } else if (!bodyDoc["aux_led_pin"].isNull()) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"aux_led_pin must be integer 0..3\"}");
                return;
            }

            if (bodyDoc["aux_led_count"].is<uint8_t>()) {
                uint8_t parsed = bodyDoc["aux_led_count"].as<uint8_t>();
                if (parsed < AUX_LED_COUNT_DEFAULT) {
                    req->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"aux_led_count must be 1..255\"}");
                    return;
                }
                working.auxLedCount = parsed;
                changed = true;
            } else if (!bodyDoc["aux_led_count"].isNull()) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"aux_led_count must be integer 1..255\"}");
                return;
            }

            JsonVariantConst domeCfg = bodyDoc["dome"];
            if (!domeCfg.isNull()) {
                if (domeCfg["wifiPeerIp"].is<const char*>()) {
                    if (!parseDomeWifiPeerIp(domeCfg["wifiPeerIp"].as<const char*>(),
                                            working.domeWifiPeerIp,
                                            sizeof(working.domeWifiPeerIp))) {
                        req->send(400, "application/json",
                                  "{\"ok\":false,\"error\":\"dome.wifiPeerIp must be empty or a valid IPv4 address\"}");
                        return;
                    }
                    changed = true;
                } else if (!domeCfg["wifiPeerIp"].isNull()) {
                    req->send(400, "application/json",
                              "{\"ok\":false,\"error\":\"dome.wifiPeerIp must be a string\"}");
                    return;
                }
            }
        }

        struct BoolCfgField {
            const char* param;
            bool* field;
        };

        BoolCfgField boolFields[] = {
            {"enableArm1", &working.enableArm1},
            {"enableArm2", &working.enableArm2},
            {"enableAux1", &working.enableAux1},
            {"enableAux2", &working.enableAux2},
            {"enableAux3", &working.enableAux3},
            {"enableDome", &working.enableDome},
            {"enableRcCh1", &working.enableRcCh1},
            {"enableRcCh2", &working.enableRcCh2},
            {"enableRcCh3", &working.enableRcCh3},
            {"enableRcCh4", &working.enableRcCh4},
            {"enableRcCh5", &working.enableRcCh5},
            {"enableRcCh6", &working.enableRcCh6},
            {"enableS1Hoverboard", &working.enableS1Hoverboard},
            {"enableS2Sound", &working.enableS2Sound},
            {"enableS3DomeCtrl", &working.enableS3DomeCtrl},
        };

        for (size_t i = 0; i < sizeof(boolFields) / sizeof(boolFields[0]); ++i) {
            if (!req->hasParam(boolFields[i].param, true)) {
                continue;
            }
            if (!parseBoolValue(req->getParam(boolFields[i].param, true)->value().c_str(),
                                &boolValue)) {
                char err[160];
                snprintf(err, sizeof(err),
                         "{\"ok\":false,\"error\":\"%s must be true/false or 1/0\"}",
                         boolFields[i].param);
                req->send(400, "application/json", err);
                return;
            }
            *boolFields[i].field = boolValue;
            PA_LOG_INFO(TAG, "[CFG] %s updated to %s", boolFields[i].param,
                        boolValue ? "true" : "false");
            changed = true;
        }

        uint16_t domeU16;
        if (parseUint16Param(req, "domeNeutralUs", 1000, 2000, &domeU16)) {
            working.domeNeutralUs = domeU16;
            changed = true;
        } else if (req->hasParam("domeNeutralUs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeNeutralUs must be 1000..2000\"}");
            return;
        }

        if (parseUint16Param(req, "domeMinPulseUs", 1000, 2000, &domeU16)) {
            working.domeMinPulseUs = domeU16;
            changed = true;
        } else if (req->hasParam("domeMinPulseUs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeMinPulseUs must be 1000..2000\"}");
            return;
        }

        if (parseUint16Param(req, "domeMaxPulseUs", 1000, 2000, &domeU16)) {
            working.domeMaxPulseUs = domeU16;
            changed = true;
        } else if (req->hasParam("domeMaxPulseUs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeMaxPulseUs must be 1000..2000\"}");
            return;
        }

        uint8_t domePct;
        if (parseUint8Param(req, "domeSpeedLimitPct", 0, 100, &domePct)) {
            working.domeSpeedLimitPct = domePct;
            changed = true;
        } else if (req->hasParam("domeSpeedLimitPct", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeSpeedLimitPct must be 0..100\"}");
            return;
        }

        if (req->hasParam("domeWifiPeerIp", true)) {
            const char* rawPeerIp = req->getParam("domeWifiPeerIp", true)->value().c_str();
            if (!parseDomeWifiPeerIp(rawPeerIp, working.domeWifiPeerIp,
                                     sizeof(working.domeWifiPeerIp))) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"domeWifiPeerIp must be empty or a valid IPv4 address\"}");
                return;
            }
            changed = true;
        }

        struct ServoCalField {
            const char* param;
            uint16_t* field;
        };

        ServoCalField servoCalFields[] = {
            {"arm1OpenUs", &working.arm1OpenUs},
            {"arm1CloseUs", &working.arm1CloseUs},
            {"arm2OpenUs", &working.arm2OpenUs},
            {"arm2CloseUs", &working.arm2CloseUs},
            {"aux1OpenUs", &working.aux1OpenUs},
            {"aux1CloseUs", &working.aux1CloseUs},
            {"aux2OpenUs", &working.aux2OpenUs},
            {"aux2CloseUs", &working.aux2CloseUs},
            {"aux3OpenUs", &working.aux3OpenUs},
            {"aux3CloseUs", &working.aux3CloseUs},
        };

        for (size_t i = 0; i < sizeof(servoCalFields) / sizeof(servoCalFields[0]); ++i) {
            if (!req->hasParam(servoCalFields[i].param, true)) {
                continue;
            }
            uint16_t pulseUs = 0;
            if (!parseUint16Param(req, servoCalFields[i].param, kServoPulseMinUs,
                                  kServoPulseMaxUs, &pulseUs)) {
                char err[192];
                snprintf(err, sizeof(err),
                         "{\"ok\":false,\"error\":\"%s must be 500..2500\"}",
                         servoCalFields[i].param);
                req->send(400, "application/json", err);
                return;
            }
            *servoCalFields[i].field = pulseUs;
            changed = true;
        }

        struct ServoTypeField {
            const char* param;
            ServoComponentType* field;
        };

        ServoTypeField servoTypeFields[] = {
            {"arm1Type", &working.arm1Type}, {"arm2Type", &working.arm2Type},
            {"aux1Type", &working.aux1Type}, {"aux2Type", &working.aux2Type},
            {"aux3Type", &working.aux3Type},
        };

        for (size_t i = 0; i < sizeof(servoTypeFields) / sizeof(servoTypeFields[0]); ++i) {
            if (!req->hasParam(servoTypeFields[i].param, true)) {
                continue;
            }

            const char* raw = req->getParam(servoTypeFields[i].param, true)->value().c_str();
            ServoComponentType parsed = SERVO_COMP_NONE;
            if (strcmp(raw, "0") == 0 || strcmp(raw, "1") == 0 || strcmp(raw, "2") == 0 ||
                strcmp(raw, "3") == 0) {
                parsed = (ServoComponentType)atoi(raw);
            } else {
                parsed = parseServoCompType(raw);
            }

            if (!isValidServoCompType((uint8_t)parsed)) {
                char err[180];
                snprintf(err, sizeof(err),
                         "{\"ok\":false,\"error\":\"%s must be none/mg996r/mg90s/rgb\"}",
                         servoTypeFields[i].param);
                req->send(400, "application/json", err);
                return;
            }

            *servoTypeFields[i].field = parsed;
            changed = true;
        }

        struct BindingField {
            const char* param;
            RcBindingConfig* field;
        };

        BindingField bindingFields[] = {
            {"rcPwmDriveSpeed", &working.rcPwmDriveSpeed},
            {"rcPwmDriveSteer", &working.rcPwmDriveSteer},
            {"rcPwmDomeSpeed", &working.rcPwmDomeSpeed},
            {"rcPwmArm1", &working.rcPwmArm1},
            {"rcPwmArm2", &working.rcPwmArm2},
            {"rcPwmSound", &working.rcPwmSound},
            {"rcSbusDriveSpeed", &working.rcSbusDriveSpeed},
            {"rcSbusDriveSteer", &working.rcSbusDriveSteer},
            {"rcSbusDomeSpeed", &working.rcSbusDomeSpeed},
            {"rcSbusArm1", &working.rcSbusArm1},
            {"rcSbusArm2", &working.rcSbusArm2},
            {"rcSbusSound", &working.rcSbusSound},
        };

        for (size_t i = 0; i < sizeof(bindingFields) / sizeof(bindingFields[0]); ++i) {
            if (!req->hasParam(bindingFields[i].param, true)) {
                continue;
            }

            RcInputMode activeMode = working.rcInputMode;

            RcBindingConfig parsed;
            if (parseRcBindingParam(req, bindingFields[i].param, &parsed)) {
                *bindingFields[i].field = parsed;
                changed = true;
                continue;
            }

            // Compatibility: arm/sound legacy fields may carry trigger-format values.
            RcTriggerBinding triggerParsed;
            if ((strcmp(bindingFields[i].param, "rcPwmArm1") == 0 ||
                 strcmp(bindingFields[i].param, "rcSbusArm1") == 0) &&
                parseRcTriggerParam(req, bindingFields[i].param, &triggerParsed)) {
                working.rcArm1 = triggerParsed;
                changed = true;
                continue;
            }

            if ((strcmp(bindingFields[i].param, "rcPwmArm2") == 0 ||
                 strcmp(bindingFields[i].param, "rcSbusArm2") == 0) &&
                parseRcTriggerParam(req, bindingFields[i].param, &triggerParsed)) {
                working.rcArm2 = triggerParsed;
                changed = true;
                continue;
            }

            if ((strcmp(bindingFields[i].param, "rcPwmSound") == 0 ||
                 strcmp(bindingFields[i].param, "rcSbusSound") == 0) &&
                parseRcTriggerParam(req, bindingFields[i].param, &triggerParsed)) {
                working.rcSound = triggerParsed;
                changed = true;
                continue;
            }

            char err[180];
            snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"invalid binding value for %s\"}",
                     bindingFields[i].param);
            req->send(400, "application/json", err);
            return;
        }

        struct TriggerField {
            const char* param;
            RcTriggerBinding* field;
        };

        TriggerField triggerFields[] = {
            {"rcArm1", &working.rcArm1},
            {"rcArm2", &working.rcArm2},
            {"rcAux1", &working.rcAux1},
            {"rcAux2", &working.rcAux2},
            {"rcAux3", &working.rcAux3},
            {"rcSound", &working.rcSound},
            {"rcOpMode", &working.rcOpmode},
            {"rcFree0", &working.rcFree0},
            {"rcFree1", &working.rcFree1},
            {"rcFree2", &working.rcFree2},
            {"rcFree3", &working.rcFree3},
            // Compatibility aliases from in-flight UI names
            {"rcPwmAux1", &working.rcAux1},
            {"rcPwmAux2", &working.rcAux2},
            {"rcPwmAux3", &working.rcAux3},
            {"rcPwmOpMode", &working.rcOpmode},
            {"rcPwmFree0", &working.rcFree0},
            {"rcPwmFree1", &working.rcFree1},
            {"rcPwmFree2", &working.rcFree2},
            {"rcPwmFree3", &working.rcFree3},
            {"rcSbusAux1", &working.rcAux1},
            {"rcSbusAux2", &working.rcAux2},
            {"rcSbusAux3", &working.rcAux3},
            {"rcSbusOpMode", &working.rcOpmode},
            {"rcSbusFree0", &working.rcFree0},
            {"rcSbusFree1", &working.rcFree1},
            {"rcSbusFree2", &working.rcFree2},
            {"rcSbusFree3", &working.rcFree3},
        };

        for (size_t i = 0; i < sizeof(triggerFields) / sizeof(triggerFields[0]); ++i) {
            if (!req->hasParam(triggerFields[i].param, true)) {
                continue;
            }

            RcInputMode activeMode = working.rcInputMode;

            RcTriggerBinding parsed;
            if (!parseRcTriggerParam(req, triggerFields[i].param, &parsed)) {
                char err[180];
                snprintf(err, sizeof(err),
                         "{\"ok\":false,\"error\":\"invalid trigger binding value for %s\"}",
                         triggerFields[i].param);
                req->send(400, "application/json", err);
                return;
            }

            if (!triggerTargetAllowedByRuntime(parsed)) {
                char err[220];
                snprintf(
                    err, sizeof(err),
                    "{\"ok\":false,\"error\":\"dome_seq is not available until Phase 4 (%s)\"}",
                    triggerFields[i].param);
                req->send(400, "application/json", err);
                return;
            }

            *triggerFields[i].field = parsed;
            changed = true;
        }

        if (!changed) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"no supported config fields supplied\"}");
            return;
        }

        bool queueDriveOn = false;
        bool queueDomeOn = false;
        bool wasStationary = false;
        bool wasDomeEnabled = false;

        taskENTER_CRITICAL(&robotStateMux);
        wasStationary = robotState.stationary;
        wasDomeEnabled = robotState.cfg_enable_dome;
        robotState.cfg_speedLimitMax = working.speedLimitMax;
        robotState.cfg_speedPresetSlow = working.speedPresetSlow;
        robotState.cfg_speedPresetNormal = working.speedPresetNormal;
        robotState.cfg_speedPresetTurbo = working.speedPresetTurbo;
        robotState.cfg_speedPresetActive = activePresetAfter;
        robotState.cfg_sbusTimeoutMs = working.sbusTimeoutMs;
        robotState.cfg_webDriveTimeoutMs = working.webDriveTimeoutMs;
        robotState.cfg_stationary = working.stationary;
        robotState.stationary = working.stationary;
        robotState.cfg_logLevel = working.logLevel;
        robotState.cfg_rc_input_mode = working.rcInputMode;
        robotState.cfg_single_sbus_use_ch2 = working.sbusRecvCh2;
        robotState.cfg_aux_led_pin = working.auxLedPin;
        robotState.cfg_aux_led_count = working.auxLedCount;

        robotState.cfg_enable_arm1 = working.enableArm1;
        robotState.cfg_enable_arm2 = working.enableArm2;
        robotState.cfg_enable_aux1 = working.enableAux1;
        robotState.cfg_enable_aux2 = working.enableAux2;
        robotState.cfg_enable_aux3 = working.enableAux3;
        robotState.cfg_enable_dome = working.enableDome;
        robotState.cfg_enable_rc_ch1 = working.enableRcCh1;
        robotState.cfg_enable_rc_ch2 = working.enableRcCh2;
        robotState.cfg_enable_rc_ch3 = working.enableRcCh3;
        robotState.cfg_enable_rc_ch4 = working.enableRcCh4;
        robotState.cfg_enable_rc_ch5 = working.enableRcCh5;
        robotState.cfg_enable_rc_ch6 = working.enableRcCh6;
        robotState.cfg_enable_s1_hoverboard = working.enableS1Hoverboard;
        robotState.cfg_enable_s2_sound = working.enableS2Sound;
        robotState.cfg_enable_s3_dome_ctrl = working.enableS3DomeCtrl;

        robotState.cfg_dome_neutral_us = working.domeNeutralUs;
        robotState.cfg_dome_min_pulse_us = working.domeMinPulseUs;
        robotState.cfg_dome_max_pulse_us = working.domeMaxPulseUs;
        robotState.cfg_dome_speed_limit_pct = working.domeSpeedLimitPct;
        snprintf(robotState.cfg_dome_wifi_peer_ip, sizeof(robotState.cfg_dome_wifi_peer_ip), "%s",
                 working.domeWifiPeerIp);

        robotState.cfg_arm1_type = working.arm1Type;
        robotState.cfg_arm2_type = working.arm2Type;
        robotState.cfg_aux1_type = working.aux1Type;
        robotState.cfg_aux2_type = working.aux2Type;
        robotState.cfg_aux3_type = working.aux3Type;
        robotState.cfg_arm1_open_us = working.arm1OpenUs;
        robotState.cfg_arm1_close_us = working.arm1CloseUs;
        robotState.cfg_arm2_open_us = working.arm2OpenUs;
        robotState.cfg_arm2_close_us = working.arm2CloseUs;
        robotState.cfg_aux1_open_us = working.aux1OpenUs;
        robotState.cfg_aux1_close_us = working.aux1CloseUs;
        robotState.cfg_aux2_open_us = working.aux2OpenUs;
        robotState.cfg_aux2_close_us = working.aux2CloseUs;
        robotState.cfg_aux3_open_us = working.aux3OpenUs;
        robotState.cfg_aux3_close_us = working.aux3CloseUs;

        robotState.cfg_rc_pwm_drive_speed = working.rcPwmDriveSpeed;
        robotState.cfg_rc_pwm_drive_steer = working.rcPwmDriveSteer;
        robotState.cfg_rc_pwm_dome_speed = working.rcPwmDomeSpeed;
        robotState.cfg_rc_pwm_arm1 = working.rcPwmArm1;
        robotState.cfg_rc_pwm_arm2 = working.rcPwmArm2;
        robotState.cfg_rc_pwm_sound = working.rcPwmSound;

        robotState.cfg_rc_sbus_drive_speed = working.rcSbusDriveSpeed;
        robotState.cfg_rc_sbus_drive_steer = working.rcSbusDriveSteer;
        robotState.cfg_rc_sbus_dome_speed = working.rcSbusDomeSpeed;
        robotState.cfg_rc_sbus_arm1 = working.rcSbusArm1;
        robotState.cfg_rc_sbus_arm2 = working.rcSbusArm2;
        robotState.cfg_rc_sbus_sound = working.rcSbusSound;

        robotState.cfg_rc_arm1 = working.rcArm1;
        robotState.cfg_rc_arm2 = working.rcArm2;
        robotState.cfg_rc_aux1 = working.rcAux1;
        robotState.cfg_rc_aux2 = working.rcAux2;
        robotState.cfg_rc_aux3 = working.rcAux3;
        robotState.cfg_rc_sound = working.rcSound;
        robotState.cfg_rc_opmode = working.rcOpmode;
        robotState.cfg_rc_free0 = working.rcFree0;
        robotState.cfg_rc_free1 = working.rcFree1;
        robotState.cfg_rc_free2 = working.rcFree2;
        robotState.cfg_rc_free3 = working.rcFree3;
        if (stationaryProvided && wasStationary && !robotState.stationary) {
            queueDriveOn = true;
        }
        if (!wasDomeEnabled && robotState.cfg_enable_dome) {
            queueDomeOn = true;
        }
        taskEXIT_CRITICAL(&robotStateMux);

        if (queueDriveOn) {
            audioQueuePlaySlot(AUDIO_SLOT_SYS_DRIVE_ON, SRC_INTERNAL);
        }
        if (queueDomeOn) {
            audioQueuePlaySlot(AUDIO_SLOT_SYS_DOME_ON, SRC_INTERNAL);
        }

        if (!saveConfigToNvs()) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist config\"}");
            return;
        }

        ConfigSnapshot snap;
        captureConfigSnapshot(&snap);
        JsonDocument doc;
        if (!populateConfigJson(doc, snap)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"config json build failed\"}");
            return;
        }
        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response stream alloc failed\"}");
            return;
        }
        serializeJson(doc, *stream);
        req->send(stream);
    });
}
