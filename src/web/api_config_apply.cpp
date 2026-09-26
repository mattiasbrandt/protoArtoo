// =============================================================================
// src/web/api_config_apply.cpp
//
// Apply Core for POST /api/config (ADR 0011). See api_config_apply.h.
// =============================================================================

#include "api_config_apply.h"

#include <ArduinoJson.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api_helpers.h"
#include "board_outputs.h"
#include "config.h"
#include "config_settings.h"  // every Setting's declaration - the scalar half loops over them
#include "dome_math.h"  // domePulsesInOrder() - the one order rule for the ESC pulse set
#include "drive_speed_preset.h"
#include "ledc_pwm.h"  // SERVO_PULSE_MIN_US/MAX_US - what any servo takes
#include "servo_component_helpers.h"

namespace {

void appendAppliedV(ConfigAppliedFields* applied, const char* fmt, va_list args) {
    if (applied->count >= ConfigAppliedFields::kMaxLines) {
        applied->dropped++;
        return;
    }
    vsnprintf(applied->lines[applied->count], sizeof(applied->lines[0]), fmt, args);
    applied->count++;
}

void appendApplied(ConfigAppliedFields* applied, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    appendAppliedV(applied, fmt, args);
    va_end(args);
}

}  // namespace

// A Record's applied-fields line, into the record the Commit Step replays
// (include/config_records.h).
void configRecordLog(const ConfigRecordCheck& check, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    appendAppliedV(check.applied, fmt, args);
    va_end(args);
}

namespace {

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

// -----------------------------------------------------------------------------
// The Configuration in the shape GET /api/config reads it (ADR 0068, #423)
//
// A JSON body is the GET shape, so a restore posts back what a backup holds and
// nothing in the browser flattens it first. Every droid Setting says where GET
// puts it (include/config_settings.h), beside the form name the pages and the
// Controller Console send it under. Both doors reach the SAME check: a value
// found at a path is answered under its form name, so a refusal reads the same
// whichever door the value came in by.
//
// The Records - the Droid Build and guided Setup's record - travel with a
// backup too (operator, 2026-09-17 on #371). Each of their fields says where
// GET puts it (include/config_records.h), and its lists are JSON arrays on GET,
// read as the comma-joined list the form takes.
//
// A key GET carries that neither names is a reading, not a setting - `wifi`,
// `activeToggles`, `drive.speedPreset`, a label - and is ignored rather than
// refused, so a whole GET answer can be posted back as it stands.
// -----------------------------------------------------------------------------

// The GET path a form name is read at, or nullptr for one GET does not carry.
const char* getPathOf(const char* param) {
    const ConfigSetting* setting = configSettingByForm(param);
    if (setting != nullptr) {
        return setting->path;
    }
    const ConfigRecordField* field = configRecordFieldByForm(param);
    return field != nullptr ? field->path : nullptr;
}

// What a JSON value that no form field could ever hold reads as: an object
// where a number belongs, or a list with something other than words in it. It
// is a value no check below takes, so the field is refused with its own
// sentence rather than dropped - an object sent as a peer IP must not read as
// the empty string that clears it.
constexpr const char kNotAFieldValue[] = "(not a value)";

// One key of a dotted path, copied out so it can be looked up. A key longer
// than any GET key names nothing.
constexpr size_t kPathKeyMax = 24;

// The next key of `dotted` from `*cursor` into `key`, advancing the cursor
// past it. False at the end of the path.
bool nextPathKey(const char** cursor, char* key) {
    const char* at = *cursor;
    if (at == nullptr || *at == '\0') {
        return false;
    }
    const char* dot = strchr(at, '.');
    const size_t span = dot != nullptr ? (size_t)(dot - at) : strlen(at);
    const size_t kept = span < kPathKeyMax ? span : kPathKeyMax - 1;
    memcpy(key, at, kept);
    key[kept] = '\0';
    *cursor = dot != nullptr ? dot + 1 : at + span;
    return true;
}

// The leaf a dotted path names, or a null variant when any key on the way is
// absent. Read-only: a lookup never adds a member to the body.
JsonVariantConst getShapeLeaf(JsonObjectConst body, const char* dotted) {
    JsonVariantConst at = body;
    char key[kPathKeyMax];
    const char* cursor = dotted;
    while (nextPathKey(&cursor, key)) {
        at = at[static_cast<const char*>(key)];
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
bool normaliseGetShapeField(JsonDocument& body, const char* dotted) {
    JsonObject parent = body.as<JsonObject>();
    char key[kPathKeyMax];
    const char* cursor = dotted;
    if (!nextPathKey(&cursor, key)) {
        return true;
    }
    while (*cursor != '\0') {
        parent = parent[static_cast<const char*>(key)].as<JsonObject>();
        if (parent.isNull()) {
            return true;
        }
        nextPathKey(&cursor, key);
    }
    if (parent.isNull() || !parent[static_cast<const char*>(key)].is<JsonVariantConst>()) {
        return true;
    }
    return normaliseLeaf(parent[static_cast<const char*>(key)].as<JsonVariant>());
}

// -----------------------------------------------------------------------------
// The Output rows (ADR 0068, #423)
//
// A JSON body's `outputs` is the row door: one row per Output, keyed by its
// Output Address, in the shape GET /api/servo/outputs reads it. Every key a row
// can set is an Output row Setting (include/config_settings.h) and is read as
// text like any other field; the rest of a row - its name, its band, where it
// has been told to be - is a reading and is ignored, so a row read by GET can be
// posted back as it stands.
//
// A form can carry one row too: `outputRow` names its Output Address and the
// row's keys ride beside it by name. That is how the Controller Console writes
// one Setting of an Output without building a body.
// -----------------------------------------------------------------------------
constexpr const char kRowAddressKey[] = "address";
constexpr const char kFormRowParam[] = "outputRow";

// The text of one key of a JSON row, as configRequestGet() answers a field:
// nullptr when absent, the text when it is one, kNotAFieldValue otherwise.
const char* rowText(JsonObjectConst row, const char* key) {
    JsonVariantConst value = row[key];
    if (value.isNull()) {
        return nullptr;
    }
    return value.is<const char*>() ? value.as<const char*>() : kNotAFieldValue;
}

// A row, whichever door it came by: its keys, read as text.
struct RowSource {
    const void* ctx;
    const char* (*get)(const void* ctx, const char* key);
};

const char* jsonRowGet(const void* ctx, const char* key) {
    return rowText(*static_cast<const JsonObjectConst*>(ctx), key);
}

const char* formRowGet(const void* ctx, const char* key) {
    return configParamGet(*static_cast<const ConfigParamSource*>(ctx), key);
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
    const char* path = getPathOf(name);
    if (path == nullptr) {
        return nullptr;
    }
    JsonVariantConst leaf = getShapeLeaf(request->body, path);
    if (leaf.isNull()) {
        return nullptr;
    }
    return leaf.is<const char*>() ? leaf.as<const char*>() : kNotAFieldValue;
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
    for (size_t i = 0; i < configSettingCount(); ++i) {
        const ConfigSetting& setting = configSettingAt(i);
        if (setting.path != nullptr) {
            whole = normaliseGetShapeField(*body, setting.path) && whole;
        }
    }
    for (size_t r = 0; r < CONFIG_RECORD_COUNT; ++r) {
        size_t count = 0;
        const ConfigRecordField* fields = configRecordFields((ConfigRecordId)r, &count);
        for (size_t f = 0; f < count; ++f) {
            whole = normaliseGetShapeField(*body, fields[f].path) && whole;
        }
    }
    // A row's keys the same way, in place. Its Part list becomes the
    // comma-joined list a form row carries, so both doors read one form of it.
    for (JsonVariant item : (*body)["outputs"].as<JsonArray>()) {
        JsonObject row = item.as<JsonObject>();
        if (row.isNull()) {
            continue;
        }
        if (row[kRowAddressKey].is<JsonVariantConst>()) {
            whole = normaliseLeaf(row[kRowAddressKey].as<JsonVariant>()) && whole;
        }
        for (size_t i = 0; i < outputRowSettingCount(); ++i) {
            const char* key = outputRowSettingAt(i).key;
            if (row[key].is<JsonVariantConst>()) {
                whole = normaliseLeaf(row[key].as<JsonVariant>()) && whole;
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
// Each row is one Output, named by its Output Address, and a Setting it carries
// is set. A refusal about a row's Setting names both, `<address>.<key>`
// (`ledc:4.ledCount`), with the canonical address - at most 8 characters, so
// the name always fits a refusal's field. What each Setting takes is its
// declaration's (include/config_settings.h); the ends and the centre are then
// clamped into the fitted component's band by the Commit Step, not refused.
// -----------------------------------------------------------------------------
void rowField(const char* address, const char* key, char* field) {
    snprintf(field, APPLY_REFUSAL_FIELD_MAX, "%s.%s", address, key);
}

void setRowError(ConfigApplyResult* result, const char* address, const char* key,
                 const char* says, ApplyRefusalReason reason, const char* accepts = nullptr) {
    char field[APPLY_REFUSAL_FIELD_MAX];
    rowField(address, key, field);
    char message[sizeof(ConfigApplyError::message)];
    snprintf(message, sizeof(message), "%s %s", field, says);
    setError(result, message, reason, field, accepts);
}

// One row's Part list, as stated: comma-joined Part ids, at most the parts
// Setting's `hi` of them, each one this build models. `stated` is every Part an
// earlier row in this request named; naming one of those again puts a Part on
// two Outputs, which the glossary's Part forbids, and is refused `conflict`.
bool readRowParts(const OutputRowSetting& setting, const char* text, const char* address,
                  uint32_t* stated, ServoOutputEdit* edit, ConfigApplyResult* result) {
    edit->partCount = 0;
    const char* cursor = text;
    while (*cursor != '\0') {
        const char* comma = strchr(cursor, ',');
        const size_t span = comma != nullptr ? (size_t)(comma - cursor) : strlen(cursor);
        char id[SERVO_OUTPUT_PART_ID_MAX + 1] = {};
        const bool fits = span > 0 && span <= SERVO_OUTPUT_PART_ID_MAX;
        if (fits) {
            memcpy(id, cursor, span);
        }
        if (!fits || !servoOutputPartIdIsValid(id)) {
            setRowError(result, address, setting.key, "names a Part this build does not model",
                        ApplyRefusalReason::OutOfRange);
            return false;
        }
        const uint8_t index = (uint8_t)droidPartIndexOf(id);
        bool onThisRow = false;
        for (uint8_t i = 0; i < edit->partCount; ++i) {
            onThisRow = onThisRow || edit->parts[i] == index;
        }
        if (!onThisRow) {  // the same Part twice on one wire is the same wire
            if (edit->partCount >= setting.hi) {
                char field[APPLY_REFUSAL_FIELD_MAX];
                rowField(address, setting.key, field);
                char message[sizeof(ConfigApplyError::message)];
                snprintf(message, sizeof(message), "%s must be %ld..%ld Parts", field,
                         (long)setting.lo, (long)setting.hi);
                setRangeError(result, message, field, setting.lo, setting.hi);
                return false;
            }
            const uint32_t bit = (uint32_t)1u << (index % 32);
            if ((stated[index / 32] & bit) != 0) {
                setRowError(result, address, setting.key,
                            "names a Part another row names too: a Part is on at most one Output",
                            ApplyRefusalReason::Conflict);
                return false;
            }
            stated[index / 32] |= bit;
            edit->parts[edit->partCount++] = index;
        }
        cursor = comma != nullptr ? comma + 1 : cursor + span;
    }
    edit->fields |= setting.fieldBit;
    return true;
}

// One row of the row door, onto the edit for its address and, for its wired
// tick, onto `working`. False, with the refusal set, on the first Setting it
// cannot take. `*ticked` says whether it set a wired tick.
bool readOutputRow(const RowSource& row, const char* address, const BoardOutput* board,
                   uint32_t* stated, ServoOutputEdit* edit, ConfigSnapshot* working,
                   bool* ticked, ConfigApplyResult* result) {
    for (size_t i = 0; i < outputRowSettingCount(); ++i) {
        const OutputRowSetting& setting = outputRowSettingAt(i);
        const char* raw = row.get(row.ctx, setting.key);
        if (raw == nullptr) {
            continue;
        }
        if (!outputRowSettingIsOn(setting, board)) {
            setRowError(result, address, setting.key, "cannot be set: no light can go on this Output",
                        ApplyRefusalReason::OutOfRange);
            return false;
        }
        if (setting.store == RowSettingStore::Parts) {
            if (!readRowParts(setting, raw, address, stated, edit, result)) {
                return false;
            }
            continue;
        }

        char field[APPLY_REFUSAL_FIELD_MAX];
        rowField(address, setting.key, field);
        int32_t value = 0;
        if (!outputRowSettingParse(setting, raw, field, &value, &result->error.refusal,
                                   result->error.message, sizeof(result->error.message))) {
            result->error.hasError = true;
            return false;
        }

        if (setting.store == RowSettingStore::Row) {
            outputRowSettingSetOnEdit(setting, value, edit);
            continue;
        }
        // The wired tick. An Output with one has it stored as the droid Setting
        // its board Output names; one with none - an expander's - is always
        // wired, and saying otherwise is refused.
        const ConfigSetting* tick = board != nullptr ? configSettingByForm(board->enabledField) : nullptr;
        if (tick == nullptr) {
            if (value == 0) {
                setRowError(result, address, setting.key,
                            "is always true: this Output has no wired tick",
                            ApplyRefusalReason::OutOfRange, "true");
                return false;
            }
            continue;
        }
        configSettingSetNumber(*tick, working, value);
        *ticked = true;
    }
    return true;
}

// One addressed row read whole onto the request's edit list. `stated` and the
// seen-address list span the whole request.
bool applyOneRow(const RowSource& row, const char* rawAddress, uint32_t* stated, uint16_t* seen,
                 size_t* seenCount, ConfigSnapshot* working, ConfigApplyResult* result) {
    ServoOutputDriver driver = SERVO_DRIVER_LEDC;
    uint8_t channel = 0;
    if (rawAddress == nullptr) {
        setError(result, "every Output row must carry its address",
                 ApplyRefusalReason::MissingArgument, kRowAddressKey);
        return false;
    }
    if (!servoOutputParseAddress(rawAddress, &driver, &channel)) {
        setError(result, "an Output row's address must be an Output Address, such as ledc:3",
                 ApplyRefusalReason::OutOfRange, kRowAddressKey);
        return false;
    }
    char address[SERVO_OUTPUT_ADDRESS_STR_MAX + 1] = {};
    servoOutputFormatAddress(address, sizeof(address), driver, channel);

    const uint16_t key = (uint16_t)(((uint16_t)driver << 8) | channel);
    for (size_t i = 0; i < *seenCount; ++i) {
        if (seen[i] == key) {
            setRowError(result, address, kRowAddressKey, "is named by two rows: one row per Output",
                        ApplyRefusalReason::Conflict);
            return false;
        }
    }
    // The body's rows and a form's one together are still one row per Output,
    // which is what the edit list is sized for.
    if (*seenCount >= SERVO_OUTPUT_ROW_MAX) {
        setRangeError(result, "outputs holds at most one row per Output", "outputs", 0,
                      SERVO_OUTPUT_ROW_MAX);
        return false;
    }
    seen[(*seenCount)++] = key;

    // Read whole before it joins the list, so a refused row leaves nothing of
    // itself behind. One row per address (checked above), so a row is one
    // typed edit and there is nothing to merge.
    ServoOutputEdit edit = {};
    edit.driver = driver;
    edit.channel = channel;
    const BoardOutput* board = driver == SERVO_DRIVER_LEDC ? boardOutputOnChannel(channel) : nullptr;
    bool ticked = false;
    if (!readOutputRow(row, address, board, stated, &edit, working, &ticked, result)) {
        return false;
    }
    if (edit.fields == 0 && !ticked) {
        return true;
    }
    if (edit.fields != 0) {
        // Rows number at most SERVO_OUTPUT_ROW_MAX (checked above), which the
        // list holds with room for a capture and a reverse.
        result->servoOutputs.edits[result->servoOutputs.count++] = edit;
    }
    appendApplied(&result->applied, "[CFG] output %s updated", address);
    result->changed = true;
    return true;
}

// The whole row set: at most one row per Output, at most one Output per Part.
// Every row is read and checked before any lands - the Commit Step applies the
// edits only when this and every other field in the request have passed, so a
// refused row leaves the scalars beside it unwritten too (ADR 0068, one Write
// Window).
bool applyOutputRows(JsonObjectConst body, const ConfigParamSource& form, ConfigSnapshot* working,
                     ConfigApplyResult* result) {
    uint32_t statedParts[(DROID_PART_COUNT + 31) / 32] = {};
    uint16_t seen[SERVO_OUTPUT_ROW_MAX] = {};
    size_t seenCount = 0;

    JsonVariantConst value = body.isNull() ? JsonVariantConst() : body["outputs"];
    if (!value.isNull()) {
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
        for (JsonVariantConst item : rows) {
            JsonObjectConst row = item.as<JsonObjectConst>();
            const RowSource source{&row, jsonRowGet};
            if (!applyOneRow(source, row.isNull() ? nullptr : rowText(row, kRowAddressKey),
                             statedParts, seen, &seenCount, working, result)) {
                return false;
            }
        }
    }

    // A form's one row, after the body's: a request naming the same Output both
    // ways is refused as two rows for one Output.
    if (configParamHas(form, kFormRowParam)) {
        const RowSource source{&form, formRowGet};
        if (!applyOneRow(source, configParamGet(form, kFormRowParam), statedParts, seen, &seenCount,
                         working, result)) {
            return false;
        }
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

    const SpeedPresetId activePresetBefore =
        normalizeSpeedPresetId((uint8_t)working->drive.speedPresetActive);
    SpeedPresetId activePresetAfter = activePresetBefore;

    // Every droid Setting the request named, each through its one declaration
    // (include/config_settings.h): its check, its words, its refusal. Nothing
    // below names a Setting by hand except to judge a rule that spans several.
    for (size_t i = 0; i < configSettingCount(); ++i) {
        const ConfigSetting& setting = configSettingAt(i);
        const char* raw = configParamGet(params, setting.form);
        if (raw == nullptr) {
            continue;
        }
        if (!configSettingApply(setting, raw, working, &result->error.refusal,
                                result->error.message, sizeof(result->error.message))) {
            result->error.hasError = true;
            return;
        }
        char text[24] = {};
        configSettingFormat(setting, *working, text, sizeof(text));
        appendApplied(&result->applied, "[CFG] %s updated to %s", setting.form,
                      text[0] != '\0' ? text : "(none)");
        result->changed = true;
    }

    // The three speed presets must differ, judged with the values they will be
    // stored beside. speedLimitMax names the active preset; presets alone
    // re-derive it from the one that was active.
    static const char* const kPresets[] = {"speedPresetSlow", "speedPresetNormal",
                                           "speedPresetTurbo"};
    const bool speedPresetValuesProvided =
        firstSent(params, kPresets, sizeof(kPresets) / sizeof(kPresets[0])) != nullptr;
    const bool speedLimitMaxProvided = configParamHas(params, "speedLimitMax");
    if (speedPresetValuesProvided &&
        !speedPresetValuesAreUnique(working->drive.speedPresetSlow, working->drive.speedPresetNormal,
                                     working->drive.speedPresetTurbo)) {
        // Named on a preset the request sent that shares its number with
        // another, so a page names the one that clashes - not the slow preset
        // because it happens to be read first.
        const int16_t values[] = {working->drive.speedPresetSlow, working->drive.speedPresetNormal,
                                  working->drive.speedPresetTurbo};
        const char* clashing = nullptr;
        for (size_t i = 0; i < 3 && clashing == nullptr; ++i) {
            for (size_t j = 0; j < 3; ++j) {
                if (i != j && values[i] == values[j] && configParamHas(params, kPresets[i])) {
                    clashing = kPresets[i];
                    break;
                }
            }
        }
        // Two stored presets can clash while the request sent only the third:
        // the refusal still names what the request sent.
        if (clashing == nullptr) {
            clashing = firstSent(params, kPresets, sizeof(kPresets) / sizeof(kPresets[0]));
        }
        setError(result, "speed presets must be distinct values", ApplyRefusalReason::Conflict,
                 clashing);
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

    // The stationary release cue stays in the shell (ADR 0012); the shell also
    // needs to know whether this request is the one deciding the mode.
    result->stationaryStated = configParamHas(params, "stationary");

    // The three dome pulses as a set, once each has passed on its own and been
    // merged over what is stored: a request naming one of them is judged with
    // the other two it will be stored beside. Out of order, speed 0 is not a
    // stop (include/dome_math.h domePulsesInOrder()), so the set is refused
    // whole rather than stored. Only when the request named one, so a POST
    // about something else is never refused over a set it did not touch.
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

    // The idle turn's pauses as a pair: the shortest may not exceed the
    // longest. Judged with the value stored beside the one sent, and only when
    // the request named one, as the pulses are. The Dome page held this rule
    // until the page-side checks went (#431); the droid holds it now.
    static const char* const kDomePauses[] = {"domeEscRndPauseMin", "domeEscRndPauseMax"};
    const char* domePauseSent =
        firstSent(params, kDomePauses, sizeof(kDomePauses) / sizeof(kDomePauses[0]));
    if (domePauseSent != nullptr &&
        working->dome.dome_rnd_pause_min > working->dome.dome_rnd_pause_max) {
        setError(result, "domeEscRndPauseMin must be at most domeEscRndPauseMax",
                 ApplyRefusalReason::Conflict, domePauseSent);
        return;
    }

    // The Records (include/config_records.h): the Droid Build and guided
    // Setup's record, each checked by its own module, which stages what the
    // request stated for the Commit Step to merge.
    for (size_t r = 0; r < CONFIG_RECORD_COUNT; ++r) {
        const ConfigRecordCheck check{params, &result->error.refusal, result->error.message,
                                      sizeof(result->error.message), &result->applied};
        if (!configRecordCheck((ConfigRecordId)r, check, &result->records)) {
            result->error.hasError = true;
            return;
        }
        if (result->records.stated[r] != 0) {
            result->changed = true;
        }
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

    // The row door: an Output's Settings, one row per Output (ADR 0068). The
    // only door onto them - pages and a restore send rows in a body, the Console
    // one row on its form - beside the capture, reverse and Part-move acts below
    // and above.
    if (!applyOutputRows(request.body, form, working, result)) {
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
        // The widest a servo takes, as for a typed end: the fitted component's
        // band is applied on the row. A literal, not a buffer - this frame is on
        // the Console chain - and pinned to the constants it states.
        static_assert(SERVO_PULSE_MIN_US == 500 && SERVO_PULSE_MAX_US == 2500,
                      "the capture refusal's sentence states SERVO_PULSE_MIN_US..MAX_US");
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
        if (!paramUint16(params, "captureUs", SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US, &capturedUs)) {
            setRangeError(result, kCaptureRefusal, "captureUs", SERVO_PULSE_MIN_US,
                          SERVO_PULSE_MAX_US);
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
}
