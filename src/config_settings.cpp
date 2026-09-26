// =============================================================================
// src/config_settings.cpp
//
// Each Setting, declared once (ADR 0068, amended 2026-09-26). See
// include/config_settings.h for who reads these and why they are the one home.
// =============================================================================

#include "config_settings.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <type_traits>

#include "api_helpers.h"          // parseDriveValue(), parseUint32Value(), parseBoolValue()
#include "audio_dollar_parser.h"  // AUDIO_TRACK_*, AUDIO_RAND_* - the audio defaults
#include "board_outputs.h"
#include "component_registry.h"
#include "config.h"               // SPEED_*, SBUS_TIMEOUT_MS, WEB_DRIVE_TIMEOUT_MS, PA_LOG_LEVEL*
#include "mood_sound_mapping.h"   // MOOD_CATEGORY_MASK_MAX - a mood mask's bits
#include "servo_component_helpers.h"

namespace {

// -----------------------------------------------------------------------------
// The word lists. Each names its words through the vocabulary's own function,
// so a Setting takes and reports exactly the words the rest of the firmware
// uses for the same thing.
// -----------------------------------------------------------------------------
const char* logLevelName(uint8_t level) {
    switch (level) {
        case PA_LOG_LEVEL_ERROR:
            return "error";
        case PA_LOG_LEVEL_WARN:
            return "warning";
        case PA_LOG_LEVEL_INFO:
            return "info";
        case PA_LOG_LEVEL_DEBUG:
            return "debug";
        default:
            return nullptr;
    }
}

const char* componentName(uint8_t value) {
    return servoCompTypeToString((ServoComponentType)value);
}
const char* easingName(uint8_t value) { return servoEasingToString((ServoEasing)value); }
const char* bootName(uint8_t value) { return servoBootBehaviourToString((ServoBootBehaviour)value); }

constexpr SettingWords kRcInputModeWords = {RC_INPUT_STANDARD_PWM, 4, rcInputModeName};
constexpr SettingWords kLogLevelWords = {PA_LOG_LEVEL_ERROR, 4, logLevelName};
constexpr SettingWords kComponentWords = {SERVO_COMP_NONE, 4, componentName};
constexpr SettingWords kEasingWords = {SERVO_EASE_NONE, SERVO_EASE_COUNT, easingName};
constexpr SettingWords kBootWords = {SERVO_BOOT_LIMP, SERVO_BOOT_COUNT, bootName};

// -----------------------------------------------------------------------------
// Settings of the droid
//
// One line each. The helpers below only spell the fields a rule does not use as
// zero; what a Setting IS is the line.
// -----------------------------------------------------------------------------
#define PA_SETTING_FIELD(Section, Type, member)                                    \
    SettingSection::Section, (uint16_t)offsetof(Type, member),                     \
        settingStorageOf<decltype(Type::member)>(), (uint8_t)sizeof(Type::member)

#define PA_RANGE(form, path, key, timing, Section, Type, member, lo, hi, def) \
    {form, path, key, ApplyTiming::timing, PA_SETTING_FIELD(Section, Type, member), SettingRule::Range, lo, hi, def, nullptr, 0, nullptr}
#define PA_BOOL(form, path, key, timing, Section, Type, member, def) \
    {form, path, key, ApplyTiming::timing, PA_SETTING_FIELD(Section, Type, member), SettingRule::Bool, 0, 1, def, nullptr, 0, nullptr}
#define PA_WORDS(form, path, key, timing, Section, Type, member, words, def) \
    {form, path, key, ApplyTiming::timing, PA_SETTING_FIELD(Section, Type, member), SettingRule::Words, 0, 0, def, &words, 0, nullptr}
#define PA_MEMBER(form, path, key, timing, Section, Type, member, family, says) \
    {form, path, key, ApplyTiming::timing, PA_SETTING_FIELD(Section, Type, member), SettingRule::Member, 0, 0, 0, nullptr, family, says}

const ConfigSetting kConfigSettings[] = {
    // Foot Drive. The three presets must differ and speedLimitMax picks the
    // active one: both are rules across Settings, in configApply(). DriveTask
    // reads the limit and the timeout from the cache every frame
    // (src/tasks/drive.cpp), so each is Immediate.
    PA_RANGE("speedLimitMax", "drive.speedLimitMax", "spd_max", Immediate, Drive, DriveConfig, speedLimitMax,
             0, SPEED_LIMIT_MAX, SPEED_LIMIT_MAX),
    PA_RANGE("speedPresetSlow", "drive.speedPresetSlow", "spd_pre_s", Immediate, Drive, DriveConfig,
             speedPresetSlow, 0, SPEED_LIMIT_MAX, SPEED_PRESET_SLOW),
    PA_RANGE("speedPresetNormal", "drive.speedPresetNormal", "spd_pre_n", Immediate, Drive, DriveConfig,
             speedPresetNormal, 0, SPEED_LIMIT_MAX, SPEED_PRESET_NORMAL),
    PA_RANGE("speedPresetTurbo", "drive.speedPresetTurbo", "spd_pre_t", Immediate, Drive, DriveConfig,
             speedPresetTurbo, 0, SPEED_LIMIT_MAX, SPEED_PRESET_TURBO),
    PA_RANGE("webDriveTimeoutMs", "drive.webDriveTimeoutMs", "web_tmo", Immediate, Drive, DriveConfig,
             webDriveTimeoutMs, 100, 5000, WEB_DRIVE_TIMEOUT_MS),
    PA_BOOL("stationary", "drive.stationary", "op_mode", Immediate, System, SystemConfig, stationary, false),

    // The radio. The receiver and the channels it reads are projected once at
    // start into the settings the droid is driven on
    // (rcInputActiveConfigFromSystem()), so a change waits for a restart the
    // builder makes. The radio itself drives nothing on the controller.
    PA_WORDS("rcInputMode", "rc.inputMode", "rc_mode", RestartRequired, System, SystemConfig, rc_input_mode,
             kRcInputModeWords, RC_INPUT_DUAL_SBUS),
    PA_RANGE("sbusTimeoutMs", "rc.sbusTimeoutMs", "sbus_tmo", Immediate, Drive, DriveConfig, sbusTimeoutMs,
             50, 5000, SBUS_TIMEOUT_MS),
    PA_MEMBER("rcMember", "rc.member", "rc_member", Immediate, System, SystemConfig, rc_member,
              COMPONENT_CATEGORY_RADIO_CONTROLLER, "is not a radio this firmware lists"),
    PA_BOOL("sbusRecvCh2", "rc.sbus.recvCh2", "sbus_recv_ch2", RestartRequired, System, SystemConfig,
            single_sbus_use_ch2, false),

    // The Component Toggles that are not an Output, and the Sound member: each
    // read once at start (ADR 0027, ADR 0042). The RC channel ticks are part of
    // the radio's start-up projection above, so they are a restart.
    PA_BOOL("enableDomeEsc", "components.domeEsc.enabled", "en_dome_esc", AtReboot, System, SystemConfig,
            enable_dome_esc, false),
    PA_BOOL("enableRcCh1", "components.rcCh1.enabled", "en_rc_ch1", RestartRequired, System, SystemConfig,
            enable_rc_ch1, false),
    PA_BOOL("enableRcCh2", "components.rcCh2.enabled", "en_rc_ch2", RestartRequired, System, SystemConfig,
            enable_rc_ch2, false),
    PA_BOOL("enableRcCh3", "components.rcCh3.enabled", "en_rc_ch3", RestartRequired, System, SystemConfig,
            enable_rc_ch3, false),
    PA_BOOL("enableRcCh4", "components.rcCh4.enabled", "en_rc_ch4", RestartRequired, System, SystemConfig,
            enable_rc_ch4, false),
    PA_BOOL("enableRcCh5", "components.rcCh5.enabled", "en_rc_ch5", RestartRequired, System, SystemConfig,
            enable_rc_ch5, false),
    PA_BOOL("enableRcCh6", "components.rcCh6.enabled", "en_rc_ch6", RestartRequired, System, SystemConfig,
            enable_rc_ch6, false),
    PA_BOOL("enableDrive", "components.drive.enabled", "en_drive", AtReboot, System, SystemConfig,
            enable_drive, false),
    PA_BOOL("enableAudio", "components.audio.enabled", "en_audio", AtReboot, System, SystemConfig,
            enable_audio, false),
    PA_MEMBER("soundMember", "components.audio.member", "snd_member", AtReboot, System, SystemConfig,
              sound_member, COMPONENT_CATEGORY_SOUND,
              "is not a sound module this firmware can drive"),
    PA_BOOL("enableProtoR2link", "components.protoR2link.enabled", "en_r2link", AtReboot, System,
            SystemConfig, enable_protor2link, false),

    // Each board Output's wired tick. GET reads it on the Output's row and POST
    // takes it back there (`wired`), so it has no path here; the form name is
    // what the Console writes it by (BOARD_OUTPUTS' `enabledField`). Latched
    // once at start (servoTaskInit(), ADR 0027).
    PA_BOOL("enableArm1", nullptr, "en_arm1", AtReboot, System, SystemConfig, enable_arm1, false),
    PA_BOOL("enableArm2", nullptr, "en_arm2", AtReboot, System, SystemConfig, enable_arm2, false),
    PA_BOOL("enableAux1", nullptr, "en_aux1", AtReboot, System, SystemConfig, enable_aux1, false),
    PA_BOOL("enableAux2", nullptr, "en_aux2", AtReboot, System, SystemConfig, enable_aux2, false),
    PA_BOOL("enableAux3", nullptr, "en_aux3", AtReboot, System, SystemConfig, enable_aux3, false),

    // The Dome ESC. The three pulses must stay in order: a rule across them,
    // in configApply() and on load. DomeTask reads them from the cache on every
    // command and every loop.
    PA_RANGE("domeEscNeutralUs", "domeEsc.neutralUs", "dome_neu", Immediate, Dome, DomeConfig,
             dome_neutral_us, 1000, 2000, 1500),
    PA_RANGE("domeEscMinPulseUs", "domeEsc.minPulseUs", "dome_minp", Immediate, Dome, DomeConfig,
             dome_min_pulse_us, 1000, 2000, 1000),
    PA_RANGE("domeEscMaxPulseUs", "domeEsc.maxPulseUs", "dome_maxp", Immediate, Dome, DomeConfig,
             dome_max_pulse_us, 1000, 2000, 2000),
    PA_RANGE("domeEscSpeedLimitPct", "domeEsc.speedLimitPct", "dome_pct", Immediate, Dome, DomeConfig,
             dome_speed_limit_pct, 0, 100, 100),
    PA_BOOL("domeEscRndEnable", "domeEsc.rndEnable", "dome_rnd_en", Immediate, Dome, DomeConfig,
            dome_rnd_enable, false),
    PA_RANGE("domeEscRndSpeedPct", "domeEsc.rndSpeedPct", "dome_rnd_spd", Immediate, Dome, DomeConfig,
             dome_rnd_speed_pct, 5, 100, 30),
    PA_RANGE("domeEscRndPauseMin", "domeEsc.rndPauseMin", "dome_rnd_pmin", Immediate, Dome, DomeConfig,
             dome_rnd_pause_min, 1, 120, 6),
    PA_RANGE("domeEscRndPauseMax", "domeEsc.rndPauseMax", "dome_rnd_pmax", Immediate, Dome, DomeConfig,
             dome_rnd_pause_max, 1, 120, 12),
    PA_RANGE("domeEscRndMoveMs", "domeEsc.rndMoveMs", "dome_rnd_ms", Immediate, Dome, DomeConfig,
             dome_rnd_move_ms, 500, 10000, 2500),
    {"protoR2linkWifiPeerIp", "protoR2link.wifiPeerIp", "dome_wip", ApplyTiming::Immediate,
     PA_SETTING_FIELD(Dome, DomeConfig, dome_wifi_peer_ip), SettingRule::Ipv4, 0, 0, 0, nullptr, 0,
     "must be empty or a valid IPv4 address"},

    // The log level takes its words as well as its number, at every door, and
    // GET reads the number (#423's round trip).
    {"logLevel", "system.logLevel", "log_level", ApplyTiming::Immediate,
     PA_SETTING_FIELD(System, SystemConfig, logLevel), SettingRule::Range, PA_LOG_LEVEL_ERROR, PA_LOG_LEVEL_DEBUG, PA_LOG_LEVEL, &kLogLevelWords, 0,
     nullptr},
};

#undef PA_RANGE
#undef PA_BOOL
#undef PA_WORDS
#undef PA_MEMBER

// -----------------------------------------------------------------------------
// The audio Settings (#431 addendum)
//
// A track a sound action plays is 1..999; one marked optional here takes 0 for
// "no sound for this". A random-chatter interval is seconds, 0 for never. A
// category's track range bound is 0..999, and the pair rule (0/0 or lo <= hi,
// both set) spans two Settings, so it stays in the category-range core. A CHIRP
// catalog binding (bank, page and index) is the track binding's catalog form,
// stored by the tracks write path on its own keys and checked there.
// -----------------------------------------------------------------------------
#define PA_AUDIO_AT(name, key, timing, member, lo, hi, def, door, repair) \
    {name, nullptr, key, ApplyTiming::timing, PA_SETTING_FIELD(Audio, AudioConfig, member), SettingRule::Range, lo, hi, def, \
     nullptr, 0, nullptr, SettingDoor::door, repair}
// A track field is read back as stored: it may hold a banked CHIRP index.
#define PA_AUDIO(name, key, timing, member, lo, hi, def, door) \
    PA_AUDIO_AT(name, key, timing, member, lo, hi, def, door, false)
#define PA_TRACK(name, key, timing, member, def) \
    PA_AUDIO(name, key, timing, member, 1, 999, def, AudioTracks)
#define PA_OPTIONAL_TRACK(name, key, timing, member) \
    PA_AUDIO(name, key, timing, member, 0, 999, 0, AudioTracks)
#define PA_INTERVAL(name, timing, member, def) \
    PA_AUDIO(name, name, timing, member, 0, 3600, def, AudioTracks)
#define PA_CATEGORY_BOUND(name, key, timing, member) \
    PA_AUDIO(name, key, timing, member, 0, 999, 0, AudioTracks)
#define PA_MOOD_MASK(name, key, timing, member, def) \
    {name, nullptr, key, ApplyTiming::timing, PA_SETTING_FIELD(Audio, AudioConfig, member), SettingRule::Mask, 0, \
     MOOD_CATEGORY_MASK_MAX, def, nullptr, 0, nullptr, SettingDoor::AudioMoodMap}

// Every audio Setting is Immediate: AudioTask reads the tracks, intervals,
// ranges and masks from the cache every pass (audio_config_map.cpp), and the
// volume is pushed to the running module by its Commit Step.
const ConfigSetting kAudioSettings[] = {
    // The DFPlayer Mini's 0..30, and repaired on load as it always was.
    PA_AUDIO_AT("volume", "aud_vol", Immediate, audioVolume, 0, 30, 20, AudioVolume, true),

    PA_TRACK("scream", "snd_scream", Immediate, snd_scream, AUDIO_TRACK_SCREAM),
    PA_TRACK("faint", "snd_faint", Immediate, snd_faint, AUDIO_TRACK_FAINT),
    PA_TRACK("leia", "snd_leia", Immediate, snd_leia, AUDIO_TRACK_LEIA),
    PA_TRACK("cantina_s", "snd_cantina_s", Immediate, snd_cantina_s, AUDIO_TRACK_CANTINA_S),
    PA_TRACK("sw_theme", "snd_sw", Immediate, snd_sw_theme, AUDIO_TRACK_SW_THEME),
    PA_TRACK("imp_march", "snd_march", Immediate, snd_imp_march, AUDIO_TRACK_IMP_MARCH),
    PA_TRACK("cantina_l", "snd_cantina_l", Immediate, snd_cantina_l, AUDIO_TRACK_CANTINA_L),
    PA_TRACK("startup", "snd_startup", Immediate, snd_startup, AUDIO_TRACK_STARTUP),
    PA_OPTIONAL_TRACK("doodoo", "snd_doodoo", Immediate, snd_doodoo),
    PA_OPTIONAL_TRACK("failure", "snd_failure", Immediate, snd_failure),
    PA_OPTIONAL_TRACK("disco", "snd_disco", Immediate, snd_disco),
    PA_OPTIONAL_TRACK("mahna", "snd_mahna", Immediate, snd_mahna),
    PA_OPTIONAL_TRACK("inlove", "snd_inlove", Immediate, snd_inlove),
    PA_OPTIONAL_TRACK("macho", "snd_macho", Immediate, snd_macho),
    PA_OPTIONAL_TRACK("gangnam", "snd_gangnam", Immediate, snd_gangnam),
    PA_OPTIONAL_TRACK("uptown", "snd_uptown", Immediate, snd_uptown),
    PA_OPTIONAL_TRACK("celebr", "snd_celebr", Immediate, snd_celebr),
    PA_OPTIONAL_TRACK("stayin", "snd_stayin", Immediate, snd_stayin),
    PA_OPTIONAL_TRACK("harlem", "snd_harlem", Immediate, snd_harlem),
    PA_OPTIONAL_TRACK("pbjtime", "snd_pbjtime", Immediate, snd_pbjtime),
    PA_OPTIONAL_TRACK("sys_boot", "snd_sys_boot", Immediate, snd_sys_boot),
    PA_OPTIONAL_TRACK("sys_mode_n", "snd_sys_mode_n", Immediate, snd_sys_mode_n),
    PA_OPTIONAL_TRACK("sys_mode_s", "snd_sys_mode_s", Immediate, snd_sys_mode_s),
    PA_OPTIONAL_TRACK("sys_mode_t", "snd_sys_mode_t", Immediate, snd_sys_mode_t),
    PA_OPTIONAL_TRACK("sys_drv_on", "snd_sys_drv_on", Immediate, snd_sys_drv_on),
    PA_OPTIONAL_TRACK("sys_dome_on", "snd_sys_dome_on", Immediate, snd_sys_dome_on),
    // "snd_sys_netdown": 15 characters, the ESP-IDF Preferences key ceiling (#189).
    PA_OPTIONAL_TRACK("sys_net_down", "snd_sys_netdown", Immediate, snd_sys_net_down),
    PA_TRACK("rand_min", "snd_rand_min", Immediate, snd_rand_min, AUDIO_RAND_TRACK_MIN),
    PA_TRACK("rand_max", "snd_rand_max", Immediate, snd_rand_max, AUDIO_RAND_TRACK_MAX),

    PA_INTERVAL("snd_int_quiet", Immediate, snd_int_quiet, AUDIO_RAND_INT_QUIET),
    PA_INTERVAL("snd_int_mid", Immediate, snd_int_mid, AUDIO_RAND_INT_MID),
    PA_INTERVAL("snd_int_full", Immediate, snd_int_full, AUDIO_RAND_INT_FULL),
    PA_INTERVAL("snd_int_awake", Immediate, snd_int_awake, AUDIO_RAND_INT_AWAKE),

    PA_CATEGORY_BOUND("snd_cat_gen_lo", "snd_cat_gen_lo", Immediate, snd_cat_gen_lo),
    PA_CATEGORY_BOUND("snd_cat_gen_hi", "snd_cat_gen_hi", Immediate, snd_cat_gen_hi),
    PA_CATEGORY_BOUND("snd_cat_chat_lo", "snd_cat_chat_lo", Immediate, snd_cat_chat_lo),
    PA_CATEGORY_BOUND("snd_cat_chat_hi", "snd_cat_chat_hi", Immediate, snd_cat_chat_hi),
    PA_CATEGORY_BOUND("snd_cat_hap_lo", "snd_cat_hap_lo", Immediate, snd_cat_hap_lo),
    PA_CATEGORY_BOUND("snd_cat_hap_hi", "snd_cat_hap_hi", Immediate, snd_cat_hap_hi),
    PA_CATEGORY_BOUND("snd_cat_proc_lo", "snd_cat_proc_lo", Immediate, snd_cat_proc_lo),
    PA_CATEGORY_BOUND("snd_cat_proc_hi", "snd_cat_proc_hi", Immediate, snd_cat_proc_hi),
    PA_CATEGORY_BOUND("snd_cat_sad_lo", "snd_cat_sad_lo", Immediate, snd_cat_sad_lo),
    PA_CATEGORY_BOUND("snd_cat_sad_hi", "snd_cat_sad_hi", Immediate, snd_cat_sad_hi),
    PA_CATEGORY_BOUND("snd_cat_sent_lo", "snd_cat_sent_lo", Immediate, snd_cat_sent_lo),
    PA_CATEGORY_BOUND("snd_cat_sent_hi", "snd_cat_sent_hi", Immediate, snd_cat_sent_hi),
    PA_CATEGORY_BOUND("snd_cat_hum_lo", "snd_cat_hum_lo", Immediate, snd_cat_hum_lo),
    PA_CATEGORY_BOUND("snd_cat_hum_hi", "snd_cat_hum_hi", Immediate, snd_cat_hum_hi),
    PA_CATEGORY_BOUND("snd_cat_scrm_lo", "snd_cat_scrm_lo", Immediate, snd_cat_scrm_lo),
    PA_CATEGORY_BOUND("snd_cat_scrm_hi", "snd_cat_scrm_hi", Immediate, snd_cat_scrm_hi),
    PA_CATEGORY_BOUND("snd_cat_ooh_lo", "snd_cat_ooh_lo", Immediate, snd_cat_ooh_lo),
    PA_CATEGORY_BOUND("snd_cat_ooh_hi", "snd_cat_ooh_hi", Immediate, snd_cat_ooh_hi),
    PA_CATEGORY_BOUND("snd_cat_alrm_lo", "snd_cat_alrm_lo", Immediate, snd_cat_alrm_lo),
    PA_CATEGORY_BOUND("snd_cat_alrm_hi", "snd_cat_alrm_hi", Immediate, snd_cat_alrm_hi),
    PA_CATEGORY_BOUND("snd_cat_snrk_lo", "snd_cat_snrk_lo", Immediate, snd_cat_snarky_lo),
    PA_CATEGORY_BOUND("snd_cat_snrk_hi", "snd_cat_snrk_hi", Immediate, snd_cat_snarky_hi),
    PA_CATEGORY_BOUND("snd_cat_whis_lo", "snd_cat_whis_lo", Immediate, snd_cat_whis_lo),
    PA_CATEGORY_BOUND("snd_cat_whis_hi", "snd_cat_whis_hi", Immediate, snd_cat_whis_hi),

    PA_MOOD_MASK("quiet", "snd_moodcat_q", Immediate, snd_moodcat_quiet, 0x0048),
    PA_MOOD_MASK("mid", "snd_moodcat_m", Immediate, snd_moodcat_mid, 0x004F),
    PA_MOOD_MASK("full", "snd_moodcat_f", Immediate, snd_moodcat_full, 0x090F),
    PA_MOOD_MASK("awakeplus", "snd_moodcat_a", Immediate, snd_moodcat_awakeplus, 0x0F8F),
};

#undef PA_AUDIO_AT
#undef PA_AUDIO
#undef PA_TRACK
#undef PA_OPTIONAL_TRACK
#undef PA_INTERVAL
#undef PA_CATEGORY_BOUND
#undef PA_MOOD_MASK

constexpr size_t kAudioSettingCount = sizeof(kAudioSettings) / sizeof(kAudioSettings[0]);

// The CHIRP catalog binding's parts: checks only, stored by the binding's own
// writer on the action's keys (include/config_settings.h), so no section field
// or NVS key is named here and no loop over the stored Settings reaches them.
const ConfigSetting kCatalogBindingSettings[] = {
    {"bank", nullptr, nullptr, ApplyTiming::Immediate, SettingSection::Audio, 0, SettingStorage::U8, 1, SettingRule::Range, 1,
     6, 1, nullptr, 0, nullptr, SettingDoor::AudioTracks, false},
    {"page", nullptr, nullptr, ApplyTiming::Immediate, SettingSection::Audio, 0, SettingStorage::U8, 1, SettingRule::Letter,
     'A', 'Z', 'A', nullptr, 0, nullptr, SettingDoor::AudioTracks, false},
    {"index", nullptr, nullptr, ApplyTiming::Immediate, SettingSection::Audio, 0, SettingStorage::U16, 2, SettingRule::Range,
     1, 65535, 1, nullptr, 0, nullptr, SettingDoor::AudioTracks, false},
};

// Every Setting, the droid's and the audio ones, for the loops that treat them
// alike: the defaults and the NVS save and load.
template <typename Fn>
void forEachSetting(Fn fn) {
    for (const ConfigSetting& setting : kConfigSettings) {
        fn(setting);
    }
    for (const ConfigSetting& setting : kAudioSettings) {
        fn(setting);
    }
}


constexpr size_t kConfigSettingCount = sizeof(kConfigSettings) / sizeof(kConfigSettings[0]);

// -----------------------------------------------------------------------------
// Settings of an Output
//
// The Motion Profile's bounds are the stored row's own
// (servoOutputRowNormalise()), so a number the row door takes is exactly one the
// row keeps. The ends and the centre are checked against what any servo takes,
// SERVO_PULSE_MIN_US..SERVO_PULSE_MAX_US, and nothing narrower: the fitted
// component's band is applied on the row by the Commit Step, which CLAMPS into
// it and names what it moved, because the band can narrow after the ends were
// recorded and refusing would throw a calibration away (ADR 0068).
// -----------------------------------------------------------------------------
// A row Setting's storage, from a row member and the edit member that carries
// it: one type, or the edit would write a different width than the row reads.
template <typename RowMember, typename EditMember>
constexpr SettingStorage rowStorageOf() {
    static_assert(std::is_same<RowMember, EditMember>::value,
                  "a row Setting's ServoOutputRow and ServoOutputEdit members must share a type");
    return settingStorageOf<RowMember>();
}

#define PA_ROW_FIELD(member)                                                             \
    rowStorageOf<decltype(ServoOutputRow::member), decltype(ServoOutputEdit::member)>(), \
        (uint16_t)offsetof(ServoOutputRow, member), (uint16_t)offsetof(ServoOutputEdit, member)

// When each takes effect: the Motion Profile, the ends, the calibrated bit and
// the Parts are read off the live row on every move, and so is the servo model
// `component` names, which bounds the very next move. Whether an Output is
// wired, its LED count and its power-up setting are read once at start
// (servoTaskInit(), auxLedTask(), the boot pass). A `component` change between
// a servo and a light is read at start too (servoTaskInit()'s lit mask); the
// Lights page, the one door that makes it, says so itself (data/lights.js).
const OutputRowSetting kOutputRowSettings[] = {
    {"wired", ApplyTiming::AtReboot, RowSettingStore::Wired, RowSettingOn::Every, SettingStorage::Bool, 0, 0, 0,
     SettingRule::Bool, 0, 1, nullptr},
    {"component", ApplyTiming::Immediate, RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(component),
     SERVO_FIELD_COMPONENT, SettingRule::Words, 0, 0, &kComponentWords},
    {"ledCount", ApplyTiming::AtReboot, RowSettingStore::Row, RowSettingOn::LightCapable, PA_ROW_FIELD(led_count),
     SERVO_FIELD_LED_COUNT, SettingRule::Range, SERVO_LIGHT_LEDS_MIN, SERVO_LIGHT_LEDS_MAX, nullptr},
    {"throwMs", ApplyTiming::Immediate, RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(throw_ms),
     SERVO_FIELD_THROW_MS, SettingRule::Range, SERVO_THROW_MS_MIN, SERVO_THROW_MS_MAX, nullptr},
    {"accelMs", ApplyTiming::Immediate, RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(accel_ms),
     SERVO_FIELD_ACCEL_MS, SettingRule::Range, SERVO_ACCEL_MS_MIN, SERVO_ACCEL_MS_MAX, nullptr},
    {"ease", ApplyTiming::Immediate, RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(easing), SERVO_FIELD_EASING,
     SettingRule::Words, 0, 0, &kEasingWords},
    {"boot", ApplyTiming::AtReboot, RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(boot), SERVO_FIELD_BOOT,
     SettingRule::Words, 0, 0, &kBootWords},
    {"openUs", ApplyTiming::Immediate, RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(open_us), SERVO_FIELD_OPEN,
     SettingRule::Range, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US, nullptr},
    {"centreUs", ApplyTiming::Immediate, RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(centre_us),
     SERVO_FIELD_CENTRE, SettingRule::Range, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US, nullptr},
    {"closeUs", ApplyTiming::Immediate, RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(close_us),
     SERVO_FIELD_CLOSE, SettingRule::Range, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US, nullptr},
    {"calibrated", ApplyTiming::Immediate, RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(calibrated),
     SERVO_FIELD_CALIBRATED, SettingRule::Bool, 0, 1, nullptr},
    {"parts", ApplyTiming::Immediate, RowSettingStore::Parts, RowSettingOn::Every, SettingStorage::Text, 0, 0,
     SERVO_FIELD_PARTS, SettingRule::Range, 0, SERVO_OUTPUT_PART_SLOTS, nullptr},
};

#undef PA_ROW_FIELD
#undef PA_SETTING_FIELD

constexpr size_t kOutputRowSettingCount = sizeof(kOutputRowSettings) / sizeof(kOutputRowSettings[0]);

// -----------------------------------------------------------------------------
// Storage
// -----------------------------------------------------------------------------
int32_t loadNumber(SettingStorage storage, const uint8_t* at) {
    switch (storage) {
        case SettingStorage::Bool: {
            bool v;
            memcpy(&v, at, sizeof(v));
            return v ? 1 : 0;
        }
        case SettingStorage::U8:
            return *at;
        case SettingStorage::U16: {
            uint16_t v;
            memcpy(&v, at, sizeof(v));
            return v;
        }
        case SettingStorage::I16: {
            int16_t v;
            memcpy(&v, at, sizeof(v));
            return v;
        }
        case SettingStorage::U32: {
            uint32_t v;
            memcpy(&v, at, sizeof(v));
            return (int32_t)v;
        }
        case SettingStorage::Text:
        default:
            return 0;
    }
}

void storeNumber(SettingStorage storage, uint8_t* at, int32_t value) {
    switch (storage) {
        case SettingStorage::Bool: {
            const bool v = value != 0;
            memcpy(at, &v, sizeof(v));
            break;
        }
        case SettingStorage::U8:
            *at = (uint8_t)value;
            break;
        case SettingStorage::U16: {
            const uint16_t v = (uint16_t)value;
            memcpy(at, &v, sizeof(v));
            break;
        }
        case SettingStorage::I16: {
            const int16_t v = (int16_t)value;
            memcpy(at, &v, sizeof(v));
            break;
        }
        case SettingStorage::U32: {
            const uint32_t v = (uint32_t)value;
            memcpy(at, &v, sizeof(v));
            break;
        }
        case SettingStorage::Text:
        default:
            break;
    }
}

uint8_t* sectionOf(ConfigSnapshot* snap, SettingSection section) {
    switch (section) {
        case SettingSection::Drive:
            return reinterpret_cast<uint8_t*>(&snap->drive);
        case SettingSection::Dome:
            return reinterpret_cast<uint8_t*>(&snap->dome);
        case SettingSection::Audio:
            return reinterpret_cast<uint8_t*>(&snap->audio);
        case SettingSection::System:
        default:
            return reinterpret_cast<uint8_t*>(&snap->system);
    }
}

const uint8_t* sectionOf(const ConfigSnapshot& snap, SettingSection section) {
    return sectionOf(const_cast<ConfigSnapshot*>(&snap), section);
}

// -----------------------------------------------------------------------------
// Words
// -----------------------------------------------------------------------------
bool wordEqualsIgnoringCase(const char* typed, const char* word) {
    size_t i = 0;
    for (; word[i] != '\0'; ++i) {
        if (tolower((unsigned char)typed[i]) != tolower((unsigned char)word[i])) {
            return false;
        }
    }
    return typed[i] == '\0';
}

bool wordValue(const SettingWords& words, const char* raw, int32_t* value) {
    for (uint8_t i = 0; i < words.count; ++i) {
        const uint8_t v = (uint8_t)(words.first + i);
        const char* name = words.nameOf(v);
        if (name != nullptr && wordEqualsIgnoringCase(raw, name)) {
            *value = v;
            return true;
        }
    }
    return false;
}

bool wordIsKnown(const SettingWords& words, int32_t value) {
    return value >= words.first && value < words.first + words.count &&
           words.nameOf((uint8_t)value) != nullptr;
}

// `a,b,c` for accepts, or `a, b, c or d` for the sentence.
void joinWords(const SettingWords& words, bool forSentence, char* out, size_t outSize) {
    size_t used = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < words.count && used < outSize; ++i) {
        const char* sep = i == 0 ? "" : !forSentence ? "," : i + 1 == words.count ? " or " : ", ";
        const int n = snprintf(out + used, outSize - used, "%s%s", sep,
                               words.nameOf((uint8_t)(words.first + i)));
        if (n < 0) {
            break;
        }
        used += (size_t)n;
    }
}

// -----------------------------------------------------------------------------
// Checks shared by both kinds of Setting
// -----------------------------------------------------------------------------
constexpr const char* kBoolAccepts = "true,false,1,0";

// The number `raw` names under a Range, Words or Bool rule. False, with the
// refusal and its sentence written, when the rule does not take it.
bool parseNumberRule(SettingRule rule, SettingStorage storage, int32_t lo, int32_t hi,
                     const SettingWords* words, const char* raw, const char* field,
                     int32_t* value, ApplyRefusal* refusal, char* sentence, size_t sentenceSize) {
    if (rule == SettingRule::Bool) {
        bool b = false;
        if (raw != nullptr && parseBoolValue(raw, &b)) {
            *value = b ? 1 : 0;
            return true;
        }
        snprintf(sentence, sentenceSize, "%s must be true/false or 1/0", field);
        applyRefusalSet(refusal, ApplyRefusalReason::OutOfRange, field, kBoolAccepts);
        return false;
    }

    if (raw != nullptr && words != nullptr && wordValue(*words, raw, value)) {
        return true;
    }

    if (rule == SettingRule::Letter) {
        const int letter = raw != nullptr && raw[0] != '\0' && raw[1] == '\0'
                               ? toupper((unsigned char)raw[0])
                               : -1;
        if (letter >= lo && letter <= hi) {
            *value = letter;
            return true;
        }
        if (sentence != nullptr) {
            snprintf(sentence, sentenceSize, "%s must be a single letter %c-%c", field, (char)lo,
                     (char)hi);
        }
        char accepts[8];
        snprintf(accepts, sizeof(accepts), "%c..%c", (char)lo, (char)hi);
        applyRefusalSet(refusal, ApplyRefusalReason::OutOfRange, field, accepts);
        return false;
    }

    if ((rule == SettingRule::Range || rule == SettingRule::Mask) && raw != nullptr) {
        bool parsed = false;
        if (storage == SettingStorage::I16) {
            int16_t v = 0;
            parsed = parseDriveValue(raw, &v);
            *value = v;
        } else {
            uint32_t v = 0;
            parsed = parseUint32Value(raw, &v) && v <= (uint32_t)INT32_MAX;
            *value = (int32_t)v;
        }
        if (parsed && *value >= lo && *value <= hi) {
            return true;
        }
    }

    // A Setting that takes words is refused in its words, a number in its range.
    if (words != nullptr) {
        char list[APPLY_REFUSAL_ACCEPTS_MAX];
        joinWords(*words, true, list, sizeof(list));
        snprintf(sentence, sentenceSize, "%s must be %s", field, list);
        joinWords(*words, false, list, sizeof(list));
        applyRefusalSet(refusal, ApplyRefusalReason::OutOfRange, field, list);
    } else {
        snprintf(sentence, sentenceSize, "%s must be %ld..%ld", field, (long)lo, (long)hi);
        applyRefusalSetRange(refusal, field, lo, hi);
    }
    return false;
}

bool isValidIpv4Literal(const char* raw) {
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }
    const char* p = raw;
    int octetCount = 0;
    while (*p != '\0') {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        int value = 0;
        int digits = 0;
        while (*p != '\0' && *p != '.') {
            if (!isdigit((unsigned char)*p)) {
                return false;
            }
            value = (value * 10) + (*p - '0');
            digits++;
            if (digits > 3 || value > 255) {
                return false;
            }
            p++;
        }
        if (digits == 0) {
            return false;
        }
        octetCount++;
        if (octetCount > 4) {
            return false;
        }
        if (*p == '.') {
            p++;
            if (*p == '\0') {
                return false;
            }
        }
    }
    return octetCount == 4;
}

}  // namespace

// =============================================================================
// Settings of the droid
// =============================================================================
size_t configSettingCount() { return kConfigSettingCount; }

const ConfigSetting& configSettingAt(size_t index) { return kConfigSettings[index]; }

const ConfigSetting* configSettingByForm(const char* form) {
    if (form == nullptr) {
        return nullptr;
    }
    for (const ConfigSetting& setting : kConfigSettings) {
        if (strcmp(setting.form, form) == 0) {
            return &setting;
        }
    }
    return nullptr;
}

int32_t configSettingNumber(const ConfigSetting& setting, const ConfigSnapshot& snap) {
    return loadNumber(setting.storage, sectionOf(snap, setting.section) + setting.offset);
}

void configSettingSetNumber(const ConfigSetting& setting, ConfigSnapshot* snap, int32_t value) {
    storeNumber(setting.storage, sectionOf(snap, setting.section) + setting.offset, value);
}

void configSettingFormat(const ConfigSetting& setting, const ConfigSnapshot& snap, char* out,
                         size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return;
    }
    const uint8_t* at = sectionOf(snap, setting.section) + setting.offset;
    const int32_t value = loadNumber(setting.storage, at);
    switch (setting.rule) {
        case SettingRule::Bool:
            snprintf(out, outSize, "%s", value != 0 ? "true" : "false");
            return;
        case SettingRule::Words: {
            const char* word = wordIsKnown(*setting.words, value) ? setting.words->nameOf((uint8_t)value) : "";
            snprintf(out, outSize, "%s", word);
            return;
        }
        case SettingRule::Member: {
            const ComponentPartEntry* part = componentPartByValue((uint8_t)value);
            snprintf(out, outSize, "%s", part != nullptr ? part->id : "");
            return;
        }
        case SettingRule::Ipv4:
            snprintf(out, outSize, "%.*s", (int)setting.size, reinterpret_cast<const char*>(at));
            return;
        case SettingRule::Range:
        default:
            snprintf(out, outSize, "%ld", (long)value);
            return;
    }
}

bool configSettingApply(const ConfigSetting& setting, const char* raw, ConfigSnapshot* snap,
                        ApplyRefusal* refusal, char* sentence, size_t sentenceSize) {
    uint8_t* at = sectionOf(snap, setting.section) + setting.offset;
    switch (setting.rule) {
        case SettingRule::Member: {
            // Named by its Component Registry id rather than the number it is
            // stored as, so nothing outside the registry has to know the
            // numbering. A roadmap row and a member of another family are both
            // refused, by the rule a picker's lineup comes from. The sentence
            // never echoes what was asked: it lands in a JSON error body.
            const ComponentPartEntry* part = componentPartById(raw);
            if (part == nullptr || part->category != setting.family ||
                !componentPartIsSelectable(*part)) {
                snprintf(sentence, sentenceSize, "%s %s", setting.form, setting.says);
                applyRefusalSet(refusal, ApplyRefusalReason::OutOfRange, setting.form);
                return false;
            }
            storeNumber(setting.storage, at, part->value);
            return true;
        }
        case SettingRule::Ipv4: {
            const bool empty = raw != nullptr && raw[0] == '\0';
            if (raw == nullptr || strlen(raw) >= setting.size || (!empty && !isValidIpv4Literal(raw))) {
                snprintf(sentence, sentenceSize, "%s %s", setting.form, setting.says);
                applyRefusalSet(refusal, ApplyRefusalReason::OutOfRange, setting.form);
                return false;
            }
            snprintf(reinterpret_cast<char*>(at), setting.size, "%s", raw);
            return true;
        }
        case SettingRule::Range:
        case SettingRule::Words:
        case SettingRule::Bool:
        default: {
            int32_t value = 0;
            if (!parseNumberRule(setting.rule, setting.storage, setting.lo, setting.hi,
                                 setting.words, raw, setting.form, &value, refusal, sentence,
                                 sentenceSize)) {
                return false;
            }
            storeNumber(setting.storage, at, value);
            return true;
        }
    }
}

void configSettingsDefaults(ConfigSnapshot* snap) {
    forEachSetting([snap](const ConfigSetting& setting) {
        uint8_t* at = sectionOf(snap, setting.section) + setting.offset;
        switch (setting.rule) {
            case SettingRule::Member:
                // A controller never asked which product it has starts on the
                // one its build names.
                storeNumber(setting.storage, at,
                            componentCategoryDefaultMember((ComponentCategoryId)setting.family));
                break;
            case SettingRule::Ipv4:
                at[0] = '\0';
                break;
            default:
                storeNumber(setting.storage, at, setting.def);
                break;
        }
    });
}

bool configSettingsWrite(SettingSection section, const void* sectionData, ConfigWriter& writer) {
    const uint8_t* base = static_cast<const uint8_t*>(sectionData);
    bool ok = true;
    forEachSetting([&](const ConfigSetting& setting) {
        if (setting.section != section) {
            return;
        }
        const uint8_t* at = base + setting.offset;
        int32_t value = loadNumber(setting.storage, at);
        if (setting.rule == SettingRule::Mask) {
            value &= setting.hi;  // the bits above the mask were flags, and are not stored
        }
        switch (setting.storage) {
            case SettingStorage::Bool:
                ok = writer.writeBool(setting.nvsKey, value != 0) && ok;
                break;
            case SettingStorage::U8:
                ok = writer.writeU8(setting.nvsKey, (uint8_t)value) && ok;
                break;
            case SettingStorage::U16:
                ok = writer.writeU16(setting.nvsKey, (uint16_t)value) && ok;
                break;
            case SettingStorage::I16:
                ok = writer.writeI16(setting.nvsKey, (int16_t)value) && ok;
                break;
            case SettingStorage::U32:
                ok = writer.writeU32(setting.nvsKey, (uint32_t)value) && ok;
                break;
            case SettingStorage::Text:
                // Written unconditionally, empty included. An empty peer IP is
                // the "not configured" state, and it must overwrite whatever an
                // earlier save stored: NVS keeps every key a save does not
                // touch, so skipping the key here would leave the old address
                // to come back on the next cold boot. Writing "" is a real
                // store, not a no-op: Preferences::putString only short-circuits
                // on a null pointer, then calls nvs_set_str (arduino-esp32
                // 3.3.7, libraries/Preferences/src/Preferences.cpp:264-279),
                // which stores strlen(value) + 1 bytes -- one byte for ""
                // (ESP-IDF 5.5, components/nvs_flash/src/nvs_handle_simple.cpp:31-37).
                // PrefsWriter::writeStr already treats putString's 0 return for
                // an empty string as success, and the WiFi serializer relies on
                // the same path for an empty STA SSID.
                ok = writer.writeStr(setting.nvsKey, reinterpret_cast<const char*>(at)) && ok;
                break;
        }
    });
    return ok;
}

void configSettingsRead(SettingSection section, const ConfigReader& reader, void* sectionData) {
    uint8_t* base = static_cast<uint8_t*>(sectionData);
    forEachSetting([&](const ConfigSetting& setting) {
        if (setting.section != section) {
            return;
        }
        uint8_t* at = base + setting.offset;
        const int32_t held = loadNumber(setting.storage, at);
        int32_t value = held;
        switch (setting.storage) {
            case SettingStorage::Bool:
                value = reader.readBool(setting.nvsKey, held != 0) ? 1 : 0;
                break;
            case SettingStorage::U8:
                value = reader.readU8(setting.nvsKey, (uint8_t)held);
                break;
            case SettingStorage::U16:
                value = reader.readU16(setting.nvsKey, (uint16_t)held);
                break;
            case SettingStorage::I16:
                value = reader.readI16(setting.nvsKey, (int16_t)held);
                break;
            case SettingStorage::U32: {
                // A stored number past INT32_MAX is out of every range here.
                const uint32_t raw = reader.readU32(setting.nvsKey, (uint32_t)held);
                value = raw > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)raw;
                break;
            }
            case SettingStorage::Text: {
                // Anything too long for its field is refused rather than cut:
                // half an address is not an address.
                const String stored = reader.readStr(setting.nvsKey, reinterpret_cast<const char*>(at));
                if (stored.length() >= setting.size) {
                    at[0] = '\0';
                } else {
                    snprintf(reinterpret_cast<char*>(at), setting.size, "%s", stored.c_str());
                }
                return;
            }
        }
        // What the door would refuse is repaired on the way in, so a value no
        // page could have saved never reaches the task that reads it.
        if (setting.repairOnLoad) {
            switch (setting.rule) {
                case SettingRule::Range:
                    value = value < setting.lo ? setting.lo : value > setting.hi ? setting.hi : value;
                    break;
                case SettingRule::Words:
                    if (!wordIsKnown(*setting.words, value)) {
                        value = setting.def;
                    }
                    break;
                case SettingRule::Mask:
                    value &= setting.hi;
                    break;
                case SettingRule::Bool:
                case SettingRule::Member:
                case SettingRule::Ipv4:
                default:
                    break;
            }
        }
        storeNumber(setting.storage, at, value);
    });
}

// =============================================================================
// The audio Settings
// =============================================================================
size_t catalogBindingSettingCount() {
    return sizeof(kCatalogBindingSettings) / sizeof(kCatalogBindingSettings[0]);
}

const ConfigSetting& catalogBindingSettingAt(size_t index) { return kCatalogBindingSettings[index]; }

const ConfigSetting* catalogBindingSetting(const char* part) {
    if (part == nullptr) {
        return nullptr;
    }
    for (const ConfigSetting& setting : kCatalogBindingSettings) {
        if (strcmp(setting.form, part) == 0) {
            return &setting;
        }
    }
    return nullptr;
}

size_t audioSettingCount() { return kAudioSettingCount; }

const ConfigSetting& audioSettingAt(size_t index) { return kAudioSettings[index]; }

const ConfigSetting* audioSettingByName(const char* name, SettingDoor door) {
    if (name == nullptr) {
        return nullptr;
    }
    for (const ConfigSetting& setting : kAudioSettings) {
        if (setting.door == door && strcmp(setting.form, name) == 0) {
            return &setting;
        }
    }
    return nullptr;
}

bool configSettingCheck(const ConfigSetting& setting, const char* raw, const char* field,
                        int32_t* value, ApplyRefusal* refusal, char* sentence,
                        size_t sentenceSize) {
    return parseNumberRule(setting.rule, setting.storage, setting.lo, setting.hi, setting.words,
                           raw, field, value, refusal, sentence, sentenceSize);
}

// =============================================================================
// Settings of an Output
// =============================================================================
size_t outputRowSettingCount() { return kOutputRowSettingCount; }

const OutputRowSetting& outputRowSettingAt(size_t index) { return kOutputRowSettings[index]; }

bool outputRowSettingIsOn(const OutputRowSetting& setting, const BoardOutput* board) {
    return setting.on == RowSettingOn::Every || (board != nullptr && board->lightCapable);
}

int32_t outputRowSettingNumber(const OutputRowSetting& setting, const ServoOutputRow& row) {
    return loadNumber(setting.storage, reinterpret_cast<const uint8_t*>(&row) + setting.rowOffset);
}

const char* outputRowSettingWord(const OutputRowSetting& setting, const ServoOutputRow& row) {
    const int32_t value = outputRowSettingNumber(setting, row);
    return setting.words != nullptr && wordIsKnown(*setting.words, value)
               ? setting.words->nameOf((uint8_t)value)
               : "";
}

bool outputRowSettingParse(const OutputRowSetting& setting, const char* raw, const char* field,
                           int32_t* value, ApplyRefusal* refusal, char* sentence,
                           size_t sentenceSize) {
    return parseNumberRule(setting.rule, setting.storage, setting.lo, setting.hi, setting.words, raw,
                           field, value, refusal, sentence, sentenceSize);
}

void outputRowSettingSetOnEdit(const OutputRowSetting& setting, int32_t value, ServoOutputEdit* edit) {
    storeNumber(setting.storage, reinterpret_cast<uint8_t*>(edit) + setting.editOffset, value);
    edit->fields |= setting.fieldBit;
}
