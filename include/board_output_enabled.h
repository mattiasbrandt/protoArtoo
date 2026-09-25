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
// boardOutputTickAdoptedLight()
// The second half of reading `main`'s one lit wire onto its row (#417).
//
// `main` drove its strip from the stored slot alone (`aux_led_pin`) and never
// asked the wire's own tick; this firmware drives a strip only on a wire that
// is ticked in AND names a Light Type (outputWireStripDriven()). So the row
// half of the adoption on its own turns a strip `main` was lighting dark
// whenever that wire had been left unticked. Ticking it here is what keeps
// "a controller that was lighting a wire keeps lighting it" true.
//
// Only where the loader actually adopted - the same gate as the row half, so a
// wire whose row already carries the builder's own answer is not ticked behind
// their back. Before configCacheReplace(), so the tick is in the snapshot the
// first save writes; configSave() removes the retired keys only once that has
// landed.
// -----------------------------------------------------------------------------
inline void boardOutputTickAdoptedLight(const ServoOutputRepairReport& report,
                                        SystemConfig* system) {
    if (system == nullptr || !report.litAdopted || report.litOutput >= BOARD_OUTPUT_COUNT) {
        return;
    }
    system->*BOARD_OUTPUT_ENABLED[report.litOutput].enabled = true;
}
