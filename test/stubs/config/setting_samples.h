// =============================================================================
// test/stubs/config/setting_samples.h
//
// A value each declared Setting takes, other than the one a Configuration
// holds - worked out from the Setting's declaration (include/config_settings.h),
// never listed by hand. The round trip (test_api_config_write) and the save and
// load (test_config_settings) build their non-default Configuration from it, so
// a Setting declared tomorrow is carried by both on the day it is declared.
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
