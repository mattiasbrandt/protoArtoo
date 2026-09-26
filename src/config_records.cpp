// =============================================================================
// src/config_records.cpp
//
// The loops' dispatchers over the Records (include/config_records.h). Each is
// a switch generated from the list, include/config_records.inc, that calls the
// Record's own function directly; no function here knows what a Record holds.
// =============================================================================

#include "config_records.h"

#include <string.h>

#include "config_write_window_check.h"  // a Record's save is a config write, inside its Write Window

#define PA_RECORD_CASE(Name, call) \
    case ConfigRecordId::Name:     \
        return call;

const char* configRecordKey(ConfigRecordId id) {
    switch (id) {
#define PA_CONFIG_RECORD(Name, Value, key) PA_RECORD_CASE(Name, key)
#include "config_records.inc"
#undef PA_CONFIG_RECORD
        case ConfigRecordId::Count:
        default:
            return "";
    }
}

const ConfigRecordField* configRecordFields(ConfigRecordId id, size_t* count) {
    switch (id) {
#define PA_CONFIG_RECORD(Name, Value, key) PA_RECORD_CASE(Name, configRecord##Name##Fields(count))
#include "config_records.inc"
#undef PA_CONFIG_RECORD
        case ConfigRecordId::Count:
        default:
            *count = 0;
            return nullptr;
    }
}

const ConfigRecordField* configRecordFieldByForm(const char* form) {
    if (form == nullptr) {
        return nullptr;
    }
    for (size_t r = 0; r < CONFIG_RECORD_COUNT; ++r) {
        size_t count = 0;
        const ConfigRecordField* fields = configRecordFields((ConfigRecordId)r, &count);
        for (size_t f = 0; f < count; ++f) {
            if (strcmp(fields[f].form, form) == 0) {
                return &fields[f];
            }
        }
    }
    return nullptr;
}

bool configRecordCheck(ConfigRecordId id, const ConfigRecordCheck& check, ConfigRecordEdits* edits) {
    uint32_t* stated = &edits->stated[(size_t)id];
    switch (id) {
#define PA_CONFIG_RECORD(Name, Value, key) \
    PA_RECORD_CASE(Name, configRecord##Name##Check(check, &edits->Name, stated))
#include "config_records.inc"
#undef PA_CONFIG_RECORD
        case ConfigRecordId::Count:
        default:
            return true;
    }
}

void configRecordMerge(ConfigRecordId id, const ConfigRecordEdits& edits) {
    const uint32_t stated = edits.stated[(size_t)id];
    if (stated == 0) {
        return;
    }
    switch (id) {
#define PA_CONFIG_RECORD(Name, Value, key) \
    PA_RECORD_CASE(Name, configRecord##Name##Merge(edits.Name, stated))
#include "config_records.inc"
#undef PA_CONFIG_RECORD
        case ConfigRecordId::Count:
        default:
            return;
    }
}

void configRecordAnswer(ConfigRecordId id, JsonObject out) {
    switch (id) {
#define PA_CONFIG_RECORD(Name, Value, key) PA_RECORD_CASE(Name, configRecord##Name##Answer(out))
#include "config_records.inc"
#undef PA_CONFIG_RECORD
        case ConfigRecordId::Count:
        default:
            return;
    }
}

bool configRecordLoad(ConfigRecordId id, const ConfigReader& reader, char* repaired,
                      size_t repairedSize) {
    switch (id) {
#define PA_CONFIG_RECORD(Name, Value, key) \
    PA_RECORD_CASE(Name, configRecord##Name##Load(reader, repaired, repairedSize))
#include "config_records.inc"
#undef PA_CONFIG_RECORD
        case ConfigRecordId::Count:
        default:
            return false;
    }
}

// The loop lives here rather than in the save that asks for it, and calls each
// Record's save from its own switch rather than through a second dispatcher:
// the artoo-esp32 listing misframes a loop body that follows alignment padding,
// so a call hidden that way drops out of the task stack walk (#401), and this
// sits on the Console's deepest config-write chain, where every frame counts
// (include/config.h). configPersist() makes one straight call to this.
//
// The Write Window is checked here, once, rather than in each Record's save:
// the check's log line is the deepest thing a save reaches, and from here it
// sits under this small frame instead of under a Record's copy of its values.
bool configRecordsSave(uint32_t records, ConfigWriter& writer) {
    configWriteWindowExpectHeld("configRecordsSave");
    bool ok = true;
    for (size_t r = 0; ok && r < CONFIG_RECORD_COUNT; ++r) {
        if ((records & ((uint32_t)1u << r)) == 0) {
            continue;
        }
        switch ((ConfigRecordId)r) {
#define PA_CONFIG_RECORD(Name, Value, key)          \
    case ConfigRecordId::Name:                      \
        ok = configRecord##Name##Save(writer);      \
        break;
#include "config_records.inc"
#undef PA_CONFIG_RECORD
            case ConfigRecordId::Count:
            default:
                ok = false;
                break;
        }
    }
    return ok;
}

uint32_t configRecordsStated(const ConfigRecordEdits& edits) {
    uint32_t records = 0;
    for (size_t r = 0; r < CONFIG_RECORD_COUNT; ++r) {
        if (edits.stated[r] != 0) {
            records |= (uint32_t)1u << r;
        }
    }
    return records;
}

#undef PA_RECORD_CASE
