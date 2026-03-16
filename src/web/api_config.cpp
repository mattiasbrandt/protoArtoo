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
#include <ESPAsyncWebServer.h>
#include <string.h>

#include "api_helpers.h"
#include "config.h"
#include "logging.h"
#include "robot_state.h"
#include "servo_component_helpers.h"

static const char* TAG = "WebServer";

extern bool saveConfigToNvs();

namespace {

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
    // Phase 4 deferred: RC_ACTION_DOME_SEQ is intentionally blocked at API level
    // until DomeLinkTask routing is implemented in runtime trigger handling.
    return binding.target != RC_ACTION_DOME_SEQ;
}

bool buildConfigJson(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return false;
    }

    int16_t speedLimitMax;
    uint32_t webDriveTimeoutMs;
    bool ch8ModeLock;
    bool stationary;
    RcInputMode rcInputMode;
    bool enableArm1, enableArm2, enableAux1, enableAux2, enableAux3, enableDome;
    bool enableRcCh1, enableRcCh2, enableRcCh3, enableRcCh4, enableRcCh5, enableRcCh6;
    bool enableS1Hoverboard, enableS2Sound, enableS3DomeCtrl;
    uint16_t domeNeutralUs, domeMinPulseUs, domeMaxPulseUs;
    uint8_t domeSpeedLimitPct;
    ServoComponentType arm1Type, arm2Type, aux1Type, aux2Type, aux3Type;

    RcBindingConfig rcPwmDriveSpeed, rcPwmDriveSteer, rcPwmDriveLimit, rcPwmDomeSpeed, rcPwmArm1,
        rcPwmArm2, rcPwmSound;
    RcBindingConfig rcSbusDriveSpeed, rcSbusDriveSteer, rcSbusDriveLimit, rcSbusDomeSpeed,
        rcSbusArm1, rcSbusArm2, rcSbusSound;

    RcTriggerBinding rcArm1, rcArm2, rcAux1, rcAux2, rcAux3, rcSound, rcOpmode;
    RcTriggerBinding rcFree0, rcFree1, rcFree2, rcFree3;

    taskENTER_CRITICAL(&robotStateMux);
    speedLimitMax = robotState.cfg_speedLimitMax;
    webDriveTimeoutMs = robotState.cfg_webDriveTimeoutMs;
    ch8ModeLock = robotState.cfg_ch8ModeLock;
    stationary = robotState.cfg_stationary;
    rcInputMode = robotState.cfg_rc_input_mode;

    enableArm1 = robotState.cfg_enable_arm1;
    enableArm2 = robotState.cfg_enable_arm2;
    enableAux1 = robotState.cfg_enable_aux1;
    enableAux2 = robotState.cfg_enable_aux2;
    enableAux3 = robotState.cfg_enable_aux3;
    enableDome = robotState.cfg_enable_dome;
    enableRcCh1 = robotState.cfg_enable_rc_ch1;
    enableRcCh2 = robotState.cfg_enable_rc_ch2;
    enableRcCh3 = robotState.cfg_enable_rc_ch3;
    enableRcCh4 = robotState.cfg_enable_rc_ch4;
    enableRcCh5 = robotState.cfg_enable_rc_ch5;
    enableRcCh6 = robotState.cfg_enable_rc_ch6;
    enableS1Hoverboard = robotState.cfg_enable_s1_hoverboard;
    enableS2Sound = robotState.cfg_enable_s2_sound;
    enableS3DomeCtrl = robotState.cfg_enable_s3_dome_ctrl;

    domeNeutralUs = robotState.cfg_dome_neutral_us;
    domeMinPulseUs = robotState.cfg_dome_min_pulse_us;
    domeMaxPulseUs = robotState.cfg_dome_max_pulse_us;
    domeSpeedLimitPct = robotState.cfg_dome_speed_limit_pct;

    arm1Type = robotState.cfg_arm1_type;
    arm2Type = robotState.cfg_arm2_type;
    aux1Type = robotState.cfg_aux1_type;
    aux2Type = robotState.cfg_aux2_type;
    aux3Type = robotState.cfg_aux3_type;

    rcPwmDriveSpeed = robotState.cfg_rc_pwm_drive_speed;
    rcPwmDriveSteer = robotState.cfg_rc_pwm_drive_steer;
    rcPwmDriveLimit = robotState.cfg_rc_pwm_drive_limit;
    rcPwmDomeSpeed = robotState.cfg_rc_pwm_dome_speed;
    rcPwmArm1 = robotState.cfg_rc_pwm_arm1;
    rcPwmArm2 = robotState.cfg_rc_pwm_arm2;
    rcPwmSound = robotState.cfg_rc_pwm_sound;

    rcSbusDriveSpeed = robotState.cfg_rc_sbus_drive_speed;
    rcSbusDriveSteer = robotState.cfg_rc_sbus_drive_steer;
    rcSbusDriveLimit = robotState.cfg_rc_sbus_drive_limit;
    rcSbusDomeSpeed = robotState.cfg_rc_sbus_dome_speed;
    rcSbusArm1 = robotState.cfg_rc_sbus_arm1;
    rcSbusArm2 = robotState.cfg_rc_sbus_arm2;
    rcSbusSound = robotState.cfg_rc_sbus_sound;

    rcArm1 = robotState.cfg_rc_arm1;
    rcArm2 = robotState.cfg_rc_arm2;
    rcAux1 = robotState.cfg_rc_aux1;
    rcAux2 = robotState.cfg_rc_aux2;
    rcAux3 = robotState.cfg_rc_aux3;
    rcSound = robotState.cfg_rc_sound;
    rcOpmode = robotState.cfg_rc_opmode;
    rcFree0 = robotState.cfg_rc_free0;
    rcFree1 = robotState.cfg_rc_free1;
    rcFree2 = robotState.cfg_rc_free2;
    rcFree3 = robotState.cfg_rc_free3;
    taskEXIT_CRITICAL(&robotStateMux);

    char rcPwmDriveSpeedStr[48] = {};
    char rcPwmDriveSteerStr[48] = {};
    char rcPwmDriveLimitStr[48] = {};
    char rcPwmDomeSpeedStr[48] = {};
    char rcPwmArm1Str[48] = {};
    char rcPwmArm2Str[48] = {};
    char rcPwmSoundStr[48] = {};
    char rcSbusDriveSpeedStr[48] = {};
    char rcSbusDriveSteerStr[48] = {};
    char rcSbusDriveLimitStr[48] = {};
    char rcSbusDomeSpeedStr[48] = {};
    char rcSbusArm1Str[48] = {};
    char rcSbusArm2Str[48] = {};
    char rcSbusSoundStr[48] = {};

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

    if (!formatRcBindingConfig(rcPwmDriveSpeedStr, sizeof(rcPwmDriveSpeedStr), rcPwmDriveSpeed) ||
        !formatRcBindingConfig(rcPwmDriveSteerStr, sizeof(rcPwmDriveSteerStr), rcPwmDriveSteer) ||
        !formatRcBindingConfig(rcPwmDriveLimitStr, sizeof(rcPwmDriveLimitStr), rcPwmDriveLimit) ||
        !formatRcBindingConfig(rcPwmDomeSpeedStr, sizeof(rcPwmDomeSpeedStr), rcPwmDomeSpeed) ||
        !formatRcBindingConfig(rcPwmArm1Str, sizeof(rcPwmArm1Str), rcPwmArm1) ||
        !formatRcBindingConfig(rcPwmArm2Str, sizeof(rcPwmArm2Str), rcPwmArm2) ||
        !formatRcBindingConfig(rcPwmSoundStr, sizeof(rcPwmSoundStr), rcPwmSound) ||
        !formatRcBindingConfig(rcSbusDriveSpeedStr, sizeof(rcSbusDriveSpeedStr),
                               rcSbusDriveSpeed) ||
        !formatRcBindingConfig(rcSbusDriveSteerStr, sizeof(rcSbusDriveSteerStr),
                               rcSbusDriveSteer) ||
        !formatRcBindingConfig(rcSbusDriveLimitStr, sizeof(rcSbusDriveLimitStr),
                               rcSbusDriveLimit) ||
        !formatRcBindingConfig(rcSbusDomeSpeedStr, sizeof(rcSbusDomeSpeedStr), rcSbusDomeSpeed) ||
        !formatRcBindingConfig(rcSbusArm1Str, sizeof(rcSbusArm1Str), rcSbusArm1) ||
        !formatRcBindingConfig(rcSbusArm2Str, sizeof(rcSbusArm2Str), rcSbusArm2) ||
        !formatRcBindingConfig(rcSbusSoundStr, sizeof(rcSbusSoundStr), rcSbusSound) ||
        !triggerBindingToUiString(rcArm1Str, sizeof(rcArm1Str), rcArm1) ||
        !triggerBindingToUiString(rcArm2Str, sizeof(rcArm2Str), rcArm2) ||
        !triggerBindingToUiString(rcAux1Str, sizeof(rcAux1Str), rcAux1) ||
        !triggerBindingToUiString(rcAux2Str, sizeof(rcAux2Str), rcAux2) ||
        !triggerBindingToUiString(rcAux3Str, sizeof(rcAux3Str), rcAux3) ||
        !triggerBindingToUiString(rcSoundStr, sizeof(rcSoundStr), rcSound) ||
        !triggerBindingToUiString(rcOpmodeStr, sizeof(rcOpmodeStr), rcOpmode) ||
        !triggerBindingToUiString(rcFree0Str, sizeof(rcFree0Str), rcFree0) ||
        !triggerBindingToUiString(rcFree1Str, sizeof(rcFree1Str), rcFree1) ||
        !triggerBindingToUiString(rcFree2Str, sizeof(rcFree2Str), rcFree2) ||
        !triggerBindingToUiString(rcFree3Str, sizeof(rcFree3Str), rcFree3)) {
        return false;
    }

    int n = snprintf(
        buffer, bufferSize,
        "{"
        "\"speedLimitMax\":%d,"
        "\"webDriveTimeoutMs\":%lu,"
        "\"ch8ModeLock\":%s,"
        "\"stationary\":%s,"
        "\"rcInputMode\":\"%s\","
        "\"enableArm1\":%s,\"enableArm2\":%s,\"enableAux1\":%s,\"enableAux2\":%s,"
        "\"enableAux3\":%s,\"enableDome\":%s,"
        "\"enableRcCh1\":%s,\"enableRcCh2\":%s,\"enableRcCh3\":%s,\"enableRcCh4\":%s,"
        "\"enableRcCh5\":%s,\"enableRcCh6\":%s,"
        "\"enableS1Hoverboard\":%s,\"enableS2Sound\":%s,\"enableS3DomeCtrl\":%s,"
        "\"arm1Type\":%u,\"arm2Type\":%u,\"aux1Type\":%u,\"aux2Type\":%u,\"aux3Type\":%u,"
        "\"domeNeutralUs\":%u,\"domeMinPulseUs\":%u,\"domeMaxPulseUs\":%u,\"domeSpeedLimitPct\":%u,"
        "\"rcPwmDriveSpeed\":\"%s\",\"rcPwmDriveSteer\":\"%s\",\"rcPwmDriveLimit\":\"%s\","
        "\"rcPwmDomeSpeed\":\"%s\",\"rcPwmArm1\":\"%s\",\"rcPwmArm2\":\"%s\",\"rcPwmSound\":\"%s\","
        "\"rcSbusDriveSpeed\":\"%s\",\"rcSbusDriveSteer\":\"%s\",\"rcSbusDriveLimit\":\"%s\","
        "\"rcSbusDomeSpeed\":\"%s\",\"rcSbusArm1\":\"%s\",\"rcSbusArm2\":\"%s\",\"rcSbusSound\":\"%"
        "s\","
        "\"rcArm1\":\"%s\",\"rcArm2\":\"%s\",\"rcAux1\":\"%s\",\"rcAux2\":\"%s\","
        "\"rcAux3\":\"%s\",\"rcSound\":\"%s\",\"rcOpMode\":\"%s\","
        "\"rcFree0\":\"%s\",\"rcFree1\":\"%s\",\"rcFree2\":\"%s\",\"rcFree3\":\"%s\""
        "}",
        (int)speedLimitMax, (unsigned long)webDriveTimeoutMs, ch8ModeLock ? "true" : "false",
        stationary ? "true" : "false", rcModeToString(rcInputMode), enableArm1 ? "true" : "false",
        enableArm2 ? "true" : "false", enableAux1 ? "true" : "false", enableAux2 ? "true" : "false",
        enableAux3 ? "true" : "false", enableDome ? "true" : "false",
        enableRcCh1 ? "true" : "false", enableRcCh2 ? "true" : "false",
        enableRcCh3 ? "true" : "false", enableRcCh4 ? "true" : "false",
        enableRcCh5 ? "true" : "false", enableRcCh6 ? "true" : "false",
        enableS1Hoverboard ? "true" : "false", enableS2Sound ? "true" : "false",
        enableS3DomeCtrl ? "true" : "false", (unsigned int)arm1Type, (unsigned int)arm2Type,
        (unsigned int)aux1Type, (unsigned int)aux2Type, (unsigned int)aux3Type,
        (unsigned int)domeNeutralUs, (unsigned int)domeMinPulseUs, (unsigned int)domeMaxPulseUs,
        (unsigned int)domeSpeedLimitPct, rcPwmDriveSpeedStr, rcPwmDriveSteerStr, rcPwmDriveLimitStr,
        rcPwmDomeSpeedStr, rcPwmArm1Str, rcPwmArm2Str, rcPwmSoundStr, rcSbusDriveSpeedStr,
        rcSbusDriveSteerStr, rcSbusDriveLimitStr, rcSbusDomeSpeedStr, rcSbusArm1Str, rcSbusArm2Str,
        rcSbusSoundStr, rcArm1Str, rcArm2Str, rcAux1Str, rcAux2Str, rcAux3Str, rcSoundStr,
        rcOpmodeStr, rcFree0Str, rcFree1Str, rcFree2Str, rcFree3Str);

    return n > 0 && (size_t)n < bufferSize;
}

}  // namespace

void registerConfigRoutes(AsyncWebServer& server) {
    static char configJsonBuf[4096];

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!buildConfigJson(configJsonBuf, sizeof(configJsonBuf))) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"config json build failed\"}");
            return;
        }
        req->send(200, "application/json", configJsonBuf);
    });

    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool changed = false;

        int16_t speedLimitMax;
        if (parseInt16Param(req, "speedLimitMax", 0, SPEED_LIMIT_MAX, &speedLimitMax)) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.cfg_speedLimitMax = speedLimitMax;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_INFO(TAG, "[CFG] speedLimitMax updated to %d", (int)speedLimitMax);
            changed = true;
        } else if (req->hasParam("speedLimitMax", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"speedLimitMax must be 0..600\"}");
            return;
        }

        uint32_t webDriveTimeoutMs;
        if (parseUint32Param(req, "webDriveTimeoutMs", 100, 5000, &webDriveTimeoutMs)) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.cfg_webDriveTimeoutMs = webDriveTimeoutMs;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_INFO(TAG, "[CFG] webDriveTimeoutMs updated to %u", (unsigned)webDriveTimeoutMs);
            changed = true;
        } else if (req->hasParam("webDriveTimeoutMs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"webDriveTimeoutMs must be 100..5000\"}");
            return;
        }

        bool boolValue;
        if (parseBoolParam(req, "ch8ModeLock", &boolValue)) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.cfg_ch8ModeLock = boolValue;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_INFO(TAG, "[CFG] ch8ModeLock updated to %s", boolValue ? "true" : "false");
            changed = true;
        } else if (req->hasParam("ch8ModeLock", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"ch8ModeLock must be true/false or 1/0\"}");
            return;
        }

        if (parseBoolParam(req, "stationary", &boolValue)) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.cfg_stationary = boolValue;
            robotState.stationary = boolValue;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_INFO(TAG, "[CFG] stationary updated to %s", boolValue ? "true" : "false");
            changed = true;
        } else if (req->hasParam("stationary", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"stationary must be true/false or 1/0\"}");
            return;
        }

        if (req->hasParam("rcInputMode", true)) {
            RcInputMode mode;
            if (!parseRcInputMode(req->getParam("rcInputMode", true)->value().c_str(), &mode)) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"rcInputMode must be standard_pwm, "
                          "single_sbus, or dual_sbus\"}");
                return;
            }
            taskENTER_CRITICAL(&robotStateMux);
            robotState.cfg_rc_input_mode = mode;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_INFO(TAG, "[CFG] rcInputMode updated to %s", rcModeToString(mode));
            changed = true;
        }

        struct BoolCfgField {
            const char* param;
            bool* field;
        };

        BoolCfgField boolFields[] = {
            {"enableArm1", &robotState.cfg_enable_arm1},
            {"enableArm2", &robotState.cfg_enable_arm2},
            {"enableAux1", &robotState.cfg_enable_aux1},
            {"enableAux2", &robotState.cfg_enable_aux2},
            {"enableAux3", &robotState.cfg_enable_aux3},
            {"enableDome", &robotState.cfg_enable_dome},
            {"enableRcCh1", &robotState.cfg_enable_rc_ch1},
            {"enableRcCh2", &robotState.cfg_enable_rc_ch2},
            {"enableRcCh3", &robotState.cfg_enable_rc_ch3},
            {"enableRcCh4", &robotState.cfg_enable_rc_ch4},
            {"enableRcCh5", &robotState.cfg_enable_rc_ch5},
            {"enableRcCh6", &robotState.cfg_enable_rc_ch6},
            {"enableS1Hoverboard", &robotState.cfg_enable_s1_hoverboard},
            {"enableS2Sound", &robotState.cfg_enable_s2_sound},
            {"enableS3DomeCtrl", &robotState.cfg_enable_s3_dome_ctrl},
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
            taskENTER_CRITICAL(&robotStateMux);
            *boolFields[i].field = boolValue;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_INFO(TAG, "[CFG] %s updated to %s", boolFields[i].param,
                        boolValue ? "true" : "false");
            changed = true;
        }

        uint16_t domeU16;
        if (parseUint16Param(req, "domeNeutralUs", 1000, 2000, &domeU16)) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.cfg_dome_neutral_us = domeU16;
            taskEXIT_CRITICAL(&robotStateMux);
            changed = true;
        } else if (req->hasParam("domeNeutralUs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeNeutralUs must be 1000..2000\"}");
            return;
        }

        if (parseUint16Param(req, "domeMinPulseUs", 1000, 2000, &domeU16)) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.cfg_dome_min_pulse_us = domeU16;
            taskEXIT_CRITICAL(&robotStateMux);
            changed = true;
        } else if (req->hasParam("domeMinPulseUs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeMinPulseUs must be 1000..2000\"}");
            return;
        }

        if (parseUint16Param(req, "domeMaxPulseUs", 1000, 2000, &domeU16)) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.cfg_dome_max_pulse_us = domeU16;
            taskEXIT_CRITICAL(&robotStateMux);
            changed = true;
        } else if (req->hasParam("domeMaxPulseUs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeMaxPulseUs must be 1000..2000\"}");
            return;
        }

        uint8_t domePct;
        if (parseUint8Param(req, "domeSpeedLimitPct", 0, 100, &domePct)) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.cfg_dome_speed_limit_pct = domePct;
            taskEXIT_CRITICAL(&robotStateMux);
            changed = true;
        } else if (req->hasParam("domeSpeedLimitPct", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeSpeedLimitPct must be 0..100\"}");
            return;
        }

        struct ServoTypeField {
            const char* param;
            ServoComponentType* field;
        };

        ServoTypeField servoTypeFields[] = {
            {"arm1Type", &robotState.cfg_arm1_type}, {"arm2Type", &robotState.cfg_arm2_type},
            {"aux1Type", &robotState.cfg_aux1_type}, {"aux2Type", &robotState.cfg_aux2_type},
            {"aux3Type", &robotState.cfg_aux3_type},
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

            taskENTER_CRITICAL(&robotStateMux);
            *servoTypeFields[i].field = parsed;
            taskEXIT_CRITICAL(&robotStateMux);
            changed = true;
        }

        struct BindingField {
            const char* param;
            RcBindingConfig* field;
        };

        BindingField bindingFields[] = {
            {"rcPwmDriveSpeed", &robotState.cfg_rc_pwm_drive_speed},
            {"rcPwmDriveSteer", &robotState.cfg_rc_pwm_drive_steer},
            {"rcPwmDriveLimit", &robotState.cfg_rc_pwm_drive_limit},
            {"rcPwmDomeSpeed", &robotState.cfg_rc_pwm_dome_speed},
            {"rcPwmArm1", &robotState.cfg_rc_pwm_arm1},
            {"rcPwmArm2", &robotState.cfg_rc_pwm_arm2},
            {"rcPwmSound", &robotState.cfg_rc_pwm_sound},
            {"rcSbusDriveSpeed", &robotState.cfg_rc_sbus_drive_speed},
            {"rcSbusDriveSteer", &robotState.cfg_rc_sbus_drive_steer},
            {"rcSbusDriveLimit", &robotState.cfg_rc_sbus_drive_limit},
            {"rcSbusDomeSpeed", &robotState.cfg_rc_sbus_dome_speed},
            {"rcSbusArm1", &robotState.cfg_rc_sbus_arm1},
            {"rcSbusArm2", &robotState.cfg_rc_sbus_arm2},
            {"rcSbusSound", &robotState.cfg_rc_sbus_sound},
        };

        for (size_t i = 0; i < sizeof(bindingFields) / sizeof(bindingFields[0]); ++i) {
            if (!req->hasParam(bindingFields[i].param, true)) {
                continue;
            }

            RcInputMode activeMode;
            taskENTER_CRITICAL(&robotStateMux);
            activeMode = robotState.cfg_rc_input_mode;
            taskEXIT_CRITICAL(&robotStateMux);

            RcBindingConfig parsed;
            if (parseRcBindingParam(req, bindingFields[i].param, &parsed)) {
                if (!sourceAllowedForMode(activeMode, parsed.source)) {
                    char err[200];
                    snprintf(
                        err, sizeof(err),
                        "{\"ok\":false,\"error\":\"invalid source for %s in current rcInputMode\"}",
                        bindingFields[i].param);
                    req->send(400, "application/json", err);
                    return;
                }
                taskENTER_CRITICAL(&robotStateMux);
                *bindingFields[i].field = parsed;
                taskEXIT_CRITICAL(&robotStateMux);
                changed = true;
                continue;
            }

            // Compatibility: arm/sound legacy fields may carry trigger-format values.
            RcTriggerBinding triggerParsed;
            if ((strcmp(bindingFields[i].param, "rcPwmArm1") == 0 ||
                 strcmp(bindingFields[i].param, "rcSbusArm1") == 0) &&
                parseRcTriggerParam(req, bindingFields[i].param, &triggerParsed)) {
                if (!sourceAllowedForMode(activeMode, triggerParsed.source)) {
                    char err[200];
                    snprintf(
                        err, sizeof(err),
                        "{\"ok\":false,\"error\":\"invalid source for %s in current rcInputMode\"}",
                        bindingFields[i].param);
                    req->send(400, "application/json", err);
                    return;
                }
                taskENTER_CRITICAL(&robotStateMux);
                robotState.cfg_rc_arm1 = triggerParsed;
                taskEXIT_CRITICAL(&robotStateMux);
                changed = true;
                continue;
            }

            if ((strcmp(bindingFields[i].param, "rcPwmArm2") == 0 ||
                 strcmp(bindingFields[i].param, "rcSbusArm2") == 0) &&
                parseRcTriggerParam(req, bindingFields[i].param, &triggerParsed)) {
                if (!sourceAllowedForMode(activeMode, triggerParsed.source)) {
                    char err[200];
                    snprintf(
                        err, sizeof(err),
                        "{\"ok\":false,\"error\":\"invalid source for %s in current rcInputMode\"}",
                        bindingFields[i].param);
                    req->send(400, "application/json", err);
                    return;
                }
                taskENTER_CRITICAL(&robotStateMux);
                robotState.cfg_rc_arm2 = triggerParsed;
                taskEXIT_CRITICAL(&robotStateMux);
                changed = true;
                continue;
            }

            if ((strcmp(bindingFields[i].param, "rcPwmSound") == 0 ||
                 strcmp(bindingFields[i].param, "rcSbusSound") == 0) &&
                parseRcTriggerParam(req, bindingFields[i].param, &triggerParsed)) {
                if (!sourceAllowedForMode(activeMode, triggerParsed.source)) {
                    char err[200];
                    snprintf(
                        err, sizeof(err),
                        "{\"ok\":false,\"error\":\"invalid source for %s in current rcInputMode\"}",
                        bindingFields[i].param);
                    req->send(400, "application/json", err);
                    return;
                }
                taskENTER_CRITICAL(&robotStateMux);
                robotState.cfg_rc_sound = triggerParsed;
                taskEXIT_CRITICAL(&robotStateMux);
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
            {"rcArm1", &robotState.cfg_rc_arm1},
            {"rcArm2", &robotState.cfg_rc_arm2},
            {"rcAux1", &robotState.cfg_rc_aux1},
            {"rcAux2", &robotState.cfg_rc_aux2},
            {"rcAux3", &robotState.cfg_rc_aux3},
            {"rcSound", &robotState.cfg_rc_sound},
            {"rcOpMode", &robotState.cfg_rc_opmode},
            {"rcFree0", &robotState.cfg_rc_free0},
            {"rcFree1", &robotState.cfg_rc_free1},
            {"rcFree2", &robotState.cfg_rc_free2},
            {"rcFree3", &robotState.cfg_rc_free3},
            // Compatibility aliases from in-flight UI names
            {"rcPwmAux1", &robotState.cfg_rc_aux1},
            {"rcPwmAux2", &robotState.cfg_rc_aux2},
            {"rcPwmAux3", &robotState.cfg_rc_aux3},
            {"rcPwmOpMode", &robotState.cfg_rc_opmode},
            {"rcPwmFree0", &robotState.cfg_rc_free0},
            {"rcPwmFree1", &robotState.cfg_rc_free1},
            {"rcPwmFree2", &robotState.cfg_rc_free2},
            {"rcPwmFree3", &robotState.cfg_rc_free3},
            {"rcSbusAux1", &robotState.cfg_rc_aux1},
            {"rcSbusAux2", &robotState.cfg_rc_aux2},
            {"rcSbusAux3", &robotState.cfg_rc_aux3},
            {"rcSbusOpMode", &robotState.cfg_rc_opmode},
            {"rcSbusFree0", &robotState.cfg_rc_free0},
            {"rcSbusFree1", &robotState.cfg_rc_free1},
            {"rcSbusFree2", &robotState.cfg_rc_free2},
            {"rcSbusFree3", &robotState.cfg_rc_free3},
        };

        for (size_t i = 0; i < sizeof(triggerFields) / sizeof(triggerFields[0]); ++i) {
            if (!req->hasParam(triggerFields[i].param, true)) {
                continue;
            }

            RcInputMode activeMode;
            taskENTER_CRITICAL(&robotStateMux);
            activeMode = robotState.cfg_rc_input_mode;
            taskEXIT_CRITICAL(&robotStateMux);

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

            if (!sourceAllowedForMode(activeMode, parsed.source)) {
                char err[200];
                snprintf(
                    err, sizeof(err),
                    "{\"ok\":false,\"error\":\"invalid source for %s in current rcInputMode\"}",
                    triggerFields[i].param);
                req->send(400, "application/json", err);
                return;
            }

            taskENTER_CRITICAL(&robotStateMux);
            *triggerFields[i].field = parsed;
            taskEXIT_CRITICAL(&robotStateMux);
            changed = true;
        }

        if (!changed) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"no supported config fields supplied\"}");
            return;
        }

        if (!saveConfigToNvs()) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist config\"}");
            return;
        }

        if (!buildConfigJson(configJsonBuf, sizeof(configJsonBuf))) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"config json build failed\"}");
            return;
        }
        req->send(200, "application/json", configJsonBuf);
    });
}
