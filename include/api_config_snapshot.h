// =============================================================================
// include/api_config_snapshot.h
//
// ConfigSnapshot — a plain-data copy of all NVS-backed config fields from
// RobotState, captured under portMUX so the JSON builder is FreeRTOS-free.
//
// captureConfigSnapshot(): takes the critical section, copies cfg_* fields.
//   Defined in src/web/api_config.cpp (requires FreeRTOS).
//
// populateConfigJson(): pure function — no global state, no FreeRTOS.
//   Builds an ArduinoJson document from the snapshot.
//   Returns false if any RC binding format call fails (caller sends 500).
//   Defined in src/web/api_config.cpp.
// =============================================================================
#pragma once

#include <ArduinoJson.h>

#include "rc_mapping.h"
#include "robot_state.h"

// All NVS-backed config fields, copied out of RobotState under portMUX.
struct ConfigSnapshot {
    int16_t speedLimitMax;
    uint32_t webDriveTimeoutMs;
    bool ch8ModeLock;
    bool stationary;
    uint8_t logLevel;
    RcInputMode rcInputMode;

    bool enableArm1;
    bool enableArm2;
    bool enableAux1;
    bool enableAux2;
    bool enableAux3;
    bool enableDome;
    bool enableRcCh1;
    bool enableRcCh2;
    bool enableRcCh3;
    bool enableRcCh4;
    bool enableRcCh5;
    bool enableRcCh6;
    bool enableS1Hoverboard;
    bool enableS2Sound;
    bool enableS3DomeCtrl;

    uint16_t domeNeutralUs;
    uint16_t domeMinPulseUs;
    uint16_t domeMaxPulseUs;
    uint8_t domeSpeedLimitPct;

    ServoComponentType arm1Type;
    ServoComponentType arm2Type;
    ServoComponentType aux1Type;
    ServoComponentType aux2Type;
    ServoComponentType aux3Type;

    RcBindingConfig rcPwmDriveSpeed;
    RcBindingConfig rcPwmDriveSteer;
    RcBindingConfig rcPwmDriveLimit;
    RcBindingConfig rcPwmDomeSpeed;
    RcBindingConfig rcPwmArm1;
    RcBindingConfig rcPwmArm2;
    RcBindingConfig rcPwmSound;

    RcBindingConfig rcSbusDriveSpeed;
    RcBindingConfig rcSbusDriveSteer;
    RcBindingConfig rcSbusDriveLimit;
    RcBindingConfig rcSbusDomeSpeed;
    RcBindingConfig rcSbusArm1;
    RcBindingConfig rcSbusArm2;
    RcBindingConfig rcSbusSound;

    RcTriggerBinding rcArm1;
    RcTriggerBinding rcArm2;
    RcTriggerBinding rcAux1;
    RcTriggerBinding rcAux2;
    RcTriggerBinding rcAux3;
    RcTriggerBinding rcSound;
    RcTriggerBinding rcOpmode;
    RcTriggerBinding rcFree0;
    RcTriggerBinding rcFree1;
    RcTriggerBinding rcFree2;
    RcTriggerBinding rcFree3;
};

// Copies robotState.cfg_* into *out under portMUX critical section.
// Defined in src/web/api_config.cpp.
void captureConfigSnapshot(ConfigSnapshot* out);

// Populates doc from snap. Pure: no globals, no FreeRTOS.
// Returns false if any binding string format fails.
// Defined in src/web/api_config.cpp.
bool populateConfigJson(JsonDocument& doc, const ConfigSnapshot& snap);
