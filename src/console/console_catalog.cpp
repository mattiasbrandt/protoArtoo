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
// (ADR 0029): available_on_board is the entry's `board_capability:` macro and
// available_in_build its `build_flag:`, emitted by name rather than resolved
// here. A board that sets PA_CAP_DRIVE_BACKEND_HOVERBOARD=0 or
// PA_CAP_HOSTED_WIFI=0 flips those rows with no generator change and no
// regeneration.
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
static const char* const g_enum_servo_action_open_target[] = { "both", NULL };
static const char* const g_enum_servo_action_close_target[] = { "both", NULL };
static const char* const g_enum_servo_action_release_target[] = { "both", NULL };
static const char* const g_enum_servo_action_stop_target[] = { "both", NULL };
static const char* const g_enum_aux_action_led_effect_effect[] = { "solid", "blink", "pulse", "off", NULL };
static const char* const g_enum_system_action_set_mood_mood[] = { "10", "11", "13", "14", NULL };
static const char* const g_enum_wifi_config_settings_mode[] = { "client", "standalone_ap", NULL };

// Total enum-value arrays: 13

static const ConsoleParamDescriptor g_params_drive_action_move[] = {
    {"speed", "int16", true, true, -1000.0, 1000.0, NULL, false, false},
    {"steer", "int16", true, true, -1000.0, 1000.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed[] = {
    {"value", "float", true, true, -1.0, 1.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_steer[] = {
    {"value", "float", true, true, -1.0, 1.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_slow[] = {
    {"preset", "string", true, false, 0.0, 0.0, g_enum_drive_action_speed_preset_slow_preset, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_normal[] = {
    {"preset", "string", true, false, 0.0, 0.0, g_enum_drive_action_speed_preset_normal_preset, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_drive_action_speed_preset_turbo[] = {
    {"preset", "string", true, false, 0.0, 0.0, g_enum_drive_action_speed_preset_turbo_preset, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_move[] = {
    {"speed", "float", true, true, -1.0, 1.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_api_get_sequence[] = {
    {"name", "string", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_delete_sequence[] = {
    {"name", "string", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_dome_action_test_sequence[] = {
    {"name", "string", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_play_track[] = {
    {"track", "uint16", true, true, 1.0, 999.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_api_play_banked[] = {
    {"bank", "uint8", true, true, 1.0, 6.0, NULL, false, false},
    {"page", "string", true, false, 0.0, 0.0, g_enum_sound_api_play_banked_page, false, false},
    {"index", "uint16", true, true, 1.0, 65535.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_mood_map[] = {
    {"quiet", "uint16", true, true, 0.0, 4095.0, NULL, false, false},
    {"mid", "uint16", true, true, 0.0, 4095.0, NULL, false, false},
    {"full", "uint16", true, true, 0.0, 4095.0, NULL, false, false},
    {"awakeplus", "uint16", true, true, 0.0, 4095.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_category_range[] = {
    {"lo_key", "string", true, false, 0.0, 0.0, NULL, false, false},
    {"hi_key", "string", true, false, 0.0, 0.0, NULL, false, false},
    {"lo", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {"hi", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_action_set_volume[] = {
    {"volume", "uint8", true, true, 0.0, 30.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_volume[] = {
    {"volume", "uint8", true, true, 0.0, 30.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_random_min[] = {
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_random_max[] = {
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_startup_track[] = {
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_boot_complete_track[] = {
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_network_down_track[] = {
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_track_assignments[] = {
    {"key", "string", true, false, 0.0, 0.0, g_enum_sound_config_track_assignments_key, false, false},
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_system_track_assignments[] = {
    {"key", "string", true, false, 0.0, 0.0, g_enum_sound_config_system_track_assignments_key, false, false},
    {"track", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_category_ranges[] = {
    {"lo_key", "string", true, false, 0.0, 0.0, NULL, false, false},
    {"hi_key", "string", true, false, 0.0, 0.0, NULL, false, false},
    {"lo", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {"hi", "uint16", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_sound_config_mood_category_map[] = {
    {"quiet", "uint16", true, true, 0.0, 4095.0, NULL, false, false},
    {"mid", "uint16", true, true, 0.0, 4095.0, NULL, false, false},
    {"full", "uint16", true, true, 0.0, 4095.0, NULL, false, false},
    {"awakeplus", "uint16", true, true, 0.0, 4095.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_open[] = {
    {"target", "string", true, false, 0.0, 0.0, g_enum_servo_action_open_target, false, true},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_close[] = {
    {"target", "string", true, false, 0.0, 0.0, g_enum_servo_action_close_target, false, true},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_set_position[] = {
    {"target", "string", true, false, 0.0, 0.0, NULL, false, true},
    {"position_us", "uint16", true, true, 500.0, 2500.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_nudge[] = {
    {"target", "string", true, false, 0.0, 0.0, NULL, false, true},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_travel[] = {
    {"target", "string", true, false, 0.0, 0.0, NULL, false, true},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_hold[] = {
    {"target", "string", true, false, 0.0, 0.0, NULL, false, true},
    {"position_us", "uint16", true, true, 500.0, 2500.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_release[] = {
    {"target", "string", true, false, 0.0, 0.0, g_enum_servo_action_release_target, false, true},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_servo_action_stop[] = {
    {"target", "string", true, false, 0.0, 0.0, g_enum_servo_action_stop_target, false, true},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_action_led_color[] = {
    {"r", "uint8", true, true, 0.0, 255.0, NULL, false, false},
    {"g", "uint8", true, true, 0.0, 255.0, NULL, false, false},
    {"b", "uint8", true, true, 0.0, 255.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_action_led_effect[] = {
    {"effect", "string", true, false, 0.0, 0.0, g_enum_aux_action_led_effect_effect, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_aux_config_led_count[] = {
    {"target", "string", true, false, 0.0, 0.0, NULL, false, true},
    {"value", "uint8", false, true, 1.0, 255.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_system_action_set_mood[] = {
    {"mood", "uint8", true, false, 0.0, 0.0, g_enum_system_action_set_mood_mood, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_wifi_config_settings[] = {
    {"mode", "string", false, false, 0.0, 0.0, g_enum_wifi_config_settings_mode, false, false},
    {"sta-ssid", "string", false, false, 0.0, 0.0, NULL, false, false},
    {"ap-ssid", "string", false, false, 0.0, 0.0, NULL, false, false},
    {"sta-password", "string", false, false, 0.0, 0.0, NULL, true, false},
    {"ap-password", "string", false, false, 0.0, 0.0, NULL, true, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_system_action_set_identity[] = {
    {"droidName", "string", true, false, 0.0, 0.0, NULL, false, false},
    {"mdnsUseName", "bool", false, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

static const ConsoleParamDescriptor g_params_rc_action_test_bindable[] = {
    {"token", "string", true, false, 0.0, 0.0, NULL, false, false},
    {NULL, NULL, false, false, 0.0, 0.0, NULL, false, false}  // terminator
};

// =============================================================================
// Status Query Field Lists (API JSON keys, verbatim)
// =============================================================================

static const char* const g_fields_dome_status_current[] = { "domeTargetSpeed", "domeEnabled", NULL };
static const char* const g_fields_dome_api_get_sequence_last_run[] = { "valid", "name", "source", "outcome", "running", "reason", "startMs", "endMs", NULL };
static const char* const g_fields_sound_api_get_catalog[] = { "ready", NULL };
static const char* const g_fields_sound_api_get_mood_map[] = { "quiet", "mid", "full", "awakeplus", NULL };
static const char* const g_fields_sound_status_current[] = { "driver", "output", "capabilities", "link_ok", "active", "play_state", "device", "total_tracks", "current_track", "missing_track", "rx_status", "rx_detail", NULL };
static const char* const g_fields_system_status_health[] = { "estop", "sbusSignalLost", "sbusHwFailsafe", "webControlEnabled", "wifiConnected", "wifiClientConnected", "littleFsReady", "heapFree", "heapMin", "heapLargestBlock", "wifiRssi", "uptimeMs", "resetReason", NULL };
static const char* const g_fields_system_status_wifi[] = { "apSsid", "apIp", "staEnabled", "staConnected", "staIp", "staSsid", "wifiRssi", "networkRecovery", NULL };
static const char* const g_fields_dome_status_serial_link[] = { "active", "heartbeatRx", "heartbeatTx", NULL };
static const char* const g_fields_system_api_get_identity[] = { "droidName", "mdnsUseName", NULL };
static const char* const g_fields_system_api_get_validation[] = { "updatedMs", "drive", "domeLink", "audio", "rc", NULL };
static const char* const g_fields_rc_status_snapshot[] = { "mode", "sbus1", "sbus2", NULL };

// Total field-name arrays: 11

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
        125,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        126,  // help_offset
        107,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        234,  // help_offset
        105,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        340,  // help_offset
        121,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        462,  // help_offset
        127,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        590,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        715,  // help_offset
        136,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        852,  // help_offset
        102,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
        NULL,  // output
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
        955,  // help_offset
        116,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        1072,  // help_offset
        124,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        1197,  // help_offset
        81,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        1279,  // help_offset
        99,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        1379,  // help_offset
        125,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        1505,  // help_offset
        110,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        1616,  // help_offset
        132,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        1749,  // help_offset
        102,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        1852,  // help_offset
        93,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        1946,  // help_offset
        103,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        2050,  // help_offset
        104,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        2155,  // help_offset
        128,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        2284,  // help_offset
        107,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        2392,  // help_offset
        123,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        2516,  // help_offset
        106,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        2623,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        2736,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        2851,  // help_offset
        103,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        2955,  // help_offset
        137,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        3093,  // help_offset
        128,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        3222,  // help_offset
        115,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        3338,  // help_offset
        116,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        3455,  // help_offset
        119,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        3575,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        3688,  // help_offset
        99,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        3788,  // help_offset
        122,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        3911,  // help_offset
        95,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        4007,  // help_offset
        96,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        4104,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        4197,  // help_offset
        88,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        4286,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        4399,  // help_offset
        108,  // help_length
        g_fields_dome_status_current,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        4508,  // help_offset
        141,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        4650,  // help_offset
        151,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        4802,  // help_offset
        126,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        4929,  // help_offset
        119,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        5049,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        5162,  // help_offset
        144,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        5307,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        5420,  // help_offset
        146,  // help_length
        g_fields_dome_api_get_sequence_last_run,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        5567,  // help_offset
        103,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        5671,  // help_offset
        86,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        5758,  // help_offset
        99,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        5858,  // help_offset
        96,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        5955,  // help_offset
        107,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        6063,  // help_offset
        104,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        6168,  // help_offset
        106,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        6275,  // help_offset
        110,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        6386,  // help_offset
        101,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        6488,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "sound.api.get-catalog",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        6603,  // help_offset
        94,  // help_length
        g_fields_sound_api_get_catalog,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        6698,  // help_offset
        123,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        6822,  // help_offset
        150,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "sound.api.get-mood-map",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        6973,  // help_offset
        109,  // help_length
        g_fields_sound_api_get_mood_map,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        7083,  // help_offset
        202,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        7286,  // help_offset
        172,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        7459,  // help_offset
        133,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        7593,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        7686,  // help_offset
        106,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        7793,  // help_offset
        94,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        7888,  // help_offset
        77,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        7966,  // help_offset
        82,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8049,  // help_offset
        93,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8143,  // help_offset
        89,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8233,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8324,  // help_offset
        105,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8430,  // help_offset
        93,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8524,  // help_offset
        99,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8624,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8717,  // help_offset
        89,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8807,  // help_offset
        86,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8894,  // help_offset
        101,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        8996,  // help_offset
        80,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        9077,  // help_offset
        104,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        9182,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        9275,  // help_offset
        89,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        9365,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        9464,  // help_offset
        86,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        9551,  // help_offset
        89,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        9641,  // help_offset
        92,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        9734,  // help_offset
        121,  // help_length
        g_fields_sound_status_current,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        9856,  // help_offset
        98,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        9955,  // help_offset
        111,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        10067,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        10180,  // help_offset
        146,  // help_length
        NULL,  // fields
        true,  // is_query
        true,  // read_only
        NULL,  // output
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
        10327,  // help_offset
        147,  // help_length
        NULL,  // fields
        true,  // is_query
        true,  // read_only
        NULL,  // output
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
        10475,  // help_offset
        150,  // help_length
        NULL,  // fields
        true,  // is_query
        true,  // read_only
        NULL,  // output
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
        10626,  // help_offset
        148,  // help_length
        NULL,  // fields
        true,  // is_query
        true,  // read_only
        NULL,  // output
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
        10775,  // help_offset
        150,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        10926,  // help_offset
        155,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        11082,  // help_offset
        162,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        11245,  // help_offset
        142,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        11388,  // help_offset
        168,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        11557,  // help_offset
        172,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        11730,  // help_offset
        215,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        11946,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        12037,  // help_offset
        94,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        12132,  // help_offset
        138,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "servo.action.nudge",
        "action",
        NULL,  // aliases
        g_params_servo_action_nudge,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        12271,  // help_offset
        185,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "servo.action.travel",
        "action",
        NULL,  // aliases
        g_params_servo_action_travel,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        12457,  // help_offset
        170,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "servo.action.hold",
        "action",
        NULL,  // aliases
        g_params_servo_action_hold,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        12628,  // help_offset
        205,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "servo.action.release",
        "action",
        NULL,  // aliases
        g_params_servo_action_release,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        12834,  // help_offset
        142,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "servo.action.centre-all",
        "action",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        12977,  // help_offset
        195,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        13173,  // help_offset
        109,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        13283,  // help_offset
        91,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        "arm1",  // output
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
        13375,  // help_offset
        91,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        "arm2",  // output
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
        13467,  // help_offset
        91,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        "aux1",  // output
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
        13559,  // help_offset
        91,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        "aux2",  // output
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
        13651,  // help_offset
        91,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        "aux3",  // output
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
        13743,  // help_offset
        88,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "servo.api.get-outputs",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        13832,  // help_offset
        161,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        13994,  // help_offset
        107,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        14102,  // help_offset
        96,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        14199,  // help_offset
        117,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
        NULL,  // output
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
        14317,  // help_offset
        153,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        14471,  // help_offset
        121,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        14593,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        14684,  // help_offset
        110,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        14795,  // help_offset
        107,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        14903,  // help_offset
        125,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        15029,  // help_offset
        73,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "system.action.reboot-wifi-module",
        "action",
        NULL,  // aliases
        NULL,
        PA_CAP_HOSTED_WIFI,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        15103,  // help_offset
        209,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        15313,  // help_offset
        127,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        15441,  // help_offset
        134,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        15576,  // help_offset
        104,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        15681,  // help_offset
        110,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        15792,  // help_offset
        79,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        15872,  // help_offset
        68,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        15941,  // help_offset
        102,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        16044,  // help_offset
        106,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
        NULL,  // output
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
        16151,  // help_offset
        104,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
        NULL,  // output
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
        16256,  // help_offset
        73,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        16330,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        "arm1",  // output
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
        16445,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        "arm2",  // output
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
        16560,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        "aux1",  // output
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
        16675,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        "aux2",  // output
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
        16790,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        "aux3",  // output
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
        16905,  // help_offset
        110,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        17016,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        17114,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        17212,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        17310,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        17408,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        17506,  // help_offset
        97,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        17604,  // help_offset
        109,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        17714,  // help_offset
        89,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        17804,  // help_offset
        105,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        17910,  // help_offset
        106,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        18017,  // help_offset
        130,  // help_length
        g_fields_system_status_health,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        18148,  // help_offset
        130,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
        NULL,  // output
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
        18279,  // help_offset
        56,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        18336,  // help_offset
        88,  // help_length
        g_fields_system_status_wifi,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        18425,  // help_offset
        281,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        18707,  // help_offset
        129,  // help_length
        g_fields_dome_status_serial_link,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "system.api.get-identity",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        18837,  // help_offset
        127,  // help_length
        g_fields_system_api_get_identity,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "system.api.get-components",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        18965,  // help_offset
        158,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        19124,  // help_offset
        216,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        19341,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        19460,  // help_offset
        158,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        19619,  // help_offset
        120,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        19740,  // help_offset
        102,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        19843,  // help_offset
        79,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        19923,  // help_offset
        99,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        20023,  // help_offset
        151,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "system.api.get-validation",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        20175,  // help_offset
        110,  // help_length
        g_fields_system_api_get_validation,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        20286,  // help_offset
        101,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        20388,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        20507,  // help_offset
        79,  // help_length
        NULL,  // fields
        false,  // is_query
        false,  // read_only
        NULL,  // output
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
        20587,  // help_offset
        109,  // help_length
        g_fields_rc_status_snapshot,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        20697,  // help_offset
        90,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
    },
    {
        "rc.api.get-bindable-actions",
        "status",
        NULL,  // aliases
        NULL,
        1,  // available_on_board
        1,  // available_in_build
        false,  // requires_web_control
        false,  // safety_critical
        20788,  // help_offset
        110,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        20899,  // help_offset
        69,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        20969,  // help_offset
        102,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        21072,  // help_offset
        141,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        21214,  // help_offset
        94,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        21309,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        21422,  // help_offset
        110,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        21533,  // help_offset
        117,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        21651,  // help_offset
        122,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        21774,  // help_offset
        118,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        21893,  // help_offset
        128,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        22022,  // help_offset
        112,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        22135,  // help_offset
        117,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        22253,  // help_offset
        134,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        22388,  // help_offset
        127,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        22516,  // help_offset
        132,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        22649,  // help_offset
        149,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        22799,  // help_offset
        140,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        22940,  // help_offset
        134,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        23075,  // help_offset
        114,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        23190,  // help_offset
        123,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
        23314,  // help_offset
        133,  // help_length
        NULL,  // fields
        true,  // is_query
        false,  // read_only
        NULL,  // output
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
