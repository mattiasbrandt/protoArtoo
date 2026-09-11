// =============================================================================
// include/droid_part_control.h
//
// The control paths a Droid Parts Catalog entry may declare, as firmware sees
// them (#301, #356).
//
// The set itself lives in include/droid_part_control.inc, which the catalog
// generator reads too, so the firmware and the generator can never disagree
// about which control paths exist. A control path says what is DRIVABLE and
// never what is NAMEABLE: every Part the catalog declares reaches the generated
// id table whatever drives it, so nothing here decides membership of that table
// (operator decision, 2026-09-11, #358).
//
// Pure: no NVS, no FreeRTOS, no Arduino String. Header-only, so anything that
// is handed a catalog spelling can resolve it without pulling in a driver.
// =============================================================================

#pragma once

#include <stdint.h>
#include <string.h>

// -----------------------------------------------------------------------------
// DroidPartControl
// One enumerator per manifest row, in manifest order.
// -----------------------------------------------------------------------------
enum DroidPartControl : uint8_t {
#define PA_PART_CONTROL(enumerator, yaml_token) enumerator,
#include "droid_part_control.inc"
#undef PA_PART_CONTROL
};

// -----------------------------------------------------------------------------
// droidPartControlToken()
// The spelling docs/droid-parts.yaml uses for this control path, so a record or
// a log line says what the catalog says rather than a second synonym for it.
// Returns nullptr for a value outside the enum.
// -----------------------------------------------------------------------------
inline const char* droidPartControlToken(DroidPartControl control) {
    switch (control) {
#define PA_PART_CONTROL(enumerator, yaml_token) \
    case enumerator:                            \
        return yaml_token;
#include "droid_part_control.inc"
#undef PA_PART_CONTROL
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// droidPartControlFromToken()
// The reverse, for a caller holding a catalog spelling. Returns false and
// leaves `out` alone when the token is not one the firmware defines - the
// refusal the generator makes at build time, available at run time to anything
// that is handed a token from outside the image.
// -----------------------------------------------------------------------------
inline bool droidPartControlFromToken(const char* token, DroidPartControl* out) {
    if (token == nullptr || out == nullptr) {
        return false;
    }
#define PA_PART_CONTROL(enumerator, yaml_token) \
    if (strcmp(token, yaml_token) == 0) {       \
        *out = enumerator;                      \
        return true;                            \
    }
#include "droid_part_control.inc"
#undef PA_PART_CONTROL
    return false;
}
