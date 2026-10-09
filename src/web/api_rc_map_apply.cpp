// =============================================================================
// src/web/api_rc_map_apply.cpp
//
// Apply Core for POST /api/rc/map (ADR 0011). See api_rc_map_apply.h.
// =============================================================================

#include "api_rc_map_apply.h"

#include <ArduinoJson.h>
#include <string.h>

#include "droid_parts.h"      // droidPartIdIsKnown() - a puppet string's Part
#include "rc_map_rules.h"     // rcRuleMapAdd(), rcRuleDrive(), rcRuleAxisCalibration()
#include "rc_pwm_helpers.h"   // RC_PWM_VALID_MIN_US / MAX_US
#include "seq_store_index.h"  // Learned Sequence names accepted for RC binding

namespace {

// A map entry's source: any source the stored form knows
// (parseRcBindingSource(), include/rc_binding_types.h) except "none", which is
// an empty slot and never an entry.
bool rcMapSourceFromString(const char* raw, RcBindingSource* out) {
    return parseRcBindingSource(raw, out) && *out != RC_BINDING_NONE;
}

const char* const kDomeSeqPayloads[] = {
    "DM:PIES", "DM:LOW", "DM:OPENALL", "DM:FLUTTER", "DM:BLOOM",
    "DM:SCREAM", "DM:OVERLOAD", "DM:HEART", "DM:ALARM", "DM:DISCO",
    "DM:VADER", "DM:ROCKMARCH", "DM:HELLO", "DM:LEIA", "DM:CANTINA",
    "DM:RESET", "DM:RANDOM",
    nullptr
};

bool isValidDomeSeqPayload(const char* payload) {
    if (payload == nullptr || payload[0] == '\0') return false;
    for (int i = 0; kDomeSeqPayloads[i] != nullptr; ++i) {
        if (strcmp(payload, kDomeSeqPayloads[i]) == 0) return true;
    }
    // Runtime-defined sequences (learned): accept any DM:* name currently in the
    // runtime index so an RC trigger can bind to a runtime-defined sequence. If
    // the Learned Sequence is later deleted, sequenceStart() falls through to the
    // dome fallback; the binding stays valid but inert.
    if (seqStoreIndexFind(payload) != nullptr) return true;
    return false;
}

void setError(RcMapApplyResult* result, const char* message, const RcMapEntry* entry,
              ApplyRefusalReason reason, const char* field = nullptr, const char* accepts = nullptr) {
    snprintf(result->errorMessage, sizeof(result->errorMessage), "%s", message);
    applyRefusalSet(&result->refusal, reason, field, accepts);
    if (entry != nullptr) {
        result->errorEntry.present = true;
        snprintf(result->errorEntry.source, sizeof(result->errorEntry.source), "%s",
                 rcBindingSourceToString(entry->source));
        result->errorEntry.channel = entry->channel;
        snprintf(result->errorEntry.action, sizeof(result->errorEntry.action), "%s",
                 robotActionIdToString(entry->action));
        snprintf(result->errorEntry.payload, sizeof(result->errorEntry.payload), "%s", entry->payload);
    }
}

// A refusal by the RC Map's rules, with the field, reason and accepts it names.
void setRefusal(RcMapApplyResult* result, const RcRuleVerdict& verdict, const RcMapEntry* entry) {
    char accepts[APPLY_REFUSAL_ACCEPTS_MAX] = {};
    rcRuleFormatAccepts(verdict, accepts, sizeof(accepts));
    setError(result, verdict.sentence, entry, verdict.reason, verdict.field, accepts);
}

// A Reaction's threshold or quiet period as the request sent it: absent is
// kRcMapEntryKeep, and anything no Reaction takes is held at the largest value
// below it, which rcRuleMapAdd() refuses.
uint16_t reactionNumber(JsonVariantConst value) {
    if (value.isNull()) {
        return kRcMapEntryKeep;
    }
    const uint32_t number = value | 0xFFFFFFFFu;
    return number >= kRcMapEntryKeep ? (uint16_t)(kRcMapEntryKeep - 1) : (uint16_t)number;
}

// A map entry as the drive-pair rule reads a binding: only its receiver and
// RC Channel matter there.
RcBindingConfig axisOf(const RcMapEntry* entry) {
    if (entry == nullptr) {
        return disabledRcBinding();
    }
    return makeRcBindingConfig(entry->source, entry->channel, 0, 0, 0, 0, false);
}

// The entry that binds an axis, to echo beside a refusal of its calibration.
const RcMapEntry* entryFor(RobotActionId axis, const RcMapEntry* entries, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].action == axis) {
            return &entries[i];
        }
    }
    return nullptr;
}

// The drive and dome axes' calibration, as a request may set it beside the
// map (#389): {"calibration":{"drive_speed":{"min":..,"center":..,"max":..,
// "reverse":..}, ...}}. A field left out keeps what the axis already holds
// (stored or reused, assignRcMapEntryToSnapshot()). An axis the map does not
// bind cannot be calibrated. Both the PWM and the SBUS slot of an axis hold
// the same binding, so both take the calibration.
__attribute__((noinline)) bool applyAxisCalibration(JsonVariantConst calibration, ConfigSnapshot* working,
                          const RcMapEntry* entries, size_t count, RcMapApplyResult* result) {
    if (calibration.isNull()) {
        return true;
    }
    if (!calibration.is<JsonObjectConst>()) {
        setError(result, "calibration must be object", nullptr, ApplyRefusalReason::OutOfRange, "calibration");
        return false;
    }
    struct Axis {
        const char* token;
        RobotActionId action;
        RcBindingConfig* pwm;
        RcBindingConfig* sbus;
    };
    SystemConfig& sys = working->system;
    const Axis axes[] = {
        {"drive_speed", DRIVE_ACTION_SPEED, &sys.rc_pwm_drive_speed, &sys.rc_sbus_drive_speed},
        {"drive_steer", DRIVE_ACTION_STEER, &sys.rc_pwm_drive_steer, &sys.rc_sbus_drive_steer},
        {"dome_speed", DOME_ACTION_SPEED, &sys.rc_pwm_dome_speed, &sys.rc_sbus_dome_speed},
    };
    char field[APPLY_REFUSAL_FIELD_MAX] = {};
    for (JsonPairConst pair : calibration.as<JsonObjectConst>()) {
        const Axis* axis = nullptr;
        for (const Axis& candidate : axes) {
            if (strcmp(pair.key().c_str(), candidate.token) == 0) {
                axis = &candidate;
            }
        }
        if (axis == nullptr) {
            setError(result, "calibration names no drive or dome axis", nullptr, ApplyRefusalReason::OutOfRange, "calibration",
                     "drive_speed,drive_steer,dome_speed");
            return false;
        }
        if (axis->sbus->source == RC_BINDING_NONE) {
            setError(result, "calibration for an axis the map does not bind", nullptr, ApplyRefusalReason::Conflict, "calibration");
            return false;
        }
        JsonObjectConst fields = pair.value().as<JsonObjectConst>();
        if (fields.isNull()) {
            setError(result, "calibration entry must be object", nullptr, ApplyRefusalReason::OutOfRange, "calibration");
            return false;
        }
        RcBindingConfig binding = *axis->sbus;
        const bool pwm = binding.source == RC_BINDING_PWM;
        const uint32_t lo = pwm ? RC_PWM_VALID_MIN_US : 0;
        const uint32_t hi = pwm ? RC_PWM_VALID_MAX_US : 2047;
        const char* const keys[] = {"min", "center", "max"};
        uint16_t* const slots[] = {&binding.min, &binding.center, &binding.max};
        for (size_t i = 0; i < 3; ++i) {
            JsonVariantConst value = fields[keys[i]];
            if (value.isNull()) {
                continue;
            }
            const uint32_t v = value | 0xFFFFFFFFu;
            if (!value.is<uint32_t>() || v < lo || v > hi) {
                char accepts[24] = {};
                snprintf(accepts, sizeof(accepts), "%u..%u", (unsigned)lo, (unsigned)hi);
                snprintf(field, sizeof(field), "calibration.%s", keys[i]);
                setError(result, "calibration out of range", entryFor(axis->action, entries, count),
                         ApplyRefusalReason::OutOfRange, field, accepts);
                return false;
            }
            *slots[i] = (uint16_t)v;
        }
        JsonVariantConst reverse = fields["reverse"];
        if (!reverse.isNull()) {
            if (!reverse.is<bool>()) {
                setError(result, "calibration reverse must be true or false",
                         entryFor(axis->action, entries, count), ApplyRefusalReason::OutOfRange, "calibration.reverse", "true,false");
                return false;
            }
            binding.reverse = reverse.as<bool>();
        }
        const RcRuleVerdict verdict = rcRuleAxisCalibration(axis->action, binding);
        if (!verdict.ok()) {
            setRefusal(result, verdict, entryFor(axis->action, entries, count));
            return false;
        }
        *axis->pwm = binding;
        *axis->sbus = binding;
    }
    return true;
}

// Whether each axis the map binds holds a calibration the rules take. Both
// slots of an axis hold the same binding (assignRcMapEntryToSnapshot()).
bool boundAxesCalibrated(const ConfigSnapshot& working, const RcMapEntry* entries, size_t count,
                         RcMapApplyResult* result) {
    const RobotActionId actions[] = {DRIVE_ACTION_SPEED, DRIVE_ACTION_STEER, DOME_ACTION_SPEED};
    const RcBindingConfig* const axes[] = {&working.system.rc_sbus_drive_speed,
                                           &working.system.rc_sbus_drive_steer,
                                           &working.system.rc_sbus_dome_speed};
    for (size_t i = 0; i < 3; ++i) {
        if (axes[i]->source == RC_BINDING_NONE) {
            continue;
        }
        const RcRuleVerdict verdict = rcRuleAxisCalibration(actions[i], *axes[i]);
        if (!verdict.ok()) {
            setRefusal(result, verdict, entryFor(actions[i], entries, count));
            return false;
        }
    }
    return true;
}

}  // namespace

void rcMapApply(const ConfigParamSource& params, ConfigSnapshot* working, RcMapApplyResult* result) {
    *result = RcMapApplyResult{};

    const char* rawBody = configParamGet(params, "plain");
    if (rawBody == nullptr) {
        setError(result, "map body required", nullptr, ApplyRefusalReason::MissingArgument, "map");
        return;
    }

    JsonDocument body;
    if (deserializeJson(body, rawBody)) {
        setError(result, "invalid json body", nullptr, ApplyRefusalReason::MalformedArgument, "plain");
        return;
    }

    JsonVariantConst mapVar = body["map"];
    if (!mapVar.is<JsonArrayConst>()) {
        setError(result, "map must be array", nullptr, ApplyRefusalReason::OutOfRange, "map");
        return;
    }

    RcMapEntry entries[kRcMapMaxEntries] = {};
    size_t count = 0;

    JsonArrayConst map = mapVar.as<JsonArrayConst>();
    for (JsonVariantConst itemVar : map) {
        if (!itemVar.is<JsonObjectConst>()) {
            setError(result, "map entry must be object", nullptr, ApplyRefusalReason::OutOfRange, "map");
            return;
        }
        JsonObjectConst item = itemVar.as<JsonObjectConst>();
        const char* sourceRaw = item["source"] | "";
        const char* actionRaw = item["action"] | "";
        uint32_t channelValue = item["channel"] | 0;
        const char* payloadRaw = item["payload"] | "";

        RcMapEntry entry = {};
        entry.threshold = kRcMapEntryKeep;
        entry.quietS = kRcMapEntryKeep;
        if (!rcMapSourceFromString(sourceRaw, &entry.source)) {
            setError(result, "invalid source", nullptr, ApplyRefusalReason::OutOfRange, "map.source");
            return;
        }
        if (channelValue > 255) {
            setError(result, "invalid channel", nullptr, ApplyRefusalReason::OutOfRange, "map.channel");
            return;
        }
        entry.channel = (uint8_t)channelValue;
        if (!parseRobotActionId(actionRaw, &entry.action) || entry.action == ROBOT_ACTION_NONE) {
            setError(result, "invalid action token", nullptr, ApplyRefusalReason::OutOfRange, "map.action");
            return;
        }
        snprintf(entry.payload, sizeof(entry.payload), "%s", payloadRaw);
        // A Reaction's numbers, judged by rcRuleMapAdd(). A value no Reaction
        // takes (not a whole number, or past 16 bits) is held just below
        // kRcMapEntryKeep, which every rule refuses, so it cannot wrap into range.
        if (rcBindingSourceIsDroidCondition(entry.source)) {
            entry.threshold = reactionNumber(item["threshold"]);
            entry.quietS = reactionNumber(item["quietS"]);
        }

        // What must name something that exists is checked here, where the
        // live state is: the RC Map's rules hold none of it (rc_map_rules.h).
        if (entry.action == DOME_ACTION_SEQ && !isValidDomeSeqPayload(entry.payload)) {
            setError(result, "invalid dome sequence payload (expected DM:NAME)", &entry, ApplyRefusalReason::OutOfRange,
                     "map.payload");
            return;
        }
        // A puppet string names a Part by catalog id (#442, ADR 0061).
        if (entry.action == SERVO_ACTION_PUPPET_PART && !droidPartIdIsKnown(entry.payload)) {
            setError(result, "a puppet string needs a Part", &entry, ApplyRefusalReason::OutOfRange, "map.payload");
            return;
        }

        // Judged for the receiver type the droid has saved, which is the one
        // the RC page maps for (it may still run another until a restart).
        const RcRuleVerdict verdict =
            rcRuleMapAdd(entries, count, entry, working->system.rc_input_mode);
        if (!verdict.ok()) {
            setRefusal(result, verdict, verdict.aboutEntry ? &entry : nullptr);
            return;
        }

        entries[count++] = entry;
    }

    // The drive pair, judged on what the map binds each axis to.
    const RcMapEntry* speed = nullptr;
    const RcMapEntry* steer = nullptr;
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].action == DRIVE_ACTION_SPEED) speed = &entries[i];
        if (entries[i].action == DRIVE_ACTION_STEER) steer = &entries[i];
    }
    const RcRuleVerdict drive = rcRuleDrive(axisOf(speed), axisOf(steer));
    if (!drive.ok()) {
        setRefusal(result, drive, drive.axis == DRIVE_ACTION_SPEED ? speed : steer);
        return;
    }

    ConfigSnapshot existing = *working;
    clearRcMapSlots(working);

    for (size_t i = 0; i < count; ++i) {
        char assignErr[96] = {};
        ApplyRefusal slotRefusal;
        if (!assignRcMapEntryToSnapshot(entries[i], existing, working, assignErr, sizeof(assignErr),
                                        &slotRefusal)) {
            setError(result, assignErr, &entries[i], slotRefusal.reason, slotRefusal.field);
            return;
        }
    }

    if (!applyAxisCalibration(body["calibration"], working, entries, count, result)) {
        return;
    }
    // Every bound axis, its calibration kept or set: one the rules refuse would
    // be stored and never read (ADR 0070).
    if (!boundAxesCalibrated(*working, entries, count, result)) {
        return;
    }

    result->ok = true;
}
