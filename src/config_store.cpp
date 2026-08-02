// =============================================================================
// src/config_store.cpp
//
// Config schema module implementation — centralized NVS load/save and validation.
// =============================================================================

#include "config_store.h"

#include "api_helpers.h"
#include "audio_dollar_parser.h"
#include "config.h"
#include "config_serializer.h"
#include "config_nvsio.h"
#include "logging.h"
#include "rc_mapping.h"

#include <cstring>

// IPAddress.h is only available in Arduino/ESP-IDF environments, not native tests
#ifdef ARDUINO
#include <IPAddress.h>
#endif

namespace {

struct AudioTrackKeyMapEntry {
    const char* key;
    uint16_t AudioConfig::*field;
};

constexpr AudioTrackKeyMapEntry AUDIO_TRACK_KEYS[] = {
    {"scream", &AudioConfig::snd_scream},
    {"faint", &AudioConfig::snd_faint},
    {"leia", &AudioConfig::snd_leia},
    {"cantina_s", &AudioConfig::snd_cantina_s},
    {"sw_theme", &AudioConfig::snd_sw_theme},
    {"imp_march", &AudioConfig::snd_imp_march},
    {"cantina_l", &AudioConfig::snd_cantina_l},
    {"startup", &AudioConfig::snd_startup},
    {"doodoo", &AudioConfig::snd_doodoo},
    {"failure", &AudioConfig::snd_failure},
    {"disco", &AudioConfig::snd_disco},
    {"mahna", &AudioConfig::snd_mahna},
    {"inlove", &AudioConfig::snd_inlove},
    {"macho", &AudioConfig::snd_macho},
    {"gangnam", &AudioConfig::snd_gangnam},
    {"uptown", &AudioConfig::snd_uptown},
    {"celebr", &AudioConfig::snd_celebr},
    {"stayin", &AudioConfig::snd_stayin},
    {"harlem", &AudioConfig::snd_harlem},
    {"pbjtime", &AudioConfig::snd_pbjtime},
    {"sys_boot", &AudioConfig::snd_sys_boot},
    {"sys_mode_n", &AudioConfig::snd_sys_mode_n},
    {"sys_mode_s", &AudioConfig::snd_sys_mode_s},
    {"sys_mode_t", &AudioConfig::snd_sys_mode_t},
    {"sys_drv_on", &AudioConfig::snd_sys_drv_on},
    {"sys_dome_on", &AudioConfig::snd_sys_dome_on},
    {"rand_min", &AudioConfig::snd_rand_min},
    {"rand_max", &AudioConfig::snd_rand_max},
    {"snd_int_quiet", &AudioConfig::snd_int_quiet},
    {"snd_int_mid", &AudioConfig::snd_int_mid},
    {"snd_int_full", &AudioConfig::snd_int_full},
    {"snd_int_awake", &AudioConfig::snd_int_awake},
    {"snd_cat_gen_lo", &AudioConfig::snd_cat_gen_lo},
    {"snd_cat_gen_hi", &AudioConfig::snd_cat_gen_hi},
    {"snd_cat_chat_lo", &AudioConfig::snd_cat_chat_lo},
    {"snd_cat_chat_hi", &AudioConfig::snd_cat_chat_hi},
    {"snd_cat_hap_lo", &AudioConfig::snd_cat_hap_lo},
    {"snd_cat_hap_hi", &AudioConfig::snd_cat_hap_hi},
    {"snd_cat_proc_lo", &AudioConfig::snd_cat_proc_lo},
    {"snd_cat_proc_hi", &AudioConfig::snd_cat_proc_hi},
    {"snd_cat_sad_lo", &AudioConfig::snd_cat_sad_lo},
    {"snd_cat_sad_hi", &AudioConfig::snd_cat_sad_hi},
    {"snd_cat_sent_lo", &AudioConfig::snd_cat_sent_lo},
    {"snd_cat_sent_hi", &AudioConfig::snd_cat_sent_hi},
    {"snd_cat_hum_lo", &AudioConfig::snd_cat_hum_lo},
    {"snd_cat_hum_hi", &AudioConfig::snd_cat_hum_hi},
    {"snd_cat_scrm_lo", &AudioConfig::snd_cat_scrm_lo},
    {"snd_cat_scrm_hi", &AudioConfig::snd_cat_scrm_hi},
    {"snd_cat_ooh_lo", &AudioConfig::snd_cat_ooh_lo},
    {"snd_cat_ooh_hi", &AudioConfig::snd_cat_ooh_hi},
    {"snd_cat_alrm_lo", &AudioConfig::snd_cat_alrm_lo},
    {"snd_cat_alrm_hi", &AudioConfig::snd_cat_alrm_hi},
    {"snd_cat_snrk_lo", &AudioConfig::snd_cat_snarky_lo},
    {"snd_cat_snrk_hi", &AudioConfig::snd_cat_snarky_hi},
    {"snd_cat_whis_lo", &AudioConfig::snd_cat_whis_lo},
    {"snd_cat_whis_hi", &AudioConfig::snd_cat_whis_hi},
};

const AudioTrackKeyMapEntry* audioTrackKeyEntry(const char* key) {
    if (key == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < sizeof(AUDIO_TRACK_KEYS) / sizeof(AUDIO_TRACK_KEYS[0]); ++i) {
        if (strcmp(AUDIO_TRACK_KEYS[i].key, key) == 0) {
            return &AUDIO_TRACK_KEYS[i];
        }
    }
    return nullptr;
}

}  // namespace

// Helper: Populate ConfigSnapshot with defaults
void configSnapshotDefaults(ConfigSnapshot* snap) {
    snprintf(snap->system.droid_name, sizeof(snap->system.droid_name), "%s", DROID_NAME_DEFAULT);
    snap->system.mdns_use_name = false;
    snap->drive.speedLimitMax = SPEED_LIMIT_MAX;
    snap->drive.speedPresetSlow = SPEED_PRESET_SLOW;
    snap->drive.speedPresetNormal = SPEED_PRESET_NORMAL;
    snap->drive.speedPresetTurbo = SPEED_PRESET_TURBO;
    snap->drive.speedPresetActive = SpeedPresetId::Normal;
    snap->drive.sbusTimeoutMs = SBUS_TIMEOUT_MS;
    snap->drive.webDriveTimeoutMs = WEB_DRIVE_TIMEOUT_MS;
    snap->audio.audioVolume = 20;
    snap->system.logLevel = PA_LOG_LEVEL;
    snap->audio.snd_scream = AUDIO_TRACK_SCREAM;
    snap->audio.snd_faint = AUDIO_TRACK_FAINT;
    snap->audio.snd_leia = AUDIO_TRACK_LEIA;
    snap->audio.snd_cantina_s = AUDIO_TRACK_CANTINA_S;
    snap->audio.snd_sw_theme = AUDIO_TRACK_SW_THEME;
    snap->audio.snd_imp_march = AUDIO_TRACK_IMP_MARCH;
    snap->audio.snd_cantina_l = AUDIO_TRACK_CANTINA_L;
    snap->audio.snd_startup = AUDIO_TRACK_STARTUP;
    snap->audio.snd_doodoo = 0;
    snap->audio.snd_failure = 0;
    snap->audio.snd_disco = 0;
    snap->audio.snd_happy = AUDIO_TRACK_HAPPY;
    snap->audio.snd_mahna = 0;
    snap->audio.snd_inlove = 0;
    snap->audio.snd_macho = 0;
    snap->audio.snd_gangnam = 0;
    snap->audio.snd_uptown = 0;
    snap->audio.snd_celebr = 0;
    snap->audio.snd_stayin = 0;
    snap->audio.snd_harlem = 0;
    snap->audio.snd_pbjtime = 0;
    snap->audio.snd_sys_boot = 0;
    snap->audio.snd_sys_mode_n = 0;
    snap->audio.snd_sys_mode_s = 0;
    snap->audio.snd_sys_mode_t = 0;
    snap->audio.snd_sys_drv_on = 0;
    snap->audio.snd_sys_dome_on = 0;
    snap->audio.snd_rand_min = AUDIO_RAND_TRACK_MIN;
    snap->audio.snd_rand_max = AUDIO_RAND_TRACK_MAX;
    snap->audio.snd_int_quiet = AUDIO_RAND_INT_QUIET;
    snap->audio.snd_int_mid = AUDIO_RAND_INT_MID;
    snap->audio.snd_int_full = AUDIO_RAND_INT_FULL;
    snap->audio.snd_int_awake = AUDIO_RAND_INT_AWAKE;
    snap->audio.snd_moodcat_quiet = 0x0048;
    snap->audio.snd_moodcat_mid = 0x004F;
    snap->audio.snd_moodcat_full = 0x090F;
    snap->audio.snd_moodcat_awakeplus = 0x0F8F;
    snap->audio.snd_cat_gen_lo = 0;
    snap->audio.snd_cat_gen_hi = 0;
    snap->audio.snd_cat_chat_lo = 0;
    snap->audio.snd_cat_chat_hi = 0;
    snap->audio.snd_cat_hap_lo = 0;
    snap->audio.snd_cat_hap_hi = 0;
    snap->audio.snd_cat_proc_lo = 0;
    snap->audio.snd_cat_proc_hi = 0;
    snap->audio.snd_cat_sad_lo = 0;
    snap->audio.snd_cat_sad_hi = 0;
    snap->audio.snd_cat_sent_lo = 0;
    snap->audio.snd_cat_sent_hi = 0;
    snap->audio.snd_cat_hum_lo = 0;
    snap->audio.snd_cat_hum_hi = 0;
    snap->audio.snd_cat_scrm_lo = 0;
    snap->audio.snd_cat_scrm_hi = 0;
    snap->audio.snd_cat_ooh_lo = 0;
    snap->audio.snd_cat_ooh_hi = 0;
    snap->audio.snd_cat_alrm_lo = 0;
    snap->audio.snd_cat_alrm_hi = 0;
    snap->audio.snd_cat_snarky_lo = 0;
    snap->audio.snd_cat_snarky_hi = 0;
    snap->audio.snd_cat_whis_lo = 0;
    snap->audio.snd_cat_whis_hi = 0;

    snap->servo.arm1_open_us = 2000;
    snap->servo.arm1_close_us = 1000;
    snap->servo.arm2_open_us = 2000;
    snap->servo.arm2_close_us = 1000;
    snap->servo.arm1_type = SERVO_COMP_MG996R;
    snap->servo.arm2_type = SERVO_COMP_MG996R;
    snap->servo.aux1_open_us = 2000;
    snap->servo.aux1_close_us = 1000;
    snap->servo.aux2_open_us = 2000;
    snap->servo.aux2_close_us = 1000;
    snap->servo.aux3_open_us = 2000;
    snap->servo.aux3_close_us = 1000;
    snap->servo.aux1_type = SERVO_COMP_NONE;
    snap->servo.aux2_type = SERVO_COMP_NONE;
    snap->servo.aux3_type = SERVO_COMP_NONE;

    snap->dome.dome_min_speed = 0.0f;
    snap->dome.dome_max_speed = 1.0f;
    snap->dome.dome_neutral_us = 1500;
    snap->dome.dome_min_pulse_us = 1000;
    snap->dome.dome_max_pulse_us = 2000;
    snap->dome.dome_speed_limit_pct = 100;
    snap->dome.dome_rnd_enable = false;
    snap->dome.dome_rnd_speed_pct = 30;
    snap->dome.dome_rnd_pause_min = 6;
    snap->dome.dome_rnd_pause_max = 12;
    snap->dome.dome_rnd_move_ms = 2500;
    snap->dome.dome_wifi_peer_ip[0] = '\0';

    // Device WiFi Settings default to an Unprovisioned Controller (ADR 0015):
    // no saved posture yet, AP identity pre-filled with the documented,
    // operator-changeable Default AP Credential.
    snap->wifi.provisioned = false;
    snap->wifi.mode = WifiMode::CLIENT;
    snap->wifi.sta_ssid[0] = '\0';
    snap->wifi.sta_password[0] = '\0';
    snprintf(snap->wifi.ap_ssid, sizeof(snap->wifi.ap_ssid), "%s", WIFI_AP_SSID);
    snprintf(snap->wifi.ap_password, sizeof(snap->wifi.ap_password), "%s", WIFI_DEFAULT_AP_PASSWORD);

    snap->servo.seq_open_ms = 1000;
    snap->servo.seq_close_ms = 1000;

    snap->servo.aux_led_pin = AUX_LED_PIN_DISABLED;
    snap->servo.aux_led_count = AUX_LED_COUNT_DEFAULT;

    snap->system.enable_arm1 = false;
    snap->system.enable_arm2 = false;
    snap->system.enable_aux1 = false;
    snap->system.enable_aux2 = false;
    snap->system.enable_aux3 = false;
    snap->system.enable_dome = false;
    snap->system.enable_rc_ch1 = false;
    snap->system.enable_rc_ch2 = false;
    snap->system.enable_rc_ch3 = false;
    snap->system.enable_rc_ch4 = false;
    snap->system.enable_rc_ch5 = false;
    snap->system.enable_rc_ch6 = false;
    snap->system.single_sbus_use_ch2 = false;
    snap->system.enable_s1_hoverboard = false;
    snap->system.enable_s2_sound = false;
    snap->system.enable_s3_dome_ctrl = false;
    snap->system.stationary = false;
    snap->system.rc_input_mode = RC_INPUT_DUAL_SBUS;

    snap->system.rc_pwm_drive_speed = defaultPwmBinding(1);
    snap->system.rc_pwm_drive_steer = defaultPwmBinding(2);
    snap->system.rc_pwm_dome_speed = defaultPwmBinding(3);
    snap->system.rc_pwm_arm1 = defaultPwmBinding(4);
    snap->system.rc_pwm_arm2 = defaultPwmBinding(5);
    snap->system.rc_pwm_sound = defaultPwmBinding(6);

    snap->system.rc_sbus_drive_speed = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    snap->system.rc_sbus_drive_steer = defaultSbusBinding(RC_BINDING_SBUS1, 2);
    snap->system.rc_sbus_dome_speed = defaultSbusBinding(RC_BINDING_SBUS2, 1);
    snap->system.rc_sbus_arm1 = defaultSbusBinding(RC_BINDING_SBUS2, 2);
    snap->system.rc_sbus_arm2 = defaultSbusBinding(RC_BINDING_SBUS2, 3);
    snap->system.rc_sbus_sound = disabledRcBinding();

    snap->system.rc_arm1 = makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                                         RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                                         RC_SBUS_DEFAULT_MAX, 0,
                                         rcTriggerDefaultReverse(RC_BINDING_SBUS1, 4));
    snap->system.rc_arm2 = makeRcTriggerBinding(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM2_TOGGLE, nullptr,
                                         RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                                         RC_SBUS_DEFAULT_MAX, 0,
                                         rcTriggerDefaultReverse(RC_BINDING_SBUS1, 5));
    snap->system.rc_aux1 = disabledRcTriggerBinding();
    snap->system.rc_aux2 = disabledRcTriggerBinding();
    snap->system.rc_aux3 = disabledRcTriggerBinding();
    snap->system.rc_sound = disabledRcTriggerBinding();
    snap->system.rc_opmode = disabledRcTriggerBinding();
    snap->system.rc_free0 = disabledRcTriggerBinding();
    snap->system.rc_free1 = disabledRcTriggerBinding();
    snap->system.rc_free2 = disabledRcTriggerBinding();
    snap->system.rc_free3 = disabledRcTriggerBinding();
}

// =============================================================================
// Public API Implementation
// =============================================================================

WifiConfigView wifiConfigToView(const WifiConfig& cfg) {
    WifiConfigView view = {};
    view.provisioned = cfg.provisioned;
    view.mode = cfg.mode;
    snprintf(view.sta_ssid, sizeof(view.sta_ssid), "%s", cfg.sta_ssid);
    view.sta_password_set = cfg.sta_password[0] != '\0';
    snprintf(view.ap_ssid, sizeof(view.ap_ssid), "%s", cfg.ap_ssid);
    view.ap_password_set = cfg.ap_password[0] != '\0';
    return view;
}

bool wifiConfigsDiffer(const WifiConfig& a, const WifiConfig& b) {
    return a.provisioned != b.provisioned || a.mode != b.mode ||
           strcmp(a.sta_ssid, b.sta_ssid) != 0 || strcmp(a.sta_password, b.sta_password) != 0 ||
           strcmp(a.ap_ssid, b.ap_ssid) != 0 || strcmp(a.ap_password, b.ap_password) != 0;
}

ConfigSnapshot configCache = {};
WifiConfig activeWifiConfig = {};
bool activeWifiRecovery = false;
portMUX_TYPE configCacheMux = portMUX_INITIALIZER_UNLOCKED;

void configCacheRead(ConfigSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&configCacheMux);
    *out = configCache;
    taskEXIT_CRITICAL(&configCacheMux);
}

void configCacheReadDome(DomeConfig* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&configCacheMux);
    *out = configCache.dome;
    taskEXIT_CRITICAL(&configCacheMux);
}

bool configCacheDomeEnabled() {
    bool enabled;
    taskENTER_CRITICAL(&configCacheMux);
    enabled = configCache.system.enable_dome;
    taskEXIT_CRITICAL(&configCacheMux);
    return enabled;
}

void configCacheReadServo(ServoConfig* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&configCacheMux);
    *out = configCache.servo;
    taskEXIT_CRITICAL(&configCacheMux);
}

bool configCacheServoAnyEnabled() {
    bool result;
    taskENTER_CRITICAL(&configCacheMux);
    result = configCache.system.enable_arm1 || configCache.system.enable_arm2 ||
             configCache.system.enable_aux1 || configCache.system.enable_aux2 ||
             configCache.system.enable_aux3;
    taskEXIT_CRITICAL(&configCacheMux);
    return result;
}

void configCacheReadWifi(WifiConfig* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&configCacheMux);
    *out = configCache.wifi;
    taskEXIT_CRITICAL(&configCacheMux);
}

void configCacheSetActiveWifi(const WifiConfig& cfg) {
    taskENTER_CRITICAL(&configCacheMux);
    activeWifiConfig = cfg;
    taskEXIT_CRITICAL(&configCacheMux);
}

void configCacheReadActiveWifi(WifiConfig* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&configCacheMux);
    *out = activeWifiConfig;
    taskEXIT_CRITICAL(&configCacheMux);
}

// See declaration comment in config_store.h.
void configCacheSetActiveWifiRecovery(bool recovering) {
    taskENTER_CRITICAL(&configCacheMux);
    activeWifiRecovery = recovering;
    taskEXIT_CRITICAL(&configCacheMux);
}

// See declaration comment in config_store.h.
bool configCacheReadActiveWifiRecovery() {
    bool result;
    taskENTER_CRITICAL(&configCacheMux);
    result = activeWifiRecovery;
    taskEXIT_CRITICAL(&configCacheMux);
    return result;
}

void configCacheApply(const ConfigSnapshot& snap) {
    taskENTER_CRITICAL(&configCacheMux);
    configCache = snap;
    taskEXIT_CRITICAL(&configCacheMux);

    taskENTER_CRITICAL(&robotStateMux);
    robotState.rcConfigDirty = true;
    taskEXIT_CRITICAL(&robotStateMux);
}

uint8_t configCurrentLogLevel() {
    uint8_t level = 0;
    taskENTER_CRITICAL(&configCacheMux);
    level = configCache.system.logLevel;
    taskEXIT_CRITICAL(&configCacheMux);
    return level == 0 ? PA_LOG_LEVEL : level;
}

void configResolvedMdnsHostname(const SystemConfig& system, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return;
    }
    const char* host =
        (system.mdns_use_name && system.droid_name[0] != '\0') ? system.droid_name : WIFI_MDNS_HOST;
    snprintf(out, outSize, "%s", host);
    for (size_t i = 0; out[i] != '\0'; ++i) {
        if (out[i] >= 'A' && out[i] <= 'Z') {
            out[i] = (char)(out[i] - 'A' + 'a');
        }
    }
}

void configCacheResolvedMdnsHostname(char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return;
    }
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    configResolvedMdnsHostname(snap.system, out, outSize);
}

bool configAudioGetTrackByKey(const AudioConfig& config, const char* key, uint16_t* out) {
    if (out == nullptr) {
        return false;
    }
    const AudioTrackKeyMapEntry* entry = audioTrackKeyEntry(key);
    if (entry == nullptr) {
        return false;
    }
    *out = config.*(entry->field);
    return true;
}

bool configAudioSetTrackByKey(AudioConfig* config, const char* key, uint16_t value) {
    if (config == nullptr) {
        return false;
    }
    const AudioTrackKeyMapEntry* entry = audioTrackKeyEntry(key);
    if (entry == nullptr) {
        return false;
    }
    config->*(entry->field) = value;
    return true;
}

const char* configAudioCategoryCompanionKey(const char* key) {
    if (key == nullptr) {
        return nullptr;
    }

    constexpr const char* PAIRS[][2] = {
        {"snd_cat_gen_lo", "snd_cat_gen_hi"},
        {"snd_cat_chat_lo", "snd_cat_chat_hi"},
        {"snd_cat_hap_lo", "snd_cat_hap_hi"},
        {"snd_cat_proc_lo", "snd_cat_proc_hi"},
        {"snd_cat_sad_lo", "snd_cat_sad_hi"},
        {"snd_cat_sent_lo", "snd_cat_sent_hi"},
        {"snd_cat_hum_lo", "snd_cat_hum_hi"},
        {"snd_cat_scrm_lo", "snd_cat_scrm_hi"},
        {"snd_cat_ooh_lo", "snd_cat_ooh_hi"},
        {"snd_cat_alrm_lo", "snd_cat_alrm_hi"},
        {"snd_cat_snrk_lo", "snd_cat_snrk_hi"},
        {"snd_cat_whis_lo", "snd_cat_whis_hi"},
    };
    for (size_t i = 0; i < sizeof(PAIRS) / sizeof(PAIRS[0]); ++i) {
        if (strcmp(key, PAIRS[i][0]) == 0) {
            return PAIRS[i][1];
        }
        if (strcmp(key, PAIRS[i][1]) == 0) {
            return PAIRS[i][0];
        }
    }
    return nullptr;
}

bool configUpdateAudioMoodMasks(Preferences& prefs, uint16_t quiet, uint16_t mid, uint16_t full,
                                uint16_t awakeplus) {
    if (configValidate(ConfigKey::SND_MOODCAT_QUIET, quiet) != ConfigValidationResult::OK ||
        configValidate(ConfigKey::SND_MOODCAT_MID, mid) != ConfigValidationResult::OK ||
        configValidate(ConfigKey::SND_MOODCAT_FULL, full) != ConfigValidationResult::OK ||
        configValidate(ConfigKey::SND_MOODCAT_AWAKEPLUS, awakeplus) !=
            ConfigValidationResult::OK) {
        return false;
    }

    ConfigSnapshot snap = {};
    configCacheRead(&snap);

    snap.audio.snd_moodcat_quiet = quiet;
    snap.audio.snd_moodcat_mid = mid;
    snap.audio.snd_moodcat_full = full;
    snap.audio.snd_moodcat_awakeplus = awakeplus;

    if (!configSaveAudio(prefs, snap.audio)) {
        return false;
    }

    configCacheApply(snap);
    return true;
}

bool configLoad(Preferences& prefs, ConfigSnapshot* out) {
    if (out == nullptr) {
        return false;
    }

    PrefsReader reader(prefs);
    uint8_t stored = reader.schemaVersion();

    if (stored > CONFIG_SCHEMA_VERSION) {
        // Future/unknown schema: safe fallback to defaults, stamp current version.
        configSnapshotDefaults(out);
        prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION);
        PA_LOG_WARN("config", "unsupported schema version %u (current=%u), resetting to defaults",
                    (unsigned)stored, (unsigned)CONFIG_SCHEMA_VERSION);
        return false;
    }

    bool ok = configDeserialize(reader, out);

    if (stored < CONFIG_SCHEMA_VERSION) {
        // Migration succeeded: stamp current version so next boot is clean.
        prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION);
        PA_LOG_INFO("config", "schema migrated %u -> %u",
                    (unsigned)stored, (unsigned)CONFIG_SCHEMA_VERSION);
    }

    return ok;
}


void configLoadDrive(Preferences& prefs, DriveConfig* out) {
    if (out == nullptr) return;
    PrefsReader reader(prefs);
    configDeserializeDrive(reader, out);
}

void configLoadAudio(Preferences& prefs, AudioConfig* out) {
    if (out == nullptr) return;
    PrefsReader reader(prefs);
    configDeserializeAudio(reader, out);
}

void configLoadServo(Preferences& prefs, ServoConfig* out) {
    if (out == nullptr) return;
    PrefsReader reader(prefs);
    configDeserializeServo(reader, out);
}

void configLoadDome(Preferences& prefs, DomeConfig* out) {
    if (out == nullptr) return;
    PrefsReader reader(prefs);
    configDeserializeDome(reader, out);
}

void configLoadSystem(Preferences& prefs, SystemConfig* out) {
    if (out == nullptr) return;
    PrefsReader reader(prefs);
    configDeserializeSystem(reader, out);
}

void configLoadWifi(Preferences& prefs, WifiConfig* out) {
    if (out == nullptr) return;
    PrefsReader reader(prefs);
    configDeserializeWifi(reader, out);
}

bool configSave(Preferences& prefs, const ConfigSnapshot& snapshot) {
    PrefsWriter writer(prefs);
    return configSerialize(snapshot, writer);
}

bool configSaveDrive(Preferences& prefs, const DriveConfig& config) {
    PrefsWriter writer(prefs);
    return configSerializeDrive(config, writer);
}

bool configSaveAudio(Preferences& prefs, const AudioConfig& config) {
    PrefsWriter writer(prefs);
    return configSerializeAudio(config, writer);
}

bool configSaveServo(Preferences& prefs, const ServoConfig& config) {
    PrefsWriter writer(prefs);
    return configSerializeServo(config, writer);
}

bool configSaveDome(Preferences& prefs, const DomeConfig& config) {
    PrefsWriter writer(prefs);
    return configSerializeDome(config, writer);
}

bool configSaveWifi(Preferences& prefs, const WifiConfig& config) {
    PrefsWriter writer(prefs);
    return configSerializeWifi(config, writer);
}

bool configSaveSystem(Preferences& prefs, const SystemConfig& config) {
    PrefsWriter writer(prefs);
    bool ok = configSerializeSystem(config, writer);

    // Mark RC config dirty for RcInputTask rebuild
    if (ok) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.rcConfigDirty = true;
        taskEXIT_CRITICAL(&robotStateMux);
    }

    return ok;
}

ConfigValidationResult configValidate(ConfigKey key, int32_t value) {
    switch (key) {
        // Speed
        case ConfigKey::SPEED_LIMIT_MAX:
            return (value >= 0 && value <= SPEED_LIMIT_MAX) ? ConfigValidationResult::OK
                                                             : ConfigValidationResult::OUT_OF_RANGE;
        case ConfigKey::SPEED_PRESET_SLOW:
        case ConfigKey::SPEED_PRESET_NORMAL:
        case ConfigKey::SPEED_PRESET_TURBO:
            return (value >= 0 && value <= SPEED_LIMIT_MAX) ? ConfigValidationResult::OK
                                                             : ConfigValidationResult::OUT_OF_RANGE;
        case ConfigKey::SPEED_PRESET_ACTIVE:
            return (value >= 0 && value <= 2) ? ConfigValidationResult::OK : ConfigValidationResult::INVALID_VALUE;

        // Timeouts
        case ConfigKey::SBUS_TIMEOUT_MS:
            return (value >= 50 && value <= 5000) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;
        case ConfigKey::WEB_DRIVE_TIMEOUT_MS:
            return (value >= 100 && value <= 5000) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        // Audio
        case ConfigKey::AUDIO_VOLUME:
            return (value >= 0 && value <= 30) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;
        case ConfigKey::LOG_LEVEL:
            return (value >= 1 && value <= 3) ? ConfigValidationResult::OK : ConfigValidationResult::INVALID_VALUE;

        // Audio tracks (uint16, 0..65535 — accept all)
        case ConfigKey::SND_SCREAM:
        case ConfigKey::SND_FAINT:
        case ConfigKey::SND_LEIA:
        case ConfigKey::SND_CANTINA_S:
        case ConfigKey::SND_SW_THEME:
        case ConfigKey::SND_IMP_MARCH:
        case ConfigKey::SND_CANTINA_L:
        case ConfigKey::SND_STARTUP:
        case ConfigKey::SND_DOODOO:
        case ConfigKey::SND_FAILURE:
        case ConfigKey::SND_DISCO:
        case ConfigKey::SND_MAHNA:
        case ConfigKey::SND_INLOVE:
        case ConfigKey::SND_MACHO:
        case ConfigKey::SND_GANGNAM:
        case ConfigKey::SND_UPTOWN:
        case ConfigKey::SND_CELEBR:
        case ConfigKey::SND_STAYIN:
        case ConfigKey::SND_HARLEM:
        case ConfigKey::SND_PBJTIME:
        case ConfigKey::SND_SYS_BOOT:
        case ConfigKey::SND_SYS_MODE_N:
        case ConfigKey::SND_SYS_MODE_S:
        case ConfigKey::SND_SYS_MODE_T:
        case ConfigKey::SND_SYS_DRV_ON:
        case ConfigKey::SND_SYS_DOME_ON:
        case ConfigKey::SND_RAND_MIN:
        case ConfigKey::SND_RAND_MAX:
        case ConfigKey::SND_INT_QUIET:
        case ConfigKey::SND_INT_MID:
        case ConfigKey::SND_INT_FULL:
        case ConfigKey::SND_INT_AWAKE:
            return (value >= 0 && value <= 0xFFFF) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        // Mood categories (12-bit masks)
        case ConfigKey::SND_MOODCAT_QUIET:
        case ConfigKey::SND_MOODCAT_MID:
        case ConfigKey::SND_MOODCAT_FULL:
        case ConfigKey::SND_MOODCAT_AWAKEPLUS:
            return (value >= 0 && value <= 0x0FFF) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        // Sound category ranges (lo/hi)
        case ConfigKey::SND_CAT_GEN_LO:
        case ConfigKey::SND_CAT_GEN_HI:
        case ConfigKey::SND_CAT_CHAT_LO:
        case ConfigKey::SND_CAT_CHAT_HI:
        case ConfigKey::SND_CAT_HAP_LO:
        case ConfigKey::SND_CAT_HAP_HI:
        case ConfigKey::SND_CAT_PROC_LO:
        case ConfigKey::SND_CAT_PROC_HI:
        case ConfigKey::SND_CAT_SAD_LO:
        case ConfigKey::SND_CAT_SAD_HI:
        case ConfigKey::SND_CAT_SENT_LO:
        case ConfigKey::SND_CAT_SENT_HI:
        case ConfigKey::SND_CAT_HUM_LO:
        case ConfigKey::SND_CAT_HUM_HI:
        case ConfigKey::SND_CAT_SCRM_LO:
        case ConfigKey::SND_CAT_SCRM_HI:
        case ConfigKey::SND_CAT_OOH_LO:
        case ConfigKey::SND_CAT_OOH_HI:
        case ConfigKey::SND_CAT_ALRM_LO:
        case ConfigKey::SND_CAT_ALRM_HI:
        case ConfigKey::SND_CAT_SNARKY_LO:
        case ConfigKey::SND_CAT_SNARKY_HI:
        case ConfigKey::SND_CAT_WHIS_LO:
        case ConfigKey::SND_CAT_WHIS_HI:
            return (value >= 0 && value <= 0xFFFF) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        // Servo pulse widths
        case ConfigKey::ARM1_OPEN_US:
        case ConfigKey::ARM1_CLOSE_US:
        case ConfigKey::ARM2_OPEN_US:
        case ConfigKey::ARM2_CLOSE_US:
        case ConfigKey::AUX1_OPEN_US:
        case ConfigKey::AUX1_CLOSE_US:
        case ConfigKey::AUX2_OPEN_US:
        case ConfigKey::AUX2_CLOSE_US:
        case ConfigKey::AUX3_OPEN_US:
        case ConfigKey::AUX3_CLOSE_US:
            return (value >= 500 && value <= 2500) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        // Servo types (0..3)
        case ConfigKey::ARM1_TYPE:
        case ConfigKey::ARM2_TYPE:
        case ConfigKey::AUX1_TYPE:
        case ConfigKey::AUX2_TYPE:
        case ConfigKey::AUX3_TYPE:
            return (value >= 0 && value <= SERVO_COMP_RGB) ? ConfigValidationResult::OK
                                                            : ConfigValidationResult::INVALID_VALUE;

        // Dome ESC pulse widths
        case ConfigKey::DOME_NEUTRAL_US:
        case ConfigKey::DOME_MIN_PULSE_US:
        case ConfigKey::DOME_MAX_PULSE_US:
            return (value >= 1000 && value <= 2000) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        case ConfigKey::DOME_SPEED_LIMIT_PCT:
            return (value >= 0 && value <= 100) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        case ConfigKey::DOME_RND_SPEED_PCT:
            return (value >= 0 && value <= 100) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        case ConfigKey::DOME_RND_PAUSE_MIN:
        case ConfigKey::DOME_RND_PAUSE_MAX:
            return (value >= 0 && value <= 255) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        case ConfigKey::DOME_RND_MOVE_MS:
            return (value >= 100 && value <= 10000) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        // Sequence timing
        case ConfigKey::SEQ_OPEN_MS:
        case ConfigKey::SEQ_CLOSE_MS:
            return (value >= 100 && value <= 5000) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;

        // AUX LED
        case ConfigKey::AUX_LED_PIN:
            return (value >= 0 && value <= AUX_LED_PIN_MAX) ? ConfigValidationResult::OK
                                                             : ConfigValidationResult::INVALID_VALUE;
        case ConfigKey::AUX_LED_COUNT:
            return (value >= AUX_LED_COUNT_DEFAULT && value <= AUX_LED_COUNT_MAX) ? ConfigValidationResult::OK
                                                                                     : ConfigValidationResult::OUT_OF_RANGE;

        // RC Input Mode
        case ConfigKey::RC_INPUT_MODE:
            return (value >= 0 && value <= RC_INPUT_DUAL_SBUS) ? ConfigValidationResult::OK
                                                                : ConfigValidationResult::INVALID_VALUE;

        // Booleans are handled separately in configValidateBool
        case ConfigKey::ENABLE_ARM1:
        case ConfigKey::ENABLE_ARM2:
        case ConfigKey::ENABLE_AUX1:
        case ConfigKey::ENABLE_AUX2:
        case ConfigKey::ENABLE_AUX3:
        case ConfigKey::ENABLE_DOME:
        case ConfigKey::ENABLE_RC_CH1:
        case ConfigKey::ENABLE_RC_CH2:
        case ConfigKey::ENABLE_RC_CH3:
        case ConfigKey::ENABLE_RC_CH4:
        case ConfigKey::ENABLE_RC_CH5:
        case ConfigKey::ENABLE_RC_CH6:
        case ConfigKey::SINGLE_SBUS_USE_CH2:
        case ConfigKey::ENABLE_S1_HOVERBOARD:
        case ConfigKey::ENABLE_S2_SOUND:
        case ConfigKey::ENABLE_S3_DOME_CTRL:
        case ConfigKey::STATIONARY:
        case ConfigKey::DOME_RND_ENABLE:
            return (value == 0 || value == 1) ? ConfigValidationResult::OK : ConfigValidationResult::INVALID_VALUE;

        // Float fields handled separately
        case ConfigKey::DOME_MIN_SPEED:
        case ConfigKey::DOME_MAX_SPEED:
        case ConfigKey::DOME_WIFI_PEER_IP:
            return ConfigValidationResult::INVALID_VALUE;

        default:
            return ConfigValidationResult::INVALID_VALUE;
    }
}

ConfigValidationResult configValidateFloat(ConfigKey key, float value) {
    switch (key) {
        case ConfigKey::DOME_MIN_SPEED:
            return (value >= 0.0f && value <= 1.0f) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;
        case ConfigKey::DOME_MAX_SPEED:
            return (value >= 0.0f && value <= 1.0f) ? ConfigValidationResult::OK : ConfigValidationResult::OUT_OF_RANGE;
        default:
            return ConfigValidationResult::INVALID_VALUE;
    }
}

ConfigValidationResult configValidateBool(ConfigKey key, bool value) {
    (void)value;  // All booleans are valid (true or false)
    switch (key) {
        case ConfigKey::ENABLE_ARM1:
        case ConfigKey::ENABLE_ARM2:
        case ConfigKey::ENABLE_AUX1:
        case ConfigKey::ENABLE_AUX2:
        case ConfigKey::ENABLE_AUX3:
        case ConfigKey::ENABLE_DOME:
        case ConfigKey::ENABLE_RC_CH1:
        case ConfigKey::ENABLE_RC_CH2:
        case ConfigKey::ENABLE_RC_CH3:
        case ConfigKey::ENABLE_RC_CH4:
        case ConfigKey::ENABLE_RC_CH5:
        case ConfigKey::ENABLE_RC_CH6:
        case ConfigKey::SINGLE_SBUS_USE_CH2:
        case ConfigKey::ENABLE_S1_HOVERBOARD:
        case ConfigKey::ENABLE_S2_SOUND:
        case ConfigKey::ENABLE_S3_DOME_CTRL:
        case ConfigKey::STATIONARY:
        case ConfigKey::DOME_RND_ENABLE:
            return ConfigValidationResult::OK;
        default:
            return ConfigValidationResult::INVALID_VALUE;
    }
}
