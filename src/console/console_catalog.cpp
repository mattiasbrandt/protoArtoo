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

static const char* const g_enum_drive_action_speed_preset_slow_preset[] = { "slow", NULL };
static const char* const g_enum_drive_action_speed_preset_normal_preset[] = { "normal", NULL };
static const char* const g_enum_drive_action_speed_preset_turbo_preset[] = { "turbo", NULL };
static const char* const g_enum_sound_api_play_banked_page[] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", NULL };
static const char* const g_enum_sound_config_track_assignments_key[] = { "scream", "faint", "leia", "cantina_s", "sw_theme", "imp_march", "cantina_l", "startup", "doodoo", "failure", "disco", "mahna", "inlove", "macho", "gangnam", "uptown", "celebr", "stayin", "harlem", "pbjtime", NULL };
static const char* const g_enum_sound_config_system_track_assignments_key[] = { "sys_boot", "sys_mode_n", "sys_mode_s", "sys_mode_t", "sys_drv_on", "sys_dome_on", "sys_net_down", NULL };
static const char* const g_enum_servo_action_open_target[] = { "arm1", "arm2", "aux1", "aux2", "aux3", "both", NULL };
static const char* const g_enum_servo_action_close_target[] = { "arm1", "arm2", "aux1", "aux2", "aux3", "both", NULL };
static const char* const g_enum_servo_action_set_position_target[] = { "arm1", "arm2", "aux1", "aux2", "aux3", NULL };
static const char* const g_enum_servo_action_stop_target[] = { "arm1", "arm2", "aux1", "aux2", "aux3", "both", NULL };
static const char* const g_enum_aux_action_led_effect_effect[] = { "solid", "blink", "pulse", "off", NULL };
static const char* const g_enum_aux_config_led_pin_aux_led_pin[] = { "0", "1", "2", "3", NULL };
static const char* const g_enum_system_action_set_mood_mood[] = { "10", "11", "13", "14", NULL };
static const char* const g_enum_wifi_config_settings_mode[] = { "client", "standalone_ap", NULL };

// Total enum-value arrays: 14

static const ConsoleParamDescriptor g_params_drive_action_move[] = {
    {"speed", "int16", true, true, -1000.0, 1000.0, NULL, false},
    {"steer", "int16", true, true, -1000.0, 1000.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed[] = {
    {"value", "float", true, true, -1.0, 1.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_steer[] = {
    {"value", "float", true, true, -1.0, 1.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_slow[] = {
    {"preset", "string", true, false, 0.0, 0.0, g_enum_drive_action_speed_preset_slow_preset, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_normal[] = {
    {"preset", "string", true, false, 0.0, 0.0, g_enum_drive_action_speed_preset_normal_preset, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_turbo[] = {
    {"preset", "string", true, false, 0.0, 0.0, g_enum_drive_action_speed_preset_turbo_preset, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_move[] = {
    {"speed", "float", true, true, -1.0, 1.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_api_get_sequence[] = {
    {"name", "string", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_delete_sequence[] = {
    {"name", "string", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_test_sequence[] = {
    {"name", "string", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_play_track[] = {
    {"track", "uint16", true, true, 1.0, 999.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_api_get_catalog[] = {
    {"bank", "uint8", false, true, 1.0, 6.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_api_play_banked[] = {
    {"bank", "uint8", true, true, 1.0, 6.0, NULL, false},
    {"page", "string", true, false, 0.0, 0.0, g_enum_sound_api_play_banked_page, false},
    {"index", "uint16", true, true, 1.0, 65535.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_mood_map[] = {
    {"quiet", "uint16", true, true, 0.0, 4095.0, NULL, false},
    {"mid", "uint16", true, true, 0.0, 4095.0, NULL, false},
    {"full", "uint16", true, true, 0.0, 4095.0, NULL, false},
    {"awakeplus", "uint16", true, true, 0.0, 4095.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_category_range[] = {
    {"lo_key", "string", true, false, 0.0, 0.0, NULL, false},
    {"hi_key", "string", true, false, 0.0, 0.0, NULL, false},
    {"lo", "uint16", true, false, 0.0, 0.0, NULL, false},
    {"hi", "uint16", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_volume[] = {
    {"volume", "uint8", true, true, 0.0, 30.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_volume[] = {
    {"volume", "uint8", true, true, 0.0, 30.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_random_min[] = {
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_random_max[] = {
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_startup_track[] = {
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_boot_complete_track[] = {
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_network_down_track[] = {
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_track_assignments[] = {
    {"key", "string", true, false, 0.0, 0.0, g_enum_sound_config_track_assignments_key, false},
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_system_track_assignments[] = {
    {"key", "string", true, false, 0.0, 0.0, g_enum_sound_config_system_track_assignments_key, false},
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_category_ranges[] = {
    {"lo_key", "string", true, false, 0.0, 0.0, NULL, false},
    {"hi_key", "string", true, false, 0.0, 0.0, NULL, false},
    {"lo", "uint16", true, false, 0.0, 0.0, NULL, false},
    {"hi", "uint16", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_mood_category_map[] = {
    {"quiet", "uint16", true, true, 0.0, 4095.0, NULL, false},
    {"mid", "uint16", true, true, 0.0, 4095.0, NULL, false},
    {"full", "uint16", true, true, 0.0, 4095.0, NULL, false},
    {"awakeplus", "uint16", true, true, 0.0, 4095.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_open[] = {
    {"target", "string", true, false, 0.0, 0.0, g_enum_servo_action_open_target, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_close[] = {
    {"target", "string", true, false, 0.0, 0.0, g_enum_servo_action_close_target, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_set_position[] = {
    {"target", "string", true, false, 0.0, 0.0, g_enum_servo_action_set_position_target, false},
    {"position_us", "uint16", true, true, 500.0, 2500.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_stop[] = {
    {"target", "string", true, false, 0.0, 0.0, g_enum_servo_action_stop_target, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_action_led_color[] = {
    {"r", "uint8", true, true, 0.0, 255.0, NULL, false},
    {"g", "uint8", true, true, 0.0, 255.0, NULL, false},
    {"b", "uint8", true, true, 0.0, 255.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_action_led_effect[] = {
    {"effect", "string", true, false, 0.0, 0.0, g_enum_aux_action_led_effect_effect, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_config_led_pin[] = {
    {"aux_led_pin", "uint8", true, false, 0.0, 0.0, g_enum_aux_config_led_pin_aux_led_pin, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_config_led_count[] = {
    {"aux_led_count", "uint8", true, true, 1.0, 255.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_system_action_set_mood[] = {
    {"mood", "uint8", true, false, 0.0, 0.0, g_enum_system_action_set_mood_mood, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_wifi_config_settings[] = {
    {"mode", "string", false, false, 0.0, 0.0, g_enum_wifi_config_settings_mode, false},
    {"sta-ssid", "string", false, false, 0.0, 0.0, NULL, false},
    {"ap-ssid", "string", false, false, 0.0, 0.0, NULL, false},
    {"sta-password", "string", false, false, 0.0, 0.0, NULL, true},
    {"ap-password", "string", false, false, 0.0, 0.0, NULL, true},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_system_action_set_identity[] = {
    {"droidName", "string", true, false, 0.0, 0.0, NULL, false},
    {"mdnsUseName", "bool", false, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

static const ConsoleParamDescriptor g_params_rc_action_test_bindable[] = {
    {"token", "string", true, false, 0.0, 0.0, NULL, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false}  // terminator
};

// =============================================================================
// Status Query Field Lists (API JSON keys, verbatim)
// =============================================================================

static const char* const g_fields_dome_status_current[] = { "domeTargetSpeed", "domeEnabled", NULL };
static const char* const g_fields_dome_api_get_sequence_last_run[] = { "valid", "name", "source", "outcome", "running", "reason", "startMs", "endMs", NULL };
static const char* const g_fields_sound_status_current[] = { "driver", "capabilities", "link_ok", "active", "play_state", "device", "total_tracks", "current_track", "rx_status", "rx_detail", NULL };
static const char* const g_fields_system_status_health[] = { "estop", "sbusSignalLost", "sbusHwFailsafe", "webControlEnabled", "wifiConnected", "wifiClientConnected", "littleFsReady", "heapFree", "heapMin", "heapLargestBlock", "wifiRssi", "uptimeMs", "resetReason", NULL };
static const char* const g_fields_system_status_wifi[] = { "apSsid", "apIp", "staEnabled", "staConnected", "staIp", "staSsid", "wifiRssi", "networkRecovery", NULL };
static const char* const g_fields_dome_status_serial_link[] = { "active", "heartbeatRx", "heartbeatTx", NULL };
static const char* const g_fields_rc_status_snapshot[] = { "mode", "sbus1", "sbus2", NULL };

// Total field-name arrays: 7

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
        0,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        99,  // help_offset
        107,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        207,  // help_offset
        99,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        307,  // help_offset
        148,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        456,  // help_offset
        154,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        611,  // help_offset
        151,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        763,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        882,  // help_offset
        100,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
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
        983,  // help_offset
        119,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        1103,  // help_offset
        95,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        1199,  // help_offset
        80,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        1280,  // help_offset
        109,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        1390,  // help_offset
        139,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        1530,  // help_offset
        123,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        1654,  // help_offset
        561,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        2216,  // help_offset
        121,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        2338,  // help_offset
        105,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        2444,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        2560,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        2676,  // help_offset
        137,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        2814,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        2939,  // help_offset
        132,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        3072,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        3197,  // help_offset
        120,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        3318,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        3443,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        3559,  // help_offset
        321,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        3881,  // help_offset
        297,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        4179,  // help_offset
        148,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        4328,  // help_offset
        149,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        4478,  // help_offset
        158,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        4637,  // help_offset
        111,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        4749,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        4868,  // help_offset
        144,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        5013,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        5129,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        5254,  // help_offset
        117,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        5372,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        5485,  // help_offset
        197,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        5683,  // help_offset
        104,  // help_length
        g_fields_dome_status_current,  // fields
        true,  // is_query
        false,  // read_only
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
        5788,  // help_offset
        250,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "dome.api.list-sequences",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        6039,  // help_offset
        161,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "dome.api.list-builtin-sequences",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        6201,  // help_offset
        500,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        6702,  // help_offset
        129,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        6832,  // help_offset
        143,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        6976,  // help_offset
        222,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        7199,  // help_offset
        236,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "dome.api.get-sequence-last-run",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        7436,  // help_offset
        311,  // help_length
        g_fields_dome_api_get_sequence_last_run,  // fields
        true,  // is_query
        false,  // read_only
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
        7748,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        7864,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        7977,  // help_offset
        132,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        8110,  // help_offset
        120,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        8231,  // help_offset
        136,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        8368,  // help_offset
        142,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        8511,  // help_offset
        139,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        8651,  // help_offset
        139,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        8791,  // help_offset
        129,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        8921,  // help_offset
        150,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        9072,  // help_offset
        145,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        9218,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        9343,  // help_offset
        160,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        9504,  // help_offset
        126,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        9631,  // help_offset
        205,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        9837,  // help_offset
        265,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        10103,  // help_offset
        208,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        10312,  // help_offset
        383,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        10696,  // help_offset
        288,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        10985,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        11104,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        11197,  // help_offset
        96,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        11294,  // help_offset
        103,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        11398,  // help_offset
        103,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        11502,  // help_offset
        102,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        11605,  // help_offset
        116,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        11722,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        11821,  // help_offset
        101,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        11923,  // help_offset
        128,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        12052,  // help_offset
        125,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        12178,  // help_offset
        122,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        12301,  // help_offset
        137,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        12439,  // help_offset
        116,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        12556,  // help_offset
        140,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        12697,  // help_offset
        128,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        12826,  // help_offset
        125,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        12952,  // help_offset
        134,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        13087,  // help_offset
        122,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        13210,  // help_offset
        125,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        13336,  // help_offset
        128,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        13465,  // help_offset
        111,  // help_length
        g_fields_sound_status_current,  // fields
        true,  // is_query
        false,  // read_only
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
        13577,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "sound.config.random-min",
        "config",
        NULL,  // aliases
        g_params_sound_config_random_min,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        13675,  // help_offset
        120,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "sound.config.random-max",
        "config",
        NULL,  // aliases
        g_params_sound_config_random_max,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        13796,  // help_offset
        121,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        13918,  // help_offset
        182,  // help_length
        NULL,  // fields
        true,  // is_query
        true,  // read_only
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
        14101,  // help_offset
        183,  // help_length
        NULL,  // fields
        true,  // is_query
        true,  // read_only
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
        14285,  // help_offset
        186,  // help_length
        NULL,  // fields
        true,  // is_query
        true,  // read_only
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
        14472,  // help_offset
        184,  // help_length
        NULL,  // fields
        true,  // is_query
        true,  // read_only
    },
    {
        "sound.config.startup-track",
        "config",
        NULL,  // aliases
        g_params_sound_config_startup_track,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        14657,  // help_offset
        189,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "sound.config.boot-complete-track",
        "config",
        NULL,  // aliases
        g_params_sound_config_boot_complete_track,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        14847,  // help_offset
        190,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "sound.config.network-down-track",
        "config",
        NULL,  // aliases
        g_params_sound_config_network_down_track,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        15038,  // help_offset
        254,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "sound.config.track-assignments",
        "config",
        NULL,  // aliases
        g_params_sound_config_track_assignments,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        15293,  // help_offset
        201,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "sound.config.system-track-assignments",
        "config",
        NULL,  // aliases
        g_params_sound_config_system_track_assignments,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        15495,  // help_offset
        194,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "sound.config.category-ranges",
        "config",
        NULL,  // aliases
        g_params_sound_config_category_ranges,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        15690,  // help_offset
        195,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        15886,  // help_offset
        583,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        16470,  // help_offset
        106,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        16577,  // help_offset
        111,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        16689,  // help_offset
        137,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "servo.action.stop",
        "action",
        NULL,  // aliases
        g_params_servo_action_stop,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        16827,  // help_offset
        387,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        17215,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        17314,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        17413,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        17512,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        17611,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        17710,  // help_offset
        99,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
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
        17810,  // help_offset
        100,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        17911,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        18002,  // help_offset
        100,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
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
        18103,  // help_offset
        146,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        18250,  // help_offset
        105,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        18356,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        18455,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        18570,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        18685,  // help_offset
        116,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        18802,  // help_offset
        122,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        18925,  // help_offset
        81,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        19007,  // help_offset
        141,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        19149,  // help_offset
        246,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        19396,  // help_offset
        208,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        19605,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        19730,  // help_offset
        108,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        19839,  // help_offset
        87,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        19927,  // help_offset
        132,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        20060,  // help_offset
        189,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
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
        20250,  // help_offset
        120,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
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
        20371,  // help_offset
        75,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        20447,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        20540,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        20633,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        20724,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        20815,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        20906,  // help_offset
        110,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        21017,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        21115,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        21213,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        21311,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        21409,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        21507,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        21605,  // help_offset
        107,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        21713,  // help_offset
        87,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        21801,  // help_offset
        103,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "system.config.log-level",
        "config",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        21905,  // help_offset
        101,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        22007,  // help_offset
        132,  // help_length
        g_fields_system_status_health,  // fields
        true,  // is_query
        false,  // read_only
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
        22140,  // help_offset
        221,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
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
        22362,  // help_offset
        64,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        22427,  // help_offset
        114,  // help_length
        g_fields_system_status_wifi,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "wifi.config.settings",
        "config",
        NULL,  // aliases
        g_params_wifi_config_settings,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        22542,  // help_offset
        390,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        22933,  // help_offset
        116,  // help_length
        g_fields_dome_status_serial_link,  // fields
        true,  // is_query
        false,  // read_only
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
        23050,  // help_offset
        108,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        23159,  // help_offset
        198,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
    },
    {
        "system.api.get-profiler",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        PA_HEAP_PROFILE,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        23358,  // help_offset
        347,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        23706,  // help_offset
        412,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        24119,  // help_offset
        247,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        24367,  // help_offset
        138,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        24506,  // help_offset
        208,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        24715,  // help_offset
        123,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        24839,  // help_offset
        252,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        25092,  // help_offset
        140,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        25233,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        25352,  // help_offset
        191,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        25544,  // help_offset
        120,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
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
        25665,  // help_offset
        125,  // help_length
        g_fields_rc_status_snapshot,  // fields
        true,  // is_query
        false,  // read_only
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
        25791,  // help_offset
        104,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        25896,  // help_offset
        148,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        26045,  // help_offset
        85,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        26131,  // help_offset
        195,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        26327,  // help_offset
        177,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        26505,  // help_offset
        96,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        26602,  // help_offset
        152,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        26755,  // help_offset
        125,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        26881,  // help_offset
        149,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        27031,  // help_offset
        139,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        27171,  // help_offset
        154,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        27326,  // help_offset
        152,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        27479,  // help_offset
        147,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        27627,  // help_offset
        157,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        27785,  // help_offset
        165,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        27951,  // help_offset
        168,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        28120,  // help_offset
        174,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        28295,  // help_offset
        196,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        28492,  // help_offset
        171,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        28664,  // help_offset
        156,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        28821,  // help_offset
        198,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        29020,  // help_offset
        215,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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
        29236,  // help_offset
        161,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
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

// =============================================================================
// Named Body/Dome Sequences (registry marcduino_cmd, literal DM:<NAME> only)
// =============================================================================

typedef struct {
    const char* operationName;
    const char* sequence;
} ConsoleSequenceRow;

static const ConsoleSequenceRow g_sequenceRows[] = {
    {"dome.seq.vader", "DM:VADER"},
    {"dome.seq.hello", "DM:HELLO"},
    {"dome.seq.nod", "DM:NOD"},
    {"dome.seq.flutter", "DM:FLUTTER"},
    {"dome.seq.bloom", "DM:BLOOM"},
    {"dome.seq.leia", "DM:LEIA"},
    {"dome.seq.alarm", "DM:ALARM"},
    {"dome.seq.heart", "DM:HEART"},
    {"dome.seq.reset", "DM:RESET"},
    {"dome.seq.pies", "DM:PIES"},
    {"dome.seq.low", "DM:LOW"},
    {"dome.seq.openall", "DM:OPENALL"},
    {"dome.seq.cantina", "DM:CANTINA"},
    {"dome.seq.rockmarch", "DM:ROCKMARCH"},
    {"dome.seq.scream", "DM:SCREAM"},
    {"dome.seq.overload", "DM:OVERLOAD"},
};

static const size_t g_sequenceRowCount = sizeof(g_sequenceRows) / sizeof(g_sequenceRows[0]);

const char* consoleCatalogSequenceFor(const char* operationName) {
    if (!operationName) return NULL;
    for (size_t i = 0; i < g_sequenceRowCount; ++i) {
        if (strcmp(g_sequenceRows[i].operationName, operationName) == 0) {
            return g_sequenceRows[i].sequence;
        }
    }
    return NULL;
}
