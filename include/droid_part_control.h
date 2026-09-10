// =============================================================================
// include/droid_part_control.h
//
// The control paths a Droid Parts Catalog entry may declare, as firmware sees
// them (#301, #356).
//
// The set itself lives in include/droid_part_control.inc, which the catalog
// generator reads too, so the firmware and the generator can never disagree
// about which control paths exist or which of them reach firmware.
//
// Pure: no NVS, no FreeRTOS, no Arduino String. Header-only and constexpr, so
// the generated id table can assert against it at compile time.
// =============================================================================

#pragma once

#include <stdint.h>
#include <string.h>

// -----------------------------------------------------------------------------
// DroidPartControl
// One enumerator per manifest row, in manifest order.
// -----------------------------------------------------------------------------
enum DroidPartControl : uint8_t {
#define PA_PART_CONTROL(enumerator, yaml_token, reaches_firmware) enumerator,
#include "droid_part_control.inc"
#undef PA_PART_CONTROL
};

// -----------------------------------------------------------------------------
// droidPartControlReachesFirmware()
// Whether Parts on this control path are generated into the firmware id table.
//
// constexpr because the generated header asserts on it: include/droid_parts.h
// carries one static_assert per control path it emitted a part under, so a
// catalog that starts emitting Parts on a path the body cannot drive fails the
// build rather than shipping ids firmware can never resolve to an Output.
// -----------------------------------------------------------------------------
constexpr bool droidPartControlReachesFirmware(DroidPartControl control) {
    switch (control) {
#define PA_PART_CONTROL(enumerator, yaml_token, reaches_firmware) \
    case enumerator:                                             \
        return (reaches_firmware) != 0;
#include "droid_part_control.inc"
#undef PA_PART_CONTROL
    }
    return false;
}

// -----------------------------------------------------------------------------
// droidPartControlToken()
// The spelling docs/droid-parts.yaml uses for this control path, so a record or
// a log line says what the catalog says rather than a second synonym for it.
// Returns nullptr for a value outside the enum.
// -----------------------------------------------------------------------------
inline const char* droidPartControlToken(DroidPartControl control) {
    switch (control) {
#define PA_PART_CONTROL(enumerator, yaml_token, reaches_firmware) \
    case enumerator:                                             \
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
#define PA_PART_CONTROL(enumerator, yaml_token, reaches_firmware) \
    if (strcmp(token, yaml_token) == 0) {                        \
        *out = enumerator;                                       \
        return true;                                             \
    }
#include "droid_part_control.inc"
#undef PA_PART_CONTROL
    return false;
}
