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
//   RobotState cfg_* fields under robotStateMux, and persisted via configSave().
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
#include "config_store.h"
#include "logging.h"
#include "robot_state.h"
#include "servo_component_helpers.h"

#include <Preferences.h>

static const char* TAG = "WebServer";

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




bool triggerTargetAllowedByRuntime(const RcTriggerBinding& binding) {
    return true;
}

static const char* const kDomeSeqPayloads[] = {
    "DM:PIES", "DM:LOW", "DM:OPENALL", "DM:FLUTTER", "DM:BLOOM",
    "DM:SCREAM", "DM:OVERLOAD", "DM:HEART", "DM:ALARM", "DM:DISCO",
    "DM:VADER", "DM:ROCKMARCH", "DM:HELLO", "DM:LEIA", "DM:CANTINA",
    "DM:RESET", "DM:RANDOM",
    nullptr
};

static bool isValidDomeSeqPayload(const char* payload) {
    if (payload == nullptr || payload[0] == '\0') return false;
    for (int i = 0; kDomeSeqPayloads[i] != nullptr; ++i) {
        if (strcmp(payload, kDomeSeqPayloads[i]) == 0) return true;
    }
    return false;
}

bool rcMapSourceFromString(const char* raw, RcBindingSource* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(raw, "pwm") == 0) {
        *out = RC_BINDING_PWM;
        return true;
    }
    if (strcmp(raw, "sbus1") == 0) {
        *out = RC_BINDING_SBUS1;
        return true;
    }
    if (strcmp(raw, "sbus2") == 0) {
        *out = RC_BINDING_SBUS2;
        return true;
    }
    return false;
}

const char* rcMapSourceToString(RcBindingSource source) {
    switch (source) {
        case RC_BINDING_PWM:
            return "pwm";
        case RC_BINDING_SBUS1:
            return "sbus1";
        case RC_BINDING_SBUS2:
            return "sbus2";
        case RC_BINDING_NONE:
        default:
            return "none";
    }
}

bool rcMapBindingIsMapped(const RcBindingConfig& binding) {
    return binding.source != RC_BINDING_NONE &&
           rcBindingChannelIsValid(binding.source, binding.channel);
}

bool rcMapTriggerIsMapped(const RcTriggerBinding& binding) {
    return binding.source != RC_BINDING_NONE && binding.target != ROBOT_ACTION_NONE &&
           rcBindingChannelIsValid(binding.source, binding.channel);
}

bool rcMapTryReuseCalibration(const ConfigSnapshot& existing, RcBindingSource source, uint8_t channel,
                              uint16_t* min, uint16_t* center, uint16_t* max,
                              uint16_t* deadband, bool* reverse) {
    if (min == nullptr || center == nullptr || max == nullptr || deadband == nullptr ||
        reverse == nullptr) {
        return false;
    }

    const RcBindingConfig backboneBindings[] = {
        existing.rc_pwm_drive_speed, existing.rc_pwm_drive_steer, existing.rc_pwm_dome_speed,
        existing.rc_pwm_arm1,       existing.rc_pwm_arm2,       existing.rc_pwm_sound,
        existing.rc_sbus_drive_speed, existing.rc_sbus_drive_steer, existing.rc_sbus_dome_speed,
        existing.rc_sbus_arm1,      existing.rc_sbus_arm2,      existing.rc_sbus_sound,
    };
    for (size_t i = 0; i < sizeof(backboneBindings) / sizeof(backboneBindings[0]); ++i) {
        const RcBindingConfig& binding = backboneBindings[i];
        if (binding.source == source && binding.channel == channel &&
            rcBindingChannelIsValid(binding.source, binding.channel)) {
            *min = binding.min;
            *center = binding.center;
            *max = binding.max;
            *deadband = binding.deadband;
            *reverse = binding.reverse;
            return true;
        }
    }

    const RcTriggerBinding triggerBindings[] = {
        existing.rc_arm1, existing.rc_arm2, existing.rc_aux1, existing.rc_aux2, existing.rc_aux3,
        existing.rc_sound, existing.rc_opmode, existing.rc_free0, existing.rc_free1, existing.rc_free2,
        existing.rc_free3,
    };
    for (size_t i = 0; i < sizeof(triggerBindings) / sizeof(triggerBindings[0]); ++i) {
        const RcTriggerBinding& binding = triggerBindings[i];
        if (binding.source == source && binding.channel == channel &&
            rcBindingChannelIsValid(binding.source, binding.channel)) {
            *min = binding.min;
            *center = binding.center;
            *max = binding.max;
            *deadband = binding.deadband;
            *reverse = binding.reverse;
            return true;
        }
    }

    return false;
}

bool rcMapBuildBackboneBinding(RcBindingSource source, uint8_t channel,
                               const ConfigSnapshot& existing, RcBindingConfig* out) {
    if (out == nullptr || !rcBindingChannelIsValid(source, channel)) {
        return false;
    }

    RcBindingConfig binding =
        (source == RC_BINDING_PWM) ? defaultPwmBinding(channel) : defaultSbusBinding(source, channel);

    uint16_t min = binding.min;
    uint16_t center = binding.center;
    uint16_t max = binding.max;
    uint16_t deadband = binding.deadband;
    bool reverse = binding.reverse;
    if (rcMapTryReuseCalibration(existing, source, channel, &min, &center, &max, &deadband,
                                  &reverse)) {
        RcBindingConfig reused =
            makeRcBindingConfig(source, channel, min, center, max, deadband, reverse);
        if (rcBindingIsValid(reused)) {
            binding = reused;
        }
    }

    *out = binding;
    return true;
}

bool rcMapBuildTriggerBinding(const RcMapEntry& entry, const ConfigSnapshot& existing,
                              RcTriggerBinding* out) {
    if (out == nullptr || !rcBindingChannelIsValid(entry.source, entry.channel)) {
        return false;
    }

    uint16_t min = 1000;
    uint16_t center = 1500;
    uint16_t max = 2000;
    if (entry.source == RC_BINDING_SBUS1 || entry.source == RC_BINDING_SBUS2) {
        min = RC_SBUS_DEFAULT_MIN;
        center = RC_SBUS_DEFAULT_CENTER;
        max = RC_SBUS_DEFAULT_MAX;
    }
    uint16_t deadband = 0;
    bool reverse = rcTriggerDefaultReverse(entry.source, entry.channel);
    rcMapTryReuseCalibration(existing, entry.source, entry.channel, &min, &center, &max, &deadband,
                              &reverse);

    *out = makeRcTriggerBinding(entry.source, entry.channel, entry.action, entry.payload, min,
                                center, max, deadband, reverse);
    if (!rcTriggerBindingIsValid(*out)) {
        return false;
    }
    return triggerTargetAllowedByRuntime(*out);
}

RcBindingConfig rcMapSelectBackboneForMode(const ConfigSnapshot& snap, const RcBindingConfig& pwm,
                                           const RcBindingConfig& sbus) {
    if (snap.rc_input_mode == RC_INPUT_STANDARD_PWM) {
        return rcMapBindingIsMapped(pwm) ? pwm : sbus;
    }
    return rcMapBindingIsMapped(sbus) ? sbus : pwm;
}

void rcMapAppendEntry(JsonArray map, RcBindingSource source, uint8_t channel, RobotActionId action,
                      const char* payload) {
    JsonObject item = map.add<JsonObject>();
    item["source"] = rcMapSourceToString(source);
    item["channel"] = channel;
    item["action"] = robotActionIdToString(action);
    if (payload != nullptr && payload[0] != '\0') {
        item["payload"] = payload;
    }
}

}  // namespace
bool populateRcMapJson(JsonDocument& doc, const ConfigSnapshot& snap) {
    doc.clear();
    doc["mode"] = rcModeToString(snap.rc_input_mode);

    JsonArray map = doc["map"].to<JsonArray>();

    RcBindingConfig driveSpeed =
        rcMapSelectBackboneForMode(snap, snap.rc_pwm_drive_speed, snap.rc_sbus_drive_speed);
    RcBindingConfig driveSteer =
        rcMapSelectBackboneForMode(snap, snap.rc_pwm_drive_steer, snap.rc_sbus_drive_steer);
    RcBindingConfig domeSpeed =
        rcMapSelectBackboneForMode(snap, snap.rc_pwm_dome_speed, snap.rc_sbus_dome_speed);

    if (rcMapBindingIsMapped(driveSpeed)) {
        rcMapAppendEntry(map, driveSpeed.source, driveSpeed.channel, DRIVE_ACTION_SPEED, nullptr);
    }
    if (rcMapBindingIsMapped(driveSteer)) {
        rcMapAppendEntry(map, driveSteer.source, driveSteer.channel, DRIVE_ACTION_STEER, nullptr);
    }
    if (rcMapBindingIsMapped(domeSpeed)) {
        rcMapAppendEntry(map, domeSpeed.source, domeSpeed.channel, DOME_ACTION_SPEED, nullptr);
    }

    const RcTriggerBinding namedSlots[] = {snap.rc_arm1, snap.rc_arm2, snap.rc_aux1, snap.rc_aux2,
                                           snap.rc_aux3, snap.rc_opmode, snap.rc_sound, snap.rc_free0,
                                           snap.rc_free1, snap.rc_free2, snap.rc_free3};

    for (size_t i = 0; i < sizeof(namedSlots) / sizeof(namedSlots[0]); ++i) {
        const RcTriggerBinding& binding = namedSlots[i];
        if (!rcMapTriggerIsMapped(binding)) {
            continue;
        }
        rcMapAppendEntry(map, binding.source, binding.channel, binding.target, binding.marcduinoPayload);
    }

    JsonObject capacity = doc["capacity"].to<JsonObject>();
    capacity["total"] = kRcMapMaxEntries;
    capacity["used"] = map.size();
    return !doc.overflowed();
}

void clearRcMapSlots(ConfigSnapshot* working) {
    if (working == nullptr) {
        return;
    }

    working->rc_pwm_drive_speed = disabledRcBinding();
    working->rc_pwm_drive_steer = disabledRcBinding();
    working->rc_pwm_dome_speed = disabledRcBinding();
    working->rc_sbus_drive_speed = disabledRcBinding();
    working->rc_sbus_drive_steer = disabledRcBinding();
    working->rc_sbus_dome_speed = disabledRcBinding();

    working->rc_arm1 = disabledRcTriggerBinding();
    working->rc_arm2 = disabledRcTriggerBinding();
    working->rc_aux1 = disabledRcTriggerBinding();
    working->rc_aux2 = disabledRcTriggerBinding();
    working->rc_aux3 = disabledRcTriggerBinding();
    working->rc_opmode = disabledRcTriggerBinding();
    working->rc_sound = disabledRcTriggerBinding();
    working->rc_free0 = disabledRcTriggerBinding();
    working->rc_free1 = disabledRcTriggerBinding();
    working->rc_free2 = disabledRcTriggerBinding();
    working->rc_free3 = disabledRcTriggerBinding();
}

static bool triggerSlotIsFree(const RcTriggerBinding& binding) {
    return binding.source == RC_BINDING_NONE || binding.target == ROBOT_ACTION_NONE;
}

bool assignRcMapEntryToSnapshot(const RcMapEntry& entry, const ConfigSnapshot& existing,
                                ConfigSnapshot* working, char* error, size_t errorSize) {
    if (working == nullptr || error == nullptr || errorSize == 0) {
        return false;
    }

    // Slot-assignment algorithm for POST /api/rc/map
    //
    // Backbone actions are exclusive logical slots and mirror into both persisted
    // profile groups (PWM + SBUS) to keep runtime mode switching behavior stable.
    //
    // - drive_speed -> rcPwmDriveSpeed + rcSbusDriveSpeed
    // - drive_steer -> rcPwmDriveSteer + rcSbusDriveSteer
    // - dome_speed  -> rcPwmDomeSpeed  + rcSbusDomeSpeed
    //
    // Named trigger actions map to dedicated trigger slots:
    // - arm1_toggle -> rcArm1
    // - arm2_toggle -> rcArm2
    // - aux1_toggle -> rcAux1
    // - aux2_toggle -> rcAux2
    // - aux3_toggle -> rcAux3
    // - op_mode     -> rcOpmode
    //
    // All remaining trigger actions fill first-free in this order:
    // rcSound, rcFree0, rcFree1, rcFree2, rcFree3.
    RcBindingConfig backbone = disabledRcBinding();
    RcTriggerBinding trigger = disabledRcTriggerBinding();

    if (entry.action == DRIVE_ACTION_SPEED || entry.action == DRIVE_ACTION_STEER ||
        entry.action == DOME_ACTION_SPEED) {
        if (!rcMapBuildBackboneBinding(entry.source, entry.channel, existing, &backbone)) {
            snprintf(error, errorSize, "invalid backbone binding");
            return false;
        }
        if (entry.action == DRIVE_ACTION_SPEED) {
            working->rc_pwm_drive_speed = backbone;
            working->rc_sbus_drive_speed = backbone;
        } else if (entry.action == DRIVE_ACTION_STEER) {
            working->rc_pwm_drive_steer = backbone;
            working->rc_sbus_drive_steer = backbone;
        } else {
            working->rc_pwm_dome_speed = backbone;
            working->rc_sbus_dome_speed = backbone;
        }
        return true;
    }

    if (!rcMapBuildTriggerBinding(entry, existing, &trigger)) {
        snprintf(error, errorSize, "invalid trigger binding");
        return false;
    }

    if (entry.action == SERVO_ACTION_ARM1_TOGGLE) {
        if (!triggerSlotIsFree(working->rc_arm1)) {
            snprintf(error, errorSize, "conflict: arm1_toggle mapped more than once");
            return false;
        }
        working->rc_arm1 = trigger;
        return true;
    }
    if (entry.action == SERVO_ACTION_ARM2_TOGGLE) {
        if (!triggerSlotIsFree(working->rc_arm2)) {
            snprintf(error, errorSize, "conflict: arm2_toggle mapped more than once");
            return false;
        }
        working->rc_arm2 = trigger;
        return true;
    }
    if (entry.action == SERVO_ACTION_AUX1_TOGGLE) {
        if (!triggerSlotIsFree(working->rc_aux1)) {
            snprintf(error, errorSize, "conflict: aux1_toggle mapped more than once");
            return false;
        }
        working->rc_aux1 = trigger;
        return true;
    }
    if (entry.action == SERVO_ACTION_AUX2_TOGGLE) {
        if (!triggerSlotIsFree(working->rc_aux2)) {
            snprintf(error, errorSize, "conflict: aux2_toggle mapped more than once");
            return false;
        }
        working->rc_aux2 = trigger;
        return true;
    }
    if (entry.action == SERVO_ACTION_AUX3_TOGGLE) {
        if (!triggerSlotIsFree(working->rc_aux3)) {
            snprintf(error, errorSize, "conflict: aux3_toggle mapped more than once");
            return false;
        }
        working->rc_aux3 = trigger;
        return true;
    }
    if (entry.action == SYSTEM_ACTION_OP_MODE) {
        if (!triggerSlotIsFree(working->rc_opmode)) {
            snprintf(error, errorSize, "conflict: op_mode mapped more than once");
            return false;
        }
        working->rc_opmode = trigger;
        return true;
    }

    RcTriggerBinding* spillSlots[] = {&working->rc_sound, &working->rc_free0, &working->rc_free1,
                                      &working->rc_free2, &working->rc_free3};
    for (size_t i = 0; i < sizeof(spillSlots) / sizeof(spillSlots[0]); ++i) {
        if (triggerSlotIsFree(*spillSlots[i])) {
            *spillSlots[i] = trigger;
            return true;
        }
    }

    snprintf(error, errorSize, "conflict: no trigger slot available");
    return false;
}


// -----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// populateConfigJson()
//
// Pure function — no global state, no FreeRTOS. Accepts a snapshot produced by
// captureConfigSnapshot() and builds the ArduinoJson document field by field.
// Builds the JSON snapshot consumed by the web config UI and API clients.
// Returns false only if the JsonDocument overflows.
// -----------------------------------------------------------------------------
bool populateConfigJson(JsonDocument& doc, const ConfigSnapshot& snap) {
    doc.clear();

    JsonObject drive = doc["drive"].to<JsonObject>();
    drive["speedLimitMax"] = snap.speedLimitMax;
    drive["speedPresetSlow"] = snap.speedPresetSlow;
    drive["speedPresetNormal"] = snap.speedPresetNormal;
    drive["speedPresetTurbo"] = snap.speedPresetTurbo;
    drive["speedPreset"] = speedPresetIdToString(snap.speedPresetActive);
    drive["webDriveTimeoutMs"] = snap.webDriveTimeoutMs;
    drive["stationary"] = snap.stationary;

    JsonObject rc = doc["rc"].to<JsonObject>();
    rc["inputMode"] = rcModeToString(snap.rc_input_mode);
    rc["sbusTimeoutMs"] = snap.sbusTimeoutMs;

    JsonObject rcSbus = rc["sbus"].to<JsonObject>();
    rcSbus["recvCh2"] = snap.single_sbus_use_ch2;

    JsonObject components = doc["components"].to<JsonObject>();
    components["arm1"]["enabled"] = snap.enable_arm1;
    components["arm1"]["type"] = servoCompTypeToString(snap.arm1_type);
    components["arm2"]["enabled"] = snap.enable_arm2;
    components["arm2"]["type"] = servoCompTypeToString(snap.arm2_type);
    components["aux1"]["enabled"] = snap.enable_aux1;
    components["aux1"]["type"] = servoCompTypeToString(snap.aux1_type);
    components["aux2"]["enabled"] = snap.enable_aux2;
    components["aux2"]["type"] = servoCompTypeToString(snap.aux2_type);
    components["aux3"]["enabled"] = snap.enable_aux3;
    components["aux3"]["type"] = servoCompTypeToString(snap.aux3_type);
    components["dome"]["enabled"] = snap.enable_dome;
    components["rcCh1"]["enabled"] = snap.enable_rc_ch1;
    components["rcCh2"]["enabled"] = snap.enable_rc_ch2;
    components["rcCh3"]["enabled"] = snap.enable_rc_ch3;
    components["rcCh4"]["enabled"] = snap.enable_rc_ch4;
    components["rcCh5"]["enabled"] = snap.enable_rc_ch5;
    components["rcCh6"]["enabled"] = snap.enable_rc_ch6;
    components["s1Hoverboard"]["enabled"] = snap.enable_s1_hoverboard;
    components["s2Sound"]["enabled"] = snap.enable_s2_sound;
    components["s3DomeCtrl"]["enabled"] = snap.enable_s3_dome_ctrl;

    // Legacy top-level calibration fields consumed by data/servo.js
    doc["arm1OpenUs"] = snap.arm1_open_us;
    doc["arm1CloseUs"] = snap.arm1_close_us;
    doc["arm2OpenUs"] = snap.arm2_open_us;
    doc["arm2CloseUs"] = snap.arm2_close_us;
    doc["aux1OpenUs"] = snap.aux1_open_us;
    doc["aux1CloseUs"] = snap.aux1_close_us;
    doc["aux2OpenUs"] = snap.aux2_open_us;
    doc["aux2CloseUs"] = snap.aux2_close_us;
    doc["aux3OpenUs"] = snap.aux3_open_us;
    doc["aux3CloseUs"] = snap.aux3_close_us;
    doc["aux_led_pin"] = snap.aux_led_pin;
    doc["aux_led_count"] = snap.aux_led_count;

    JsonObject dome = doc["dome"].to<JsonObject>();
    dome["neutralUs"] = snap.dome_neutral_us;
    dome["minPulseUs"] = snap.dome_min_pulse_us;
    dome["maxPulseUs"] = snap.dome_max_pulse_us;
    dome["speedLimitPct"] = snap.dome_speed_limit_pct;
    dome["rndEnable"] = snap.dome_rnd_enable;
    dome["rndSpeedPct"] = snap.dome_rnd_speed_pct;
    dome["rndPauseMin"] = snap.dome_rnd_pause_min;
    dome["rndPauseMax"] = snap.dome_rnd_pause_max;
    dome["rndMoveMs"] = snap.dome_rnd_move_ms;
    dome["wifiPeerIp"] = snap.dome_wifi_peer_ip;

    JsonObject system = doc["system"].to<JsonObject>();
    system["logLevel"] = snap.logLevel;

    return !doc.overflowed();
}

void registerConfigRoutes(AsyncWebServer& server) {
    server.on("/api/rc/map", HTTP_GET, [](AsyncWebServerRequest* req) {
        ConfigSnapshot snap;
        taskENTER_CRITICAL(&robotStateMux);
        configSnapshotFromRobotState(&snap);
        taskEXIT_CRITICAL(&robotStateMux);
        JsonDocument doc;
        if (!populateRcMapJson(doc, snap)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"rc map json build failed\"}");
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

    server.on("/api/rc/map", HTTP_POST, [](AsyncWebServerRequest* req) {
        auto sendValidationError = [&](const char* message, const RcMapEntry* entry) {
            JsonDocument err;
            err["ok"] = false;
            err["error"] = message != nullptr ? message : "invalid map";
            if (entry != nullptr) {
                JsonObject at = err["entry"].to<JsonObject>();
                at["source"] = rcMapSourceToString(entry->source);
                at["channel"] = entry->channel;
                at["action"] = robotActionIdToString(entry->action);
                if (entry->payload[0] != '\0') {
                    at["payload"] = entry->payload;
                }
            }
            char payload[320] = {};
            serializeJson(err, payload, sizeof(payload));
            req->send(400, "application/json", payload);
        };

        if (!req->hasParam("plain", true)) {
            sendValidationError("map body required", nullptr);
            return;
        }

        JsonDocument body;
        const String rawBody = req->getParam("plain", true)->value();
        if (deserializeJson(body, rawBody.c_str())) {
            sendValidationError("invalid json body", nullptr);
            return;
        }

        JsonVariantConst mapVar = body["map"];
        if (!mapVar.is<JsonArrayConst>()) {
            sendValidationError("map must be array", nullptr);
            return;
        }

        RcMapEntry entries[kRcMapMaxEntries] = {};
        size_t count = 0;
        bool seenDriveSpeed = false;
        bool seenDriveSteer = false;
        bool seenDomeSpeed = false;

        JsonArrayConst map = mapVar.as<JsonArrayConst>();
        for (JsonVariantConst itemVar : map) {
            if (!itemVar.is<JsonObjectConst>()) {
                sendValidationError("map entry must be object", nullptr);
                return;
            }
            if (count >= kRcMapMaxEntries) {
                sendValidationError("conflict: map exceeds capacity", nullptr);
                return;
            }

            JsonObjectConst item = itemVar.as<JsonObjectConst>();
            const char* sourceRaw = item["source"] | "";
            const char* actionRaw = item["action"] | "";
            uint32_t channelValue = item["channel"] | 0;
            const char* payloadRaw = item["payload"] | "";

            RcMapEntry entry = {};
            if (!rcMapSourceFromString(sourceRaw, &entry.source)) {
                sendValidationError("invalid source", nullptr);
                return;
            }
            if (channelValue > 255) {
                sendValidationError("invalid channel", nullptr);
                return;
            }
            entry.channel = (uint8_t)channelValue;
            if (!rcBindingChannelIsValid(entry.source, entry.channel)) {
                sendValidationError("channel out of range", nullptr);
                return;
            }
            if (!parseRobotActionId(actionRaw, &entry.action) ||
                entry.action == ROBOT_ACTION_NONE) {
                sendValidationError("invalid action token", nullptr);
                return;
            }
            snprintf(entry.payload, sizeof(entry.payload), "%s", payloadRaw);

            if (entry.action == DOME_ACTION_SEQ && !isValidDomeSeqPayload(entry.payload)) {
                sendValidationError("invalid dome sequence payload (expected DM:NAME)", &entry);
                return;
            }

            for (size_t i = 0; i < count; ++i) {
                if (entries[i].source == entry.source && entries[i].channel == entry.channel) {
                    sendValidationError("conflict: source+channel mapped more than once", &entry);
                    return;
                }
            }

            if (entry.action == DRIVE_ACTION_SPEED) {
                if (seenDriveSpeed) {
                    sendValidationError("conflict: drive_speed mapped more than once", &entry);
                    return;
                }
                seenDriveSpeed = true;
            } else if (entry.action == DRIVE_ACTION_STEER) {
                if (seenDriveSteer) {
                    sendValidationError("conflict: drive_steer mapped more than once", &entry);
                    return;
                }
                seenDriveSteer = true;
            } else if (entry.action == DOME_ACTION_SPEED) {
                if (seenDomeSpeed) {
                    sendValidationError("conflict: dome_speed mapped more than once", &entry);
                    return;
                }
                seenDomeSpeed = true;
            }

            entries[count++] = entry;
        }

        ConfigSnapshot working;
        taskENTER_CRITICAL(&robotStateMux);
        configSnapshotFromRobotState(&working);
        taskEXIT_CRITICAL(&robotStateMux);
        ConfigSnapshot existing = working;
        clearRcMapSlots(&working);

        for (size_t i = 0; i < count; ++i) {
            char assignErr[96] = {};
            if (!assignRcMapEntryToSnapshot(entries[i], existing, &working, assignErr,
                                            sizeof(assignErr))) {
                sendValidationError(assignErr, &entries[i]);
                return;
            }
        }

        taskENTER_CRITICAL(&robotStateMux);
        configApplyToRobotState(working);
        taskEXIT_CRITICAL(&robotStateMux);

        ConfigSnapshot snap;
        taskENTER_CRITICAL(&robotStateMux);
        configSnapshotFromRobotState(&snap);
        taskEXIT_CRITICAL(&robotStateMux);

        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, false)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist config\"}");
            return;
        }

        if (!configSave(prefs, snap)) {
            prefs.end();
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist config\"}");
            return;
        }
        prefs.end();

        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        ConfigSnapshot snap;
        taskENTER_CRITICAL(&robotStateMux);
        configSnapshotFromRobotState(&snap);
        taskEXIT_CRITICAL(&robotStateMux);
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
        taskENTER_CRITICAL(&robotStateMux);
        configSnapshotFromRobotState(&working);
        taskEXIT_CRITICAL(&robotStateMux);
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
            working.rc_input_mode = mode;
            PA_LOG_INFO(TAG, "[CFG] rcInputMode updated to %s", rcModeToString(mode));
            changed = true;
        }

        if (parseBoolParam(req, "sbusRecvCh2", &boolValue)) {
            working.single_sbus_use_ch2 = boolValue;
            changed = true;
        } else if (req->hasParam("sbusRecvCh2", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"sbusRecvCh2 must be true/false or 1/0\"}");
            return;
        }

        uint8_t auxLedPin = 0;
        if (parseUint8Param(req, "aux_led_pin", AUX_LED_PIN_DISABLED, AUX_LED_PIN_MAX, &auxLedPin)) {
            working.aux_led_pin = auxLedPin;
            changed = true;
        } else if (req->hasParam("aux_led_pin", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"aux_led_pin must be 0..3\"}");
            return;
        }

        uint8_t auxLedCount = 0;
        if (parseUint8Param(req, "aux_led_count", AUX_LED_COUNT_DEFAULT, AUX_LED_COUNT_MAX,
                            &auxLedCount)) {
            working.aux_led_count = auxLedCount;
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
                    working.single_sbus_use_ch2 = rcSbus["recvCh2"].as<bool>();
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
                working.aux_led_pin = parsed;
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
                working.aux_led_count = parsed;
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
                                            working.dome_wifi_peer_ip,
                                            sizeof(working.dome_wifi_peer_ip))) {
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
            {"enableArm1", &working.enable_arm1},
            {"enableArm2", &working.enable_arm2},
            {"enableAux1", &working.enable_aux1},
            {"enableAux2", &working.enable_aux2},
            {"enableAux3", &working.enable_aux3},
            {"enableDome", &working.enable_dome},
            {"enableRcCh1", &working.enable_rc_ch1},
            {"enableRcCh2", &working.enable_rc_ch2},
            {"enableRcCh3", &working.enable_rc_ch3},
            {"enableRcCh4", &working.enable_rc_ch4},
            {"enableRcCh5", &working.enable_rc_ch5},
            {"enableRcCh6", &working.enable_rc_ch6},
            {"enableS1Hoverboard", &working.enable_s1_hoverboard},
            {"enableS2Sound", &working.enable_s2_sound},
            {"enableS3DomeCtrl", &working.enable_s3_dome_ctrl},
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
            working.dome_neutral_us = domeU16;
            changed = true;
        } else if (req->hasParam("domeNeutralUs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeNeutralUs must be 1000..2000\"}");
            return;
        }

        if (parseUint16Param(req, "domeMinPulseUs", 1000, 2000, &domeU16)) {
            working.dome_min_pulse_us = domeU16;
            changed = true;
        } else if (req->hasParam("domeMinPulseUs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeMinPulseUs must be 1000..2000\"}");
            return;
        }

        if (parseUint16Param(req, "domeMaxPulseUs", 1000, 2000, &domeU16)) {
            working.dome_max_pulse_us = domeU16;
            changed = true;
        } else if (req->hasParam("domeMaxPulseUs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeMaxPulseUs must be 1000..2000\"}");
            return;
        }

        uint8_t domePct;
        if (parseUint8Param(req, "domeSpeedLimitPct", 0, 100, &domePct)) {
            working.dome_speed_limit_pct = domePct;
            changed = true;
        } else if (req->hasParam("domeSpeedLimitPct", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeSpeedLimitPct must be 0..100\"}");
            return;
        }

        if (req->hasParam("domeWifiPeerIp", true)) {
            const char* rawPeerIp = req->getParam("domeWifiPeerIp", true)->value().c_str();
            if (!parseDomeWifiPeerIp(rawPeerIp, working.dome_wifi_peer_ip,
                                     sizeof(working.dome_wifi_peer_ip))) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"domeWifiPeerIp must be empty or a valid IPv4 address\"}");
                return;
            }
            changed = true;
        }

        bool domeRndEnableBool;
        if (parseBoolParam(req, "domeRndEnable", &domeRndEnableBool)) {
            working.dome_rnd_enable = domeRndEnableBool;
            PA_LOG_INFO(TAG, "[CFG] domeRndEnable updated to %s", domeRndEnableBool ? "true" : "false");
            changed = true;
        } else if (req->hasParam("domeRndEnable", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeRndEnable must be true/false or 1/0\"}");
            return;
        }

        uint8_t domeRndSpeedPct;
        if (parseUint8Param(req, "domeRndSpeedPct", 5, 100, &domeRndSpeedPct)) {
            working.dome_rnd_speed_pct = domeRndSpeedPct;
            PA_LOG_INFO(TAG, "[CFG] domeRndSpeedPct updated to %u", (unsigned)domeRndSpeedPct);
            changed = true;
        } else if (req->hasParam("domeRndSpeedPct", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeRndSpeedPct must be 5..100\"}");
            return;
        }

        uint8_t domeRndPauseMin;
        if (parseUint8Param(req, "domeRndPauseMin", 1, 120, &domeRndPauseMin)) {
            working.dome_rnd_pause_min = domeRndPauseMin;
            PA_LOG_INFO(TAG, "[CFG] domeRndPauseMin updated to %u", (unsigned)domeRndPauseMin);
            changed = true;
        } else if (req->hasParam("domeRndPauseMin", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeRndPauseMin must be 1..120\"}");
            return;
        }

        uint8_t domeRndPauseMax;
        if (parseUint8Param(req, "domeRndPauseMax", 1, 120, &domeRndPauseMax)) {
            working.dome_rnd_pause_max = domeRndPauseMax;
            PA_LOG_INFO(TAG, "[CFG] domeRndPauseMax updated to %u", (unsigned)domeRndPauseMax);
            changed = true;
        } else if (req->hasParam("domeRndPauseMax", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeRndPauseMax must be 1..120\"}");
            return;
        }

        uint16_t domeRndMoveMs;
        if (parseUint16Param(req, "domeRndMoveMs", 500, 10000, &domeRndMoveMs)) {
            working.dome_rnd_move_ms = domeRndMoveMs;
            PA_LOG_INFO(TAG, "[CFG] domeRndMoveMs updated to %u", (unsigned)domeRndMoveMs);
            changed = true;
        } else if (req->hasParam("domeRndMoveMs", true)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"domeRndMoveMs must be 500..10000\"}");
            return;
        }

        struct ServoCalField {
            const char* param;
            uint16_t* field;
        };

        ServoCalField servoCalFields[] = {
            {"arm1OpenUs", &working.arm1_open_us},
            {"arm1CloseUs", &working.arm1_close_us},
            {"arm2OpenUs", &working.arm2_open_us},
            {"arm2CloseUs", &working.arm2_close_us},
            {"aux1OpenUs", &working.aux1_open_us},
            {"aux1CloseUs", &working.aux1_close_us},
            {"aux2OpenUs", &working.aux2_open_us},
            {"aux2CloseUs", &working.aux2_close_us},
            {"aux3OpenUs", &working.aux3_open_us},
            {"aux3CloseUs", &working.aux3_close_us},
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
            {"arm1Type", &working.arm1_type}, {"arm2Type", &working.arm2_type},
            {"aux1Type", &working.aux1_type}, {"aux2Type", &working.aux2_type},
            {"aux3Type", &working.aux3_type},
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
        // Apply working snapshot but preserve speedPresetActive to the special activePresetAfter value
        configApplyToRobotState(working);
        robotState.cfg_speedPresetActive = activePresetAfter;
        robotState.stationary = working.stationary;  // sync both fields
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

        ConfigSnapshot snap;
        taskENTER_CRITICAL(&robotStateMux);
        configSnapshotFromRobotState(&snap);
        taskEXIT_CRITICAL(&robotStateMux);

        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, false)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist config\"}");
            return;
        }

        if (!configSave(prefs, snap)) {
            prefs.end();
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist config\"}");
            return;
        }
        prefs.end();
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
