// =============================================================================
// include/config_settings.h
//
// Each Setting, declared once (ADR 0068, amended 2026-09-26; CONTEXT.md
// "Setting").
//
// A Setting is one value the droid stores that a builder can change. It
// belongs to the droid or to one Output, and every door that reads or writes
// it goes through the declaration below rather than naming it by hand:
//
//   - GET /api/config writes each droid Setting at its GET path
//     (populateConfigJson(), src/web/api_config.cpp), and GET
//     /api/servo/outputs writes each Output row Setting under its row key;
//   - POST /api/config reads each one under its form name or at its GET path,
//     and checks it here (configApply(), src/web/api_config_apply.cpp);
//   - the NVS save and load read and write each droid Setting under its key,
//     and the defaults come from here (src/config_serializer.cpp,
//     configSnapshotDefaults());
//   - the Controller Console's single-field ops read and write by form name
//     (src/console/console_module.cpp).
//
// What a Setting accepts is decided here, once: a range, or a list of words,
// or a Component Registry family, or an IPv4 address. A refusal carries the
// field, the reason and what the Setting accepts as data (#425), identically at
// every door, and a Setting that takes words takes the same words at every
// door. The builder's words for each Setting live in the browser
// (data/setting_words.js), never here; tools/check_setting_words.py fails when a
// Setting declared here has none.
//
// Rules that span Settings - three speed presets that must differ, the dome
// pulses kept in order, the ends clamped into a component's band - stay
// hand-written beside the loops that read these, not inside one declaration.
//
// The declarations are const data in flash. Nothing here allocates; the
// Console, the web server and the boot path may all call it.
//
// Defined in src/config_settings.cpp.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "api_apply_refusal.h"
#include "config_io.h"
#include "config_store.h"
#include "servo_output_row.h"

// How a Setting's value is held in its struct. Worked out from the member's
// own type (settingStorageOf() below), so a declaration cannot disagree with
// the field it names.
enum class SettingStorage : uint8_t { Bool, U8, U16, I16, U32, Text };

// What a Setting accepts.
//   Range  - a whole number lo..hi. With words, a word stands for its number
//            (the log level's `debug` is 4) and GET still reads the number.
//   Words  - one of its words, stored as the word's number; GET reads the word.
//   Bool   - true/false or 1/0.
//   Member - a Component Registry id of one family that this image can drive;
//            stored as the part's number, read as its id.
//   Ipv4   - empty, or a dotted-quad IPv4 address.
enum class SettingRule : uint8_t { Range, Words, Bool, Member, Ipv4 };

// A word list: the words for values first .. first + count - 1, each named by
// `nameOf`. The name function is the vocabulary's one home (the RC receiver
// modes' is rcInputModeName(), include/robot_state.h), so the words a Setting
// accepts and the words it is read back as cannot drift apart.
struct SettingWords {
    uint8_t first;
    uint8_t count;
    const char* (*nameOf)(uint8_t value);
};

// The struct inside ConfigSnapshot a droid Setting lives in.
enum class SettingSection : uint8_t { Drive, Dome, System };

struct ConfigSetting {
    const char* form;     // the POST /api/config form name, and a refusal's field
    // Where GET /api/config has it, dotted (`rc.sbusTimeoutMs`). nullptr for an
    // Output's wired tick, which GET reads on the Output's row (`wired`) and
    // POST takes back there too.
    const char* path;
    const char* nvsKey;   // unchanged from before the declarations, so a stored Configuration loads as it is
    SettingSection section;
    uint16_t offset;      // of the field inside its section's struct
    SettingStorage storage;
    uint8_t size;         // Text: the field's size, terminator included
    SettingRule rule;
    int32_t lo;           // Range
    int32_t hi;           // Range
    int32_t def;          // the default, for every rule but Member (the family's default member) and Ipv4 (empty)
    const SettingWords* words;  // Words, and Range where a word stands for a number
    uint8_t family;       // Member: the ComponentCategoryId
    const char* says;     // Member and Ipv4: what the refusal's sentence says after the form name
};

// What an Output row Setting is stored in.
//   Row   - a field of the Servo Output row (and of the edit that sets it)
//   Wired - the Output's wired tick: the droid Setting named by its board
//           Output's `enabledField`; an expander's Output is always wired
//   Parts - the Parts it drives, a comma-joined list of Part ids; its range is
//           how many one Output may drive
enum class RowSettingStore : uint8_t { Row, Wired, Parts };

// Which Outputs a row Setting exists on.
enum class RowSettingOn : uint8_t { Every, LightCapable };

struct OutputRowSetting {
    const char* key;      // the row key, on GET /api/servo/outputs and in a POST row
    RowSettingStore store;
    RowSettingOn on;
    SettingStorage storage;
    uint16_t rowOffset;   // Row: in ServoOutputRow
    uint16_t editOffset;  // Row: in ServoOutputEdit
    uint16_t fieldBit;    // Row and Parts: the SERVO_FIELD_* bit an edit carries it under
    SettingRule rule;     // Range, Words or Bool
    int32_t lo;
    int32_t hi;
    const SettingWords* words;
};

// -----------------------------------------------------------------------------
// Settings of the droid
// -----------------------------------------------------------------------------
size_t configSettingCount();
const ConfigSetting& configSettingAt(size_t index);
const ConfigSetting* configSettingByForm(const char* form);

// The Setting's value, read out of / written into its section of `snap`.
// Numbers, words and members as their stored number; Bool as 0/1.
int32_t configSettingNumber(const ConfigSetting& setting, const ConfigSnapshot& snap);
void configSettingSetNumber(const ConfigSetting& setting, ConfigSnapshot* snap, int32_t value);

// The Setting as text: a number, `true`/`false`, a word, a registry id, an IP.
// A Member whose stored number names nothing this image knows writes "" - the
// one case where an id would have to be invented. `out` of 24 bytes holds
// every Setting.
void configSettingFormat(const ConfigSetting& setting, const ConfigSnapshot& snap, char* out,
                         size_t outSize);

// What a Setting's refusal says, beside its field, reason and accepts.
constexpr size_t CONFIG_SETTING_SENTENCE_MAX = 192;

// Parse `raw` against the Setting and, when it is taken, store it into `snap`.
// False when it is refused: `refusal` holds field, reason and accepts and
// `sentence` the sentence for the log and HTTP's `error`, both naming the
// Setting by its form name. `snap` is untouched on a refusal.
bool configSettingApply(const ConfigSetting& setting, const char* raw, ConfigSnapshot* snap,
                        ApplyRefusal* refusal, char* sentence, size_t sentenceSize);

// Every droid Setting at its default.
void configSettingsDefaults(ConfigSnapshot* snap);

// The NVS half: every droid Setting of one section, written under its key, or
// read from it with anything the Setting would refuse repaired - a number
// clamped into its range, an unknown word back to the default, an address too
// long for its field emptied. A Member is read as it is stored, because whether
// this image can still drive it is componentResolveMember()'s question.
// `sectionData` is the section's struct (a DriveConfig, DomeConfig or
// SystemConfig), and a read starts from what it already holds.
bool configSettingsWrite(SettingSection section, const void* sectionData, ConfigWriter& writer);
void configSettingsRead(SettingSection section, const ConfigReader& reader, void* sectionData);

// -----------------------------------------------------------------------------
// Settings of an Output
// -----------------------------------------------------------------------------
size_t outputRowSettingCount();
const OutputRowSetting& outputRowSettingAt(size_t index);

// Whether the row Setting exists on an Output: `board` is its board Output, or
// nullptr for an expander's.
bool outputRowSettingIsOn(const OutputRowSetting& setting, const BoardOutput* board);

// A Row Setting's value on a row, as a number (a word's number, Bool as 0/1).
int32_t outputRowSettingNumber(const OutputRowSetting& setting, const ServoOutputRow& row);

// A Words row Setting's value on a row, as its word.
const char* outputRowSettingWord(const OutputRowSetting& setting, const ServoOutputRow& row);

// Parse `raw` against a Range, Words or Bool row Setting. False, with the
// refusal written under `field` (`<address>.<key>`), when it is not taken.
bool outputRowSettingParse(const OutputRowSetting& setting, const char* raw, const char* field,
                           int32_t* value, ApplyRefusal* refusal, char* sentence,
                           size_t sentenceSize);

// Store a parsed value into a Row Setting's field of `edit` and mark it.
void outputRowSettingSetOnEdit(const OutputRowSetting& setting, int32_t value, ServoOutputEdit* edit);

// -----------------------------------------------------------------------------
// Offsets and storage, worked out from the member (used by the tables)
// -----------------------------------------------------------------------------
template <typename T>
constexpr SettingStorage settingStorageOf();
template <>
constexpr SettingStorage settingStorageOf<bool>() { return SettingStorage::Bool; }
template <>
constexpr SettingStorage settingStorageOf<uint8_t>() { return SettingStorage::U8; }
template <>
constexpr SettingStorage settingStorageOf<uint16_t>() { return SettingStorage::U16; }
template <>
constexpr SettingStorage settingStorageOf<int16_t>() { return SettingStorage::I16; }
template <>
constexpr SettingStorage settingStorageOf<uint32_t>() { return SettingStorage::U32; }
template <>
constexpr SettingStorage settingStorageOf<char[16]>() { return SettingStorage::Text; }
// An enum stored in a byte is stored as that byte.
template <>
constexpr SettingStorage settingStorageOf<RcInputMode>() { return SettingStorage::U8; }
template <>
constexpr SettingStorage settingStorageOf<ServoComponentType>() { return SettingStorage::U8; }
template <>
constexpr SettingStorage settingStorageOf<ServoEasing>() { return SettingStorage::U8; }
template <>
constexpr SettingStorage settingStorageOf<ServoBootBehaviour>() { return SettingStorage::U8; }
