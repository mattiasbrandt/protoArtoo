// =============================================================================
// include/droid_part_availability.h
//
// Whether this droid can move a Part, asked of the wiring at the moment a step
// runs (#301, #356).
//
// A Part being KNOWN and a Part being DRIVEABLE HERE are separate facts. The
// first is the catalog's, compiled in as include/droid_parts.h, and it is what
// Protocol Check gates at save: authoring a step for an arm that is not wired
// yet is legal and deliberate. The second is the builder's own droid's, held in
// the Servo Output rows, and it is what decides whether anything moves.
//
// So this question is never cached from discovery. Wire the arm, let an Output
// record the Part, and the same saved step starts working with nothing
// re-authored - which only holds if the answer is recomputed every time it is
// needed, from the table as it stands.
//
// Pure: no NVS, no FreeRTOS, no Arduino String. It reads a table it is handed.
// =============================================================================

#pragma once

#include "console_module.h"     // ConsoleReason - the Availability Reason set
#include "droid_parts.h"        // the compiled id vocabulary
#include "servo_output_row.h"   // ServoOutputTable - this droid's own wiring

// A generated id that cannot be stored against an Output could never be
// claimed by one, so it would report part-not-assigned forever. The generator
// refuses such an id when it writes the table; this catches the other
// direction, where the row model shrinks under a catalog that already fits.
static_assert(DROID_PART_ID_MAX_LEN <= SERVO_OUTPUT_PART_ID_MAX,
              "a generated Part id is longer than the Part field on a Servo Output row");

// -----------------------------------------------------------------------------
// droidPartAvailabilityReason()
// The Availability Reason this droid reports for a Part, right now.
//
//   CONSOLE_REASON_NONE               an Output claims it; it can move
//   CONSOLE_REASON_PART_NOT_ASSIGNED  a known Part no Output claims; inert, and
//                                     the rest of the sequence carries on
//   CONSOLE_REASON_UNKNOWN_ARGUMENT   not a Part this build models at all
//
// The last case is deliberately not part-not-assigned. Reporting an id the
// catalog never declared as "no Output drives it" would dress a step that
// outlived its catalog up as unwired hardware, and send a builder to the bench
// to fix a wiring fault they do not have. Protocol Check gates the id at save,
// so reaching here means the stored step and the image disagree.
// -----------------------------------------------------------------------------
inline ConsoleReason droidPartAvailabilityReason(const ServoOutputTable& table,
                                                 const char* partId) {
    if (!droidPartIdIsKnown(partId)) {
        return CONSOLE_REASON_UNKNOWN_ARGUMENT;
    }
    // Clamped the way every other reader of this table clamps it: a stored
    // count that outran the row array must not walk off the end of it.
    const uint8_t count =
        (table.count <= SERVO_OUTPUT_ROW_MAX) ? table.count : SERVO_OUTPUT_ROW_MAX;
    for (uint8_t row = 0; row < count; ++row) {
        if (servoOutputDrivesPart(table.rows[row], partId)) {
            return CONSOLE_REASON_NONE;
        }
    }
    return CONSOLE_REASON_PART_NOT_ASSIGNED;
}
