// =============================================================================
// include/console_config_fields.h
//
// Component Toggle field table (ADR 0027 staged-at-reboot, ADR 0033 identity)
// for the Controller Console's system.config.enable_* dispatch (#226).
//
// HEADER-ONLY DELIBERATELY, same reasoning as include/console_args.h and
// include/console_completion.h: [env:native]'s build_src_filter in
// platformio.ini is an explicit allowlist of src/*.cpp translation units and
// is fenced on this ticket, so a new .cpp here would have no way to reach the
// native test binary. `inline`/`constexpr` needs no build_src_filter entry.
//
// Single source of truth for two independent consumers that must never
// disagree on which 15 fields are Component Toggles or what their console-
// visible names are:
//   - src/console/console_module.cpp's system.config.enable_* dispatch
//     (read/write through the existing api_config_apply.cpp Apply Core).
//   - src/config_store.cpp's Active Component Toggle boot snapshot
//     (include/config_cache.h's configCacheSetActiveComponentToggles(), used
//     to answer "read shows saved vs active").
//
// `paramKey` is copied verbatim from api_config_apply.cpp's own `boolFields[]`
// table (the literal string each field's ConfigParamSource lookup uses) - not
// re-derived, so a rename in that table needs a matching edit here. There is
// no code generator reaching this table: docs/action-registry.yaml's
// generated catalog (tools/generate_console_catalog.py) also regenerates
// data/console_help.txt, which is unconditionally fenced on this ticket, so
// this mapping is intentionally hand-maintained C++ rather than registry-
// driven metadata (see console_module.cpp's CONSOLE_OP_CONFIG dispatch
// comment for the full reasoning).
// =============================================================================
#pragma once

#include <stddef.h>
#include <string.h>

#include "config_store.h"

struct ComponentToggleField {
    const char* operationName;  // canonical console operation name (registry `name:`)
    const char* paramKey;       // api_config_apply.cpp boolFields[] param name, verbatim
    bool SystemConfig::* field;  // pointer-to-member for a generic get/set
};

inline constexpr ComponentToggleField kComponentToggleFields[] = {
    {"system.config.enable_arm1", "enableArm1", &SystemConfig::enable_arm1},
    {"system.config.enable_arm2", "enableArm2", &SystemConfig::enable_arm2},
    {"system.config.enable_aux1", "enableAux1", &SystemConfig::enable_aux1},
    {"system.config.enable_aux2", "enableAux2", &SystemConfig::enable_aux2},
    {"system.config.enable_aux3", "enableAux3", &SystemConfig::enable_aux3},
    {"system.config.enable_dome_esc", "enableDomeEsc", &SystemConfig::enable_dome_esc},
    {"system.config.enable_rc_ch1", "enableRcCh1", &SystemConfig::enable_rc_ch1},
    {"system.config.enable_rc_ch2", "enableRcCh2", &SystemConfig::enable_rc_ch2},
    {"system.config.enable_rc_ch3", "enableRcCh3", &SystemConfig::enable_rc_ch3},
    {"system.config.enable_rc_ch4", "enableRcCh4", &SystemConfig::enable_rc_ch4},
    {"system.config.enable_rc_ch5", "enableRcCh5", &SystemConfig::enable_rc_ch5},
    {"system.config.enable_rc_ch6", "enableRcCh6", &SystemConfig::enable_rc_ch6},
    {"system.config.enable_drive", "enableDrive", &SystemConfig::enable_drive},
    {"system.config.enable_audio", "enableAudio", &SystemConfig::enable_audio},
    {"system.config.enable_protor2link", "enableProtoR2link", &SystemConfig::enable_protor2link},
};

inline constexpr size_t kComponentToggleFieldCount =
    sizeof(kComponentToggleFields) / sizeof(kComponentToggleFields[0]);

// Linear scan by canonical operation name - 15 entries, called once per
// Console request, not a hot loop. Returns nullptr if `name` is not a
// Component Toggle operation.
inline const ComponentToggleField* consoleFindComponentToggleField(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < kComponentToggleFieldCount; ++i) {
        if (strcmp(kComponentToggleFields[i].operationName, name) == 0) {
            return &kComponentToggleFields[i];
        }
    }
    return nullptr;
}
