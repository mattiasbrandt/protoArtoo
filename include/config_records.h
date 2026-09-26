// =============================================================================
// include/config_records.h
//
// The Records, behind the one interface every Record shares (CONTEXT.md
// "Record"; ADR 0068, second amendment of 2026-09-26).
//
// A Record is values the droid stores that the builder, or guided Setup, states
// together and that only mean something together: the Droid Build, and guided
// Setup's record of its run. It is not a Setting. Its fields carry rules that
// span them - a design and its variant arrive together, the Fitted Parts arrive
// whole - so it cannot be one declaration line a field; it is one module that
// owns its fields end to end:
//
//   Fields - each field's form name, its GET path and an example value
//   Check  - the Apply Core half: read every field the request states, check
//            it (answering field, reason and accepts on a refusal) and stage it
//   Merge  - the Commit Step half: the stated fields onto the live copy, inside
//            the Write Window; a field the request did not state keeps its value
//   Read   - the live copy, whole
//   Answer - GET /api/config's object for the Record
//   Save   - the live copy onto its NVS keys, which never change
//   Load   - its NVS keys into the live copy on the boot path, saying what a
//            stored value this image cannot name cost
//
// The Apply Core, the Commit Step, GET and the NVS save and load loop over the
// list (include/config_records.inc) through the dispatchers below; none of them
// names a Record, and a Record is saved only when a request stated one of its
// fields, because an absent record is itself an answer (include/config_store.h
// "ConfigSaveExtras").
//
// The dispatchers switch over the list and call each Record's functions
// directly, rather than through a table of pointers: the task stack walk
// (tools/check_task_stack_chains.py) follows a direct call on its own, and a
// Record's save sits on the Console's deepest config-write chain.
//
// Each Record's live copy is its module's own, under its own lock, and is
// loaded straight into place on the boot path so no Record's value becomes a
// frame on loopTask's measured stack.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <ArduinoJson.h>

#include "api_apply_refusal.h"
#include "api_param_source.h"
#include "config_io.h"
#include "droid_build.h"
#include "guided_setup.h"

struct ConfigAppliedFields;  // include/api_config_apply.h

// One field of a Record.
struct ConfigRecordField {
    // What POST /api/config takes it under, and a refusal's field.
    const char* form;
    // Where GET /api/config has it, dotted. A list is a JSON array there and the
    // comma-joined text on a form.
    const char* path;
    // A value the field's check takes that a fresh controller does not hold, as
    // a form carries it - a list in the order GET answers it. The generated
    // round trips state every field's example (test_api_config_write), so a
    // field added here is proved through GET, POST and NVS with no test edit.
    const char* example;
};

enum class ConfigRecordId : uint8_t {
#define PA_CONFIG_RECORD(Name, Value, key) Name,
#include "config_records.inc"
#undef PA_CONFIG_RECORD
    Count,
};

constexpr size_t CONFIG_RECORD_COUNT = (size_t)ConfigRecordId::Count;
// A Record's stated fields are a bit mask, and so is which Records a save writes.
static_assert(CONFIG_RECORD_COUNT <= 32, "a save names the Records it writes in 32 bits");

// What one request stated of every Record: each Record's staged values, and a
// bit per field it named (bit i is the Record's field i). A Record with no bit
// set is one the request said nothing about. Held in the Apply Core's result,
// which is never a stack local (include/api_config_apply.h).
struct ConfigRecordEdits {
    uint32_t stated[CONFIG_RECORD_COUNT] = {};
#define PA_CONFIG_RECORD(Name, Value, key) Value Name = {};
#include "config_records.inc"
#undef PA_CONFIG_RECORD
};

// What a Record's check writes to, and reads from.
struct ConfigRecordCheck {
    const ConfigParamSource& params;
    // A refusal: field, reason and accepts as data, and the sentence for the log
    // and HTTP's `error`. The sentence never echoes what was asked for - it
    // lands in a JSON error body - which is also a Member Setting's rule.
    ApplyRefusal* refusal;
    char* sentence;
    size_t sentenceSize;
    // A line per field the request stated, replayed by the Commit Step.
    ConfigAppliedFields* applied;
};

inline void configRecordRefuse(const ConfigRecordCheck& check, const char* sentence,
                               ApplyRefusalReason reason, const char* field,
                               const char* accepts = nullptr) {
    snprintf(check.sentence, check.sentenceSize, "%s", sentence);
    applyRefusalSet(check.refusal, reason, field, accepts);
}

// One applied-fields line (defined in src/web/api_config_apply.cpp, beside the
// record it writes to).
void configRecordLog(const ConfigRecordCheck& check, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

// -----------------------------------------------------------------------------
// The interface each Record's module defines
// -----------------------------------------------------------------------------
#define PA_CONFIG_RECORD(Name, Value, key)                                                        \
    const ConfigRecordField* configRecord##Name##Fields(size_t* count);                          \
    bool configRecord##Name##Check(const ConfigRecordCheck& check, Value* staged,                 \
                                   uint32_t* stated);                                             \
    void configRecord##Name##Merge(const Value& staged, uint32_t stated);                         \
    void configRecord##Name##Read(Value* out);                                                    \
    void configRecord##Name##Answer(JsonObject out);                                              \
    bool configRecord##Name##Save(ConfigWriter& writer);                                          \
    bool configRecord##Name##Load(const ConfigReader& reader, char* repaired, size_t repairedSize);
#include "config_records.inc"
#undef PA_CONFIG_RECORD

// -----------------------------------------------------------------------------
// The loops' dispatchers (src/config_records.cpp)
// -----------------------------------------------------------------------------
const char* configRecordKey(ConfigRecordId id);
const ConfigRecordField* configRecordFields(ConfigRecordId id, size_t* count);

// The field of any Record POST takes under `form`, or nullptr.
const ConfigRecordField* configRecordFieldByForm(const char* form);

// The Record's check onto its staged values in `edits`. False when it refused,
// with the refusal written.
bool configRecordCheck(ConfigRecordId id, const ConfigRecordCheck& check, ConfigRecordEdits* edits);

// The Record's stated fields onto its live copy; nothing when it has none.
// Inside the config Write Window.
void configRecordMerge(ConfigRecordId id, const ConfigRecordEdits& edits);

void configRecordAnswer(ConfigRecordId id, JsonObject out);

// Every Record `records` names (bit i is ConfigRecordId i), in list order,
// stopping at the first that does not land. Inside the config Write Window,
// which it checks; a Record's own save does not.
bool configRecordsSave(uint32_t records, ConfigWriter& writer);

// True when the load repaired something, with a sentence in `repaired`.
bool configRecordLoad(ConfigRecordId id, const ConfigReader& reader, char* repaired,
                      size_t repairedSize);

// Which Records `edits` stated anything of: bit i is ConfigRecordId i. What a
// save writes (ConfigSaveExtras::records).
uint32_t configRecordsStated(const ConfigRecordEdits& edits);
