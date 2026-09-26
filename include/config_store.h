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
//   The saves a Commit Step makes are not: configPersist(), saveConfigToNvs() and
//   configPersistSystem() open the namespace themselves and write in the store's
//   one order (#424).
// - configSave() performs no mutex lock and no robotState reads; callers capture snapshot.
// - What a Setting accepts, its NVS key and its default are its declaration's
//   (include/config_settings.h), not this module's.
// =============================================================================
#pragma once

#include <Preferences.h>

#include "config.h"
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

// ServoConfig is gone (#413). It ended as two fields - which single Output
// carried the LED strip, and how many LEDs were on it - and both were a second
// store of something an addressed Servo Output row already answers: a wire
// carries a Light Type when its `component` names one, and the LEDs on it are
// that row's `led_count` (ADR 0067). One droid, one strip was the limit that
// model imposed, and there is now no servo-adjacent setting that is not
// per-output, so there is no struct left to hold one. The endpoints and
// component types left earlier (#345, ADR 0041) and the sequence dwell after
// them (#354, #362).

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
    // Which radio the droid is driven with: a Component Registry part `value`
    // in the Radio Controller family. Nothing on the controller branches on it
    // - the receiver is read according to rc_input_mode - so it is the
    // builder's statement of their product, kept on the droid so every browser
    // shows the same one.
    uint8_t rc_member;
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
// they carried - and 916 B until #362 took out the sequence dwell, two uint16_t
// fields. A shrink needs no re-measurement to be safe, because every chain this
// struct is on gets shorter; the number is still updated here so the next
// reader is not told a frame is bigger than it is.
//
// #369 grew it to 916 B: SystemConfig.rc_member, the Radio Controller's
// Component Member. One byte, but SystemConfig had no hole left for it (it is
// 2-byte aligned and ends flush), so the struct grows by two and
// ConfigSnapshot, 4-byte aligned, by four. The Console chain was re-walked
// with tools/check_task_stack_chains.py against the linked image before this
// number moved.
//
// The same number on both chip targets and on the host compiler: every member
// is an integral, float, enum or char array type, so this struct's alignment
// is 4 everywhere. A member needing 8-byte alignment would change that, and
// this assertion is where it would say so.
//
// #413 deleted ServoConfig - the last two bytes of servo config that were not
// an Output's own - and the number did NOT move: measured at 916 B before and
// after, because those two bytes sat in padding the members either side of
// them already carried. A deletion is not automatically a shrink, and the
// figure here is the compiler's rather than the arithmetic's.
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
void configLoadDome(Preferences& prefs, DomeConfig* out);
void configLoadSystem(Preferences& prefs, SystemConfig* out);
void configLoadWifi(Preferences& prefs, WifiConfig* out);

// configSave: Persist full ConfigSnapshot to NVS.
// Caller opens Preferences with begin() before calling.
// Holds no mutex and performs no robotState reads/writes. After boot it runs
// inside a Write Window, as every configSave*() writer and
// configUpdateAudioMoodMasks() do: each checks that its caller holds the config
// write lock, and logs when it does not (include/config_write_window_check.h).
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

// configLoadRecords: every Record (include/config_records.h) off its own NVS
// keys, straight into its live copy, on the boot path. Outside ConfigSnapshot
// for the same reason the rows above are. Caller opens Preferences with
// begin() before calling. Logs a warning for each Record a stored value this
// image cannot name cost, rather than letting it change silently.
void configLoadRecords(Preferences& prefs);

bool configSaveDrive(Preferences& prefs, const DriveConfig& config);
bool configSaveAudio(Preferences& prefs, const AudioConfig& config);
bool configSaveDome(Preferences& prefs, const DomeConfig& config);
bool configSaveSystem(Preferences& prefs, const SystemConfig& config);
bool configSaveWifi(Preferences& prefs, const WifiConfig& config);

// -----------------------------------------------------------------------------
// Store-opened saves (#424)
//
// Every full save and every system-only save goes through one of these three.
// (Device WiFi Settings and the audio saves keep their own namespace opens, so
// this module never learns the CHIRP binding keys.) Each opens the config
// namespace itself, writes in the order below, closes it, and answers saved or
// not; a namespace that will not open is a save that did not happen. What a
// caller decides is WHAT changed - which records its request said something
// about (ADR 0011, 2026-08-27 amendment) - never the order they land in or
// which handle they land through.
//
// The order is rows first, and the fixed field sets only once the rows are
// down. While both forms are stored, the fixed sets are the copy of what is
// being replaced, and a failed save has to stop the replace: a row write that
// fails leaves both stores holding the same older value, which is
// recoverable, where the other order would leave the rows stale and winning
// over a field set that already carried the new number (#286, ADR 0041).
// Nothing after a failed write is attempted.
//
// Like the caller-opened writers above, each runs inside a Write Window; the
// writers they call check the lock is held.
// -----------------------------------------------------------------------------

// The Records a full save writes after the Configuration only when the
// request said something about them. Absence is an answer for each - no
// Fitted Parts record tells the next boot nobody has answered yet, and no
// visited record tells it guided Setup was never drawn - so a save that was
// about the log level must not write one.
struct ConfigSaveExtras {
    // Bit i: save ConfigRecordId i from its live copy
    // (configRecordsStated(), include/config_records.h).
    uint32_t records = 0;
};

// The Servo Output rows and `snapshot`, in that order, then the Records named.
// The rows and the Records are written from their live copies; `snapshot` is
// expected to be the cache as it stands too, which is what every caller passes.
bool configPersist(const ConfigSnapshot& snapshot, const ConfigSaveExtras& extras);

// configPersist() of the config cache as it stands, and no extras: the save a
// runtime change to one field makes (a mode, a speed preset, the volume).
bool saveConfigToNvs();

// The system field set alone, for a change that touches nothing else (the RC
// Map, the droid's identity). The rows are not rewritten, so there is no order
// to keep.
bool configPersistSystem(const SystemConfig& system);
