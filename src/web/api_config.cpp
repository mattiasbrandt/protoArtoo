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
//   the config cache, and persisted via configSave().
// =============================================================================

#include "api_config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ctype.h>
#include <string.h>

#include "api_config_apply.h"
#include "api_config_snapshot.h"
#include "api_rc_map_apply.h"
#include "api_helpers.h"
#include "audio_task.h"
#include "config.h"
#include "config_store.h"
#include "logging.h"
#include "robot_state.h"
#include "seq_store_index.h"   // Learned Sequence names accepted for RC binding
#include "servo_component_helpers.h"
#include "web_server.h"

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

bool triggerTargetAllowedByRuntime(const RcTriggerBinding& binding) {
    return true;
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
        existing.system.rc_pwm_drive_speed, existing.system.rc_pwm_drive_steer, existing.system.rc_pwm_dome_speed,
        existing.system.rc_pwm_arm1,       existing.system.rc_pwm_arm2,       existing.system.rc_pwm_sound,
        existing.system.rc_sbus_drive_speed, existing.system.rc_sbus_drive_steer, existing.system.rc_sbus_dome_speed,
        existing.system.rc_sbus_arm1,      existing.system.rc_sbus_arm2,      existing.system.rc_sbus_sound,
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
        existing.system.rc_arm1, existing.system.rc_arm2, existing.system.rc_aux1, existing.system.rc_aux2, existing.system.rc_aux3,
        existing.system.rc_sound, existing.system.rc_opmode, existing.system.rc_free0, existing.system.rc_free1, existing.system.rc_free2,
        existing.system.rc_free3,
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
    if (snap.system.rc_input_mode == RC_INPUT_STANDARD_PWM) {
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
    doc["mode"] = rcModeToString(snap.system.rc_input_mode);

    JsonArray map = doc["map"].to<JsonArray>();

    RcBindingConfig driveSpeed =
        rcMapSelectBackboneForMode(snap, snap.system.rc_pwm_drive_speed, snap.system.rc_sbus_drive_speed);
    RcBindingConfig driveSteer =
        rcMapSelectBackboneForMode(snap, snap.system.rc_pwm_drive_steer, snap.system.rc_sbus_drive_steer);
    RcBindingConfig domeSpeed =
        rcMapSelectBackboneForMode(snap, snap.system.rc_pwm_dome_speed, snap.system.rc_sbus_dome_speed);

    if (rcMapBindingIsMapped(driveSpeed)) {
        rcMapAppendEntry(map, driveSpeed.source, driveSpeed.channel, DRIVE_ACTION_SPEED, nullptr);
    }
    if (rcMapBindingIsMapped(driveSteer)) {
        rcMapAppendEntry(map, driveSteer.source, driveSteer.channel, DRIVE_ACTION_STEER, nullptr);
    }
    if (rcMapBindingIsMapped(domeSpeed)) {
        rcMapAppendEntry(map, domeSpeed.source, domeSpeed.channel, DOME_ACTION_SPEED, nullptr);
    }

    const RcTriggerBinding namedSlots[] = {snap.system.rc_arm1, snap.system.rc_arm2, snap.system.rc_aux1, snap.system.rc_aux2,
                                           snap.system.rc_aux3, snap.system.rc_opmode, snap.system.rc_sound, snap.system.rc_free0,
                                           snap.system.rc_free1, snap.system.rc_free2, snap.system.rc_free3};

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

    working->system.rc_pwm_drive_speed = disabledRcBinding();
    working->system.rc_pwm_drive_steer = disabledRcBinding();
    working->system.rc_pwm_dome_speed = disabledRcBinding();
    working->system.rc_sbus_drive_speed = disabledRcBinding();
    working->system.rc_sbus_drive_steer = disabledRcBinding();
    working->system.rc_sbus_dome_speed = disabledRcBinding();

    working->system.rc_arm1 = disabledRcTriggerBinding();
    working->system.rc_arm2 = disabledRcTriggerBinding();
    working->system.rc_aux1 = disabledRcTriggerBinding();
    working->system.rc_aux2 = disabledRcTriggerBinding();
    working->system.rc_aux3 = disabledRcTriggerBinding();
    working->system.rc_opmode = disabledRcTriggerBinding();
    working->system.rc_sound = disabledRcTriggerBinding();
    working->system.rc_free0 = disabledRcTriggerBinding();
    working->system.rc_free1 = disabledRcTriggerBinding();
    working->system.rc_free2 = disabledRcTriggerBinding();
    working->system.rc_free3 = disabledRcTriggerBinding();
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
            working->system.rc_pwm_drive_speed = backbone;
            working->system.rc_sbus_drive_speed = backbone;
        } else if (entry.action == DRIVE_ACTION_STEER) {
            working->system.rc_pwm_drive_steer = backbone;
            working->system.rc_sbus_drive_steer = backbone;
        } else {
            working->system.rc_pwm_dome_speed = backbone;
            working->system.rc_sbus_dome_speed = backbone;
        }
        return true;
    }

    if (!rcMapBuildTriggerBinding(entry, existing, &trigger)) {
        snprintf(error, errorSize, "invalid trigger binding");
        return false;
    }

    if (entry.action == SERVO_ACTION_ARM1_TOGGLE) {
        if (!triggerSlotIsFree(working->system.rc_arm1)) {
            snprintf(error, errorSize, "conflict: arm1_toggle mapped more than once");
            return false;
        }
        working->system.rc_arm1 = trigger;
        return true;
    }
    if (entry.action == SERVO_ACTION_ARM2_TOGGLE) {
        if (!triggerSlotIsFree(working->system.rc_arm2)) {
            snprintf(error, errorSize, "conflict: arm2_toggle mapped more than once");
            return false;
        }
        working->system.rc_arm2 = trigger;
        return true;
    }
    if (entry.action == SERVO_ACTION_AUX1_TOGGLE) {
        if (!triggerSlotIsFree(working->system.rc_aux1)) {
            snprintf(error, errorSize, "conflict: aux1_toggle mapped more than once");
            return false;
        }
        working->system.rc_aux1 = trigger;
        return true;
    }
    if (entry.action == SERVO_ACTION_AUX2_TOGGLE) {
        if (!triggerSlotIsFree(working->system.rc_aux2)) {
            snprintf(error, errorSize, "conflict: aux2_toggle mapped more than once");
            return false;
        }
        working->system.rc_aux2 = trigger;
        return true;
    }
    if (entry.action == SERVO_ACTION_AUX3_TOGGLE) {
        if (!triggerSlotIsFree(working->system.rc_aux3)) {
            snprintf(error, errorSize, "conflict: aux3_toggle mapped more than once");
            return false;
        }
        working->system.rc_aux3 = trigger;
        return true;
    }
    if (entry.action == SYSTEM_ACTION_OP_MODE) {
        if (!triggerSlotIsFree(working->system.rc_opmode)) {
            snprintf(error, errorSize, "conflict: op_mode mapped more than once");
            return false;
        }
        working->system.rc_opmode = trigger;
        return true;
    }

    RcTriggerBinding* spillSlots[] = {&working->system.rc_sound, &working->system.rc_free0, &working->system.rc_free1,
                                      &working->system.rc_free2, &working->system.rc_free3};
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
    drive["speedLimitMax"] = snap.drive.speedLimitMax;
    drive["speedPresetSlow"] = snap.drive.speedPresetSlow;
    drive["speedPresetNormal"] = snap.drive.speedPresetNormal;
    drive["speedPresetTurbo"] = snap.drive.speedPresetTurbo;
    drive["speedPreset"] = speedPresetIdToString(snap.drive.speedPresetActive);
    drive["webDriveTimeoutMs"] = snap.drive.webDriveTimeoutMs;
    drive["stationary"] = snap.system.stationary;

    JsonObject rc = doc["rc"].to<JsonObject>();
    rc["inputMode"] = rcModeToString(snap.system.rc_input_mode);
    rc["sbusTimeoutMs"] = snap.drive.sbusTimeoutMs;

    JsonObject rcSbus = rc["sbus"].to<JsonObject>();
    rcSbus["recvCh2"] = snap.system.single_sbus_use_ch2;

    JsonObject components = doc["components"].to<JsonObject>();
    components["arm1"]["enabled"] = snap.system.enable_arm1;
    components["arm1"]["type"] = servoCompTypeToString(snap.servo.arm1_type);
    components["arm2"]["enabled"] = snap.system.enable_arm2;
    components["arm2"]["type"] = servoCompTypeToString(snap.servo.arm2_type);
    components["aux1"]["enabled"] = snap.system.enable_aux1;
    components["aux1"]["type"] = servoCompTypeToString(snap.servo.aux1_type);
    components["aux2"]["enabled"] = snap.system.enable_aux2;
    components["aux2"]["type"] = servoCompTypeToString(snap.servo.aux2_type);
    components["aux3"]["enabled"] = snap.system.enable_aux3;
    components["aux3"]["type"] = servoCompTypeToString(snap.servo.aux3_type);
    components["dome"]["enabled"] = snap.system.enable_dome;
    components["rcCh1"]["enabled"] = snap.system.enable_rc_ch1;
    components["rcCh2"]["enabled"] = snap.system.enable_rc_ch2;
    components["rcCh3"]["enabled"] = snap.system.enable_rc_ch3;
    components["rcCh4"]["enabled"] = snap.system.enable_rc_ch4;
    components["rcCh5"]["enabled"] = snap.system.enable_rc_ch5;
    components["rcCh6"]["enabled"] = snap.system.enable_rc_ch6;
    components["s1Hoverboard"]["enabled"] = snap.system.enable_s1_hoverboard;
    components["s2Sound"]["enabled"] = snap.system.enable_s2_sound;
    components["s3DomeCtrl"]["enabled"] = snap.system.enable_s3_dome_ctrl;

    // Legacy top-level calibration fields consumed by data/servo.js
    doc["arm1OpenUs"] = snap.servo.arm1_open_us;
    doc["arm1CloseUs"] = snap.servo.arm1_close_us;
    doc["arm2OpenUs"] = snap.servo.arm2_open_us;
    doc["arm2CloseUs"] = snap.servo.arm2_close_us;
    doc["aux1OpenUs"] = snap.servo.aux1_open_us;
    doc["aux1CloseUs"] = snap.servo.aux1_close_us;
    doc["aux2OpenUs"] = snap.servo.aux2_open_us;
    doc["aux2CloseUs"] = snap.servo.aux2_close_us;
    doc["aux3OpenUs"] = snap.servo.aux3_open_us;
    doc["aux3CloseUs"] = snap.servo.aux3_close_us;
    doc["aux_led_pin"] = snap.servo.aux_led_pin;
    doc["aux_led_count"] = snap.servo.aux_led_count;

    JsonObject dome = doc["dome"].to<JsonObject>();
    dome["neutralUs"] = snap.dome.dome_neutral_us;
    dome["minPulseUs"] = snap.dome.dome_min_pulse_us;
    dome["maxPulseUs"] = snap.dome.dome_max_pulse_us;
    dome["speedLimitPct"] = snap.dome.dome_speed_limit_pct;
    dome["rndEnable"] = snap.dome.dome_rnd_enable;
    dome["rndSpeedPct"] = snap.dome.dome_rnd_speed_pct;
    dome["rndPauseMin"] = snap.dome.dome_rnd_pause_min;
    dome["rndPauseMax"] = snap.dome.dome_rnd_pause_max;
    dome["rndMoveMs"] = snap.dome.dome_rnd_move_ms;
    dome["wifiPeerIp"] = snap.dome.dome_wifi_peer_ip;

    JsonObject system = doc["system"].to<JsonObject>();
    system["logLevel"] = snap.system.logLevel;

    return !doc.overflowed();
}

void registerConfigRoutes(AsyncWebServer& server) {
    server.on("/api/rc/map", HTTP_GET, [](AsyncWebServerRequest* req) {
        ConfigSnapshot snap;
        configCacheRead(&snap);
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
        ConfigParamSource params;
        params.ctx = req;
        params.get = [](void* ctx, const char* name) -> const char* {
            auto* r = static_cast<AsyncWebServerRequest*>(ctx);
            if (!r->hasParam(name, true)) {
                return nullptr;
            }
            return r->getParam(name, true)->value().c_str();
        };

        ConfigSnapshot working;
        configCacheRead(&working);

        // RcMapApplyResult is small (~150 bytes); static kept for consistency
        // with the ADR 0011 slice 1 out-parameter convention.
        static RcMapApplyResult result;
        rcMapApply(params, &working, &result);
        if (!result.ok) {
            JsonDocument err;
            err["ok"] = false;
            err["error"] = result.errorMessage;
            if (result.errorEntry.present) {
                JsonObject at = err["entry"].to<JsonObject>();
                at["source"] = result.errorEntry.source;
                at["channel"] = result.errorEntry.channel;
                at["action"] = result.errorEntry.action;
                if (result.errorEntry.payload[0] != '\0') {
                    at["payload"] = result.errorEntry.payload;
                }
            }
            char payload[320] = {};
            serializeJson(err, payload, sizeof(payload));
            req->send(400, "application/json", payload);
            return;
        }

        configCacheApply(working);

        ConfigSnapshot snap;
        configCacheRead(&snap);

        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, false)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"failed to persist config\"}");
            return;
        }

        if (!configSaveSystem(prefs, snap.system)) {
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
        configCacheRead(&snap);
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
        configCacheRead(&working);
        const bool domeEnabledBefore = working.system.enable_dome;

        ConfigParamSource params;
        params.ctx = req;
        params.get = [](void* ctx, const char* name) -> const char* {
            auto* r = static_cast<AsyncWebServerRequest*>(ctx);
            if (!r->hasParam(name, true)) {
                return nullptr;
            }
            return r->getParam(name, true)->value().c_str();
        };
        const bool stationaryProvided = configParamHas(params, "stationary");

        // ConfigApplyResult is ~2.5 KB (dominated by the applied-fields log
        // record) — static avoids a large stack frame on the AsyncTCP task,
        // matching api_seq.cpp's SeqRunEvidence precedent.
        static ConfigApplyResult result;
        configApply(params, &working, domeEnabledBefore, &result);
        if (result.error.hasError) {
            char err[224];
            snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"%s\"}", result.error.message);
            req->send(400, "application/json", err);
            return;
        }

        for (size_t i = 0; i < result.applied.count; ++i) {
            PA_LOG_INFO(TAG, "%s", result.applied.lines[i]);
        }

        bool queueDriveOn = false;
        bool wasStationary = false;

        taskENTER_CRITICAL(&robotStateMux);
        wasStationary = robotState.stationary;
        taskEXIT_CRITICAL(&robotStateMux);
        // Apply working snapshot but preserve speedPresetActive to the special activePresetAfter value
        configCacheApply(working);
        taskENTER_CRITICAL(&robotStateMux);
        robotState.stationary = working.system.stationary;  // sync both fields
        if (stationaryProvided && wasStationary && !robotState.stationary) {
            queueDriveOn = true;
        }
        taskEXIT_CRITICAL(&robotStateMux);

        if (queueDriveOn) {
            audioQueuePlaySlot(AUDIO_SLOT_SYS_DRIVE_ON, SRC_INTERNAL);
        }
        if (result.actions.playDomeOnCue) {
            audioQueuePlaySlot(AUDIO_SLOT_SYS_DOME_ON, SRC_INTERNAL);
        }

        ConfigSnapshot snap;
        configCacheRead(&snap);

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
        requestStatusBroadcastNow();
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
