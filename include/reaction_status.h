// =============================================================================
// include/reaction_status.h
//
// What ReactionTask publishes about each Reaction, for the RC diagnostics to
// read (ADR 0053, #450). A header of its own because RobotState holds it and
// the diagnostics snapshot copies it, and the snapshot is built natively too.
//
// Pure: no Arduino, no FreeRTOS.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

// What one trigger slot's Reaction is doing (ADR 0053, #450), slot for slot
// with rcTriggerSlotsCopy() (include/config_store.h; ReactionTask asserts the
// counts agree). `source` is RC_BINDING_NONE
// in a slot that holds no Reaction. This is how "my Reaction never fires" gets
// an answer on the RC page: it is not armed (`availability`), or it was held
// back while the droid was driving (`refusals`).
struct ReactionStatus {
    uint8_t source;        // RcBindingSource
    uint8_t channel;
    uint8_t availability;  // ReactionAvailability (include/reaction_evaluator.h)
    uint16_t fires;
    uint16_t refusals;
};
constexpr size_t REACTION_STATUS_SLOTS = 11;
