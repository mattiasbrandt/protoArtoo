// =============================================================================
// src/console/console_catalog.cpp
//
// Auto-generated from docs/action-registry.yaml by tools/generate_console_catalog.py
// DO NOT EDIT MANUALLY
//
// Operation Catalog - runtime table mapping operation names to descriptors,
// parameter schemas, availability metadata, and help text addressing.
// Help text (description, display_name, parameter schema, executor details)
// is stored in LittleFS and addressed by offset/length.
// =============================================================================

#include "console_catalog.h"
#include <string.h>

// =============================================================================
// Parameter Descriptors
// =============================================================================

static const ConsoleParamDescriptor g_params_drive_action_move[] = {
    {"speed", "int16", true},
    {"steer", "int16", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed[] = {
    {"value", "float", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_steer[] = {
    {"value", "float", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_slow[] = {
    {"preset", "string", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_normal[] = {
    {"preset", "string", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_turbo[] = {
    {"preset", "string", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_move[] = {
    {"speed", "float", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_api_list_builtin_sequences[] = {
    {"name", "string", false},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_api_get_sequence[] = {
    {"name", "string", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_delete_sequence[] = {
    {"name", "string", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_test_sequence[] = {
    {"name", "string", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_play_track[] = {
    {"track", "uint16", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_api_get_catalog[] = {
    {"bank", "uint8", false},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_api_play_banked[] = {
    {"bank", "uint8", true},
    {"page", "string", true},
    {"index", "uint16", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_mood_map[] = {
    {"quiet", "uint16", true},
    {"mid", "uint16", true},
    {"full", "uint16", true},
    {"awakeplus", "uint16", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_category_range[] = {
    {"lo_key", "string", true},
    {"hi_key", "string", true},
    {"lo", "uint16", true},
    {"hi", "uint16", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_volume[] = {
    {"volume", "uint8", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_volume[] = {
    {"volume", "uint8", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_mood_category_map[] = {
    {"quiet", "uint16", true},
    {"mid", "uint16", true},
    {"full", "uint16", true},
    {"awakeplus", "uint16", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_open[] = {
    {"target", "string", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_close[] = {
    {"target", "string", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_set_position[] = {
    {"target", "string", true},
    {"position_us", "uint16", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_action_led_color[] = {
    {"r", "uint8", true},
    {"g", "uint8", true},
    {"b", "uint8", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_action_led_effect[] = {
    {"effect", "string", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_config_led_pin[] = {
    {"aux_led_pin", "uint8", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_config_led_count[] = {
    {"aux_led_count", "uint8", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_system_action_set_mood[] = {
    {"mood", "uint8", true},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_system_action_set_identity[] = {
    {"droidName", "string", true},
    {"mdnsUseName", "bool", false},
    {NULL, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_rc_action_test_bindable[] = {
    {"token", "string", true},
    {NULL, NULL, false}  // terminator
};

// =============================================================================
// Complete Operation Catalog
// =============================================================================

static const ConsoleCatalogEntry g_catalogEntries[] = {
    {
        "drive.action.move",
        "action",
        NULL,  // aliases
        g_params_drive_action_move,
        true,  // available_on_board
        true,  // available_in_build
        true,  // requires_web_control
        true,  // safety_critical
        true,  // executor_ready
        0,  // help_offset
        98,  // help_length
    },
    {
        "drive.action.speed",
        "action",
        NULL,  // aliases
        g_params_drive_action_speed,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        99,  // help_offset
        107,  // help_length
    },
    {
        "drive.action.steer",
        "action",
        NULL,  // aliases
        g_params_drive_action_steer,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        207,  // help_offset
        99,  // help_length
    },
    {
        "drive.action.speed-preset-slow",
        "action",
        NULL,  // aliases
        g_params_drive_action_speed_preset_slow,
        true,  // available_on_board
        true,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        307,  // help_offset
        148,  // help_length
    },
    {
        "drive.action.speed-preset-normal",
        "action",
        NULL,  // aliases
        g_params_drive_action_speed_preset_normal,
        true,  // available_on_board
        true,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        456,  // help_offset
        154,  // help_length
    },
    {
        "drive.action.speed-preset-turbo",
        "action",
        NULL,  // aliases
        g_params_drive_action_speed_preset_turbo,
        true,  // available_on_board
        true,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        611,  // help_offset
        151,  // help_length
    },
    {
        "drive.action.speed-preset-cycle",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        763,  // help_offset
        118,  // help_length
    },
    {
        "drive.status.current",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        882,  // help_offset
        100,  // help_length
    },
    {
        "drive.event.failsafe-triggered",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        true,  // safety_critical
        true,  // executor_ready
        983,  // help_offset
        119,  // help_length
    },
    {
        "drive.config.speed-limit",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1103,  // help_offset
        95,  // help_length
    },
    {
        "dome.action.set-speed",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1199,  // help_offset
        80,  // help_length
    },
    {
        "dome.action.send-command",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1280,  // help_offset
        109,  // help_length
    },
    {
        "dome.action.marcduino-sequence",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1390,  // help_offset
        139,  // help_length
    },
    {
        "dome.action.marcduino-command",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1530,  // help_offset
        123,  // help_length
    },
    {
        "dome.action.dome-sequence",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1654,  // help_offset
        561,  // help_length
    },
    {
        "dome.action.droid-sequence-scream",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2216,  // help_offset
        121,  // help_length
    },
    {
        "dome.action.droid-sequence-wave",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2338,  // help_offset
        105,  // help_length
    },
    {
        "dome.action.droid-sequence-fast-wave",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2444,  // help_offset
        115,  // help_length
    },
    {
        "dome.action.droid-sequence-open-wave",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2560,  // help_offset
        115,  // help_length
    },
    {
        "dome.action.droid-sequence-beep-cantina",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2676,  // help_offset
        137,  // help_length
    },
    {
        "dome.action.droid-sequence-faint",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2814,  // help_offset
        124,  // help_length
    },
    {
        "dome.action.droid-sequence-cantina",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2939,  // help_offset
        132,  // help_length
    },
    {
        "dome.action.droid-sequence-leia",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3072,  // help_offset
        124,  // help_length
    },
    {
        "dome.action.droid-sequence-disco",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3197,  // help_offset
        120,  // help_length
    },
    {
        "dome.action.droid-sequence-screams",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3318,  // help_offset
        124,  // help_length
    },
    {
        "dome.action.droid-sequence-wiggle",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3443,  // help_offset
        115,  // help_length
    },
    {
        "dome.api.get-layout",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3559,  // help_offset
        321,  // help_length
    },
    {
        "dome.action.sequence-stop",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3881,  // help_offset
        297,  // help_length
    },
    {
        "dome.event.cue-scream",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4179,  // help_offset
        148,  // help_length
    },
    {
        "dome.event.cue-happy",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4328,  // help_offset
        149,  // help_length
    },
    {
        "dome.event.cue-overload",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4478,  // help_offset
        158,  // help_length
    },
    {
        "dome.event.cue-alarm",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4637,  // help_offset
        111,  // help_length
    },
    {
        "dome.event.cue-vader",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4749,  // help_offset
        118,  // help_length
    },
    {
        "dome.event.cue-rockmarch",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4868,  // help_offset
        144,  // help_length
    },
    {
        "dome.event.cue-leia",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5013,  // help_offset
        115,  // help_length
    },
    {
        "dome.event.cue-cantina",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5129,  // help_offset
        124,  // help_length
    },
    {
        "dome.event.cue-heart",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5254,  // help_offset
        117,  // help_length
    },
    {
        "dome.event.cue-hello",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5372,  // help_offset
        112,  // help_length
    },
    {
        "dome.event.cue-reset",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5485,  // help_offset
        197,  // help_length
    },
    {
        "dome.status.current",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5683,  // help_offset
        94,  // help_length
    },
    {
        "dome.action.move",
        "action",
        NULL,  // aliases
        g_params_dome_action_move,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5778,  // help_offset
        250,  // help_length
    },
    {
        "dome.api.list-sequences",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6029,  // help_offset
        161,  // help_length
    },
    {
        "dome.api.list-builtin-sequences",
        "action",
        NULL,  // aliases
        g_params_dome_api_list_builtin_sequences,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6191,  // help_offset
        258,  // help_length
    },
    {
        "dome.api.get-sequence",
        "action",
        NULL,  // aliases
        g_params_dome_api_get_sequence,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6450,  // help_offset
        129,  // help_length
    },
    {
        "dome.action.save-sequence",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6580,  // help_offset
        143,  // help_length
    },
    {
        "dome.action.delete-sequence",
        "action",
        NULL,  // aliases
        g_params_dome_action_delete_sequence,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6724,  // help_offset
        222,  // help_length
    },
    {
        "dome.action.test-sequence",
        "action",
        NULL,  // aliases
        g_params_dome_action_test_sequence,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6947,  // help_offset
        236,  // help_length
    },
    {
        "dome.api.get-sequence-last-run",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7184,  // help_offset
        311,  // help_length
    },
    {
        "sound.action.play-track",
        "action",
        NULL,  // aliases
        g_params_sound_action_play_track,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7496,  // help_offset
        115,  // help_length
    },
    {
        "sound.action.play-track-scream",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7612,  // help_offset
        112,  // help_length
    },
    {
        "sound.action.play-track-faint",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7725,  // help_offset
        132,  // help_length
    },
    {
        "sound.action.play-track-leia",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7858,  // help_offset
        120,  // help_length
    },
    {
        "sound.action.play-track-cantina-short",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7979,  // help_offset
        136,  // help_length
    },
    {
        "sound.action.play-track-cantina-long",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8116,  // help_offset
        142,  // help_length
    },
    {
        "sound.action.play-track-sw-theme",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8259,  // help_offset
        139,  // help_length
    },
    {
        "sound.action.play-track-imperial-march",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8399,  // help_offset
        139,  // help_length
    },
    {
        "sound.action.play-track-startup",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8539,  // help_offset
        129,  // help_length
    },
    {
        "sound.action.play-track-disco",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8669,  // help_offset
        150,  // help_length
    },
    {
        "sound.api.get-catalog",
        "action",
        NULL,  // aliases
        g_params_sound_api_get_catalog,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8820,  // help_offset
        145,  // help_length
    },
    {
        "sound.api.refresh-catalog",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8966,  // help_offset
        124,  // help_length
    },
    {
        "sound.api.play-banked",
        "action",
        NULL,  // aliases
        g_params_sound_api_play_banked,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        9091,  // help_offset
        160,  // help_length
    },
    {
        "sound.api.get-mood-map",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        9252,  // help_offset
        126,  // help_length
    },
    {
        "sound.action.set-mood-map",
        "action",
        NULL,  // aliases
        g_params_sound_action_set_mood_map,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        9379,  // help_offset
        205,  // help_length
    },
    {
        "sound.action.set-category-range",
        "action",
        NULL,  // aliases
        g_params_sound_action_set_category_range,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        9585,  // help_offset
        265,  // help_length
    },
    {
        "sound.action.query-status",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        9851,  // help_offset
        208,  // help_length
    },
    {
        "sound.action.track-stop",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        10060,  // help_offset
        383,  // help_length
    },
    {
        "sound.action.quiet",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        10444,  // help_offset
        288,  // help_length
    },
    {
        "sound.action.set-volume",
        "action",
        NULL,  // aliases
        g_params_sound_action_set_volume,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        10733,  // help_offset
        118,  // help_length
    },
    {
        "sound.action.volume-up",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        10852,  // help_offset
        92,  // help_length
    },
    {
        "sound.action.volume-down",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        10945,  // help_offset
        96,  // help_length
    },
    {
        "sound.action.volume-preset-mid",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11042,  // help_offset
        103,  // help_length
    },
    {
        "sound.action.volume-preset-max",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11146,  // help_offset
        103,  // help_length
    },
    {
        "sound.action.volume-preset-min",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11250,  // help_offset
        102,  // help_length
    },
    {
        "sound.action.dollar-command",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11353,  // help_offset
        116,  // help_length
    },
    {
        "sound.action.random-on",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11470,  // help_offset
        98,  // help_length
    },
    {
        "sound.action.random-off",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11569,  // help_offset
        101,  // help_length
    },
    {
        "sound.action.random-general",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11671,  // help_offset
        128,  // help_length
    },
    {
        "sound.action.random-chatty",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11800,  // help_offset
        125,  // help_length
    },
    {
        "sound.action.random-happy",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11926,  // help_offset
        122,  // help_length
    },
    {
        "sound.action.random-processing",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12049,  // help_offset
        137,  // help_length
    },
    {
        "sound.action.random-sad",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12187,  // help_offset
        116,  // help_length
    },
    {
        "sound.action.random-sentimental",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12304,  // help_offset
        140,  // help_length
    },
    {
        "sound.action.random-humming",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12445,  // help_offset
        128,  // help_length
    },
    {
        "sound.action.random-scream",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12574,  // help_offset
        125,  // help_length
    },
    {
        "sound.action.random-surprised",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12700,  // help_offset
        134,  // help_length
    },
    {
        "sound.action.random-alert",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12835,  // help_offset
        122,  // help_length
    },
    {
        "sound.action.random-snarky",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12958,  // help_offset
        125,  // help_length
    },
    {
        "sound.action.random-whistle",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13084,  // help_offset
        128,  // help_length
    },
    {
        "sound.status.current",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13213,  // help_offset
        91,  // help_length
    },
    {
        "sound.config.volume",
        "config",
        NULL,  // aliases
        g_params_sound_config_volume,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13305,  // help_offset
        81,  // help_length
    },
    {
        "sound.config.random-min",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13387,  // help_offset
        106,  // help_length
    },
    {
        "sound.config.random-max",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13494,  // help_offset
        107,  // help_length
    },
    {
        "sound.config.mood-interval-quiet",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13602,  // help_offset
        114,  // help_length
    },
    {
        "sound.config.mood-interval-mid",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13717,  // help_offset
        115,  // help_length
    },
    {
        "sound.config.mood-interval-full",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13833,  // help_offset
        118,  // help_length
    },
    {
        "sound.config.mood-interval-awake-plus",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13952,  // help_offset
        116,  // help_length
    },
    {
        "sound.config.startup-track",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14069,  // help_offset
        175,  // help_length
    },
    {
        "sound.config.boot-complete-track",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14245,  // help_offset
        176,  // help_length
    },
    {
        "sound.config.track-assignments",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14422,  // help_offset
        174,  // help_length
    },
    {
        "sound.config.system-track-assignments",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14597,  // help_offset
        167,  // help_length
    },
    {
        "sound.config.category-ranges",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14765,  // help_offset
        140,  // help_length
    },
    {
        "sound.config.mood-category-map",
        "config",
        NULL,  // aliases
        g_params_sound_config_mood_category_map,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14906,  // help_offset
        583,  // help_length
    },
    {
        "servo.action.open",
        "action",
        NULL,  // aliases
        g_params_servo_action_open,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        15490,  // help_offset
        106,  // help_length
    },
    {
        "servo.action.close",
        "action",
        NULL,  // aliases
        g_params_servo_action_close,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        15597,  // help_offset
        111,  // help_length
    },
    {
        "servo.action.set-position",
        "action",
        NULL,  // aliases
        g_params_servo_action_set_position,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        15709,  // help_offset
        137,  // help_length
    },
    {
        "servo.action.stop",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        15847,  // help_offset
        83,  // help_length
    },
    {
        "servo.action.toggle-arm1",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        15931,  // help_offset
        98,  // help_length
    },
    {
        "servo.action.toggle-arm2",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16030,  // help_offset
        98,  // help_length
    },
    {
        "servo.action.toggle-aux1",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16129,  // help_offset
        98,  // help_length
    },
    {
        "servo.action.toggle-aux2",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16228,  // help_offset
        98,  // help_length
    },
    {
        "servo.action.toggle-aux3",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16327,  // help_offset
        98,  // help_length
    },
    {
        "servo.status.current",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16426,  // help_offset
        99,  // help_length
    },
    {
        "aux.action.led-color",
        "action",
        NULL,  // aliases
        g_params_aux_action_led_color,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16526,  // help_offset
        100,  // help_length
    },
    {
        "aux.action.led-effect",
        "action",
        NULL,  // aliases
        g_params_aux_action_led_effect,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16627,  // help_offset
        90,  // help_length
    },
    {
        "aux.status.led-state",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16718,  // help_offset
        100,  // help_length
    },
    {
        "aux.config.led-pin",
        "config",
        NULL,  // aliases
        g_params_aux_config_led_pin,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16819,  // help_offset
        146,  // help_length
    },
    {
        "aux.config.led-count",
        "config",
        NULL,  // aliases
        g_params_aux_config_led_count,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16966,  // help_offset
        105,  // help_length
    },
    {
        "system.action.set-mode",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17072,  // help_offset
        98,  // help_length
    },
    {
        "system.action.estop",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        true,  // safety_critical
        true,  // executor_ready
        17171,  // help_offset
        114,  // help_length
    },
    {
        "system.action.estop-clear",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        true,  // safety_critical
        true,  // executor_ready
        17286,  // help_offset
        114,  // help_length
    },
    {
        "system.action.enable-web-control",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17401,  // help_offset
        116,  // help_length
    },
    {
        "system.action.disable-web-control",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17518,  // help_offset
        122,  // help_length
    },
    {
        "system.action.reboot",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17641,  // help_offset
        81,  // help_length
    },
    {
        "system.action.set-mood",
        "action",
        NULL,  // aliases
        g_params_system_action_set_mood,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17723,  // help_offset
        141,  // help_length
    },
    {
        "system.action.sleep",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17865,  // help_offset
        246,  // help_length
    },
    {
        "system.action.wake",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18112,  // help_offset
        208,  // help_length
    },
    {
        "system.action.sleep-toggle",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18321,  // help_offset
        124,  // help_length
    },
    {
        "system.event.drives-engaged",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18446,  // help_offset
        108,  // help_length
    },
    {
        "system.event.dome-enabled",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18555,  // help_offset
        87,  // help_length
    },
    {
        "system.event.boot-complete",
        "event",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18643,  // help_offset
        132,  // help_length
    },
    {
        "system.status.sleep-mode",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18776,  // help_offset
        189,  // help_length
    },
    {
        "system.status.mood",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18966,  // help_offset
        120,  // help_length
    },
    {
        "system.config.mood",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19087,  // help_offset
        75,  // help_length
    },
    {
        "system.config.enable_arm1",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19163,  // help_offset
        92,  // help_length
    },
    {
        "system.config.enable_arm2",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19256,  // help_offset
        92,  // help_length
    },
    {
        "system.config.enable_aux1",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19349,  // help_offset
        90,  // help_length
    },
    {
        "system.config.enable_aux2",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19440,  // help_offset
        90,  // help_length
    },
    {
        "system.config.enable_aux3",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19531,  // help_offset
        90,  // help_length
    },
    {
        "system.config.enable_dome_esc",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19622,  // help_offset
        110,  // help_length
    },
    {
        "system.config.enable_rc_ch1",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19733,  // help_offset
        97,  // help_length
    },
    {
        "system.config.enable_rc_ch2",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19831,  // help_offset
        97,  // help_length
    },
    {
        "system.config.enable_rc_ch3",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19929,  // help_offset
        97,  // help_length
    },
    {
        "system.config.enable_rc_ch4",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20027,  // help_offset
        97,  // help_length
    },
    {
        "system.config.enable_rc_ch5",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20125,  // help_offset
        97,  // help_length
    },
    {
        "system.config.enable_rc_ch6",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20223,  // help_offset
        97,  // help_length
    },
    {
        "system.config.enable_drive",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20321,  // help_offset
        107,  // help_length
    },
    {
        "system.config.enable_audio",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20429,  // help_offset
        87,  // help_length
    },
    {
        "system.config.enable_protor2link",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20517,  // help_offset
        103,  // help_length
    },
    {
        "system.status.health",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20621,  // help_offset
        104,  // help_length
    },
    {
        "system.status.dashboard-health",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20726,  // help_offset
        221,  // help_length
    },
    {
        "system.status.logs",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20948,  // help_offset
        66,  // help_length
    },
    {
        "system.status.wifi",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21015,  // help_offset
        104,  // help_length
    },
    {
        "dome.status.serial-link",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21120,  // help_offset
        102,  // help_length
    },
    {
        "system.api.get-identity",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21223,  // help_offset
        108,  // help_length
    },
    {
        "system.action.set-identity",
        "action",
        NULL,  // aliases
        g_params_system_action_set_identity,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21332,  // help_offset
        198,  // help_length
    },
    {
        "system.api.get-profiler",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21531,  // help_offset
        352,  // help_length
    },
    {
        "system.action.profiler-trace-start",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21884,  // help_offset
        265,  // help_length
    },
    {
        "system.action.profiler-trace-stop",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        22150,  // help_offset
        141,  // help_length
    },
    {
        "system.api.get-coredump-status",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        22292,  // help_offset
        138,  // help_length
    },
    {
        "system.api.get-coredump",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        22431,  // help_offset
        208,  // help_length
    },
    {
        "system.action.erase-coredump",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        22640,  // help_offset
        123,  // help_length
    },
    {
        "system.api.get-admission-trace",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        22764,  // help_offset
        252,  // help_length
    },
    {
        "system.api.get-validation",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23017,  // help_offset
        140,  // help_length
    },
    {
        "system.action.upload-firmware",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23158,  // help_offset
        118,  // help_length
    },
    {
        "system.action.upload-filesystem",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23277,  // help_offset
        191,  // help_length
    },
    {
        "system.api.event-stream",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23469,  // help_offset
        120,  // help_length
    },
    {
        "rc.status.snapshot",
        "status",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23590,  // help_offset
        125,  // help_length
    },
    {
        "rc.action.toggle-debug",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23716,  // help_offset
        104,  // help_length
    },
    {
        "rc.api.get-bindable-actions",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23821,  // help_offset
        148,  // help_length
    },
    {
        "rc.api.get-map",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23970,  // help_offset
        85,  // help_length
    },
    {
        "rc.action.set-map",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24056,  // help_offset
        195,  // help_length
    },
    {
        "rc.action.test-bindable",
        "action",
        NULL,  // aliases
        g_params_rc_action_test_bindable,
        true,  // available_on_board
        true,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24252,  // help_offset
        177,  // help_length
    },
    {
        "rc.config.mode",
        "config",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24430,  // help_offset
        96,  // help_length
    },
    {
        "dome.seq.vader",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24527,  // help_offset
        152,  // help_length
    },
    {
        "dome.seq.hello",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24680,  // help_offset
        125,  // help_length
    },
    {
        "dome.seq.nod",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24806,  // help_offset
        149,  // help_length
    },
    {
        "dome.seq.flutter",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24956,  // help_offset
        139,  // help_length
    },
    {
        "dome.seq.bloom",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25096,  // help_offset
        154,  // help_length
    },
    {
        "dome.seq.leia",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25251,  // help_offset
        152,  // help_length
    },
    {
        "dome.seq.alarm",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25404,  // help_offset
        147,  // help_length
    },
    {
        "dome.seq.heart",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25552,  // help_offset
        157,  // help_length
    },
    {
        "dome.seq.reset",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25710,  // help_offset
        165,  // help_length
    },
    {
        "dome.seq.pies",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25876,  // help_offset
        168,  // help_length
    },
    {
        "dome.seq.low",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26045,  // help_offset
        174,  // help_length
    },
    {
        "dome.seq.openall",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26220,  // help_offset
        196,  // help_length
    },
    {
        "dome.seq.cantina",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26417,  // help_offset
        171,  // help_length
    },
    {
        "dome.seq.rockmarch",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26589,  // help_offset
        156,  // help_length
    },
    {
        "dome.seq.scream",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26746,  // help_offset
        198,  // help_length
    },
    {
        "dome.seq.overload",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26945,  // help_offset
        215,  // help_length
    },
    {
        "system.console",
        "action",
        NULL,  // aliases
        NULL,
        true,  // available_on_board
        true,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        27161,  // help_offset
        161,  // help_length
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
