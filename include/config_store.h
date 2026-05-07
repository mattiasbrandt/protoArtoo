// =============================================================================
// include/config_store.h
//
// Config schema module — centralized NVS key definitions, load/save, and
// scalar validation for all cfg_* configuration fields.
//
// Design:
// - ConfigSnapshot is an in-flight copy of config state; used only at
//   load/save boundaries (boot and API updates).
// - All cfg_* NVS keys are defined and owned by this module.
// - Schema versioning: Version 0 (legacy) → 1 (current). Bump on rename/removal/type change.
// - configLoad/configSave are caller-opened (Preferences lifecycle managed by caller).
// - configSave() performs no mutex lock and no robotState reads; callers capture snapshot.
// - configValidate() handles scalar fields only (int/float/bool/enum). Complex structs
//   (RcBindingConfig, RcTriggerBinding) are validated in the API layer.
// =============================================================================
#pragma once

#include <Preferences.h>

#include "robot_state.h"

// NVS schema version
constexpr uint8_t CONFIG_SCHEMA_VERSION = 1;
constexpr char CONFIG_SCHEMA_VERSION_KEY[] = "proto.schema_ver";

// Validation result
enum class ConfigValidationResult : uint8_t {
    OK = 0,
    OUT_OF_RANGE = 1,
    INVALID_VALUE = 2,
};

// ConfigKey enum — enumerates all scalar cfg_* fields for validation and lookup
enum class ConfigKey : uint8_t {
    // Speed control
    SPEED_LIMIT_MAX = 0,
    SPEED_PRESET_SLOW = 1,
    SPEED_PRESET_NORMAL = 2,
    SPEED_PRESET_TURBO = 3,
    SPEED_PRESET_ACTIVE = 4,

    // Timeouts
    SBUS_TIMEOUT_MS = 5,
    WEB_DRIVE_TIMEOUT_MS = 6,

    // Audio
    AUDIO_VOLUME = 7,
    LOG_LEVEL = 8,
    SND_SCREAM = 9,
    SND_FAINT = 10,
    SND_LEIA = 11,
    SND_CANTINA_S = 12,
    SND_SW_THEME = 13,
    SND_IMP_MARCH = 14,
    SND_CANTINA_L = 15,
    SND_STARTUP = 16,
    SND_DOODOO = 17,
    SND_FAILURE = 18,
    SND_DISCO = 19,
    SND_MAHNA = 20,
    SND_INLOVE = 21,
    SND_MACHO = 22,
    SND_GANGNAM = 23,
    SND_UPTOWN = 24,
    SND_CELEBR = 25,
    SND_STAYIN = 26,
    SND_HARLEM = 27,
    SND_PBJTIME = 28,
    SND_SYS_BOOT = 29,
    SND_SYS_MODE_N = 30,
    SND_SYS_MODE_S = 31,
    SND_SYS_MODE_T = 32,
    SND_SYS_DRV_ON = 33,
    SND_SYS_DOME_ON = 34,
    SND_RAND_MIN = 35,
    SND_RAND_MAX = 36,
    SND_INT_QUIET = 37,
    SND_INT_MID = 38,
    SND_INT_FULL = 39,
    SND_INT_AWAKE = 40,
    SND_MOODCAT_QUIET = 41,
    SND_MOODCAT_MID = 42,
    SND_MOODCAT_FULL = 43,
    SND_MOODCAT_AWAKEPLUS = 44,
    SND_CAT_GEN_LO = 45,
    SND_CAT_GEN_HI = 46,
    SND_CAT_CHAT_LO = 47,
    SND_CAT_CHAT_HI = 48,
    SND_CAT_HAP_LO = 49,
    SND_CAT_HAP_HI = 50,
    SND_CAT_PROC_LO = 51,
    SND_CAT_PROC_HI = 52,
    SND_CAT_SAD_LO = 53,
    SND_CAT_SAD_HI = 54,
    SND_CAT_SENT_LO = 55,
    SND_CAT_SENT_HI = 56,
    SND_CAT_HUM_LO = 57,
    SND_CAT_HUM_HI = 58,
    SND_CAT_SCRM_LO = 59,
    SND_CAT_SCRM_HI = 60,
    SND_CAT_OOH_LO = 61,
    SND_CAT_OOH_HI = 62,
    SND_CAT_ALRM_LO = 63,
    SND_CAT_ALRM_HI = 64,
    SND_CAT_SNARKY_LO = 65,
    SND_CAT_SNARKY_HI = 66,
    SND_CAT_WHIS_LO = 67,
    SND_CAT_WHIS_HI = 68,

    // Servo calibration
    ARM1_OPEN_US = 69,
    ARM1_CLOSE_US = 70,
    ARM2_OPEN_US = 71,
    ARM2_CLOSE_US = 72,
    ARM1_TYPE = 73,
    ARM2_TYPE = 74,
    AUX1_OPEN_US = 75,
    AUX1_CLOSE_US = 76,
    AUX2_OPEN_US = 77,
    AUX2_CLOSE_US = 78,
    AUX3_OPEN_US = 79,
    AUX3_CLOSE_US = 80,
    AUX1_TYPE = 81,
    AUX2_TYPE = 82,
    AUX3_TYPE = 83,

    // Dome
    DOME_MIN_SPEED = 84,
    DOME_MAX_SPEED = 85,
    DOME_NEUTRAL_US = 86,
    DOME_MIN_PULSE_US = 87,
    DOME_MAX_PULSE_US = 88,
    DOME_SPEED_LIMIT_PCT = 89,
    DOME_RND_ENABLE = 90,
    DOME_RND_SPEED_PCT = 91,
    DOME_RND_PAUSE_MIN = 92,
    DOME_RND_PAUSE_MAX = 93,
    DOME_RND_MOVE_MS = 94,
    DOME_WIFI_PEER_IP = 95,

    // Sequence timing
    SEQ_OPEN_MS = 96,
    SEQ_CLOSE_MS = 97,

    // AUX LED
    AUX_LED_PIN = 98,
    AUX_LED_COUNT = 99,

    // Feature toggles
    ENABLE_ARM1 = 100,
    ENABLE_ARM2 = 101,
    ENABLE_AUX1 = 102,
    ENABLE_AUX2 = 103,
    ENABLE_AUX3 = 104,
    ENABLE_DOME = 105,
    ENABLE_RC_CH1 = 106,
    ENABLE_RC_CH2 = 107,
    ENABLE_RC_CH3 = 108,
    ENABLE_RC_CH4 = 109,
    ENABLE_RC_CH5 = 110,
    ENABLE_RC_CH6 = 111,
    SINGLE_SBUS_USE_CH2 = 112,
    ENABLE_S1_HOVERBOARD = 113,
    ENABLE_S2_SOUND = 114,
    ENABLE_S3_DOME_CTRL = 115,
    STATIONARY = 116,
    RC_INPUT_MODE = 117,

    // Total count for array bounds
    _COUNT = 118,
};

struct DriveConfig {
    int16_t speedLimitMax;
    int16_t speedPresetSlow;
    int16_t speedPresetNormal;
    int16_t speedPresetTurbo;
    SpeedPresetId speedPresetActive;
    uint32_t sbusTimeoutMs;
    uint32_t webDriveTimeoutMs;
};

struct AudioConfig {
    uint8_t audioVolume;
    uint16_t snd_scream;
    uint16_t snd_faint;
    uint16_t snd_leia;
    uint16_t snd_cantina_s;
    uint16_t snd_sw_theme;
    uint16_t snd_imp_march;
    uint16_t snd_cantina_l;
    uint16_t snd_startup;
    uint16_t snd_doodoo;
    uint16_t snd_failure;
    uint16_t snd_disco;
    uint16_t snd_mahna;
    uint16_t snd_inlove;
    uint16_t snd_macho;
    uint16_t snd_gangnam;
    uint16_t snd_uptown;
    uint16_t snd_celebr;
    uint16_t snd_stayin;
    uint16_t snd_harlem;
    uint16_t snd_pbjtime;
    uint16_t snd_sys_boot;
    uint16_t snd_sys_mode_n;
    uint16_t snd_sys_mode_s;
    uint16_t snd_sys_mode_t;
    uint16_t snd_sys_drv_on;
    uint16_t snd_sys_dome_on;
    uint16_t snd_rand_min;
    uint16_t snd_rand_max;
    uint16_t snd_int_quiet;
    uint16_t snd_int_mid;
    uint16_t snd_int_full;
    uint16_t snd_int_awake;
    uint16_t snd_moodcat_quiet;
    uint16_t snd_moodcat_mid;
    uint16_t snd_moodcat_full;
    uint16_t snd_moodcat_awakeplus;
    uint16_t snd_cat_gen_lo;
    uint16_t snd_cat_gen_hi;
    uint16_t snd_cat_chat_lo;
    uint16_t snd_cat_chat_hi;
    uint16_t snd_cat_hap_lo;
    uint16_t snd_cat_hap_hi;
    uint16_t snd_cat_proc_lo;
    uint16_t snd_cat_proc_hi;
    uint16_t snd_cat_sad_lo;
    uint16_t snd_cat_sad_hi;
    uint16_t snd_cat_sent_lo;
    uint16_t snd_cat_sent_hi;
    uint16_t snd_cat_hum_lo;
    uint16_t snd_cat_hum_hi;
    uint16_t snd_cat_scrm_lo;
    uint16_t snd_cat_scrm_hi;
    uint16_t snd_cat_ooh_lo;
    uint16_t snd_cat_ooh_hi;
    uint16_t snd_cat_alrm_lo;
    uint16_t snd_cat_alrm_hi;
    uint16_t snd_cat_snarky_lo;
    uint16_t snd_cat_snarky_hi;
    uint16_t snd_cat_whis_lo;
    uint16_t snd_cat_whis_hi;
};

struct ServoConfig {
    uint16_t arm1_open_us;
    uint16_t arm1_close_us;
    uint16_t arm2_open_us;
    uint16_t arm2_close_us;
    ServoComponentType arm1_type;
    ServoComponentType arm2_type;
    uint16_t aux1_open_us;
    uint16_t aux1_close_us;
    uint16_t aux2_open_us;
    uint16_t aux2_close_us;
    uint16_t aux3_open_us;
    uint16_t aux3_close_us;
    ServoComponentType aux1_type;
    ServoComponentType aux2_type;
    ServoComponentType aux3_type;
    uint16_t seq_open_ms;
    uint16_t seq_close_ms;
    uint8_t aux_led_pin;
    uint8_t aux_led_count;
};

struct DomeConfig {
    float dome_min_speed;
    float dome_max_speed;
    uint16_t dome_neutral_us;
    uint16_t dome_min_pulse_us;
    uint16_t dome_max_pulse_us;
    uint8_t dome_speed_limit_pct;
    bool dome_rnd_enable;
    uint8_t dome_rnd_speed_pct;
    uint8_t dome_rnd_pause_min;
    uint8_t dome_rnd_pause_max;
    uint16_t dome_rnd_move_ms;
    char dome_wifi_peer_ip[16];
};

struct SystemConfig {
    uint8_t logLevel;
    bool enable_arm1;
    bool enable_arm2;
    bool enable_aux1;
    bool enable_aux2;
    bool enable_aux3;
    bool enable_dome;
    bool enable_rc_ch1;
    bool enable_rc_ch2;
    bool enable_rc_ch3;
    bool enable_rc_ch4;
    bool enable_rc_ch5;
    bool enable_rc_ch6;
    bool single_sbus_use_ch2;
    bool enable_s1_hoverboard;
    bool enable_s2_sound;
    bool enable_s3_dome_ctrl;
    bool stationary;
    RcInputMode rc_input_mode;
    RcBindingConfig rc_pwm_drive_speed;
    RcBindingConfig rc_pwm_drive_steer;
    RcBindingConfig rc_pwm_dome_speed;
    RcBindingConfig rc_pwm_arm1;
    RcBindingConfig rc_pwm_arm2;
    RcBindingConfig rc_pwm_sound;

    RcBindingConfig rc_sbus_drive_speed;
    RcBindingConfig rc_sbus_drive_steer;
    RcBindingConfig rc_sbus_dome_speed;
    RcBindingConfig rc_sbus_arm1;
    RcBindingConfig rc_sbus_arm2;
    RcBindingConfig rc_sbus_sound;
    RcTriggerBinding rc_arm1;
    RcTriggerBinding rc_arm2;
    RcTriggerBinding rc_aux1;
    RcTriggerBinding rc_aux2;
    RcTriggerBinding rc_aux3;
    RcTriggerBinding rc_sound;
    RcTriggerBinding rc_opmode;
    RcTriggerBinding rc_free0;
    RcTriggerBinding rc_free1;
    RcTriggerBinding rc_free2;
    RcTriggerBinding rc_free3;
};

// ConfigSnapshot — in-flight snapshot of all cfg_* fields, used only at
// load/save boundaries. NOT persisted or shared with tasks at runtime.
struct ConfigSnapshot {
    DriveConfig drive;
    AudioConfig audio;
    ServoConfig servo;
    DomeConfig dome;
    SystemConfig system;
};

// =============================================================================
// Public API
// =============================================================================

// configLoad: Load NVS config into a ConfigSnapshot.
// Caller opens Preferences with begin() before calling.
// On schema version mismatch, fills snapshot with defaults and logs warning.
// Returns false + logs warning on schema mismatch; true on success.
bool configLoad(Preferences& prefs, ConfigSnapshot* out);

void configLoadDrive(Preferences& prefs, DriveConfig* out);
void configLoadAudio(Preferences& prefs, AudioConfig* out);
void configLoadServo(Preferences& prefs, ServoConfig* out);
void configLoadDome(Preferences& prefs, DomeConfig* out);
void configLoadSystem(Preferences& prefs, SystemConfig* out);

bool configAudioGetTrackByKey(const AudioConfig& config, const char* key, uint16_t* out);
bool configAudioSetTrackByKey(AudioConfig* config, const char* key, uint16_t value);
const char* configAudioCategoryCompanionKey(const char* key);
bool configUpdateAudioMoodMasks(Preferences& prefs, uint16_t quiet, uint16_t mid, uint16_t full,
                                uint16_t awakeplus);

// configSave: Persist full ConfigSnapshot to NVS.
// Caller opens Preferences with begin() before calling.
// Holds no mutex and performs no robotState reads/writes.
// Returns false if any write fails; true on success.
bool configSave(Preferences& prefs, const ConfigSnapshot& snapshot);

bool configSaveDrive(Preferences& prefs, const DriveConfig& config);
bool configSaveAudio(Preferences& prefs, const AudioConfig& config);
bool configSaveServo(Preferences& prefs, const ServoConfig& config);
bool configSaveDome(Preferences& prefs, const DomeConfig& config);
bool configSaveSystem(Preferences& prefs, const SystemConfig& config);

// configCacheRead: Fill a ConfigSnapshot from the live config cache.
// This uses configCacheMux, not robotStateMux. Runtime tasks should copy the
// domain they need into stack locals, then release the cache lock before doing work.
void configCacheRead(ConfigSnapshot* out);
void configCacheReadDome(DomeConfig* out);
bool configCacheDomeEnabled();
void configCacheReadServo(ServoConfig* out);
bool configCacheServoAnyEnabled();

// configCacheApply: Replace the live config cache with a full snapshot.
// Marks RobotState.rcConfigDirty so RcInputTask rebuilds cached mapping config.
void configCacheApply(const ConfigSnapshot& snap);

// configCurrentLogLevel: lightweight runtime log-level accessor used by logging.h.
uint8_t configCurrentLogLevel();

// configValidate: Validate a scalar field value before writing.
// Covers int, float, bool, and enum fields only.
// Complex struct fields (RcBindingConfig, RcTriggerBinding) are validated in API layer.
// Returns ConfigValidationResult enum.
ConfigValidationResult configValidate(ConfigKey key, int32_t value);
ConfigValidationResult configValidateFloat(ConfigKey key, float value);
ConfigValidationResult configValidateBool(ConfigKey key, bool value);
