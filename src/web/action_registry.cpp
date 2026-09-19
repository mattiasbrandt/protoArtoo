// =============================================================================
// src/web/action_registry.cpp
//
// Compile-time table of all RC-bindable robot actions.
// Every non-NONE RobotActionId value must appear exactly once.
//
// Entry order matches the RobotActionId enum declaration in rc_mapping.h.
// Names and descriptions must stay consistent with docs/action-registry.yaml.
// A row about one Output names it `{output}` and carries its stored id last:
// the name is the running board's, composed when the row is served
// (boardOutputComposeText(), include/board_outputs.h), never stored here.
// The current bindable population is universal, so the two nullable requirement
// fields use their nullptr defaults. Annotated rows must name manifest entries.
// =============================================================================

#include "../../include/action_registry.h"

// clang-format off
const ActionEntry ACTION_REGISTRY[] = {
    //  id                            name                                display_name           domain    description                                              safety_critical
    { DRIVE_ACTION_SPEED,             "drive.action.speed",               "Speed",               "drive",  "Forward and back on the feet. Bind it to a stick.",            false },
    { DRIVE_ACTION_STEER,             "drive.action.steer",               "Steer",               "drive",  "Left and right on the feet. Bind it to a stick.",                    false },
    { DOME_ACTION_SPEED,              "dome.action.set-speed",            "Dome Speed",          "dome",   "Turn the dome. Bind it to a stick.",                    false },
    { SYSTEM_ACTION_OP_MODE,          "system.action.set-mode",           "Set Mode",            "system", "Switch between Stationary and Driving. Stationary locks the feet.",            false },
    { SERVO_ACTION_ARM1_TOGGLE,     "servo.action.toggle-arm1",         "{output} Toggle",     "servo",  "Open or close the part on {output}.", false, nullptr, nullptr, "arm1" },
    { SERVO_ACTION_ARM2_TOGGLE,     "servo.action.toggle-arm2",         "{output} Toggle",     "servo",  "Open or close the part on {output}.", false, nullptr, nullptr, "arm2" },
    { SERVO_ACTION_AUX1_TOGGLE,     "servo.action.toggle-aux1",         "{output} Toggle",     "servo",  "Open or close the part on {output}.", false, nullptr, nullptr, "aux1" },
    { SERVO_ACTION_AUX2_TOGGLE,     "servo.action.toggle-aux2",         "{output} Toggle",     "servo",  "Open or close the part on {output}.", false, nullptr, nullptr, "aux2" },
    { SERVO_ACTION_AUX3_TOGGLE,     "servo.action.toggle-aux3",         "{output} Toggle",     "servo",  "Open or close the part on {output}.", false, nullptr, nullptr, "aux3" },
    { DOME_ACTION_MARCDUINO_SEQ,      "dome.action.marcduino-sequence",   "Marcduino Sequence",  "dome",   "Play a numbered body sequence, usually SE30 to SE36.", false },
    { DOME_ACTION_MARCDUINO_CMD,      "dome.action.marcduino-command",    "Marcduino Command",   "dome",   "Send one Marcduino command to the dome.", false },
    { SOUND_ACTION_RANDOM_GENERAL,    "sound.action.random-general",      "Random General",      "sound",  "Play a random General sound.",  false },
    { SOUND_ACTION_RANDOM_CHATTY,     "sound.action.random-chatty",       "Random Chatty",       "sound",  "Play a random Chatty sound.",   false },
    { SOUND_ACTION_RANDOM_HAPPY,      "sound.action.random-happy",        "Random Happy",        "sound",  "Play a random Happy sound.",    false },
    { SOUND_ACTION_RANDOM_PROCESSING, "sound.action.random-processing",   "Random Processing",   "sound",  "Play a random Processing sound.",false },
    { SOUND_ACTION_RANDOM_SAD,        "sound.action.random-sad",          "Random Sad",          "sound",  "Play a random Sad sound.",      false },
    { SOUND_ACTION_RANDOM_SENTIMENTAL,"sound.action.random-sentimental",  "Random Sentimental",  "sound",  "Play a random Sentimental sound.",false },
    { SOUND_ACTION_RANDOM_HUMMING,    "sound.action.random-humming",      "Random Humming",      "sound",  "Play a random Humming sound.",  false },
    { SOUND_ACTION_RANDOM_SCREAM,     "sound.action.random-scream",       "Random Scream",       "sound",  "Play a random Scream sound.",   false },
    { SOUND_ACTION_RANDOM_SURPRISED,  "sound.action.random-surprised",    "Random Surprised",    "sound",  "Play a random Surprised sound.",false },
    { SOUND_ACTION_RANDOM_ALERT,      "sound.action.random-alert",        "Random Alert",        "sound",  "Play a random Alert sound.",    false },
    { SOUND_ACTION_RANDOM_SNARKY,       "sound.action.random-snarky",         "Random Snarky",         "sound",  "Play a random Snarky sound.",     false },
    { SOUND_ACTION_RANDOM_WHISTLE,    "sound.action.random-whistle",      "Random Whistle",      "sound",  "Play a random Whistle sound.",  false },
    { SYSTEM_ACTION_ESTOP,            "system.action.estop",              "Emergency Stop",      "system", "Stop the feet now and latch the estop.",    true  },
    { SYSTEM_ACTION_SLEEP_TOGGLE,     "system.action.sleep-toggle",       "Sleep Toggle",        "system", "Sleep or wake. Drive stays awake either way.", false },
    { DOME_ACTION_SEQ,                "dome.action.dome-sequence",        "Dome Sequence",       "dome",   "Play a dome show by name, like DM:FLUTTER. The Sequences page lists them all.",  false },
    { DROID_SEQ_SCREAM,               "dome.action.droid-sequence-scream", "Scream",              "dome",   "SE01. A scream, and the body and dome join in.", false },
    { DROID_SEQ_WAVE,                 "dome.action.droid-sequence-wave",   "Wave",                "dome",   "SE02. A body wave, and the dome joins in.", false },
    { DROID_SEQ_FAST_WAVE,            "dome.action.droid-sequence-fast-wave", "Fast Wave",         "dome",   "SE03. A fast wave, and the dome joins in.", false },
    { DROID_SEQ_OPEN_WAVE,            "dome.action.droid-sequence-open-wave", "Open Wave",         "dome",   "SE04. An open wave, and the dome joins in.", false },
    { DROID_SEQ_BEEP_CANTINA,         "dome.action.droid-sequence-beep-cantina", "Beep Cantina",  "dome",   "SE05. Short Cantina with a body wave, and the dome joins in.", false },
    { DROID_SEQ_FAINT,                "dome.action.droid-sequence-faint",  "Faint",               "dome",   "SE06. A faint: the body parks, and the dome joins in.", false },
    { DROID_SEQ_CANTINA,              "dome.action.droid-sequence-cantina", "Cantina Dance",     "dome",   "SE07. Long Cantina with a body wave, and the dome joins in.", false },
    { DROID_SEQ_LEIA,                 "dome.action.droid-sequence-leia",   "Leia Message",        "dome",   "SE08. The Leia message, and the dome joins in.", false },
    { DROID_SEQ_DISCO,                "dome.action.droid-sequence-disco",  "Disco",               "dome",   "SE09. Disco music with a body wave, and the dome joins in.", false },
    { DROID_SEQ_SCREAMS,              "dome.action.droid-sequence-screams", "Screams",           "dome",   "SE15. Screams from the body, and the dome does its part.", false },
    { DROID_SEQ_WIGGLE,               "dome.action.droid-sequence-wiggle", "Panel Wiggle",        "dome",   "SE16. A body wave, and the dome joins in.", false },
    { DRIVE_ACTION_SPEED_PRESET_CYCLE,   "drive.action.speed-preset-cycle", "Speed Preset Cycle",  "drive",  "Step the speed preset: Slow, Normal, Turbo, and round again.",      false },
};
// clang-format on

const size_t ACTION_REGISTRY_SIZE = sizeof(ACTION_REGISTRY) / sizeof(ACTION_REGISTRY[0]);

