// =============================================================================
// src/config_store.cpp
//
// Config schema module implementation  --  centralized NVS load/save and validation.
// =============================================================================

#include "config_store.h"
#include "config_cache.h"

#include "audio_dollar_parser.h"
#include "config.h"
#include "config_serializer.h"
#include "config_nvsio.h"
#include "console_config_fields.h"  // kComponentToggleFields[] - Active Component Toggle snapshot
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
    {"sys_net_down", &AudioConfig::snd_sys_net_down},
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

// Schema 2 -> 3 migration: component toggle identity rename (ADR 0033)
// Migrates old NVS keys to new keys, then deletes the old keys.
// Logs once per migration.
void migrateSchema2To3(Preferences& prefs) {
    struct KeyMap {
        const char* oldKey;
        const char* newKey;
    };

    static const KeyMap componentToggleMigrations[] = {
        {"en_s1", "en_drive"},
        {"en_dome", "en_dome_esc"},
        {"en_s3", "en_r2link"},
        {"en_s2", "en_audio"},
    };

    static const KeyMap rcAudioMigrations[] = {
        {"rcp_snd", "rcp_aud"},
        {"rcs_snd", "rcs_aud"},
        {"rc_sound", "rc_aud"},
    };

    // Migrate boolean component toggles
    for (size_t i = 0; i < sizeof(componentToggleMigrations) / sizeof(componentToggleMigrations[0]); ++i) {
        const char* oldKey = componentToggleMigrations[i].oldKey;
        const char* newKey = componentToggleMigrations[i].newKey;
        if (prefs.isKey(oldKey)) {
            bool value = prefs.getBool(oldKey, false);
            prefs.putBool(newKey, value);
            prefs.remove(oldKey);
        }
    }

    // Migrate RC audio bindings (string format)
    for (size_t i = 0; i < sizeof(rcAudioMigrations) / sizeof(rcAudioMigrations[0]); ++i) {
        const char* oldKey = rcAudioMigrations[i].oldKey;
        const char* newKey = rcAudioMigrations[i].newKey;
        if (prefs.isKey(oldKey)) {
            String value = prefs.getString(oldKey, "");
            if (value.length() > 0) {
                prefs.putString(newKey, value.c_str());
            }
            prefs.remove(oldKey);
        }
    }
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
    snap->audio.snd_sys_net_down = 0;
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
    snap->system.enable_dome_esc = false;
    snap->system.enable_rc_ch1 = false;
    snap->system.enable_rc_ch2 = false;
    snap->system.enable_rc_ch3 = false;
    snap->system.enable_rc_ch4 = false;
    snap->system.enable_rc_ch5 = false;
    snap->system.enable_rc_ch6 = false;
    snap->system.single_sbus_use_ch2 = false;
    snap->system.enable_drive = false;
    snap->system.enable_audio = false;
    snap->system.enable_protor2link = false;
    snap->system.stationary = false;
    snap->system.rc_input_mode = RC_INPUT_DUAL_SBUS;

    snap->system.rc_pwm_drive_speed = defaultPwmBinding(1);
    snap->system.rc_pwm_drive_steer = defaultPwmBinding(2);
    snap->system.rc_pwm_dome_speed = defaultPwmBinding(3);
    snap->system.rc_pwm_arm1 = defaultPwmBinding(4);
    snap->system.rc_pwm_arm2 = defaultPwmBinding(5);
    snap->system.rc_pwm_audio = defaultPwmBinding(6);

    snap->system.rc_sbus_drive_speed = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    snap->system.rc_sbus_drive_steer = defaultSbusBinding(RC_BINDING_SBUS1, 2);
    snap->system.rc_sbus_dome_speed = defaultSbusBinding(RC_BINDING_SBUS2, 1);
    snap->system.rc_sbus_arm1 = defaultSbusBinding(RC_BINDING_SBUS2, 2);
    snap->system.rc_sbus_arm2 = defaultSbusBinding(RC_BINDING_SBUS2, 3);
    snap->system.rc_sbus_audio = disabledRcBinding();

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
    snap->system.rc_audio = disabledRcTriggerBinding();
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
RcInputActiveConfig activeRcInputConfig = {};
bool activeWifiRecovery = false;
WifiBootPosture activeWifiBootPosture = WifiBootPosture::PROVISIONING;
bool activeDomeEnabled = false;
bool activeAudioEnabled = false;
// Packed bitmask, 2 B: bit i is kComponentToggleFields[i]'s value as booted.
// See include/config_cache.h and include/console_config_fields.h.
uint16_t activeComponentToggleMask = 0;
portMUX_TYPE configCacheMux = portMUX_INITIALIZER_UNLOCKED;

// The addressed Servo Output rows, live (ADR 0041). Declared here rather than
// beside the accessors below because the two cache reads project it into the
// fixed servo fields on their way out -- see projectServoOutputRows().
//
// Zero-initialised like configCache above, and filled by configLoadServoOutputs()
// from main's boot path before any task starts -- the same boot-order contract
// configCacheApply() already relies on. A reader that runs before that sees a
// count of zero, which is the truthful answer at that point rather than a
// guessed row.
static ServoOutputTable servoOutputCache = {};

// -----------------------------------------------------------------------------
// projectServoOutputRows()  --  called with configCacheMux held.
//
// The migrate phase's read direction (#286). The rows are where an endpoint
// lives now, and the ten fixed fields are a view of them: a surface still
// asking for arm1OpenUs, and the serializer that writes the old form back to
// NVS, both see the number the droid will actually drive to. That is what makes
// "a calibration cannot disagree with itself depending on which path read it"
// true while two shapes coexist, rather than true only as long as every writer
// remembers to touch both.
//
// A row is the only source: with no rows loaded yet the fields stand as they
// are, which is the boot window before configLoadServoOutputs() has run.
// Deleted with the fields it fills.
// -----------------------------------------------------------------------------
static void projectServoOutputRows(ServoConfig* servo) {
    const uint8_t count = (servoOutputCache.count <= SERVO_OUTPUT_ROW_MAX)
                              ? servoOutputCache.count
                              : SERVO_OUTPUT_ROW_MAX;
    for (uint8_t i = 0; i < count; ++i) {
        configProjectServoRowIntoFixedFields(servoOutputCache.rows[i], servo);
    }
}

void configCacheRead(ConfigSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&configCacheMux);
    *out = configCache;
    projectServoOutputRows(&out->servo);
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
    enabled = configCache.system.enable_dome_esc;
    taskEXIT_CRITICAL(&configCacheMux);
    return enabled;
}

uint8_t configCacheServoOutputCount() {
    uint8_t count;
    taskENTER_CRITICAL(&configCacheMux);
    count = servoOutputCache.count;
    taskEXIT_CRITICAL(&configCacheMux);
    return count;
}

// One row at a time, deliberately: the whole table is far larger than anything
// this cache hands out by value, and a task that wants one output should not
// pay for twenty-four.
bool configCacheReadServoOutput(uint8_t index, ServoOutputRow* out) {
    if (out == nullptr || index >= SERVO_OUTPUT_ROW_MAX) {
        return false;
    }
    bool live;
    taskENTER_CRITICAL(&configCacheMux);
    live = index < servoOutputCache.count;
    if (live) {
        *out = servoOutputCache.rows[index];
    }
    taskEXIT_CRITICAL(&configCacheMux);
    return live;
}

// The write direction of the migrate-phase bridge (#286, ADR 0041).
//
// POST /api/config still carries a builder's endpoints as arm1OpenUs and its
// nine siblings, and the Apply Core that validates them is pure -- it mutates a
// ConfigSnapshot and cannot reach this table. So the Commit Step calls this
// with the snapshot it just applied, and the numbers land on the rows the whole
// firmware now reads. Without it a builder would calibrate an arm, get the old
// value back on the next read, and watch the droid drive to it.
//
// One direction only, and only from the Commit Step. Nothing on the boot path
// may call it: configLoadServoOutputs() has already crossed the bridge in the
// other direction there, with a stored row winning over the old form, and
// pushing the fields back over the top would undo exactly that. It is deleted
// with the fields it reads.
//
// Returns what the component band moved, in the same report the loader fills,
// so a value changing under a builder is said in one voice wherever it happens.
ServoOutputRepairReport configCacheApplyServoCalibration(const ServoConfig& servo) {
    ServoOutputRepairReport report = {};
    // The whole pass is inside one critical section: it is bounded by the row
    // count, does no allocation and no I/O, and a half-applied table is a table
    // a reader could catch mid-edit.
    taskENTER_CRITICAL(&configCacheMux);
    const uint8_t count = (servoOutputCache.count <= SERVO_OUTPUT_ROW_MAX)
                              ? servoOutputCache.count
                              : SERVO_OUTPUT_ROW_MAX;
    for (uint8_t i = 0; i < count; ++i) {
        const uint16_t repaired = configAdoptFixedServoFields(&servoOutputCache.rows[i], servo);
        if (repaired == 0) {
            continue;
        }
        if (report.rowsRepaired == 0) {
            report.firstRow = i;
            report.firstRowMask = repaired;
        }
        report.rowsRepaired++;
        for (uint8_t bit = 0; bit < SERVO_OUTPUT_FIELD_COUNT; ++bit) {
            if ((repaired & (uint16_t)(1u << bit)) != 0) {
                report.fieldsRepaired++;
            }
        }
    }
    taskEXIT_CRITICAL(&configCacheMux);
    return report;
}

// -----------------------------------------------------------------------------
// The two questions the servo drive path asks of a row  --  answered as values,
// never as a row.
//
// Both live here rather than as one find-me-the-row accessor because their
// caller is ServoTask, whose worst-case static chain is a measured constant
// (SERVO_TASK_MEASURED_CHAIN_BYTES, include/config.h) that ADR 0040's checker
// re-derives from the linked image on every slice. A ServoOutputRow is 70 B,
// so handing one out puts 70 B on a Core 1 real-time frame to answer a question
// whose answer is two numbers or one. A caller that only wants an endpoint pair
// should not pay for a Part list, a Motion Profile and a boot behaviour it will
// not read.
//
// Neither copies a row inside this file either: the clamp takes its row by
// reference and the pair is read field by field, both straight out of the live
// table under the lock.
// -----------------------------------------------------------------------------

// The pulse width this output will actually be driven to, bounded by what the
// component fitted to it takes (ADR 0041). *component comes back so a caller
// that wants to say what moved the number can name the part without holding the
// row it came from.
//
// With no live row addressed there, the request is returned unchanged and
// *component is SERVO_COMP_NONE: an output the table does not describe has no
// band to be held to, and clamping it into the cautious one would be inventing
// a component nobody fitted. The two cases stay apart at the caller because a
// returned value equal to the request is, by construction, nothing to report.
uint16_t configCacheClampServoOutputPulse(ServoOutputDriver driver, uint8_t channel,
                                          uint16_t requestedUs, ServoComponentType* component) {
    uint16_t clamped = requestedUs;
    taskENTER_CRITICAL(&configCacheMux);
    const uint8_t index = servoOutputTableFindByAddress(servoOutputCache, driver, channel);
    if (index < SERVO_OUTPUT_ROW_MAX) {
        clamped = servoOutputClampPulse(servoOutputCache.rows[index], requestedUs);
        if (component != nullptr) {
            *component = servoOutputCache.rows[index].component;
        }
    } else if (component != nullptr) {
        *component = SERVO_COMP_NONE;
    }
    taskEXIT_CRITICAL(&configCacheMux);
    return clamped;
}

// The Endpoint Pair of the output addressed there, directional: `open` is
// whichever number the builder recorded as open, larger or smaller than close.
// False when no live row is addressed there, and the out-params are untouched
// so a caller's own fallback stands.
bool configCacheReadServoOutputEndpoints(ServoOutputDriver driver, uint8_t channel,
                                         uint16_t* openUs, uint16_t* closeUs) {
    if (openUs == nullptr || closeUs == nullptr) {
        return false;
    }
    bool found;
    taskENTER_CRITICAL(&configCacheMux);
    const uint8_t index = servoOutputTableFindByAddress(servoOutputCache, driver, channel);
    found = index < SERVO_OUTPUT_ROW_MAX;
    if (found) {
        *openUs = servoOutputCache.rows[index].open_us;
        *closeUs = servoOutputCache.rows[index].close_us;
    }
    taskEXIT_CRITICAL(&configCacheMux);
    return found;
}

void configCacheReadServo(ServoConfig* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&configCacheMux);
    *out = configCache.servo;
    projectServoOutputRows(out);
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

// See declaration comment in config_cache.h.
void configCacheSetActiveWifiBootPosture(WifiBootPosture posture) {
    taskENTER_CRITICAL(&configCacheMux);
    activeWifiBootPosture = posture;
    taskEXIT_CRITICAL(&configCacheMux);
}

// See declaration comment in config_cache.h.
WifiBootPosture configCacheReadActiveWifiBootPosture() {
    WifiBootPosture result;
    taskENTER_CRITICAL(&configCacheMux);
    result = activeWifiBootPosture;
    taskEXIT_CRITICAL(&configCacheMux);
    return result;
}

// See declaration comment in config_cache.h.
void configCacheSetActiveDomeEnabled(bool enabled) {
    taskENTER_CRITICAL(&configCacheMux);
    activeDomeEnabled = enabled;
    taskEXIT_CRITICAL(&configCacheMux);
}

// See declaration comment in config_cache.h.
bool configCacheReadActiveDomeEnabled() {
    bool result;
    taskENTER_CRITICAL(&configCacheMux);
    result = activeDomeEnabled;
    taskEXIT_CRITICAL(&configCacheMux);
    return result;
}

// See declaration comment in config_cache.h.
void configCacheSetActiveAudioEnabled(bool enabled) {
    taskENTER_CRITICAL(&configCacheMux);
    activeAudioEnabled = enabled;
    taskEXIT_CRITICAL(&configCacheMux);
}

// See declaration comment in config_cache.h.
bool configCacheReadActiveAudioEnabled() {
    bool result;
    taskENTER_CRITICAL(&configCacheMux);
    result = activeAudioEnabled;
    taskEXIT_CRITICAL(&configCacheMux);
    return result;
}

// See declaration comment in config_cache.h.
void configCacheSetActiveComponentToggles(const SystemConfig& system) {
    uint16_t mask = 0;
    for (size_t i = 0; i < kComponentToggleFieldCount; ++i) {
        if (system.*(kComponentToggleFields[i].field)) {
            mask |= (uint16_t)(1u << i);
        }
    }
    taskENTER_CRITICAL(&configCacheMux);
    activeComponentToggleMask = mask;
    taskEXIT_CRITICAL(&configCacheMux);
}

// See declaration comment in config_cache.h.
bool configCacheReadActiveComponentToggle(size_t bitIndex) {
    bool result;
    taskENTER_CRITICAL(&configCacheMux);
    result = (activeComponentToggleMask & (uint16_t)(1u << bitIndex)) != 0;
    taskEXIT_CRITICAL(&configCacheMux);
    return result;
}

// Live log level, published on every cache apply. Read lock-free by the log
// macros: a single aligned byte is atomic on this core, and the log path runs
// on Core 1 real-time loops where a critical section per suppressed log call
// is not acceptable.
static volatile uint8_t s_liveLogLevel = 0;

void configCacheApply(const ConfigSnapshot& snap) {
    taskENTER_CRITICAL(&configCacheMux);
    configCache = snap;
    taskEXIT_CRITICAL(&configCacheMux);
    s_liveLogLevel = snap.system.logLevel;

    taskENTER_CRITICAL(&robotStateMux);
    robotState.rcConfigDirty = true;
    taskEXIT_CRITICAL(&robotStateMux);
}

// See declaration comment in config_cache.h. Deliberately NOT a call to
// configCacheApply(): the point of this setter is to touch one field and to
// leave the RC mapping dirty flag alone.
void configCacheSetStationary(bool stationary) {
    taskENTER_CRITICAL(&configCacheMux);
    configCache.system.stationary = stationary;
    taskEXIT_CRITICAL(&configCacheMux);
}

// Project SystemConfig into the RC values that take effect only at boot. main
// calls this after NVS load so later saves remain pending without changing the
// running decoder/mapping/reporting posture (ADR 0027).
RcInputActiveConfig rcInputActiveConfigFromSystem(const SystemConfig& system) {
    RcInputActiveConfig out = {};
    out.mode = system.rc_input_mode;
    out.useCh2 = system.single_sbus_use_ch2;
    out.enableRc[0] = system.enable_rc_ch1;
    out.enableRc[1] = system.enable_rc_ch2;
    out.enableRc[2] = system.enable_rc_ch3;
    out.enableRc[3] = system.enable_rc_ch4;
    out.enableRc[4] = system.enable_rc_ch5;
    out.enableRc[5] = system.enable_rc_ch6;
    out.enableDome = system.enable_dome_esc;
    out.enableArm1 = system.enable_arm1;
    out.enableArm2 = system.enable_arm2;
    out.enableSound = system.enable_audio;
    return out;
}

// Publish the immutable RC boot projection under the cache lock. main calls
// this once before starting RC and web tasks so all consumers share one truth.
void configCacheSetActiveRcInput(const RcInputActiveConfig& cfg) {
    taskENTER_CRITICAL(&configCacheMux);
    activeRcInputConfig = cfg;
    taskEXIT_CRITICAL(&configCacheMux);
}

// Copy the boot-active RC posture safely. RC input and RC-facing diagnostics
// call this instead of treating newly saved staged settings as already active.
void configCacheReadActiveRcInput(RcInputActiveConfig* out) {
    if (out == nullptr) {
        return;
    }
    taskENTER_CRITICAL(&configCacheMux);
    *out = activeRcInputConfig;
    taskEXIT_CRITICAL(&configCacheMux);
}

uint8_t configCurrentLogLevel() {
    uint8_t level = s_liveLogLevel;
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

    // Perform schema migrations BEFORE deserializing (so new keys exist for deserialization)
    if (stored < 2) {
        // Schema 1 -> 2: log_level renumbered when the WARN tier was inserted.
        // Old: 1=Error 2=Info 3=Debug. New: 1=Error 2=Warn 3=Info 4=Debug.
        // Only 2 and 3 changed meaning; 1 and values already >= 4 are unaffected.
        // (log_level migration happens in-place; no key rename needed)
    }

    if (stored < 3) {
        // Schema 2 -> 3: component toggle identity rename (ADR 0033)
        migrateSchema2To3(prefs);
    }

    // Now that migrations are done, deserialize from the migrated NVS
    PrefsReader migratedReader(prefs);
    bool ok = configDeserialize(migratedReader, out);

    // Apply in-place schema 1->2 migration if needed
    if (stored < 2) {
        if (out->system.logLevel == 2 || out->system.logLevel == 3) {
            out->system.logLevel += 1;
            prefs.putUChar("log_level", out->system.logLevel);
        }
    }

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

void configLoadServoOutputs(Preferences& prefs, ServoOutputRepairReport* report) {
    PrefsReader reader(prefs);
    // Deserialised straight into the live table rather than through a caller's
    // local. ServoOutputTable is the largest thing this schema stores and
    // loadConfigToState() runs on loopTask, whose stack is sized against a
    // measured worst-case chain (include/config.h, #250) -- so the table never
    // becomes a stack frame. Safe because this runs once, from setup(), before
    // any task that reads the table exists.
    configDeserializeServoOutputs(reader, &servoOutputCache, report);
}

bool configSaveServoOutputs(Preferences& prefs) {
    PrefsWriter writer(prefs);
    const uint8_t count = configCacheServoOutputCount();
    bool ok = configSerializeServoOutputCount(count, writer);
    for (uint8_t i = 0; i < count; ++i) {
        // A row at a time under the cache lock: no whole-table copy on this
        // caller's stack, and no critical section held across an NVS write.
        ServoOutputRow row = {};
        if (!configCacheReadServoOutput(i, &row)) {
            continue;
        }
        ok = configSerializeServoOutputRow(i, row, writer) && ok;
    }
    return ok;
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

        // Audio tracks (uint16, 0..65535  --  accept all)
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
        case ConfigKey::ENABLE_DRIVE:
        case ConfigKey::ENABLE_AUDIO:
        case ConfigKey::ENABLE_PROTOR2LINK:
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
        case ConfigKey::ENABLE_DRIVE:
        case ConfigKey::ENABLE_AUDIO:
        case ConfigKey::ENABLE_PROTOR2LINK:
        case ConfigKey::STATIONARY:
        case ConfigKey::DOME_RND_ENABLE:
            return ConfigValidationResult::OK;
        default:
            return ConfigValidationResult::INVALID_VALUE;
    }
}
