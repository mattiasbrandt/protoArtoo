// =============================================================================
// src/web/action_registry.cpp
//
// Compile-time table of all RC-bindable robot actions.
// Every non-NONE RobotActionId value must appear exactly once.
//
// Entry order matches the RobotActionId enum declaration in rc_mapping.h.
// Names and descriptions must stay consistent with docs/action-registry.yaml.
// =============================================================================

#include "../../include/action_registry.h"

// clang-format off
const ActionEntry ACTION_REGISTRY[] = {
    //  id                          name                               display_name         domain    description                                              safety_critical
    { DRIVE_ACTION_SPEED,         "drive.action.speed",              "Speed",             "drive",  "Forward/reverse drive speed (analog axis)",            false },
    { DRIVE_ACTION_STEER,         "drive.action.steer",              "Steer",             "drive",  "Left/right steering (analog axis)",                    false },
    { DRIVE_ACTION_SPEED_LIMIT,   "drive.action.speed-limit",        "Speed Limit",       "drive",  "Maximum drive speed as a fraction of full range",      false },
    { DOME_ACTION_SPEED,          "dome.action.speed",               "Dome Speed",        "dome",   "Dome rotation speed (analog axis)",                    false },
    { DOME_ACTION_MARCDUINO_SEQ,  "dome.action.marcduino-sequence",  "Marcduino Sequence","dome",   "Trigger a numbered Marcduino sequence on the dome",    false },
    { DOME_ACTION_MARCDUINO_CMD,  "dome.action.marcduino-command",   "Marcduino Command", "dome",   "Send a specific Marcduino command string to the dome", false },
    { DOME_ACTION_SEQ,            "dome.action.dome-sequence",       "Dome Sequence",     "dome",   "Trigger a dome panel/light sequence by number",        false },
    { SERVO_ACTION_ARM1_TOGGLE,   "servo.action.toggle-arm1",        "Toggle Arm 1",      "servo",  "Toggle arm 1 servo between open and closed",           false },
    { SERVO_ACTION_ARM2_TOGGLE,   "servo.action.toggle-arm2",        "Toggle Arm 2",      "servo",  "Toggle arm 2 servo between open and closed",           false },
    { SERVO_ACTION_AUX1_TOGGLE,   "servo.action.toggle-aux1",        "Toggle Aux 1",      "servo",  "Toggle aux 1 servo between open and closed",           false },
    { SERVO_ACTION_AUX2_TOGGLE,   "servo.action.toggle-aux2",        "Toggle Aux 2",      "servo",  "Toggle aux 2 servo between open and closed",           false },
    { SERVO_ACTION_AUX3_TOGGLE,   "servo.action.toggle-aux3",        "Toggle Aux 3",      "servo",  "Toggle aux 3 servo between open and closed",           false },
    { SYSTEM_ACTION_OP_MODE,      "system.action.set-mode",          "Set Mode",          "system", "Switch between stationary and driving mode",           false },
    { SYSTEM_ACTION_ESTOP,        "system.action.estop",             "Emergency Stop",    "system", "Immediately stop all drive output and latch estop",    true  },
};
// clang-format on

const size_t ACTION_REGISTRY_SIZE = sizeof(ACTION_REGISTRY) / sizeof(ACTION_REGISTRY[0]);
