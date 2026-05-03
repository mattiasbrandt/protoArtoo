// =============================================================================
// src/config_store.cpp
//
// Config schema module implementation — centralized NVS load/save and validation.
// =============================================================================

#include "config_store.h"

#include "audio_dollar_parser.h"
#include "config.h"
#include "logging.h"
#include "rc_mapping.h"

#include <cstring>

// IPAddress.h is only available in Arduino/ESP-IDF environments, not native tests
#ifdef ARDUINO
#include <IPAddress.h>
#endif

namespace {

// NVS key strings — all cfg_* keys are defined here and ONLY here
const struct {
    const char* speedLimitMax = "spd_max";
    const char* speedPresetSlow = "spd_pre_s";
    const char* speedPresetNormal = "spd_pre_n";
    const char* speedPresetTurbo = "spd_pre_t";
    const char* speedPresetActive = "spd_pre_a";
    const char* sbusTimeoutMs = "sbus_tmo";
    const char* webDriveTimeoutMs = "web_tmo";
    const char* audioVolume = "aud_vol";
    const char* logLevel = "log_level";
    const char* sndScream = "snd_scream";
    const char* sndFaint = "snd_faint";
    const char* sndLeia = "snd_leia";
    const char* sndCantinaS = "snd_cantina_s";
    const char* sndSwTheme = "snd_sw";
    const char* sndImpMarch = "snd_march";
    const char* sndCantinaL = "snd_cantina_l";
    const char* sndStartup = "snd_startup";
    const char* sndDoodoo = "snd_doodoo";
    const char* sndFailure = "snd_failure";
    const char* sndDisco = "snd_disco";
    const char* sndMahna = "snd_mahna";
    const char* sndInlove = "snd_inlove";
    const char* sndMacho = "snd_macho";
    const char* sndGangnam = "snd_gangnam";
    const char* sndUptown = "snd_uptown";
    const char* sndCelebr = "snd_celebr";
    const char* sndStayin = "snd_stayin";
    const char* sndHarlem = "snd_harlem";
    const char* sndPbjtime = "snd_pbjtime";
    const char* sndSysBoot = "snd_sys_boot";
    const char* sndSysModeN = "snd_sys_mode_n";
    const char* sndSysModeS = "snd_sys_mode_s";
    const char* sndSysModeT = "snd_sys_mode_t";
    const char* sndSysDrvOn = "snd_sys_drv_on";
    const char* sndSysDomeOn = "snd_sys_dome_on";
    const char* sndRandMin = "snd_rand_min";
    const char* sndRandMax = "snd_rand_max";
    const char* sndIntQuiet = "snd_int_quiet";
    const char* sndIntMid = "snd_int_mid";
    const char* sndIntFull = "snd_int_full";
    const char* sndIntAwake = "snd_int_awake";
    const char* sndMoodcatQuiet = "snd_moodcat_q";
    const char* sndMoodcatMid = "snd_moodcat_m";
    const char* sndMoodcatFull = "snd_moodcat_f";
    const char* sndMoodcatAwakeplus = "snd_moodcat_a";
    const char* sndCatGenLo = "snd_cat_gen_lo";
    const char* sndCatGenHi = "snd_cat_gen_hi";
    const char* sndCatChatLo = "snd_cat_chat_lo";
    const char* sndCatChatHi = "snd_cat_chat_hi";
    const char* sndCatHapLo = "snd_cat_hap_lo";
    const char* sndCatHapHi = "snd_cat_hap_hi";
    const char* sndCatProcLo = "snd_cat_proc_lo";
    const char* sndCatProcHi = "snd_cat_proc_hi";
    const char* sndCatSadLo = "snd_cat_sad_lo";
    const char* sndCatSadHi = "snd_cat_sad_hi";
    const char* sndCatSentLo = "snd_cat_sent_lo";
    const char* sndCatSentHi = "snd_cat_sent_hi";
    const char* sndCatHumLo = "snd_cat_hum_lo";
    const char* sndCatHumHi = "snd_cat_hum_hi";
    const char* sndCatScrmLo = "snd_cat_scrm_lo";
    const char* sndCatScrmHi = "snd_cat_scrm_hi";
    const char* sndCatOohLo = "snd_cat_ooh_lo";
    const char* sndCatOohHi = "snd_cat_ooh_hi";
    const char* sndCatAlrmLo = "snd_cat_alrm_lo";
    const char* sndCatAlrmHi = "snd_cat_alrm_hi";
    const char* sndCatSnarkyLo = "snd_cat_snrk_lo";
    const char* sndCatSnarkyHi = "snd_cat_snrk_hi";
    const char* sndCatWhisLo = "snd_cat_whis_lo";
    const char* sndCatWhisHi = "snd_cat_whis_hi";
    const char* arm1OpenUs = "arm1_op";
    const char* arm1CloseUs = "arm1_cl";
    const char* arm2OpenUs = "arm2_op";
    const char* arm2CloseUs = "arm2_cl";
    const char* arm1Type = "arm1_type";
    const char* arm2Type = "arm2_type";
    const char* aux1OpenUs = "aux1_op";
    const char* aux1CloseUs = "aux1_cl";
    const char* aux2OpenUs = "aux2_op";
    const char* aux2CloseUs = "aux2_cl";
    const char* aux3OpenUs = "aux3_op";
    const char* aux3CloseUs = "aux3_cl";
    const char* aux1Type = "aux1_type";
    const char* aux2Type = "aux2_type";
    const char* aux3Type = "aux3_type";
    const char* domeMin = "dome_min";
    const char* domeMax = "dome_max";
    const char* domeNeutralUs = "dome_neu";
    const char* domeMinPulseUs = "dome_minp";
    const char* domeMaxPulseUs = "dome_maxp";
    const char* domeSpeedLimitPct = "dome_pct";
    const char* domeRndEnable = "dome_rnd_en";
    const char* domeRndSpeedPct = "dome_rnd_spd";
    const char* domeRndPauseMin = "dome_rnd_pmin";
    const char* domeRndPauseMax = "dome_rnd_pmax";
    const char* domeRndMoveMs = "dome_rnd_ms";
    const char* domeWifiPeerIp = "dome_wip";
    const char* seqOpenMs = "seq_op";
    const char* seqCloseMs = "seq_cl";
    const char* auxLedPin = NVS_KEY_AUX_LED_PIN;
    const char* auxLedCount = NVS_KEY_AUX_LED_COUNT;
    const char* enableArm1 = "en_arm1";
    const char* enableArm2 = "en_arm2";
    const char* enableAux1 = "en_aux1";
    const char* enableAux2 = "en_aux2";
    const char* enableAux3 = "en_aux3";
    const char* enableDome = "en_dome";
    const char* enableRcCh1 = "en_rc_ch1";
    const char* enableRcCh2 = "en_rc_ch2";
    const char* enableRcCh3 = "en_rc_ch3";
    const char* enableRcCh4 = "en_rc_ch4";
    const char* enableRcCh5 = "en_rc_ch5";
    const char* enableRcCh6 = "en_rc_ch6";
    const char* singleSbusUseCh2 = "sbus_recv_ch2";
    const char* enableS1Hoverboard = "en_s1";
    const char* enableS2Sound = "en_s2";
    const char* enableS3DomeCtrl = "en_s3";
    const char* stationary = "op_mode";
    const char* rcInputMode = "rc_mode";
    const char* rcPwmDriveSpeed = "rcp_drv";
    const char* rcPwmDriveSteer = "rcp_str";
    const char* rcPwmDomeSpeed = "rcp_dom";
    const char* rcPwmArm1 = "rcp_a1";
    const char* rcPwmArm2 = "rcp_a2";
    const char* rcPwmSound = "rcp_snd";
    const char* rcSbusDriveSpeed = "rcs_drv";
    const char* rcSbusDriveSteer = "rcs_str";
    const char* rcSbusDomeSpeed = "rcs_dom";
    const char* rcSbusArm1 = "rcs_a1";
    const char* rcSbusArm2 = "rcs_a2";
    const char* rcSbusSound = "rcs_snd";
    const char* rcArm1 = "rc_arm1";
    const char* rcArm2 = "rc_arm2";
    const char* rcAux1 = "rc_aux1";
    const char* rcAux2 = "rc_aux2";
    const char* rcAux3 = "rc_aux3";
    const char* rcSound = "rc_sound";
    const char* rcOpmode = "rc_opmode";
    const char* rcFree0 = "rc_free0";
    const char* rcFree1 = "rc_free1";
    const char* rcFree2 = "rc_free2";
    const char* rcFree3 = "rc_free3";
} NVS_KEYS;

// Helper: Load RC binding from NVS
bool loadRcBindingFromPrefs(Preferences& prefs, const char* key,
                            const RcBindingConfig& defaultValue, RcBindingConfig* out) {
    if (out == nullptr) {
        return false;
    }
    char fallback[48] = {};
    if (!formatRcBindingConfig(fallback, sizeof(fallback), defaultValue)) {
        *out = defaultValue;
        return false;
    }
    String stored = prefs.getString(key, String(fallback));
    RcBindingConfig parsed = defaultValue;
    if (!parseRcBindingConfig(stored.c_str(), &parsed)) {
        parsed = defaultValue;
    }
    *out = parsed;
    return true;
}

// Helper: Save RC binding to NVS
bool saveRcBindingToPrefs(Preferences& prefs, const char* key, const RcBindingConfig& binding) {
    char encoded[48] = {};
    if (!formatRcBindingConfig(encoded, sizeof(encoded), binding)) {
        return false;
    }
    return prefs.putString(key, encoded) > 0;
}

// Helper: Load RC trigger binding from NVS
bool loadRcTriggerBindingFromPrefs(Preferences& prefs, const char* key,
                                   const RcTriggerBinding& defaultValue, RcTriggerBinding* out) {
    if (out == nullptr) {
        return false;
    }
    char fallback[64] = {};
    if (!formatRcTriggerBinding(fallback, sizeof(fallback), defaultValue)) {
        *out = defaultValue;
        return false;
    }
    String stored = prefs.getString(key, String(fallback));
    RcTriggerBinding parsed = defaultValue;
    if (!parseRcTriggerBinding(stored.c_str(), &parsed)) {
        parsed = defaultValue;
    }
    *out = parsed;
    return true;
}

// Helper: Save RC trigger binding to NVS
bool saveRcTriggerBindingToPrefs(Preferences& prefs, const char* key,
                                 const RcTriggerBinding& binding) {
    char encoded[64] = {};
    if (!formatRcTriggerBinding(encoded, sizeof(encoded), binding)) {
        return false;
    }
    return prefs.putString(key, encoded) > 0;
}

float floatFromBits(uint32_t value) {
    float result = 0.0f;
    memcpy(&result, &value, sizeof(result));
    return result;
}

uint32_t floatToBits(float value) {
    uint32_t result = 0;
    memcpy(&result, &value, sizeof(result));
    return result;
}

// Helper: Populate ConfigSnapshot with defaults
void configSnapshotDefaults(ConfigSnapshot* snap) {
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

}  // namespace

// =============================================================================
// Public API Implementation
// =============================================================================

void configSnapshotFromRobotState(ConfigSnapshot* out) {
    out->drive.speedLimitMax        = robotState.cfg_speedLimitMax;
    out->drive.speedPresetSlow      = robotState.cfg_speedPresetSlow;
    out->drive.speedPresetNormal    = robotState.cfg_speedPresetNormal;
    out->drive.speedPresetTurbo     = robotState.cfg_speedPresetTurbo;
    out->drive.speedPresetActive    = robotState.cfg_speedPresetActive;
    out->drive.sbusTimeoutMs        = robotState.cfg_sbusTimeoutMs;
    out->drive.webDriveTimeoutMs    = robotState.cfg_webDriveTimeoutMs;
    out->audio.audioVolume          = robotState.cfg_audioVolume;
    out->system.logLevel             = robotState.cfg_logLevel;
    out->audio.snd_scream           = robotState.cfg_snd_scream;
    out->audio.snd_faint            = robotState.cfg_snd_faint;
    out->audio.snd_leia             = robotState.cfg_snd_leia;
    out->audio.snd_cantina_s        = robotState.cfg_snd_cantina_s;
    out->audio.snd_sw_theme         = robotState.cfg_snd_sw_theme;
    out->audio.snd_imp_march        = robotState.cfg_snd_imp_march;
    out->audio.snd_cantina_l        = robotState.cfg_snd_cantina_l;
    out->audio.snd_startup          = robotState.cfg_snd_startup;
    out->audio.snd_doodoo           = robotState.cfg_snd_doodoo;
    out->audio.snd_failure          = robotState.cfg_snd_failure;
    out->audio.snd_disco            = robotState.cfg_snd_disco;
    out->audio.snd_mahna            = robotState.cfg_snd_mahna;
    out->audio.snd_inlove           = robotState.cfg_snd_inlove;
    out->audio.snd_macho            = robotState.cfg_snd_macho;
    out->audio.snd_gangnam          = robotState.cfg_snd_gangnam;
    out->audio.snd_uptown           = robotState.cfg_snd_uptown;
    out->audio.snd_celebr           = robotState.cfg_snd_celebr;
    out->audio.snd_stayin           = robotState.cfg_snd_stayin;
    out->audio.snd_harlem           = robotState.cfg_snd_harlem;
    out->audio.snd_pbjtime          = robotState.cfg_snd_pbjtime;
    out->audio.snd_sys_boot         = robotState.cfg_snd_sys_boot;
    out->audio.snd_sys_mode_n       = robotState.cfg_snd_sys_mode_n;
    out->audio.snd_sys_mode_s       = robotState.cfg_snd_sys_mode_s;
    out->audio.snd_sys_mode_t       = robotState.cfg_snd_sys_mode_t;
    out->audio.snd_sys_drv_on       = robotState.cfg_snd_sys_drv_on;
    out->audio.snd_sys_dome_on      = robotState.cfg_snd_sys_dome_on;
    out->audio.snd_rand_min         = robotState.cfg_snd_rand_min;
    out->audio.snd_rand_max         = robotState.cfg_snd_rand_max;
    out->audio.snd_int_quiet        = robotState.cfg_snd_int_quiet;
    out->audio.snd_int_mid          = robotState.cfg_snd_int_mid;
    out->audio.snd_int_full         = robotState.cfg_snd_int_full;
    out->audio.snd_int_awake        = robotState.cfg_snd_int_awake;
    out->audio.snd_moodcat_quiet    = robotState.cfg_snd_moodcat_quiet;
    out->audio.snd_moodcat_mid      = robotState.cfg_snd_moodcat_mid;
    out->audio.snd_moodcat_full     = robotState.cfg_snd_moodcat_full;
    out->audio.snd_moodcat_awakeplus = robotState.cfg_snd_moodcat_awakeplus;
    out->audio.snd_cat_gen_lo       = robotState.cfg_snd_cat_gen_lo;
    out->audio.snd_cat_gen_hi       = robotState.cfg_snd_cat_gen_hi;
    out->audio.snd_cat_chat_lo      = robotState.cfg_snd_cat_chat_lo;
    out->audio.snd_cat_chat_hi      = robotState.cfg_snd_cat_chat_hi;
    out->audio.snd_cat_hap_lo       = robotState.cfg_snd_cat_hap_lo;
    out->audio.snd_cat_hap_hi       = robotState.cfg_snd_cat_hap_hi;
    out->audio.snd_cat_proc_lo      = robotState.cfg_snd_cat_proc_lo;
    out->audio.snd_cat_proc_hi      = robotState.cfg_snd_cat_proc_hi;
    out->audio.snd_cat_sad_lo       = robotState.cfg_snd_cat_sad_lo;
    out->audio.snd_cat_sad_hi       = robotState.cfg_snd_cat_sad_hi;
    out->audio.snd_cat_sent_lo      = robotState.cfg_snd_cat_sent_lo;
    out->audio.snd_cat_sent_hi      = robotState.cfg_snd_cat_sent_hi;
    out->audio.snd_cat_hum_lo       = robotState.cfg_snd_cat_hum_lo;
    out->audio.snd_cat_hum_hi       = robotState.cfg_snd_cat_hum_hi;
    out->audio.snd_cat_scrm_lo      = robotState.cfg_snd_cat_scrm_lo;
    out->audio.snd_cat_scrm_hi      = robotState.cfg_snd_cat_scrm_hi;
    out->audio.snd_cat_ooh_lo       = robotState.cfg_snd_cat_ooh_lo;
    out->audio.snd_cat_ooh_hi       = robotState.cfg_snd_cat_ooh_hi;
    out->audio.snd_cat_alrm_lo      = robotState.cfg_snd_cat_alrm_lo;
    out->audio.snd_cat_alrm_hi      = robotState.cfg_snd_cat_alrm_hi;
    out->audio.snd_cat_snarky_lo    = robotState.cfg_snd_cat_snarky_lo;
    out->audio.snd_cat_snarky_hi    = robotState.cfg_snd_cat_snarky_hi;
    out->audio.snd_cat_whis_lo      = robotState.cfg_snd_cat_whis_lo;
    out->audio.snd_cat_whis_hi      = robotState.cfg_snd_cat_whis_hi;
    out->servo.arm1_open_us         = robotState.cfg_arm1_open_us;
    out->servo.arm1_close_us        = robotState.cfg_arm1_close_us;
    out->servo.arm2_open_us         = robotState.cfg_arm2_open_us;
    out->servo.arm2_close_us        = robotState.cfg_arm2_close_us;
    out->servo.arm1_type            = robotState.cfg_arm1_type;
    out->servo.arm2_type            = robotState.cfg_arm2_type;
    out->servo.aux1_open_us         = robotState.cfg_aux1_open_us;
    out->servo.aux1_close_us        = robotState.cfg_aux1_close_us;
    out->servo.aux2_open_us         = robotState.cfg_aux2_open_us;
    out->servo.aux2_close_us        = robotState.cfg_aux2_close_us;
    out->servo.aux3_open_us         = robotState.cfg_aux3_open_us;
    out->servo.aux3_close_us        = robotState.cfg_aux3_close_us;
    out->servo.aux1_type            = robotState.cfg_aux1_type;
    out->servo.aux2_type            = robotState.cfg_aux2_type;
    out->servo.aux3_type            = robotState.cfg_aux3_type;
    out->dome.dome_min_speed       = robotState.cfg_dome_min_speed;
    out->dome.dome_max_speed       = robotState.cfg_dome_max_speed;
    out->dome.dome_neutral_us      = robotState.cfg_dome_neutral_us;
    out->dome.dome_min_pulse_us    = robotState.cfg_dome_min_pulse_us;
    out->dome.dome_max_pulse_us    = robotState.cfg_dome_max_pulse_us;
    out->dome.dome_speed_limit_pct = robotState.cfg_dome_speed_limit_pct;
    out->dome.dome_rnd_enable      = robotState.cfg_dome_rnd_enable;
    out->dome.dome_rnd_speed_pct   = robotState.cfg_dome_rnd_speed_pct;
    out->dome.dome_rnd_pause_min   = robotState.cfg_dome_rnd_pause_min;
    out->dome.dome_rnd_pause_max   = robotState.cfg_dome_rnd_pause_max;
    out->dome.dome_rnd_move_ms     = robotState.cfg_dome_rnd_move_ms;
    snprintf(out->dome.dome_wifi_peer_ip, sizeof(out->dome.dome_wifi_peer_ip), "%s",
             robotState.cfg_dome_wifi_peer_ip);
    out->servo.seq_open_ms          = robotState.cfg_seq_open_ms;
    out->servo.seq_close_ms         = robotState.cfg_seq_close_ms;
    out->servo.aux_led_pin          = robotState.cfg_aux_led_pin;
    out->servo.aux_led_count        = robotState.cfg_aux_led_count;
    out->system.enable_arm1          = robotState.cfg_enable_arm1;
    out->system.enable_arm2          = robotState.cfg_enable_arm2;
    out->system.enable_aux1          = robotState.cfg_enable_aux1;
    out->system.enable_aux2          = robotState.cfg_enable_aux2;
    out->system.enable_aux3          = robotState.cfg_enable_aux3;
    out->system.enable_dome          = robotState.cfg_enable_dome;
    out->system.enable_rc_ch1        = robotState.cfg_enable_rc_ch1;
    out->system.enable_rc_ch2        = robotState.cfg_enable_rc_ch2;
    out->system.enable_rc_ch3        = robotState.cfg_enable_rc_ch3;
    out->system.enable_rc_ch4        = robotState.cfg_enable_rc_ch4;
    out->system.enable_rc_ch5        = robotState.cfg_enable_rc_ch5;
    out->system.enable_rc_ch6        = robotState.cfg_enable_rc_ch6;
    out->system.single_sbus_use_ch2  = robotState.cfg_single_sbus_use_ch2;
    out->system.enable_s1_hoverboard = robotState.cfg_enable_s1_hoverboard;
    out->system.enable_s2_sound      = robotState.cfg_enable_s2_sound;
    out->system.enable_s3_dome_ctrl  = robotState.cfg_enable_s3_dome_ctrl;
    out->system.stationary           = robotState.cfg_stationary;
    out->system.rc_input_mode        = robotState.cfg_rc_input_mode;
    out->system.rc_pwm_drive_speed   = robotState.cfg_rc_pwm_drive_speed;
    out->system.rc_pwm_drive_steer   = robotState.cfg_rc_pwm_drive_steer;
    out->system.rc_pwm_dome_speed    = robotState.cfg_rc_pwm_dome_speed;
    out->system.rc_pwm_arm1          = robotState.cfg_rc_pwm_arm1;
    out->system.rc_pwm_arm2          = robotState.cfg_rc_pwm_arm2;
    out->system.rc_pwm_sound         = robotState.cfg_rc_pwm_sound;
    out->system.rc_sbus_drive_speed  = robotState.cfg_rc_sbus_drive_speed;
    out->system.rc_sbus_drive_steer  = robotState.cfg_rc_sbus_drive_steer;
    out->system.rc_sbus_dome_speed   = robotState.cfg_rc_sbus_dome_speed;
    out->system.rc_sbus_arm1         = robotState.cfg_rc_sbus_arm1;
    out->system.rc_sbus_arm2         = robotState.cfg_rc_sbus_arm2;
    out->system.rc_sbus_sound        = robotState.cfg_rc_sbus_sound;
    out->system.rc_arm1              = robotState.cfg_rc_arm1;
    out->system.rc_arm2              = robotState.cfg_rc_arm2;
    out->system.rc_aux1              = robotState.cfg_rc_aux1;
    out->system.rc_aux2              = robotState.cfg_rc_aux2;
    out->system.rc_aux3              = robotState.cfg_rc_aux3;
    out->system.rc_sound             = robotState.cfg_rc_sound;
    out->system.rc_opmode            = robotState.cfg_rc_opmode;
    out->system.rc_free0             = robotState.cfg_rc_free0;
    out->system.rc_free1             = robotState.cfg_rc_free1;
    out->system.rc_free2             = robotState.cfg_rc_free2;
    out->system.rc_free3             = robotState.cfg_rc_free3;
}

void configApplyToRobotState(const ConfigSnapshot& snap) {
    robotState.cfg_speedLimitMax = snap.drive.speedLimitMax;
    robotState.cfg_speedPresetSlow = snap.drive.speedPresetSlow;
    robotState.cfg_speedPresetNormal = snap.drive.speedPresetNormal;
    robotState.cfg_speedPresetTurbo = snap.drive.speedPresetTurbo;
    robotState.cfg_speedPresetActive = snap.drive.speedPresetActive;
    robotState.cfg_sbusTimeoutMs = snap.drive.sbusTimeoutMs;
    robotState.cfg_webDriveTimeoutMs = snap.drive.webDriveTimeoutMs;
    robotState.cfg_audioVolume = snap.audio.audioVolume;
    robotState.cfg_logLevel = snap.system.logLevel;
    robotState.cfg_snd_scream = snap.audio.snd_scream;
    robotState.cfg_snd_faint = snap.audio.snd_faint;
    robotState.cfg_snd_leia = snap.audio.snd_leia;
    robotState.cfg_snd_cantina_s = snap.audio.snd_cantina_s;
    robotState.cfg_snd_sw_theme = snap.audio.snd_sw_theme;
    robotState.cfg_snd_imp_march = snap.audio.snd_imp_march;
    robotState.cfg_snd_cantina_l = snap.audio.snd_cantina_l;
    robotState.cfg_snd_startup = snap.audio.snd_startup;
    robotState.cfg_snd_doodoo = snap.audio.snd_doodoo;
    robotState.cfg_snd_failure = snap.audio.snd_failure;
    robotState.cfg_snd_disco = snap.audio.snd_disco;
    robotState.cfg_snd_mahna = snap.audio.snd_mahna;
    robotState.cfg_snd_inlove = snap.audio.snd_inlove;
    robotState.cfg_snd_macho = snap.audio.snd_macho;
    robotState.cfg_snd_gangnam = snap.audio.snd_gangnam;
    robotState.cfg_snd_uptown = snap.audio.snd_uptown;
    robotState.cfg_snd_celebr = snap.audio.snd_celebr;
    robotState.cfg_snd_stayin = snap.audio.snd_stayin;
    robotState.cfg_snd_harlem = snap.audio.snd_harlem;
    robotState.cfg_snd_pbjtime = snap.audio.snd_pbjtime;
    robotState.cfg_snd_sys_boot = snap.audio.snd_sys_boot;
    robotState.cfg_snd_sys_mode_n = snap.audio.snd_sys_mode_n;
    robotState.cfg_snd_sys_mode_s = snap.audio.snd_sys_mode_s;
    robotState.cfg_snd_sys_mode_t = snap.audio.snd_sys_mode_t;
    robotState.cfg_snd_sys_drv_on = snap.audio.snd_sys_drv_on;
    robotState.cfg_snd_sys_dome_on = snap.audio.snd_sys_dome_on;
    robotState.cfg_snd_rand_min = snap.audio.snd_rand_min;
    robotState.cfg_snd_rand_max = snap.audio.snd_rand_max;
    robotState.cfg_snd_int_quiet = snap.audio.snd_int_quiet;
    robotState.cfg_snd_int_mid = snap.audio.snd_int_mid;
    robotState.cfg_snd_int_full = snap.audio.snd_int_full;
    robotState.cfg_snd_int_awake = snap.audio.snd_int_awake;
    robotState.cfg_snd_moodcat_quiet = snap.audio.snd_moodcat_quiet;
    robotState.cfg_snd_moodcat_mid = snap.audio.snd_moodcat_mid;
    robotState.cfg_snd_moodcat_full = snap.audio.snd_moodcat_full;
    robotState.cfg_snd_moodcat_awakeplus = snap.audio.snd_moodcat_awakeplus;
    robotState.cfg_snd_cat_gen_lo = snap.audio.snd_cat_gen_lo;
    robotState.cfg_snd_cat_gen_hi = snap.audio.snd_cat_gen_hi;
    robotState.cfg_snd_cat_chat_lo = snap.audio.snd_cat_chat_lo;
    robotState.cfg_snd_cat_chat_hi = snap.audio.snd_cat_chat_hi;
    robotState.cfg_snd_cat_hap_lo = snap.audio.snd_cat_hap_lo;
    robotState.cfg_snd_cat_hap_hi = snap.audio.snd_cat_hap_hi;
    robotState.cfg_snd_cat_proc_lo = snap.audio.snd_cat_proc_lo;
    robotState.cfg_snd_cat_proc_hi = snap.audio.snd_cat_proc_hi;
    robotState.cfg_snd_cat_sad_lo = snap.audio.snd_cat_sad_lo;
    robotState.cfg_snd_cat_sad_hi = snap.audio.snd_cat_sad_hi;
    robotState.cfg_snd_cat_sent_lo = snap.audio.snd_cat_sent_lo;
    robotState.cfg_snd_cat_sent_hi = snap.audio.snd_cat_sent_hi;
    robotState.cfg_snd_cat_hum_lo = snap.audio.snd_cat_hum_lo;
    robotState.cfg_snd_cat_hum_hi = snap.audio.snd_cat_hum_hi;
    robotState.cfg_snd_cat_scrm_lo = snap.audio.snd_cat_scrm_lo;
    robotState.cfg_snd_cat_scrm_hi = snap.audio.snd_cat_scrm_hi;
    robotState.cfg_snd_cat_ooh_lo = snap.audio.snd_cat_ooh_lo;
    robotState.cfg_snd_cat_ooh_hi = snap.audio.snd_cat_ooh_hi;
    robotState.cfg_snd_cat_alrm_lo = snap.audio.snd_cat_alrm_lo;
    robotState.cfg_snd_cat_alrm_hi = snap.audio.snd_cat_alrm_hi;
    robotState.cfg_snd_cat_snarky_lo = snap.audio.snd_cat_snarky_lo;
    robotState.cfg_snd_cat_snarky_hi = snap.audio.snd_cat_snarky_hi;
    robotState.cfg_snd_cat_whis_lo = snap.audio.snd_cat_whis_lo;
    robotState.cfg_snd_cat_whis_hi = snap.audio.snd_cat_whis_hi;
    robotState.cfg_arm1_open_us = snap.servo.arm1_open_us;
    robotState.cfg_arm1_close_us = snap.servo.arm1_close_us;
    robotState.cfg_arm2_open_us = snap.servo.arm2_open_us;
    robotState.cfg_arm2_close_us = snap.servo.arm2_close_us;
    robotState.cfg_arm1_type = snap.servo.arm1_type;
    robotState.cfg_arm2_type = snap.servo.arm2_type;
    robotState.cfg_aux1_open_us = snap.servo.aux1_open_us;
    robotState.cfg_aux1_close_us = snap.servo.aux1_close_us;
    robotState.cfg_aux2_open_us = snap.servo.aux2_open_us;
    robotState.cfg_aux2_close_us = snap.servo.aux2_close_us;
    robotState.cfg_aux3_open_us = snap.servo.aux3_open_us;
    robotState.cfg_aux3_close_us = snap.servo.aux3_close_us;
    robotState.cfg_aux1_type = snap.servo.aux1_type;
    robotState.cfg_aux2_type = snap.servo.aux2_type;
    robotState.cfg_aux3_type = snap.servo.aux3_type;
    robotState.cfg_dome_min_speed = snap.dome.dome_min_speed;
    robotState.cfg_dome_max_speed = snap.dome.dome_max_speed;
    robotState.cfg_seq_open_ms = snap.servo.seq_open_ms;
    robotState.cfg_seq_close_ms = snap.servo.seq_close_ms;
    robotState.cfg_dome_neutral_us = snap.dome.dome_neutral_us;
    robotState.cfg_dome_min_pulse_us = snap.dome.dome_min_pulse_us;
    robotState.cfg_dome_max_pulse_us = snap.dome.dome_max_pulse_us;
    robotState.cfg_dome_speed_limit_pct = snap.dome.dome_speed_limit_pct;
    robotState.cfg_dome_rnd_enable = snap.dome.dome_rnd_enable;
    robotState.cfg_dome_rnd_speed_pct = snap.dome.dome_rnd_speed_pct;
    robotState.cfg_dome_rnd_pause_min = snap.dome.dome_rnd_pause_min;
    robotState.cfg_dome_rnd_pause_max = snap.dome.dome_rnd_pause_max;
    robotState.cfg_dome_rnd_move_ms = snap.dome.dome_rnd_move_ms;
    robotState.cfg_rc_input_mode = snap.system.rc_input_mode;
    robotState.cfg_enable_arm1 = snap.system.enable_arm1;
    robotState.cfg_enable_arm2 = snap.system.enable_arm2;
    robotState.cfg_enable_aux1 = snap.system.enable_aux1;
    robotState.cfg_enable_aux2 = snap.system.enable_aux2;
    robotState.cfg_enable_aux3 = snap.system.enable_aux3;
    robotState.cfg_enable_dome = snap.system.enable_dome;
    robotState.cfg_enable_rc_ch1 = snap.system.enable_rc_ch1;
    robotState.cfg_enable_rc_ch2 = snap.system.enable_rc_ch2;
    robotState.cfg_enable_rc_ch3 = snap.system.enable_rc_ch3;
    robotState.cfg_enable_rc_ch4 = snap.system.enable_rc_ch4;
    robotState.cfg_enable_rc_ch5 = snap.system.enable_rc_ch5;
    robotState.cfg_enable_rc_ch6 = snap.system.enable_rc_ch6;
    robotState.cfg_single_sbus_use_ch2 = snap.system.single_sbus_use_ch2;
    robotState.cfg_enable_s1_hoverboard = snap.system.enable_s1_hoverboard;
    robotState.cfg_enable_s2_sound = snap.system.enable_s2_sound;
    robotState.cfg_enable_s3_dome_ctrl = snap.system.enable_s3_dome_ctrl;
    snprintf(robotState.cfg_dome_wifi_peer_ip, sizeof(robotState.cfg_dome_wifi_peer_ip), "%s",
             snap.dome.dome_wifi_peer_ip);
    robotState.cfg_stationary = snap.system.stationary;
    robotState.cfg_aux_led_pin = snap.servo.aux_led_pin;
    robotState.cfg_aux_led_count = snap.servo.aux_led_count;
    robotState.cfg_rc_pwm_drive_speed = snap.system.rc_pwm_drive_speed;
    robotState.cfg_rc_pwm_drive_steer = snap.system.rc_pwm_drive_steer;
    robotState.cfg_rc_pwm_dome_speed = snap.system.rc_pwm_dome_speed;
    robotState.cfg_rc_pwm_arm1 = snap.system.rc_pwm_arm1;
    robotState.cfg_rc_pwm_arm2 = snap.system.rc_pwm_arm2;
    robotState.cfg_rc_pwm_sound = snap.system.rc_pwm_sound;
    robotState.cfg_rc_sbus_drive_speed = snap.system.rc_sbus_drive_speed;
    robotState.cfg_rc_sbus_drive_steer = snap.system.rc_sbus_drive_steer;
    robotState.cfg_rc_sbus_dome_speed = snap.system.rc_sbus_dome_speed;
    robotState.cfg_rc_sbus_arm1 = snap.system.rc_sbus_arm1;
    robotState.cfg_rc_sbus_arm2 = snap.system.rc_sbus_arm2;
    robotState.cfg_rc_sbus_sound = snap.system.rc_sbus_sound;
    robotState.cfg_rc_arm1 = snap.system.rc_arm1;
    robotState.cfg_rc_arm2 = snap.system.rc_arm2;
    robotState.cfg_rc_aux1 = snap.system.rc_aux1;
    robotState.cfg_rc_aux2 = snap.system.rc_aux2;
    robotState.cfg_rc_aux3 = snap.system.rc_aux3;
    robotState.cfg_rc_sound = snap.system.rc_sound;
    robotState.cfg_rc_opmode = snap.system.rc_opmode;
    robotState.cfg_rc_free0 = snap.system.rc_free0;
    robotState.cfg_rc_free1 = snap.system.rc_free1;
    robotState.cfg_rc_free2 = snap.system.rc_free2;
    robotState.cfg_rc_free3 = snap.system.rc_free3;
}

bool configLoad(Preferences& prefs, ConfigSnapshot* out) {
    if (out == nullptr) {
        return false;
    }

    configSnapshotDefaults(out);

    uint8_t storedVersion = prefs.getUChar(CONFIG_SCHEMA_VERSION_KEY, 0);
    bool isLegacy = (storedVersion == 0);
    bool schemaMismatch = (!isLegacy && storedVersion != CONFIG_SCHEMA_VERSION);

    if (schemaMismatch) {
        PA_LOG_WARN("config", "schema version mismatch: stored=%u current=%u; using defaults",
                    (unsigned)storedVersion, (unsigned)CONFIG_SCHEMA_VERSION);
        // Fill with defaults, which was already done above
        // Write new schema version
        prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION);
        return false;
    }

    if (isLegacy) {
        PA_LOG_INFO("config", "legacy config detected (version 0); loading and stamping as v1");
    }

    configLoadDrive(prefs, &out->drive);
    configLoadAudio(prefs, &out->audio);
    configLoadServo(prefs, &out->servo);
    configLoadDome(prefs, &out->dome);
    configLoadSystem(prefs, &out->system);

    if (isLegacy) {
        prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION);
    }

    return true;
}

void configLoadDrive(Preferences& prefs, DriveConfig* out) {
    if (out == nullptr) {
        return;
    }
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    *out = snap.drive;

    out->speedLimitMax = prefs.getShort(NVS_KEYS.speedLimitMax, SPEED_LIMIT_MAX);
    out->speedPresetSlow = prefs.getShort(NVS_KEYS.speedPresetSlow, SPEED_PRESET_SLOW);
    out->speedPresetNormal = prefs.getShort(NVS_KEYS.speedPresetNormal, SPEED_PRESET_NORMAL);
    out->speedPresetTurbo = prefs.getShort(NVS_KEYS.speedPresetTurbo, SPEED_PRESET_TURBO);
    out->speedPresetActive =
        normalizeSpeedPresetId(prefs.getUChar(NVS_KEYS.speedPresetActive, (uint8_t)SpeedPresetId::Normal));
    out->sbusTimeoutMs = prefs.getULong(NVS_KEYS.sbusTimeoutMs, SBUS_TIMEOUT_MS);
    out->webDriveTimeoutMs = prefs.getULong(NVS_KEYS.webDriveTimeoutMs, WEB_DRIVE_TIMEOUT_MS);

    out->speedLimitMax = constrain(out->speedLimitMax, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->speedPresetSlow = constrain(out->speedPresetSlow, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->speedPresetNormal = constrain(out->speedPresetNormal, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->speedPresetTurbo = constrain(out->speedPresetTurbo, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->sbusTimeoutMs = constrain(out->sbusTimeoutMs, (uint32_t)50, (uint32_t)5000);
    out->webDriveTimeoutMs = constrain(out->webDriveTimeoutMs, (uint32_t)100, (uint32_t)5000);
}

void configLoadAudio(Preferences& prefs, AudioConfig* out) {
    if (out == nullptr) {
        return;
    }
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    *out = snap.audio;

    out->audioVolume = prefs.getUChar(NVS_KEYS.audioVolume, 20);
    out->snd_scream = prefs.getUShort(NVS_KEYS.sndScream, AUDIO_TRACK_SCREAM);
    out->snd_faint = prefs.getUShort(NVS_KEYS.sndFaint, AUDIO_TRACK_FAINT);
    out->snd_leia = prefs.getUShort(NVS_KEYS.sndLeia, AUDIO_TRACK_LEIA);
    out->snd_cantina_s = prefs.getUShort(NVS_KEYS.sndCantinaS, AUDIO_TRACK_CANTINA_S);
    out->snd_sw_theme = prefs.getUShort(NVS_KEYS.sndSwTheme, AUDIO_TRACK_SW_THEME);
    out->snd_imp_march = prefs.getUShort(NVS_KEYS.sndImpMarch, AUDIO_TRACK_IMP_MARCH);
    out->snd_cantina_l = prefs.getUShort(NVS_KEYS.sndCantinaL, AUDIO_TRACK_CANTINA_L);
    out->snd_startup = prefs.getUShort(NVS_KEYS.sndStartup, AUDIO_TRACK_STARTUP);
    out->snd_doodoo = prefs.getUShort(NVS_KEYS.sndDoodoo, 0);
    out->snd_failure = prefs.getUShort(NVS_KEYS.sndFailure, 0);
    out->snd_disco = prefs.getUShort(NVS_KEYS.sndDisco, 0);
    out->snd_mahna = prefs.getUShort(NVS_KEYS.sndMahna, 0);
    out->snd_inlove = prefs.getUShort(NVS_KEYS.sndInlove, 0);
    out->snd_macho = prefs.getUShort(NVS_KEYS.sndMacho, 0);
    out->snd_gangnam = prefs.getUShort(NVS_KEYS.sndGangnam, 0);
    out->snd_uptown = prefs.getUShort(NVS_KEYS.sndUptown, 0);
    out->snd_celebr = prefs.getUShort(NVS_KEYS.sndCelebr, 0);
    out->snd_stayin = prefs.getUShort(NVS_KEYS.sndStayin, 0);
    out->snd_harlem = prefs.getUShort(NVS_KEYS.sndHarlem, 0);
    out->snd_pbjtime = prefs.getUShort(NVS_KEYS.sndPbjtime, 0);
    out->snd_sys_boot = prefs.getUShort(NVS_KEYS.sndSysBoot, 0);
    out->snd_sys_mode_n = prefs.getUShort(NVS_KEYS.sndSysModeN, 0);
    out->snd_sys_mode_s = prefs.getUShort(NVS_KEYS.sndSysModeS, 0);
    out->snd_sys_mode_t = prefs.getUShort(NVS_KEYS.sndSysModeT, 0);
    out->snd_sys_drv_on = prefs.getUShort(NVS_KEYS.sndSysDrvOn, 0);
    out->snd_sys_dome_on = prefs.getUShort(NVS_KEYS.sndSysDomeOn, 0);
    out->snd_rand_min = prefs.getUShort(NVS_KEYS.sndRandMin, AUDIO_RAND_TRACK_MIN);
    out->snd_rand_max = prefs.getUShort(NVS_KEYS.sndRandMax, AUDIO_RAND_TRACK_MAX);
    out->snd_int_quiet = prefs.getUShort(NVS_KEYS.sndIntQuiet, AUDIO_RAND_INT_QUIET);
    out->snd_int_mid = prefs.getUShort(NVS_KEYS.sndIntMid, AUDIO_RAND_INT_MID);
    out->snd_int_full = prefs.getUShort(NVS_KEYS.sndIntFull, AUDIO_RAND_INT_FULL);
    out->snd_int_awake = prefs.getUShort(NVS_KEYS.sndIntAwake, AUDIO_RAND_INT_AWAKE);
    out->snd_moodcat_quiet = prefs.getUShort(NVS_KEYS.sndMoodcatQuiet, 0x0048);
    out->snd_moodcat_mid = prefs.getUShort(NVS_KEYS.sndMoodcatMid, 0x004F);
    out->snd_moodcat_full = prefs.getUShort(NVS_KEYS.sndMoodcatFull, 0x090F);
    out->snd_moodcat_awakeplus = prefs.getUShort(NVS_KEYS.sndMoodcatAwakeplus, 0x0F8F);
    out->snd_cat_gen_lo = prefs.getUShort(NVS_KEYS.sndCatGenLo, 0);
    out->snd_cat_gen_hi = prefs.getUShort(NVS_KEYS.sndCatGenHi, 0);
    out->snd_cat_chat_lo = prefs.getUShort(NVS_KEYS.sndCatChatLo, 0);
    out->snd_cat_chat_hi = prefs.getUShort(NVS_KEYS.sndCatChatHi, 0);
    out->snd_cat_hap_lo = prefs.getUShort(NVS_KEYS.sndCatHapLo, 0);
    out->snd_cat_hap_hi = prefs.getUShort(NVS_KEYS.sndCatHapHi, 0);
    out->snd_cat_proc_lo = prefs.getUShort(NVS_KEYS.sndCatProcLo, 0);
    out->snd_cat_proc_hi = prefs.getUShort(NVS_KEYS.sndCatProcHi, 0);
    out->snd_cat_sad_lo = prefs.getUShort(NVS_KEYS.sndCatSadLo, 0);
    out->snd_cat_sad_hi = prefs.getUShort(NVS_KEYS.sndCatSadHi, 0);
    out->snd_cat_sent_lo = prefs.getUShort(NVS_KEYS.sndCatSentLo, 0);
    out->snd_cat_sent_hi = prefs.getUShort(NVS_KEYS.sndCatSentHi, 0);
    out->snd_cat_hum_lo = prefs.getUShort(NVS_KEYS.sndCatHumLo, 0);
    out->snd_cat_hum_hi = prefs.getUShort(NVS_KEYS.sndCatHumHi, 0);
    out->snd_cat_scrm_lo = prefs.getUShort(NVS_KEYS.sndCatScrmLo, 0);
    out->snd_cat_scrm_hi = prefs.getUShort(NVS_KEYS.sndCatScrmHi, 0);
    out->snd_cat_ooh_lo = prefs.getUShort(NVS_KEYS.sndCatOohLo, 0);
    out->snd_cat_ooh_hi = prefs.getUShort(NVS_KEYS.sndCatOohHi, 0);
    out->snd_cat_alrm_lo = prefs.getUShort(NVS_KEYS.sndCatAlrmLo, 0);
    out->snd_cat_alrm_hi = prefs.getUShort(NVS_KEYS.sndCatAlrmHi, 0);
    out->snd_cat_snarky_lo = prefs.getUShort(NVS_KEYS.sndCatSnarkyLo, 0);
    out->snd_cat_snarky_hi = prefs.getUShort(NVS_KEYS.sndCatSnarkyHi, 0);
    out->snd_cat_whis_lo = prefs.getUShort(NVS_KEYS.sndCatWhisLo, 0);
    out->snd_cat_whis_hi = prefs.getUShort(NVS_KEYS.sndCatWhisHi, 0);

    out->audioVolume = constrain(out->audioVolume, (uint8_t)0, (uint8_t)30);
}

void configLoadServo(Preferences& prefs, ServoConfig* out) {
    if (out == nullptr) {
        return;
    }
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    *out = snap.servo;

    out->arm1_open_us = prefs.getUShort(NVS_KEYS.arm1OpenUs, 2000);
    out->arm1_close_us = prefs.getUShort(NVS_KEYS.arm1CloseUs, 1000);
    out->arm2_open_us = prefs.getUShort(NVS_KEYS.arm2OpenUs, 2000);
    out->arm2_close_us = prefs.getUShort(NVS_KEYS.arm2CloseUs, 1000);
    out->arm1_type = (ServoComponentType)prefs.getUChar(NVS_KEYS.arm1Type, SERVO_COMP_MG996R);
    out->arm2_type = (ServoComponentType)prefs.getUChar(NVS_KEYS.arm2Type, SERVO_COMP_MG996R);
    out->aux1_open_us = prefs.getUShort(NVS_KEYS.aux1OpenUs, 2000);
    out->aux1_close_us = prefs.getUShort(NVS_KEYS.aux1CloseUs, 1000);
    out->aux2_open_us = prefs.getUShort(NVS_KEYS.aux2OpenUs, 2000);
    out->aux2_close_us = prefs.getUShort(NVS_KEYS.aux2CloseUs, 1000);
    out->aux3_open_us = prefs.getUShort(NVS_KEYS.aux3OpenUs, 2000);
    out->aux3_close_us = prefs.getUShort(NVS_KEYS.aux3CloseUs, 1000);
    out->aux1_type = (ServoComponentType)prefs.getUChar(NVS_KEYS.aux1Type, SERVO_COMP_NONE);
    out->aux2_type = (ServoComponentType)prefs.getUChar(NVS_KEYS.aux2Type, SERVO_COMP_NONE);
    out->aux3_type = (ServoComponentType)prefs.getUChar(NVS_KEYS.aux3Type, SERVO_COMP_NONE);
    out->seq_open_ms = prefs.getUShort(NVS_KEYS.seqOpenMs, 1000);
    out->seq_close_ms = prefs.getUShort(NVS_KEYS.seqCloseMs, 1000);
    out->aux_led_pin = prefs.getUChar(NVS_KEYS.auxLedPin, AUX_LED_PIN_DISABLED);
    out->aux_led_count = prefs.getUChar(NVS_KEYS.auxLedCount, AUX_LED_COUNT_DEFAULT);

    out->arm1_open_us = constrain(out->arm1_open_us, (uint16_t)500, (uint16_t)2500);
    out->arm1_close_us = constrain(out->arm1_close_us, (uint16_t)500, (uint16_t)2500);
    out->arm2_open_us = constrain(out->arm2_open_us, (uint16_t)500, (uint16_t)2500);
    out->arm2_close_us = constrain(out->arm2_close_us, (uint16_t)500, (uint16_t)2500);
    out->aux1_open_us = constrain(out->aux1_open_us, (uint16_t)500, (uint16_t)2500);
    out->aux1_close_us = constrain(out->aux1_close_us, (uint16_t)500, (uint16_t)2500);
    out->aux2_open_us = constrain(out->aux2_open_us, (uint16_t)500, (uint16_t)2500);
    out->aux2_close_us = constrain(out->aux2_close_us, (uint16_t)500, (uint16_t)2500);
    out->aux3_open_us = constrain(out->aux3_open_us, (uint16_t)500, (uint16_t)2500);
    out->aux3_close_us = constrain(out->aux3_close_us, (uint16_t)500, (uint16_t)2500);

    if (out->arm1_type > SERVO_COMP_RGB)
        out->arm1_type = SERVO_COMP_MG996R;
    if (out->arm2_type > SERVO_COMP_RGB)
        out->arm2_type = SERVO_COMP_MG996R;
    if (out->aux1_type > SERVO_COMP_RGB)
        out->aux1_type = SERVO_COMP_NONE;
    if (out->aux2_type > SERVO_COMP_RGB)
        out->aux2_type = SERVO_COMP_NONE;
    if (out->aux3_type > SERVO_COMP_RGB)
        out->aux3_type = SERVO_COMP_NONE;

    if (out->seq_open_ms < 100)
        out->seq_open_ms = 100;
    if (out->seq_open_ms > 5000)
        out->seq_open_ms = 5000;
    if (out->seq_close_ms < 100)
        out->seq_close_ms = 100;
    if (out->seq_close_ms > 5000)
        out->seq_close_ms = 5000;

    if (!auxLedPinSettingValid(out->aux_led_pin)) {
        out->aux_led_pin = AUX_LED_PIN_DISABLED;
    }
    out->aux_led_count = constrain(out->aux_led_count, AUX_LED_COUNT_DEFAULT, AUX_LED_COUNT_MAX);
}

void configLoadDome(Preferences& prefs, DomeConfig* out) {
    if (out == nullptr) {
        return;
    }
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    *out = snap.dome;

    out->dome_min_speed = floatFromBits(prefs.getULong(NVS_KEYS.domeMin, 0));
    out->dome_max_speed = floatFromBits(prefs.getULong(NVS_KEYS.domeMax, 0x3F800000));

    out->dome_neutral_us = prefs.getUShort(NVS_KEYS.domeNeutralUs, 1500);
    out->dome_min_pulse_us = prefs.getUShort(NVS_KEYS.domeMinPulseUs, 1000);
    out->dome_max_pulse_us = prefs.getUShort(NVS_KEYS.domeMaxPulseUs, 2000);
    out->dome_speed_limit_pct = prefs.getUChar(NVS_KEYS.domeSpeedLimitPct, 100);
    out->dome_rnd_enable = prefs.getBool(NVS_KEYS.domeRndEnable, false);
    out->dome_rnd_speed_pct = prefs.getUChar(NVS_KEYS.domeRndSpeedPct, 30);
    out->dome_rnd_pause_min = prefs.getUChar(NVS_KEYS.domeRndPauseMin, 6);
    out->dome_rnd_pause_max = prefs.getUChar(NVS_KEYS.domeRndPauseMax, 12);
    out->dome_rnd_move_ms = prefs.getUShort(NVS_KEYS.domeRndMoveMs, 2500);

    String domeWifiPeerIp = prefs.getString(NVS_KEYS.domeWifiPeerIp, "");
    if (domeWifiPeerIp.length() >= sizeof(out->dome_wifi_peer_ip)) {
        domeWifiPeerIp = "";
    }
    snprintf(out->dome_wifi_peer_ip, sizeof(out->dome_wifi_peer_ip), "%s", domeWifiPeerIp.c_str());

    if (out->dome_min_speed < 0.0f)
        out->dome_min_speed = 0.0f;
    if (out->dome_max_speed > 1.0f)
        out->dome_max_speed = 1.0f;
    out->dome_neutral_us = constrain(out->dome_neutral_us, (uint16_t)1000, (uint16_t)2000);
    out->dome_min_pulse_us = constrain(out->dome_min_pulse_us, (uint16_t)1000, (uint16_t)2000);
    out->dome_max_pulse_us = constrain(out->dome_max_pulse_us, (uint16_t)1000, (uint16_t)2000);
    out->dome_speed_limit_pct = constrain(out->dome_speed_limit_pct, (uint8_t)0, (uint8_t)100);

#ifdef ARDUINO
    if (out->dome_wifi_peer_ip[0] != '\0') {
        IPAddress parsedPeerIp;
        if (!parsedPeerIp.fromString(out->dome_wifi_peer_ip)) {
            out->dome_wifi_peer_ip[0] = '\0';
        }
    }
#endif
}

void configLoadSystem(Preferences& prefs, SystemConfig* out) {
    if (out == nullptr) {
        return;
    }
    ConfigSnapshot snap = {};
    configSnapshotDefaults(&snap);
    *out = snap.system;

    out->logLevel = prefs.getUChar(NVS_KEYS.logLevel, PA_LOG_LEVEL);
    out->enable_arm1 = prefs.getBool(NVS_KEYS.enableArm1, false);
    out->enable_arm2 = prefs.getBool(NVS_KEYS.enableArm2, false);
    out->enable_aux1 = prefs.getBool(NVS_KEYS.enableAux1, false);
    out->enable_aux2 = prefs.getBool(NVS_KEYS.enableAux2, false);
    out->enable_aux3 = prefs.getBool(NVS_KEYS.enableAux3, false);
    out->enable_dome = prefs.getBool(NVS_KEYS.enableDome, false);
    out->enable_rc_ch1 = prefs.getBool(NVS_KEYS.enableRcCh1, false);
    out->enable_rc_ch2 = prefs.getBool(NVS_KEYS.enableRcCh2, false);
    out->enable_rc_ch3 = prefs.getBool(NVS_KEYS.enableRcCh3, false);
    out->enable_rc_ch4 = prefs.getBool(NVS_KEYS.enableRcCh4, false);
    out->enable_rc_ch5 = prefs.getBool(NVS_KEYS.enableRcCh5, false);
    out->enable_rc_ch6 = prefs.getBool(NVS_KEYS.enableRcCh6, false);
    out->single_sbus_use_ch2 = prefs.getBool(NVS_KEYS.singleSbusUseCh2, false);
    out->enable_s1_hoverboard = prefs.getBool(NVS_KEYS.enableS1Hoverboard, false);
    out->enable_s2_sound = prefs.getBool(NVS_KEYS.enableS2Sound, false);
    out->enable_s3_dome_ctrl = prefs.getBool(NVS_KEYS.enableS3DomeCtrl, false);
    out->stationary = prefs.getBool(NVS_KEYS.stationary, false);
    out->rc_input_mode = (RcInputMode)prefs.getUChar(NVS_KEYS.rcInputMode, RC_INPUT_DUAL_SBUS);

    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcPwmDriveSpeed, defaultPwmBinding(1), &out->rc_pwm_drive_speed);
    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcPwmDriveSteer, defaultPwmBinding(2), &out->rc_pwm_drive_steer);
    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcPwmDomeSpeed, defaultPwmBinding(3), &out->rc_pwm_dome_speed);
    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcPwmArm1, defaultPwmBinding(4), &out->rc_pwm_arm1);
    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcPwmArm2, defaultPwmBinding(5), &out->rc_pwm_arm2);
    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcPwmSound, defaultPwmBinding(6), &out->rc_pwm_sound);

    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcSbusDriveSpeed, defaultSbusBinding(RC_BINDING_SBUS1, 1),
                           &out->rc_sbus_drive_speed);
    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcSbusDriveSteer, defaultSbusBinding(RC_BINDING_SBUS1, 2),
                           &out->rc_sbus_drive_steer);
    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcSbusDomeSpeed, defaultSbusBinding(RC_BINDING_SBUS2, 1),
                           &out->rc_sbus_dome_speed);
    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcSbusArm1, defaultSbusBinding(RC_BINDING_SBUS2, 2), &out->rc_sbus_arm1);
    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcSbusArm2, defaultSbusBinding(RC_BINDING_SBUS2, 3), &out->rc_sbus_arm2);
    loadRcBindingFromPrefs(prefs, NVS_KEYS.rcSbusSound, disabledRcBinding(), &out->rc_sbus_sound);

    RcTriggerBinding arm1Default = makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                                                        RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                                                        RC_SBUS_DEFAULT_MAX, 0, rcTriggerDefaultReverse(RC_BINDING_SBUS1, 4));
    RcTriggerBinding arm2Default = makeRcTriggerBinding(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM2_TOGGLE, nullptr,
                                                        RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                                                        RC_SBUS_DEFAULT_MAX, 0, rcTriggerDefaultReverse(RC_BINDING_SBUS1, 5));
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcArm1, arm1Default, &out->rc_arm1);
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcArm2, arm2Default, &out->rc_arm2);
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcAux1, disabledRcTriggerBinding(), &out->rc_aux1);
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcAux2, disabledRcTriggerBinding(), &out->rc_aux2);
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcAux3, disabledRcTriggerBinding(), &out->rc_aux3);
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcSound, disabledRcTriggerBinding(), &out->rc_sound);
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcOpmode, disabledRcTriggerBinding(), &out->rc_opmode);
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcFree0, disabledRcTriggerBinding(), &out->rc_free0);
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcFree1, disabledRcTriggerBinding(), &out->rc_free1);
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcFree2, disabledRcTriggerBinding(), &out->rc_free2);
    loadRcTriggerBindingFromPrefs(prefs, NVS_KEYS.rcFree3, disabledRcTriggerBinding(), &out->rc_free3);

    if (out->rc_input_mode > RC_INPUT_DUAL_SBUS) {
        out->rc_input_mode = RC_INPUT_DUAL_SBUS;
    }

    RcBindingConfig* bindings[] = {
        &out->rc_pwm_drive_speed,  &out->rc_pwm_drive_steer,
        &out->rc_pwm_dome_speed,   &out->rc_pwm_arm1,
        &out->rc_pwm_arm2,         &out->rc_pwm_sound,
        &out->rc_sbus_drive_speed, &out->rc_sbus_drive_steer,
        &out->rc_sbus_dome_speed,  &out->rc_sbus_arm1,
        &out->rc_sbus_arm2,        &out->rc_sbus_sound,
    };
    const RcBindingConfig defaults[] = {
        defaultPwmBinding(1),                    defaultPwmBinding(2),
        defaultPwmBinding(3),                    defaultPwmBinding(4),
        defaultPwmBinding(5),                    defaultPwmBinding(6),
        defaultSbusBinding(RC_BINDING_SBUS1, 1), defaultSbusBinding(RC_BINDING_SBUS1, 2),
        defaultSbusBinding(RC_BINDING_SBUS2, 1), defaultSbusBinding(RC_BINDING_SBUS2, 2),
        defaultSbusBinding(RC_BINDING_SBUS2, 3), disabledRcBinding(),
    };
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
        if (!rcBindingIsValid(*bindings[i])) {
            *bindings[i] = defaults[i];
        }
    }

    RcTriggerBinding* triggerBindings[] = {
        &out->rc_arm1,  &out->rc_arm2,  &out->rc_aux1,  &out->rc_aux2,  &out->rc_aux3, &out->rc_sound,
        &out->rc_opmode, &out->rc_free0, &out->rc_free1, &out->rc_free2, &out->rc_free3,
    };
    const RcTriggerBinding triggerDefaults[] = {
        makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                             RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX, 0,
                             rcTriggerDefaultReverse(RC_BINDING_SBUS1, 4)),
        makeRcTriggerBinding(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM2_TOGGLE, nullptr,
                             RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX, 0,
                             rcTriggerDefaultReverse(RC_BINDING_SBUS1, 5)),
        disabledRcTriggerBinding(), disabledRcTriggerBinding(), disabledRcTriggerBinding(),
        disabledRcTriggerBinding(), disabledRcTriggerBinding(), disabledRcTriggerBinding(),
        disabledRcTriggerBinding(), disabledRcTriggerBinding(), disabledRcTriggerBinding(),
    };
    for (size_t i = 0; i < sizeof(triggerBindings) / sizeof(triggerBindings[0]); ++i) {
        if (!rcTriggerBindingIsValid(*triggerBindings[i])) {
            *triggerBindings[i] = triggerDefaults[i];
        }
    }
}

bool configSave(Preferences& prefs, const ConfigSnapshot& snapshot) {
    bool ok = true;
    ok = configSaveDrive(prefs, snapshot.drive) && ok;
    ok = configSaveAudio(prefs, snapshot.audio) && ok;
    ok = configSaveServo(prefs, snapshot.servo) && ok;
    ok = configSaveDome(prefs, snapshot.dome) && ok;
    ok = configSaveSystem(prefs, snapshot.system) && ok;
    ok = prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION) > 0 && ok;
    return ok;
}

bool configSaveDrive(Preferences& prefs, const DriveConfig& config) {
    bool ok = true;
    ok = prefs.putShort(NVS_KEYS.speedLimitMax, config.speedLimitMax) > 0 && ok;
    ok = prefs.putShort(NVS_KEYS.speedPresetSlow, config.speedPresetSlow) > 0 && ok;
    ok = prefs.putShort(NVS_KEYS.speedPresetNormal, config.speedPresetNormal) > 0 && ok;
    ok = prefs.putShort(NVS_KEYS.speedPresetTurbo, config.speedPresetTurbo) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.speedPresetActive, (uint8_t)config.speedPresetActive) > 0 && ok;
    ok = prefs.putULong(NVS_KEYS.sbusTimeoutMs, config.sbusTimeoutMs) > 0 && ok;
    ok = prefs.putULong(NVS_KEYS.webDriveTimeoutMs, config.webDriveTimeoutMs) > 0 && ok;
    ok = prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION) > 0 && ok;
    return ok;
}

bool configSaveAudio(Preferences& prefs, const AudioConfig& config) {
    bool ok = true;
    ok = prefs.putUChar(NVS_KEYS.audioVolume, config.audioVolume) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndScream, config.snd_scream) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndFaint, config.snd_faint) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndLeia, config.snd_leia) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCantinaS, config.snd_cantina_s) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSwTheme, config.snd_sw_theme) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndImpMarch, config.snd_imp_march) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCantinaL, config.snd_cantina_l) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndStartup, config.snd_startup) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndDoodoo, config.snd_doodoo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndFailure, config.snd_failure) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndDisco, config.snd_disco) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMahna, config.snd_mahna) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndInlove, config.snd_inlove) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMacho, config.snd_macho) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndGangnam, config.snd_gangnam) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndUptown, config.snd_uptown) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCelebr, config.snd_celebr) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndStayin, config.snd_stayin) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndHarlem, config.snd_harlem) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndPbjtime, config.snd_pbjtime) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysBoot, config.snd_sys_boot) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysModeN, config.snd_sys_mode_n) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysModeS, config.snd_sys_mode_s) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysModeT, config.snd_sys_mode_t) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysDrvOn, config.snd_sys_drv_on) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysDomeOn, config.snd_sys_dome_on) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndRandMin, config.snd_rand_min) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndRandMax, config.snd_rand_max) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndIntQuiet, config.snd_int_quiet) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndIntMid, config.snd_int_mid) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndIntFull, config.snd_int_full) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndIntAwake, config.snd_int_awake) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMoodcatQuiet, config.snd_moodcat_quiet & 0x0FFF) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMoodcatMid, config.snd_moodcat_mid & 0x0FFF) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMoodcatFull, config.snd_moodcat_full & 0x0FFF) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMoodcatAwakeplus, config.snd_moodcat_awakeplus & 0x0FFF) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatGenLo, config.snd_cat_gen_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatGenHi, config.snd_cat_gen_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatChatLo, config.snd_cat_chat_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatChatHi, config.snd_cat_chat_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatHapLo, config.snd_cat_hap_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatHapHi, config.snd_cat_hap_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatProcLo, config.snd_cat_proc_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatProcHi, config.snd_cat_proc_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSadLo, config.snd_cat_sad_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSadHi, config.snd_cat_sad_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSentLo, config.snd_cat_sent_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSentHi, config.snd_cat_sent_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatHumLo, config.snd_cat_hum_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatHumHi, config.snd_cat_hum_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatScrmLo, config.snd_cat_scrm_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatScrmHi, config.snd_cat_scrm_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatOohLo, config.snd_cat_ooh_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatOohHi, config.snd_cat_ooh_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatAlrmLo, config.snd_cat_alrm_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatAlrmHi, config.snd_cat_alrm_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSnarkyLo, config.snd_cat_snarky_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSnarkyHi, config.snd_cat_snarky_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatWhisLo, config.snd_cat_whis_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatWhisHi, config.snd_cat_whis_hi) > 0 && ok;
    ok = prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION) > 0 && ok;
    return ok;
}

bool configSaveServo(Preferences& prefs, const ServoConfig& config) {
    bool ok = true;
    ok = prefs.putUShort(NVS_KEYS.arm1OpenUs, config.arm1_open_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.arm1CloseUs, config.arm1_close_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.arm2OpenUs, config.arm2_open_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.arm2CloseUs, config.arm2_close_us) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.arm1Type, (uint8_t)config.arm1_type) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.arm2Type, (uint8_t)config.arm2_type) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux1OpenUs, config.aux1_open_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux1CloseUs, config.aux1_close_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux2OpenUs, config.aux2_open_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux2CloseUs, config.aux2_close_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux3OpenUs, config.aux3_open_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux3CloseUs, config.aux3_close_us) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.aux1Type, (uint8_t)config.aux1_type) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.aux2Type, (uint8_t)config.aux2_type) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.aux3Type, (uint8_t)config.aux3_type) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.seqOpenMs, config.seq_open_ms) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.seqCloseMs, config.seq_close_ms) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.auxLedPin, config.aux_led_pin) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.auxLedCount, config.aux_led_count) > 0 && ok;
    ok = prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION) > 0 && ok;
    return ok;
}

bool configSaveDome(Preferences& prefs, const DomeConfig& config) {
    bool ok = true;
    ok = prefs.putULong(NVS_KEYS.domeMin, floatToBits(config.dome_min_speed)) > 0 && ok;
    ok = prefs.putULong(NVS_KEYS.domeMax, floatToBits(config.dome_max_speed)) > 0 && ok;

    ok = prefs.putUShort(NVS_KEYS.domeNeutralUs, config.dome_neutral_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.domeMinPulseUs, config.dome_min_pulse_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.domeMaxPulseUs, config.dome_max_pulse_us) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.domeSpeedLimitPct, config.dome_speed_limit_pct) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.domeRndEnable, config.dome_rnd_enable) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.domeRndSpeedPct, config.dome_rnd_speed_pct) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.domeRndPauseMin, config.dome_rnd_pause_min) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.domeRndPauseMax, config.dome_rnd_pause_max) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.domeRndMoveMs, config.dome_rnd_move_ms) > 0 && ok;

    if (config.dome_wifi_peer_ip[0] == '\0') {
        prefs.remove(NVS_KEYS.domeWifiPeerIp);
    } else {
        ok = prefs.putString(NVS_KEYS.domeWifiPeerIp, config.dome_wifi_peer_ip) > 0 && ok;
    }
    ok = prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION) > 0 && ok;
    return ok;
}

bool configSaveSystem(Preferences& prefs, const SystemConfig& config) {
    bool ok = true;
    ok = prefs.putUChar(NVS_KEYS.logLevel, config.logLevel) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableArm1, config.enable_arm1) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableArm2, config.enable_arm2) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableAux1, config.enable_aux1) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableAux2, config.enable_aux2) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableAux3, config.enable_aux3) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableDome, config.enable_dome) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh1, config.enable_rc_ch1) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh2, config.enable_rc_ch2) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh3, config.enable_rc_ch3) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh4, config.enable_rc_ch4) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh5, config.enable_rc_ch5) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh6, config.enable_rc_ch6) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.singleSbusUseCh2, config.single_sbus_use_ch2) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableS1Hoverboard, config.enable_s1_hoverboard) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableS2Sound, config.enable_s2_sound) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableS3DomeCtrl, config.enable_s3_dome_ctrl) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.stationary, config.stationary) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.rcInputMode, (uint8_t)config.rc_input_mode) > 0 && ok;

    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmDriveSpeed, config.rc_pwm_drive_speed) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmDriveSteer, config.rc_pwm_drive_steer) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmDomeSpeed, config.rc_pwm_dome_speed) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmArm1, config.rc_pwm_arm1) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmArm2, config.rc_pwm_arm2) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmSound, config.rc_pwm_sound) && ok;

    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusDriveSpeed, config.rc_sbus_drive_speed) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusDriveSteer, config.rc_sbus_drive_steer) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusDomeSpeed, config.rc_sbus_dome_speed) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusArm1, config.rc_sbus_arm1) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusArm2, config.rc_sbus_arm2) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusSound, config.rc_sbus_sound) && ok;

    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcArm1, config.rc_arm1) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcArm2, config.rc_arm2) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcAux1, config.rc_aux1) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcAux2, config.rc_aux2) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcAux3, config.rc_aux3) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcSound, config.rc_sound) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcOpmode, config.rc_opmode) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcFree0, config.rc_free0) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcFree1, config.rc_free1) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcFree2, config.rc_free2) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcFree3, config.rc_free3) && ok;

    ok = prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION) > 0 && ok;
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
