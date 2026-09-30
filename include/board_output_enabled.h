// =============================================================================
// include/board_output_enabled.h
//
// Which stored tick says an Output is wired.
//
// include/board_outputs.h is pure - it knows the Outputs and what each board
// prints beside them, and nothing about how config is stored. SystemConfig
// holds the wired ticks under five fixed names. This header is the one place
// the two are paired, checked against BOARD_OUTPUTS' own ids so a tick can
// never be read for the wrong Output.
//
// It was private to src/web/api_config.cpp until #413. A lit wire is an Output
// that is wired AND carries a Light Type, so the task that drives the strips
// and the status frame that reports them both need this answer, and a second
// copy of the pairing is exactly the kind of table that drifts.
// =============================================================================
#pragma once

#include <stddef.h>

#include "board_outputs.h"  // BOARD_OUTPUTS, BOARD_OUTPUT_COUNT
#include "config_store.h"   // SystemConfig - where the wired ticks are stored

struct BoardOutputEnabledField {
    const char* id;
    bool SystemConfig::*enabled;
};

inline constexpr BoardOutputEnabledField BOARD_OUTPUT_ENABLED[] = {
    {"arm1", &SystemConfig::enable_arm1},
    {"arm2", &SystemConfig::enable_arm2},
    {"aux1", &SystemConfig::enable_aux1},
    {"aux2", &SystemConfig::enable_aux2},
    {"aux3", &SystemConfig::enable_aux3},
};

constexpr bool boardOutputEnabledFieldsAlign() {
    if (sizeof(BOARD_OUTPUT_ENABLED) / sizeof(BOARD_OUTPUT_ENABLED[0]) != BOARD_OUTPUT_COUNT) {
        return false;
    }
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        if (!board_outputs_detail::equals(BOARD_OUTPUT_ENABLED[i].id, BOARD_OUTPUTS[i].id)) {
            return false;
        }
    }
    return true;
}
static_assert(boardOutputEnabledFieldsAlign(),
              "BOARD_OUTPUT_ENABLED must list BOARD_OUTPUTS' ids, in its order");

// Whether the Output at `index` in BOARD_OUTPUTS is ticked as wired. An index
// past the table reads as not wired, so a caller that miscounts gets the safe
// answer rather than a neighbour's tick.
inline bool boardOutputIsWired(const SystemConfig& system, size_t index) {
    if (index >= BOARD_OUTPUT_COUNT) {
        return false;
    }
    return system.*BOARD_OUTPUT_ENABLED[index].enabled;
}

// -----------------------------------------------------------------------------
// boardOutputTickFollowsParts()
// An Output with a Part on it is wired, and one with none is free: there is no
// separate wired switch any more (CONTEXT.md "Wiring"; operator, 2026-09-29 on
// #411: "it was meant to define and wire a body part/panel to a output").
//
// The tick stays the stored answer every consumer already reads - ServoTask and
// AuxLedTask at start, the RC mapper, the status frame - so rather than each of
// them learning to count Parts, every board Output's tick is set from how many
// Parts it holds (configCacheTicksFollowParts()): at start, before anything
// reads it, and in every config commit, after the Parts have moved. No door
// writes a tick on its own: a Part move and a row's `parts` move it, a row's
// `wired` yields to the Parts, and the Console's enableArm1..enableAux3 are
// refused (configApply()). It is still read at start, so a Part put on a free
// Output moves from the next start (ADR 0027).
//
// `output` is one of BOARD_OUTPUTS, or nullptr for an Output the board does not
// declare - an expander's channel, which has no tick and is left alone.
// -----------------------------------------------------------------------------
inline void boardOutputTickFollowsParts(SystemConfig* system, const BoardOutput* output,
                                        uint8_t partCount) {
    if (system == nullptr || output == nullptr) {
        return;
    }
    const size_t index = (size_t)(output - BOARD_OUTPUTS);
    if (index >= BOARD_OUTPUT_COUNT) {
        return;
    }
    system->*BOARD_OUTPUT_ENABLED[index].enabled = partCount > 0;
}
