// =============================================================================
// src/web/api_config_apply.cpp
//
// Apply Core for POST /api/config (ADR 0011). See api_config_apply.h.
// =============================================================================

#include "api_config_apply.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "api_helpers.h"
#include "config.h"
#include "drive_speed_preset.h"
#include "servo_component_helpers.h"

namespace {

constexpr uint16_t kServoPulseMinUs = 500;
constexpr uint16_t kServoPulseMaxUs = 2500;

void appendApplied(ConfigAppliedFields* applied, const char* fmt, ...) {
    if (applied->count >= ConfigAppliedFields::kMaxLines) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(applied->lines[applied->count], sizeof(applied->lines[0]), fmt, args);
    va_end(args);
    applied->count++;
}

void setError(ConfigApplyResult* result, const char* message) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
}

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

bool paramInt16(const ConfigParamSource& params, const char* name, int16_t minValue,
                 int16_t maxValue, int16_t* out) {
    const char* raw = configParamGet(params, name);
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    int16_t value = 0;
    if (!parseDriveValue(raw, &value)) {
        return false;
    }
    if (value < minValue || value > maxValue) {
        return false;
    }
    *out = value;
    return true;
}

bool paramUint32(const ConfigParamSource& params, const char* name, uint32_t minValue,
                  uint32_t maxValue, uint32_t* out) {
    const char* raw = configParamGet(params, name);
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    uint32_t value = 0;
    if (!parseUint32Value(raw, &value)) {
        return false;
    }
    if (value < minValue || value > maxValue) {
        return false;
    }
    *out = value;
    return true;
}

bool paramUint16(const ConfigParamSource& params, const char* name, uint16_t minValue,
                  uint16_t maxValue, uint16_t* out) {
    uint32_t temp = 0;
    if (!paramUint32(params, name, minValue, maxValue, &temp)) {
        return false;
    }
    *out = (uint16_t)temp;
    return true;
}

bool paramUint8(const ConfigParamSource& params, const char* name, uint8_t minValue,
                 uint8_t maxValue, uint8_t* out) {
    uint32_t temp = 0;
    if (!paramUint32(params, name, minValue, maxValue, &temp)) {
        return false;
    }
    *out = (uint8_t)temp;
    return true;
}

bool paramBool(const ConfigParamSource& params, const char* name, bool* out) {
    const char* raw = configParamGet(params, name);
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    return parseBoolValue(raw, out);
}

}  // namespace

void configApply(const ConfigParamSource& params, ConfigSnapshot* working,
                  bool domeEnabledBefore, ConfigApplyResult* result) {
    *result = ConfigApplyResult{};

    bool speedLimitMaxProvided = false;
    bool speedPresetValuesProvided = false;
    SpeedPresetId activePresetBefore =
        normalizeSpeedPresetId((uint8_t)working->drive.speedPresetActive);
    SpeedPresetId activePresetAfter = activePresetBefore;

    int16_t speedLimitMax;
    if (paramInt16(params, "speedLimitMax", 0, SPEED_LIMIT_MAX, &speedLimitMax)) {
        working->drive.speedLimitMax = speedLimitMax;
        speedLimitMaxProvided = true;
        appendApplied(&result->applied, "[CFG] speedLimitMax updated to %d", (int)speedLimitMax);
        result->changed = true;
    } else if (configParamHas(params, "speedLimitMax")) {
        setError(result, "speedLimitMax must be 0..600");
        return;
    }

    int16_t speedPresetSlow;
    if (paramInt16(params, "speedPresetSlow", 0, SPEED_LIMIT_MAX, &speedPresetSlow)) {
        working->drive.speedPresetSlow = speedPresetSlow;
        speedPresetValuesProvided = true;
        appendApplied(&result->applied, "[CFG] speedPresetSlow updated to %d", (int)speedPresetSlow);
        result->changed = true;
    } else if (configParamHas(params, "speedPresetSlow")) {
        setError(result, "speedPresetSlow must be 0..600");
        return;
    }

    int16_t speedPresetNormal;
    if (paramInt16(params, "speedPresetNormal", 0, SPEED_LIMIT_MAX, &speedPresetNormal)) {
        working->drive.speedPresetNormal = speedPresetNormal;
        speedPresetValuesProvided = true;
        appendApplied(&result->applied, "[CFG] speedPresetNormal updated to %d", (int)speedPresetNormal);
        result->changed = true;
    } else if (configParamHas(params, "speedPresetNormal")) {
        setError(result, "speedPresetNormal must be 0..600");
        return;
    }

    int16_t speedPresetTurbo;
    if (paramInt16(params, "speedPresetTurbo", 0, SPEED_LIMIT_MAX, &speedPresetTurbo)) {
        working->drive.speedPresetTurbo = speedPresetTurbo;
        speedPresetValuesProvided = true;
        appendApplied(&result->applied, "[CFG] speedPresetTurbo updated to %d", (int)speedPresetTurbo);
        result->changed = true;
    } else if (configParamHas(params, "speedPresetTurbo")) {
        setError(result, "speedPresetTurbo must be 0..600");
        return;
    }

    if (speedPresetValuesProvided &&
        !speedPresetValuesAreUnique(working->drive.speedPresetSlow, working->drive.speedPresetNormal,
                                     working->drive.speedPresetTurbo)) {
        setError(result, "speed presets must be distinct values");
        return;
    }
    if (speedPresetValuesProvided && !speedLimitMaxProvided) {
        working->drive.speedLimitMax = speedPresetValueForId(
            activePresetBefore, working->drive.speedPresetSlow, working->drive.speedPresetNormal,
            working->drive.speedPresetTurbo);
        appendApplied(&result->applied, "[CFG] speedLimitMax derived from active preset %s -> %d",
                      speedPresetIdToString(activePresetBefore), (int)working->drive.speedLimitMax);
    }
    if (speedLimitMaxProvided) {
        if (!resolveSpeedPresetForLimit(working->drive.speedLimitMax, working->drive.speedPresetSlow,
                                        working->drive.speedPresetNormal,
                                        working->drive.speedPresetTurbo, &activePresetAfter)) {
            activePresetAfter = SpeedPresetId::Normal;
        }
    }

    uint32_t webDriveTimeoutMs;
    if (paramUint32(params, "webDriveTimeoutMs", 100, 5000, &webDriveTimeoutMs)) {
        working->drive.webDriveTimeoutMs = webDriveTimeoutMs;
        appendApplied(&result->applied, "[CFG] webDriveTimeoutMs updated to %u", (unsigned)webDriveTimeoutMs);
        result->changed = true;
    } else if (configParamHas(params, "webDriveTimeoutMs")) {
        setError(result, "webDriveTimeoutMs must be 100..5000");
        return;
    }

    uint32_t sbusTimeoutMs;
    if (paramUint32(params, "sbusTimeoutMs", 50, 5000, &sbusTimeoutMs)) {
        working->drive.sbusTimeoutMs = sbusTimeoutMs;
        appendApplied(&result->applied, "[CFG] sbusTimeoutMs updated to %u", (unsigned)sbusTimeoutMs);
        result->changed = true;
    } else if (configParamHas(params, "sbusTimeoutMs")) {
        setError(result, "sbusTimeoutMs must be 50..5000");
        return;
    }

    bool boolValue;
    bool stationaryProvided = false;

    if (paramBool(params, "stationary", &boolValue)) {
        stationaryProvided = true;
        working->system.stationary = boolValue;
        appendApplied(&result->applied, "[CFG] stationary updated to %s", boolValue ? "true" : "false");
        result->changed = true;
    } else if (configParamHas(params, "stationary")) {
        setError(result, "stationary must be true/false or 1/0");
        return;
    }
    (void)stationaryProvided;  // stationary release cue stays in the shell (ADR 0012)

    if (configParamHas(params, "logLevel")) {
        int16_t lvl = 0;
        if (paramInt16(params, "logLevel", 1, 4, &lvl)) {
            working->system.logLevel = (uint8_t)lvl;
            appendApplied(&result->applied, "[CFG] logLevel updated to %d", (int)lvl);
            result->changed = true;
        } else {
            setError(result, "logLevel must be 1 (Error), 2 (Warning), 3 (Info), or 4 (Debug)");
            return;
        }
    }

    if (configParamHas(params, "rcInputMode")) {
        RcInputMode mode;
        if (!parseRcInputMode(configParamGet(params, "rcInputMode"), &mode)) {
            setError(result, "rcInputMode must be standard_pwm, single_sbus, or dual_sbus");
            return;
        }
        working->system.rc_input_mode = mode;
        appendApplied(&result->applied, "[CFG] rcInputMode updated to %s", rcModeToString(mode));
        result->changed = true;
    }

    if (paramBool(params, "sbusRecvCh2", &boolValue)) {
        working->system.single_sbus_use_ch2 = boolValue;
        result->changed = true;
    } else if (configParamHas(params, "sbusRecvCh2")) {
        setError(result, "sbusRecvCh2 must be true/false or 1/0");
        return;
    }

    uint8_t auxLedPin = 0;
    if (paramUint8(params, "aux_led_pin", AUX_LED_PIN_DISABLED, AUX_LED_PIN_MAX, &auxLedPin)) {
        working->servo.aux_led_pin = auxLedPin;
        result->changed = true;
    } else if (configParamHas(params, "aux_led_pin")) {
        setError(result, "aux_led_pin must be 0..3");
        return;
    }

    uint8_t auxLedCount = 0;
    if (paramUint8(params, "aux_led_count", AUX_LED_COUNT_DEFAULT, AUX_LED_COUNT_MAX, &auxLedCount)) {
        working->servo.aux_led_count = auxLedCount;
        result->changed = true;
    } else if (configParamHas(params, "aux_led_count")) {
        setError(result, "aux_led_count must be 1..255");
        return;
    }

    if (configParamHas(params, "plain")) {
        JsonDocument bodyDoc;
        DeserializationError jsonErr = deserializeJson(bodyDoc, configParamGet(params, "plain"));
        if (jsonErr) {
            setError(result, "invalid json body");
            return;
        }

        JsonVariantConst rcBody = bodyDoc["rc"];
        if (!rcBody.isNull()) {
            if (rcBody["sbusTimeoutMs"].is<uint32_t>()) {
                uint32_t parsedSbusTimeout = rcBody["sbusTimeoutMs"].as<uint32_t>();
                if (parsedSbusTimeout < 50 || parsedSbusTimeout > 5000) {
                    setError(result, "rc.sbusTimeoutMs must be 50..5000");
                    return;
                }
                working->drive.sbusTimeoutMs = parsedSbusTimeout;
                result->changed = true;
            } else if (!rcBody["sbusTimeoutMs"].isNull()) {
                setError(result, "rc.sbusTimeoutMs must be integer");
                return;
            }
        }

        JsonVariantConst rcSbus = rcBody["sbus"];
        if (!rcSbus.isNull()) {
            if (rcSbus["recvCh2"].is<bool>()) {
                working->system.single_sbus_use_ch2 = rcSbus["recvCh2"].as<bool>();
                result->changed = true;
            } else if (!rcSbus["recvCh2"].isNull()) {
                setError(result, "rc.sbus.recvCh2 must be boolean");
                return;
            }
        }

        if (bodyDoc["aux_led_pin"].is<uint8_t>()) {
            uint8_t parsed = bodyDoc["aux_led_pin"].as<uint8_t>();
            if (!auxLedPinSettingValid(parsed)) {
                setError(result, "aux_led_pin must be 0..3");
                return;
            }
            working->servo.aux_led_pin = parsed;
            result->changed = true;
        } else if (!bodyDoc["aux_led_pin"].isNull()) {
            setError(result, "aux_led_pin must be integer 0..3");
            return;
        }

        if (bodyDoc["aux_led_count"].is<uint8_t>()) {
            uint8_t parsed = bodyDoc["aux_led_count"].as<uint8_t>();
            if (parsed < AUX_LED_COUNT_DEFAULT) {
                setError(result, "aux_led_count must be 1..255");
                return;
            }
            working->servo.aux_led_count = parsed;
            result->changed = true;
        } else if (!bodyDoc["aux_led_count"].isNull()) {
            setError(result, "aux_led_count must be integer 1..255");
            return;
        }

        JsonVariantConst protoR2linkCfg = bodyDoc["protoR2link"];
        if (!protoR2linkCfg.isNull()) {
            if (protoR2linkCfg["wifiPeerIp"].is<const char*>()) {
                if (!parseDomeWifiPeerIp(protoR2linkCfg["wifiPeerIp"].as<const char*>(),
                                        working->dome.dome_wifi_peer_ip,
                                        sizeof(working->dome.dome_wifi_peer_ip))) {
                    setError(result, "protoR2link.wifiPeerIp must be empty or a valid IPv4 address");
                    return;
                }
                result->changed = true;
            } else if (!protoR2linkCfg["wifiPeerIp"].isNull()) {
                setError(result, "protoR2link.wifiPeerIp must be a string");
                return;
            }
        }
    }

    struct BoolCfgField {
        const char* param;
        bool* field;
    };

    BoolCfgField boolFields[] = {
        {"enableArm1", &working->system.enable_arm1},
        {"enableArm2", &working->system.enable_arm2},
        {"enableAux1", &working->system.enable_aux1},
        {"enableAux2", &working->system.enable_aux2},
        {"enableAux3", &working->system.enable_aux3},
        {"enableDomeEsc", &working->system.enable_dome_esc},
        {"enableRcCh1", &working->system.enable_rc_ch1},
        {"enableRcCh2", &working->system.enable_rc_ch2},
        {"enableRcCh3", &working->system.enable_rc_ch3},
        {"enableRcCh4", &working->system.enable_rc_ch4},
        {"enableRcCh5", &working->system.enable_rc_ch5},
        {"enableRcCh6", &working->system.enable_rc_ch6},
        {"enableDrive", &working->system.enable_drive},
        {"enableAudio", &working->system.enable_audio},
        {"enableProtoR2link", &working->system.enable_protor2link},
    };

    for (size_t i = 0; i < sizeof(boolFields) / sizeof(boolFields[0]); ++i) {
        if (!configParamHas(params, boolFields[i].param)) {
            continue;
        }
        if (!parseBoolValue(configParamGet(params, boolFields[i].param), &boolValue)) {
            char err[160];
            snprintf(err, sizeof(err), "%s must be true/false or 1/0", boolFields[i].param);
            setError(result, err);
            return;
        }
        *boolFields[i].field = boolValue;
        appendApplied(&result->applied, "[CFG] %s updated to %s", boolFields[i].param,
                      boolValue ? "true" : "false");
        result->changed = true;
    }

    uint16_t domeU16;
    if (paramUint16(params, "domeEscNeutralUs", 1000, 2000, &domeU16)) {
        working->dome.dome_neutral_us = domeU16;
        result->changed = true;
    } else if (configParamHas(params, "domeEscNeutralUs")) {
        setError(result, "domeEscNeutralUs must be 1000..2000");
        return;
    }

    if (paramUint16(params, "domeEscMinPulseUs", 1000, 2000, &domeU16)) {
        working->dome.dome_min_pulse_us = domeU16;
        result->changed = true;
    } else if (configParamHas(params, "domeEscMinPulseUs")) {
        setError(result, "domeEscMinPulseUs must be 1000..2000");
        return;
    }

    if (paramUint16(params, "domeEscMaxPulseUs", 1000, 2000, &domeU16)) {
        working->dome.dome_max_pulse_us = domeU16;
        result->changed = true;
    } else if (configParamHas(params, "domeEscMaxPulseUs")) {
        setError(result, "domeEscMaxPulseUs must be 1000..2000");
        return;
    }

    uint8_t domePct;
    if (paramUint8(params, "domeEscSpeedLimitPct", 0, 100, &domePct)) {
        working->dome.dome_speed_limit_pct = domePct;
        result->changed = true;
    } else if (configParamHas(params, "domeEscSpeedLimitPct")) {
        setError(result, "domeEscSpeedLimitPct must be 0..100");
        return;
    }

    if (configParamHas(params, "protoR2linkWifiPeerIp")) {
        const char* rawPeerIp = configParamGet(params, "protoR2linkWifiPeerIp");
        if (!parseDomeWifiPeerIp(rawPeerIp, working->dome.dome_wifi_peer_ip,
                                 sizeof(working->dome.dome_wifi_peer_ip))) {
            setError(result, "protoR2linkWifiPeerIp must be empty or a valid IPv4 address");
            return;
        }
        result->changed = true;
    }

    bool domeRndEnableBool;
    if (paramBool(params, "domeEscRndEnable", &domeRndEnableBool)) {
        working->dome.dome_rnd_enable = domeRndEnableBool;
        appendApplied(&result->applied, "[CFG] domeEscRndEnable updated to %s",
                      domeRndEnableBool ? "true" : "false");
        result->changed = true;
    } else if (configParamHas(params, "domeEscRndEnable")) {
        setError(result, "domeEscRndEnable must be true/false or 1/0");
        return;
    }

    uint8_t domeRndSpeedPct;
    if (paramUint8(params, "domeEscRndSpeedPct", 5, 100, &domeRndSpeedPct)) {
        working->dome.dome_rnd_speed_pct = domeRndSpeedPct;
        appendApplied(&result->applied, "[CFG] domeEscRndSpeedPct updated to %u", (unsigned)domeRndSpeedPct);
        result->changed = true;
    } else if (configParamHas(params, "domeEscRndSpeedPct")) {
        setError(result, "domeEscRndSpeedPct must be 5..100");
        return;
    }

    uint8_t domeRndPauseMin;
    if (paramUint8(params, "domeEscRndPauseMin", 1, 120, &domeRndPauseMin)) {
        working->dome.dome_rnd_pause_min = domeRndPauseMin;
        appendApplied(&result->applied, "[CFG] domeEscRndPauseMin updated to %u", (unsigned)domeRndPauseMin);
        result->changed = true;
    } else if (configParamHas(params, "domeEscRndPauseMin")) {
        setError(result, "domeEscRndPauseMin must be 1..120");
        return;
    }

    uint8_t domeRndPauseMax;
    if (paramUint8(params, "domeEscRndPauseMax", 1, 120, &domeRndPauseMax)) {
        working->dome.dome_rnd_pause_max = domeRndPauseMax;
        appendApplied(&result->applied, "[CFG] domeEscRndPauseMax updated to %u", (unsigned)domeRndPauseMax);
        result->changed = true;
    } else if (configParamHas(params, "domeEscRndPauseMax")) {
        setError(result, "domeEscRndPauseMax must be 1..120");
        return;
    }

    uint16_t domeRndMoveMs;
    if (paramUint16(params, "domeEscRndMoveMs", 500, 10000, &domeRndMoveMs)) {
        working->dome.dome_rnd_move_ms = domeRndMoveMs;
        appendApplied(&result->applied, "[CFG] domeEscRndMoveMs updated to %u", (unsigned)domeRndMoveMs);
        result->changed = true;
    } else if (configParamHas(params, "domeEscRndMoveMs")) {
        setError(result, "domeEscRndMoveMs must be 500..10000");
        return;
    }

    struct ServoCalField {
        const char* param;
        uint16_t* field;
    };

    ServoCalField servoCalFields[] = {
        {"arm1OpenUs", &working->servo.arm1_open_us},   {"arm1CloseUs", &working->servo.arm1_close_us},
        {"arm2OpenUs", &working->servo.arm2_open_us},   {"arm2CloseUs", &working->servo.arm2_close_us},
        {"aux1OpenUs", &working->servo.aux1_open_us},   {"aux1CloseUs", &working->servo.aux1_close_us},
        {"aux2OpenUs", &working->servo.aux2_open_us},   {"aux2CloseUs", &working->servo.aux2_close_us},
        {"aux3OpenUs", &working->servo.aux3_open_us},   {"aux3CloseUs", &working->servo.aux3_close_us},
    };

    for (size_t i = 0; i < sizeof(servoCalFields) / sizeof(servoCalFields[0]); ++i) {
        if (!configParamHas(params, servoCalFields[i].param)) {
            continue;
        }
        uint16_t pulseUs = 0;
        if (!paramUint16(params, servoCalFields[i].param, kServoPulseMinUs, kServoPulseMaxUs, &pulseUs)) {
            char err[192];
            snprintf(err, sizeof(err), "%s must be 500..2500", servoCalFields[i].param);
            setError(result, err);
            return;
        }
        *servoCalFields[i].field = pulseUs;
        result->changed = true;
    }

    struct ServoTypeField {
        const char* param;
        ServoComponentType* field;
    };

    ServoTypeField servoTypeFields[] = {
        {"arm1Type", &working->servo.arm1_type}, {"arm2Type", &working->servo.arm2_type},
        {"aux1Type", &working->servo.aux1_type}, {"aux2Type", &working->servo.aux2_type},
        {"aux3Type", &working->servo.aux3_type},
    };

    for (size_t i = 0; i < sizeof(servoTypeFields) / sizeof(servoTypeFields[0]); ++i) {
        if (!configParamHas(params, servoTypeFields[i].param)) {
            continue;
        }

        const char* raw = configParamGet(params, servoTypeFields[i].param);
        ServoComponentType parsed = SERVO_COMP_NONE;
        if (strcmp(raw, "0") == 0 || strcmp(raw, "1") == 0 || strcmp(raw, "2") == 0 ||
            strcmp(raw, "3") == 0) {
            parsed = (ServoComponentType)atoi(raw);
        } else {
            parsed = parseServoCompType(raw);
        }

        if (!isValidServoCompType((uint8_t)parsed)) {
            char err[180];
            snprintf(err, sizeof(err), "%s must be none/mg996r/mg90s/rgb", servoTypeFields[i].param);
            setError(result, err);
            return;
        }

        *servoTypeFields[i].field = parsed;
        result->changed = true;
    }

    if (!result->changed) {
        setError(result, "no supported config fields supplied");
        return;
    }

    working->drive.speedPresetActive = activePresetAfter;
    result->actions.playDomeOnCue = !domeEnabledBefore && working->system.enable_dome_esc;

    return;
}
