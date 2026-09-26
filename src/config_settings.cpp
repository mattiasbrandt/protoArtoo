// =============================================================================
// src/config_settings.cpp
//
// Each Setting, declared once (ADR 0068, amended 2026-09-26). See
// include/config_settings.h for who reads these and why they are the one home.
// =============================================================================

#include "config_settings.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "api_helpers.h"          // parseDriveValue(), parseUint32Value(), parseBoolValue()
#include "board_outputs.h"
#include "component_registry.h"
#include "config.h"               // SPEED_*, SBUS_TIMEOUT_MS, WEB_DRIVE_TIMEOUT_MS, PA_LOG_LEVEL*
#include "servo_component_helpers.h"

namespace {

// -----------------------------------------------------------------------------
// The word lists. Each names its words through the vocabulary's own function,
// so a Setting takes and reports exactly the words the rest of the firmware
// uses for the same thing.
// -----------------------------------------------------------------------------
const char* logLevelName(uint8_t level) {
    switch (level) {
        case PA_LOG_LEVEL_ERROR:
            return "error";
        case 2:
            return "warning";
        case 3:
            return "info";
        case PA_LOG_LEVEL_DEBUG:
            return "debug";
        default:
            return nullptr;
    }
}

const char* componentName(uint8_t value) {
    return servoCompTypeToString((ServoComponentType)value);
}
const char* easingName(uint8_t value) { return servoEasingToString((ServoEasing)value); }
const char* bootName(uint8_t value) { return servoBootBehaviourToString((ServoBootBehaviour)value); }

constexpr SettingWords kRcInputModeWords = {RC_INPUT_STANDARD_PWM, 4, rcInputModeName};
constexpr SettingWords kLogLevelWords = {PA_LOG_LEVEL_ERROR, 4, logLevelName};
constexpr SettingWords kComponentWords = {SERVO_COMP_NONE, 4, componentName};
constexpr SettingWords kEasingWords = {SERVO_EASE_NONE, SERVO_EASE_COUNT, easingName};
constexpr SettingWords kBootWords = {SERVO_BOOT_LIMP, SERVO_BOOT_COUNT, bootName};

// -----------------------------------------------------------------------------
// Settings of the droid
//
// One line each. The helpers below only spell the fields a rule does not use as
// zero; what a Setting IS is the line.
// -----------------------------------------------------------------------------
#define PA_SETTING_FIELD(Section, Type, member)                                    \
    SettingSection::Section, (uint16_t)offsetof(Type, member),                     \
        settingStorageOf<decltype(Type::member)>(), (uint8_t)sizeof(Type::member)

#define PA_RANGE(form, path, key, Section, Type, member, lo, hi, def) \
    {form, path, key, PA_SETTING_FIELD(Section, Type, member), SettingRule::Range, lo, hi, def, nullptr, 0, nullptr}
#define PA_BOOL(form, path, key, Section, Type, member, def) \
    {form, path, key, PA_SETTING_FIELD(Section, Type, member), SettingRule::Bool, 0, 1, def, nullptr, 0, nullptr}
#define PA_WORDS(form, path, key, Section, Type, member, words, def) \
    {form, path, key, PA_SETTING_FIELD(Section, Type, member), SettingRule::Words, 0, 0, def, &words, 0, nullptr}
#define PA_MEMBER(form, path, key, Section, Type, member, family, says) \
    {form, path, key, PA_SETTING_FIELD(Section, Type, member), SettingRule::Member, 0, 0, 0, nullptr, family, says}

const ConfigSetting kConfigSettings[] = {
    // Foot Drive. The three presets must differ and speedLimitMax picks the
    // active one: both are rules across Settings, in configApply().
    PA_RANGE("speedLimitMax", "drive.speedLimitMax", "spd_max", Drive, DriveConfig, speedLimitMax,
             0, SPEED_LIMIT_MAX, SPEED_LIMIT_MAX),
    PA_RANGE("speedPresetSlow", "drive.speedPresetSlow", "spd_pre_s", Drive, DriveConfig,
             speedPresetSlow, 0, SPEED_LIMIT_MAX, SPEED_PRESET_SLOW),
    PA_RANGE("speedPresetNormal", "drive.speedPresetNormal", "spd_pre_n", Drive, DriveConfig,
             speedPresetNormal, 0, SPEED_LIMIT_MAX, SPEED_PRESET_NORMAL),
    PA_RANGE("speedPresetTurbo", "drive.speedPresetTurbo", "spd_pre_t", Drive, DriveConfig,
             speedPresetTurbo, 0, SPEED_LIMIT_MAX, SPEED_PRESET_TURBO),
    PA_RANGE("webDriveTimeoutMs", "drive.webDriveTimeoutMs", "web_tmo", Drive, DriveConfig,
             webDriveTimeoutMs, 100, 5000, WEB_DRIVE_TIMEOUT_MS),
    PA_BOOL("stationary", "drive.stationary", "op_mode", System, SystemConfig, stationary, false),

    // The radio
    PA_WORDS("rcInputMode", "rc.inputMode", "rc_mode", System, SystemConfig, rc_input_mode,
             kRcInputModeWords, RC_INPUT_DUAL_SBUS),
    PA_RANGE("sbusTimeoutMs", "rc.sbusTimeoutMs", "sbus_tmo", Drive, DriveConfig, sbusTimeoutMs,
             50, 5000, SBUS_TIMEOUT_MS),
    PA_MEMBER("rcMember", "rc.member", "rc_member", System, SystemConfig, rc_member,
              COMPONENT_CATEGORY_RADIO_CONTROLLER, "is not a radio this firmware lists"),
    PA_BOOL("sbusRecvCh2", "rc.sbus.recvCh2", "sbus_recv_ch2", System, SystemConfig,
            single_sbus_use_ch2, false),

    // The Component Toggles that are not an Output, and the Sound member
    PA_BOOL("enableDomeEsc", "components.domeEsc.enabled", "en_dome_esc", System, SystemConfig,
            enable_dome_esc, false),
    PA_BOOL("enableRcCh1", "components.rcCh1.enabled", "en_rc_ch1", System, SystemConfig,
            enable_rc_ch1, false),
    PA_BOOL("enableRcCh2", "components.rcCh2.enabled", "en_rc_ch2", System, SystemConfig,
            enable_rc_ch2, false),
    PA_BOOL("enableRcCh3", "components.rcCh3.enabled", "en_rc_ch3", System, SystemConfig,
            enable_rc_ch3, false),
    PA_BOOL("enableRcCh4", "components.rcCh4.enabled", "en_rc_ch4", System, SystemConfig,
            enable_rc_ch4, false),
    PA_BOOL("enableRcCh5", "components.rcCh5.enabled", "en_rc_ch5", System, SystemConfig,
            enable_rc_ch5, false),
    PA_BOOL("enableRcCh6", "components.rcCh6.enabled", "en_rc_ch6", System, SystemConfig,
            enable_rc_ch6, false),
    PA_BOOL("enableDrive", "components.drive.enabled", "en_drive", System, SystemConfig,
            enable_drive, false),
    PA_BOOL("enableAudio", "components.audio.enabled", "en_audio", System, SystemConfig,
            enable_audio, false),
    PA_MEMBER("soundMember", "components.audio.member", "snd_member", System, SystemConfig,
              sound_member, COMPONENT_CATEGORY_SOUND,
              "is not a sound module this firmware can drive"),
    PA_BOOL("enableProtoR2link", "components.protoR2link.enabled", "en_r2link", System,
            SystemConfig, enable_protor2link, false),

    // Each board Output's wired tick. GET reads it on the Output's row and POST
    // takes it back there (`wired`), so it has no path here; the form name is
    // what the Console writes it by (BOARD_OUTPUTS' `enabledField`).
    PA_BOOL("enableArm1", nullptr, "en_arm1", System, SystemConfig, enable_arm1, false),
    PA_BOOL("enableArm2", nullptr, "en_arm2", System, SystemConfig, enable_arm2, false),
    PA_BOOL("enableAux1", nullptr, "en_aux1", System, SystemConfig, enable_aux1, false),
    PA_BOOL("enableAux2", nullptr, "en_aux2", System, SystemConfig, enable_aux2, false),
    PA_BOOL("enableAux3", nullptr, "en_aux3", System, SystemConfig, enable_aux3, false),

    // The Dome ESC. The three pulses must stay in order: a rule across them,
    // in configApply() and on load.
    PA_RANGE("domeEscNeutralUs", "domeEsc.neutralUs", "dome_neu", Dome, DomeConfig,
             dome_neutral_us, 1000, 2000, 1500),
    PA_RANGE("domeEscMinPulseUs", "domeEsc.minPulseUs", "dome_minp", Dome, DomeConfig,
             dome_min_pulse_us, 1000, 2000, 1000),
    PA_RANGE("domeEscMaxPulseUs", "domeEsc.maxPulseUs", "dome_maxp", Dome, DomeConfig,
             dome_max_pulse_us, 1000, 2000, 2000),
    PA_RANGE("domeEscSpeedLimitPct", "domeEsc.speedLimitPct", "dome_pct", Dome, DomeConfig,
             dome_speed_limit_pct, 0, 100, 100),
    PA_BOOL("domeEscRndEnable", "domeEsc.rndEnable", "dome_rnd_en", Dome, DomeConfig,
            dome_rnd_enable, false),
    PA_RANGE("domeEscRndSpeedPct", "domeEsc.rndSpeedPct", "dome_rnd_spd", Dome, DomeConfig,
             dome_rnd_speed_pct, 5, 100, 30),
    PA_RANGE("domeEscRndPauseMin", "domeEsc.rndPauseMin", "dome_rnd_pmin", Dome, DomeConfig,
             dome_rnd_pause_min, 1, 120, 6),
    PA_RANGE("domeEscRndPauseMax", "domeEsc.rndPauseMax", "dome_rnd_pmax", Dome, DomeConfig,
             dome_rnd_pause_max, 1, 120, 12),
    PA_RANGE("domeEscRndMoveMs", "domeEsc.rndMoveMs", "dome_rnd_ms", Dome, DomeConfig,
             dome_rnd_move_ms, 500, 10000, 2500),
    {"protoR2linkWifiPeerIp", "protoR2link.wifiPeerIp", "dome_wip",
     PA_SETTING_FIELD(Dome, DomeConfig, dome_wifi_peer_ip), SettingRule::Ipv4, 0, 0, 0, nullptr, 0,
     "must be empty or a valid IPv4 address"},

    // The log level takes its words as well as its number, at every door, and
    // GET reads the number (#423's round trip).
    {"logLevel", "system.logLevel", "log_level", PA_SETTING_FIELD(System, SystemConfig, logLevel),
     SettingRule::Range, PA_LOG_LEVEL_ERROR, PA_LOG_LEVEL_DEBUG, PA_LOG_LEVEL, &kLogLevelWords, 0,
     nullptr},
};

#undef PA_RANGE
#undef PA_BOOL
#undef PA_WORDS
#undef PA_MEMBER

constexpr size_t kConfigSettingCount = sizeof(kConfigSettings) / sizeof(kConfigSettings[0]);

// -----------------------------------------------------------------------------
// Settings of an Output
//
// The Motion Profile's bounds are the stored row's own
// (servoOutputRowNormalise()), so a number the row door takes is exactly one the
// row keeps. The ends and the centre are checked against what any servo takes,
// SERVO_PULSE_MIN_US..SERVO_PULSE_MAX_US, and nothing narrower: the fitted
// component's band is applied on the row by the Commit Step, which CLAMPS into
// it and names what it moved, because the band can narrow after the ends were
// recorded and refusing would throw a calibration away (ADR 0068).
// -----------------------------------------------------------------------------
#define PA_ROW_FIELD(member)                                                     \
    settingStorageOf<decltype(ServoOutputRow::member)>(),                        \
        (uint16_t)offsetof(ServoOutputRow, member), (uint16_t)offsetof(ServoOutputEdit, member)

const OutputRowSetting kOutputRowSettings[] = {
    {"wired", RowSettingStore::Wired, RowSettingOn::Every, SettingStorage::Bool, 0, 0, 0,
     SettingRule::Bool, 0, 1, nullptr},
    {"component", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(component),
     SERVO_FIELD_COMPONENT, SettingRule::Words, 0, 0, &kComponentWords},
    {"ledCount", RowSettingStore::Row, RowSettingOn::LightCapable, PA_ROW_FIELD(led_count),
     SERVO_FIELD_LED_COUNT, SettingRule::Range, SERVO_LIGHT_LEDS_MIN, SERVO_LIGHT_LEDS_MAX, nullptr},
    {"throwMs", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(throw_ms),
     SERVO_FIELD_THROW_MS, SettingRule::Range, SERVO_THROW_MS_MIN, SERVO_THROW_MS_MAX, nullptr},
    {"accelMs", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(accel_ms),
     SERVO_FIELD_ACCEL_MS, SettingRule::Range, SERVO_ACCEL_MS_MIN, SERVO_ACCEL_MS_MAX, nullptr},
    {"ease", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(easing), SERVO_FIELD_EASING,
     SettingRule::Words, 0, 0, &kEasingWords},
    {"boot", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(boot), SERVO_FIELD_BOOT,
     SettingRule::Words, 0, 0, &kBootWords},
    {"openUs", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(open_us), SERVO_FIELD_OPEN,
     SettingRule::Range, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US, nullptr},
    {"centreUs", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(centre_us),
     SERVO_FIELD_CENTRE, SettingRule::Range, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US, nullptr},
    {"closeUs", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(close_us),
     SERVO_FIELD_CLOSE, SettingRule::Range, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US, nullptr},
    {"calibrated", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(calibrated),
     SERVO_FIELD_CALIBRATED, SettingRule::Bool, 0, 1, nullptr},
    {"parts", RowSettingStore::Parts, RowSettingOn::Every, SettingStorage::Text, 0, 0,
     SERVO_FIELD_PARTS, SettingRule::Range, 0, SERVO_OUTPUT_PART_SLOTS, nullptr},
};

#undef PA_ROW_FIELD
#undef PA_SETTING_FIELD

constexpr size_t kOutputRowSettingCount = sizeof(kOutputRowSettings) / sizeof(kOutputRowSettings[0]);

// -----------------------------------------------------------------------------
// Storage
// -----------------------------------------------------------------------------
int32_t loadNumber(SettingStorage storage, const uint8_t* at) {
    switch (storage) {
        case SettingStorage::Bool: {
            bool v;
            memcpy(&v, at, sizeof(v));
            return v ? 1 : 0;
        }
        case SettingStorage::U8:
            return *at;
        case SettingStorage::U16: {
            uint16_t v;
            memcpy(&v, at, sizeof(v));
            return v;
        }
        case SettingStorage::I16: {
            int16_t v;
            memcpy(&v, at, sizeof(v));
            return v;
        }
        case SettingStorage::U32: {
            uint32_t v;
            memcpy(&v, at, sizeof(v));
            return (int32_t)v;
        }
        case SettingStorage::Text:
        default:
            return 0;
    }
}

void storeNumber(SettingStorage storage, uint8_t* at, int32_t value) {
    switch (storage) {
        case SettingStorage::Bool: {
            const bool v = value != 0;
            memcpy(at, &v, sizeof(v));
            break;
        }
        case SettingStorage::U8:
            *at = (uint8_t)value;
            break;
        case SettingStorage::U16: {
            const uint16_t v = (uint16_t)value;
            memcpy(at, &v, sizeof(v));
            break;
        }
        case SettingStorage::I16: {
            const int16_t v = (int16_t)value;
            memcpy(at, &v, sizeof(v));
            break;
        }
        case SettingStorage::U32: {
            const uint32_t v = (uint32_t)value;
            memcpy(at, &v, sizeof(v));
            break;
        }
        case SettingStorage::Text:
        default:
            break;
    }
}

uint8_t* sectionOf(ConfigSnapshot* snap, SettingSection section) {
    switch (section) {
        case SettingSection::Drive:
            return reinterpret_cast<uint8_t*>(&snap->drive);
        case SettingSection::Dome:
            return reinterpret_cast<uint8_t*>(&snap->dome);
        case SettingSection::System:
        default:
            return reinterpret_cast<uint8_t*>(&snap->system);
    }
}

const uint8_t* sectionOf(const ConfigSnapshot& snap, SettingSection section) {
    return sectionOf(const_cast<ConfigSnapshot*>(&snap), section);
}

// -----------------------------------------------------------------------------
// Words
// -----------------------------------------------------------------------------
bool wordEqualsIgnoringCase(const char* typed, const char* word) {
    size_t i = 0;
    for (; word[i] != '\0'; ++i) {
        if (tolower((unsigned char)typed[i]) != tolower((unsigned char)word[i])) {
            return false;
        }
    }
    return typed[i] == '\0';
}

bool wordValue(const SettingWords& words, const char* raw, int32_t* value) {
    for (uint8_t i = 0; i < words.count; ++i) {
        const uint8_t v = (uint8_t)(words.first + i);
        const char* name = words.nameOf(v);
        if (name != nullptr && wordEqualsIgnoringCase(raw, name)) {
            *value = v;
            return true;
        }
    }
    return false;
}

bool wordIsKnown(const SettingWords& words, int32_t value) {
    return value >= words.first && value < words.first + words.count &&
           words.nameOf((uint8_t)value) != nullptr;
}

// `a,b,c` for accepts, or `a, b, c or d` for the sentence.
void joinWords(const SettingWords& words, bool forSentence, char* out, size_t outSize) {
    size_t used = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < words.count && used < outSize; ++i) {
        const char* sep = i == 0 ? "" : !forSentence ? "," : i + 1 == words.count ? " or " : ", ";
        const int n = snprintf(out + used, outSize - used, "%s%s", sep,
                               words.nameOf((uint8_t)(words.first + i)));
        if (n < 0) {
            break;
        }
        used += (size_t)n;
    }
}

// -----------------------------------------------------------------------------
// Checks shared by both kinds of Setting
// -----------------------------------------------------------------------------
constexpr const char* kBoolAccepts = "true,false,1,0";

// The number `raw` names under a Range, Words or Bool rule. False, with the
// refusal and its sentence written, when the rule does not take it.
bool parseNumberRule(SettingRule rule, SettingStorage storage, int32_t lo, int32_t hi,
                     const SettingWords* words, const char* raw, const char* field,
                     int32_t* value, ApplyRefusal* refusal, char* sentence, size_t sentenceSize) {
    if (rule == SettingRule::Bool) {
        bool b = false;
        if (raw != nullptr && parseBoolValue(raw, &b)) {
            *value = b ? 1 : 0;
            return true;
        }
        snprintf(sentence, sentenceSize, "%s must be true/false or 1/0", field);
        applyRefusalSet(refusal, ApplyRefusalReason::OutOfRange, field, kBoolAccepts);
        return false;
    }

    if (raw != nullptr && words != nullptr && wordValue(*words, raw, value)) {
        return true;
    }

    if (rule == SettingRule::Range && raw != nullptr) {
        bool parsed = false;
        if (storage == SettingStorage::I16) {
            int16_t v = 0;
            parsed = parseDriveValue(raw, &v);
            *value = v;
        } else {
            uint32_t v = 0;
            parsed = parseUint32Value(raw, &v) && v <= (uint32_t)INT32_MAX;
            *value = (int32_t)v;
        }
        if (parsed && *value >= lo && *value <= hi) {
            return true;
        }
    }

    // A Setting that takes words is refused in its words, a number in its range.
    if (words != nullptr) {
        char list[APPLY_REFUSAL_ACCEPTS_MAX];
        joinWords(*words, true, list, sizeof(list));
        snprintf(sentence, sentenceSize, "%s must be %s", field, list);
        joinWords(*words, false, list, sizeof(list));
        applyRefusalSet(refusal, ApplyRefusalReason::OutOfRange, field, list);
    } else {
        snprintf(sentence, sentenceSize, "%s must be %ld..%ld", field, (long)lo, (long)hi);
        applyRefusalSetRange(refusal, field, lo, hi);
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

}  // namespace

// =============================================================================
// Settings of the droid
// =============================================================================
size_t configSettingCount() { return kConfigSettingCount; }

const ConfigSetting& configSettingAt(size_t index) { return kConfigSettings[index]; }

const ConfigSetting* configSettingByForm(const char* form) {
    if (form == nullptr) {
        return nullptr;
    }
    for (const ConfigSetting& setting : kConfigSettings) {
        if (strcmp(setting.form, form) == 0) {
            return &setting;
        }
    }
    return nullptr;
}

int32_t configSettingNumber(const ConfigSetting& setting, const ConfigSnapshot& snap) {
    return loadNumber(setting.storage, sectionOf(snap, setting.section) + setting.offset);
}

void configSettingSetNumber(const ConfigSetting& setting, ConfigSnapshot* snap, int32_t value) {
    storeNumber(setting.storage, sectionOf(snap, setting.section) + setting.offset, value);
}

void configSettingFormat(const ConfigSetting& setting, const ConfigSnapshot& snap, char* out,
                         size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return;
    }
    const uint8_t* at = sectionOf(snap, setting.section) + setting.offset;
    const int32_t value = loadNumber(setting.storage, at);
    switch (setting.rule) {
        case SettingRule::Bool:
            snprintf(out, outSize, "%s", value != 0 ? "true" : "false");
            return;
        case SettingRule::Words: {
            const char* word = wordIsKnown(*setting.words, value) ? setting.words->nameOf((uint8_t)value) : "";
            snprintf(out, outSize, "%s", word);
            return;
        }
        case SettingRule::Member: {
            const ComponentPartEntry* part = componentPartByValue((uint8_t)value);
            snprintf(out, outSize, "%s", part != nullptr ? part->id : "");
            return;
        }
        case SettingRule::Ipv4:
            snprintf(out, outSize, "%.*s", (int)setting.size, reinterpret_cast<const char*>(at));
            return;
        case SettingRule::Range:
        default:
            snprintf(out, outSize, "%ld", (long)value);
            return;
    }
}

bool configSettingApply(const ConfigSetting& setting, const char* raw, ConfigSnapshot* snap,
                        ApplyRefusal* refusal, char* sentence, size_t sentenceSize) {
    uint8_t* at = sectionOf(snap, setting.section) + setting.offset;
    switch (setting.rule) {
        case SettingRule::Member: {
            // Named by its Component Registry id rather than the number it is
            // stored as, so nothing outside the registry has to know the
            // numbering. A roadmap row and a member of another family are both
            // refused, by the rule a picker's lineup comes from. The sentence
            // never echoes what was asked: it lands in a JSON error body.
            const ComponentPartEntry* part = componentPartById(raw);
            if (part == nullptr || part->category != setting.family ||
                !componentPartIsSelectable(*part)) {
                snprintf(sentence, sentenceSize, "%s %s", setting.form, setting.says);
                applyRefusalSet(refusal, ApplyRefusalReason::OutOfRange, setting.form);
                return false;
            }
            storeNumber(setting.storage, at, part->value);
            return true;
        }
        case SettingRule::Ipv4: {
            const bool empty = raw != nullptr && raw[0] == '\0';
            if (raw == nullptr || strlen(raw) >= setting.size || (!empty && !isValidIpv4Literal(raw))) {
                snprintf(sentence, sentenceSize, "%s %s", setting.form, setting.says);
                applyRefusalSet(refusal, ApplyRefusalReason::OutOfRange, setting.form);
                return false;
            }
            snprintf(reinterpret_cast<char*>(at), setting.size, "%s", raw);
            return true;
        }
        case SettingRule::Range:
        case SettingRule::Words:
        case SettingRule::Bool:
        default: {
            int32_t value = 0;
            if (!parseNumberRule(setting.rule, setting.storage, setting.lo, setting.hi,
                                 setting.words, raw, setting.form, &value, refusal, sentence,
                                 sentenceSize)) {
                return false;
            }
            storeNumber(setting.storage, at, value);
            return true;
        }
    }
}

void configSettingsDefaults(ConfigSnapshot* snap) {
    for (const ConfigSetting& setting : kConfigSettings) {
        uint8_t* at = sectionOf(snap, setting.section) + setting.offset;
        switch (setting.rule) {
            case SettingRule::Member:
                // A controller never asked which product it has starts on the
                // one its build names.
                storeNumber(setting.storage, at,
                            componentCategoryDefaultMember((ComponentCategoryId)setting.family));
                break;
            case SettingRule::Ipv4:
                at[0] = '\0';
                break;
            default:
                storeNumber(setting.storage, at, setting.def);
                break;
        }
    }
}

bool configSettingsWrite(SettingSection section, const void* sectionData, ConfigWriter& writer) {
    const uint8_t* base = static_cast<const uint8_t*>(sectionData);
    bool ok = true;
    for (const ConfigSetting& setting : kConfigSettings) {
        if (setting.section != section) {
            continue;
        }
        const uint8_t* at = base + setting.offset;
        const int32_t value = loadNumber(setting.storage, at);
        switch (setting.storage) {
            case SettingStorage::Bool:
                ok = writer.writeBool(setting.nvsKey, value != 0) && ok;
                break;
            case SettingStorage::U8:
                ok = writer.writeU8(setting.nvsKey, (uint8_t)value) && ok;
                break;
            case SettingStorage::U16:
                ok = writer.writeU16(setting.nvsKey, (uint16_t)value) && ok;
                break;
            case SettingStorage::I16:
                ok = writer.writeI16(setting.nvsKey, (int16_t)value) && ok;
                break;
            case SettingStorage::U32:
                ok = writer.writeU32(setting.nvsKey, (uint32_t)value) && ok;
                break;
            case SettingStorage::Text:
                // Written unconditionally, empty included. An empty peer IP is
                // the "not configured" state, and it must overwrite whatever an
                // earlier save stored: NVS keeps every key a save does not
                // touch, so skipping the key here would leave the old address
                // to come back on the next cold boot. Writing "" is a real
                // store, not a no-op: Preferences::putString only short-circuits
                // on a null pointer, then calls nvs_set_str (arduino-esp32
                // 3.3.7, libraries/Preferences/src/Preferences.cpp:264-279),
                // which stores strlen(value) + 1 bytes -- one byte for ""
                // (ESP-IDF 5.5, components/nvs_flash/src/nvs_handle_simple.cpp:31-37).
                // PrefsWriter::writeStr already treats putString's 0 return for
                // an empty string as success, and the WiFi serializer relies on
                // the same path for an empty STA SSID.
                ok = writer.writeStr(setting.nvsKey, reinterpret_cast<const char*>(at)) && ok;
                break;
        }
    }
    return ok;
}

void configSettingsRead(SettingSection section, const ConfigReader& reader, void* sectionData) {
    uint8_t* base = static_cast<uint8_t*>(sectionData);
    for (const ConfigSetting& setting : kConfigSettings) {
        if (setting.section != section) {
            continue;
        }
        uint8_t* at = base + setting.offset;
        const int32_t held = loadNumber(setting.storage, at);
        int32_t value = held;
        switch (setting.storage) {
            case SettingStorage::Bool:
                value = reader.readBool(setting.nvsKey, held != 0) ? 1 : 0;
                break;
            case SettingStorage::U8:
                value = reader.readU8(setting.nvsKey, (uint8_t)held);
                break;
            case SettingStorage::U16:
                value = reader.readU16(setting.nvsKey, (uint16_t)held);
                break;
            case SettingStorage::I16:
                value = reader.readI16(setting.nvsKey, (int16_t)held);
                break;
            case SettingStorage::U32: {
                // A stored number past INT32_MAX is out of every range here.
                const uint32_t raw = reader.readU32(setting.nvsKey, (uint32_t)held);
                value = raw > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)raw;
                break;
            }
            case SettingStorage::Text: {
                // Anything too long for its field is refused rather than cut:
                // half an address is not an address.
                const String stored = reader.readStr(setting.nvsKey, reinterpret_cast<const char*>(at));
                if (stored.length() >= setting.size) {
                    at[0] = '\0';
                } else {
                    snprintf(reinterpret_cast<char*>(at), setting.size, "%s", stored.c_str());
                }
                continue;
            }
        }
        // What the door would refuse is repaired on the way in, so a value no
        // page could have saved never reaches the task that reads it.
        switch (setting.rule) {
            case SettingRule::Range:
                value = value < setting.lo ? setting.lo : value > setting.hi ? setting.hi : value;
                break;
            case SettingRule::Words:
                if (!wordIsKnown(*setting.words, value)) {
                    value = setting.def;
                }
                break;
            case SettingRule::Bool:
            case SettingRule::Member:
            case SettingRule::Ipv4:
            default:
                break;
        }
        storeNumber(setting.storage, at, value);
    }
}

// =============================================================================
// Settings of an Output
// =============================================================================
size_t outputRowSettingCount() { return kOutputRowSettingCount; }

const OutputRowSetting& outputRowSettingAt(size_t index) { return kOutputRowSettings[index]; }

bool outputRowSettingIsOn(const OutputRowSetting& setting, const BoardOutput* board) {
    return setting.on == RowSettingOn::Every || (board != nullptr && board->lightCapable);
}

int32_t outputRowSettingNumber(const OutputRowSetting& setting, const ServoOutputRow& row) {
    return loadNumber(setting.storage, reinterpret_cast<const uint8_t*>(&row) + setting.rowOffset);
}

const char* outputRowSettingWord(const OutputRowSetting& setting, const ServoOutputRow& row) {
    const int32_t value = outputRowSettingNumber(setting, row);
    return setting.words != nullptr && wordIsKnown(*setting.words, value)
               ? setting.words->nameOf((uint8_t)value)
               : "";
}

bool outputRowSettingParse(const OutputRowSetting& setting, const char* raw, const char* field,
                           int32_t* value, ApplyRefusal* refusal, char* sentence,
                           size_t sentenceSize) {
    return parseNumberRule(setting.rule, setting.storage, setting.lo, setting.hi, setting.words, raw,
                           field, value, refusal, sentence, sentenceSize);
}

void outputRowSettingSetOnEdit(const OutputRowSetting& setting, int32_t value, ServoOutputEdit* edit) {
    storeNumber(setting.storage, reinterpret_cast<uint8_t*>(edit) + setting.editOffset, value);
    edit->fields |= setting.fieldBit;
}
