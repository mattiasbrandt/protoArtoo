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
//   the config cache, and persisted through the config store's saves
//   (configPersist(), configPersistSystem()).
// =============================================================================

#include "api_config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <string.h>

#include "api_config_apply.h"
#include "api_config_snapshot.h"
#include "api_json_response.h"
#include "api_rc_map_apply.h"
#include "api_status.h"  // captureServoOutputCommanded(), shared with the Console
#include "api_wifi_apply.h"
#include "board_outputs.h"  // BOARD_OUTPUTS, boardComponentLabel() - one label source
#include "board_output_enabled.h"  // BOARD_OUTPUTS indices, which configCacheOutputIsWired() takes
#include "web_param_source.h"
#include "web_request_scratch.h"
#include "drive_speed_preset.h"
#include "audio_task.h"
#include "commanded_modes.h"
#include "component_registry.h"
#include "config.h"
#include "config_store.h"
#include "config_cache.h"
#include "config_settings.h"  // every Setting's GET path, and its value
#include "config_write_lock.h"  // this file implements the config and RC Map Write Windows
#include "console_config_fields.h"  // kComponentToggleFields - the boot mask's bit order
#include "logging.h"
#include "robot_state.h"
#include "seq_store_index.h"   // Learned Sequence names accepted for RC binding
#include "servo_component_helpers.h"
#include "web_server.h"

static const char* TAG = "WebServer";

namespace {

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
    doc["mode"] = rcInputModeToString(snap.system.rc_input_mode);

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

// The slot a dotted GET path names, made on the way: every key but the last
// is an object. Keys are copied - the pool keeps one copy of each - because a
// key cut out of a longer literal cannot be linked by length.
JsonVariant getShapeSlot(JsonObject root, const char* dotted) {
    JsonObject parent = root;
    const char* cursor = dotted;
    for (;;) {
        const char* dot = strchr(cursor, '.');
        const size_t span = dot != nullptr ? (size_t)(dot - cursor) : strlen(cursor);
        char key[24] = {};
        memcpy(key, cursor, span < sizeof(key) ? span : sizeof(key) - 1);
        if (dot == nullptr) {
            return parent[key].to<JsonVariant>();  // made now, while `key` lives
        }
        JsonObject next = parent[key].as<JsonObject>();
        parent = next.isNull() ? parent[key].to<JsonObject>() : next;
        cursor = dot + 1;
    }
}

// Each Component Toggle's Board Component Label, beside its `enabled`. A label
// is a reading, not a Setting, so it is named here rather than declared.
struct ComponentLabel {
    const char* key;        // under "components"
    const char* component;  // its key in include/component_labels.inc
};

constexpr ComponentLabel kComponentLabels[] = {
    {"domeEsc", "enable_dome_esc"}, {"rcCh1", "enable_rc_ch1"}, {"rcCh2", "enable_rc_ch2"},
    {"rcCh3", "enable_rc_ch3"},     {"rcCh4", "enable_rc_ch4"}, {"rcCh5", "enable_rc_ch5"},
    {"rcCh6", "enable_rc_ch6"},     {"drive", "enable_drive"},  {"audio", "enable_audio"},
    {"protoR2link", "enable_protor2link"},
};

//-----------------------------------------------------------------------------
// populateConfigJson()
//
// Pure function - no global state, no FreeRTOS. Accepts a snapshot produced by
// captureConfigSnapshot() and builds the ArduinoJson document. Every droid
// Setting is written at its GET path by its declaration
// (include/config_settings.h), which is also where POST reads it back, so the
// two cannot drift (ADR 0068). An Output's wired tick has no path here: it is
// read whole from its row on GET /api/servo/outputs and written back the same
// way. Returns false only if the JsonDocument overflows.
// -----------------------------------------------------------------------------
bool populateConfigJson(JsonDocument& doc, const ConfigSnapshot& snap) {
    doc.clear();
    JsonObject root = doc.to<JsonObject>();

    for (size_t i = 0; i < configSettingCount(); ++i) {
        const ConfigSetting& setting = configSettingAt(i);
        if (setting.path == nullptr) {
            continue;
        }
        char text[24] = {};
        switch (setting.rule) {
            case SettingRule::Bool:
                getShapeSlot(root, setting.path).set(configSettingNumber(setting, snap) != 0);
                break;
            case SettingRule::Range:
                getShapeSlot(root, setting.path).set(configSettingNumber(setting, snap));
                break;
            case SettingRule::Member:
                // The Component Member, as its Component Registry id, so a
                // picker never carries its own copy of the numbering (ADR
                // 0042). Absent when the stored value names nothing this image
                // knows, which is the one case where an id would have to be
                // invented. This is the SAVED choice: what the droid is
                // actually playing through until it reboots is
                // `activeMember`, added in sendConfigSnapshot().
                configSettingFormat(setting, snap, text, sizeof(text));
                if (text[0] != '\0') {
                    getShapeSlot(root, setting.path).set(text);  // char[]: copied
                }
                break;
            case SettingRule::Words:
            case SettingRule::Ipv4:
            default:
                configSettingFormat(setting, snap, text, sizeof(text));
                getShapeSlot(root, setting.path).set(text);  // char[]: copied
                break;
        }
    }

    // Readings beside the Settings: which preset is active, and each Component
    // Toggle's label on the running board.
    root["drive"]["speedPreset"] = speedPresetIdToString(snap.drive.speedPresetActive);
    JsonObject components = root["components"];
    for (const ComponentLabel& entry : kComponentLabels) {
        if (const char* label = getComponentLabel(entry.component)) {
            components[entry.key]["label"] = label;
        }
    }

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
// Out here rather than in populateConfigJson(): the boot-latched value is
// runtime state a pure snapshot serializer cannot see.
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
// on at start, not as a flag on every entry: one list of the ones that are on
// is about half the bytes of fifteen "activeEnabled" fields, on a payload every
// page load reads.
//
// The id is the payload's own component key: the param name without its
// "enable" and with the first letter lowered (enableDomeEsc -> domeEsc), so the
// list names exactly the entries under "components" and nothing keeps a second
// spelling of them. An Output's tick is left out for the same reason: an Output
// is not under "components" but on its row (ADR 0068), and a page reads what
// it was first reported with from there.
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
        if (boardOutputById(id) != nullptr) {
            continue;
        }
        toggles.add(id);
    }

    RcInputActiveConfig activeRc = {};
    configCacheReadActiveRcInput(&activeRc);
    doc["rc"]["activeInputMode"] = rcInputModeToString(static_cast<RcInputMode>(activeRc.mode));
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
    guidedSetup["summaryDone"] = guided.summaryDone;

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

// The recorded widths this write stored at a different number than it was
// sent, as rows: keyed by Output Address, each under the row key the request
// names it by and holding the number the row now has (#417, ADR 0068). The
// component band's clamp is deliberate (#286 decision 5): an MG996R cannot take
// 2200 us however it arrives, the band can narrow after the ends were recorded,
// and a type-only edit pulls the ends it did not name into the new band.
// Refusing would break a type change and throw a restored calibration away, so
// the write stands; this is what makes it not silent. Only on a write that
// clamped something, and only the write route passes a commit here, so a read
// never carries it.
void addClampedEndpointFields(JsonDocument& doc, const ConfigCommitOutcome& commit) {
    const uint32_t moved = commit.openClampedRows | commit.closeClampedRows | commit.centreClampedRows;
    if (moved == 0) {
        return;
    }
    JsonObject clamped = doc["clamped"].to<JsonObject>();
    const uint8_t count = configCacheServoOutputCount();
    for (uint8_t i = 0; i < count; ++i) {
        const uint32_t bit = (uint32_t)1u << i;
        ServoOutputRow row = {};
        if ((moved & bit) == 0 || !configCacheReadServoOutput(i, &row)) {
            continue;
        }
        // A mutable buffer, so ArduinoJson copies the key rather than keeping a
        // pointer into a frame that is gone by the time the answer serializes.
        char address[SERVO_OUTPUT_ADDRESS_STR_MAX + 1] = {};
        if (!servoOutputFormatAddress(address, sizeof(address), row.driver, row.channel)) {
            continue;
        }
        JsonObject at = clamped[address].to<JsonObject>();
        if ((commit.openClampedRows & bit) != 0) {
            at["openUs"] = row.open_us;
        }
        if ((commit.centreClampedRows & bit) != 0) {
            at["centreUs"] = row.centre_us;
        }
        if ((commit.closeClampedRows & bit) != 0) {
            at["closeUs"] = row.close_us;
        }
    }
}

// The config snapshot response, shared by the read route and the write route's
// echo. Both must return the same shape for the same device state, so they
// build it the same way rather than twice.
//
// pendingApply, networkRecovery and the rest are added on top of
// populateConfigJson(): they are runtime state (is a Staged Network Switch
// outstanding, was Network Recovery Mode the posture actually entered at boot,
// what did the droid start with) that a pure snapshot serializer cannot see.
void sendConfigSnapshot(WebRequest& req, const ConfigSnapshot& snap,
                        const ConfigCommitOutcome* commit = nullptr) {
    JsonDocument doc;
    if (!populateConfigJson(doc, snap)) {
        webSendJsonError(req, 500, "config json build failed");
        return;
    }
    if (commit != nullptr) {
        addClampedEndpointFields(doc, *commit);
    }
    addAudioMemberFields(doc);
    addActiveFields(doc);
    addDroidBuildFields(doc);
    addGuidedSetupFields(doc);
    WifiConfig activeWifi = {};
    configCacheReadActiveWifi(&activeWifi);
    doc["wifi"]["pendingApply"] = wifiConfigsDiffer(snap.wifi, activeWifi);
    doc["wifi"]["networkRecovery"] = configCacheReadActiveWifiRecovery();

    // Serialized into a buffer allocated at the measured size and freed before
    // this returns (include/api_json_response.h), not into a fixed static one.
    // The payload measures ~1.3 KB on a provisioned device, but its reachable
    // worst case - every Part fitted, every string at its longest, the guided
    // run's record full - outgrew the 3,072 B static buffer this route used to
    // own, and a config read that 500s is a Configuration, Setup and Backup that
    // will not load. A static buffer sized to that worst case spends permanent
    // BSS, the scarcest budget on this target, on a case almost no droid is in
    // (operator decision, 2026-09-19 on #371). Too big for the stack either way:
    // the psychic server task has 8 KB, beside ArduinoJson's serializer frames.
    //
    // kConfigResponseCeiling is a sanity ceiling, not a size: the measured worst
    // case (test_api_config_get) is about 3.4 KB, and a payload at or past the
    // ceiling is a 500 rather than a runaway allocation.
    static constexpr size_t kConfigResponseCeiling = 6144;
    webSendJsonDocument(req, doc, kConfigResponseCeiling, TAG);
}

// Write Window for POST /api/rc/map (ADR 0011, amended 2026-09-24). The route
// read-modify-writes the same config cache and the same NVS namespace the
// config write does, so it is guarded the same way. False -> busy, nothing
// read or written. True -> `*result` holds rcMapApply()'s answer, and when it
// is ok the map is in the cache and `*persisted` says whether NVS took it.
// One adapter today, so it stays in this file.
bool rcMapWriteWindow(const ConfigParamSource& params, ConfigSnapshot* working,
                      RcMapApplyResult* result, bool* persisted) {
    ConfigWriteLock lock;
    if (!lock.acquired()) {
        return false;
    }
    configCacheRead(working);
    rcMapApply(params, working, result);
    if (result->ok) {
        configCacheApply(*working);
        // Re-read what the cache actually holds, then persist from that - one
        // snapshot on the caller's stack, not two. WebRequest-free, as ADR
        // 0036's Consequences asked of the persistSystemConfig(WebRequest&,
        // ...) this once was: the caller renders its own failure.
        configCacheRead(working);
        *persisted = configPersistSystem(working->system);
    }
    return true;
}

}  // namespace

// See include/api_config.h for the full contract.
ConfigCommitOutcome configCommitApplied(ConfigSnapshot* working, const ConfigApplyResult& result,
                                         CommandSource source) {
    ConfigCommitOutcome outcome;

    // A Part move goes first, and decides whether anything happens at all.
    // Whether the Part is where the request says can only be answered against
    // the live table, and a request that would take a Part off an Output its
    // sender never read it on must change nothing - not the move, and not the
    // fields riding beside it (#347). configWriteWindow() holds the config write
    // lock across this call, so no other writer can move the Part between this
    // answer and the write.
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
    if (result.applied.dropped > 0) {
        PA_LOG_INFO(TAG, "[CFG] and %u more field(s) updated", (unsigned)result.applied.dropped);
    }

    // Not configCacheApply(), which keeps both: this request can state the
    // speed group and stationary, and a stated one must land. They are also
    // written at runtime by RC input on Core 1, which cannot take the config
    // write lock this commit holds, so `working` may carry a value from before
    // one landed. Whichever of them the request did not state keeps its live
    // value (#417).
    configCacheApplyKeepingLive(*working, result.speedLimitStated, result.stationaryStated);

    // An Output's settings arrive as rows (ADR 0068) and as the capture and
    // reverse acts, and the Apply Core that validated them is pure, so this is
    // where they reach the addressed rows the firmware reads (#286, ADR 0041).
    // A pulse width the component band moved is said out loud rather than
    // quietly applied -- an MG996R output cannot take 500 us, and a builder
    // who sent it is owed the reason.
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
    outcome.openClampedRows = servoOutputRepair.openMovedRows;
    outcome.closeClampedRows = servoOutputRepair.closeMovedRows;
    outcome.centreClampedRows = servoOutputRepair.centreMovedRows;

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
    if (result.guidedSetup.runChanged || result.guidedSetup.visitedChanged ||
        result.guidedSetup.summaryDoneChanged) {
        GuidedSetupConfig guided = {};
        configCacheReadGuidedSetup(&guided);
        if (result.guidedSetup.runChanged) {
            guided.run = result.guidedSetup.run;
        }
        if (result.guidedSetup.summaryDoneChanged) {
            guided.summaryDone = result.guidedSetup.summaryDone;
        }
        if (result.guidedSetup.visitedChanged) {
            guided.recorded = true;
            memcpy(guided.visited, result.guidedSetup.visited.visited, sizeof(guided.visited));
        }
        configCacheApplyGuidedSetup(guided);
    }

    // Sync stationary mode with edge detection and drive-on cue - only when the
    // request stated it. When it did not, `working` holds the value read at the
    // start of the request, and an RC toggle since (commandedSetStationary() on
    // Core 1, which keeps robotState and the cache in lockstep) would be undone
    // here and its cue replayed; the apply above has kept the live value (#417).
    if (result.stationaryStated) {
        commandedSetStationary(working->system.stationary, source);
    }

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

    // What this request changed is the Commit Step's to say; the order it
    // lands in is the store's (include/config_store.h, "Store-opened saves").
    //
    // The Droid Build and guided Setup's record only where the request said
    // something about them: an absent Fitted Parts record is what tells the
    // next boot that nobody has answered yet, and an absent visited record
    // that guided Setup has never been drawn on this controller. Writing
    // either on every config POST would spend that distinction on a request
    // that was about the log level.
    ConfigSaveExtras extras;
    extras.droidBuild = result.droidBuild.domeChanged || result.droidBuild.bodyChanged ||
                        result.droidBuild.fittedChanged;
    extras.guidedSetup = result.guidedSetup.runChanged || result.guidedSetup.visitedChanged ||
                         result.guidedSetup.summaryDoneChanged;
    if (!configPersist(*working, extras)) {
        outcome.persisted = false;
        return outcome;
    }

    requestStatusBroadcastNow();
    outcome.persisted = true;
    return outcome;
}

// See include/api_config.h for the full contract.
ConfigWriteWindowAnswer configWriteWindow(const ConfigParamSource& params, ConfigSnapshot* working,
                                          ConfigApplyResult* result, CommandSource source,
                                          ConfigCommitOutcome* commit, ApplyRefusal* refused) {
    ConfigWriteLock lock;
    if (!lock.acquired()) {
        return ConfigWriteWindowAnswer::Busy;
    }
    configCacheRead(working);
    const bool domeEnabledBefore = working->system.enable_dome_esc;
    configApply(params, working, domeEnabledBefore, result);
    if (result->error.hasError) {
        if (refused != nullptr) {
            *refused = result->error.refusal;
        }
        return ConfigWriteWindowAnswer::Refused;
    }
    *commit = configCommitApplied(working, *result, source);
    return ConfigWriteWindowAnswer::Committed;
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

    // Bounded like the config snapshot above, and for the same reasons, at
    // RC_MAP_JSON_BODY_BYTES (include/web_request_scratch.h).
    WebRequestScratch<WebScratchText<RC_MAP_JSON_BODY_BYTES>> scratch;
    if (!scratch) {
        webSendJsonError(req, 500, "request scratch unavailable");
        return;
    }
    char* body = scratch->text;
    const size_t bodySize = sizeof(scratch->text);
    if (measureJson(doc) >= bodySize) {
        webSendJsonError(req, 500, "rc map response overflow");
        return;
    }
    serializeJson(doc, body, bodySize);
    req.send(200, "application/json", body);
}

// POST /api/rc/map - replace the RC binding map.
void handleRcMapPost(WebRequest& req) {
    ConfigParamSource params = webParamSource(req);

    ConfigSnapshot working;

    // RcMapApplyResult is small (163 B on artoo-esp32); it shares the web
    // request scratch rather than holding a static of its own (#428).
    WebRequestScratch<RcMapApplyResult> scratch;
    if (!scratch) {
        webSendJsonError(req, 500, "request scratch unavailable");
        return;
    }
    RcMapApplyResult& result = *scratch;

    // Answers are rendered after the Write Window returns: nothing below
    // touches config state.
    bool persisted = false;
    const bool busy = !rcMapWriteWindow(params, &working, &result, &persisted);
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
    // A body past the route's bound is not buffered, and without this it would
    // read as a request that sent nothing: say what happened instead.
    if (req.contentLength() > kConfigPostMaxBodyBytes) {
        webSendJsonError(req, 413, "payload too large");
        return;
    }
    ConfigParamSource params = webParamSource(req);

    ConfigSnapshot working;

    // ConfigApplyResult is 2,060 B on artoo-esp32 (dominated by the
    // applied-fields log record) - too large for the server task's stack, so
    // it lives in the web request scratch (include/web_request_scratch.h), as
    // api_seq.cpp's SeqRunEvidence does. The Write Window's lock is about the
    // shared config cache and NVS, not this buffer.
    WebRequestScratch<ConfigApplyResult> scratch;
    if (!scratch) {
        webSendJsonError(req, 500, "request scratch unavailable");
        return;
    }
    ConfigApplyResult& result = *scratch;

    // configWriteWindow() leaves the post-commit snapshot in `working`.
    ConfigCommitOutcome commit = {};
    const ConfigWriteWindowAnswer answer =
        configWriteWindow(params, &working, &result, SRC_WEB_API, &commit);
    if (answer == ConfigWriteWindowAnswer::Busy) {
        webSendJsonError(req, 503, "config write busy");
        return;
    }
    if (answer == ConfigWriteWindowAnswer::Refused) {
        webSendApplyRefusal(req, 400, result.error.message, result.error.refusal);
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

    sendConfigSnapshot(req, working, &commit);
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
// response already runs to about 3.4 KB at its worst, and a table of
// twenty-four rows beside it would double every page load's read; the Parts surface asks
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

        // `id` is the Output's stored config id where the board has one, never
        // shown; the flags say what the row can save as data, so no surface
        // works it out from a form name.
        const BoardOutput* board =
            row.driver == SERVO_DRIVER_LEDC ? boardOutputOnChannel(row.channel) : nullptr;
        if (board != nullptr) {
            output["id"] = board->id;
        }
        // An Output with a wired tick can be switched off; one with none - an
        // expander's - is always wired, and says so.
        output["switchable"] = board != nullptr;
        // Whether a Light Type may go on this wire (ADR 0067): its LED count is
        // a Setting exactly there.
        output["lightCapable"] = board != nullptr && board->lightCapable;

        // Every Setting of an Output, each by its declaration
        // (include/config_settings.h), in the shape POST /api/config takes it
        // back as a row (ADR 0068): this answer is the row door's own read.
        // The ends are directional as they are stored: `openUs` is whichever
        // end the builder recorded as open, larger or smaller than `closeUs`,
        // because a reversed linkage is open < close and there is no invert
        // flag anywhere (ADR 0041). `calibrated` says whether anybody has
        // measured this Output against its linkage - what test sweep needs and
        // what degrades overshoot (ADR 0052).
        for (size_t s = 0; s < outputRowSettingCount(); ++s) {
            const OutputRowSetting& setting = outputRowSettingAt(s);
            if (!outputRowSettingIsOn(setting, board)) {
                continue;
            }
            switch (setting.store) {
                case RowSettingStore::Wired:
                    output[setting.key] = board == nullptr ||
                                          configCacheOutputIsWired((size_t)(board - BOARD_OUTPUTS));
                    break;
                case RowSettingStore::Parts: {
                    JsonArray parts = output[setting.key].to<JsonArray>();
                    const uint8_t partCount = servoOutputPartCount(row);
                    for (uint8_t slot = 0; slot < partCount; ++slot) {
                        parts.add(servoOutputPartAt(row, slot));
                    }
                    break;
                }
                case RowSettingStore::Row:
                default:
                    if (setting.rule == SettingRule::Words) {
                        output[setting.key] = outputRowSettingWord(setting, row);
                    } else if (setting.rule == SettingRule::Bool) {
                        output[setting.key] = outputRowSettingNumber(setting, row) != 0;
                    } else {
                        output[setting.key] = outputRowSettingNumber(setting, row);
                    }
                    break;
            }
        }

        // The span both position marks are drawn across, and the span the
        // calibration dial opens at: the band this Output can be driven in, set
        // by the component fitted to it. Every commanded width is clamped into
        // it on the way to the pin (servoOutputClampPulse()), so neither mark
        // can fall off either end and the dial cannot offer a width the
        // firmware would refuse (ADR 0041, #364). `component` above is what
        // decides it, so the dial can say WHICH band it opened at and why.
        const ServoPulseBand band = servoComponentBand(row.component);
        output["bandLoUs"] = band.lo;
        output["bandHiUs"] = band.hi;
        // The pair `main` stored here, when the component band narrowed it on
        // the way onto this row and the builder has not saved this Output
        // since (#417). The operator's call: the band stays, and it narrows
        // visibly - so the Servos row can say what the builder's own numbers
        // were. null on every other Output.
        uint16_t narrowedOpenUs = 0;
        uint16_t narrowedCloseUs = 0;
        if (configCacheReadServoOutputNarrowedFrom(row.driver, row.channel, &narrowedOpenUs,
                                                   &narrowedCloseUs)) {
            JsonObject narrowedFrom = output["narrowedFrom"].to<JsonObject>();
            narrowedFrom["openUs"] = narrowedOpenUs;
            narrowedFrom["closeUs"] = narrowedCloseUs;
        } else {
            output["narrowedFrom"] = nullptr;
        }

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
    // leaves the same kind of headroom 4096 left over 3589. `narrowedFrom`
    // (#417) took the measured answer to 6800 B, and 6920 B with a pair on all
    // five rows that can carry one. The row became the one place an Output is
    // read (ADR 0068, #423): its wired tick, what it can save, its light's LED
    // count and its Motion Profile and boot behaviour took the answer to
    // 9536 B, about 9660 B with those five pairs. Raised to 12288 for that, on
    // the same reasoning as 8192: a per-request bound, not BSS.
    //
    // What that worst case is NOT is what this controller sends. Twenty-four
    // rows is the expander nobody has fitted; the five LEDC outputs answer in
    // 1948 B (1219 B before #423), and that is what the Parts page's one-second
    // bench feed carries. A fitted expander would also be the moment to ask
    // whether calibration fields belong on a feed that repeats them every
    // second - they change only when somebody edits one (#364).
    webSendJsonDocument(req, doc, 12288, TAG);
}

// POST /api/wifi - stage Device WiFi Settings (ADR 0015 Staged Network Switch).
void handleWifiPost(WebRequest& req) {
    ConfigParamSource params = webParamSource(req);

    WifiConfig working = {};

    // WifiApplyResult is small (274 B on artoo-esp32); it shares the web
    // request scratch rather than holding a static of its own (#428).
    WebRequestScratch<WifiApplyResult> scratch;
    if (!scratch) {
        webSendJsonError(req, 500, "request scratch unavailable");
        return;
    }
    WifiApplyResult& result = *scratch;

    // The Write Window shared with the Console's WiFi write (api_wifi_apply.h).
    WifiCommitOutcome commit = {};
    const bool busy = !wifiWriteWindow(params, &working, &result, &commit);
    if (busy) {
        webSendJsonError(req, 503, "config write busy");
        return;
    }
    if (!result.ok) {
        webSendApplyRefusal(req, 400, result.errorMessage, result.refusal);
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
