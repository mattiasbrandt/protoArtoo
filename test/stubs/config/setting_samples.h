// =============================================================================
// test/stubs/config/setting_samples.h
//
// A value each declared Setting takes, other than the one a Configuration
// holds - worked out from the Setting's declaration (include/config_settings.h),
// never listed by hand. The round trip (test_api_config_write) and the save and
// load (test_config_settings) build their non-default Configuration from it, so
// a Setting declared tomorrow is carried by both on the day it is declared. The
// same for an Output's row Settings: the round trip's rows are built from
// rowSettingOtherValue().
//
// Header-only: the native build_src_filter lists translation units by hand, and
// a test helper has no business there.
// =============================================================================
#pragma once

#include <stdio.h>
#include <string.h>

#include "component_registry.h"
#include "config_settings.h"

// The text of a value `setting` accepts whose stored value differs from what
// `from` holds. `index` spreads numbers across a table, so Settings that share
// a range (the three speed presets) are not all given one number. False only
// when the Setting has no second value to give, which no declaration allows.
inline bool settingOtherText(const ConfigSetting& setting, const ConfigSnapshot& from, size_t index,
                             char* out, size_t outSize) {
    const int32_t held = configSettingNumber(setting, from);
    switch (setting.rule) {
        case SettingRule::Bool:
            snprintf(out, outSize, "%s", held != 0 ? "false" : "true");
            return true;
        case SettingRule::Words: {
            const char* pick = nullptr;
            for (uint8_t i = 0; i < setting.words->count; ++i) {
                const uint8_t value = (uint8_t)(setting.words->first + i);
                const char* name = setting.words->nameOf(value);
                if (name != nullptr && value != held) {
                    pick = name;
                }
            }
            if (pick == nullptr) {
                return false;
            }
            snprintf(out, outSize, "%s", pick);
            return true;
        }
        case SettingRule::Member: {
            const ComponentPartEntry* pick = nullptr;
            for (int value = 0; value <= 255; ++value) {
                const ComponentPartEntry* part = componentPartByValue((uint8_t)value);
                if (part != nullptr && part->category == setting.family &&
                    componentPartIsSelectable(*part) && value != held) {
                    pick = part;
                }
            }
            if (pick == nullptr) {
                return false;
            }
            snprintf(out, outSize, "%s", pick->id);
            return true;
        }
        case SettingRule::Ipv4: {
            char current[24] = {};
            configSettingFormat(setting, from, current, sizeof(current));
            snprintf(out, outSize, "%s", strcmp(current, "10.1.2.3") == 0 ? "10.1.2.4" : "10.1.2.3");
            return true;
        }
        case SettingRule::Range:
        default: {
            const int32_t span = setting.hi - setting.lo + 1;
            int32_t value = setting.hi - (int32_t)(index % (size_t)span);
            if (value == held) {
                value = value == setting.hi ? setting.hi - 1 : setting.hi;
            }
            snprintf(out, outSize, "%ld", (long)value);
            return true;
        }
    }
}

// A value an Output row Setting (a Row one: stored on the row itself) accepts
// on `from`, other than the one `from` holds, that the row keeps as stated -
// tried through the row's own merge (servoOutputApplyEdit()) and read back, so
// a number the fitted component's band would move, or a word the row repairs,
// is never the one given. `index` spreads numbers across rows and Settings.
// False only when the Setting has no such value on this row.
inline bool rowSettingOtherValue(const OutputRowSetting& setting, const ServoOutputRow& from,
                                 size_t index, int32_t* out) {
    int32_t lo = setting.lo;
    int32_t hi = setting.hi;
    if (setting.rule == SettingRule::Bool) {
        lo = 0;
        hi = 1;
    } else if (setting.rule == SettingRule::Words) {
        lo = setting.words->first;
        hi = setting.words->first + setting.words->count - 1;
    }
    const int32_t held = outputRowSettingNumber(setting, from);
    const int32_t span = hi - lo + 1;
    for (int32_t step = 0; step < span; ++step) {
        const int32_t value = hi - (int32_t)((index + (size_t)step) % (size_t)span);
        if (value == held ||
            (setting.rule == SettingRule::Words && setting.words->nameOf((uint8_t)value) == nullptr)) {
            continue;
        }
        ServoOutputEdit edit = {};
        edit.driver = from.driver;
        edit.channel = from.channel;
        outputRowSettingSetOnEdit(setting, value, &edit);
        ServoOutputRow trial = from;
        if (servoOutputApplyEdit(&trial, edit) == 0 && outputRowSettingNumber(setting, trial) == value) {
            *out = value;
            return true;
        }
    }
    return false;
}
