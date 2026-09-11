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
//   - data/servo.js and data/setup.js still speak the old field names on
//     /api/config, in both directions. The C1 wave rebuilds those pages onto
//     the rows; until then the API answers them FROM the rows, so the browser
//     sees the number the droid will actually drive to.
//
// This table is the only statement anywhere of which Output Address each set
// was ever about: the names carry their channel in their spelling and nowhere
// else, which is exactly why an expander could never be another one of them.
// Nothing here stores a value. It is deleted whole when both reasons above
// have gone.
//
// Pure: names and a channel. No NVS, no FreeRTOS, no Arduino String.
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
