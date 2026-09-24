// =============================================================================
// include/servo_legacy_field_sets.h
//
// The five fixed servo field sets, reduced to names (#286, #345, ADR 0041).
//
// A servo output used to BE these names. `arm1_open_us` and its nine siblings
// were the store, the API contract and the validation surface at once, and the
// component type beside them was a sixteenth field of the same shape. #345
// deleted the store: an endpoint lives on an addressed Servo Output row and
// nowhere else, so a calibration can no longer disagree with itself depending
// on which path read it.
//
// What could not be deleted with it is the names, for two reasons that have
// nothing to do with each other:
//
//   - A controller that has not saved a row yet still holds its calibration
//     under the NVS keys below. They are read once, by the row loader, on a
//     row nothing has written -- and never written again. Deleting that read
//     would not delete a builder's calibration; it would silently drop it,
//     which is the one thing #286 refuses. configSaveServoOutputs() removes
//     the keys once the rows they became are safely down.
//   - Backup and Restore (data/maintenance.js) still speak the old field
//     names on /api/config, in both directions: a backup is that payload and a
//     restore posts it back. The API answers them FROM the rows, so a backup
//     holds the number the droid will actually drive to.
//
// This table is the only statement anywhere of which Output Address each set
// was ever about: the names carry their channel in their spelling and nowhere
// else, which is exactly why an expander could never be another one of them.
// The table stores no value. It is deleted whole when both reasons above have
// gone, and ServoLegacyNarrowing below goes with it.
//
// Pure: names, a channel, and the shape of one load's answer. No NVS, no
// FreeRTOS, no Arduino String.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ledc_pwm.h"  // LedcChannel  --  the Output Addresses the names meant

struct ServoLegacyFieldSet {
    uint8_t channel;           // the Output Address the whole set was about
    const char* nvsOpenKey;    // "arm1_op"     -- read-only since #345
    const char* nvsCloseKey;   // "arm1_cl"     -- read-only since #345
    const char* nvsTypeKey;    // "arm1_type"   -- read-only since #345
    const char* openField;     // "arm1OpenUs"  -- /api/config, both directions
    const char* closeField;    // "arm1CloseUs" -- /api/config, both directions
    const char* typeParam;     // "arm1Type"    -- POST /api/config only
    const char* componentKey;  // "arm1"        -- components{} key on the GET
};

constexpr size_t SERVO_LEGACY_FIELD_SET_COUNT = 5;

// `inline` so the four translation units that read this table share one copy
// of it. A plain `constexpr` array in a header has internal linkage, and each
// of them was carrying its own 160 B of pointers in flash.
inline constexpr ServoLegacyFieldSet SERVO_LEGACY_FIELD_SETS[SERVO_LEGACY_FIELD_SET_COUNT] = {
    {LEDC_CH_ARM1, "arm1_op", "arm1_cl", "arm1_type", "arm1OpenUs", "arm1CloseUs", "arm1Type",
     "arm1"},
    {LEDC_CH_ARM2, "arm2_op", "arm2_cl", "arm2_type", "arm2OpenUs", "arm2CloseUs", "arm2Type",
     "arm2"},
    {LEDC_CH_AUX1, "aux1_op", "aux1_cl", "aux1_type", "aux1OpenUs", "aux1CloseUs", "aux1Type",
     "aux1"},
    {LEDC_CH_AUX2, "aux2_op", "aux2_cl", "aux2_type", "aux2OpenUs", "aux2CloseUs", "aux2Type",
     "aux2"},
    {LEDC_CH_AUX3, "aux3_op", "aux3_cl", "aux3_type", "aux3OpenUs", "aux3CloseUs", "aux3Type",
     "aux3"},
};

// Which Output a set was about, as an index into SERVO_LEGACY_FIELD_SETS, or
// SERVO_LEGACY_FIELD_SET_COUNT for a channel no set ever named.
inline size_t servoLegacyFieldSetForChannel(uint8_t channel) {
    for (size_t i = 0; i < SERVO_LEGACY_FIELD_SET_COUNT; ++i) {
        if (SERVO_LEGACY_FIELD_SETS[i].channel == channel) {
            return i;
        }
    }
    return SERVO_LEGACY_FIELD_SET_COUNT;
}

// -----------------------------------------------------------------------------
// ServoLegacyNarrowing  --  the pairs the band moved on the way in (#417)
//
// `main` clamped every endpoint to 500..2500 whatever was fitted; a row clamps
// into its component's band, and MG996R and "nothing recorded" both take
// 1000..2000. So a pair `main` held legally can arrive narrowed: 2200/800
// becomes 2000/1000, and 2200/2100 becomes 2000/2000, which no longer moves.
//
// The band stays (#286). What the operator decided on 2026-09-24 is that it
// narrows visibly: the Output says it was narrowed and what the builder's own
// numbers were, and those numbers are not deleted until the builder saves that
// Output. So a set's keys outlive the first save while its bit is set here,
// and the bit is what GET /api/servo/outputs reads the original pair from.
//
// Bit i is SERVO_LEGACY_FIELD_SETS[i]. The two pairs are what `main` stored,
// as stored; they mean nothing while the bit is clear.
// -----------------------------------------------------------------------------
struct ServoLegacyNarrowing {
    uint8_t sets;
    uint16_t openUs[SERVO_LEGACY_FIELD_SET_COUNT];
    uint16_t closeUs[SERVO_LEGACY_FIELD_SET_COUNT];
};
static_assert(SERVO_LEGACY_FIELD_SET_COUNT <= 8, "ServoLegacyNarrowing::sets is one byte");
