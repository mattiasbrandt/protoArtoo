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
#include "board_outputs.h"
#include "component_registry.h"
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

// The typed edit this request is already making to the Output on `channel`, or
// a new one for it. A per-Output setting MERGES into the edit the five field
// sets may already have made for the same Output Address rather than adding one
// beside it, which is what keeps the list inside its bound: every BOARD_OUTPUTS
// channel is one of the five those sets cover, so the typed edits never number
// more than five. nullptr only if that stopped being true, and the caller
// refuses the request rather than writing past the list.
ServoOutputEdit* typedEditFor(ConfigServoOutputEdits* edits, uint8_t channel) {
    for (size_t e = 0; e < edits->count; ++e) {
        ServoOutputEdit& candidate = edits->edits[e];
        if (candidate.driver == SERVO_DRIVER_LEDC && candidate.channel == channel &&
            candidate.kind == SERVO_EDIT_TYPED) {
            return &candidate;
        }
    }
    if (edits->count >= sizeof(edits->edits) / sizeof(edits->edits[0])) {
        return nullptr;
    }
    ServoOutputEdit* edit = &edits->edits[edits->count++];
    *edit = ServoOutputEdit{};
    edit->driver = SERVO_DRIVER_LEDC;
    edit->channel = channel;
    return edit;
}

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
    if (strcmp(raw, "elrs") == 0) {
        *out = RC_INPUT_ELRS;
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

// -----------------------------------------------------------------------------
// applyDroidBuildHalf()
// One half of a Droid Build - a design and the variant of that design - read,
// checked against the catalog vocabulary, and recorded for the Commit Step.
//
// Returns false and sets the error when the request named this half and got it
// wrong. A request that named neither field of the half leaves `*changed` false
// and is not an error: a POST that is not about the Droid Build is most of
// them.
//
// The pair moves together because a variant means nothing on its own. Sending
// one without the other would ask this function to validate half an answer
// against the other half's stored design, which is a pairing the builder never
// stated - and on a design change it is exactly the pairing that is wrong.
//
// The refusal does not echo what was asked for, for the reason the sound member
// refusal above gives: setError() takes a literal and the message lands in a
// JSON body.
bool applyDroidBuildHalf(const ConfigParamSource& params, const char* designName,
                         const char* variantName, const char* refusal,
                         DroidDesignChoice* out, bool* changed,
                         ConfigApplyResult* result) {
    const bool hasDesign = configParamHas(params, designName);
    const bool hasVariant = configParamHas(params, variantName);
    if (!hasDesign && !hasVariant) {
        return true;
    }
    if (!hasDesign || !hasVariant) {
        setError(result, refusal);
        return false;
    }
    DroidDesignChoice choice = {};
    if (!droidDesignChoiceSet(&choice, configParamGet(params, designName),
                              configParamGet(params, variantName)) ||
        !droidDesignChoiceIsKnown(choice)) {
        setError(result, refusal);
        return false;
    }
    *out = choice;
    *changed = true;
    return true;
}

// -----------------------------------------------------------------------------
// parsePartMoveEnd()
// One end of a Part move: `none`, or an Output Address a driver actually has.
// Anything else - an absent field included - is not an end.
// -----------------------------------------------------------------------------
bool parsePartMoveEnd(const char* raw, bool* onOutput, ServoOutputDriver* driver,
                      uint8_t* channel) {
    if (raw == nullptr) {
        return false;
    }
    if (strcmp(raw, "none") == 0) {
        *onOutput = false;
        return true;
    }
    *onOutput = true;
    return servoOutputParseAddress(raw, driver, channel);
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
            setError(result, "rcInputMode must be standard_pwm, single_sbus, dual_sbus, or elrs");
            return;
        }
        working->system.rc_input_mode = mode;
        appendApplied(&result->applied, "[CFG] rcInputMode updated to %s", rcModeToString(mode));
        result->changed = true;
    }

    // The Sound Component Member, named by its Component Registry id rather than
    // by the number it is stored as: a picker offering the registry's rows sends
    // back what the registry gave it, and nothing outside the registry has to
    // know the numbering. A roadmap row and a member from another family are
    // both refused, by the same rule the picker's lineup comes from.
    //
    // The refusal deliberately does NOT echo what was asked for. setError()
    // takes a literal and the message lands in a JSON error body, so echoing an
    // arbitrary request value would put unescaped operator input there; the
    // registry id space is small enough that the sentence is diagnosis enough.
    if (configParamHas(params, "soundMember")) {
        const char* memberId = configParamGet(params, "soundMember");
        const ComponentPartEntry* member = componentPartById(memberId);
        if (member == nullptr || member->category != COMPONENT_CATEGORY_SOUND ||
            !componentPartIsSelectable(*member)) {
            setError(result, "soundMember is not a sound module this firmware can drive");
            return;
        }
        working->system.sound_member = member->value;
        appendApplied(&result->applied, "[CFG] soundMember updated to %s (takes effect at reboot)",
                      member->name);
        result->changed = true;
    }

    // The Radio Controller Component Member, by the same rule as soundMember
    // above: a registry id from the radio family that the registry calls
    // selectable, never echoed back in the refusal.
    if (configParamHas(params, "rcMember")) {
        const char* memberId = configParamGet(params, "rcMember");
        const ComponentPartEntry* member = componentPartById(memberId);
        if (member == nullptr || member->category != COMPONENT_CATEGORY_RADIO_CONTROLLER ||
            !componentPartIsSelectable(*member)) {
            setError(result, "rcMember is not a radio this firmware lists");
            return;
        }
        working->system.rc_member = member->value;
        appendApplied(&result->applied, "[CFG] rcMember updated to %s", member->name);
        result->changed = true;
    }

    // The Droid Build (ADR 0047): which droid a builder says they built, and
    // which Parts are on it.
    //
    // Nothing downstream is gated on any of it. The Fitted Parts are checked
    // against the catalog vocabulary only so a Part id this build cannot name
    // never reaches storage - the same form check droidPartIdIsKnown() is, and
    // NOT a narrowing of it: a Part outside the fitted set stays authorable,
    // saveable and wirable, which is the decision this whole field exists to
    // keep (ADR 0047, #333).
    //
    // The two halves are never compared. An MK4.1 dome on an MK4 Basic body is
    // an ordinary droid, and refusing that pairing is the other way this could
    // quietly undo itself.
    if (!applyDroidBuildHalf(params, "domeDesign", "domeVariant",
                             "domeDesign and domeVariant must be sent together, and name a "
                             "design and one of its own variants",
                             &result->droidBuild.dome, &result->droidBuild.domeChanged, result)) {
        return;
    }
    if (result->droidBuild.domeChanged) {
        appendApplied(&result->applied, "[CFG] domeDesign updated to %s/%s",
                      result->droidBuild.dome.design, result->droidBuild.dome.variant);
        result->changed = true;
    }
    if (!applyDroidBuildHalf(params, "bodyDesign", "bodyVariant",
                             "bodyDesign and bodyVariant must be sent together, and name a "
                             "design and one of its own variants",
                             &result->droidBuild.body, &result->droidBuild.bodyChanged, result)) {
        return;
    }
    if (result->droidBuild.bodyChanged) {
        appendApplied(&result->applied, "[CFG] bodyDesign updated to %s/%s",
                      result->droidBuild.body.design, result->droidBuild.body.variant);
        result->changed = true;
    }

    // The Fitted Parts arrive whole, as a comma-separated Part id list. An
    // EMPTY value is a real answer - a droid with nothing fitted yet - and is
    // applied; the field being absent is what means "this request is not about
    // the Fitted Parts".
    if (configParamHas(params, "fittedParts")) {
        const char* raw = configParamGet(params, "fittedParts");
        if (droidFittedPartsParse(raw, &result->droidBuild.fitted) != 0) {
            setError(result, "fittedParts names a Part this build does not model");
            return;
        }
        result->droidBuild.fittedChanged = true;
        appendApplied(&result->applied, "[CFG] fittedParts updated to %u part(s)",
                      (unsigned)droidFittedPartsCount(result->droidBuild.fitted));
        result->changed = true;
    }

    // Guided Setup's record (#351): where the run stands, and which of its steps
    // the builder has actually been shown.
    //
    // Nothing downstream is gated on either. Firmware stores this record and
    // checks its FORM - a run state this image can name, step keys made of
    // characters a key may contain - because an arbitrary request string would
    // otherwise reach NVS and come back out in a JSON payload. Which steps EXIST
    // is the browser's question, not this one's: the run is drawn there and the
    // list grows (include/guided_setup.h).
    //
    // The refusals do not echo what was asked for, for the reason the sound
    // member refusal above gives: setError() takes a literal and the message
    // lands in a JSON error body.
    if (configParamHas(params, "guidedSetupRun")) {
        GuidedSetupRun run = GUIDED_SETUP_NOT_RUN;
        if (!guidedSetupRunFromId(configParamGet(params, "guidedSetupRun"), &run)) {
            setError(result, "guidedSetupRun must be not-run, skipped or completed");
            return;
        }
        result->guidedSetup.run = run;
        result->guidedSetup.runChanged = true;
        appendApplied(&result->applied, "[CFG] guidedSetupRun updated to %s",
                      guidedSetupRunId(run));
        result->changed = true;
    }

    // The visited list arrives whole. An EMPTY value is a real answer - the run
    // has been drawn and nothing has been shown yet - and is applied; the field
    // being absent is what means "this request is not about the visited record".
    // A key this image cannot read is refused rather than dropped: a shortened
    // record would report a step the builder WAS shown as one they never were,
    // which is the untruth the record exists to prevent.
    if (configParamHas(params, "guidedSetupVisited")) {
        guidedSetupDefaults(&result->guidedSetup.visited);
        const size_t dropped = guidedSetupVisitedSet(&result->guidedSetup.visited,
                                                     configParamGet(params, "guidedSetupVisited"));
        if (dropped > 0) {
            setError(result,
                     "guidedSetupVisited must be a comma-separated list of step keys, each at "
                     "most 12 characters of a-z, 0-9 and _");
            return;
        }
        result->guidedSetup.visited.recorded = true;
        result->guidedSetup.visitedChanged = true;
        appendApplied(&result->applied, "[CFG] guidedSetupVisited updated");
        result->changed = true;
    }

    // Whether the ended run's summary has been dismissed (#371): a third fact,
    // merged on its own for the reason the two above are.
    if (configParamHas(params, "guidedSetupSummaryDone")) {
        const char* value = configParamGet(params, "guidedSetupSummaryDone");
        if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0) {
            setError(result, "guidedSetupSummaryDone must be true or false");
            return;
        }
        result->guidedSetup.summaryDone = strcmp(value, "true") == 0;
        result->guidedSetup.summaryDoneChanged = true;
        appendApplied(&result->applied, "[CFG] guidedSetupSummaryDone updated to %s", value);
        result->changed = true;
    }

    // A Part's place on the Outputs (ADR 0050, #347). All three fields or none:
    // a move that names only where a Part is going cannot say which Output it is
    // taking the Part away from, and that half is the one a builder has to be
    // told about before it happens.
    if (configParamHas(params, "movePart") || configParamHas(params, "movePartFrom") ||
        configParamHas(params, "movePartTo")) {
        const char* part = configParamGet(params, "movePart");
        ServoOutputPartMove move = {};
        if (part == nullptr || part[0] == '\0' ||
            strlen(part) > SERVO_OUTPUT_PART_ID_MAX || !servoOutputPartIdIsValid(part) ||
            !parsePartMoveEnd(configParamGet(params, "movePartFrom"), &move.fromOutput,
                              &move.fromDriver, &move.fromChannel) ||
            !parsePartMoveEnd(configParamGet(params, "movePartTo"), &move.toOutput,
                              &move.toDriver, &move.toChannel)) {
            setError(result, "movePart, movePartFrom and movePartTo must be sent together: a "
                             "Part this build models, and each end an Output Address or none");
            return;
        }
        snprintf(move.part, sizeof(move.part), "%s", part);
        result->partMove.requested = true;
        result->partMove.move = move;
        appendApplied(&result->applied, "[CFG] movePart %s to %s", move.part,
                      configParamGet(params, "movePartTo"));
        result->changed = true;
    }

    if (paramBool(params, "sbusRecvCh2", &boolValue)) {
        working->system.single_sbus_use_ch2 = boolValue;
        result->changed = true;
    } else if (configParamHas(params, "sbusRecvCh2")) {
        setError(result, "sbusRecvCh2 must be true/false or 1/0");
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

    // The five fixed field sets, taken as edits to the rows that replaced them
    // (#345, ADR 0041). One pass per Output Address, so the three parameters
    // that name the same output arrive as one edit and the component type is
    // settled against the pair it will clamp rather than by parameter order.
    //
    // The bounds here are still 500..2500, the widest a servo takes, and they
    // are not the authoritative clamp: that is the fitted component's band, and
    // it is applied on the row, where it can report having moved a number. A
    // 500 us arriving for an MG996R is a legal request this core accepts and
    // the Commit Step answers with a warning naming the part.
    for (size_t i = 0; i < SERVO_LEGACY_FIELD_SET_COUNT; ++i) {
        const ServoLegacyFieldSet& set = SERVO_LEGACY_FIELD_SETS[i];
        ServoOutputEdit edit = {};
        edit.driver = SERVO_DRIVER_LEDC;
        edit.channel = set.channel;
        edit.fields = 0;

        struct EndpointParam {
            const char* param;
            uint16_t ServoOutputEdit::*member;
            uint16_t bit;
        };
        const EndpointParam kEndpoints[] = {
            {set.openField, &ServoOutputEdit::open_us, SERVO_FIELD_OPEN},
            {set.closeField, &ServoOutputEdit::close_us, SERVO_FIELD_CLOSE},
        };
        for (size_t e = 0; e < sizeof(kEndpoints) / sizeof(kEndpoints[0]); ++e) {
            if (!configParamHas(params, kEndpoints[e].param)) {
                continue;
            }
            uint16_t pulseUs = 0;
            if (!paramUint16(params, kEndpoints[e].param, kServoPulseMinUs, kServoPulseMaxUs,
                             &pulseUs)) {
                char err[192];
                snprintf(err, sizeof(err), "%s must be 500..2500", kEndpoints[e].param);
                setError(result, err);
                return;
            }
            edit.*(kEndpoints[e].member) = pulseUs;
            edit.fields |= kEndpoints[e].bit;
        }

        if (configParamHas(params, set.typeParam)) {
            const char* raw = configParamGet(params, set.typeParam);
            ServoComponentType parsed = SERVO_COMP_NONE;
            if (strcmp(raw, "0") == 0 || strcmp(raw, "1") == 0 || strcmp(raw, "2") == 0 ||
                strcmp(raw, "3") == 0) {
                parsed = (ServoComponentType)atoi(raw);
            } else {
                parsed = parseServoCompType(raw);
            }

            if (!isValidServoCompType((uint8_t)parsed)) {
                char err[180];
                snprintf(err, sizeof(err), "%s must be none/mg996r/mg90s/rgb", set.typeParam);
                setError(result, err);
                return;
            }

            edit.component = parsed;
            edit.fields |= SERVO_FIELD_COMPONENT;
        }

        if (edit.fields == 0) {
            continue;
        }
        result->servoOutputs.edits[result->servoOutputs.count++] = edit;
        result->changed = true;
    }

    // A light's settings, one per Output that can carry one: how many LEDs are
    // on that wire (ADR 0067, #413). The field names are BOARD_OUTPUTS'
    // `ledCountField`, NOT one of the five legacy sets above - this is a new
    // per-Output answer, and it outlives the fixed names the loop above exists
    // to keep reading.
    //
    // It MERGES into the edit that loop may already have made for the same
    // Output Address (typedEditFor()).
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        const BoardOutput& output = BOARD_OUTPUTS[i];
        if (output.ledCountField == nullptr || !configParamHas(params, output.ledCountField)) {
            continue;
        }
        uint8_t ledCount = 0;
        if (!paramUint8(params, output.ledCountField, SERVO_LIGHT_LEDS_MIN, SERVO_LIGHT_LEDS_MAX,
                        &ledCount)) {
            char err[192];
            snprintf(err, sizeof(err), "%s must be %u..%u", output.ledCountField,
                     (unsigned)SERVO_LIGHT_LEDS_MIN, (unsigned)SERVO_LIGHT_LEDS_MAX);
            setError(result, err);
            return;
        }

        ServoOutputEdit* edit = typedEditFor(&result->servoOutputs, output.channel);
        if (edit == nullptr) {
            setError(result, "too many Output settings in one request");
            return;
        }
        edit->led_count = ledCount;
        edit->fields |= SERVO_FIELD_LED_COUNT;
        result->changed = true;
    }

    // Each Output's Motion Profile (ADR 0052, #414): time to full throw, time
    // to get up to speed and the ease, and what it does at power-up, under the
    // names configMotionFieldName() gives them. A value outside what the stored row takes is REFUSED with the
    // field and the range, never clamped: the bounds are the NVS parser's own
    // (servoOutputRowNormalise(), SERVO_THROW_MS_MIN and the rest), so a number
    // this door takes is exactly one the row keeps, and a number it refuses is
    // one the builder hears about instead of finding a different one saved.
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        const BoardOutput& output = BOARD_OUTPUTS[i];
        char throwField[CONFIG_MOTION_FIELD_NAME_MAX] = {};
        char accelField[CONFIG_MOTION_FIELD_NAME_MAX] = {};
        char easeField[CONFIG_MOTION_FIELD_NAME_MAX] = {};
        char bootField[CONFIG_MOTION_FIELD_NAME_MAX] = {};
        if (!configMotionFieldName(throwField, sizeof(throwField), output.id, CONFIG_MOTION_THROW) ||
            !configMotionFieldName(accelField, sizeof(accelField), output.id, CONFIG_MOTION_ACCEL) ||
            !configMotionFieldName(easeField, sizeof(easeField), output.id, CONFIG_MOTION_EASE) ||
            !configMotionFieldName(bootField, sizeof(bootField), output.id, CONFIG_MOTION_BOOT)) {
            // The buffers are sized from the longest stored id, so this is a
            // build whose table outgrew CONFIG_MOTION_FIELD_NAME_MAX: refuse,
            // rather than read a truncated name as some other field.
            setError(result, "an Output's Motion Profile field name does not fit");
            return;
        }
        const bool hasThrow = configParamHas(params, throwField);
        const bool hasAccel = configParamHas(params, accelField);
        const bool hasEase = configParamHas(params, easeField);
        const bool hasBoot = configParamHas(params, bootField);
        if (!hasThrow && !hasAccel && !hasEase && !hasBoot) {
            continue;
        }

        uint16_t throwMs = 0;
        if (hasThrow &&
            !paramUint16(params, throwField, SERVO_THROW_MS_MIN, SERVO_THROW_MS_MAX, &throwMs)) {
            char err[192];
            snprintf(err, sizeof(err), "%s must be %u..%u ms", throwField,
                     (unsigned)SERVO_THROW_MS_MIN, (unsigned)SERVO_THROW_MS_MAX);
            setError(result, err);
            return;
        }
        uint16_t accelMs = 0;
        if (hasAccel &&
            !paramUint16(params, accelField, SERVO_ACCEL_MS_MIN, SERVO_ACCEL_MS_MAX, &accelMs)) {
            char err[192];
            snprintf(err, sizeof(err), "%s must be %u..%u ms", accelField,
                     (unsigned)SERVO_ACCEL_MS_MIN, (unsigned)SERVO_ACCEL_MS_MAX);
            setError(result, err);
            return;
        }
        ServoEasing easing = SERVO_EASE_NONE;
        if (hasEase && !servoParseEasing(configParamGet(params, easeField), &easing)) {
            char err[192];
            snprintf(err, sizeof(err), "%s must be none, soft or overshoot", easeField);
            setError(result, err);
            return;
        }
        // Limp is what a row nobody configured does; a value that is not one of
        // the three is refused rather than read as limp, so a typo can never
        // quietly take a Part off its power-up home or put one on it.
        ServoBootBehaviour boot = SERVO_BOOT_LIMP;
        if (hasBoot && !servoParseBootBehaviour(configParamGet(params, bootField), &boot)) {
            char err[192];
            snprintf(err, sizeof(err), "%s must be limp, home-hold or home-release", bootField);
            setError(result, err);
            return;
        }

        ServoOutputEdit* edit = typedEditFor(&result->servoOutputs, output.channel);
        if (edit == nullptr) {
            setError(result, "too many Output settings in one request");
            return;
        }
        if (hasThrow) {
            edit->throw_ms = throwMs;
            edit->fields |= SERVO_FIELD_THROW_MS;
            appendApplied(&result->applied, "[CFG] %s updated to %u", throwField, (unsigned)throwMs);
        }
        if (hasAccel) {
            edit->accel_ms = accelMs;
            edit->fields |= SERVO_FIELD_ACCEL_MS;
            appendApplied(&result->applied, "[CFG] %s updated to %u", accelField, (unsigned)accelMs);
        }
        if (hasEase) {
            edit->easing = easing;
            edit->fields |= SERVO_FIELD_EASING;
            appendApplied(&result->applied, "[CFG] %s updated to %s", easeField,
                          servoEasingToString(easing));
        }
        if (hasBoot) {
            edit->boot = boot;
            edit->fields |= SERVO_FIELD_BOOT;
            appendApplied(&result->applied, "[CFG] %s updated to %s", bootField,
                          servoBootBehaviourToString(boot));
        }
        result->changed = true;
    }

    // A capture: the builder drove the Part until it looked right and pressed
    // Set MIN / Set CENTER / Set MAX, so what arrives is one Output Address, one
    // position on it, and the width the dial was standing at (#364, #291).
    //
    // All three fields or none, the shape a Part move already uses, and for the
    // same reason: they mean nothing apart. An address without a position is not
    // a capture, and a width with no address is not one either.
    //
    // It rides the same addressed-edit list the five field sets fill, so it
    // reaches the rows through the one door the Commit Step already opens; what
    // marks it out is `capture`, which is what makes it record that a human
    // measured this Output rather than that somebody typed a number (ADR 0041).
    // The bounds are the widest a servo takes, as they are for a typed endpoint:
    // the authoritative clamp is the fitted component's band, applied on the row
    // where it can report having moved the number.
    if (configParamHas(params, "captureOutput") || configParamHas(params, "captureEnd") ||
        configParamHas(params, "captureUs")) {
        ServoOutputEdit capture = {};
        ServoOutputEnd end = SERVO_END_CENTRE;
        uint16_t capturedUs = 0;
        const char* address = configParamGet(params, "captureOutput");
        if (address == nullptr ||
            !servoOutputParseAddress(address, &capture.driver, &capture.channel) ||
            !servoParseOutputEnd(configParamGet(params, "captureEnd"), &end) ||
            !paramUint16(params, "captureUs", kServoPulseMinUs, kServoPulseMaxUs, &capturedUs)) {
            setError(result,
                     "captureOutput, captureEnd and captureUs must be sent together: an Output "
                     "Address, one of open/centre/close, and a width 500..2500");
            return;
        }
        capture.kind = SERVO_EDIT_CAPTURE;
        switch (end) {
            case SERVO_END_OPEN:
                capture.fields = SERVO_FIELD_OPEN;
                capture.open_us = capturedUs;
                break;
            case SERVO_END_CENTRE:
                capture.fields = SERVO_FIELD_CENTRE;
                capture.centre_us = capturedUs;
                break;
            case SERVO_END_CLOSE:
            default:
                capture.fields = SERVO_FIELD_CLOSE;
                capture.close_us = capturedUs;
                break;
        }
        result->servoOutputs.edits[result->servoOutputs.count++] = capture;
        appendApplied(&result->applied, "[CFG] capture %s %s at %u us", address,
                      configParamGet(params, "captureEnd"), (unsigned)capturedUs);
        result->changed = true;
    }

    // Reverse (#364, ADR 0041): the builder has ticked `reverse` on the dial,
    // saying the linkage runs the other way. One Output Address and nothing
    // else -- no width travels with it, so a page working from a second-old
    // copy of the pair cannot write a stale number back, and the swap is made
    // on the row from what the row holds.
    if (configParamHas(params, "reverseOutput")) {
        ServoOutputEdit reverse = {};
        const char* address = configParamGet(params, "reverseOutput");
        if (address == nullptr ||
            !servoOutputParseAddress(address, &reverse.driver, &reverse.channel)) {
            setError(result, "reverseOutput must be an Output Address");
            return;
        }
        reverse.kind = SERVO_EDIT_REVERSE;
        result->servoOutputs.edits[result->servoOutputs.count++] = reverse;
        appendApplied(&result->applied, "[CFG] reverse %s", address);
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
