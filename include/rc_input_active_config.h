// =============================================================================
// include/rc_input_active_config.h
//
// Lightweight boot-active RC configuration shared by startup planning, the RC
// task adapter, and RC reporting. It intentionally has no dependency on NVS,
// SystemConfig, FreeRTOS, or hardware so the pure Step Core stays isolated.
// =============================================================================
#pragma once

#include <stdint.h>

struct RcInputActiveConfig {
    uint8_t mode;
    bool useCh2;
    bool enableRc[6];
    bool enableDome;
    bool enableArm1;
    bool enableArm2;
    bool enableSound;
};
