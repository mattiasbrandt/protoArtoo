// =============================================================================
// src/console/console_catalog.cpp
//
// Auto-generated from docs/action-registry.yaml by tools/generate_console_catalog.py
// DO NOT EDIT MANUALLY
//
// Operation Catalog - runtime table mapping operation names to descriptors,
// parameter schemas, availability metadata, and executor references.
// Help text (description, display_name) is stored in LittleFS.
// =============================================================================

#include "console_catalog.h"
#include <string.h>

// Forward declarations for executor references (will be resolved by link-time)
// extern void driveArbiterSubmit(void);
// extern void soundCommandExecutor(void);
// etc. - these are reference strings, not function pointers

// =============================================================================
// Parameter Descriptors
// =============================================================================

static const ConsoleParamDescriptor g_params_drive_action_move[] = {
    {"speed", "int16", "-1000", "1000", true},
    {"steer", "int16", "-1000", "1000", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed[] = {
    {"value", "float", "-1", "1.0", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_steer[] = {
    {"value", "float", "-1", "1.0", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_slow[] = {
    {"preset", "string", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_normal[] = {
    {"preset", "string", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_turbo[] = {
    {"preset", "string", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_move[] = {
    {"speed", "float", "-1.0", "1.0", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_api_list_builtin_sequences[] = {
    {"name", "string", NULL, NULL, false},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_api_get_sequence[] = {
    {"name", "string", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_delete_sequence[] = {
    {"name", "string", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_test_sequence[] = {
    {"name", "string", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_play_track[] = {
    {"track", "uint16", "1", "999", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_api_get_catalog[] = {
    {"bank", "uint8", "1", "6", false},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_api_play_banked[] = {
    {"bank", "uint8", "1", "6", true},
    {"page", "string", NULL, NULL, true},
    {"index", "uint16", "1", "65535", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_mood_map[] = {
    {"quiet", "uint16", "0", "4095", true},
    {"mid", "uint16", "0", "4095", true},
    {"full", "uint16", "0", "4095", true},
    {"awakeplus", "uint16", "0", "4095", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_category_range[] = {
    {"lo_key", "string", NULL, NULL, true},
    {"hi_key", "string", NULL, NULL, true},
    {"lo", "uint16", NULL, NULL, true},
    {"hi", "uint16", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_volume[] = {
    {"volume", "uint8", "0", "30", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_volume[] = {
    {"volume", "uint8", "0", "30", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_mood_category_map[] = {
    {"quiet", "uint16", "0", "4095", true},
    {"mid", "uint16", "0", "4095", true},
    {"full", "uint16", "0", "4095", true},
    {"awakeplus", "uint16", "0", "4095", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_open[] = {
    {"target", "string", "arm1", "arm2", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_close[] = {
    {"target", "string", "arm1", "arm2", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_set_position[] = {
    {"target", "string", "arm1", "arm2", true},
    {"position_us", "uint16", "500", "2500", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_action_led_color[] = {
    {"r", "uint8", "0", "255", true},
    {"g", "uint8", "0", "255", true},
    {"b", "uint8", "0", "255", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_action_led_effect[] = {
    {"effect", "string", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_config_led_pin[] = {
    {"aux_led_pin", "uint8", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_config_led_count[] = {
    {"aux_led_count", "uint8", "1", "255", true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_system_action_set_mood[] = {
    {"mood", "uint8", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_system_action_set_identity[] = {
    {"droidName", "string", NULL, NULL, true},
    {"mdnsUseName", "bool", NULL, NULL, false},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_rc_action_test_bindable[] = {
    {"token", "string", NULL, NULL, true},
    {NULL, NULL, NULL, NULL, false}  // terminator
};

// =============================================================================
// Complete Operation Catalog
// =============================================================================

static const ConsoleCatalogEntry g_catalogEntries[] = {
    {
        "drive.action.move",
        "action",
        "Move",
        "driveArbiterSubmit",
        NULL,  // TODO: aliases for drive.action.move
        g_params_drive_action_move,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        true,  // requires_web_control
        true,  // safety_critical
    },
    {
        "drive.action.speed",
        "action",
        "Speed",
        "driveArbiterSubmit",
        NULL,  // TODO: aliases for drive.action.speed
        g_params_drive_action_speed,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "drive.action.steer",
        "action",
        "Steer",
        "driveArbiterSubmit",
        NULL,  // TODO: aliases for drive.action.steer
        g_params_drive_action_steer,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "drive.action.speed-preset-slow",
        "action",
        "Speed Preset Slow",
        "applySpeedPresetRuntime",
        NULL,  // TODO: aliases for drive.action.speed-preset-slow
        g_params_drive_action_speed_preset_slow,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        true,  // requires_web_control
        false,  // safety_critical
    },
    {
        "drive.action.speed-preset-normal",
        "action",
        "Speed Preset Normal",
        "applySpeedPresetRuntime",
        NULL,  // TODO: aliases for drive.action.speed-preset-normal
        g_params_drive_action_speed_preset_normal,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        true,  // requires_web_control
        false,  // safety_critical
    },
    {
        "drive.action.speed-preset-turbo",
        "action",
        "Speed Preset Turbo",
        "applySpeedPresetRuntime",
        NULL,  // TODO: aliases for drive.action.speed-preset-turbo
        g_params_drive_action_speed_preset_turbo,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        true,  // requires_web_control
        false,  // safety_critical
    },
    {
        "drive.action.speed-preset-cycle",
        "action",
        "Speed Preset Cycle",
        "applySpeedPresetRuntime",
        NULL,  // TODO: aliases for drive.action.speed-preset-cycle
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "drive.status.current",
        "status",
        "Drive Status",
        "buildStatusJson",
        NULL,  // TODO: aliases for drive.status.current
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "drive.event.failsafe-triggered",
        "event",
        "Failsafe Triggered",
        "failsafeTrigger",
        NULL,  // TODO: aliases for drive.event.failsafe-triggered
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        true,  // safety_critical
    },
    {
        "drive.config.speed-limit",
        "config",
        "Speed Limit Setting",
        "configApply",
        NULL,  // TODO: aliases for drive.config.speed-limit
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.set-speed",
        "action",
        "Dome Speed",
        "domeCmdQueue",
        NULL,  // TODO: aliases for dome.action.set-speed
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.send-command",
        "action",
        "Send Dome Command",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.send-command
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.marcduino-sequence",
        "action",
        "Marcduino Sequence",
        "parseMarcduinoCommand",
        NULL,  // TODO: aliases for dome.action.marcduino-sequence
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.marcduino-command",
        "action",
        "Marcduino Command",
        "parseMarcduinoCommand",
        NULL,  // TODO: aliases for dome.action.marcduino-command
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.dome-sequence",
        "action",
        "Dome Sequence",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.dome-sequence
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-scream",
        "action",
        "Scream",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-scream
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-wave",
        "action",
        "Wave",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-wave
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-fast-wave",
        "action",
        "Fast Wave",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-fast-wave
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-open-wave",
        "action",
        "Open Wave",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-open-wave
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-beep-cantina",
        "action",
        "Beep Cantina",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-beep-cantina
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-faint",
        "action",
        "Faint",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-faint
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-cantina",
        "action",
        "Cantina Dance",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-cantina
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-leia",
        "action",
        "Leia Message",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-leia
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-disco",
        "action",
        "Disco",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-disco
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-screams",
        "action",
        "Screams",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-screams
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.droid-sequence-wiggle",
        "action",
        "Panel Wiggle",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.droid-sequence-wiggle
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.api.get-layout",
        "action",
        "Get Dome Layout",
        "domeLayoutCacheReadChunk",
        NULL,  // TODO: aliases for dome.api.get-layout
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.sequence-stop",
        "action",
        "Stop Sequence",
        "robotState.seqStopRequested",
        NULL,  // TODO: aliases for dome.action.sequence-stop
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-scream",
        "event",
        "BD:SCREAM",
        "audioQueuePlayCategory",
        NULL,  // TODO: aliases for dome.event.cue-scream
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-happy",
        "event",
        "BD:HAPPY",
        "audioQueuePlayCategory",
        NULL,  // TODO: aliases for dome.event.cue-happy
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-overload",
        "event",
        "BD:OVERLOAD",
        "audioQueuePlayCategory",
        NULL,  // TODO: aliases for dome.event.cue-overload
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-alarm",
        "event",
        "BD:ALARM",
        "audioQueuePlayCategory",
        NULL,  // TODO: aliases for dome.event.cue-alarm
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-vader",
        "event",
        "BD:VADER",
        "audioQueuePlaySlot",
        NULL,  // TODO: aliases for dome.event.cue-vader
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-rockmarch",
        "event",
        "BD:ROCKMARCH",
        "audioQueuePlaySlot",
        NULL,  // TODO: aliases for dome.event.cue-rockmarch
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-leia",
        "event",
        "BD:LEIA",
        "audioQueuePlaySlot",
        NULL,  // TODO: aliases for dome.event.cue-leia
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-cantina",
        "event",
        "BD:CANTINA",
        "audioQueuePlaySlot",
        NULL,  // TODO: aliases for dome.event.cue-cantina
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-heart",
        "event",
        "BD:HEART",
        "audioQueuePlayCategory",
        NULL,  // TODO: aliases for dome.event.cue-heart
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-hello",
        "event",
        "BD:HELLO",
        "audioQueuePlayCategory",
        NULL,  // TODO: aliases for dome.event.cue-hello
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.event.cue-reset",
        "event",
        "BD:RESET",
        "audioQueueTrackStop",
        NULL,  // TODO: aliases for dome.event.cue-reset
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.status.current",
        "status",
        "Dome Status",
        "buildStatusJson",
        NULL,  // TODO: aliases for dome.status.current
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.move",
        "action",
        "Dome Speed (Web)",
        "domeCmdQueue",
        NULL,  // TODO: aliases for dome.action.move
        g_params_dome_action_move,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.api.list-sequences",
        "action",
        "List Learned Sequences",
        "seqStoreIndexAt",
        NULL,  // TODO: aliases for dome.api.list-sequences
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.api.list-builtin-sequences",
        "action",
        "List Factory Sequences",
        "sequenceCatalogAt",
        NULL,  // TODO: aliases for dome.api.list-builtin-sequences
        g_params_dome_api_list_builtin_sequences,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.api.get-sequence",
        "action",
        "Get Learned Sequence",
        "seqStoreReadFileSlice",
        NULL,  // TODO: aliases for dome.api.get-sequence
        g_params_dome_api_get_sequence,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.save-sequence",
        "action",
        "Save Learned Sequence",
        "seqStoreSave",
        NULL,  // TODO: aliases for dome.action.save-sequence
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.delete-sequence",
        "action",
        "Memory Wipe",
        "seqStoreDelete",
        NULL,  // TODO: aliases for dome.action.delete-sequence
        g_params_dome_action_delete_sequence,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.action.test-sequence",
        "action",
        "Test Sequence",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.action.test-sequence
        g_params_dome_action_test_sequence,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.api.get-sequence-last-run",
        "action",
        "Get Last Sequence Run",
        "seqEvidenceSnapshot",
        NULL,  // TODO: aliases for dome.api.get-sequence-last-run
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.play-track",
        "action",
        "Play Track",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.play-track
        g_params_sound_action_play_track,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.play-track-scream",
        "action",
        "Play Scream",
        "audioQueuePlayTrackBanked",
        NULL,  // TODO: aliases for sound.action.play-track-scream
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.play-track-faint",
        "action",
        "Play Short Circuit",
        "audioQueuePlayTrackBanked",
        NULL,  // TODO: aliases for sound.action.play-track-faint
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.play-track-leia",
        "action",
        "Play Leia Message",
        "audioQueuePlayTrackBanked",
        NULL,  // TODO: aliases for sound.action.play-track-leia
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.play-track-cantina-short",
        "action",
        "Play Short Cantina",
        "audioQueuePlayTrackBanked",
        NULL,  // TODO: aliases for sound.action.play-track-cantina-short
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.play-track-cantina-long",
        "action",
        "Play Long Cantina",
        "audioQueuePlayTrackBanked",
        NULL,  // TODO: aliases for sound.action.play-track-cantina-long
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.play-track-sw-theme",
        "action",
        "Play Star Wars Theme",
        "audioQueuePlayTrackBanked",
        NULL,  // TODO: aliases for sound.action.play-track-sw-theme
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.play-track-imperial-march",
        "action",
        "Play Imperial March",
        "audioQueuePlayTrackBanked",
        NULL,  // TODO: aliases for sound.action.play-track-imperial-march
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.play-track-startup",
        "action",
        "Play Startup Sound",
        "audioQueuePlayTrackBanked",
        NULL,  // TODO: aliases for sound.action.play-track-startup
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.play-track-disco",
        "action",
        "Play Disco",
        "audioQueuePlayTrackBanked",
        NULL,  // TODO: aliases for sound.action.play-track-disco
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.api.get-catalog",
        "action",
        "Get Catalog",
        "audioGetCatalogEntries",
        NULL,  // TODO: aliases for sound.api.get-catalog
        g_params_sound_api_get_catalog,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.api.refresh-catalog",
        "action",
        "Refresh Catalog",
        "audioQueueRefreshCatalog",
        NULL,  // TODO: aliases for sound.api.refresh-catalog
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.api.play-banked",
        "action",
        "Play Banked Entry",
        "audioQueuePlayTrackBanked",
        NULL,  // TODO: aliases for sound.api.play-banked
        g_params_sound_api_play_banked,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.api.get-mood-map",
        "action",
        "Get Mood Category Map",
        "configCacheRead",
        NULL,  // TODO: aliases for sound.api.get-mood-map
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.set-mood-map",
        "action",
        "Set Mood Category Map",
        "audioMoodMapApply",
        NULL,  // TODO: aliases for sound.action.set-mood-map
        g_params_sound_action_set_mood_map,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.set-category-range",
        "action",
        "Set Category Range",
        "audioCategoryRangeApply",
        NULL,  // TODO: aliases for sound.action.set-category-range
        g_params_sound_action_set_category_range,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.query-status",
        "action",
        "Query Module Status",
        "audioQueueQueryStatus",
        NULL,  // TODO: aliases for sound.action.query-status
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.track-stop",
        "action",
        "Track Stop",
        "audioQueueTrackStop",
        NULL,  // TODO: aliases for sound.action.track-stop
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.quiet",
        "action",
        "Quiet",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.quiet
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.set-volume",
        "action",
        "Set Volume",
        "audioQueueSetVolume",
        NULL,  // TODO: aliases for sound.action.set-volume
        g_params_sound_action_set_volume,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.volume-up",
        "action",
        "Volume Up",
        "audioQueueSetVolume",
        NULL,  // TODO: aliases for sound.action.volume-up
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.volume-down",
        "action",
        "Volume Down",
        "audioQueueSetVolume",
        NULL,  // TODO: aliases for sound.action.volume-down
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.volume-preset-mid",
        "action",
        "Volume Preset Mid",
        "audioQueueSetVolume",
        NULL,  // TODO: aliases for sound.action.volume-preset-mid
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.volume-preset-max",
        "action",
        "Volume Preset Max",
        "audioQueueSetVolume",
        NULL,  // TODO: aliases for sound.action.volume-preset-max
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.volume-preset-min",
        "action",
        "Volume Preset Min",
        "audioQueueSetVolume",
        NULL,  // TODO: aliases for sound.action.volume-preset-min
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.dollar-command",
        "action",
        "Raw Sound Command",
        "audioQueueDollar",
        NULL,  // TODO: aliases for sound.action.dollar-command
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-on",
        "action",
        "Random Sound On",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-on
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-off",
        "action",
        "Random Sound Off",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-off
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-general",
        "action",
        "Random General",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-general
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-chatty",
        "action",
        "Random Chatty",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-chatty
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-happy",
        "action",
        "Random Happy",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-happy
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-processing",
        "action",
        "Random Processing",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-processing
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-sad",
        "action",
        "Random Sad",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-sad
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-sentimental",
        "action",
        "Random Sentimental",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-sentimental
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-humming",
        "action",
        "Random Humming",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-humming
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-scream",
        "action",
        "Random Scream",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-scream
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-surprised",
        "action",
        "Random Surprised",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-surprised
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-alert",
        "action",
        "Random Alert",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-alert
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-snarky",
        "action",
        "Random Snarky",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-snarky
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.action.random-whistle",
        "action",
        "Random Whistle",
        "audioQueuePlayTrack",
        NULL,  // TODO: aliases for sound.action.random-whistle
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.status.current",
        "status",
        "Sound Status",
        "Core 0",
        NULL,  // TODO: aliases for sound.status.current
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.volume",
        "config",
        "Volume",
        "configApply",
        NULL,  // TODO: aliases for sound.config.volume
        g_params_sound_config_volume,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.random-min",
        "config",
        "Random Min Track",
        "audioTracksApply",
        NULL,  // TODO: aliases for sound.config.random-min
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.random-max",
        "config",
        "Random Max Track",
        "audioTracksApply",
        NULL,  // TODO: aliases for sound.config.random-max
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.mood-interval-quiet",
        "config",
        "Quiet Mode Interval",
        "configApply",
        NULL,  // TODO: aliases for sound.config.mood-interval-quiet
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.mood-interval-mid",
        "config",
        "Mid-Awake Interval",
        "configApply",
        NULL,  // TODO: aliases for sound.config.mood-interval-mid
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.mood-interval-full",
        "config",
        "Full-Awake Interval",
        "configApply",
        NULL,  // TODO: aliases for sound.config.mood-interval-full
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.mood-interval-awake-plus",
        "config",
        "Awake+ Interval",
        "configApply",
        NULL,  // TODO: aliases for sound.config.mood-interval-awake-plus
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.startup-track",
        "config",
        "Startup Boot Sound Track ($B)",
        "audioTracksApply",
        NULL,  // TODO: aliases for sound.config.startup-track
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.boot-complete-track",
        "config",
        "Boot Complete System Track",
        "audioTracksApply",
        NULL,  // TODO: aliases for sound.config.boot-complete-track
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.track-assignments",
        "config",
        "Track Assignments",
        "audioTracksApply",
        NULL,  // TODO: aliases for sound.config.track-assignments
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.system-track-assignments",
        "config",
        "System Sound Assignments",
        "audioTracksApply",
        NULL,  // TODO: aliases for sound.config.system-track-assignments
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.category-ranges",
        "config",
        "Category Ranges",
        "audioCategoryRangeApply",
        NULL,  // TODO: aliases for sound.config.category-ranges
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "sound.config.mood-category-map",
        "config",
        "Mood Category Map",
        "audioMoodMapApply",
        NULL,  // TODO: aliases for sound.config.mood-category-map
        g_params_sound_config_mood_category_map,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "servo.action.open",
        "action",
        "Open",
        "servoCmdQueue",
        NULL,  // TODO: aliases for servo.action.open
        g_params_servo_action_open,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "servo.action.close",
        "action",
        "Close",
        "servoCmdQueue",
        NULL,  // TODO: aliases for servo.action.close
        g_params_servo_action_close,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "servo.action.set-position",
        "action",
        "Set Position",
        "servoCmdQueue",
        NULL,  // TODO: aliases for servo.action.set-position
        g_params_servo_action_set_position,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "servo.action.stop",
        "action",
        "Stop",
        "servoCmdQueue",
        NULL,  // TODO: aliases for servo.action.stop
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "servo.action.toggle-arm1",
        "action",
        "ARM1 Toggle",
        "servoCmdQueue",
        NULL,  // TODO: aliases for servo.action.toggle-arm1
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "servo.action.toggle-arm2",
        "action",
        "ARM2 Toggle",
        "servoCmdQueue",
        NULL,  // TODO: aliases for servo.action.toggle-arm2
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "servo.action.toggle-aux1",
        "action",
        "AUX1 Toggle",
        "servoCmdQueue",
        NULL,  // TODO: aliases for servo.action.toggle-aux1
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "servo.action.toggle-aux2",
        "action",
        "AUX2 Toggle",
        "servoCmdQueue",
        NULL,  // TODO: aliases for servo.action.toggle-aux2
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "servo.action.toggle-aux3",
        "action",
        "AUX3 Toggle",
        "servoCmdQueue",
        NULL,  // TODO: aliases for servo.action.toggle-aux3
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "servo.status.current",
        "status",
        "Servo Status",
        "buildStatusJson",
        NULL,  // TODO: aliases for servo.status.current
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "aux.action.led-color",
        "action",
        "LED Color",
        "auxLedQueue",
        NULL,  // TODO: aliases for aux.action.led-color
        g_params_aux_action_led_color,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "aux.action.led-effect",
        "action",
        "LED Effect",
        "auxLedQueue",
        NULL,  // TODO: aliases for aux.action.led-effect
        g_params_aux_action_led_effect,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "aux.status.led-state",
        "status",
        "AUX LED State",
        "buildStatusJson",
        NULL,  // TODO: aliases for aux.status.led-state
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "aux.config.led-pin",
        "config",
        "LED Header Selection",
        "configApply",
        NULL,  // TODO: aliases for aux.config.led-pin
        g_params_aux_config_led_pin,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "aux.config.led-count",
        "config",
        "LED Count",
        "configApply",
        NULL,  // TODO: aliases for aux.config.led-count
        g_params_aux_config_led_count,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.set-mode",
        "action",
        "Set Mode",
        "commandedSetStationary",
        NULL,  // TODO: aliases for system.action.set-mode
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.estop",
        "action",
        "Emergency Stop",
        "failsafeTrigger",
        NULL,  // TODO: aliases for system.action.estop
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        true,  // safety_critical
    },
    {
        "system.action.estop-clear",
        "action",
        "Clear E-Stop",
        "failsafeClearEstop",
        NULL,  // TODO: aliases for system.action.estop-clear
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        true,  // safety_critical
    },
    {
        "system.action.enable-web-control",
        "action",
        "Enable Web Control",
        "commandedSetWebControl",
        NULL,  // TODO: aliases for system.action.enable-web-control
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.disable-web-control",
        "action",
        "Disable Web Control",
        "commandedSetWebControl",
        NULL,  // TODO: aliases for system.action.disable-web-control
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.reboot",
        "action",
        "Reboot",
        "requestSystemRestart",
        NULL,  // TODO: aliases for system.action.reboot
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.set-mood",
        "action",
        "Set Mood",
        "applyMood",
        NULL,  // TODO: aliases for system.action.set-mood
        g_params_system_action_set_mood,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.sleep",
        "action",
        "Sleep",
        "commandedSetSleep",
        NULL,  // TODO: aliases for system.action.sleep
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        true,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.wake",
        "action",
        "Wake",
        "commandedSetSleep",
        NULL,  // TODO: aliases for system.action.wake
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        true,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.sleep-toggle",
        "action",
        "Sleep / Wake Toggle",
        "commandedSetSleep",
        NULL,  // TODO: aliases for system.action.sleep-toggle
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.event.drives-engaged",
        "event",
        "Drives Engaged",
        "none",
        NULL,  // TODO: aliases for system.event.drives-engaged
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.event.dome-enabled",
        "event",
        "Dome Enabled",
        "none",
        NULL,  // TODO: aliases for system.event.dome-enabled
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.event.boot-complete",
        "event",
        "Boot Complete",
        "none",
        NULL,  // TODO: aliases for system.event.boot-complete
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.status.sleep-mode",
        "status",
        "Sleep Mode",
        "buildStatusJson",
        NULL,  // TODO: aliases for system.status.sleep-mode
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.status.mood",
        "status",
        "Mood",
        "buildStatusJson",
        NULL,  // TODO: aliases for system.status.mood
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.mood",
        "config",
        "Active Mood",
        "configApply",
        NULL,  // TODO: aliases for system.config.mood
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_arm1",
        "config",
        "ARM1",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_arm1
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_arm2",
        "config",
        "ARM2",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_arm2
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_aux1",
        "config",
        "AUX1",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_aux1
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_aux2",
        "config",
        "AUX2",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_aux2
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_aux3",
        "config",
        "AUX3",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_aux3
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_dome_esc",
        "config",
        "Dome Motor",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_dome_esc
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_rc_ch1",
        "config",
        "RC CH1",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_rc_ch1
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_rc_ch2",
        "config",
        "RC CH2",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_rc_ch2
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_rc_ch3",
        "config",
        "RC CH3",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_rc_ch3
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_rc_ch4",
        "config",
        "RC CH4",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_rc_ch4
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_rc_ch5",
        "config",
        "RC CH5",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_rc_ch5
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_rc_ch6",
        "config",
        "RC CH6",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_rc_ch6
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_drive",
        "config",
        "S1 — Hoverboard",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_drive
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_audio",
        "config",
        "S2 — Sound",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_audio
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.config.enable_protor2link",
        "config",
        "S3 — Dome Control",
        "configApply",
        NULL,  // TODO: aliases for system.config.enable_protor2link
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.status.health",
        "status",
        "Health",
        "buildStatusJson",
        NULL,  // TODO: aliases for system.status.health
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.status.dashboard-health",
        "status",
        "Dashboard Health Signals",
        "buildStatusJson",
        NULL,  // TODO: aliases for system.status.dashboard-health
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.status.logs",
        "status",
        "Logs",
        "buildStatusJson",
        NULL,  // TODO: aliases for system.status.logs
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.status.wifi",
        "status",
        "WiFi",
        "buildStatusJson",
        NULL,  // TODO: aliases for system.status.wifi
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.status.serial-link",
        "status",
        "Dome Serial Link",
        "buildSerialJson",
        NULL,  // TODO: aliases for dome.status.serial-link
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.api.get-identity",
        "action",
        "Get Droid Identity",
        "sendIdentityResponse",
        NULL,  // TODO: aliases for system.api.get-identity
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.set-identity",
        "action",
        "Set Droid Identity",
        "configCacheApply",
        NULL,  // TODO: aliases for system.action.set-identity
        g_params_system_action_set_identity,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.api.get-profiler",
        "action",
        "Get Heap Profiler Snapshot",
        "buildProfilerJson",
        NULL,  // TODO: aliases for system.api.get-profiler
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.profiler-trace-start",
        "action",
        "Start Heap Trace",
        "heap_trace_start",
        NULL,  // TODO: aliases for system.action.profiler-trace-start
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.profiler-trace-stop",
        "action",
        "Stop Heap Trace",
        "heap_trace_stop",
        NULL,  // TODO: aliases for system.action.profiler-trace-stop
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.api.get-coredump-status",
        "action",
        "Get Coredump Status",
        "none",
        NULL,  // TODO: aliases for system.api.get-coredump-status
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.api.get-coredump",
        "action",
        "Download Coredump",
        "none",
        NULL,  // TODO: aliases for system.api.get-coredump
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.erase-coredump",
        "action",
        "Erase Coredump",
        "esp_core_dump_image_erase",
        NULL,  // TODO: aliases for system.action.erase-coredump
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.api.get-admission-trace",
        "action",
        "Get Admission Trace",
        "fillAdmissionTraceResponse",
        NULL,  // TODO: aliases for system.api.get-admission-trace
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.api.get-validation",
        "action",
        "Get Validation Snapshot",
        "populateValidationJson",
        NULL,  // TODO: aliases for system.api.get-validation
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.upload-firmware",
        "action",
        "Upload Firmware",
        "none",
        NULL,  // TODO: aliases for system.action.upload-firmware
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.action.upload-filesystem",
        "action",
        "Upload Filesystem",
        "none",
        NULL,  // TODO: aliases for system.action.upload-filesystem
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.api.event-stream",
        "status",
        "Event Stream",
        "none",
        NULL,  // TODO: aliases for system.api.event-stream
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "rc.status.snapshot",
        "status",
        "RC Snapshot",
        "captureRcDiagnosticsSnapshot",
        NULL,  // TODO: aliases for rc.status.snapshot
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "rc.action.toggle-debug",
        "action",
        "Toggle RC Debug",
        "commandedSetRcDebug",
        NULL,  // TODO: aliases for rc.action.toggle-debug
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "rc.api.get-bindable-actions",
        "action",
        "List Bindable Actions",
        "fillActionsResponse",
        NULL,  // TODO: aliases for rc.api.get-bindable-actions
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "rc.api.get-map",
        "action",
        "Get RC Map",
        "populateRcMapJson",
        NULL,  // TODO: aliases for rc.api.get-map
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "rc.action.set-map",
        "action",
        "Set RC Map",
        "rcMapApply",
        NULL,  // TODO: aliases for rc.action.set-map
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "rc.action.test-bindable",
        "action",
        "Test Bindable Action",
        "dispatchRcTriggerActionTest",
        NULL,  // TODO: aliases for rc.action.test-bindable
        g_params_rc_action_test_bindable,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        true,  // requires_web_control
        false,  // safety_critical
    },
    {
        "rc.config.mode",
        "config",
        "RC Mode",
        "rcMapApply",
        NULL,  // TODO: aliases for rc.config.mode
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.vader",
        "action",
        "DM:VADER — Imperial March",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.vader
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.hello",
        "action",
        "DM:HELLO — Hello There",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.hello
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.nod",
        "action",
        "DM:NOD — Acknowledgment nod",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.nod
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.flutter",
        "action",
        "DM:FLUTTER — Panel flutter",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.flutter
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.bloom",
        "action",
        "DM:BLOOM — Pie bloom",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.bloom
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.leia",
        "action",
        "DM:LEIA — Leia message",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.leia
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.alarm",
        "action",
        "DM:ALARM — Alarm",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.alarm
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.heart",
        "action",
        "DM:HEART — Heart",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.heart
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.reset",
        "action",
        "DM:RESET — Reset all",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.reset
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.pies",
        "action",
        "DM:PIES — Pie panels toggle",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.pies
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.low",
        "action",
        "DM:LOW — Ring panels toggle",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.low
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.openall",
        "action",
        "DM:OPENALL — All panels toggle",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.openall
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.cantina",
        "action",
        "DM:CANTINA — Cantina dance",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.cantina
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.rockmarch",
        "action",
        "DM:ROCKMARCH — Rock march",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.rockmarch
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.scream",
        "action",
        "DM:SCREAM — Scream",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.scream
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "dome.seq.overload",
        "action",
        "DM:OVERLOAD — Overload",
        "sequenceStart",
        NULL,  // TODO: aliases for dome.seq.overload
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
    {
        "system.console",
        "action",
        "Controller Console",
        "consoleExecuteCommand",
        NULL,  // TODO: aliases for system.console
        NULL,
        true,  // available_on_board (TODO: check board_capability)
        true,  // available_in_build (TODO: check build_flag)
        false,  // requires_web_control
        false,  // safety_critical
    },
};

static const size_t g_catalogCount = sizeof(g_catalogEntries) / sizeof(g_catalogEntries[0]);

// =============================================================================
// Public API
// =============================================================================

const ConsoleCatalogEntry* consoleCatalogGetEntries(size_t* out_count) {
    if (out_count) {
        *out_count = g_catalogCount;
    }
    return g_catalogEntries;
}

const ConsoleCatalogEntry* consoleCatalogFindByName(const char* name) {
    if (!name) return NULL;
    for (size_t i = 0; i < g_catalogCount; ++i) {
        if (strcmp(g_catalogEntries[i].name, name) == 0) {
            return &g_catalogEntries[i];
        }
    }
    return NULL;
}

size_t consoleCatalogGetCount(void) {
    return g_catalogCount;
}
