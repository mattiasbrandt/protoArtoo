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
#include <stdlib.h>
#include <string.h>

#include "api_helpers.h"
#include "board_outputs.h"
#include "component_registry.h"
#include "config.h"
#include "dome_math.h"  // domePulsesInOrder() - the one order rule for the ESC pulse set
#include "drive_speed_preset.h"
#include "servo_component_helpers.h"

namespace {

constexpr uint16_t kServoPulseMinUs = 500;
constexpr uint16_t kServoPulseMaxUs = 2500;

void appendApplied(ConfigAppliedFields* applied, const char* fmt, ...) {
    if (applied->count >= ConfigAppliedFields::kMaxLines) {
        applied->dropped++;
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(applied->lines[applied->count], sizeof(applied->lines[0]), fmt, args);
    va_end(args);
    applied->count++;
}

// Every refusal says why and about which field, beside its sentence (#425):
// the reason is a parameter, so no error write can leave it unset.
void setError(ConfigApplyResult* result, const char* message, ApplyRefusalReason reason,
              const char* field, const char* accepts = nullptr) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
    applyRefusalSet(&result->error.refusal, reason, field, accepts);
}

// A number outside [lo, hi]: `accepts` is the range, read the way the sentence
// already writes it.
void setRangeError(ConfigApplyResult* result, const char* message, const char* field, long lo,
                   long hi) {
    result->error.hasError = true;
    snprintf(result->error.message, sizeof(result->error.message), "%s", message);
    applyRefusalSetRange(&result->error.refusal, field, lo, hi);
}

// The words a true/false field takes, as every boolean refusal below states them.
constexpr const char* kBoolAccepts = "true,false,1,0";

// The first of `names` this request sent, or nullptr. A clash between values
// is refused naming the field that was sent (#425); when several were, the
// first in the order this core reads them.
const char* firstSent(const ConfigParamSource& params, const char* const* names, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (configParamHas(params, names[i])) {
            return names[i];
        }
    }
    return nullptr;
}

// The first of `names` this request did NOT send, or nullptr: the partner a
// sent-together group is missing, which is the field its refusal names.
const char* firstMissing(const ConfigParamSource& params, const char* const* names, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!configParamHas(params, names[i])) {
            return names[i];
        }
    }
    return nullptr;
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
        setError(result, refusal, ApplyRefusalReason::MissingArgument,
                 hasDesign ? variantName : designName);
        return false;
    }
    DroidDesignChoice choice = {};
    if (!droidDesignChoiceSet(&choice, configParamGet(params, designName),
                              configParamGet(params, variantName)) ||
        !droidDesignChoiceIsKnown(choice)) {
        // The catalog answers for the pair, not for either half alone, so the
        // design names the refusal.
        setError(result, refusal, ApplyRefusalReason::OutOfRange, designName);
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

// -----------------------------------------------------------------------------
// The Configuration in the shape GET /api/config reads it (ADR 0068, #423)
//
// A JSON body is the GET shape, so a restore posts back what a backup holds and
// nothing in the browser flattens it first. Each entry below says where GET
// /api/config puts a field this core reads, beside the form name the pages and
// the Controller Console send it under. Both doors reach the SAME check further
// down: a value found here is answered under its form name, so each range is
// written once and a refusal reads the same whichever door the value came in by.
//
// A key GET carries that no entry names is a reading, not a setting - `wifi`,
// `activeToggles`, `drive.speedPreset`, a label - and is ignored rather than
// refused, so a whole GET answer can be posted back as it stands.
// -----------------------------------------------------------------------------
struct GetShapeField {
    const char* param;    // the form name, which is what the checks below read
    const char* path[3];  // where GET /api/config has it; unused trailing keys are nullptr
};

const GetShapeField kGetShapeFields[] = {
    {"speedLimitMax", {"drive", "speedLimitMax"}},
    {"speedPresetSlow", {"drive", "speedPresetSlow"}},
    {"speedPresetNormal", {"drive", "speedPresetNormal"}},
    {"speedPresetTurbo", {"drive", "speedPresetTurbo"}},
    {"stationary", {"drive", "stationary"}},
    {"webDriveTimeoutMs", {"drive", "webDriveTimeoutMs"}},
    {"sbusTimeoutMs", {"rc", "sbusTimeoutMs"}},
    {"sbusRecvCh2", {"rc", "sbus", "recvCh2"}},
    {"rcInputMode", {"rc", "inputMode"}},
    {"rcMember", {"rc", "member"}},
    {"soundMember", {"components", "audio", "member"}},
    {"logLevel", {"system", "logLevel"}},
    {"protoR2linkWifiPeerIp", {"protoR2link", "wifiPeerIp"}},
    {"domeEscNeutralUs", {"domeEsc", "neutralUs"}},
    {"domeEscMinPulseUs", {"domeEsc", "minPulseUs"}},
    {"domeEscMaxPulseUs", {"domeEsc", "maxPulseUs"}},
    {"domeEscSpeedLimitPct", {"domeEsc", "speedLimitPct"}},
    {"domeEscRndEnable", {"domeEsc", "rndEnable"}},
    {"domeEscRndSpeedPct", {"domeEsc", "rndSpeedPct"}},
    {"domeEscRndPauseMin", {"domeEsc", "rndPauseMin"}},
    {"domeEscRndPauseMax", {"domeEsc", "rndPauseMax"}},
    {"domeEscRndMoveMs", {"domeEsc", "rndMoveMs"}},
    // The Component Toggles that are not an Output. An Output's wired tick is
    // a field of its row (the `outputs` rows below), not of components{}.
    {"enableDomeEsc", {"components", "domeEsc", "enabled"}},
    {"enableRcCh1", {"components", "rcCh1", "enabled"}},
    {"enableRcCh2", {"components", "rcCh2", "enabled"}},
    {"enableRcCh3", {"components", "rcCh3", "enabled"}},
    {"enableRcCh4", {"components", "rcCh4", "enabled"}},
    {"enableRcCh5", {"components", "rcCh5", "enabled"}},
    {"enableRcCh6", {"components", "rcCh6", "enabled"}},
    {"enableDrive", {"components", "drive", "enabled"}},
    {"enableAudio", {"components", "audio", "enabled"}},
    {"enableProtoR2link", {"components", "protoR2link", "enabled"}},
    // The Droid Build and Guided Setup's record travel with a backup like any
    // other config key (operator, 2026-09-17 on #371). The two lists are JSON
    // arrays on GET and are read as the comma-joined list the form takes.
    {"domeDesign", {"droidBuild", "domeDesign"}},
    {"domeVariant", {"droidBuild", "domeVariant"}},
    {"bodyDesign", {"droidBuild", "bodyDesign"}},
    {"bodyVariant", {"droidBuild", "bodyVariant"}},
    {"fittedParts", {"droidBuild", "fitted"}},
    {"guidedSetupRun", {"guidedSetup", "run"}},
    {"guidedSetupVisited", {"guidedSetup", "visited"}},
    {"guidedSetupSummaryDone", {"guidedSetup", "summaryDone"}},
};

// What a JSON value that no form field could ever hold reads as: an object
// where a number belongs, or a list with something other than words in it. It
// is a value no check below takes, so the field is refused with its own
// sentence rather than dropped - an object sent as a peer IP must not read as
// the empty string that clears it.
constexpr const char kNotAFieldValue[] = "(not a value)";

// The leaf a path names, or a null variant when any key on the way is absent.
// Read-only: a lookup never adds a member to the body.
JsonVariantConst getShapeLeaf(JsonObjectConst body, const char* const* path) {
    JsonVariantConst at = body;
    for (size_t i = 0; i < 3 && path[i] != nullptr; ++i) {
        at = at[path[i]];
        if (at.isNull()) {
            break;
        }
    }
    return at;
}

// Turns one leaf into the text a form would have carried: a number or a bool
// as JSON writes it, a list of words comma-joined, anything else the value no
// check takes. Strings stay as they are. The text is copied into the body's
// own pool, so every pointer this core reads out of it lives as long as the
// request does - the lifetime ConfigParamSource promises.
//
// False only when the body could not hold the text, which the caller refuses:
// a field that silently fell out of a restore is the failure ADR 0068 exists
// to end.
bool normaliseLeaf(JsonVariant leaf) {
    if (leaf.isNull() || leaf.is<const char*>()) {
        return true;
    }
    if (leaf.is<JsonArrayConst>()) {
        JsonArrayConst list = leaf.as<JsonArrayConst>();
        size_t needed = 1;
        for (JsonVariantConst item : list) {
            if (!item.is<const char*>()) {
                return leaf.set(kNotAFieldValue);
            }
            needed += strlen(item.as<const char*>()) + 1;
        }
        char* joined = static_cast<char*>(malloc(needed));
        if (joined == nullptr) {
            return false;
        }
        size_t used = 0;
        joined[0] = '\0';
        for (JsonVariantConst item : list) {
            used += (size_t)snprintf(joined + used, needed - used, "%s%s", used == 0 ? "" : ",",
                                     item.as<const char*>());
        }
        const bool stored = leaf.set(joined);  // char*, so the body copies it
        free(joined);
        return stored;
    }
    if (leaf.is<JsonObjectConst>()) {
        return leaf.set(kNotAFieldValue);
    }
    // A number or a bool, written the way JSON wrote it: 1.5 stays 1.5 and is
    // refused by an integer field's own parse, rather than truncated to 1.
    char text[24] = {};
    const size_t length = serializeJson(leaf, text, sizeof(text));
    if (length == 0 || length >= sizeof(text)) {
        return leaf.set(kNotAFieldValue);
    }
    return leaf.set(text);  // char[], so the body copies it
}

// The walk normaliseLeaf() needs: the parent of the leaf as a writable object,
// found without adding anything. A path whose parent is not an object names
// nothing, and nothing is written.
bool normaliseGetShapeField(JsonDocument& body, const GetShapeField& field) {
    JsonObject parent = body.as<JsonObject>();
    size_t depth = 0;
    while (depth + 1 < 3 && field.path[depth + 1] != nullptr) {
        parent = parent[field.path[depth]].as<JsonObject>();
        if (parent.isNull()) {
            return true;
        }
        ++depth;
    }
    if (parent.isNull() || !parent[field.path[depth]].is<JsonVariantConst>()) {
        return true;
    }
    return normaliseLeaf(parent[field.path[depth]].as<JsonVariant>());
}

// -----------------------------------------------------------------------------
// The Output rows (ADR 0068, #423)
//
// A JSON body's `outputs` is the row door: one row per Output, keyed by its
// Output Address, in the shape GET /api/servo/outputs reads it. Every key a
// row can set is listed here and read as text like any other field; the rest
// of a row - its name, its band, where it has been told to be - is a reading
// and is ignored, so a row read by GET can be posted back as it stands.
// -----------------------------------------------------------------------------
const char* const kOutputRowKeys[] = {
    "address", "wired", "component", "ledCount", "throwMs", "accelMs",
    "ease", "boot", "openUs", "centreUs", "closeUs", "calibrated",
};

// The text of one key of a row, as configRequestGet() answers a field: nullptr
// when absent, the text when it is one, kNotAFieldValue otherwise.
const char* rowText(JsonObjectConst row, const char* key) {
    JsonVariantConst value = row[key];
    if (value.isNull()) {
        return nullptr;
    }
    return value.is<const char*>() ? value.as<const char*>() : kNotAFieldValue;
}

// The row a body carries for an address, or a null object.
JsonObjectConst rowAt(JsonObjectConst body, ServoOutputDriver driver, uint8_t channel) {
    for (JsonVariantConst item : body["outputs"].as<JsonArrayConst>()) {
        JsonObjectConst row = item.as<JsonObjectConst>();
        ServoOutputDriver rowDriver = SERVO_DRIVER_LEDC;
        uint8_t rowChannel = 0;
        if (!row.isNull() && servoOutputParseAddress(rowText(row, "address"), &rowDriver, &rowChannel) &&
            rowDriver == driver && rowChannel == channel) {
            return row;
        }
    }
    return JsonObjectConst();
}

// A config request: the form it came as, and, when it came with a JSON body,
// that body in the GET shape. It is the ConfigParamSource every check below
// reads, so a check never knows which door its value came in by.
struct ConfigRequest {
    const ConfigParamSource* form;
    JsonObjectConst body;
};

const char* configRequestGet(void* ctx, const char* name) {
    const ConfigRequest* request = static_cast<const ConfigRequest*>(ctx);
    // A field named on the form wins over the body. No page sends both, and a
    // form field is the more specific statement when one does.
    const char* value = configParamGet(*request->form, name);
    if (value != nullptr || request->body.isNull()) {
        return value;
    }
    for (const GetShapeField& field : kGetShapeFields) {
        if (strcmp(field.param, name) != 0) {
            continue;
        }
        JsonVariantConst leaf = getShapeLeaf(request->body, field.path);
        if (leaf.isNull()) {
            return nullptr;
        }
        return leaf.is<const char*>() ? leaf.as<const char*>() : kNotAFieldValue;
    }
    // An Output's wired tick is its row's `wired`, answered under the form name
    // that saves it (BOARD_OUTPUTS' `enabledField`), so the Component Toggle
    // check reads it whichever door it came in by.
    for (const BoardOutput& output : BOARD_OUTPUTS) {
        if (strcmp(output.enabledField, name) != 0) {
            continue;
        }
        JsonObjectConst row = rowAt(request->body, SERVO_DRIVER_LEDC, output.channel);
        return row.isNull() ? nullptr : rowText(row, "wired");
    }
    return nullptr;
}

// Read a JSON body into `body` and make every field it carries readable as
// text. False, with the refusal set, when it cannot be: a body that is not
// JSON, or one the controller could not hold whole once its numbers were
// written out as text.
bool readGetShapeBody(const char* raw, JsonDocument* body, ConfigApplyResult* result) {
    if (deserializeJson(*body, raw)) {
        setError(result, "invalid json body", ApplyRefusalReason::MalformedArgument, "plain");
        return false;
    }
    bool whole = true;
    for (const GetShapeField& field : kGetShapeFields) {
        whole = whole && normaliseGetShapeField(*body, field);
    }
    // A row's keys the same way, in place. `parts` stays a list: the row door
    // reads it as one.
    for (JsonVariant item : (*body)["outputs"].as<JsonArray>()) {
        JsonObject row = item.as<JsonObject>();
        for (const char* key : kOutputRowKeys) {
            if (!row.isNull() && row[key].is<JsonVariantConst>()) {
                whole = whole && normaliseLeaf(row[key].as<JsonVariant>());
            }
        }
    }
    if (!whole || body->overflowed()) {
        setError(result, "json body too large to read whole", ApplyRefusalReason::MalformedArgument,
                 "plain");
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// The row door (ADR 0068, #423)
//
// Each row is one Output, named by its Output Address, and a field it carries
// is set. A refusal about a row's field names both, `<address>.<key>`
// (`ledc:4.ledCount`), with the canonical address - at most 8 characters, so
// the name always fits a refusal's field.
//
// The ends and the centre are checked against what any servo takes, 500..2500,
// and nothing narrower: the fitted component's band is applied on the row by
// the Commit Step, which CLAMPS into it and names what it moved in the answer,
// because the band can narrow after the ends were recorded and refusing would
// throw a calibration away (ADR 0068). Every other field is refused when it is
// out of range, with its field, reason and accepts (#425).
// -----------------------------------------------------------------------------
void setRowError(ConfigApplyResult* result, const char* address, const char* key,
                 const char* says, ApplyRefusalReason reason, const char* accepts = nullptr) {
    char field[APPLY_REFUSAL_FIELD_MAX];
    snprintf(field, sizeof(field), "%s.%s", address, key);
    char message[sizeof(ConfigApplyError::message)];
    snprintf(message, sizeof(message), "%s %s", field, says);
    setError(result, message, reason, field, accepts);
}

void setRowRangeError(ConfigApplyResult* result, const char* address, const char* key, long lo,
                      long hi, const char* unit = "") {
    char field[APPLY_REFUSAL_FIELD_MAX];
    snprintf(field, sizeof(field), "%s.%s", address, key);
    char message[sizeof(ConfigApplyError::message)];
    snprintf(message, sizeof(message), "%s must be %ld..%ld%s", field, lo, hi, unit);
    setRangeError(result, message, field, lo, hi);
}

bool textUint16(const char* raw, uint16_t lo, uint16_t hi, uint16_t* out) {
    uint32_t value = 0;
    if (raw == nullptr || !parseUint32Value(raw, &value) || value < lo || value > hi) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

// One row's Part list, as stated: at most SERVO_OUTPUT_PART_SLOTS ids this
// build models. `stated` is every Part an earlier row in this request named;
// naming one of those again puts a Part on two Outputs, which the glossary's
// Part forbids, and is refused `conflict`.
bool readRowParts(JsonVariantConst value, const char* address, uint32_t* stated,
                  ServoOutputEdit* edit, ConfigApplyResult* result) {
    if (!value.is<JsonArrayConst>()) {
        setRowError(result, address, "parts", "must be a list of Part ids",
                    ApplyRefusalReason::OutOfRange);
        return false;
    }
    JsonArrayConst list = value.as<JsonArrayConst>();
    if (list.size() > SERVO_OUTPUT_PART_SLOTS) {
        setRowRangeError(result, address, "parts", 0, SERVO_OUTPUT_PART_SLOTS, " Parts");
        return false;
    }
    edit->partCount = 0;
    for (JsonVariantConst item : list) {
        const char* id = item.as<const char*>();
        if (id == nullptr || id[0] == '\0' || !servoOutputPartIdIsValid(id)) {
            setRowError(result, address, "parts", "names a Part this build does not model",
                        ApplyRefusalReason::OutOfRange);
            return false;
        }
        const uint8_t index = (uint8_t)droidPartIndexOf(id);
        bool onThisRow = false;
        for (uint8_t i = 0; i < edit->partCount; ++i) {
            onThisRow = onThisRow || edit->parts[i] == index;
        }
        if (onThisRow) {
            continue;  // the same Part twice on one wire is the same wire
        }
        const uint32_t bit = (uint32_t)1u << (index % 32);
        if ((stated[index / 32] & bit) != 0) {
            setRowError(result, address, "parts",
                        "names a Part another row names too: a Part is on at most one Output",
                        ApplyRefusalReason::Conflict);
            return false;
        }
        stated[index / 32] |= bit;
        edit->parts[edit->partCount++] = index;
    }
    edit->fields |= SERVO_FIELD_PARTS;
    return true;
}

// One row of the row door, onto the edit for its address. False, with the
// refusal set, on the first field it cannot take.
bool readOutputRow(JsonObjectConst row, const char* address, const BoardOutput* board,
                   uint32_t* stated, ServoOutputEdit* edit, ConfigApplyResult* result) {
    const char* raw = nullptr;

    // An Output with a wired tick has it read under its form name, through the
    // Component Toggle check (configRequestGet()). One with none - an
    // expander's - is always wired, and saying otherwise is refused.
    if (board == nullptr && (raw = rowText(row, "wired")) != nullptr) {
        bool wired = false;
        if (!parseBoolValue(raw, &wired) || !wired) {
            setRowError(result, address, "wired", "is always true: this Output has no wired tick",
                        ApplyRefusalReason::OutOfRange, "true");
            return false;
        }
    }

    if ((raw = rowText(row, "component")) != nullptr) {
        const ServoComponentType component = parseServoCompType(raw);
        if (strcmp(servoCompTypeToString(component), raw) != 0) {
            setRowError(result, address, "component", "must be none, mg996r, mg90s or rgb",
                        ApplyRefusalReason::OutOfRange, "none,mg996r,mg90s,rgb");
            return false;
        }
        edit->component = component;
        edit->fields |= SERVO_FIELD_COMPONENT;
    }

    if ((raw = rowText(row, "ledCount")) != nullptr) {
        if (board == nullptr || !board->lightCapable) {
            setRowError(result, address, "ledCount", "cannot be set: no light can go on this Output",
                        ApplyRefusalReason::OutOfRange);
            return false;
        }
        uint16_t ledCount = 0;
        if (!textUint16(raw, SERVO_LIGHT_LEDS_MIN, SERVO_LIGHT_LEDS_MAX, &ledCount)) {
            setRowRangeError(result, address, "ledCount", SERVO_LIGHT_LEDS_MIN, SERVO_LIGHT_LEDS_MAX);
            return false;
        }
        edit->led_count = (uint8_t)ledCount;
        edit->fields |= SERVO_FIELD_LED_COUNT;
    }

    // The Motion Profile's bounds are the stored row's own (servoOutputRowNormalise()),
    // so a number this door takes is exactly one the row keeps.
    if ((raw = rowText(row, "throwMs")) != nullptr) {
        if (!textUint16(raw, SERVO_THROW_MS_MIN, SERVO_THROW_MS_MAX, &edit->throw_ms)) {
            setRowRangeError(result, address, "throwMs", SERVO_THROW_MS_MIN, SERVO_THROW_MS_MAX, " ms");
            return false;
        }
        edit->fields |= SERVO_FIELD_THROW_MS;
    }
    if ((raw = rowText(row, "accelMs")) != nullptr) {
        if (!textUint16(raw, SERVO_ACCEL_MS_MIN, SERVO_ACCEL_MS_MAX, &edit->accel_ms)) {
            setRowRangeError(result, address, "accelMs", SERVO_ACCEL_MS_MIN, SERVO_ACCEL_MS_MAX, " ms");
            return false;
        }
        edit->fields |= SERVO_FIELD_ACCEL_MS;
    }
    if ((raw = rowText(row, "ease")) != nullptr) {
        if (!servoParseEasing(raw, &edit->easing)) {
            setRowError(result, address, "ease", "must be none, soft or overshoot",
                        ApplyRefusalReason::OutOfRange, "none,soft,overshoot");
            return false;
        }
        edit->fields |= SERVO_FIELD_EASING;
    }
    // Limp is what a row nobody configured does; a word that is not one of the
    // three is refused rather than read as limp, so a typo can never quietly
    // take a Part off its power-up home or put one on it.
    if ((raw = rowText(row, "boot")) != nullptr) {
        if (!servoParseBootBehaviour(raw, &edit->boot)) {
            setRowError(result, address, "boot", "must be limp, home-hold or home-release",
                        ApplyRefusalReason::OutOfRange, "limp,home-hold,home-release");
            return false;
        }
        edit->fields |= SERVO_FIELD_BOOT;
    }

    struct Width {
        const char* key;
        uint16_t ServoOutputEdit::*member;
        uint16_t bit;
    };
    static const Width kWidths[] = {
        {"openUs", &ServoOutputEdit::open_us, SERVO_FIELD_OPEN},
        {"centreUs", &ServoOutputEdit::centre_us, SERVO_FIELD_CENTRE},
        {"closeUs", &ServoOutputEdit::close_us, SERVO_FIELD_CLOSE},
    };
    for (const Width& width : kWidths) {
        if ((raw = rowText(row, width.key)) == nullptr) {
            continue;
        }
        if (!textUint16(raw, kServoPulseMinUs, kServoPulseMaxUs, &(edit->*width.member))) {
            setRowRangeError(result, address, width.key, kServoPulseMinUs, kServoPulseMaxUs);
            return false;
        }
        edit->fields |= width.bit;
    }

    if ((raw = rowText(row, "calibrated")) != nullptr) {
        if (!parseBoolValue(raw, &edit->calibrated)) {
            setRowError(result, address, "calibrated", "must be true or false",
                        ApplyRefusalReason::OutOfRange, kBoolAccepts);
            return false;
        }
        edit->fields |= SERVO_FIELD_CALIBRATED;
    }

    JsonVariantConst parts = row["parts"];
    if (!parts.isNull() && !readRowParts(parts, address, stated, edit, result)) {
        return false;
    }
    return true;
}

// The whole row set: at most one row per Output, at most one Output per Part.
// Every row is read and checked before any lands - the Commit Step applies the
// edits only when this and every other field in the request have passed, so a
// refused row leaves the scalars beside it unwritten too (ADR 0068, one Write
// Window).
bool applyOutputRows(JsonObjectConst body, ConfigApplyResult* result) {
    JsonVariantConst value = body["outputs"];
    if (value.isNull()) {
        return true;
    }
    if (!value.is<JsonArrayConst>()) {
        setError(result, "outputs must be a list of Output rows", ApplyRefusalReason::OutOfRange,
                 "outputs");
        return false;
    }
    JsonArrayConst rows = value.as<JsonArrayConst>();
    if (rows.size() > SERVO_OUTPUT_ROW_MAX) {
        setRangeError(result, "outputs holds at most one row per Output", "outputs", 0,
                      SERVO_OUTPUT_ROW_MAX);
        return false;
    }

    uint32_t statedParts[(DROID_PART_COUNT + 31) / 32] = {};
    uint16_t seen[SERVO_OUTPUT_ROW_MAX] = {};
    size_t seenCount = 0;
    for (JsonVariantConst item : rows) {
        JsonObjectConst row = item.as<JsonObjectConst>();
        ServoOutputDriver driver = SERVO_DRIVER_LEDC;
        uint8_t channel = 0;
        const char* rawAddress = row.isNull() ? nullptr : rowText(row, "address");
        if (rawAddress == nullptr) {
            setError(result, "every Output row must carry its address",
                     ApplyRefusalReason::MissingArgument, "address");
            return false;
        }
        if (!servoOutputParseAddress(rawAddress, &driver, &channel)) {
            setError(result, "an Output row's address must be an Output Address, such as ledc:3",
                     ApplyRefusalReason::OutOfRange, "address");
            return false;
        }
        char address[SERVO_OUTPUT_ADDRESS_STR_MAX + 1] = {};
        servoOutputFormatAddress(address, sizeof(address), driver, channel);

        const uint16_t key = (uint16_t)(((uint16_t)driver << 8) | channel);
        for (size_t i = 0; i < seenCount; ++i) {
            if (seen[i] == key) {
                setRowError(result, address, "address", "is named by two rows: one row per Output",
                            ApplyRefusalReason::Conflict);
                return false;
            }
        }
        seen[seenCount++] = key;

        // Read whole before it joins the list, so a refused row leaves nothing
        // of itself behind. One row per address (checked above), so a row is
        // one typed edit and there is nothing to merge.
        ServoOutputEdit edit = {};
        edit.driver = driver;
        edit.channel = channel;
        const BoardOutput* board =
            driver == SERVO_DRIVER_LEDC ? boardOutputOnChannel(channel) : nullptr;
        if (!readOutputRow(row, address, board, statedParts, &edit, result)) {
            return false;
        }
        if (edit.fields == 0) {
            continue;
        }
        // Rows come first and number at most SERVO_OUTPUT_ROW_MAX (checked
        // above), which the list holds with room for a capture and a reverse.
        result->servoOutputs.edits[result->servoOutputs.count++] = edit;
        appendApplied(&result->applied, "[CFG] output %s updated", address);
        result->changed = true;
    }
    return true;
}

}  // namespace

void configApply(const ConfigParamSource& form, ConfigSnapshot* working,
                  bool domeEnabledBefore, ConfigApplyResult* result) {
    *result = ConfigApplyResult{};

    // A JSON body is read once, here, into the GET shape; every field below is
    // then read through `params`, whichever door it came in by.
    JsonDocument body;
    ConfigRequest request{&form, JsonObjectConst()};
    if (configParamHas(form, "plain")) {
        if (!readGetShapeBody(configParamGet(form, "plain"), &body, result)) {
            return;
        }
        request.body = body.as<JsonObjectConst>();
    }
    ConfigParamSource params;
    params.ctx = &request;
    params.get = configRequestGet;

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
        setRangeError(result, "speedLimitMax must be 0..600", "speedLimitMax", 0, SPEED_LIMIT_MAX);
        return;
    }

    int16_t speedPresetSlow;
    if (paramInt16(params, "speedPresetSlow", 0, SPEED_LIMIT_MAX, &speedPresetSlow)) {
        working->drive.speedPresetSlow = speedPresetSlow;
        speedPresetValuesProvided = true;
        appendApplied(&result->applied, "[CFG] speedPresetSlow updated to %d", (int)speedPresetSlow);
        result->changed = true;
    } else if (configParamHas(params, "speedPresetSlow")) {
        setRangeError(result, "speedPresetSlow must be 0..600", "speedPresetSlow", 0, SPEED_LIMIT_MAX);
        return;
    }

    int16_t speedPresetNormal;
    if (paramInt16(params, "speedPresetNormal", 0, SPEED_LIMIT_MAX, &speedPresetNormal)) {
        working->drive.speedPresetNormal = speedPresetNormal;
        speedPresetValuesProvided = true;
        appendApplied(&result->applied, "[CFG] speedPresetNormal updated to %d", (int)speedPresetNormal);
        result->changed = true;
    } else if (configParamHas(params, "speedPresetNormal")) {
        setRangeError(result, "speedPresetNormal must be 0..600", "speedPresetNormal", 0,
                      SPEED_LIMIT_MAX);
        return;
    }

    int16_t speedPresetTurbo;
    if (paramInt16(params, "speedPresetTurbo", 0, SPEED_LIMIT_MAX, &speedPresetTurbo)) {
        working->drive.speedPresetTurbo = speedPresetTurbo;
        speedPresetValuesProvided = true;
        appendApplied(&result->applied, "[CFG] speedPresetTurbo updated to %d", (int)speedPresetTurbo);
        result->changed = true;
    } else if (configParamHas(params, "speedPresetTurbo")) {
        setRangeError(result, "speedPresetTurbo must be 0..600", "speedPresetTurbo", 0, SPEED_LIMIT_MAX);
        return;
    }

    if (speedPresetValuesProvided &&
        !speedPresetValuesAreUnique(working->drive.speedPresetSlow, working->drive.speedPresetNormal,
                                     working->drive.speedPresetTurbo)) {
        static const char* const kPresets[] = {"speedPresetSlow", "speedPresetNormal",
                                               "speedPresetTurbo"};
        setError(result, "speed presets must be distinct values", ApplyRefusalReason::Conflict,
                 firstSent(params, kPresets, sizeof(kPresets) / sizeof(kPresets[0])));
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
        setRangeError(result, "webDriveTimeoutMs must be 100..5000", "webDriveTimeoutMs", 100, 5000);
        return;
    }

    uint32_t sbusTimeoutMs;
    if (paramUint32(params, "sbusTimeoutMs", 50, 5000, &sbusTimeoutMs)) {
        working->drive.sbusTimeoutMs = sbusTimeoutMs;
        appendApplied(&result->applied, "[CFG] sbusTimeoutMs updated to %u", (unsigned)sbusTimeoutMs);
        result->changed = true;
    } else if (configParamHas(params, "sbusTimeoutMs")) {
        setRangeError(result, "sbusTimeoutMs must be 50..5000", "sbusTimeoutMs", 50, 5000);
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
        setError(result, "stationary must be true/false or 1/0", ApplyRefusalReason::OutOfRange,
                 "stationary", kBoolAccepts);
        return;
    }
    // The stationary release cue stays in the shell (ADR 0012); the shell also
    // needs to know whether this request is the one deciding the mode.
    result->stationaryStated = stationaryProvided;

    if (configParamHas(params, "logLevel")) {
        int16_t lvl = 0;
        if (paramInt16(params, "logLevel", 1, 4, &lvl)) {
            working->system.logLevel = (uint8_t)lvl;
            appendApplied(&result->applied, "[CFG] logLevel updated to %d", (int)lvl);
            result->changed = true;
        } else {
            setRangeError(result, "logLevel must be 1 (Error), 2 (Warning), 3 (Info), or 4 (Debug)",
                          "logLevel", 1, 4);
            return;
        }
    }

    if (configParamHas(params, "rcInputMode")) {
        RcInputMode mode;
        if (!parseRcInputMode(configParamGet(params, "rcInputMode"), &mode)) {
            setError(result, "rcInputMode must be standard_pwm, single_sbus, dual_sbus, or elrs",
                     ApplyRefusalReason::OutOfRange, "rcInputMode",
                     "standard_pwm,single_sbus,dual_sbus,elrs");
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
            setError(result, "soundMember is not a sound module this firmware can drive",
                     ApplyRefusalReason::OutOfRange, "soundMember");
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
            setError(result, "rcMember is not a radio this firmware lists",
                     ApplyRefusalReason::OutOfRange, "rcMember");
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
            setError(result, "fittedParts names a Part this build does not model",
                     ApplyRefusalReason::OutOfRange, "fittedParts");
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
            setError(result, "guidedSetupRun must be not-run, skipped or completed",
                     ApplyRefusalReason::OutOfRange, "guidedSetupRun", "not-run,skipped,completed");
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
                     "most 12 characters of a-z, 0-9 and _",
                     ApplyRefusalReason::OutOfRange, "guidedSetupVisited");
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
            setError(result, "guidedSetupSummaryDone must be true or false",
                     ApplyRefusalReason::OutOfRange, "guidedSetupSummaryDone", "true,false");
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
        static const char* const kMoveFields[] = {"movePart", "movePartFrom", "movePartTo"};
        const char* part = configParamGet(params, "movePart");
        ServoOutputPartMove move = {};
        const char* refused = firstMissing(params, kMoveFields,
                                           sizeof(kMoveFields) / sizeof(kMoveFields[0]));
        ApplyRefusalReason why = ApplyRefusalReason::MissingArgument;
        if (refused == nullptr) {
            why = ApplyRefusalReason::OutOfRange;
            if (part[0] == '\0' || strlen(part) > SERVO_OUTPUT_PART_ID_MAX ||
                !servoOutputPartIdIsValid(part)) {
                refused = "movePart";
            } else if (!parsePartMoveEnd(configParamGet(params, "movePartFrom"), &move.fromOutput,
                                         &move.fromDriver, &move.fromChannel)) {
                refused = "movePartFrom";
            } else if (!parsePartMoveEnd(configParamGet(params, "movePartTo"), &move.toOutput,
                                         &move.toDriver, &move.toChannel)) {
                refused = "movePartTo";
            }
        }
        if (refused != nullptr) {
            setError(result, "movePart, movePartFrom and movePartTo must be sent together: a "
                             "Part this build models, and each end an Output Address or none",
                     why, refused);
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
        appendApplied(&result->applied, "[CFG] sbusRecvCh2 updated to %s", boolValue ? "true" : "false");
        result->changed = true;
    } else if (configParamHas(params, "sbusRecvCh2")) {
        setError(result, "sbusRecvCh2 must be true/false or 1/0", ApplyRefusalReason::OutOfRange,
                 "sbusRecvCh2", kBoolAccepts);
        return;
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
            setError(result, err, ApplyRefusalReason::OutOfRange, boolFields[i].param, kBoolAccepts);
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
        appendApplied(&result->applied, "[CFG] domeEscNeutralUs updated to %u", (unsigned)domeU16);
        result->changed = true;
    } else if (configParamHas(params, "domeEscNeutralUs")) {
        setRangeError(result, "domeEscNeutralUs must be 1000..2000", "domeEscNeutralUs", 1000, 2000);
        return;
    }

    if (paramUint16(params, "domeEscMinPulseUs", 1000, 2000, &domeU16)) {
        working->dome.dome_min_pulse_us = domeU16;
        appendApplied(&result->applied, "[CFG] domeEscMinPulseUs updated to %u", (unsigned)domeU16);
        result->changed = true;
    } else if (configParamHas(params, "domeEscMinPulseUs")) {
        setRangeError(result, "domeEscMinPulseUs must be 1000..2000", "domeEscMinPulseUs", 1000, 2000);
        return;
    }

    if (paramUint16(params, "domeEscMaxPulseUs", 1000, 2000, &domeU16)) {
        working->dome.dome_max_pulse_us = domeU16;
        appendApplied(&result->applied, "[CFG] domeEscMaxPulseUs updated to %u", (unsigned)domeU16);
        result->changed = true;
    } else if (configParamHas(params, "domeEscMaxPulseUs")) {
        setRangeError(result, "domeEscMaxPulseUs must be 1000..2000", "domeEscMaxPulseUs", 1000, 2000);
        return;
    }

    // The three as a set, once each has passed on its own and been merged over
    // what is stored: a request naming one of them is judged with the other two
    // it will be stored beside. Out of order, speed 0 is not a stop
    // (include/dome_math.h domePulsesInOrder()), so the set is refused whole
    // rather than stored. Only when the request named one, so a POST about
    // something else is never refused over a set it did not touch.
    static const char* const kDomePulses[] = {"domeEscNeutralUs", "domeEscMinPulseUs",
                                              "domeEscMaxPulseUs"};
    const char* domePulseSent =
        firstSent(params, kDomePulses, sizeof(kDomePulses) / sizeof(kDomePulses[0]));
    if (domePulseSent != nullptr) {
        const DomeConfig& dome = working->dome;
        if (!domePulsesInOrder(dome.dome_min_pulse_us, dome.dome_neutral_us,
                               dome.dome_max_pulse_us)) {
            char err[192];
            snprintf(err, sizeof(err),
                     "domeEscMinPulseUs %u, domeEscNeutralUs %u, domeEscMaxPulseUs %u: "
                     "must be min <= neutral <= max",
                     (unsigned)dome.dome_min_pulse_us, (unsigned)dome.dome_neutral_us,
                     (unsigned)dome.dome_max_pulse_us);
            setError(result, err, ApplyRefusalReason::Conflict, domePulseSent);
            return;
        }
    }

    uint8_t domePct;
    if (paramUint8(params, "domeEscSpeedLimitPct", 0, 100, &domePct)) {
        working->dome.dome_speed_limit_pct = domePct;
        appendApplied(&result->applied, "[CFG] domeEscSpeedLimitPct updated to %u", (unsigned)domePct);
        result->changed = true;
    } else if (configParamHas(params, "domeEscSpeedLimitPct")) {
        setRangeError(result, "domeEscSpeedLimitPct must be 0..100", "domeEscSpeedLimitPct", 0, 100);
        return;
    }

    if (configParamHas(params, "protoR2linkWifiPeerIp")) {
        const char* rawPeerIp = configParamGet(params, "protoR2linkWifiPeerIp");
        if (!parseDomeWifiPeerIp(rawPeerIp, working->dome.dome_wifi_peer_ip,
                                 sizeof(working->dome.dome_wifi_peer_ip))) {
            setError(result, "protoR2linkWifiPeerIp must be empty or a valid IPv4 address",
                     ApplyRefusalReason::OutOfRange, "protoR2linkWifiPeerIp");
            return;
        }
        appendApplied(&result->applied, "[CFG] protoR2linkWifiPeerIp updated to %s",
                      working->dome.dome_wifi_peer_ip[0] != '\0' ? working->dome.dome_wifi_peer_ip
                                                                 : "(none)");
        result->changed = true;
    }

    bool domeRndEnableBool;
    if (paramBool(params, "domeEscRndEnable", &domeRndEnableBool)) {
        working->dome.dome_rnd_enable = domeRndEnableBool;
        appendApplied(&result->applied, "[CFG] domeEscRndEnable updated to %s",
                      domeRndEnableBool ? "true" : "false");
        result->changed = true;
    } else if (configParamHas(params, "domeEscRndEnable")) {
        setError(result, "domeEscRndEnable must be true/false or 1/0", ApplyRefusalReason::OutOfRange,
                 "domeEscRndEnable", kBoolAccepts);
        return;
    }

    uint8_t domeRndSpeedPct;
    if (paramUint8(params, "domeEscRndSpeedPct", 5, 100, &domeRndSpeedPct)) {
        working->dome.dome_rnd_speed_pct = domeRndSpeedPct;
        appendApplied(&result->applied, "[CFG] domeEscRndSpeedPct updated to %u", (unsigned)domeRndSpeedPct);
        result->changed = true;
    } else if (configParamHas(params, "domeEscRndSpeedPct")) {
        setRangeError(result, "domeEscRndSpeedPct must be 5..100", "domeEscRndSpeedPct", 5, 100);
        return;
    }

    uint8_t domeRndPauseMin;
    if (paramUint8(params, "domeEscRndPauseMin", 1, 120, &domeRndPauseMin)) {
        working->dome.dome_rnd_pause_min = domeRndPauseMin;
        appendApplied(&result->applied, "[CFG] domeEscRndPauseMin updated to %u", (unsigned)domeRndPauseMin);
        result->changed = true;
    } else if (configParamHas(params, "domeEscRndPauseMin")) {
        setRangeError(result, "domeEscRndPauseMin must be 1..120", "domeEscRndPauseMin", 1, 120);
        return;
    }

    uint8_t domeRndPauseMax;
    if (paramUint8(params, "domeEscRndPauseMax", 1, 120, &domeRndPauseMax)) {
        working->dome.dome_rnd_pause_max = domeRndPauseMax;
        appendApplied(&result->applied, "[CFG] domeEscRndPauseMax updated to %u", (unsigned)domeRndPauseMax);
        result->changed = true;
    } else if (configParamHas(params, "domeEscRndPauseMax")) {
        setRangeError(result, "domeEscRndPauseMax must be 1..120", "domeEscRndPauseMax", 1, 120);
        return;
    }

    uint16_t domeRndMoveMs;
    if (paramUint16(params, "domeEscRndMoveMs", 500, 10000, &domeRndMoveMs)) {
        working->dome.dome_rnd_move_ms = domeRndMoveMs;
        appendApplied(&result->applied, "[CFG] domeEscRndMoveMs updated to %u", (unsigned)domeRndMoveMs);
        result->changed = true;
    } else if (configParamHas(params, "domeEscRndMoveMs")) {
        setRangeError(result, "domeEscRndMoveMs must be 500..10000", "domeEscRndMoveMs", 500, 10000);
        return;
    }

    // The row door: an Output's settings, one row per Output (ADR 0068). The
    // only door onto them - pages, the Console and a restore all send rows -
    // beside the capture, reverse and Part-move acts below and above.
    if (!request.body.isNull() && !applyOutputRows(request.body, result)) {
        return;
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
        static const char* const kCaptureFields[] = {"captureOutput", "captureEnd", "captureUs"};
        static const char* const kCaptureRefusal =
            "captureOutput, captureEnd and captureUs must be sent together: an Output "
            "Address, one of open/centre/close, and a width 500..2500";
        const char* address = configParamGet(params, "captureOutput");
        const char* missing = firstMissing(params, kCaptureFields,
                                           sizeof(kCaptureFields) / sizeof(kCaptureFields[0]));
        if (missing != nullptr) {
            setError(result, kCaptureRefusal, ApplyRefusalReason::MissingArgument, missing);
            return;
        }
        if (!servoOutputParseAddress(address, &capture.driver, &capture.channel)) {
            setError(result, kCaptureRefusal, ApplyRefusalReason::OutOfRange, "captureOutput");
            return;
        }
        if (!servoParseOutputEnd(configParamGet(params, "captureEnd"), &end)) {
            setError(result, kCaptureRefusal, ApplyRefusalReason::OutOfRange, "captureEnd",
                     "open,centre,close");
            return;
        }
        if (!paramUint16(params, "captureUs", kServoPulseMinUs, kServoPulseMaxUs, &capturedUs)) {
            setRangeError(result, kCaptureRefusal, "captureUs", kServoPulseMinUs, kServoPulseMaxUs);
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
            setError(result, "reverseOutput must be an Output Address", ApplyRefusalReason::OutOfRange,
                     "reverseOutput");
            return;
        }
        reverse.kind = SERVO_EDIT_REVERSE;
        result->servoOutputs.edits[result->servoOutputs.count++] = reverse;
        appendApplied(&result->applied, "[CFG] reverse %s", address);
        result->changed = true;
    }

    if (!result->changed) {
        setError(result, "no supported config fields supplied", ApplyRefusalReason::MissingArgument,
                 nullptr);
        return;
    }

    working->drive.speedPresetActive = activePresetAfter;
    result->speedLimitStated = speedLimitMaxProvided || speedPresetValuesProvided;
    result->actions.playDomeOnCue = !domeEnabledBefore && working->system.enable_dome_esc;

    return;
}
