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
//
// Availability (available_on_board and available_in_build) is evaluated at
// compile-time via macros from include/config.h and include/board_capabilities.inc
// (ADR 0029). This allows a board that sets PA_CAP_DRIVE_BACKEND_HOVERBOARD=0
// to flip the availability of drive operations with no generator change.
// =============================================================================

#include "console_catalog.h"
#include "config.h"
#include <string.h>

// =============================================================================
// Alias Arrays (RC tokens mapped to operation names)
// =============================================================================

static const char* const g_aliases_drive_action_speed[] = { "drive_speed", NULL };
static const char* const g_aliases_drive_action_steer[] = { "drive_steer", NULL };
static const char* const g_aliases_drive_action_speed_preset_cycle[] = { "speed_preset_cycle", NULL };
static const char* const g_aliases_dome_action_set_speed[] = { "dome_speed", NULL };
static const char* const g_aliases_dome_action_marcduino_sequence[] = { "seq", NULL };
static const char* const g_aliases_dome_action_marcduino_command[] = { "cmd", NULL };
static const char* const g_aliases_dome_action_dome_sequence[] = { "dome_seq", NULL };
static const char* const g_aliases_dome_action_droid_sequence_scream[] = { "droid_seq_scream", NULL };
static const char* const g_aliases_dome_action_droid_sequence_wave[] = { "droid_seq_wave", NULL };
static const char* const g_aliases_dome_action_droid_sequence_fast_wave[] = { "droid_seq_fast_wave", NULL };
static const char* const g_aliases_dome_action_droid_sequence_open_wave[] = { "droid_seq_open_wave", NULL };
static const char* const g_aliases_dome_action_droid_sequence_beep_cantina[] = { "droid_seq_beep_cantina", NULL };
static const char* const g_aliases_dome_action_droid_sequence_faint[] = { "droid_seq_faint", NULL };
static const char* const g_aliases_dome_action_droid_sequence_cantina[] = { "droid_seq_cantina", NULL };
static const char* const g_aliases_dome_action_droid_sequence_leia[] = { "droid_seq_leia", NULL };
static const char* const g_aliases_dome_action_droid_sequence_disco[] = { "droid_seq_disco", NULL };
static const char* const g_aliases_dome_action_droid_sequence_screams[] = { "droid_seq_screams", NULL };
static const char* const g_aliases_dome_action_droid_sequence_wiggle[] = { "droid_seq_wiggle", NULL };
static const char* const g_aliases_sound_action_random_general[] = { "sound_rand_general", NULL };
static const char* const g_aliases_sound_action_random_chatty[] = { "sound_rand_chatty", NULL };
static const char* const g_aliases_sound_action_random_happy[] = { "sound_rand_happy", NULL };
static const char* const g_aliases_sound_action_random_processing[] = { "sound_rand_processing", NULL };
static const char* const g_aliases_sound_action_random_sad[] = { "sound_rand_sad", NULL };
static const char* const g_aliases_sound_action_random_sentimental[] = { "sound_rand_sentimental", NULL };
static const char* const g_aliases_sound_action_random_humming[] = { "sound_rand_humming", NULL };
static const char* const g_aliases_sound_action_random_scream[] = { "sound_rand_scream", NULL };
static const char* const g_aliases_sound_action_random_surprised[] = { "sound_rand_surprised", NULL };
static const char* const g_aliases_sound_action_random_alert[] = { "sound_rand_alert", NULL };
static const char* const g_aliases_sound_action_random_snarky[] = { "sound_rand_snarky", NULL };
static const char* const g_aliases_sound_action_random_whistle[] = { "sound_rand_whistle", NULL };
static const char* const g_aliases_servo_action_toggle_arm1[] = { "arm1_toggle", NULL };
static const char* const g_aliases_servo_action_toggle_arm2[] = { "arm2_toggle", NULL };
static const char* const g_aliases_servo_action_toggle_aux1[] = { "aux1_toggle", NULL };
static const char* const g_aliases_servo_action_toggle_aux2[] = { "aux2_toggle", NULL };
static const char* const g_aliases_servo_action_toggle_aux3[] = { "aux3_toggle", NULL };
static const char* const g_aliases_system_action_set_mode[] = { "op_mode", NULL };
static const char* const g_aliases_system_action_estop[] = { "estop", NULL };
static const char* const g_aliases_system_action_sleep_toggle[] = { "sleep_toggle", NULL };

// Total alias arrays: 38

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
// Status Query Field Lists (API JSON keys, verbatim)
// =============================================================================

static const char* const g_fields_dome_status_current[] = { "domeTargetSpeed", "domeEnabled", NULL };
static const char* const g_fields_sound_status_current[] = { "driver", "capabilities", "link_ok", "active", "play_state", "device", "total_tracks", "current_track", "rx_status", "rx_detail", NULL };
static const char* const g_fields_system_status_health[] = { "estop", "sbusSignalLost", "sbusHwFailsafe", "webControlEnabled", "wifiConnected", "wifiClientConnected", "littleFsReady", "heapFree", "heapMin", "heapLargestBlock", "wifiRssi", NULL };
static const char* const g_fields_system_status_wifi[] = { "apSsid", "apIp", "staEnabled", "staConnected", "staIp", "staSsid", "wifiRssi", "networkRecovery", NULL };
static const char* const g_fields_dome_status_serial_link[] = { "active", "heartbeatRx", "heartbeatTx", NULL };
static const char* const g_fields_rc_status_snapshot[] = { "mode", "sbus1", "sbus2", NULL };

// Total field-name arrays: 6

// =============================================================================
// Complete Operation Catalog
// =============================================================================

static const ConsoleCatalogEntry g_catalogEntries[] = {
    {
        "drive.action.move",
        "action",
        NULL,  // aliases
        g_params_drive_action_move,
        PA_CAP_DRIVE_BACKEND_HOVERBOARD,  // available_on_board
        1,  // available_in_build
        true,  // requires_web_control
        true,  // safety_critical
        true,  // executor_ready
        0,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "drive.action.speed",
        "action",
        g_aliases_drive_action_speed,  // aliases
        g_params_drive_action_speed,
        PA_CAP_DRIVE_BACKEND_HOVERBOARD,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        99,  // help_offset
        107,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "drive.action.steer",
        "action",
        g_aliases_drive_action_steer,  // aliases
        g_params_drive_action_steer,
        PA_CAP_DRIVE_BACKEND_HOVERBOARD,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        207,  // help_offset
        99,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "drive.action.speed-preset-slow",
        "action",
        NULL,  // aliases
        g_params_drive_action_speed_preset_slow,
        PA_CAP_DRIVE_BACKEND_HOVERBOARD,  // available_on_board
        1,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        307,  // help_offset
        148,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "drive.action.speed-preset-normal",
        "action",
        NULL,  // aliases
        g_params_drive_action_speed_preset_normal,
        PA_CAP_DRIVE_BACKEND_HOVERBOARD,  // available_on_board
        1,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        456,  // help_offset
        154,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "drive.action.speed-preset-turbo",
        "action",
        NULL,  // aliases
        g_params_drive_action_speed_preset_turbo,
        PA_CAP_DRIVE_BACKEND_HOVERBOARD,  // available_on_board
        1,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        611,  // help_offset
        151,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "drive.action.speed-preset-cycle",
        "action",
        g_aliases_drive_action_speed_preset_cycle,  // aliases
        NULL,
        PA_CAP_DRIVE_BACKEND_HOVERBOARD,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        763,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "drive.status.current",
        "status",
        NULL,  // aliases
        NULL,
        PA_CAP_DRIVE_BACKEND_HOVERBOARD,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        882,  // help_offset
        100,  // help_length
        NULL,  // fields
        false,  // is_query
    },
    {
        "drive.event.failsafe-triggered",
        "event",
        NULL,  // aliases
        NULL,
        PA_CAP_DRIVE_BACKEND_HOVERBOARD,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        true,  // safety_critical
        true,  // executor_ready
        983,  // help_offset
        119,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "drive.config.speed-limit",
        "config",
        NULL,  // aliases
        NULL,
        PA_CAP_DRIVE_BACKEND_HOVERBOARD,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1103,  // help_offset
        95,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.set-speed",
        "action",
        g_aliases_dome_action_set_speed,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1199,  // help_offset
        80,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.send-command",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1280,  // help_offset
        109,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.marcduino-sequence",
        "action",
        g_aliases_dome_action_marcduino_sequence,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1390,  // help_offset
        139,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.marcduino-command",
        "action",
        g_aliases_dome_action_marcduino_command,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1530,  // help_offset
        123,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.dome-sequence",
        "action",
        g_aliases_dome_action_dome_sequence,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        1654,  // help_offset
        561,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-scream",
        "action",
        g_aliases_dome_action_droid_sequence_scream,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2216,  // help_offset
        121,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-wave",
        "action",
        g_aliases_dome_action_droid_sequence_wave,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2338,  // help_offset
        105,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-fast-wave",
        "action",
        g_aliases_dome_action_droid_sequence_fast_wave,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2444,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-open-wave",
        "action",
        g_aliases_dome_action_droid_sequence_open_wave,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2560,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-beep-cantina",
        "action",
        g_aliases_dome_action_droid_sequence_beep_cantina,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2676,  // help_offset
        137,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-faint",
        "action",
        g_aliases_dome_action_droid_sequence_faint,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2814,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-cantina",
        "action",
        g_aliases_dome_action_droid_sequence_cantina,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        2939,  // help_offset
        132,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-leia",
        "action",
        g_aliases_dome_action_droid_sequence_leia,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3072,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-disco",
        "action",
        g_aliases_dome_action_droid_sequence_disco,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3197,  // help_offset
        120,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-screams",
        "action",
        g_aliases_dome_action_droid_sequence_screams,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3318,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.droid-sequence-wiggle",
        "action",
        g_aliases_dome_action_droid_sequence_wiggle,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3443,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.api.get-layout",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3559,  // help_offset
        321,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.sequence-stop",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        3881,  // help_offset
        297,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-scream",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4179,  // help_offset
        148,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-happy",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4328,  // help_offset
        149,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-overload",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4478,  // help_offset
        158,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-alarm",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4637,  // help_offset
        111,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-vader",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4749,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-rockmarch",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        4868,  // help_offset
        144,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-leia",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5013,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-cantina",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5129,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-heart",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5254,  // help_offset
        117,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-hello",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5372,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.event.cue-reset",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5485,  // help_offset
        197,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.status.current",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5683,  // help_offset
        104,  // help_length
        g_fields_dome_status_current,  // fields
        true,  // is_query
    },
    {
        "dome.action.move",
        "action",
        NULL,  // aliases
        g_params_dome_action_move,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        5788,  // help_offset
        250,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.api.list-sequences",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6039,  // help_offset
        161,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.api.list-builtin-sequences",
        "action",
        NULL,  // aliases
        g_params_dome_api_list_builtin_sequences,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6201,  // help_offset
        258,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.api.get-sequence",
        "action",
        NULL,  // aliases
        g_params_dome_api_get_sequence,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6460,  // help_offset
        129,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.save-sequence",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6590,  // help_offset
        143,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.delete-sequence",
        "action",
        NULL,  // aliases
        g_params_dome_action_delete_sequence,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6734,  // help_offset
        222,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.action.test-sequence",
        "action",
        NULL,  // aliases
        g_params_dome_action_test_sequence,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        6957,  // help_offset
        236,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.api.get-sequence-last-run",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7194,  // help_offset
        311,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.play-track",
        "action",
        NULL,  // aliases
        g_params_sound_action_play_track,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7506,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.play-track-scream",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7622,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.play-track-faint",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7735,  // help_offset
        132,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.play-track-leia",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7868,  // help_offset
        120,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.play-track-cantina-short",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        7989,  // help_offset
        136,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.play-track-cantina-long",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8126,  // help_offset
        142,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.play-track-sw-theme",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8269,  // help_offset
        139,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.play-track-imperial-march",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8409,  // help_offset
        139,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.play-track-startup",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8549,  // help_offset
        129,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.play-track-disco",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8679,  // help_offset
        150,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.api.get-catalog",
        "action",
        NULL,  // aliases
        g_params_sound_api_get_catalog,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8830,  // help_offset
        145,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.api.refresh-catalog",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        8976,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.api.play-banked",
        "action",
        NULL,  // aliases
        g_params_sound_api_play_banked,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        9101,  // help_offset
        160,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.api.get-mood-map",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        9262,  // help_offset
        126,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.set-mood-map",
        "action",
        NULL,  // aliases
        g_params_sound_action_set_mood_map,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        9389,  // help_offset
        205,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.set-category-range",
        "action",
        NULL,  // aliases
        g_params_sound_action_set_category_range,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        9595,  // help_offset
        265,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.query-status",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        9861,  // help_offset
        208,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.track-stop",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        10070,  // help_offset
        383,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.quiet",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        10454,  // help_offset
        288,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.set-volume",
        "action",
        NULL,  // aliases
        g_params_sound_action_set_volume,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        10743,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.volume-up",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        10862,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.volume-down",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        10955,  // help_offset
        96,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.volume-preset-mid",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11052,  // help_offset
        103,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.volume-preset-max",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11156,  // help_offset
        103,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.volume-preset-min",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11260,  // help_offset
        102,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.dollar-command",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11363,  // help_offset
        116,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-on",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11480,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-off",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11579,  // help_offset
        101,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-general",
        "action",
        g_aliases_sound_action_random_general,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11681,  // help_offset
        128,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-chatty",
        "action",
        g_aliases_sound_action_random_chatty,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11810,  // help_offset
        125,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-happy",
        "action",
        g_aliases_sound_action_random_happy,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        11936,  // help_offset
        122,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-processing",
        "action",
        g_aliases_sound_action_random_processing,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12059,  // help_offset
        137,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-sad",
        "action",
        g_aliases_sound_action_random_sad,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12197,  // help_offset
        116,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-sentimental",
        "action",
        g_aliases_sound_action_random_sentimental,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12314,  // help_offset
        140,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-humming",
        "action",
        g_aliases_sound_action_random_humming,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12455,  // help_offset
        128,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-scream",
        "action",
        g_aliases_sound_action_random_scream,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12584,  // help_offset
        125,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-surprised",
        "action",
        g_aliases_sound_action_random_surprised,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12710,  // help_offset
        134,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-alert",
        "action",
        g_aliases_sound_action_random_alert,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12845,  // help_offset
        122,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-snarky",
        "action",
        g_aliases_sound_action_random_snarky,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        12968,  // help_offset
        125,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.action.random-whistle",
        "action",
        g_aliases_sound_action_random_whistle,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13094,  // help_offset
        128,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.status.current",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13223,  // help_offset
        111,  // help_length
        g_fields_sound_status_current,  // fields
        true,  // is_query
    },
    {
        "sound.config.volume",
        "config",
        NULL,  // aliases
        g_params_sound_config_volume,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13335,  // help_offset
        81,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.random-min",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13417,  // help_offset
        106,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.random-max",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13524,  // help_offset
        107,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.mood-interval-quiet",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13632,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.mood-interval-mid",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13747,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.mood-interval-full",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13863,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.mood-interval-awake-plus",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        13982,  // help_offset
        116,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.startup-track",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14099,  // help_offset
        175,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.boot-complete-track",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14275,  // help_offset
        176,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.track-assignments",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14452,  // help_offset
        174,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.system-track-assignments",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14627,  // help_offset
        167,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.category-ranges",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14795,  // help_offset
        140,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "sound.config.mood-category-map",
        "config",
        NULL,  // aliases
        g_params_sound_config_mood_category_map,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        14936,  // help_offset
        583,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "servo.action.open",
        "action",
        NULL,  // aliases
        g_params_servo_action_open,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        15520,  // help_offset
        106,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "servo.action.close",
        "action",
        NULL,  // aliases
        g_params_servo_action_close,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        15627,  // help_offset
        111,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "servo.action.set-position",
        "action",
        NULL,  // aliases
        g_params_servo_action_set_position,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        15739,  // help_offset
        137,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "servo.action.stop",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        15877,  // help_offset
        83,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "servo.action.toggle-arm1",
        "action",
        g_aliases_servo_action_toggle_arm1,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        15961,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "servo.action.toggle-arm2",
        "action",
        g_aliases_servo_action_toggle_arm2,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16060,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "servo.action.toggle-aux1",
        "action",
        g_aliases_servo_action_toggle_aux1,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16159,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "servo.action.toggle-aux2",
        "action",
        g_aliases_servo_action_toggle_aux2,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16258,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "servo.action.toggle-aux3",
        "action",
        g_aliases_servo_action_toggle_aux3,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16357,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "servo.status.current",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16456,  // help_offset
        99,  // help_length
        NULL,  // fields
        false,  // is_query
    },
    {
        "aux.action.led-color",
        "action",
        NULL,  // aliases
        g_params_aux_action_led_color,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16556,  // help_offset
        100,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "aux.action.led-effect",
        "action",
        NULL,  // aliases
        g_params_aux_action_led_effect,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16657,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "aux.status.led-state",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16748,  // help_offset
        100,  // help_length
        NULL,  // fields
        false,  // is_query
    },
    {
        "aux.config.led-pin",
        "config",
        NULL,  // aliases
        g_params_aux_config_led_pin,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16849,  // help_offset
        146,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "aux.config.led-count",
        "config",
        NULL,  // aliases
        g_params_aux_config_led_count,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        16996,  // help_offset
        105,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.set-mode",
        "action",
        g_aliases_system_action_set_mode,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17102,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.estop",
        "action",
        g_aliases_system_action_estop,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        true,  // safety_critical
        true,  // executor_ready
        17201,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.estop-clear",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        true,  // safety_critical
        true,  // executor_ready
        17316,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.enable-web-control",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17431,  // help_offset
        116,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.disable-web-control",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17548,  // help_offset
        122,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.reboot",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17671,  // help_offset
        81,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.set-mood",
        "action",
        NULL,  // aliases
        g_params_system_action_set_mood,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17753,  // help_offset
        141,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.sleep",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        17895,  // help_offset
        246,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.wake",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18142,  // help_offset
        208,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.sleep-toggle",
        "action",
        g_aliases_system_action_sleep_toggle,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18351,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.event.drives-engaged",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18476,  // help_offset
        108,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.event.dome-enabled",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18585,  // help_offset
        87,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.event.boot-complete",
        "event",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18673,  // help_offset
        132,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.status.sleep-mode",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18806,  // help_offset
        189,  // help_length
        NULL,  // fields
        false,  // is_query
    },
    {
        "system.status.mood",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        18996,  // help_offset
        120,  // help_length
        NULL,  // fields
        false,  // is_query
    },
    {
        "system.config.mood",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19117,  // help_offset
        75,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_arm1",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19193,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_arm2",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19286,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_aux1",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19379,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_aux2",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19470,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_aux3",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19561,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_dome_esc",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19652,  // help_offset
        110,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_rc_ch1",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19763,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_rc_ch2",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19861,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_rc_ch3",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        19959,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_rc_ch4",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20057,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_rc_ch5",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20155,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_rc_ch6",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20253,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_drive",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20351,  // help_offset
        107,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_audio",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20459,  // help_offset
        87,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.config.enable_protor2link",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20547,  // help_offset
        103,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.status.health",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20651,  // help_offset
        110,  // help_length
        g_fields_system_status_health,  // fields
        true,  // is_query
    },
    {
        "system.status.dashboard-health",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20762,  // help_offset
        221,  // help_length
        NULL,  // fields
        false,  // is_query
    },
    {
        "system.status.logs",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        20984,  // help_offset
        66,  // help_length
        NULL,  // fields
        false,  // is_query
    },
    {
        "system.status.wifi",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21051,  // help_offset
        114,  // help_length
        g_fields_system_status_wifi,  // fields
        true,  // is_query
    },
    {
        "dome.status.serial-link",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21166,  // help_offset
        116,  // help_length
        g_fields_dome_status_serial_link,  // fields
        true,  // is_query
    },
    {
        "system.api.get-identity",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21283,  // help_offset
        108,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.set-identity",
        "action",
        NULL,  // aliases
        g_params_system_action_set_identity,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21392,  // help_offset
        198,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.api.get-profiler",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        PA_HEAP_PROFILE,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21591,  // help_offset
        352,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.profiler-trace-start",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        PA_HEAP_TRACING,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        21944,  // help_offset
        265,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.profiler-trace-stop",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        PA_HEAP_TRACING,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        22210,  // help_offset
        141,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.api.get-coredump-status",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        22352,  // help_offset
        138,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.api.get-coredump",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        22491,  // help_offset
        208,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.erase-coredump",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        22700,  // help_offset
        123,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.api.get-admission-trace",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        PA_ADMISSION_TRACE,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        22824,  // help_offset
        252,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.api.get-validation",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23077,  // help_offset
        140,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.upload-firmware",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23218,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.action.upload-filesystem",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23337,  // help_offset
        191,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.api.event-stream",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23529,  // help_offset
        120,  // help_length
        NULL,  // fields
        false,  // is_query
    },
    {
        "rc.status.snapshot",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23650,  // help_offset
        125,  // help_length
        g_fields_rc_status_snapshot,  // fields
        true,  // is_query
    },
    {
        "rc.action.toggle-debug",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23776,  // help_offset
        104,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "rc.api.get-bindable-actions",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        23881,  // help_offset
        148,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "rc.api.get-map",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24030,  // help_offset
        85,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "rc.action.set-map",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24116,  // help_offset
        195,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "rc.action.test-bindable",
        "action",
        NULL,  // aliases
        g_params_rc_action_test_bindable,
        1,  // available_on_board
        1,  // available_in_build
        true,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24312,  // help_offset
        177,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "rc.config.mode",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24490,  // help_offset
        96,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.vader",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24587,  // help_offset
        152,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.hello",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24740,  // help_offset
        125,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.nod",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        24866,  // help_offset
        149,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.flutter",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25016,  // help_offset
        139,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.bloom",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25156,  // help_offset
        154,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.leia",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25311,  // help_offset
        152,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.alarm",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25464,  // help_offset
        147,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.heart",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25612,  // help_offset
        157,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.reset",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25770,  // help_offset
        165,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.pies",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        25936,  // help_offset
        168,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.low",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26105,  // help_offset
        174,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.openall",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26280,  // help_offset
        196,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.cantina",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26477,  // help_offset
        171,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.rockmarch",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26649,  // help_offset
        156,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.scream",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        26806,  // help_offset
        198,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "dome.seq.overload",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        27005,  // help_offset
        215,  // help_length
        NULL,  // fields
        true,  // is_query
    },
    {
        "system.console",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        true,  // executor_ready
        27221,  // help_offset
        161,  // help_length
        NULL,  // fields
        true,  // is_query
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
