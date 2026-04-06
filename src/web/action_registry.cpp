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
    //  id                            name                                display_name           domain    description                                              safety_critical
    { DRIVE_ACTION_SPEED,             "drive.action.speed",               "Speed",               "drive",  "Forward/reverse drive speed (analog axis)",            false },
    { DRIVE_ACTION_STEER,             "drive.action.steer",               "Steer",               "drive",  "Left/right steering (analog axis)",                    false },
    { DOME_ACTION_SPEED,              "dome.action.set-speed",            "Dome Speed",          "dome",   "Dome rotation speed (analog axis)",                    false },
    { DRIVE_ACTION_SPEED_LIMIT,       "drive.action.speed-limit",         "Speed Limit",         "drive",  "Maximum drive speed as a fraction of full range",      false },
    { SYSTEM_ACTION_OP_MODE,          "system.action.set-mode",           "Set Mode",            "system", "Switch between stationary and driving mode",            false },
    { SERVO_ACTION_ARM1_TOGGLE,       "servo.action.toggle-arm1",         "ARM1 Toggle",         "servo",  "Toggle arm 1 servo between open and closed",           false },
    { SERVO_ACTION_ARM2_TOGGLE,       "servo.action.toggle-arm2",         "ARM2 Toggle",         "servo",  "Toggle arm 2 servo between open and closed",           false },
    { SERVO_ACTION_AUX1_TOGGLE,       "servo.action.toggle-aux1",         "AUX1 Toggle",         "servo",  "Toggle aux 1 servo between open and closed",           false },
    { SERVO_ACTION_AUX2_TOGGLE,       "servo.action.toggle-aux2",         "AUX2 Toggle",         "servo",  "Toggle aux 2 servo between open and closed",           false },
    { SERVO_ACTION_AUX3_TOGGLE,       "servo.action.toggle-aux3",         "AUX3 Toggle",         "servo",  "Toggle aux 3 servo between open and closed",           false },
    { DOME_ACTION_MARCDUINO_SEQ,      "dome.action.marcduino-sequence",   "Marcduino Sequence",  "dome",   "Trigger a numbered Marcduino sequence on the dome",    false },
    { DOME_ACTION_MARCDUINO_CMD,      "dome.action.marcduino-command",    "Marcduino Command",   "dome",   "Send a specific Marcduino command string to the dome", false },
    { SOUND_ACTION_RANDOM_GENERAL,    "sound.action.random-general",      "Random General",      "sound",  "Play one random track from configured general range",  false },
    { SOUND_ACTION_RANDOM_CHATTY,     "sound.action.random-chatty",       "Random Chatty",       "sound",  "Play one random track from configured chatty range",   false },
    { SOUND_ACTION_RANDOM_HAPPY,      "sound.action.random-happy",        "Random Happy",        "sound",  "Play one random track from configured happy range",    false },
    { SOUND_ACTION_RANDOM_PROCESSING, "sound.action.random-processing",   "Random Processing",   "sound",  "Play one random track from configured processing range",false },
    { SOUND_ACTION_RANDOM_SAD,        "sound.action.random-sad",          "Random Sad",          "sound",  "Play one random track from configured sad range",      false },
    { SOUND_ACTION_RANDOM_SENTIMENTAL,"sound.action.random-sentimental",  "Random Sentimental",  "sound",  "Play one random track from configured sentimental range",false },
    { SOUND_ACTION_RANDOM_HUMMING,    "sound.action.random-humming",      "Random Humming",      "sound",  "Play one random track from configured humming range",  false },
    { SOUND_ACTION_RANDOM_SCREAM,     "sound.action.random-scream",       "Random Scream",       "sound",  "Play one random track from configured scream range",   false },
    { SOUND_ACTION_RANDOM_SURPRISED,  "sound.action.random-surprised",    "Random Surprised",    "sound",  "Play one random track from configured surprised range",false },
    { SOUND_ACTION_RANDOM_ALERT,      "sound.action.random-alert",        "Random Alert",        "sound",  "Play one random track from configured alert range",    false },
    { SOUND_ACTION_RANDOM_SNARKY,       "sound.action.random-snarky",         "Random Snarky",         "sound",  "Play one random track from configured snarky range",     false },
    { SOUND_ACTION_RANDOM_WHISTLE,    "sound.action.random-whistle",      "Random Whistle",      "sound",  "Play one random track from configured whistle range",  false },
    { SYSTEM_ACTION_ESTOP,            "system.action.estop",              "Emergency Stop",      "system", "Immediately stop all drive output and latch estop",    true  },
    { SYSTEM_ACTION_SLEEP_TOGGLE,     "system.action.sleep-toggle",       "Sleep Toggle",        "system", "Toggle cosmetic sleep mode while keeping drive safety active", false },
    { DOME_ACTION_SEQ,                "dome.action.dome-sequence",        "Dome Sequence",       "dome",   "Trigger a dome panel/light sequence by number",        false },
};
// clang-format on

const size_t ACTION_REGISTRY_SIZE = sizeof(ACTION_REGISTRY) / sizeof(ACTION_REGISTRY[0]);
