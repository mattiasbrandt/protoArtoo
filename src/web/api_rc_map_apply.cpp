// =============================================================================
// src/web/api_rc_map_apply.cpp
//
// Apply Core for POST /api/rc/map (ADR 0011). See api_rc_map_apply.h.
// =============================================================================

#include "api_rc_map_apply.h"

#include <ArduinoJson.h>
#include <string.h>

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

void setError(RcMapApplyResult* result, const char* message, const RcMapEntry* entry) {
    snprintf(result->errorMessage, sizeof(result->errorMessage), "%s", message);
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

}  // namespace

void rcMapApply(const ConfigParamSource& params, ConfigSnapshot* working, RcMapApplyResult* result) {
    *result = RcMapApplyResult{};

    const char* rawBody = configParamGet(params, "plain");
    if (rawBody == nullptr) {
        setError(result, "map body required", nullptr);
        return;
    }

    JsonDocument body;
    if (deserializeJson(body, rawBody)) {
        setError(result, "invalid json body", nullptr);
        return;
    }

    JsonVariantConst mapVar = body["map"];
    if (!mapVar.is<JsonArrayConst>()) {
        setError(result, "map must be array", nullptr);
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
            setError(result, "map entry must be object", nullptr);
            return;
        }
        if (count >= kRcMapMaxEntries) {
            setError(result, "conflict: map exceeds capacity", nullptr);
            return;
        }

        JsonObjectConst item = itemVar.as<JsonObjectConst>();
        const char* sourceRaw = item["source"] | "";
        const char* actionRaw = item["action"] | "";
        uint32_t channelValue = item["channel"] | 0;
        const char* payloadRaw = item["payload"] | "";

        RcMapEntry entry = {};
        if (!rcMapSourceFromString(sourceRaw, &entry.source)) {
            setError(result, "invalid source", nullptr);
            return;
        }
        if (channelValue > 255) {
            setError(result, "invalid channel", nullptr);
            return;
        }
        entry.channel = (uint8_t)channelValue;
        if (!rcBindingChannelIsValid(entry.source, entry.channel)) {
            setError(result, "channel out of range", nullptr);
            return;
        }
        if (!parseRobotActionId(actionRaw, &entry.action) || entry.action == ROBOT_ACTION_NONE) {
            setError(result, "invalid action token", nullptr);
            return;
        }
        snprintf(entry.payload, sizeof(entry.payload), "%s", payloadRaw);

        // A Reaction (#450). What it may do is refused here by name, so the
        // builder is told which rule it broke; rcTriggerBindingIsValid() holds
        // the same rules for the stored form.
        if (rcBindingSourceIsDroidCondition(entry.source)) {
            if (robotActionIsAnalog(entry.action)) {
                setError(result, "a droid condition cannot drive an axis", &entry);
                return;
            }
            if (!robotActionValidForReaction(entry.action)) {
                setError(result, "action not allowed on a droid condition", &entry);
                return;
            }
            JsonVariantConst thresholdVar = item["threshold"];
            if (!thresholdVar.isNull()) {
                const uint16_t thresholdMax = rcReactionThresholdMax(entry.source);
                const uint32_t threshold = thresholdVar | 0xFFFFFFFFu;
                if (thresholdMax == 0 ? threshold != 0
                                      : (threshold < 1 || threshold > thresholdMax)) {
                    setError(result, "threshold out of range", &entry);
                    return;
                }
                entry.hasThreshold = true;
                entry.threshold = (uint16_t)threshold;
            }
            JsonVariantConst quietVar = item["quietS"];
            if (!quietVar.isNull()) {
                const uint32_t quietS = quietVar | 0xFFFFFFFFu;
                if (quietS > RC_REACTION_QUIET_MAX_S) {
                    setError(result, "quiet period out of range", &entry);
                    return;
                }
                entry.hasQuietS = true;
                entry.quietS = (uint16_t)quietS;
            }
        }

        if (entry.action == DOME_ACTION_SEQ && !isValidDomeSeqPayload(entry.payload)) {
            setError(result, "invalid dome sequence payload (expected DM:NAME)", &entry);
            return;
        }
        if (entry.action == DOME_ACTION_MARCDUINO_CMD && strncmp(entry.payload, ":SM", 3) == 0) {
            setError(result, ":SM is diagnostic only and cannot be saved as an RC binding", &entry);
            return;
        }

        for (size_t i = 0; i < count; ++i) {
            if (entries[i].source == entry.source && entries[i].channel == entry.channel) {
                setError(result, "conflict: source+channel mapped more than once", &entry);
                return;
            }
        }

        if (entry.action == DRIVE_ACTION_SPEED) {
            if (seenDriveSpeed) {
                setError(result, "conflict: drive_speed mapped more than once", &entry);
                return;
            }
            seenDriveSpeed = true;
        } else if (entry.action == DRIVE_ACTION_STEER) {
            if (seenDriveSteer) {
                setError(result, "conflict: drive_steer mapped more than once", &entry);
                return;
            }
            seenDriveSteer = true;
        } else if (entry.action == DOME_ACTION_SPEED) {
            if (seenDomeSpeed) {
                setError(result, "conflict: dome_speed mapped more than once", &entry);
                return;
            }
            seenDomeSpeed = true;
        }

        entries[count++] = entry;
    }

    ConfigSnapshot existing = *working;
    clearRcMapSlots(working);

    for (size_t i = 0; i < count; ++i) {
        char assignErr[96] = {};
        if (!assignRcMapEntryToSnapshot(entries[i], existing, working, assignErr, sizeof(assignErr))) {
            setError(result, assignErr, &entries[i]);
            return;
        }
    }

    result->ok = true;
}
