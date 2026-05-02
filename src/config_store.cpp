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

// Helper: Populate ConfigSnapshot with defaults
void configSnapshotDefaults(ConfigSnapshot* snap) {
    snap->speedLimitMax = SPEED_LIMIT_MAX;
    snap->speedPresetSlow = SPEED_PRESET_SLOW;
    snap->speedPresetNormal = SPEED_PRESET_NORMAL;
    snap->speedPresetTurbo = SPEED_PRESET_TURBO;
    snap->speedPresetActive = SpeedPresetId::Normal;
    snap->sbusTimeoutMs = SBUS_TIMEOUT_MS;
    snap->webDriveTimeoutMs = WEB_DRIVE_TIMEOUT_MS;
    snap->audioVolume = 20;
    snap->logLevel = PA_LOG_LEVEL;
    snap->snd_scream = AUDIO_TRACK_SCREAM;
    snap->snd_faint = AUDIO_TRACK_FAINT;
    snap->snd_leia = AUDIO_TRACK_LEIA;
    snap->snd_cantina_s = AUDIO_TRACK_CANTINA_S;
    snap->snd_sw_theme = AUDIO_TRACK_SW_THEME;
    snap->snd_imp_march = AUDIO_TRACK_IMP_MARCH;
    snap->snd_cantina_l = AUDIO_TRACK_CANTINA_L;
    snap->snd_startup = AUDIO_TRACK_STARTUP;
    snap->snd_doodoo = 0;
    snap->snd_failure = 0;
    snap->snd_disco = 0;
    snap->snd_mahna = 0;
    snap->snd_inlove = 0;
    snap->snd_macho = 0;
    snap->snd_gangnam = 0;
    snap->snd_uptown = 0;
    snap->snd_celebr = 0;
    snap->snd_stayin = 0;
    snap->snd_harlem = 0;
    snap->snd_pbjtime = 0;
    snap->snd_sys_boot = 0;
    snap->snd_sys_mode_n = 0;
    snap->snd_sys_mode_s = 0;
    snap->snd_sys_mode_t = 0;
    snap->snd_sys_drv_on = 0;
    snap->snd_sys_dome_on = 0;
    snap->snd_rand_min = AUDIO_RAND_TRACK_MIN;
    snap->snd_rand_max = AUDIO_RAND_TRACK_MAX;
    snap->snd_int_quiet = AUDIO_RAND_INT_QUIET;
    snap->snd_int_mid = AUDIO_RAND_INT_MID;
    snap->snd_int_full = AUDIO_RAND_INT_FULL;
    snap->snd_int_awake = AUDIO_RAND_INT_AWAKE;
    snap->snd_moodcat_quiet = 0x0048;
    snap->snd_moodcat_mid = 0x004F;
    snap->snd_moodcat_full = 0x090F;
    snap->snd_moodcat_awakeplus = 0x0F8F;
    snap->snd_cat_gen_lo = 0;
    snap->snd_cat_gen_hi = 0;
    snap->snd_cat_chat_lo = 0;
    snap->snd_cat_chat_hi = 0;
    snap->snd_cat_hap_lo = 0;
    snap->snd_cat_hap_hi = 0;
    snap->snd_cat_proc_lo = 0;
    snap->snd_cat_proc_hi = 0;
    snap->snd_cat_sad_lo = 0;
    snap->snd_cat_sad_hi = 0;
    snap->snd_cat_sent_lo = 0;
    snap->snd_cat_sent_hi = 0;
    snap->snd_cat_hum_lo = 0;
    snap->snd_cat_hum_hi = 0;
    snap->snd_cat_scrm_lo = 0;
    snap->snd_cat_scrm_hi = 0;
    snap->snd_cat_ooh_lo = 0;
    snap->snd_cat_ooh_hi = 0;
    snap->snd_cat_alrm_lo = 0;
    snap->snd_cat_alrm_hi = 0;
    snap->snd_cat_snarky_lo = 0;
    snap->snd_cat_snarky_hi = 0;
    snap->snd_cat_whis_lo = 0;
    snap->snd_cat_whis_hi = 0;

    snap->arm1_open_us = 2000;
    snap->arm1_close_us = 1000;
    snap->arm2_open_us = 2000;
    snap->arm2_close_us = 1000;
    snap->arm1_type = SERVO_COMP_MG996R;
    snap->arm2_type = SERVO_COMP_MG996R;
    snap->aux1_open_us = 2000;
    snap->aux1_close_us = 1000;
    snap->aux2_open_us = 2000;
    snap->aux2_close_us = 1000;
    snap->aux3_open_us = 2000;
    snap->aux3_close_us = 1000;
    snap->aux1_type = SERVO_COMP_NONE;
    snap->aux2_type = SERVO_COMP_NONE;
    snap->aux3_type = SERVO_COMP_NONE;

    snap->dome_min_speed = 0.0f;
    snap->dome_max_speed = 1.0f;
    snap->dome_neutral_us = 1500;
    snap->dome_min_pulse_us = 1000;
    snap->dome_max_pulse_us = 2000;
    snap->dome_speed_limit_pct = 100;
    snap->dome_rnd_enable = false;
    snap->dome_rnd_speed_pct = 30;
    snap->dome_rnd_pause_min = 6;
    snap->dome_rnd_pause_max = 12;
    snap->dome_rnd_move_ms = 2500;
    snap->dome_wifi_peer_ip[0] = '\0';

    snap->seq_open_ms = 1000;
    snap->seq_close_ms = 1000;

    snap->aux_led_pin = AUX_LED_PIN_DISABLED;
    snap->aux_led_count = AUX_LED_COUNT_DEFAULT;

    snap->enable_arm1 = false;
    snap->enable_arm2 = false;
    snap->enable_aux1 = false;
    snap->enable_aux2 = false;
    snap->enable_aux3 = false;
    snap->enable_dome = false;
    snap->enable_rc_ch1 = false;
    snap->enable_rc_ch2 = false;
    snap->enable_rc_ch3 = false;
    snap->enable_rc_ch4 = false;
    snap->enable_rc_ch5 = false;
    snap->enable_rc_ch6 = false;
    snap->single_sbus_use_ch2 = false;
    snap->enable_s1_hoverboard = false;
    snap->enable_s2_sound = false;
    snap->enable_s3_dome_ctrl = false;
    snap->stationary = false;
    snap->rc_input_mode = RC_INPUT_DUAL_SBUS;

    snap->rc_pwm_drive_speed = defaultPwmBinding(1);
    snap->rc_pwm_drive_steer = defaultPwmBinding(2);
    snap->rc_pwm_dome_speed = defaultPwmBinding(3);
    snap->rc_pwm_arm1 = defaultPwmBinding(4);
    snap->rc_pwm_arm2 = defaultPwmBinding(5);
    snap->rc_pwm_sound = defaultPwmBinding(6);

    snap->rc_sbus_drive_speed = defaultSbusBinding(RC_BINDING_SBUS1, 1);
    snap->rc_sbus_drive_steer = defaultSbusBinding(RC_BINDING_SBUS1, 2);
    snap->rc_sbus_dome_speed = defaultSbusBinding(RC_BINDING_SBUS2, 1);
    snap->rc_sbus_arm1 = defaultSbusBinding(RC_BINDING_SBUS2, 2);
    snap->rc_sbus_arm2 = defaultSbusBinding(RC_BINDING_SBUS2, 3);
    snap->rc_sbus_sound = disabledRcBinding();

    snap->rc_arm1 = makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                                         RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                                         RC_SBUS_DEFAULT_MAX, 0,
                                         rcTriggerDefaultReverse(RC_BINDING_SBUS1, 4));
    snap->rc_arm2 = makeRcTriggerBinding(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM2_TOGGLE, nullptr,
                                         RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER,
                                         RC_SBUS_DEFAULT_MAX, 0,
                                         rcTriggerDefaultReverse(RC_BINDING_SBUS1, 5));
    snap->rc_aux1 = disabledRcTriggerBinding();
    snap->rc_aux2 = disabledRcTriggerBinding();
    snap->rc_aux3 = disabledRcTriggerBinding();
    snap->rc_sound = disabledRcTriggerBinding();
    snap->rc_opmode = disabledRcTriggerBinding();
    snap->rc_free0 = disabledRcTriggerBinding();
    snap->rc_free1 = disabledRcTriggerBinding();
    snap->rc_free2 = disabledRcTriggerBinding();
    snap->rc_free3 = disabledRcTriggerBinding();
}

}  // namespace

// =============================================================================
// Public API Implementation
// =============================================================================

void configSnapshotFromRobotState(ConfigSnapshot* out) {
    out->speedLimitMax        = robotState.cfg_speedLimitMax;
    out->speedPresetSlow      = robotState.cfg_speedPresetSlow;
    out->speedPresetNormal    = robotState.cfg_speedPresetNormal;
    out->speedPresetTurbo     = robotState.cfg_speedPresetTurbo;
    out->speedPresetActive    = robotState.cfg_speedPresetActive;
    out->sbusTimeoutMs        = robotState.cfg_sbusTimeoutMs;
    out->webDriveTimeoutMs    = robotState.cfg_webDriveTimeoutMs;
    out->audioVolume          = robotState.cfg_audioVolume;
    out->logLevel             = robotState.cfg_logLevel;
    out->snd_scream           = robotState.cfg_snd_scream;
    out->snd_faint            = robotState.cfg_snd_faint;
    out->snd_leia             = robotState.cfg_snd_leia;
    out->snd_cantina_s        = robotState.cfg_snd_cantina_s;
    out->snd_sw_theme         = robotState.cfg_snd_sw_theme;
    out->snd_imp_march        = robotState.cfg_snd_imp_march;
    out->snd_cantina_l        = robotState.cfg_snd_cantina_l;
    out->snd_startup          = robotState.cfg_snd_startup;
    out->snd_doodoo           = robotState.cfg_snd_doodoo;
    out->snd_failure          = robotState.cfg_snd_failure;
    out->snd_disco            = robotState.cfg_snd_disco;
    out->snd_mahna            = robotState.cfg_snd_mahna;
    out->snd_inlove           = robotState.cfg_snd_inlove;
    out->snd_macho            = robotState.cfg_snd_macho;
    out->snd_gangnam          = robotState.cfg_snd_gangnam;
    out->snd_uptown           = robotState.cfg_snd_uptown;
    out->snd_celebr           = robotState.cfg_snd_celebr;
    out->snd_stayin           = robotState.cfg_snd_stayin;
    out->snd_harlem           = robotState.cfg_snd_harlem;
    out->snd_pbjtime          = robotState.cfg_snd_pbjtime;
    out->snd_sys_boot         = robotState.cfg_snd_sys_boot;
    out->snd_sys_mode_n       = robotState.cfg_snd_sys_mode_n;
    out->snd_sys_mode_s       = robotState.cfg_snd_sys_mode_s;
    out->snd_sys_mode_t       = robotState.cfg_snd_sys_mode_t;
    out->snd_sys_drv_on       = robotState.cfg_snd_sys_drv_on;
    out->snd_sys_dome_on      = robotState.cfg_snd_sys_dome_on;
    out->snd_rand_min         = robotState.cfg_snd_rand_min;
    out->snd_rand_max         = robotState.cfg_snd_rand_max;
    out->snd_int_quiet        = robotState.cfg_snd_int_quiet;
    out->snd_int_mid          = robotState.cfg_snd_int_mid;
    out->snd_int_full         = robotState.cfg_snd_int_full;
    out->snd_int_awake        = robotState.cfg_snd_int_awake;
    out->snd_moodcat_quiet    = robotState.cfg_snd_moodcat_quiet;
    out->snd_moodcat_mid      = robotState.cfg_snd_moodcat_mid;
    out->snd_moodcat_full     = robotState.cfg_snd_moodcat_full;
    out->snd_moodcat_awakeplus = robotState.cfg_snd_moodcat_awakeplus;
    out->snd_cat_gen_lo       = robotState.cfg_snd_cat_gen_lo;
    out->snd_cat_gen_hi       = robotState.cfg_snd_cat_gen_hi;
    out->snd_cat_chat_lo      = robotState.cfg_snd_cat_chat_lo;
    out->snd_cat_chat_hi      = robotState.cfg_snd_cat_chat_hi;
    out->snd_cat_hap_lo       = robotState.cfg_snd_cat_hap_lo;
    out->snd_cat_hap_hi       = robotState.cfg_snd_cat_hap_hi;
    out->snd_cat_proc_lo      = robotState.cfg_snd_cat_proc_lo;
    out->snd_cat_proc_hi      = robotState.cfg_snd_cat_proc_hi;
    out->snd_cat_sad_lo       = robotState.cfg_snd_cat_sad_lo;
    out->snd_cat_sad_hi       = robotState.cfg_snd_cat_sad_hi;
    out->snd_cat_sent_lo      = robotState.cfg_snd_cat_sent_lo;
    out->snd_cat_sent_hi      = robotState.cfg_snd_cat_sent_hi;
    out->snd_cat_hum_lo       = robotState.cfg_snd_cat_hum_lo;
    out->snd_cat_hum_hi       = robotState.cfg_snd_cat_hum_hi;
    out->snd_cat_scrm_lo      = robotState.cfg_snd_cat_scrm_lo;
    out->snd_cat_scrm_hi      = robotState.cfg_snd_cat_scrm_hi;
    out->snd_cat_ooh_lo       = robotState.cfg_snd_cat_ooh_lo;
    out->snd_cat_ooh_hi       = robotState.cfg_snd_cat_ooh_hi;
    out->snd_cat_alrm_lo      = robotState.cfg_snd_cat_alrm_lo;
    out->snd_cat_alrm_hi      = robotState.cfg_snd_cat_alrm_hi;
    out->snd_cat_snarky_lo    = robotState.cfg_snd_cat_snarky_lo;
    out->snd_cat_snarky_hi    = robotState.cfg_snd_cat_snarky_hi;
    out->snd_cat_whis_lo      = robotState.cfg_snd_cat_whis_lo;
    out->snd_cat_whis_hi      = robotState.cfg_snd_cat_whis_hi;
    out->arm1_open_us         = robotState.cfg_arm1_open_us;
    out->arm1_close_us        = robotState.cfg_arm1_close_us;
    out->arm2_open_us         = robotState.cfg_arm2_open_us;
    out->arm2_close_us        = robotState.cfg_arm2_close_us;
    out->arm1_type            = robotState.cfg_arm1_type;
    out->arm2_type            = robotState.cfg_arm2_type;
    out->aux1_open_us         = robotState.cfg_aux1_open_us;
    out->aux1_close_us        = robotState.cfg_aux1_close_us;
    out->aux2_open_us         = robotState.cfg_aux2_open_us;
    out->aux2_close_us        = robotState.cfg_aux2_close_us;
    out->aux3_open_us         = robotState.cfg_aux3_open_us;
    out->aux3_close_us        = robotState.cfg_aux3_close_us;
    out->aux1_type            = robotState.cfg_aux1_type;
    out->aux2_type            = robotState.cfg_aux2_type;
    out->aux3_type            = robotState.cfg_aux3_type;
    out->dome_min_speed       = robotState.cfg_dome_min_speed;
    out->dome_max_speed       = robotState.cfg_dome_max_speed;
    out->dome_neutral_us      = robotState.cfg_dome_neutral_us;
    out->dome_min_pulse_us    = robotState.cfg_dome_min_pulse_us;
    out->dome_max_pulse_us    = robotState.cfg_dome_max_pulse_us;
    out->dome_speed_limit_pct = robotState.cfg_dome_speed_limit_pct;
    out->dome_rnd_enable      = robotState.cfg_dome_rnd_enable;
    out->dome_rnd_speed_pct   = robotState.cfg_dome_rnd_speed_pct;
    out->dome_rnd_pause_min   = robotState.cfg_dome_rnd_pause_min;
    out->dome_rnd_pause_max   = robotState.cfg_dome_rnd_pause_max;
    out->dome_rnd_move_ms     = robotState.cfg_dome_rnd_move_ms;
    snprintf(out->dome_wifi_peer_ip, sizeof(out->dome_wifi_peer_ip), "%s",
             robotState.cfg_dome_wifi_peer_ip);
    out->seq_open_ms          = robotState.cfg_seq_open_ms;
    out->seq_close_ms         = robotState.cfg_seq_close_ms;
    out->aux_led_pin          = robotState.cfg_aux_led_pin;
    out->aux_led_count        = robotState.cfg_aux_led_count;
    out->enable_arm1          = robotState.cfg_enable_arm1;
    out->enable_arm2          = robotState.cfg_enable_arm2;
    out->enable_aux1          = robotState.cfg_enable_aux1;
    out->enable_aux2          = robotState.cfg_enable_aux2;
    out->enable_aux3          = robotState.cfg_enable_aux3;
    out->enable_dome          = robotState.cfg_enable_dome;
    out->enable_rc_ch1        = robotState.cfg_enable_rc_ch1;
    out->enable_rc_ch2        = robotState.cfg_enable_rc_ch2;
    out->enable_rc_ch3        = robotState.cfg_enable_rc_ch3;
    out->enable_rc_ch4        = robotState.cfg_enable_rc_ch4;
    out->enable_rc_ch5        = robotState.cfg_enable_rc_ch5;
    out->enable_rc_ch6        = robotState.cfg_enable_rc_ch6;
    out->single_sbus_use_ch2  = robotState.cfg_single_sbus_use_ch2;
    out->enable_s1_hoverboard = robotState.cfg_enable_s1_hoverboard;
    out->enable_s2_sound      = robotState.cfg_enable_s2_sound;
    out->enable_s3_dome_ctrl  = robotState.cfg_enable_s3_dome_ctrl;
    out->stationary           = robotState.cfg_stationary;
    out->rc_input_mode        = robotState.cfg_rc_input_mode;
    out->rc_pwm_drive_speed   = robotState.cfg_rc_pwm_drive_speed;
    out->rc_pwm_drive_steer   = robotState.cfg_rc_pwm_drive_steer;
    out->rc_pwm_dome_speed    = robotState.cfg_rc_pwm_dome_speed;
    out->rc_pwm_arm1          = robotState.cfg_rc_pwm_arm1;
    out->rc_pwm_arm2          = robotState.cfg_rc_pwm_arm2;
    out->rc_pwm_sound         = robotState.cfg_rc_pwm_sound;
    out->rc_sbus_drive_speed  = robotState.cfg_rc_sbus_drive_speed;
    out->rc_sbus_drive_steer  = robotState.cfg_rc_sbus_drive_steer;
    out->rc_sbus_dome_speed   = robotState.cfg_rc_sbus_dome_speed;
    out->rc_sbus_arm1         = robotState.cfg_rc_sbus_arm1;
    out->rc_sbus_arm2         = robotState.cfg_rc_sbus_arm2;
    out->rc_sbus_sound        = robotState.cfg_rc_sbus_sound;
    out->rc_arm1              = robotState.cfg_rc_arm1;
    out->rc_arm2              = robotState.cfg_rc_arm2;
    out->rc_aux1              = robotState.cfg_rc_aux1;
    out->rc_aux2              = robotState.cfg_rc_aux2;
    out->rc_aux3              = robotState.cfg_rc_aux3;
    out->rc_sound             = robotState.cfg_rc_sound;
    out->rc_opmode            = robotState.cfg_rc_opmode;
    out->rc_free0             = robotState.cfg_rc_free0;
    out->rc_free1             = robotState.cfg_rc_free1;
    out->rc_free2             = robotState.cfg_rc_free2;
    out->rc_free3             = robotState.cfg_rc_free3;
}

void configApplyToRobotState(const ConfigSnapshot& snap) {
    robotState.cfg_speedLimitMax = snap.speedLimitMax;
    robotState.cfg_speedPresetSlow = snap.speedPresetSlow;
    robotState.cfg_speedPresetNormal = snap.speedPresetNormal;
    robotState.cfg_speedPresetTurbo = snap.speedPresetTurbo;
    robotState.cfg_speedPresetActive = snap.speedPresetActive;
    robotState.cfg_sbusTimeoutMs = snap.sbusTimeoutMs;
    robotState.cfg_webDriveTimeoutMs = snap.webDriveTimeoutMs;
    robotState.cfg_audioVolume = snap.audioVolume;
    robotState.cfg_logLevel = snap.logLevel;
    robotState.cfg_snd_scream = snap.snd_scream;
    robotState.cfg_snd_faint = snap.snd_faint;
    robotState.cfg_snd_leia = snap.snd_leia;
    robotState.cfg_snd_cantina_s = snap.snd_cantina_s;
    robotState.cfg_snd_sw_theme = snap.snd_sw_theme;
    robotState.cfg_snd_imp_march = snap.snd_imp_march;
    robotState.cfg_snd_cantina_l = snap.snd_cantina_l;
    robotState.cfg_snd_startup = snap.snd_startup;
    robotState.cfg_snd_doodoo = snap.snd_doodoo;
    robotState.cfg_snd_failure = snap.snd_failure;
    robotState.cfg_snd_disco = snap.snd_disco;
    robotState.cfg_snd_mahna = snap.snd_mahna;
    robotState.cfg_snd_inlove = snap.snd_inlove;
    robotState.cfg_snd_macho = snap.snd_macho;
    robotState.cfg_snd_gangnam = snap.snd_gangnam;
    robotState.cfg_snd_uptown = snap.snd_uptown;
    robotState.cfg_snd_celebr = snap.snd_celebr;
    robotState.cfg_snd_stayin = snap.snd_stayin;
    robotState.cfg_snd_harlem = snap.snd_harlem;
    robotState.cfg_snd_pbjtime = snap.snd_pbjtime;
    robotState.cfg_snd_sys_boot = snap.snd_sys_boot;
    robotState.cfg_snd_sys_mode_n = snap.snd_sys_mode_n;
    robotState.cfg_snd_sys_mode_s = snap.snd_sys_mode_s;
    robotState.cfg_snd_sys_mode_t = snap.snd_sys_mode_t;
    robotState.cfg_snd_sys_drv_on = snap.snd_sys_drv_on;
    robotState.cfg_snd_sys_dome_on = snap.snd_sys_dome_on;
    robotState.cfg_snd_rand_min = snap.snd_rand_min;
    robotState.cfg_snd_rand_max = snap.snd_rand_max;
    robotState.cfg_snd_int_quiet = snap.snd_int_quiet;
    robotState.cfg_snd_int_mid = snap.snd_int_mid;
    robotState.cfg_snd_int_full = snap.snd_int_full;
    robotState.cfg_snd_int_awake = snap.snd_int_awake;
    robotState.cfg_snd_moodcat_quiet = snap.snd_moodcat_quiet;
    robotState.cfg_snd_moodcat_mid = snap.snd_moodcat_mid;
    robotState.cfg_snd_moodcat_full = snap.snd_moodcat_full;
    robotState.cfg_snd_moodcat_awakeplus = snap.snd_moodcat_awakeplus;
    robotState.cfg_snd_cat_gen_lo = snap.snd_cat_gen_lo;
    robotState.cfg_snd_cat_gen_hi = snap.snd_cat_gen_hi;
    robotState.cfg_snd_cat_chat_lo = snap.snd_cat_chat_lo;
    robotState.cfg_snd_cat_chat_hi = snap.snd_cat_chat_hi;
    robotState.cfg_snd_cat_hap_lo = snap.snd_cat_hap_lo;
    robotState.cfg_snd_cat_hap_hi = snap.snd_cat_hap_hi;
    robotState.cfg_snd_cat_proc_lo = snap.snd_cat_proc_lo;
    robotState.cfg_snd_cat_proc_hi = snap.snd_cat_proc_hi;
    robotState.cfg_snd_cat_sad_lo = snap.snd_cat_sad_lo;
    robotState.cfg_snd_cat_sad_hi = snap.snd_cat_sad_hi;
    robotState.cfg_snd_cat_sent_lo = snap.snd_cat_sent_lo;
    robotState.cfg_snd_cat_sent_hi = snap.snd_cat_sent_hi;
    robotState.cfg_snd_cat_hum_lo = snap.snd_cat_hum_lo;
    robotState.cfg_snd_cat_hum_hi = snap.snd_cat_hum_hi;
    robotState.cfg_snd_cat_scrm_lo = snap.snd_cat_scrm_lo;
    robotState.cfg_snd_cat_scrm_hi = snap.snd_cat_scrm_hi;
    robotState.cfg_snd_cat_ooh_lo = snap.snd_cat_ooh_lo;
    robotState.cfg_snd_cat_ooh_hi = snap.snd_cat_ooh_hi;
    robotState.cfg_snd_cat_alrm_lo = snap.snd_cat_alrm_lo;
    robotState.cfg_snd_cat_alrm_hi = snap.snd_cat_alrm_hi;
    robotState.cfg_snd_cat_snarky_lo = snap.snd_cat_snarky_lo;
    robotState.cfg_snd_cat_snarky_hi = snap.snd_cat_snarky_hi;
    robotState.cfg_snd_cat_whis_lo = snap.snd_cat_whis_lo;
    robotState.cfg_snd_cat_whis_hi = snap.snd_cat_whis_hi;
    robotState.cfg_arm1_open_us = snap.arm1_open_us;
    robotState.cfg_arm1_close_us = snap.arm1_close_us;
    robotState.cfg_arm2_open_us = snap.arm2_open_us;
    robotState.cfg_arm2_close_us = snap.arm2_close_us;
    robotState.cfg_arm1_type = snap.arm1_type;
    robotState.cfg_arm2_type = snap.arm2_type;
    robotState.cfg_aux1_open_us = snap.aux1_open_us;
    robotState.cfg_aux1_close_us = snap.aux1_close_us;
    robotState.cfg_aux2_open_us = snap.aux2_open_us;
    robotState.cfg_aux2_close_us = snap.aux2_close_us;
    robotState.cfg_aux3_open_us = snap.aux3_open_us;
    robotState.cfg_aux3_close_us = snap.aux3_close_us;
    robotState.cfg_aux1_type = snap.aux1_type;
    robotState.cfg_aux2_type = snap.aux2_type;
    robotState.cfg_aux3_type = snap.aux3_type;
    robotState.cfg_dome_min_speed = snap.dome_min_speed;
    robotState.cfg_dome_max_speed = snap.dome_max_speed;
    robotState.cfg_seq_open_ms = snap.seq_open_ms;
    robotState.cfg_seq_close_ms = snap.seq_close_ms;
    robotState.cfg_dome_neutral_us = snap.dome_neutral_us;
    robotState.cfg_dome_min_pulse_us = snap.dome_min_pulse_us;
    robotState.cfg_dome_max_pulse_us = snap.dome_max_pulse_us;
    robotState.cfg_dome_speed_limit_pct = snap.dome_speed_limit_pct;
    robotState.cfg_dome_rnd_enable = snap.dome_rnd_enable;
    robotState.cfg_dome_rnd_speed_pct = snap.dome_rnd_speed_pct;
    robotState.cfg_dome_rnd_pause_min = snap.dome_rnd_pause_min;
    robotState.cfg_dome_rnd_pause_max = snap.dome_rnd_pause_max;
    robotState.cfg_dome_rnd_move_ms = snap.dome_rnd_move_ms;
    robotState.cfg_rc_input_mode = snap.rc_input_mode;
    robotState.cfg_enable_arm1 = snap.enable_arm1;
    robotState.cfg_enable_arm2 = snap.enable_arm2;
    robotState.cfg_enable_aux1 = snap.enable_aux1;
    robotState.cfg_enable_aux2 = snap.enable_aux2;
    robotState.cfg_enable_aux3 = snap.enable_aux3;
    robotState.cfg_enable_dome = snap.enable_dome;
    robotState.cfg_enable_rc_ch1 = snap.enable_rc_ch1;
    robotState.cfg_enable_rc_ch2 = snap.enable_rc_ch2;
    robotState.cfg_enable_rc_ch3 = snap.enable_rc_ch3;
    robotState.cfg_enable_rc_ch4 = snap.enable_rc_ch4;
    robotState.cfg_enable_rc_ch5 = snap.enable_rc_ch5;
    robotState.cfg_enable_rc_ch6 = snap.enable_rc_ch6;
    robotState.cfg_single_sbus_use_ch2 = snap.single_sbus_use_ch2;
    robotState.cfg_enable_s1_hoverboard = snap.enable_s1_hoverboard;
    robotState.cfg_enable_s2_sound = snap.enable_s2_sound;
    robotState.cfg_enable_s3_dome_ctrl = snap.enable_s3_dome_ctrl;
    snprintf(robotState.cfg_dome_wifi_peer_ip, sizeof(robotState.cfg_dome_wifi_peer_ip), "%s",
             snap.dome_wifi_peer_ip);
    robotState.cfg_stationary = snap.stationary;
    robotState.cfg_aux_led_pin = snap.aux_led_pin;
    robotState.cfg_aux_led_count = snap.aux_led_count;
    robotState.cfg_rc_pwm_drive_speed = snap.rc_pwm_drive_speed;
    robotState.cfg_rc_pwm_drive_steer = snap.rc_pwm_drive_steer;
    robotState.cfg_rc_pwm_dome_speed = snap.rc_pwm_dome_speed;
    robotState.cfg_rc_pwm_arm1 = snap.rc_pwm_arm1;
    robotState.cfg_rc_pwm_arm2 = snap.rc_pwm_arm2;
    robotState.cfg_rc_pwm_sound = snap.rc_pwm_sound;
    robotState.cfg_rc_sbus_drive_speed = snap.rc_sbus_drive_speed;
    robotState.cfg_rc_sbus_drive_steer = snap.rc_sbus_drive_steer;
    robotState.cfg_rc_sbus_dome_speed = snap.rc_sbus_dome_speed;
    robotState.cfg_rc_sbus_arm1 = snap.rc_sbus_arm1;
    robotState.cfg_rc_sbus_arm2 = snap.rc_sbus_arm2;
    robotState.cfg_rc_sbus_sound = snap.rc_sbus_sound;
    robotState.cfg_rc_arm1 = snap.rc_arm1;
    robotState.cfg_rc_arm2 = snap.rc_arm2;
    robotState.cfg_rc_aux1 = snap.rc_aux1;
    robotState.cfg_rc_aux2 = snap.rc_aux2;
    robotState.cfg_rc_aux3 = snap.rc_aux3;
    robotState.cfg_rc_sound = snap.rc_sound;
    robotState.cfg_rc_opmode = snap.rc_opmode;
    robotState.cfg_rc_free0 = snap.rc_free0;
    robotState.cfg_rc_free1 = snap.rc_free1;
    robotState.cfg_rc_free2 = snap.rc_free2;
    robotState.cfg_rc_free3 = snap.rc_free3;
}

bool configLoad(Preferences& prefs, ConfigSnapshot* out) {
    if (out == nullptr) {
        return false;
    }

    // Start with defaults
    configSnapshotDefaults(out);

    // Check schema version
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

    // Load all fields from NVS
    out->speedLimitMax = prefs.getShort(NVS_KEYS.speedLimitMax, SPEED_LIMIT_MAX);
    out->speedPresetSlow = prefs.getShort(NVS_KEYS.speedPresetSlow, SPEED_PRESET_SLOW);
    out->speedPresetNormal = prefs.getShort(NVS_KEYS.speedPresetNormal, SPEED_PRESET_NORMAL);
    out->speedPresetTurbo = prefs.getShort(NVS_KEYS.speedPresetTurbo, SPEED_PRESET_TURBO);
    out->speedPresetActive =
        normalizeSpeedPresetId(prefs.getUChar(NVS_KEYS.speedPresetActive, (uint8_t)SpeedPresetId::Normal));
    out->sbusTimeoutMs = prefs.getULong(NVS_KEYS.sbusTimeoutMs, SBUS_TIMEOUT_MS);
    out->webDriveTimeoutMs = prefs.getULong(NVS_KEYS.webDriveTimeoutMs, WEB_DRIVE_TIMEOUT_MS);
    out->audioVolume = prefs.getUChar(NVS_KEYS.audioVolume, 20);
    out->logLevel = prefs.getUChar(NVS_KEYS.logLevel, PA_LOG_LEVEL);

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

    union {
        float f;
        uint32_t u;
    } dome_conv;
    dome_conv.u = prefs.getULong(NVS_KEYS.domeMin, 0);
    out->dome_min_speed = dome_conv.f;
    dome_conv.u = prefs.getULong(NVS_KEYS.domeMax, 0x3F800000);
    out->dome_max_speed = dome_conv.f;

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

    out->seq_open_ms = prefs.getUShort(NVS_KEYS.seqOpenMs, 1000);
    out->seq_close_ms = prefs.getUShort(NVS_KEYS.seqCloseMs, 1000);

    out->aux_led_pin = prefs.getUChar(NVS_KEYS.auxLedPin, AUX_LED_PIN_DISABLED);
    out->aux_led_count = prefs.getUChar(NVS_KEYS.auxLedCount, AUX_LED_COUNT_DEFAULT);

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

    // RC Bindings Tier 1
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

    // RC Bindings Tier 2
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

    // =========================================================================
    // Validation and clamping phase — ensure all fields are within valid ranges
    // =========================================================================

    // Scalar constrain: speed limits and presets (0..SPEED_LIMIT_MAX)
    out->speedLimitMax = constrain(out->speedLimitMax, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->speedPresetSlow = constrain(out->speedPresetSlow, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->speedPresetNormal = constrain(out->speedPresetNormal, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->speedPresetTurbo = constrain(out->speedPresetTurbo, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);

    // Timeout constraints
    out->sbusTimeoutMs = constrain(out->sbusTimeoutMs, (uint32_t)50, (uint32_t)5000);
    out->webDriveTimeoutMs = constrain(out->webDriveTimeoutMs, (uint32_t)100, (uint32_t)5000);

    // Audio volume constraint (0..30)
    out->audioVolume = constrain(out->audioVolume, (uint8_t)0, (uint8_t)30);

    // Servo pulse width constraints (500..2500)
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

    // Servo type enum guard
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

    // Dome float guards
    if (out->dome_min_speed < 0.0f)
        out->dome_min_speed = 0.0f;
    if (out->dome_max_speed > 1.0f)
        out->dome_max_speed = 1.0f;

    // Sequence timing guards (100..5000 ms)
    if (out->seq_open_ms < 100)
        out->seq_open_ms = 100;
    if (out->seq_open_ms > 5000)
        out->seq_open_ms = 5000;
    if (out->seq_close_ms < 100)
        out->seq_close_ms = 100;
    if (out->seq_close_ms > 5000)
        out->seq_close_ms = 5000;

    // Dome pulse width constraints (1000..2000)
    out->dome_neutral_us = constrain(out->dome_neutral_us, (uint16_t)1000, (uint16_t)2000);
    out->dome_min_pulse_us = constrain(out->dome_min_pulse_us, (uint16_t)1000, (uint16_t)2000);
    out->dome_max_pulse_us = constrain(out->dome_max_pulse_us, (uint16_t)1000, (uint16_t)2000);

    // Dome speed limit percentage constraint (0..100)
    out->dome_speed_limit_pct = constrain(out->dome_speed_limit_pct, (uint8_t)0, (uint8_t)100);

    // RC input mode guard
    if (out->rc_input_mode > RC_INPUT_DUAL_SBUS) {
        out->rc_input_mode = RC_INPUT_DUAL_SBUS;
    }

    // AUX LED validation
    if (!auxLedPinSettingValid(out->aux_led_pin)) {
        out->aux_led_pin = AUX_LED_PIN_DISABLED;
    }
    out->aux_led_count = constrain(out->aux_led_count, AUX_LED_COUNT_DEFAULT, AUX_LED_COUNT_MAX);

    // WiFi peer IP validation (only available in Arduino environments with IPAddress library)
#ifdef ARDUINO
    if (out->dome_wifi_peer_ip[0] != '\0') {
        IPAddress parsedPeerIp;
        if (!parsedPeerIp.fromString(out->dome_wifi_peer_ip)) {
            out->dome_wifi_peer_ip[0] = '\0';
        }
    }
#endif

    // Validate RC Tier 1 bindings
    RcBindingConfig* bindings[] = {
        &out->rc_pwm_drive_speed, &out->rc_pwm_drive_steer,
        &out->rc_pwm_arm1,        &out->rc_pwm_arm2,
        &out->rc_pwm_sound,       &out->rc_sbus_drive_speed,
        &out->rc_sbus_dome_speed, &out->rc_sbus_arm1,
        &out->rc_sbus_arm2,       &out->rc_sbus_sound,
    };
    const RcBindingConfig defaults[] = {
        defaultPwmBinding(1),                               defaultPwmBinding(2),
        defaultPwmBinding(3),                               defaultPwmBinding(4),
        defaultPwmBinding(5),                               defaultPwmBinding(6),
        defaultSbusBinding(RC_BINDING_SBUS1, 1),            defaultSbusBinding(RC_BINDING_SBUS1, 2),
        defaultSbusBinding(RC_BINDING_SBUS2, 1),            defaultSbusBinding(RC_BINDING_SBUS2, 2),
        defaultSbusBinding(RC_BINDING_SBUS2, 3),            disabledRcBinding(),
    };
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
        if (!rcBindingIsValid(*bindings[i])) {
            *bindings[i] = defaults[i];
        }
    }

    // Validate RC Tier 2 Trigger bindings
    RcTriggerBinding* triggerBindings[] = {
        &out->rc_arm1,   &out->rc_arm2,  &out->rc_aux1,   &out->rc_aux2,   &out->rc_aux3,
        &out->rc_sound,  &out->rc_opmode, &out->rc_free0, &out->rc_free1, &out->rc_free2,
        &out->rc_free3,
    };
    const RcTriggerBinding triggerDefaults[] = {
        makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                             RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX, 0,
                             rcTriggerDefaultReverse(RC_BINDING_SBUS1, 4)),
        makeRcTriggerBinding(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM2_TOGGLE, nullptr,
                             RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX, 0,
                             rcTriggerDefaultReverse(RC_BINDING_SBUS1, 5)),
        disabledRcTriggerBinding(),
        disabledRcTriggerBinding(),
        disabledRcTriggerBinding(),
        disabledRcTriggerBinding(),
        disabledRcTriggerBinding(),
        disabledRcTriggerBinding(),
        disabledRcTriggerBinding(),
        disabledRcTriggerBinding(),
        disabledRcTriggerBinding(),
    };
    for (size_t i = 0; i < sizeof(triggerBindings) / sizeof(triggerBindings[0]); ++i) {
        if (!rcTriggerBindingIsValid(*triggerBindings[i])) {
            *triggerBindings[i] = triggerDefaults[i];
        }
    }

    // If legacy, stamp new schema version
    if (isLegacy) {
        prefs.putUChar(CONFIG_SCHEMA_VERSION_KEY, CONFIG_SCHEMA_VERSION);
    }

    return true;
}

bool configSave(Preferences& prefs, const ConfigSnapshot& snapshot) {
    bool ok = true;

    // Speed
    ok = prefs.putShort(NVS_KEYS.speedLimitMax, snapshot.speedLimitMax) > 0 && ok;
    ok = prefs.putShort(NVS_KEYS.speedPresetSlow, snapshot.speedPresetSlow) > 0 && ok;
    ok = prefs.putShort(NVS_KEYS.speedPresetNormal, snapshot.speedPresetNormal) > 0 && ok;
    ok = prefs.putShort(NVS_KEYS.speedPresetTurbo, snapshot.speedPresetTurbo) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.speedPresetActive, (uint8_t)snapshot.speedPresetActive) > 0 && ok;

    // Timeouts
    ok = prefs.putULong(NVS_KEYS.sbusTimeoutMs, snapshot.sbusTimeoutMs) > 0 && ok;
    ok = prefs.putULong(NVS_KEYS.webDriveTimeoutMs, snapshot.webDriveTimeoutMs) > 0 && ok;

    // Audio
    ok = prefs.putUChar(NVS_KEYS.audioVolume, snapshot.audioVolume) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.logLevel, snapshot.logLevel) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndScream, snapshot.snd_scream) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndFaint, snapshot.snd_faint) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndLeia, snapshot.snd_leia) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCantinaS, snapshot.snd_cantina_s) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSwTheme, snapshot.snd_sw_theme) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndImpMarch, snapshot.snd_imp_march) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCantinaL, snapshot.snd_cantina_l) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndStartup, snapshot.snd_startup) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndDoodoo, snapshot.snd_doodoo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndFailure, snapshot.snd_failure) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndDisco, snapshot.snd_disco) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMahna, snapshot.snd_mahna) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndInlove, snapshot.snd_inlove) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMacho, snapshot.snd_macho) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndGangnam, snapshot.snd_gangnam) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndUptown, snapshot.snd_uptown) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCelebr, snapshot.snd_celebr) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndStayin, snapshot.snd_stayin) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndHarlem, snapshot.snd_harlem) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndPbjtime, snapshot.snd_pbjtime) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysBoot, snapshot.snd_sys_boot) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysModeN, snapshot.snd_sys_mode_n) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysModeS, snapshot.snd_sys_mode_s) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysModeT, snapshot.snd_sys_mode_t) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysDrvOn, snapshot.snd_sys_drv_on) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndSysDomeOn, snapshot.snd_sys_dome_on) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndRandMin, snapshot.snd_rand_min) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndRandMax, snapshot.snd_rand_max) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndIntQuiet, snapshot.snd_int_quiet) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndIntMid, snapshot.snd_int_mid) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndIntFull, snapshot.snd_int_full) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndIntAwake, snapshot.snd_int_awake) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMoodcatQuiet, snapshot.snd_moodcat_quiet & 0x0FFF) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMoodcatMid, snapshot.snd_moodcat_mid & 0x0FFF) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMoodcatFull, snapshot.snd_moodcat_full & 0x0FFF) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndMoodcatAwakeplus, snapshot.snd_moodcat_awakeplus & 0x0FFF) > 0 && ok;

    ok = prefs.putUShort(NVS_KEYS.sndCatGenLo, snapshot.snd_cat_gen_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatGenHi, snapshot.snd_cat_gen_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatChatLo, snapshot.snd_cat_chat_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatChatHi, snapshot.snd_cat_chat_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatHapLo, snapshot.snd_cat_hap_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatHapHi, snapshot.snd_cat_hap_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatProcLo, snapshot.snd_cat_proc_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatProcHi, snapshot.snd_cat_proc_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSadLo, snapshot.snd_cat_sad_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSadHi, snapshot.snd_cat_sad_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSentLo, snapshot.snd_cat_sent_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSentHi, snapshot.snd_cat_sent_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatHumLo, snapshot.snd_cat_hum_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatHumHi, snapshot.snd_cat_hum_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatScrmLo, snapshot.snd_cat_scrm_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatScrmHi, snapshot.snd_cat_scrm_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatOohLo, snapshot.snd_cat_ooh_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatOohHi, snapshot.snd_cat_ooh_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatAlrmLo, snapshot.snd_cat_alrm_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatAlrmHi, snapshot.snd_cat_alrm_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSnarkyLo, snapshot.snd_cat_snarky_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatSnarkyHi, snapshot.snd_cat_snarky_hi) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatWhisLo, snapshot.snd_cat_whis_lo) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.sndCatWhisHi, snapshot.snd_cat_whis_hi) > 0 && ok;

    // Servo
    ok = prefs.putUShort(NVS_KEYS.arm1OpenUs, snapshot.arm1_open_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.arm1CloseUs, snapshot.arm1_close_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.arm2OpenUs, snapshot.arm2_open_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.arm2CloseUs, snapshot.arm2_close_us) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.arm1Type, (uint8_t)snapshot.arm1_type) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.arm2Type, (uint8_t)snapshot.arm2_type) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux1OpenUs, snapshot.aux1_open_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux1CloseUs, snapshot.aux1_close_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux2OpenUs, snapshot.aux2_open_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux2CloseUs, snapshot.aux2_close_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux3OpenUs, snapshot.aux3_open_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.aux3CloseUs, snapshot.aux3_close_us) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.aux1Type, (uint8_t)snapshot.aux1_type) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.aux2Type, (uint8_t)snapshot.aux2_type) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.aux3Type, (uint8_t)snapshot.aux3_type) > 0 && ok;

    // Dome
    union {
        float f;
        uint32_t u;
    } dome_conv;
    dome_conv.f = snapshot.dome_min_speed;
    ok = prefs.putULong(NVS_KEYS.domeMin, dome_conv.u) > 0 && ok;
    dome_conv.f = snapshot.dome_max_speed;
    ok = prefs.putULong(NVS_KEYS.domeMax, dome_conv.u) > 0 && ok;

    ok = prefs.putUShort(NVS_KEYS.domeNeutralUs, snapshot.dome_neutral_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.domeMinPulseUs, snapshot.dome_min_pulse_us) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.domeMaxPulseUs, snapshot.dome_max_pulse_us) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.domeSpeedLimitPct, snapshot.dome_speed_limit_pct) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.domeRndEnable, snapshot.dome_rnd_enable) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.domeRndSpeedPct, snapshot.dome_rnd_speed_pct) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.domeRndPauseMin, snapshot.dome_rnd_pause_min) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.domeRndPauseMax, snapshot.dome_rnd_pause_max) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.domeRndMoveMs, snapshot.dome_rnd_move_ms) > 0 && ok;

    if (snapshot.dome_wifi_peer_ip[0] == '\0') {
        prefs.remove(NVS_KEYS.domeWifiPeerIp);
    } else {
        ok = prefs.putString(NVS_KEYS.domeWifiPeerIp, snapshot.dome_wifi_peer_ip) > 0 && ok;
    }

    // Sequence
    ok = prefs.putUShort(NVS_KEYS.seqOpenMs, snapshot.seq_open_ms) > 0 && ok;
    ok = prefs.putUShort(NVS_KEYS.seqCloseMs, snapshot.seq_close_ms) > 0 && ok;

    // AUX LED
    ok = prefs.putUChar(NVS_KEYS.auxLedPin, snapshot.aux_led_pin) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.auxLedCount, snapshot.aux_led_count) > 0 && ok;

    // Feature toggles
    ok = prefs.putBool(NVS_KEYS.enableArm1, snapshot.enable_arm1) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableArm2, snapshot.enable_arm2) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableAux1, snapshot.enable_aux1) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableAux2, snapshot.enable_aux2) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableAux3, snapshot.enable_aux3) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableDome, snapshot.enable_dome) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh1, snapshot.enable_rc_ch1) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh2, snapshot.enable_rc_ch2) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh3, snapshot.enable_rc_ch3) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh4, snapshot.enable_rc_ch4) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh5, snapshot.enable_rc_ch5) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableRcCh6, snapshot.enable_rc_ch6) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.singleSbusUseCh2, snapshot.single_sbus_use_ch2) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableS1Hoverboard, snapshot.enable_s1_hoverboard) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableS2Sound, snapshot.enable_s2_sound) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.enableS3DomeCtrl, snapshot.enable_s3_dome_ctrl) > 0 && ok;
    ok = prefs.putBool(NVS_KEYS.stationary, snapshot.stationary) > 0 && ok;
    ok = prefs.putUChar(NVS_KEYS.rcInputMode, (uint8_t)snapshot.rc_input_mode) > 0 && ok;

    // RC Bindings Tier 1
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmDriveSpeed, snapshot.rc_pwm_drive_speed) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmDriveSteer, snapshot.rc_pwm_drive_steer) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmDomeSpeed, snapshot.rc_pwm_dome_speed) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmArm1, snapshot.rc_pwm_arm1) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmArm2, snapshot.rc_pwm_arm2) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcPwmSound, snapshot.rc_pwm_sound) && ok;

    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusDriveSpeed, snapshot.rc_sbus_drive_speed) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusDriveSteer, snapshot.rc_sbus_drive_steer) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusDomeSpeed, snapshot.rc_sbus_dome_speed) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusArm1, snapshot.rc_sbus_arm1) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusArm2, snapshot.rc_sbus_arm2) && ok;
    ok = saveRcBindingToPrefs(prefs, NVS_KEYS.rcSbusSound, snapshot.rc_sbus_sound) && ok;

    // RC Bindings Tier 2
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcArm1, snapshot.rc_arm1) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcArm2, snapshot.rc_arm2) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcAux1, snapshot.rc_aux1) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcAux2, snapshot.rc_aux2) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcAux3, snapshot.rc_aux3) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcSound, snapshot.rc_sound) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcOpmode, snapshot.rc_opmode) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcFree0, snapshot.rc_free0) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcFree1, snapshot.rc_free1) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcFree2, snapshot.rc_free2) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, NVS_KEYS.rcFree3, snapshot.rc_free3) && ok;

    // Stamp schema version
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
