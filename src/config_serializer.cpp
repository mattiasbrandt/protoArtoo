// =============================================================================
// src/config_serializer.cpp
//
// Pure config serialization implementation.
// No logging, no FreeRTOS, no task-level calls — uses Arduino String for NVS string values.
// =============================================================================

#include "config_serializer.h"

#include "api_helpers.h"
#include "audio_dollar_parser.h"
#include "config.h"
#include "rc_mapping.h"

#include <cstring>

namespace {

// NVS has no float primitive; store floats as raw IEEE 754 bits via uint32 to avoid
// text-roundtrip precision loss and platform endianness ambiguity.
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

// Forward declarations of deserialize/serialize helpers
void deserializeDrive(const ConfigReader& r, DriveConfig* out, const DriveConfig& def);
void deserializeAudio(const ConfigReader& r, AudioConfig* out, const AudioConfig& def);
void deserializeServo(const ConfigReader& r, ServoConfig* out, const ServoConfig& def);
void deserializeDome(const ConfigReader& r, DomeConfig* out, const DomeConfig& def);
void deserializeSystem(const ConfigReader& r, SystemConfig* out, const SystemConfig& def);

void deserializeDrive(const ConfigReader& r, DriveConfig* out, const DriveConfig& def) {
    *out = def;
    out->speedLimitMax     = r.readI16("spd_max",   def.speedLimitMax);
    out->speedPresetSlow   = r.readI16("spd_pre_s", def.speedPresetSlow);
    out->speedPresetNormal = r.readI16("spd_pre_n", def.speedPresetNormal);
    out->speedPresetTurbo  = r.readI16("spd_pre_t", def.speedPresetTurbo);
    out->speedPresetActive =
        normalizeSpeedPresetId(r.readU8("spd_pre_a", (uint8_t)def.speedPresetActive));
    out->sbusTimeoutMs     = r.readU32("sbus_tmo", def.sbusTimeoutMs);
    out->webDriveTimeoutMs = r.readU32("web_tmo",  def.webDriveTimeoutMs);

    // Clamp to physical hoverboard limit (SPEED_LIMIT_MAX = 600) and valid timeout windows;
    // guards against corrupt NVS values reaching DriveTask
    out->speedLimitMax     = constrain(out->speedLimitMax,     (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->speedPresetSlow   = constrain(out->speedPresetSlow,   (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->speedPresetNormal = constrain(out->speedPresetNormal, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->speedPresetTurbo  = constrain(out->speedPresetTurbo,  (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    out->sbusTimeoutMs     = constrain(out->sbusTimeoutMs,     (uint32_t)50,  (uint32_t)5000);
    out->webDriveTimeoutMs = constrain(out->webDriveTimeoutMs, (uint32_t)100, (uint32_t)5000);
}

void deserializeAudio(const ConfigReader& r, AudioConfig* out, const AudioConfig& def) {
    *out = def;
    out->audioVolume = r.readU8("aud_vol", def.audioVolume);
    out->snd_scream = r.readU16("snd_scream", def.snd_scream);
    out->snd_faint = r.readU16("snd_faint", def.snd_faint);
    out->snd_leia = r.readU16("snd_leia", def.snd_leia);
    out->snd_cantina_s = r.readU16("snd_cantina_s", def.snd_cantina_s);
    out->snd_sw_theme = r.readU16("snd_sw", def.snd_sw_theme);
    out->snd_imp_march = r.readU16("snd_march", def.snd_imp_march);
    out->snd_cantina_l = r.readU16("snd_cantina_l", def.snd_cantina_l);
    out->snd_startup = r.readU16("snd_startup", def.snd_startup);
    out->snd_doodoo = r.readU16("snd_doodoo", def.snd_doodoo);
    out->snd_failure = r.readU16("snd_failure", def.snd_failure);
    out->snd_disco = r.readU16("snd_disco", def.snd_disco);
    out->snd_happy = r.readU16("snd_happy", def.snd_happy);
    out->snd_mahna = r.readU16("snd_mahna", def.snd_mahna);
    out->snd_inlove = r.readU16("snd_inlove", def.snd_inlove);
    out->snd_macho = r.readU16("snd_macho", def.snd_macho);
    out->snd_gangnam = r.readU16("snd_gangnam", def.snd_gangnam);
    out->snd_uptown = r.readU16("snd_uptown", def.snd_uptown);
    out->snd_celebr = r.readU16("snd_celebr", def.snd_celebr);
    out->snd_stayin = r.readU16("snd_stayin", def.snd_stayin);
    out->snd_harlem = r.readU16("snd_harlem", def.snd_harlem);
    out->snd_pbjtime = r.readU16("snd_pbjtime", def.snd_pbjtime);
    out->snd_sys_boot = r.readU16("snd_sys_boot", def.snd_sys_boot);
    out->snd_sys_mode_n = r.readU16("snd_sys_mode_n", def.snd_sys_mode_n);
    out->snd_sys_mode_s = r.readU16("snd_sys_mode_s", def.snd_sys_mode_s);
    out->snd_sys_mode_t = r.readU16("snd_sys_mode_t", def.snd_sys_mode_t);
    out->snd_sys_drv_on = r.readU16("snd_sys_drv_on", def.snd_sys_drv_on);
    out->snd_sys_dome_on = r.readU16("snd_sys_dome_on", def.snd_sys_dome_on);
    out->snd_rand_min = r.readU16("snd_rand_min", def.snd_rand_min);
    out->snd_rand_max = r.readU16("snd_rand_max", def.snd_rand_max);
    out->snd_int_quiet = r.readU16("snd_int_quiet", def.snd_int_quiet);
    out->snd_int_mid = r.readU16("snd_int_mid", def.snd_int_mid);
    out->snd_int_full = r.readU16("snd_int_full", def.snd_int_full);
    out->snd_int_awake = r.readU16("snd_int_awake", def.snd_int_awake);
    // Upper nibble carries category flags (stripped on write); mask defensively on read too
    out->snd_moodcat_quiet     = r.readU16("snd_moodcat_q", def.snd_moodcat_quiet)     & 0x0FFF;
    out->snd_moodcat_mid       = r.readU16("snd_moodcat_m", def.snd_moodcat_mid)       & 0x0FFF;
    out->snd_moodcat_full      = r.readU16("snd_moodcat_f", def.snd_moodcat_full)      & 0x0FFF;
    out->snd_moodcat_awakeplus = r.readU16("snd_moodcat_a", def.snd_moodcat_awakeplus) & 0x0FFF;
    out->snd_cat_gen_lo = r.readU16("snd_cat_gen_lo", def.snd_cat_gen_lo);
    out->snd_cat_gen_hi = r.readU16("snd_cat_gen_hi", def.snd_cat_gen_hi);
    out->snd_cat_chat_lo = r.readU16("snd_cat_chat_lo", def.snd_cat_chat_lo);
    out->snd_cat_chat_hi = r.readU16("snd_cat_chat_hi", def.snd_cat_chat_hi);
    out->snd_cat_hap_lo = r.readU16("snd_cat_hap_lo", def.snd_cat_hap_lo);
    out->snd_cat_hap_hi = r.readU16("snd_cat_hap_hi", def.snd_cat_hap_hi);
    out->snd_cat_proc_lo = r.readU16("snd_cat_proc_lo", def.snd_cat_proc_lo);
    out->snd_cat_proc_hi = r.readU16("snd_cat_proc_hi", def.snd_cat_proc_hi);
    out->snd_cat_sad_lo = r.readU16("snd_cat_sad_lo", def.snd_cat_sad_lo);
    out->snd_cat_sad_hi = r.readU16("snd_cat_sad_hi", def.snd_cat_sad_hi);
    out->snd_cat_sent_lo = r.readU16("snd_cat_sent_lo", def.snd_cat_sent_lo);
    out->snd_cat_sent_hi = r.readU16("snd_cat_sent_hi", def.snd_cat_sent_hi);
    out->snd_cat_hum_lo = r.readU16("snd_cat_hum_lo", def.snd_cat_hum_lo);
    out->snd_cat_hum_hi = r.readU16("snd_cat_hum_hi", def.snd_cat_hum_hi);
    out->snd_cat_scrm_lo = r.readU16("snd_cat_scrm_lo", def.snd_cat_scrm_lo);
    out->snd_cat_scrm_hi = r.readU16("snd_cat_scrm_hi", def.snd_cat_scrm_hi);
    out->snd_cat_ooh_lo = r.readU16("snd_cat_ooh_lo", def.snd_cat_ooh_lo);
    out->snd_cat_ooh_hi = r.readU16("snd_cat_ooh_hi", def.snd_cat_ooh_hi);
    out->snd_cat_alrm_lo = r.readU16("snd_cat_alrm_lo", def.snd_cat_alrm_lo);
    out->snd_cat_alrm_hi = r.readU16("snd_cat_alrm_hi", def.snd_cat_alrm_hi);
    out->snd_cat_snarky_lo = r.readU16("snd_cat_snrk_lo", def.snd_cat_snarky_lo);
    out->snd_cat_snarky_hi = r.readU16("snd_cat_snrk_hi", def.snd_cat_snarky_hi);
    out->snd_cat_whis_lo = r.readU16("snd_cat_whis_lo", def.snd_cat_whis_lo);
    out->snd_cat_whis_hi = r.readU16("snd_cat_whis_hi", def.snd_cat_whis_hi);

    out->audioVolume = constrain(out->audioVolume, (uint8_t)0, (uint8_t)30);  // DFPlayer Mini range
}

void deserializeServo(const ConfigReader& r, ServoConfig* out, const ServoConfig& def) {
    *out = def;
    out->arm1_open_us = r.readU16("arm1_op", def.arm1_open_us);
    out->arm1_close_us = r.readU16("arm1_cl", def.arm1_close_us);
    out->arm2_open_us = r.readU16("arm2_op", def.arm2_open_us);
    out->arm2_close_us = r.readU16("arm2_cl", def.arm2_close_us);
    out->arm1_type = (ServoComponentType)r.readU8("arm1_type", (uint8_t)def.arm1_type);
    out->arm2_type = (ServoComponentType)r.readU8("arm2_type", (uint8_t)def.arm2_type);
    out->aux1_open_us = r.readU16("aux1_op", def.aux1_open_us);
    out->aux1_close_us = r.readU16("aux1_cl", def.aux1_close_us);
    out->aux2_open_us = r.readU16("aux2_op", def.aux2_open_us);
    out->aux2_close_us = r.readU16("aux2_cl", def.aux2_close_us);
    out->aux3_open_us = r.readU16("aux3_op", def.aux3_open_us);
    out->aux3_close_us = r.readU16("aux3_cl", def.aux3_close_us);
    out->aux1_type = (ServoComponentType)r.readU8("aux1_type", (uint8_t)def.aux1_type);
    out->aux2_type = (ServoComponentType)r.readU8("aux2_type", (uint8_t)def.aux2_type);
    out->aux3_type = (ServoComponentType)r.readU8("aux3_type", (uint8_t)def.aux3_type);
    out->seq_open_ms = r.readU16("seq_op", def.seq_open_ms);
    out->seq_close_ms = r.readU16("seq_cl", def.seq_close_ms);
    out->aux_led_pin = r.readU8(NVS_KEY_AUX_LED_PIN, def.aux_led_pin);
    out->aux_led_count = r.readU8(NVS_KEY_AUX_LED_COUNT, def.aux_led_count);

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

void deserializeDome(const ConfigReader& r, DomeConfig* out, const DomeConfig& def) {
    *out = def;
    out->dome_min_speed = floatFromBits(r.readU32("dome_min", floatToBits(def.dome_min_speed)));
    out->dome_max_speed = floatFromBits(r.readU32("dome_max", floatToBits(def.dome_max_speed)));
    out->dome_neutral_us = r.readU16("dome_neu", def.dome_neutral_us);
    out->dome_min_pulse_us = r.readU16("dome_minp", def.dome_min_pulse_us);
    out->dome_max_pulse_us = r.readU16("dome_maxp", def.dome_max_pulse_us);
    out->dome_speed_limit_pct = r.readU8("dome_pct", def.dome_speed_limit_pct);
    out->dome_rnd_enable = r.readBool("dome_rnd_en", def.dome_rnd_enable);
    out->dome_rnd_speed_pct = r.readU8("dome_rnd_spd", def.dome_rnd_speed_pct);
    out->dome_rnd_pause_min = r.readU8("dome_rnd_pmin", def.dome_rnd_pause_min);
    out->dome_rnd_pause_max = r.readU8("dome_rnd_pmax", def.dome_rnd_pause_max);
    out->dome_rnd_move_ms = r.readU16("dome_rnd_ms", def.dome_rnd_move_ms);

    // Reject overlong IP strings before copying into fixed-size buffer
    String domeWifiPeerIp = r.readStr("dome_wip", "");
    if (domeWifiPeerIp.length() >= sizeof(out->dome_wifi_peer_ip)) {
        domeWifiPeerIp = String("");
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
}

// Parse a stored RC analog binding. Starts from def so fields absent from the encoded
// string keep their default values. Falls back to def if the string is missing, parse
// fails, or validation rejects the result (e.g. unknown source enum).
RcBindingConfig loadRcBinding(const ConfigReader& r, const char* key, RcBindingConfig def) {
    String str = r.readStr(key, "");
    if (str.length() > 0) {
        RcBindingConfig parsed = def;
        parseRcBindingConfig(str.c_str(), &parsed);
        if (rcBindingIsValid(parsed)) return parsed;
    }
    return def;
}

// Same pattern for RC trigger bindings.
RcTriggerBinding loadRcTrigger(const ConfigReader& r, const char* key, RcTriggerBinding def) {
    String str = r.readStr(key, "");
    if (str.length() > 0) {
        RcTriggerBinding parsed = def;
        parseRcTriggerBinding(str.c_str(), &parsed);
        if (rcTriggerBindingIsValid(parsed)) return parsed;
    }
    return def;
}

void deserializeSystem(const ConfigReader& r, SystemConfig* out, const SystemConfig& def) {
    *out = def;

    {
        // Block scope: destroys droidName String before the larger field block below,
        // keeping the peak frame smaller. normalizeDroidName rejects uppercase — fall
        // back to DROID_NAME_DEFAULT if stored name is invalid.
        String droidName = r.readStr("droid_name", DROID_NAME_DEFAULT);
        char normalizedName[DROID_NAME_MAX_LEN + 1] = {};
        if (!normalizeDroidName(droidName.c_str(), normalizedName, sizeof(normalizedName))) {
            snprintf(normalizedName, sizeof(normalizedName), "%s", DROID_NAME_DEFAULT);
        }
        snprintf(out->droid_name, sizeof(out->droid_name), "%s", normalizedName);
    }

    out->mdns_use_name        = r.readBool("mdns_use_name",  def.mdns_use_name);
    out->logLevel             = r.readU8  ("log_level",       def.logLevel);
    out->enable_arm1          = r.readBool("en_arm1",         def.enable_arm1);
    out->enable_arm2          = r.readBool("en_arm2",         def.enable_arm2);
    out->enable_aux1          = r.readBool("en_aux1",         def.enable_aux1);
    out->enable_aux2          = r.readBool("en_aux2",         def.enable_aux2);
    out->enable_aux3          = r.readBool("en_aux3",         def.enable_aux3);
    out->enable_dome          = r.readBool("en_dome",         def.enable_dome);
    out->enable_rc_ch1        = r.readBool("en_rc_ch1",       def.enable_rc_ch1);
    out->enable_rc_ch2        = r.readBool("en_rc_ch2",       def.enable_rc_ch2);
    out->enable_rc_ch3        = r.readBool("en_rc_ch3",       def.enable_rc_ch3);
    out->enable_rc_ch4        = r.readBool("en_rc_ch4",       def.enable_rc_ch4);
    out->enable_rc_ch5        = r.readBool("en_rc_ch5",       def.enable_rc_ch5);
    out->enable_rc_ch6        = r.readBool("en_rc_ch6",       def.enable_rc_ch6);
    out->single_sbus_use_ch2  = r.readBool("sbus_recv_ch2",   def.single_sbus_use_ch2);
    out->enable_s1_hoverboard = r.readBool("en_s1",           def.enable_s1_hoverboard);
    out->enable_s2_sound      = r.readBool("en_s2",           def.enable_s2_sound);
    out->enable_s3_dome_ctrl  = r.readBool("en_s3",           def.enable_s3_dome_ctrl);
    out->stationary           = r.readBool("op_mode",          def.stationary);
    out->rc_input_mode        = (RcInputMode)r.readU8("rc_mode", (uint8_t)def.rc_input_mode);

    out->rc_pwm_drive_speed  = loadRcBinding(r, "rcp_drv", def.rc_pwm_drive_speed);
    out->rc_pwm_drive_steer  = loadRcBinding(r, "rcp_str", def.rc_pwm_drive_steer);
    out->rc_pwm_dome_speed   = loadRcBinding(r, "rcp_dom", def.rc_pwm_dome_speed);
    out->rc_pwm_arm1         = loadRcBinding(r, "rcp_a1",  def.rc_pwm_arm1);
    out->rc_pwm_arm2         = loadRcBinding(r, "rcp_a2",  def.rc_pwm_arm2);
    out->rc_pwm_sound        = loadRcBinding(r, "rcp_snd", def.rc_pwm_sound);
    out->rc_sbus_drive_speed = loadRcBinding(r, "rcs_drv", def.rc_sbus_drive_speed);
    out->rc_sbus_drive_steer = loadRcBinding(r, "rcs_str", def.rc_sbus_drive_steer);
    out->rc_sbus_dome_speed  = loadRcBinding(r, "rcs_dom", def.rc_sbus_dome_speed);
    out->rc_sbus_arm1        = loadRcBinding(r, "rcs_a1",  def.rc_sbus_arm1);
    out->rc_sbus_arm2        = loadRcBinding(r, "rcs_a2",  def.rc_sbus_arm2);
    out->rc_sbus_sound       = loadRcBinding(r, "rcs_snd", def.rc_sbus_sound);

    out->rc_arm1   = loadRcTrigger(r, "rc_arm1",  def.rc_arm1);
    out->rc_arm2   = loadRcTrigger(r, "rc_arm2",  def.rc_arm2);
    out->rc_aux1   = loadRcTrigger(r, "rc_aux1",  def.rc_aux1);
    out->rc_aux2   = loadRcTrigger(r, "rc_aux2",  def.rc_aux2);
    out->rc_aux3   = loadRcTrigger(r, "rc_aux3",  def.rc_aux3);
    out->rc_sound  = loadRcTrigger(r, "rc_sound", def.rc_sound);
    out->rc_opmode = loadRcTrigger(r, "rc_opmode",def.rc_opmode);
    out->rc_free0  = loadRcTrigger(r, "rc_free0", def.rc_free0);
    out->rc_free1  = loadRcTrigger(r, "rc_free1", def.rc_free1);
    out->rc_free2  = loadRcTrigger(r, "rc_free2", def.rc_free2);
    out->rc_free3  = loadRcTrigger(r, "rc_free3", def.rc_free3);

    if (out->rc_input_mode > RC_INPUT_DUAL_SBUS) {
        out->rc_input_mode = RC_INPUT_DUAL_SBUS;
    }
}

}  // namespace

// =============================================================================
// Shared defaults — ConfigSnapshot is 744 bytes, too large for the 6144-byte
// loop task stack. Static BSS allocation; populated once on first use.
// =============================================================================

static ConfigSnapshot s_defaults;
static bool s_defaults_initialised = false;

static const ConfigSnapshot& getDefaults() {
    if (!s_defaults_initialised) {
        configSnapshotDefaults(&s_defaults);
        s_defaults_initialised = true;
    }
    return s_defaults;
}

// =============================================================================
// Public API: Pure serializer functions
// =============================================================================

bool configDeserialize(const ConfigReader& reader, ConfigSnapshot* out) {
    if (out == nullptr) {
        return false;
    }
    const ConfigSnapshot& defaults = getDefaults();
    deserializeDrive(reader, &out->drive, defaults.drive);
    deserializeAudio(reader, &out->audio, defaults.audio);
    deserializeServo(reader, &out->servo, defaults.servo);
    deserializeDome(reader, &out->dome, defaults.dome);
    deserializeSystem(reader, &out->system, defaults.system);
    return true;
}

bool configSerialize(const ConfigSnapshot& snap, ConfigWriter& writer) {
    bool ok = true;
    ok = configSerializeDrive(snap.drive, writer) && ok;
    ok = configSerializeAudio(snap.audio, writer) && ok;
    ok = configSerializeServo(snap.servo, writer) && ok;
    ok = configSerializeDome(snap.dome, writer) && ok;
    ok = configSerializeSystem(snap.system, writer) && ok;
    ok = writer.writeSchemaVersion(CONFIG_SCHEMA_VERSION) && ok;
    return ok;
}

bool configSerializeDrive(const DriveConfig& cfg, ConfigWriter& w) {
    bool ok = true;
    ok = w.writeI16("spd_max", cfg.speedLimitMax) && ok;
    ok = w.writeI16("spd_pre_s", cfg.speedPresetSlow) && ok;
    ok = w.writeI16("spd_pre_n", cfg.speedPresetNormal) && ok;
    ok = w.writeI16("spd_pre_t", cfg.speedPresetTurbo) && ok;
    ok = w.writeU8("spd_pre_a", (uint8_t)cfg.speedPresetActive) && ok;
    ok = w.writeU32("sbus_tmo", cfg.sbusTimeoutMs) && ok;
    ok = w.writeU32("web_tmo", cfg.webDriveTimeoutMs) && ok;
    return ok;
}

bool configSerializeAudio(const AudioConfig& cfg, ConfigWriter& w) {
    bool ok = true;
    ok = w.writeU8("aud_vol", cfg.audioVolume) && ok;
    ok = w.writeU16("snd_scream", cfg.snd_scream) && ok;
    ok = w.writeU16("snd_faint", cfg.snd_faint) && ok;
    ok = w.writeU16("snd_leia", cfg.snd_leia) && ok;
    ok = w.writeU16("snd_cantina_s", cfg.snd_cantina_s) && ok;
    ok = w.writeU16("snd_sw", cfg.snd_sw_theme) && ok;
    ok = w.writeU16("snd_march", cfg.snd_imp_march) && ok;
    ok = w.writeU16("snd_cantina_l", cfg.snd_cantina_l) && ok;
    ok = w.writeU16("snd_startup", cfg.snd_startup) && ok;
    ok = w.writeU16("snd_doodoo", cfg.snd_doodoo) && ok;
    ok = w.writeU16("snd_failure", cfg.snd_failure) && ok;
    ok = w.writeU16("snd_disco", cfg.snd_disco) && ok;
    ok = w.writeU16("snd_mahna", cfg.snd_mahna) && ok;
    ok = w.writeU16("snd_inlove", cfg.snd_inlove) && ok;
    ok = w.writeU16("snd_macho", cfg.snd_macho) && ok;
    ok = w.writeU16("snd_gangnam", cfg.snd_gangnam) && ok;
    ok = w.writeU16("snd_uptown", cfg.snd_uptown) && ok;
    ok = w.writeU16("snd_celebr", cfg.snd_celebr) && ok;
    ok = w.writeU16("snd_stayin", cfg.snd_stayin) && ok;
    ok = w.writeU16("snd_harlem", cfg.snd_harlem) && ok;
    ok = w.writeU16("snd_pbjtime", cfg.snd_pbjtime) && ok;
    ok = w.writeU16("snd_sys_boot", cfg.snd_sys_boot) && ok;
    ok = w.writeU16("snd_sys_mode_n", cfg.snd_sys_mode_n) && ok;
    ok = w.writeU16("snd_sys_mode_s", cfg.snd_sys_mode_s) && ok;
    ok = w.writeU16("snd_sys_mode_t", cfg.snd_sys_mode_t) && ok;
    ok = w.writeU16("snd_sys_drv_on", cfg.snd_sys_drv_on) && ok;
    ok = w.writeU16("snd_sys_dome_on", cfg.snd_sys_dome_on) && ok;
    ok = w.writeU16("snd_rand_min", cfg.snd_rand_min) && ok;
    ok = w.writeU16("snd_rand_max", cfg.snd_rand_max) && ok;
    ok = w.writeU16("snd_int_quiet", cfg.snd_int_quiet) && ok;
    ok = w.writeU16("snd_int_mid", cfg.snd_int_mid) && ok;
    ok = w.writeU16("snd_int_full", cfg.snd_int_full) && ok;
    ok = w.writeU16("snd_int_awake", cfg.snd_int_awake) && ok;
    ok = w.writeU16("snd_moodcat_q", cfg.snd_moodcat_quiet & 0x0FFF) && ok;
    ok = w.writeU16("snd_moodcat_m", cfg.snd_moodcat_mid & 0x0FFF) && ok;
    ok = w.writeU16("snd_moodcat_f", cfg.snd_moodcat_full & 0x0FFF) && ok;
    ok = w.writeU16("snd_moodcat_a", cfg.snd_moodcat_awakeplus & 0x0FFF) && ok;
    ok = w.writeU16("snd_cat_gen_lo", cfg.snd_cat_gen_lo) && ok;
    ok = w.writeU16("snd_cat_gen_hi", cfg.snd_cat_gen_hi) && ok;
    ok = w.writeU16("snd_cat_chat_lo", cfg.snd_cat_chat_lo) && ok;
    ok = w.writeU16("snd_cat_chat_hi", cfg.snd_cat_chat_hi) && ok;
    ok = w.writeU16("snd_cat_hap_lo", cfg.snd_cat_hap_lo) && ok;
    ok = w.writeU16("snd_cat_hap_hi", cfg.snd_cat_hap_hi) && ok;
    ok = w.writeU16("snd_cat_proc_lo", cfg.snd_cat_proc_lo) && ok;
    ok = w.writeU16("snd_cat_proc_hi", cfg.snd_cat_proc_hi) && ok;
    ok = w.writeU16("snd_cat_sad_lo", cfg.snd_cat_sad_lo) && ok;
    ok = w.writeU16("snd_cat_sad_hi", cfg.snd_cat_sad_hi) && ok;
    ok = w.writeU16("snd_cat_sent_lo", cfg.snd_cat_sent_lo) && ok;
    ok = w.writeU16("snd_cat_sent_hi", cfg.snd_cat_sent_hi) && ok;
    ok = w.writeU16("snd_cat_hum_lo", cfg.snd_cat_hum_lo) && ok;
    ok = w.writeU16("snd_cat_hum_hi", cfg.snd_cat_hum_hi) && ok;
    ok = w.writeU16("snd_cat_scrm_lo", cfg.snd_cat_scrm_lo) && ok;
    ok = w.writeU16("snd_cat_scrm_hi", cfg.snd_cat_scrm_hi) && ok;
    ok = w.writeU16("snd_cat_ooh_lo", cfg.snd_cat_ooh_lo) && ok;
    ok = w.writeU16("snd_cat_ooh_hi", cfg.snd_cat_ooh_hi) && ok;
    ok = w.writeU16("snd_cat_alrm_lo", cfg.snd_cat_alrm_lo) && ok;
    ok = w.writeU16("snd_cat_alrm_hi", cfg.snd_cat_alrm_hi) && ok;
    ok = w.writeU16("snd_cat_snrk_lo", cfg.snd_cat_snarky_lo) && ok;
    ok = w.writeU16("snd_cat_snrk_hi", cfg.snd_cat_snarky_hi) && ok;
    ok = w.writeU16("snd_cat_whis_lo", cfg.snd_cat_whis_lo) && ok;
    ok = w.writeU16("snd_cat_whis_hi", cfg.snd_cat_whis_hi) && ok;
    return ok;
}

bool configSerializeServo(const ServoConfig& cfg, ConfigWriter& w) {
    bool ok = true;
    ok = w.writeU16("arm1_op", cfg.arm1_open_us) && ok;
    ok = w.writeU16("arm1_cl", cfg.arm1_close_us) && ok;
    ok = w.writeU16("arm2_op", cfg.arm2_open_us) && ok;
    ok = w.writeU16("arm2_cl", cfg.arm2_close_us) && ok;
    ok = w.writeU8("arm1_type", (uint8_t)cfg.arm1_type) && ok;
    ok = w.writeU8("arm2_type", (uint8_t)cfg.arm2_type) && ok;
    ok = w.writeU16("aux1_op", cfg.aux1_open_us) && ok;
    ok = w.writeU16("aux1_cl", cfg.aux1_close_us) && ok;
    ok = w.writeU16("aux2_op", cfg.aux2_open_us) && ok;
    ok = w.writeU16("aux2_cl", cfg.aux2_close_us) && ok;
    ok = w.writeU16("aux3_op", cfg.aux3_open_us) && ok;
    ok = w.writeU16("aux3_cl", cfg.aux3_close_us) && ok;
    ok = w.writeU8("aux1_type", (uint8_t)cfg.aux1_type) && ok;
    ok = w.writeU8("aux2_type", (uint8_t)cfg.aux2_type) && ok;
    ok = w.writeU8("aux3_type", (uint8_t)cfg.aux3_type) && ok;
    ok = w.writeU16("seq_op", cfg.seq_open_ms) && ok;
    ok = w.writeU16("seq_cl", cfg.seq_close_ms) && ok;
    ok = w.writeU8(NVS_KEY_AUX_LED_PIN, cfg.aux_led_pin) && ok;
    ok = w.writeU8(NVS_KEY_AUX_LED_COUNT, cfg.aux_led_count) && ok;
    return ok;
}

bool configSerializeDome(const DomeConfig& cfg, ConfigWriter& w) {
    bool ok = true;
    ok = w.writeU32("dome_min", floatToBits(cfg.dome_min_speed)) && ok;
    ok = w.writeU32("dome_max", floatToBits(cfg.dome_max_speed)) && ok;
    ok = w.writeU16("dome_neu", cfg.dome_neutral_us) && ok;
    ok = w.writeU16("dome_minp", cfg.dome_min_pulse_us) && ok;
    ok = w.writeU16("dome_maxp", cfg.dome_max_pulse_us) && ok;
    ok = w.writeU8("dome_pct", cfg.dome_speed_limit_pct) && ok;
    ok = w.writeBool("dome_rnd_en", cfg.dome_rnd_enable) && ok;
    ok = w.writeU8("dome_rnd_spd", cfg.dome_rnd_speed_pct) && ok;
    ok = w.writeU8("dome_rnd_pmin", cfg.dome_rnd_pause_min) && ok;
    ok = w.writeU8("dome_rnd_pmax", cfg.dome_rnd_pause_max) && ok;
    ok = w.writeU16("dome_rnd_ms", cfg.dome_rnd_move_ms) && ok;
    if (cfg.dome_wifi_peer_ip[0] != '\0') {
        ok = w.writeStr("dome_wip", cfg.dome_wifi_peer_ip) && ok;
    }
    // Empty peer IP is a valid "not configured" state; omit the key rather than write ""
    return ok;
}

bool configSerializeSystem(const SystemConfig& cfg, ConfigWriter& w) {
    bool ok = true;
    ok = w.writeStr("droid_name", cfg.droid_name) && ok;
    ok = w.writeBool("mdns_use_name", cfg.mdns_use_name) && ok;
    ok = w.writeU8("log_level", cfg.logLevel) && ok;
    ok = w.writeBool("en_arm1", cfg.enable_arm1) && ok;
    ok = w.writeBool("en_arm2", cfg.enable_arm2) && ok;
    ok = w.writeBool("en_aux1", cfg.enable_aux1) && ok;
    ok = w.writeBool("en_aux2", cfg.enable_aux2) && ok;
    ok = w.writeBool("en_aux3", cfg.enable_aux3) && ok;
    ok = w.writeBool("en_dome", cfg.enable_dome) && ok;
    ok = w.writeBool("en_rc_ch1", cfg.enable_rc_ch1) && ok;
    ok = w.writeBool("en_rc_ch2", cfg.enable_rc_ch2) && ok;
    ok = w.writeBool("en_rc_ch3", cfg.enable_rc_ch3) && ok;
    ok = w.writeBool("en_rc_ch4", cfg.enable_rc_ch4) && ok;
    ok = w.writeBool("en_rc_ch5", cfg.enable_rc_ch5) && ok;
    ok = w.writeBool("en_rc_ch6", cfg.enable_rc_ch6) && ok;
    ok = w.writeBool("sbus_recv_ch2", cfg.single_sbus_use_ch2) && ok;
    ok = w.writeBool("en_s1", cfg.enable_s1_hoverboard) && ok;
    ok = w.writeBool("en_s2", cfg.enable_s2_sound) && ok;
    ok = w.writeBool("en_s3", cfg.enable_s3_dome_ctrl) && ok;
    ok = w.writeBool("op_mode", cfg.stationary) && ok;
    ok = w.writeU8("rc_mode", (uint8_t)cfg.rc_input_mode) && ok;

    // RC bindings — format and write as strings
    char encoded[48] = {};
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_drive_speed)) {
        ok = w.writeStr("rcp_drv", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_drive_steer)) {
        ok = w.writeStr("rcp_str", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_dome_speed)) {
        ok = w.writeStr("rcp_dom", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_arm1)) {
        ok = w.writeStr("rcp_a1", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_arm2)) {
        ok = w.writeStr("rcp_a2", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_pwm_sound)) {
        ok = w.writeStr("rcp_snd", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_drive_speed)) {
        ok = w.writeStr("rcs_drv", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_drive_steer)) {
        ok = w.writeStr("rcs_str", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_dome_speed)) {
        ok = w.writeStr("rcs_dom", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_arm1)) {
        ok = w.writeStr("rcs_a1", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_arm2)) {
        ok = w.writeStr("rcs_a2", encoded) && ok;
    }
    if (formatRcBindingConfig(encoded, sizeof(encoded), cfg.rc_sbus_sound)) {
        ok = w.writeStr("rcs_snd", encoded) && ok;
    }

    // RC trigger bindings
    char triggerEncoded[64] = {};
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_arm1)) {
        ok = w.writeStr("rc_arm1", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_arm2)) {
        ok = w.writeStr("rc_arm2", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_aux1)) {
        ok = w.writeStr("rc_aux1", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_aux2)) {
        ok = w.writeStr("rc_aux2", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_aux3)) {
        ok = w.writeStr("rc_aux3", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_sound)) {
        ok = w.writeStr("rc_sound", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_opmode)) {
        ok = w.writeStr("rc_opmode", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_free0)) {
        ok = w.writeStr("rc_free0", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_free1)) {
        ok = w.writeStr("rc_free1", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_free2)) {
        ok = w.writeStr("rc_free2", triggerEncoded) && ok;
    }
    if (formatRcTriggerBinding(triggerEncoded, sizeof(triggerEncoded), cfg.rc_free3)) {
        ok = w.writeStr("rc_free3", triggerEncoded) && ok;
    }

    return ok;
}

// =============================================================================
// Domain-level deserializers — each loads only its own domain keys
// =============================================================================

void configDeserializeDrive(const ConfigReader& r, DriveConfig* out) {
    deserializeDrive(r, out, getDefaults().drive);
}

void configDeserializeAudio(const ConfigReader& r, AudioConfig* out) {
    deserializeAudio(r, out, getDefaults().audio);
}

void configDeserializeServo(const ConfigReader& r, ServoConfig* out) {
    deserializeServo(r, out, getDefaults().servo);
}

void configDeserializeDome(const ConfigReader& r, DomeConfig* out) {
    deserializeDome(r, out, getDefaults().dome);
}

void configDeserializeSystem(const ConfigReader& r, SystemConfig* out) {
    deserializeSystem(r, out, getDefaults().system);
}
