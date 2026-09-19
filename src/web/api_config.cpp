// =============================================================================
// src/web/api_config.cpp
//
// Config API endpoints
//   GET  /api/config  - current persisted runtime config snapshot
//   POST /api/config  - update config fields and persist to NVS
//   GET  /api/rc/map  - current RC binding map
//   POST /api/rc/map  - replace the RC binding map
//   POST /api/wifi    - stage Device WiFi Settings
//
// All written against the project-owned WebRequest seam (ADR 0021) and bound
// by the seam route table. The write paths go through the ADR 0011 apply cores
// unchanged; the only coupling this file cuts is to the request object.
//
// Notes:
// - This route is the sole web entrypoint for config writes.
// - Hardware access is not performed here; values are validated, written to
//   the config cache, and persisted via configSave().
// =============================================================================

#include "api_config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#include "api_config_apply.h"
#include "api_config_snapshot.h"
#include "api_json_response.h"
#include "api_rc_map_apply.h"
#include "api_status.h"  // captureServoOutputCommanded(), shared with the Console
#include "api_wifi_apply.h"
#include "board_outputs.h"  // BOARD_OUTPUTS, boardComponentLabel() - one label source
#include "web_param_source.h"
#include "drive_speed_preset.h"
#include "audio_task.h"
#include "commanded_modes.h"
#include "component_registry.h"
#include "config.h"
#include "config_store.h"
#include "config_cache.h"
#include "console_config_fields.h"  // kComponentToggleFields - the boot mask's bit order
#include "logging.h"
#include "robot_state.h"
#include "seq_store_index.h"   // Learned Sequence names accepted for RC binding
#include "servo_component_helpers.h"
#include "servo_legacy_field_sets.h"  // the field names /api/config still speaks
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
        case RC_INPUT_ELRS:
            return "elrs";
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

const char* wifiModeToString(WifiMode mode) {
    switch (mode) {
        case WifiMode::STANDALONE_AP:
            return "standalone_ap";
        case WifiMode::CLIENT:
        default:
            return "client";
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
        existing.system.rc_pwm_arm1,       existing.system.rc_pwm_arm2,       existing.system.rc_pwm_audio,
        existing.system.rc_sbus_drive_speed, existing.system.rc_sbus_drive_steer, existing.system.rc_sbus_dome_speed,
        existing.system.rc_sbus_arm1,      existing.system.rc_sbus_arm2,      existing.system.rc_sbus_audio,
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
        existing.system.rc_audio, existing.system.rc_opmode, existing.system.rc_free0, existing.system.rc_free1, existing.system.rc_free2,
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
                                           snap.system.rc_aux3, snap.system.rc_opmode, snap.system.rc_audio, snap.system.rc_free0,
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
    working->system.rc_audio = disabledRcTriggerBinding();
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

    RcTriggerBinding* spillSlots[] = {&working->system.rc_audio, &working->system.rc_free0, &working->system.rc_free1,
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

// A component's Board Component Label on the running board (ADR 0033), or
// nullptr where this board declares none. The lookup is boardComponentLabel()
// (include/board_outputs.h), over include/component_labels.inc - the same one
// every Output's name is read through, so a label this answer reports and the
// word POST /api/servo and the Console take for it cannot disagree.
const char* getComponentLabel(const char* componentName) {
    return boardComponentLabel(runningBoardName(), componentName);
}

// -----------------------------------------------------------------------------
// The body controller's Outputs, as GET /api/config reports them.
//
// The browser knows no Output (operator, 2026-09-19 on #411: "the outputs is
// supposed to be dynamic, thats the whole point of the wiring and mapping we
// have"). Which Outputs this board has, what the board prints beside each,
// where each is addressed, which one can carry the LED strip and which config
// fields save it all arrive in this answer, and every page draws one plate or
// row per entry, in this order, saved under the field names given here. A
// board that grows an Output grows a row, and no page changes.
//
// Those facts are BOARD_OUTPUTS (include/board_outputs.h), the table the
// Console and POST /api/servo read too. What differs between boards is what
// they print, and that stays in include/component_labels.inc. The one fact a
// config answer adds beside them is which SystemConfig field holds each
// Output's wired tick, which is this table - kept in BOARD_OUTPUTS' order and
// checked against its ids, so the two cannot pair a tick with the wrong Output.
// -----------------------------------------------------------------------------
namespace {
    struct ConfigOutputEnabled {
        const char* id;
        bool SystemConfig::*enabled;
    };

    constexpr ConfigOutputEnabled CONFIG_OUTPUT_ENABLED[] = {
        {"arm1", &SystemConfig::enable_arm1},
        {"arm2", &SystemConfig::enable_arm2},
        {"aux1", &SystemConfig::enable_aux1},
        {"aux2", &SystemConfig::enable_aux2},
        {"aux3", &SystemConfig::enable_aux3},
    };

    constexpr bool configOutputsAlign() {
        if (sizeof(CONFIG_OUTPUT_ENABLED) / sizeof(CONFIG_OUTPUT_ENABLED[0]) != BOARD_OUTPUT_COUNT) {
            return false;
        }
        for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
            if (!board_outputs_detail::equals(CONFIG_OUTPUT_ENABLED[i].id, BOARD_OUTPUTS[i].id)) {
                return false;
            }
        }
        return true;
    }
    static_assert(configOutputsAlign(),
                  "CONFIG_OUTPUT_ENABLED must list BOARD_OUTPUTS' ids, in its order");
}

// -----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// populateConfigJson()
//
// Pure function - no global state, no FreeRTOS. Accepts a snapshot produced by
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
    // The Radio Controller Component Member (ADR 0042): which radio, beside
    // how its receiver is wired (inputMode above). Registry id, as the Sound
    // member is; absent when the stored value names nothing this image knows.
    if (const ComponentPartEntry* radio = componentPartByValue(snap.system.rc_member)) {
        rc["member"] = radio->id;
    }

    JsonObject rcSbus = rc["sbus"].to<JsonObject>();
    rcSbus["recvCh2"] = snap.system.single_sbus_use_ch2;

    JsonObject components = doc["components"].to<JsonObject>();
    // The Outputs first and in table order: an entry carrying an `address` IS
    // an Output, and that order is the order every page draws them in.
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        const BoardOutput& entry = BOARD_OUTPUTS[i];
        JsonObject output = components[entry.id].to<JsonObject>();
        output["enabled"] = snap.system.*CONFIG_OUTPUT_ENABLED[i].enabled;
        if (const char* label = boardOutputLabel(entry)) output["label"] = label;
        char address[SERVO_OUTPUT_ADDRESS_STR_MAX + 1] = {};
        if (servoOutputFormatAddress(address, sizeof(address), SERVO_DRIVER_LEDC, entry.channel)) {
            output["address"] = address;
        }
        if (entry.ledStripPin != AUX_LED_PIN_DISABLED) output["ledStripPin"] = entry.ledStripPin;
        output["enabledField"] = entry.enabledField;
        output["typeField"] = entry.typeField;
    }

    components["domeEsc"]["enabled"] = snap.system.enable_dome_esc;
    if (const char* label = getComponentLabel("enable_dome_esc")) components["domeEsc"]["label"] = label;

    components["rcCh1"]["enabled"] = snap.system.enable_rc_ch1;
    if (const char* label = getComponentLabel("enable_rc_ch1")) components["rcCh1"]["label"] = label;

    components["rcCh2"]["enabled"] = snap.system.enable_rc_ch2;
    if (const char* label = getComponentLabel("enable_rc_ch2")) components["rcCh2"]["label"] = label;

    components["rcCh3"]["enabled"] = snap.system.enable_rc_ch3;
    if (const char* label = getComponentLabel("enable_rc_ch3")) components["rcCh3"]["label"] = label;

    components["rcCh4"]["enabled"] = snap.system.enable_rc_ch4;
    if (const char* label = getComponentLabel("enable_rc_ch4")) components["rcCh4"]["label"] = label;

    components["rcCh5"]["enabled"] = snap.system.enable_rc_ch5;
    if (const char* label = getComponentLabel("enable_rc_ch5")) components["rcCh5"]["label"] = label;

    components["rcCh6"]["enabled"] = snap.system.enable_rc_ch6;
    if (const char* label = getComponentLabel("enable_rc_ch6")) components["rcCh6"]["label"] = label;

    components["drive"]["enabled"] = snap.system.enable_drive;
    if (const char* label = getComponentLabel("enable_drive")) components["drive"]["label"] = label;

    components["audio"]["enabled"] = snap.system.enable_audio;
    if (const char* label = getComponentLabel("enable_audio")) components["audio"]["label"] = label;
    // The Component Member sits beside the Component Toggle and answers a
    // different question: the toggle says a sound module is fitted, the member
    // says which product it is (ADR 0042). Reported as the registry id rather
    // than the stored number, so a picker never carries its own copy of the
    // numbering. Absent when the stored value names nothing this image knows,
    // which is the one case where an id would have to be invented.
    //
    // This is the SAVED choice. What the droid is actually playing through until
    // it reboots is "activeMember", which addAudioMemberFields() adds in
    // sendConfigSnapshot() -- this builder is pure and cannot read the
    // boot-latched value.
    if (const ComponentPartEntry* member = componentPartByValue(snap.system.sound_member)) {
        components["audio"]["member"] = member->id;
    }

    components["protoR2link"]["enabled"] = snap.system.enable_protor2link;
    if (const char* label = getComponentLabel("enable_protor2link")) components["protoR2link"]["label"] = label;

    // The ten calibration fields and the five component types data/servo.js
    // reads are NOT built here. Since #345 an endpoint lives on an addressed
    // Servo Output row and nowhere else, and this builder is pure -- it cannot
    // reach the live table. addServoOutputFields() adds them in
    // sendConfigSnapshot(), the same seam "pendingApply" uses.
    doc["aux_led_pin"] = snap.servo.aux_led_pin;
    doc["aux_led_count"] = snap.servo.aux_led_count;

    JsonObject domeEsc = doc["domeEsc"].to<JsonObject>();
    domeEsc["neutralUs"] = snap.dome.dome_neutral_us;
    domeEsc["minPulseUs"] = snap.dome.dome_min_pulse_us;
    domeEsc["maxPulseUs"] = snap.dome.dome_max_pulse_us;
    domeEsc["speedLimitPct"] = snap.dome.dome_speed_limit_pct;
    domeEsc["rndEnable"] = snap.dome.dome_rnd_enable;
    domeEsc["rndSpeedPct"] = snap.dome.dome_rnd_speed_pct;
    domeEsc["rndPauseMin"] = snap.dome.dome_rnd_pause_min;
    domeEsc["rndPauseMax"] = snap.dome.dome_rnd_pause_max;
    domeEsc["rndMoveMs"] = snap.dome.dome_rnd_move_ms;

    JsonObject protoR2link = doc["protoR2link"].to<JsonObject>();
    protoR2link["wifiPeerIp"] = snap.dome.dome_wifi_peer_ip;

    JsonObject system = doc["system"].to<JsonObject>();
    system["logLevel"] = snap.system.logLevel;

    // Device WiFi Settings (ADR 0015): password-safe read shape only. The
    // "pendingApply" flag (active-vs-pending for a Staged Network Switch) and
    // "networkRecovery" flag (was Network Recovery Mode the posture actually
    // entered at boot) are runtime state, not part of this
    // pure snapshot - the caller adds them after calling populateConfigJson().
    WifiConfigView wifiView = wifiConfigToView(snap.wifi);
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["provisioned"] = wifiView.provisioned;
    wifi["mode"] = wifiModeToString(wifiView.mode);
    wifi["staSsid"] = wifiView.sta_ssid;
    wifi["staPasswordSet"] = wifiView.sta_password_set;
    wifi["apSsid"] = wifiView.ap_ssid;
    wifi["apPasswordSet"] = wifiView.ap_password_set;

    return !doc.overflowed();
}

namespace {

// The Sound family's active Component Member: the module AudioTask actually
// bound at boot, as against the saved choice populateConfigJson() reports. The
// two differ exactly while a member change is staged and the droid has not
// rebooted, which is the state an operator surface has to be able to show.
//
// Out here for the same reason addServoOutputFields() is: the boot-latched
// value is runtime state a pure snapshot serializer cannot see.
void addAudioMemberFields(JsonDocument& doc) {
    JsonObject components = doc["components"];
    if (components.isNull()) {
        return;
    }
    const ComponentPartEntry* active = componentPartByValue(configCacheReadActiveSoundMember());
    if (active != nullptr) {
        components["audio"]["activeMember"] = active->id;
    }
}

// -----------------------------------------------------------------------------
// addActiveFields()
// What the droid STARTED with, for every key that is read once at start: the
// Component Toggles (ADR 0027) and the RC Receiver mode. A surface that says a
// saved change is still waiting compares these against the saved values beside
// them - never against what it happened to read first, which a page reload
// resets to the saved value and so reports nothing waiting while the droid
// still runs the old setting (#371).
//
// Both come from the boot projections setup() already publishes
// (configCacheSetActiveComponentToggles(), configCacheSetActiveRcInput()), so
// this costs no resident byte. The toggles go out as the list of ids switched
// on at start, not as a flag on every entry: the response buffer below is a
// fixed 3072 B, and fifteen "activeEnabled" fields would not fit its worst
// case where one list of the ones that are on does.
//
// The id is the payload's own component key: the param name without its
// "enable" and with the first letter lowered (enableDomeEsc -> domeEsc,
// enableArm1 -> arm1), so the list names exactly the entries under
// "components" and nothing keeps a second spelling of them.
// -----------------------------------------------------------------------------
void addActiveFields(JsonDocument& doc) {
    static constexpr char kPrefix[] = "enable";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;

    JsonArray toggles = doc["activeToggles"].to<JsonArray>();
    for (size_t i = 0; i < kComponentToggleFieldCount; ++i) {
        if (!configCacheReadActiveComponentToggle(i)) {
            continue;
        }
        const char* param = kComponentToggleFields[i].paramKey;
        const size_t len = strlen(param);
        // A mutable array, so ArduinoJson copies it rather than keeping a
        // pointer into a buffer that is gone by the time the document
        // serializes (the rule addGuidedSetupFields() below relies on too).
        char id[24] = {};
        if (strncmp(param, kPrefix, kPrefixLen) != 0 || len <= kPrefixLen ||
            len - kPrefixLen >= sizeof(id)) {
            continue;
        }
        memcpy(id, param + kPrefixLen, len - kPrefixLen);
        id[0] = (char)tolower((unsigned char)id[0]);
        toggles.add(id);
    }

    RcInputActiveConfig activeRc = {};
    configCacheReadActiveRcInput(&activeRc);
    doc["rc"]["activeInputMode"] = rcModeToString(static_cast<RcInputMode>(activeRc.mode));
}

// -----------------------------------------------------------------------------
// addServoOutputFields()
// The five fixed field sets, answered from the rows that replaced them.
//
// data/servo.js and data/setup.js still read arm1OpenUs and its nine siblings,
// and components.arm1.type beside them; the C1 wave is what rebuilds those
// pages onto the Servo Output rows. Until then the names stay and the numbers
// come from the row addressed to each set's channel, so a surface renders what
// the droid will actually drive to (#345, ADR 0041).
//
// It sits here rather than in populateConfigJson() because the live table is
// exactly the runtime state a pure snapshot serializer cannot see -- the same
// reason "pendingApply" and "networkRecovery" are added out here.
//
// An Output Address with no live row is left out of the document rather than
// given a stand-in number: a field that is absent is one data/servo.js falls
// back on its own default for, where an invented 2000 would read as a
// calibration nobody made.
void addServoOutputFields(JsonDocument& doc) {
    JsonObject components = doc["components"];
    for (size_t i = 0; i < SERVO_LEGACY_FIELD_SET_COUNT; ++i) {
        const ServoLegacyFieldSet& set = SERVO_LEGACY_FIELD_SETS[i];
        uint16_t openUs = 0;
        uint16_t closeUs = 0;
        if (!configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, set.channel, &openUs,
                                                 &closeUs)) {
            continue;
        }
        doc[set.openField] = openUs;
        doc[set.closeField] = closeUs;

        if (!components.isNull()) {
            const ServoComponentType component =
                configCacheReadServoOutputComponent(SERVO_DRIVER_LEDC, set.channel);
            components[set.componentKey]["type"] = servoCompTypeToString(component);
        }
    }
}

// -----------------------------------------------------------------------------
// partMoveRefusal()
// What a refused Part move says, or nullptr for a move that landed or had
// nothing to do. Each sentence names the next move: the caller that meets one is
// a surface whose table has changed since it read it, or one sending an address
// it did not read.
// -----------------------------------------------------------------------------
const char* partMoveRefusal(ServoPartMoveOutcome outcome) {
    switch (outcome) {
        case SERVO_PART_MOVED:
        case SERVO_PART_ALREADY_THERE:
            return nullptr;
        case SERVO_PART_NOT_WHERE_STATED:
            return "that Part is not on the Output movePartFrom names - read the outputs again, "
                   "then move it";
        case SERVO_PART_OUTPUT_FULL:
            return "that Output already drives as many Parts as it can - move one off it first";
        case SERVO_PART_NO_SUCH_OUTPUT:
            return "no Output is addressed at movePartTo";
        case SERVO_PART_NOT_A_PART:
        default:
            return "movePart names a Part this build does not model";
    }
}

// -----------------------------------------------------------------------------
// addDroidBuildFields()
// The Droid Build: which droid a builder says they built, and which Parts are
// on it (ADR 0047).
//
// Out here with the others because it lives outside ConfigSnapshot, on its own
// NVS keys - see include/config_serializer.h - so a pure snapshot serializer
// cannot see it.
//
// The Fitted Parts go out as ids rather than as the bitmap they are held in:
// the bits are emission order, and firmware and the browser module are shipped
// by two separate steps ('make ota' and 'make uploadfs'), so a bit index is the
// one form that could mean a different Part at each end of the wire.
//
// An empty `fitted` array is a real answer - a droid with nothing fitted yet -
// and every Part the catalog declares stays nameable regardless: this block
// reports what is ON the droid, never what may be authored for it.
void addDroidBuildFields(JsonDocument& doc) {
    DroidBuildConfig build = {};
    configCacheReadDroidBuild(&build);

    JsonObject droidBuild = doc["droidBuild"].to<JsonObject>();
    droidBuild["domeDesign"] = build.dome.design;
    droidBuild["domeVariant"] = build.dome.variant;
    droidBuild["bodyDesign"] = build.body.design;
    droidBuild["bodyVariant"] = build.body.variant;

    JsonArray fitted = droidBuild["fitted"].to<JsonArray>();
    for (size_t i = droidFittedPartsNextIndex(build.fitted, 0); i < DROID_PART_COUNT;
         i = droidFittedPartsNextIndex(build.fitted, i + 1)) {
        fitted.add(droidPartIdAt(i));
    }
}

// -----------------------------------------------------------------------------
// addGuidedSetupFields()
// Guided Setup's record: where the run stands, and which of its steps the
// builder has been shown (#351).
//
// Out here with the others because it lives outside ConfigSnapshot, on its own
// NVS keys - see include/config_serializer.h - so a pure snapshot serializer
// cannot see it.
//
// `recorded` is the field that looks redundant and is not. A controller
// configured before guided Setup existed carries no record at all, and an empty
// `visited` array on its own cannot say whether that means "the run has shown
// nothing yet" or "the run has never been drawn here". Only the second of those
// may be read as "these answers were given before the record existed, and are
// real"; the browser, which is the only end that knows what the steps are, makes
// that call and needs this bit to make it.
//
// The run goes out as a token rather than its stored number for the reason the
// Fitted Parts go out as ids: firmware and the browser module ship in two
// separate steps ('make ota' and 'make uploadfs'), so a number is the one form
// that could mean a different thing at each end of the wire.
void addGuidedSetupFields(JsonDocument& doc) {
    GuidedSetupConfig guided = {};
    configCacheReadGuidedSetup(&guided);

    JsonObject guidedSetup = doc["guidedSetup"].to<JsonObject>();
    guidedSetup["run"] = guidedSetupRunId(guided.run);
    guidedSetup["recorded"] = guided.recorded;

    JsonArray visited = guidedSetup["visited"].to<JsonArray>();
    const char* cursor = guided.visited;
    while (*cursor != '\0') {
        const char* comma = strchr(cursor, ',');
        const size_t span = (comma != nullptr) ? (size_t)(comma - cursor) : strlen(cursor);
        // A mutable char array, deliberately: ArduinoJson stores a `const char*`
        // by pointer and DUPLICATES a `char*`, and this buffer is gone by the
        // time the document serializes. That is the same rule the Droid Build
        // fields above rely on when they assign a local struct's char array.
        char key[GUIDED_SETUP_STEP_KEY_MAX + 1] = {};
        if (span > 0 && span <= GUIDED_SETUP_STEP_KEY_MAX) {
            memcpy(key, cursor, span);
            visited.add(key);
        }
        if (comma == nullptr) {
            break;
        }
        cursor = comma + 1;
    }
}

// The config snapshot response, shared by the read route and the write route's
// echo. Both must return the same shape for the same device state, so they
// build it the same way rather than twice.
//
// pendingApply, networkRecovery and the servo output fields are added on top of
// populateConfigJson(): they are runtime state (is a Staged Network Switch
// outstanding, was Network Recovery Mode the posture actually entered at boot,
// what do the addressed Servo Output rows hold) that a pure snapshot serializer
// cannot see.
void sendConfigSnapshot(WebRequest& req, const ConfigSnapshot& snap) {
    JsonDocument doc;
    if (!populateConfigJson(doc, snap)) {
        webSendJsonError(req, 500, "config json build failed");
        return;
    }
    addServoOutputFields(doc);
    addAudioMemberFields(doc);
    addActiveFields(doc);
    addDroidBuildFields(doc);
    addGuidedSetupFields(doc);
    WifiConfig activeWifi = {};
    configCacheReadActiveWifi(&activeWifi);
    doc["wifi"]["pendingApply"] = wifiConfigsDiffer(snap.wifi, activeWifi);
    doc["wifi"]["networkRecovery"] = configCacheReadActiveWifiRecovery();

    // Static, not stack: the payload measures ~1.3 KB on a provisioned device,
    // and even that is more than the psychic server task's 8 KB stack should
    // carry next to ArduinoJson's serializer frames. Handlers serialize on one
    // task under both backends, so a shared buffer is race-free - the same
    // argument /api/status and /api/logs already make.
    //
    // Sized to kConfigJsonBudget, the worst-case bound test_api_config_json
    // holds populateConfigJson() to; the overflow branch below is what makes a
    // future field that breaks that bound a visible 500 rather than a silently
    // truncated config.
    //
    // Serializing into a bounded buffer instead of a response stream also
    // means no heap response object per request, which is the point of the
    // migration for a route the dashboard hits on every page load.
    static char body[3072];
    if (measureJson(doc) >= sizeof(body)) {
        webSendJsonError(req, 500, "config response overflow");
        return;
    }
    serializeJson(doc, body, sizeof(body));
    req.send(200, "application/json", body);
}

// WebRequest-free per ADR 0036's Consequences ("persistSystemConfig(WebRequest&,
// ...), which sends its own HTTP error today, is the first such extraction"):
// the caller renders its own failure, so this stays reachable from a future
// non-web caller without a request object in scope. handleRcMapPost is the
// only caller today.
bool persistSystemConfig(const SystemConfig& system) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }
    if (!configSaveSystem(prefs, system)) {
        prefs.end();
        return false;
    }
    prefs.end();
    return true;
}

}  // namespace

// =============================================================================
// The config write lock - see include/api_config.h for the contract.
// =============================================================================

// Static storage and no init call: xSemaphoreCreateMutexStatic() takes no
// heap, and a static FreeRTOS mutex may be created before the scheduler
// starts, which is where a namespace-scope initializer runs. Nothing in
// setup() has to remember to create it - which matters because the adapters
// that take it (these routes and the Console module) share no init point,
// and the one that used to own the mutex is not the seam that owns the
// serialization.
static StaticSemaphore_t s_configWriteMutexStorage;
static SemaphoreHandle_t s_configWriteMutex = xSemaphoreCreateMutexStatic(&s_configWriteMutexStorage);

// The bound a contended take waits before answering busy. One second is long
// enough to cover the other adapter's whole window including its NVS write,
// and short enough that a browser POST answers rather than hangs.
static const TickType_t kConfigWriteLockTimeoutTicks = pdMS_TO_TICKS(1000);

ConfigWriteLock::ConfigWriteLock() : held_(false) {
    if (s_configWriteMutex == nullptr) {
        // Cannot happen with static creation above; kept as the same
        // defensive single-threaded-boot fallback src/seq_store.cpp's lock()
        // takes, so a future move of the creation point cannot turn config
        // writes into a hard failure.
        held_ = true;
        return;
    }
    held_ = (xSemaphoreTake(s_configWriteMutex, kConfigWriteLockTimeoutTicks) == pdTRUE);
}

ConfigWriteLock::~ConfigWriteLock() {
    if (held_ && s_configWriteMutex != nullptr) {
        xSemaphoreGive(s_configWriteMutex);
    }
}

// See include/api_config.h for the full contract.
ConfigCommitOutcome configCommitApplied(ConfigSnapshot* working, const ConfigApplyResult& result,
                                         CommandSource source) {
    ConfigCommitOutcome outcome;

    // A Part move goes first, and decides whether anything happens at all.
    // Whether the Part is where the request says can only be answered against
    // the live table, and a request that would take a Part off an Output its
    // sender never read it on must change nothing - not the move, and not the
    // fields riding beside it (#347). Both callers hold the config write lock
    // across this call, so no other writer can move the Part between this answer
    // and the write.
    if (result.partMove.requested) {
        outcome.refusal = partMoveRefusal(configCacheMoveServoOutputPart(result.partMove.move));
        if (outcome.refusal != nullptr) {
            PA_LOG_WARN(TAG, "movePart %s refused: %s", result.partMove.move.part,
                        outcome.refusal);
            return outcome;
        }
    }

    for (size_t i = 0; i < result.applied.count; ++i) {
        PA_LOG_INFO(TAG, "%s", result.applied.lines[i]);
    }

    configCacheApply(*working);

    // The endpoints a builder just changed still arrive as arm1OpenUs and its
    // nine siblings, and the Apply Core that validated them is pure, so this is
    // where they reach the addressed rows the firmware reads (#286, ADR 0041).
    // A pulse width the component band moved is said out loud rather than
    // quietly applied -- an MG996R output cannot take the old form's legal
    // 500 us, and a builder who typed it is owed the reason.
    const ServoOutputRepairReport servoOutputRepair = configCacheApplyServoOutputEdits(
        result.servoOutputs.edits, result.servoOutputs.count);
    if (servoOutputRepair.rowsRepaired > 0) {
        // 64 B rather than the boot path's 96: this frame is on the Console
        // config-write chain include/config.h measures, and an edit can only
        // ever report the three pulse widths plus the component -- the row it
        // lands on was normalised when it was loaded, so nothing else on it can
        // newly fail. "open, centre, close, component took the safe default" is
        // 44. The note truncates safely if that ever grows.
        char note[64] = {};
        servoOutputRepairNote(servoOutputRepair.firstRowMask, true, note, sizeof(note));
        PA_LOG_WARN(TAG, "servo output %u: %s - the fitted component's range does not reach it",
                    (unsigned)servoOutputRepair.firstRow, note);
    }

    // The Droid Build the request stated, onto the live answer (ADR 0047). A
    // half the request did not name is left exactly as it stood: a builder
    // changing their Dome Design is not saying anything about their body, and
    // a merge here is what keeps that true.
    if (result.droidBuild.domeChanged || result.droidBuild.bodyChanged ||
        result.droidBuild.fittedChanged) {
        DroidBuildConfig droidBuild = {};
        configCacheReadDroidBuild(&droidBuild);
        if (result.droidBuild.domeChanged) {
            droidBuild.dome = result.droidBuild.dome;
        }
        if (result.droidBuild.bodyChanged) {
            droidBuild.body = result.droidBuild.body;
        }
        if (result.droidBuild.fittedChanged) {
            droidBuild.fitted = result.droidBuild.fitted;
        }
        configCacheApplyDroidBuild(droidBuild);
    }

    // Guided Setup's record, onto the live one (#351). Each half of it is merged
    // rather than replaced, for the reason the Droid Build's halves are: marking
    // a step visited says nothing about whether the run has ended, and ending the
    // run says nothing about which steps were shown - so a request carrying one
    // must leave the other exactly as it stood.
    if (result.guidedSetup.runChanged || result.guidedSetup.visitedChanged) {
        GuidedSetupConfig guided = {};
        configCacheReadGuidedSetup(&guided);
        if (result.guidedSetup.runChanged) {
            guided.run = result.guidedSetup.run;
        }
        if (result.guidedSetup.visitedChanged) {
            guided.recorded = true;
            memcpy(guided.visited, result.guidedSetup.visited.visited, sizeof(guided.visited));
        }
        configCacheApplyGuidedSetup(guided);
    }

    // Sync stationary mode with edge detection and drive-on cue. Safe to call
    // unconditionally: when the request omits "stationary", configApply() left
    // working->system.stationary at the cache value read before the call, which
    // always matches robotState.stationary (commandedSetStationary is the only
    // runtime writer of both, keeping them in lockstep) - so the edge-detect
    // inside it is a no-op and no cue fires.
    commandedSetStationary(working->system.stationary, source);

    if (result.actions.playDomeOnCue) {
        audioQueuePlaySlot(AUDIO_SLOT_SYS_DOME_ON, SRC_INTERNAL);
    }

    // Write the post-commit state back through `working` rather than out
    // through the outcome. This is the same read the outcome's own snapshot
    // used to take, at the same point in the sequence - after the cache apply
    // and after the stationary resync - so the bytes the caller renders are
    // unchanged; what goes away is the 944-B snapshot that used to ride home
    // inside ConfigCommitOutcome and be copied again into the caller's local.
    configCacheRead(working);

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        outcome.persisted = false;
        return outcome;
    }
    // Rows first, and the fixed field sets only once the rows are down. While
    // both forms are stored, the fixed sets are the copy of what is about to be
    // replaced, and a failed save has to stop the replace: a row write that
    // fails here leaves both stores holding the same older value, which is
    // recoverable, where the other order would leave the rows stale and winning
    // over a field set that already carried the new number (#286, ADR 0056).
    if (!configSaveServoOutputs(prefs)) {
        prefs.end();
        outcome.persisted = false;
        return outcome;
    }
    if (!configSave(prefs, *working)) {
        prefs.end();
        outcome.persisted = false;
        return outcome;
    }
    // Only where the request said something about it: an absent Fitted Parts
    // record is what tells the next boot that nobody has answered yet, and
    // writing one on every config POST would spend that distinction on a
    // request that was about the log level.
    if ((result.droidBuild.domeChanged || result.droidBuild.bodyChanged ||
         result.droidBuild.fittedChanged) &&
        !configSaveDroidBuild(prefs)) {
        prefs.end();
        outcome.persisted = false;
        return outcome;
    }
    // Only where the request said something about it, for the reason the Droid
    // Build above is written only then: an absent visited record is what tells
    // the next boot that guided Setup has never been drawn on this controller,
    // and writing one on every config POST would spend that distinction on a
    // request that was about the log level.
    if ((result.guidedSetup.runChanged || result.guidedSetup.visitedChanged) &&
        !configSaveGuidedSetup(prefs)) {
        prefs.end();
        outcome.persisted = false;
        return outcome;
    }
    prefs.end();

    requestStatusBroadcastNow();
    outcome.persisted = true;
    return outcome;
}

// GET /api/config - the config snapshot data/app.js fetches on every page load.
void handleConfigGet(WebRequest& req) {
    ConfigSnapshot snap;
    configCacheRead(&snap);
    sendConfigSnapshot(req, snap);
}

// GET /api/rc/map - the RC binding map the mapper page reads.
void handleRcMapGet(WebRequest& req) {
    ConfigSnapshot snap;
    configCacheRead(&snap);
    JsonDocument doc;
    if (!populateRcMapJson(doc, snap)) {
        webSendJsonError(req, 500, "rc map json build failed");
        return;
    }

    // Bounded like the config snapshot above, and for the same reasons. The
    // map holds at most kRcMapMaxEntries entries of source/channel/action plus
    // an optional Marcduino payload; 2 KB clears a full map with headroom.
    static char body[2048];
    if (measureJson(doc) >= sizeof(body)) {
        webSendJsonError(req, 500, "rc map response overflow");
        return;
    }
    serializeJson(doc, body, sizeof(body));
    req.send(200, "application/json", body);
}

// POST /api/rc/map - replace the RC binding map.
void handleRcMapPost(WebRequest& req) {
    ConfigParamSource params = webParamSource(req);

    ConfigSnapshot working;

    // RcMapApplyResult is small (~150 bytes); static kept for consistency
    // with the ADR 0011 apply-core out-parameter convention.
    static RcMapApplyResult result;

    // This route read-modify-writes the same config cache and the same NVS
    // namespace the config write path does, so it takes the same lock across
    // the same window. Answers are rendered after the release: nothing below
    // touches config state.
    bool busy = false;
    bool persisted = false;
    {
        ConfigWriteLock lock;
        if (!lock.acquired()) {
            busy = true;
        } else {
            configCacheRead(&working);
            rcMapApply(params, &working, &result);
            if (result.ok) {
                configCacheApply(working);
                // Re-read what the cache actually holds, then persist from
                // that - one snapshot local on this task's stack, not two.
                configCacheRead(&working);
                persisted = persistSystemConfig(working.system);
            }
        }
    }

    if (busy) {
        webSendJsonError(req, 503, "config write busy");
        return;
    }
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
        webSendJsonDocument(req, err, 320, TAG, 400);
        return;
    }
    if (!persisted) {
        webSendJsonError(req, 500, "failed to persist config");
        return;
    }

    req.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/config - the sole web entrypoint for config writes.
void handleConfigPost(WebRequest& req) {
    ConfigParamSource params = webParamSource(req);

    ConfigSnapshot working;

    // ConfigApplyResult is ~2.5 KB (dominated by the applied-fields log
    // record) - static avoids a large stack frame on the server task,
    // matching api_seq.cpp's SeqRunEvidence precedent. Only this task calls
    // this handler, so the instance needs no protection of its own; the lock
    // below is about the shared config cache and NVS, not about this buffer.
    static ConfigApplyResult result;

    // The lock spans the cache read through the commit: a writer that read
    // the cache before another writer's commit and applies afterwards is
    // exactly how the loser's fields used to be reverted before NVS.
    bool busy = false;
    ConfigCommitOutcome commit = {};
    {
        ConfigWriteLock lock;
        if (!lock.acquired()) {
            busy = true;
        } else {
            configCacheRead(&working);
            const bool domeEnabledBefore = working.system.enable_dome_esc;
            configApply(params, &working, domeEnabledBefore, &result);
            if (!result.error.hasError) {
                // configCommitApplied() leaves the post-commit snapshot in
                // `working`.
                commit = configCommitApplied(&working, result, SRC_WEB_API);
            }
        }
    }

    if (busy) {
        webSendJsonError(req, 503, "config write busy");
        return;
    }
    if (result.error.hasError) {
        webSendJsonError(req, 400, result.error.message);
        return;
    }
    if (commit.refusal != nullptr) {
        webSendJsonError(req, 409, commit.refusal);
        return;
    }
    if (!commit.persisted) {
        webSendJsonError(req, 500, "failed to persist config");
        return;
    }

    sendConfigSnapshot(req, working);
}

// GET /api/servo/outputs - every live Servo Output row, the Parts each drives,
// and where each has been told to be.
//
// Both projections of the Parts destination read this one answer, so the
// part-first table and the output-first table cannot disagree about which
// Output moves which Part (ADR 0050, #347).
//
// It is also the Parts destination's bench feed: the page reads it on a short
// cadence only while Parts is on screen, which is how a commanded position
// reaches the output-first table without riding the shared /api/events stream
// that carries the estop (#318, #362).
//
// Its own route rather than more keys on /api/config, for three reasons: that
// response is a fixed 3072 B static buffer already sized to its own worst case,
// and a table of twenty-four rows does not fit beside it; the Parts surface asks
// for this far more often than a page asks for the whole config; and the
// output-first table adds columns to every row. A per-request document spends
// no BSS, which is the scarcest budget on this target
// (include/api_json_response.h).
//
// A row is copied out one at a time. That is 70 B on the web server task's frame
// per iteration, on Core 0, which is exactly the caller configCacheReadServoOutput()
// is shaped for; the real-time path asks for values instead.
void handleServoOutputsGet(WebRequest& req) {
    JsonDocument doc;
    JsonArray outputs = doc["outputs"].to<JsonArray>();
    const uint8_t count = configCacheServoOutputCount();
    for (uint8_t i = 0; i < count; ++i) {
        ServoOutputRow row = {};
        if (!configCacheReadServoOutput(i, &row)) {
            break;
        }
        JsonObject output = outputs.add<JsonObject>();
        char address[SERVO_OUTPUT_ADDRESS_STR_MAX + 1] = {};
        servoOutputFormatAddress(address, sizeof(address), row.driver, row.channel);
        output["address"] = address;
        output["name"] = servoOutputAddressName(row.driver, row.channel);
        JsonArray parts = output["parts"].to<JsonArray>();
        const uint8_t partCount = servoOutputPartCount(row);
        for (uint8_t slot = 0; slot < partCount; ++slot) {
            parts.add(servoOutputPartAt(row, slot));
        }

        // The span both position marks are drawn across, and the span the
        // calibration dial opens at: the band this Output can be driven in, set
        // by the component fitted to it. Every commanded width is clamped into
        // it on the way to the pin (servoOutputClampPulse()), so neither mark
        // can fall off either end and the dial cannot offer a width the
        // firmware would refuse (ADR 0041, #364).
        const ServoPulseBand band = servoComponentBand(row.component);
        output["bandLoUs"] = band.lo;
        output["bandHiUs"] = band.hi;
        // What the builder said is fitted, beside the band it decides, so the
        // dial can say WHICH band it opened at and why rather than only how
        // wide it is -- "what an MG996R takes" and "nothing recorded as fitted"
        // are the same two numbers and different sentences.
        output["component"] = servoCompTypeToString(row.component);

        // The Endpoint Pair and the centre the dial captures into, directional
        // as they are stored: `openUs` is whichever end the builder recorded as
        // open, larger or smaller than `closeUs`, because a reversed linkage is
        // open < close and there is no invert flag anywhere (ADR 0041). A
        // surface wanting an ordering takes the min and max of the two.
        output["openUs"] = row.open_us;
        output["centreUs"] = row.centre_us;
        output["closeUs"] = row.close_us;
        // Whether anybody has measured this Output against its linkage. It is
        // what test sweep needs -- there is nowhere sane to sweep between until
        // ends exist -- and what degrades overshoot (ADR 0052).
        output["calibrated"] = row.calibrated;

        // Commanded, both: where ServoTask has told the Output to be now, and
        // where the move in progress ends. Nothing reads a servo back. null for
        // an Output with no pulse on it, rather than a zero that reads as a
        // position.
        ServoOutputCommandedSnapshot commanded = {};
        captureServoOutputCommanded(row.driver, row.channel, &commanded);
        if (commanded.pulsing) {
            output["commandedUs"] = commanded.nowUs;
            output["targetUs"] = commanded.targetUs;
        } else {
            output["commandedUs"] = nullptr;
            output["targetUs"] = nullptr;
        }
        // Whether the calibration dial has this Output, and why it has no pulse
        // when it has none (#364, ADR 0064). `held` says both firmware bounds
        // are armed; `limp` is only meaningful while `commandedUs` is null, and
        // it is what lets the surface say "went limp -- ten minutes is the most
        // a dial holds" rather than only that the pulse has gone.
        output["held"] = commanded.held;
        output["limp"] = servoLimpReasonToString(commanded.limp);
        // How many Find by Moving nudges have ended on this Output since boot
        // (#363). A run reads it before it asks for a nudge and knows the
        // nudge is over when it has gone up -- returned, cut short, or refused
        // -- which a "nudging" flag could not promise, since a whole nudge can
        // fall between two of the page's one-second reads. Always a number,
        // even for an Output with no pulse: a count of nothing is 0.
        output["nudgesDone"] = commanded.nudgesDone;
    }
    // A sanity ceiling, not a buffer. The largest answer the table can give -
    // twenty-four rows at their longest address holding every Part the catalog
    // declares between them - is held under it by test_api_config_get. It was
    // 2560 B over a 1621 B answer until #362 gave every row its band and its
    // commanded position, 67 B a row; 4096 over 3229 B until #363 added the
    // nudge count, 15 B a row; and 4096 over 3589 B until #364 added the seven
    // fields the calibration dial reads, 109 B a row, taking the same answer to
    // 6209 B. Raised to 8192 for that, deliberately and once: it is a bound on
    // a per-request malloc, so the spend is transient rather than BSS, and 8192
    // leaves the same kind of headroom 4096 left over 3589.
    //
    // What that worst case is NOT is what this controller sends. Twenty-four
    // rows is the expander nobody has fitted; the five LEDC outputs answer in
    // 1219 B, and that is what the Parts page's one-second bench feed carries.
    // A fitted expander would also be the moment to ask whether calibration
    // fields belong on a feed that repeats them every second - they change only
    // when somebody edits one (#364).
    webSendJsonDocument(req, doc, 8192, TAG);
}

// POST /api/wifi - stage Device WiFi Settings (ADR 0015 Staged Network Switch).
void handleWifiPost(WebRequest& req) {
    ConfigParamSource params = webParamSource(req);

    WifiConfig working = {};

    // WifiApplyResult is small; static kept for consistency with the
    // ADR 0011 apply-core out-parameter convention.
    static WifiApplyResult result;

    // wifiCommitApplied() read-modify-writes the shared config-cache snapshot
    // and then writes NVS, so an interleaved config write on any adapter
    // would lose one of the two updates. The read of the current settings is
    // inside the window too - reading them outside it would reopen exactly
    // that gap one statement earlier. The Console's own WiFi write
    // (src/console/console_module.cpp) takes the same lock.
    bool busy = false;
    WifiCommitOutcome commit = {};
    {
        ConfigWriteLock lock;
        if (!lock.acquired()) {
            busy = true;
        } else {
            configCacheReadWifi(&working);
            wifiApply(params, &working, &result);
            if (result.ok) {
                // Commit Step (ADR 0036, api_wifi_apply.h): persist to NVS,
                // stage the config cache (Staged Network Switch, ADR 0015),
                // and broadcast status - shared with the Console WiFi write
                // path instead of each adapter carrying its own copy of the
                // sequence.
                commit = wifiCommitApplied(&working);
            }
        }
    }

    if (busy) {
        webSendJsonError(req, 503, "config write busy");
        return;
    }
    if (!result.ok) {
        webSendJsonError(req, 400, result.errorMessage);
        return;
    }
    if (!commit.persisted) {
        webSendJsonError(req, 500, "failed to persist wifi settings");
        return;
    }

    JsonDocument doc;
    WifiConfigView view = wifiConfigToView(commit.config);
    doc["ok"] = true;
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["provisioned"] = view.provisioned;
    wifi["mode"] = wifiModeToString(view.mode);
    wifi["staSsid"] = view.sta_ssid;
    wifi["staPasswordSet"] = view.sta_password_set;
    wifi["apSsid"] = view.ap_ssid;
    wifi["apPasswordSet"] = view.ap_password_set;
    wifi["pendingApply"] = commit.pendingApply;
    wifi["networkRecovery"] = commit.networkRecovery;

    char payload[512];
    serializeJson(doc, payload, sizeof(payload));
    req.send(200, "application/json", payload);
}
