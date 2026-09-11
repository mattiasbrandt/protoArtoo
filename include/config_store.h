// =============================================================================
// include/config_store.h
//
// Config schema module  --  centralized NVS key definitions, load/save, and
// scalar validation for all cfg_* configuration fields.
//
// Design:
// - ConfigSnapshot is an in-flight copy of config state; used only at
//   load/save boundaries (boot and API updates).
// - All cfg_* NVS keys are defined and owned by this module.
// - Schema versioning: Version 0 (legacy) -> 1 (current). Bump on rename/removal/type change.
// - configLoad/configSave are caller-opened (Preferences lifecycle managed by caller).
// - configSave() performs no mutex lock and no robotState reads; callers capture snapshot.
// - configValidate() handles scalar fields only (int/float/bool/enum). Complex structs
//   (RcBindingConfig, RcTriggerBinding) are validated in the API layer.
// =============================================================================
#pragma once

#include <Preferences.h>

#include "config.h"
#include "droid_build.h"       // DroidBuildConfig / DroidBuildRepairReport, for the load below
#include "robot_state.h"
#include "servo_output_row.h"  // ServoOutputRepairReport, for the load below

// NVS schema version
// 0 (legacy) -> 1: key renames. 1 -> 2: log_level renumbered for the WARN tier
// (old 2=Info/3=Debug become 3/4; see configLoad()).
// 2 -> 3: component toggle identity rename (en_s1->en_drive, en_dome->en_dome_esc,
// en_s3->en_r2link, en_s2->en_audio, rcp_snd->rcp_aud, rcs_snd->rcs_aud, rc_sound->rc_aud)
//
// Deliberately NOT bumped for #345, which removed the five fixed servo key
// sets. This number gates a migration that runs at load, and the removal needs
// none: the row form has been stored beside the old keys since #338, the row
// loader adopts a key set on a row nothing has written, and a stored row always
// wins -- so the crossing is idempotent and marker-free in both directions. A
// bump would buy nothing and cost something real: `stored > CURRENT` is the
// reset-to-defaults branch, so an image from before this slice would wipe a
// controller that had already been stamped by one after it.
constexpr uint8_t CONFIG_SCHEMA_VERSION = 3;
constexpr char CONFIG_SCHEMA_VERSION_KEY[] = "schema_ver";

// Validation result
enum class ConfigValidationResult : uint8_t {
    OK = 0,
    OUT_OF_RANGE = 1,
    INVALID_VALUE = 2,
};

// ConfigKey enum  --  enumerates all scalar cfg_* fields for validation and lookup
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

    // Servo calibration has no entry here. An endpoint and the component type
    // beside it live on an addressed Servo Output row, whose validator is
    // servoOutputRowNormalise() -- one validator at every door, and this enum
    // is not one of them (#345, ADR 0041).

    // Dome
    DOME_MIN_SPEED = 69,
    DOME_MAX_SPEED = 70,
    DOME_NEUTRAL_US = 71,
    DOME_MIN_PULSE_US = 72,
    DOME_MAX_PULSE_US = 73,
    DOME_SPEED_LIMIT_PCT = 74,
    DOME_RND_ENABLE = 75,
    DOME_RND_SPEED_PCT = 76,
    DOME_RND_PAUSE_MIN = 77,
    DOME_RND_PAUSE_MAX = 78,
    DOME_RND_MOVE_MS = 79,
    DOME_WIFI_PEER_IP = 80,

    // Sequence timing
    SEQ_OPEN_MS = 81,
    SEQ_CLOSE_MS = 82,

    // AUX LED
    AUX_LED_PIN = 83,
    AUX_LED_COUNT = 84,

    // Feature toggles
    ENABLE_ARM1 = 85,
    ENABLE_ARM2 = 86,
    ENABLE_AUX1 = 87,
    ENABLE_AUX2 = 88,
    ENABLE_AUX3 = 89,
    ENABLE_DOME = 90,
    ENABLE_RC_CH1 = 91,
    ENABLE_RC_CH2 = 92,
    ENABLE_RC_CH3 = 93,
    ENABLE_RC_CH4 = 94,
    ENABLE_RC_CH5 = 95,
    ENABLE_RC_CH6 = 96,
    SINGLE_SBUS_USE_CH2 = 97,
    // The three serial-component toggles carry generic project vocabulary, not
    // the artoo.uk PCB's S1/S2/S3 silkscreen legend (ADR 0033). Their persisted
    // keys have been en_drive / en_audio / en_r2link since the schema 2 -> 3
    // migration, so these identifiers were the last place the board's own
    // labels survived and renaming them migrates nothing.
    ENABLE_DRIVE = 98,
    ENABLE_AUDIO = 99,
    ENABLE_PROTOR2LINK = 100,
    STATIONARY = 101,
    RC_INPUT_MODE = 102,

    // The Sound family's Component Member, beside ENABLE_AUDIO rather than
    // inside it: the toggle says a sound module is fitted, the member says
    // which product it is, and the two are independent in both directions
    // (ADR 0042). Validated against the Component Registry, not against a
    // numeric range -- see configValidate().
    SOUND_MEMBER = 103,

    // Total count for array bounds
    _COUNT = 104,
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
    uint16_t snd_happy;
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
    uint16_t snd_sys_net_down;  // C6 link degraded announcement (#189). NVS
                                // key is "snd_sys_netdown" (see
                                // config_serializer.cpp), 15 chars -- the
                                // ESP-IDF Preferences key length limit -- while
                                // this struct member keeps the descriptive name.
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

// No endpoint, no component type: those are an addressed Servo Output row's,
// and a row is the only place either is stored (#345, ADR 0041). What is left
// here is servo-adjacent config that is not per-output -- a sequence dwell the
// whole droid shares, and the AUX LED selection.
struct ServoConfig {
    // The dwell a body sequence holds an arm open or closed for. 1000 ms is a
    // GUESS, not a measurement, and it is the one this firmware has always
    // stood in for travel time with.
    //
    // ADR 0041 says these should default from the computed travel time once one
    // exists. Nothing computes one yet: travel time is derived from a row's
    // Motion Profile by the ramp, and the ramp is not in Waves 0-3 at all, so
    // there is no figure to default from and no way to measure this number's
    // error. Deleting them would take the dwell away from a sequence that needs
    // one; re-defaulting them would be picking a second guess. So they stay,
    // said out loud: whoever wires the Motion Profile into ServoTask should
    // default these from it and delete this paragraph (#345, routed from #338).
    uint16_t seq_open_ms;
    uint16_t seq_close_ms;
    // NOT a GPIO, despite the name: an AUX slot selection, 0..AUX_LED_PIN_MAX
    // (AUX_LED_PIN_DISABLED/AUX1/AUX2/AUX3, include/config.h). The GPIO it
    // resolves to is the board's, via auxLedSelectionToGpio(), and it is
    // robotState.auxLed.pin -- a different field with the same word in it that
    // really does hold a GPIO. The NVS key aux_led_pin is operator-visible and
    // stays; this comment is the guard against reading the two as one thing.
    uint8_t aux_led_pin;
    uint8_t aux_led_count;
};

// -----------------------------------------------------------------------------
// Device WiFi Settings (ADR 0015  --  runtime WiFi provisioning)
// -----------------------------------------------------------------------------

// WifiMode is the ongoing operator-selected posture once provisioned.
// It is meaningless while wifi.provisioned == false (Unprovisioned Controller).
enum class WifiMode : uint8_t {
    CLIENT = 0,         // WiFi Client Mode  --  join an existing network
    STANDALONE_AP = 1,  // Standalone AP Mode  --  host the controller's own network
};

constexpr size_t WIFI_SSID_MAX_LEN = 32;      // 802.11 SSID length limit
constexpr size_t WIFI_PASSWORD_MAX_LEN = 63;  // WPA2-PSK passphrase length limit
constexpr size_t WIFI_PASSWORD_MIN_LEN = 8;   // ESP32 SoftAP / WPA2 minimum (empty = open network)

// WifiConfig is Device WiFi Settings: the operator-selected WiFi posture and
// network credentials retained by the controller after provisioning. Default
// value (provisioned == false) represents an Unprovisioned Controller.
struct WifiConfig {
    bool provisioned;
    WifiMode mode;
    char sta_ssid[WIFI_SSID_MAX_LEN + 1];
    char sta_password[WIFI_PASSWORD_MAX_LEN + 1];
    char ap_ssid[WIFI_SSID_MAX_LEN + 1];
    char ap_password[WIFI_PASSWORD_MAX_LEN + 1];
};

// WifiConfigView is the normal-read shape of Device WiFi Settings: it carries
// password-set flags instead of plaintext passwords, so API/status snapshots
// never echo saved WiFi credentials.
struct WifiConfigView {
    bool provisioned;
    WifiMode mode;
    char sta_ssid[WIFI_SSID_MAX_LEN + 1];
    bool sta_password_set;
    char ap_ssid[WIFI_SSID_MAX_LEN + 1];
    bool ap_password_set;
};

// wifiConfigToView: pure projection from persisted settings to the
// password-safe read shape. No plaintext password ever reaches the result.
WifiConfigView wifiConfigToView(const WifiConfig& cfg);

// wifiConfigsDiffer: true if any operator-relevant field (provisioned state,
// mode, SSIDs, or password content) differs between two Device WiFi Settings
// snapshots. Used to report active-vs-pending state for a Staged Network
// Switch (ADR 0015): compare the settings actually applied at last WiFi
// bootstrap against the currently persisted settings.
bool wifiConfigsDiffer(const WifiConfig& a, const WifiConfig& b);

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
    char droid_name[DROID_NAME_MAX_LEN + 1];
    bool mdns_use_name;
    uint8_t logLevel;
    bool enable_arm1;
    bool enable_arm2;
    bool enable_aux1;
    bool enable_aux2;
    bool enable_aux3;
    bool enable_dome_esc;
    bool enable_rc_ch1;
    bool enable_rc_ch2;
    bool enable_rc_ch3;
    bool enable_rc_ch4;
    bool enable_rc_ch5;
    bool enable_rc_ch6;
    bool single_sbus_use_ch2;
    bool enable_drive;
    bool enable_audio;
    bool enable_protor2link;
    bool stationary;
    RcInputMode rc_input_mode;
    // Which sound module is fitted: a Component Registry part `value`, resolved
    // through componentResolveMember() rather than trusted, so a controller
    // carried to an image that no longer carries that module falls back to one
    // it can drive instead of going silent. Staged at reboot like a Component
    // Toggle; include/component_registry.h holds the contract.
    uint8_t sound_member;
    RcBindingConfig rc_pwm_drive_speed;
    RcBindingConfig rc_pwm_drive_steer;
    RcBindingConfig rc_pwm_dome_speed;
    RcBindingConfig rc_pwm_arm1;
    RcBindingConfig rc_pwm_arm2;
    RcBindingConfig rc_pwm_audio;

    RcBindingConfig rc_sbus_drive_speed;
    RcBindingConfig rc_sbus_drive_steer;
    RcBindingConfig rc_sbus_dome_speed;
    RcBindingConfig rc_sbus_arm1;
    RcBindingConfig rc_sbus_arm2;
    RcBindingConfig rc_sbus_audio;
    RcTriggerBinding rc_arm1;
    RcTriggerBinding rc_arm2;
    RcTriggerBinding rc_aux1;
    RcTriggerBinding rc_aux2;
    RcTriggerBinding rc_aux3;
    RcTriggerBinding rc_audio;
    RcTriggerBinding rc_opmode;
    RcTriggerBinding rc_free0;
    RcTriggerBinding rc_free1;
    RcTriggerBinding rc_free2;
    RcTriggerBinding rc_free3;
};

// Canonical enumeration of the RC trigger binding slots above, in tier-2
// dispatch order. Every consumer that scans "all trigger slots" (the RC input
// task, the sequence dangling-binding scan) copies through here, so adding a
// slot field is a one-place change plus this list.
static constexpr size_t RC_TRIGGER_SLOT_COUNT = 11;

inline size_t rcTriggerSlotsCopy(const SystemConfig& sys, RcTriggerBinding* out, size_t cap) {
    const RcTriggerBinding* slots[RC_TRIGGER_SLOT_COUNT] = {
        &sys.rc_arm1,  &sys.rc_arm2,  &sys.rc_aux1,  &sys.rc_aux2,
        &sys.rc_aux3,  &sys.rc_audio, &sys.rc_opmode, &sys.rc_free0,
        &sys.rc_free1, &sys.rc_free2, &sys.rc_free3,
    };
    const size_t n = (cap < RC_TRIGGER_SLOT_COUNT) ? cap : RC_TRIGGER_SLOT_COUNT;
    for (size_t i = 0; i < n; ++i) {
        out[i] = *slots[i];
    }
    return n;
}

// ConfigSnapshot  --  in-flight snapshot of all cfg_* fields, used only at
// load/save boundaries. NOT persisted or shared with tasks at runtime.
struct ConfigSnapshot {
    DriveConfig drive;
    AudioConfig audio;
    ServoConfig servo;
    DomeConfig dome;
    SystemConfig system;
    WifiConfig wifi;
};

// 916 bytes, measured - and pinned here because two comments elsewhere had
// drifted from it and one of them was load-bearing. Every by-value crossing of
// this struct contributes a 916-byte stack frame: three nested frames on the
// serial config-write path each carried one, which is how the Console task's
// chain grew past its stack and panicked both boards (#226). The serializer
// called it 744 B and ConfigCommitOutcome called itself small.
//
// It was 944 B until #345 took the five fixed servo field sets out of
// ServoConfig - ten endpoints and five component types, 28 B with the padding
// they carried. A shrink needs no re-measurement to be safe, because every
// chain this struct is on gets shorter; the number is still updated here so the
// next reader is not told a frame is bigger than it is.
//
// The same number on both chip targets and on the host compiler: every member
// is an integral, float, enum or char array type, so this struct's alignment
// is 4 everywhere. A member needing 8-byte alignment would change that, and
// this assertion is where it would say so.
//
// A field addition that moves the number is a decision, not an accident: it
// changes what every seam that crosses this struct costs, so re-measure the
// Console task's chain before updating the value here. The recipe moved out of
// include/config.h's comment block at #271: it is now a row in
// tools/task_stack_recipes.json, and tools/check_task_stack_chains.py re-walks
// it against a linked image, so the re-measure is a re-run rather than a
// procedure to follow by hand.
static_assert(sizeof(ConfigSnapshot) == 916,
              "ConfigSnapshot changed size - re-derive the Console task stack from a fresh "
              "chain measurement before moving this number");

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
void configLoadWifi(Preferences& prefs, WifiConfig* out);

// configSave: Persist full ConfigSnapshot to NVS.
// Caller opens Preferences with begin() before calling.
// Holds no mutex and performs no robotState reads/writes.
// Returns false if any write fails; true on success.
void configSnapshotDefaults(ConfigSnapshot* snap);
bool configSave(Preferences& prefs, const ConfigSnapshot& snapshot);

// configLoadServoOutputs / configSaveServoOutputs: the addressed Servo Output
// rows (ADR 0041). They sit outside ConfigSnapshot and outside configLoad /
// configSave, on their own NVS keys -- see include/config_serializer.h for why
// the table is not a snapshot field.
//
// Caller opens Preferences with begin() before calling, exactly as for the
// snapshot pair above. configLoadServoOutputs fills the live table read by
// configCacheReadServoOutput(); *report says what a damaged stored row cost.
void configLoadServoOutputs(Preferences& prefs, ServoOutputRepairReport* report);
bool configSaveServoOutputs(Preferences& prefs);

// configLoadDroidBuild / configSaveDroidBuild: the Droid Build (ADR 0047).
// Outside ConfigSnapshot for the same reason the rows above are, and on their
// own NVS keys -- see include/config_serializer.h. Caller opens Preferences
// with begin() before calling.
//
// configLoadDroidBuild fills the live answer read by configCacheReadDroidBuild();
// *report says what a stored answer this image cannot name cost.
void configLoadDroidBuild(Preferences& prefs, DroidBuildRepairReport* report);
bool configSaveDroidBuild(Preferences& prefs);

bool configSaveDrive(Preferences& prefs, const DriveConfig& config);
bool configSaveAudio(Preferences& prefs, const AudioConfig& config);
bool configSaveServo(Preferences& prefs, const ServoConfig& config);
bool configSaveDome(Preferences& prefs, const DomeConfig& config);
bool configSaveSystem(Preferences& prefs, const SystemConfig& config);
bool configSaveWifi(Preferences& prefs, const WifiConfig& config);

// configValidate: Validate a scalar field value before writing.
// Covers int, float, bool, and enum fields only.
// Complex struct fields (RcBindingConfig, RcTriggerBinding) are validated in API layer.
// Returns ConfigValidationResult enum.
ConfigValidationResult configValidate(ConfigKey key, int32_t value);
ConfigValidationResult configValidateFloat(ConfigKey key, float value);
ConfigValidationResult configValidateBool(ConfigKey key, bool value);
