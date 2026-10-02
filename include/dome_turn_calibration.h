// =============================================================================
// include/dome_turn_calibration.h
//
// The Dome Bearing's calibration (#445), built from the stored dome config in
// one place: DomeTask integrates and plans turns from it, and the route, the
// Console and the Sequence Coordinator ask whether it is complete from it. It
// lives apart from include/dome_bearing.h, which stays pure and knows nothing
// of the config store.
// =============================================================================
#pragma once

#include "config_store.h"  // DomeConfig
#include "dome_bearing.h"  // DomeTurnCalibration, domeTurnCalibrated()

inline DomeTurnCalibration domeTurnCalibrationOf(const DomeConfig& cfg) {
    return {cfg.dome_neutral_us,   cfg.dome_min_pulse_us,  cfg.dome_max_pulse_us,
            cfg.dome_full_turn_ms, cfg.dome_full_turn_pct, cfg.dome_positive_turn};
}
