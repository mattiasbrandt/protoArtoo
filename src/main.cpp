// =============================================================================
// src/main.cpp
//
// protoArtoo — ESP32 body controller for MK4 astromech droid.
// Boot sequence — Phase 1 stub.
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <cstddef>
#include <esp_task_wdt.h>

#include "audio_dollar_parser.h"
#include "audio_task.h"
#include "aux_led.h"
#include "config_store.h"
#include "dome_link.h"
#include "dome_task.h"
#include "drive.h"
#include "failsafe_gate.h"
#include "ledc_pwm.h"
#include "log_buffer.h"
#include "mood.h"
#include "rc_input.h"
#include "robot_state.h"
#include "safety.h"
#include "servo_task.h"
#include "web_server.h"

// Global state — all tasks share these
RobotState robotState = {};
portMUX_TYPE robotStateMux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t servoCmdQueue = nullptr;
QueueHandle_t domeCmdQueue = nullptr;
QueueHandle_t audioCmdQueue = nullptr;
QueueHandle_t domeTxQueue = nullptr;
static volatile bool restartRequested = false;
static volatile uint32_t restartAtMs = 0;
static portMUX_TYPE restartMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;
static LogBuffer recentLogBuf = {};

namespace {

struct RcBindingNvsSpec {
    const char* key;
    RcBindingConfig* binding;
    RcBindingConfig defaultValue;
};

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

    String stored = prefs.getString(key, fallback);
    RcBindingConfig parsed = defaultValue;
    if (!parseRcBindingConfig(stored.c_str(), &parsed)) {
        parsed = defaultValue;
    }
    *out = parsed;
    return true;
}

bool saveRcBindingToPrefs(Preferences& prefs, const char* key, const RcBindingConfig& binding) {
    char encoded[48] = {};
    if (!formatRcBindingConfig(encoded, sizeof(encoded), binding)) {
        return false;
    }
    return prefs.putString(key, encoded) > 0;
}

// Tier 2 Trigger Binding NVS helpers
struct RcTriggerBindingNvsSpec {
    const char* key;
    RcTriggerBinding* binding;
    RcTriggerBinding defaultValue;
};

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

    String stored = prefs.getString(key, fallback);
    RcTriggerBinding parsed = defaultValue;
    if (!parseRcTriggerBinding(stored.c_str(), &parsed)) {
        parsed = defaultValue;
    }
    *out = parsed;
    return true;
}

bool saveRcTriggerBindingToPrefs(Preferences& prefs, const char* key,
                                 const RcTriggerBinding& binding) {
    char encoded[64] = {};
    if (!formatRcTriggerBinding(encoded, sizeof(encoded), binding)) {
        return false;
    }
    return prefs.putString(key, encoded) > 0;
}

const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "UNKNOWN";
        case ESP_RST_POWERON:
            return "POWERON";
        case ESP_RST_EXT:
            return "EXTERNAL";
        case ESP_RST_SW:
            return "SOFTWARE";
        case ESP_RST_PANIC:
            return "PANIC";
        case ESP_RST_INT_WDT:
            return "INT_WDT";
        case ESP_RST_TASK_WDT:
            return "TASK_WDT";
        case ESP_RST_WDT:
            return "WDT";
        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT";
        case ESP_RST_SDIO:
            return "SDIO";
        default:
            return "OTHER";
    }
}

void logBootHealth() {
    PA_LOG_INFO("main", "protoArtoo boot begin");
    PA_LOG_INFO("main", "reset_reason=%s (%d)", resetReasonName(esp_reset_reason()),
                (int)esp_reset_reason());
    PA_LOG_INFO("main",
                "config speed_limit_max=%d sbus_timeout_ms=%lu web_timeout_ms=%lu audio_volume=%u",
                robotState.cfg_speedLimitMax, (unsigned long)robotState.cfg_sbusTimeoutMs,
                (unsigned long)robotState.cfg_webDriveTimeoutMs, robotState.cfg_audioVolume);
    PA_LOG_DEBUG("main", "heap_free=%lu", (unsigned long)ESP.getFreeHeap());
}

}  // namespace

void paLogLineRaw(const char* line) {
    taskENTER_CRITICAL(&logMux);
    logBufferAppend(&recentLogBuf, line);
    taskEXIT_CRITICAL(&logMux);
}

size_t copyRecentLogs(char* buffer, size_t bufferSize) {
    taskENTER_CRITICAL(&logMux);
    size_t used = logBufferCopy(&recentLogBuf, buffer, bufferSize);
    taskEXIT_CRITICAL(&logMux);
    return used;
}

// Copy up to maxLines new log lines written since lastSent into out[][LOG_LINE_MAX].
// Returns new totalWritten. Sets *linesCopied to number of entries filled.
// Lines that have already been overwritten by the ring are silently skipped.
uint32_t copyNewLogLinesSince(uint32_t lastSent, char out[][LOG_LINE_MAX], size_t maxLines,
                              size_t* linesCopied) {
    taskENTER_CRITICAL(&logMux);
    uint32_t total = recentLogBuf.totalWritten;
    uint32_t count = (uint32_t)recentLogBuf.count;
    uint32_t ringStart = (total >= count) ? (total - count) : 0;
    uint32_t from = (lastSent > ringStart) ? lastSent : ringStart;
    uint32_t n = (from < total) ? (total - from) : 0;
    if (n > (uint32_t)maxLines)
        n = (uint32_t)maxLines;
    size_t startIdx = (recentLogBuf.head + LOG_BUFFER_LINES - (size_t)count) % LOG_BUFFER_LINES;
    for (uint32_t i = 0; i < n; ++i) {
        size_t ringIdx = (startIdx + (size_t)(from - ringStart) + (size_t)i) % LOG_BUFFER_LINES;
        strncpy(out[i], recentLogBuf.lines[ringIdx], LOG_LINE_MAX - 1);
        out[i][LOG_LINE_MAX - 1] = '\0';
    }
    *linesCopied = (size_t)n;
    taskEXIT_CRITICAL(&logMux);
    return total;
}

// Return current number of lines in the log ring buffer.
size_t getLogBufferCount() {
    taskENTER_CRITICAL(&logMux);
    size_t count = recentLogBuf.count;
    taskEXIT_CRITICAL(&logMux);
    return count;
}

// Copy the log line at logical index idx (0 = oldest) into out[outSize].
// Returns true if idx is within bounds.
bool copyLogLineAt(size_t idx, char* out, size_t outSize) {
    taskENTER_CRITICAL(&logMux);
    bool valid = idx < recentLogBuf.count;
    if (valid) {
        size_t startIdx =
            (recentLogBuf.head + LOG_BUFFER_LINES - recentLogBuf.count) % LOG_BUFFER_LINES;
        size_t ringIdx = (startIdx + idx) % LOG_BUFFER_LINES;
        strncpy(out, recentLogBuf.lines[ringIdx], outSize - 1);
        out[outSize - 1] = '\0';
    }
    taskEXIT_CRITICAL(&logMux);
    return valid;
}

// -----------------------------------------------------------------------------
// setDriveCommand() — thread-safe drive command update
// Called from RcInputTask, WebAPI handler, or safety zeroing.
// -----------------------------------------------------------------------------
void setDriveCommand(int16_t speed, int16_t steer, CommandSource src) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.driveSpeed = speed;
    robotState.driveSteer = steer;
    robotState.lastDriveSource = src;
    robotState.lastDriveCommandMs = millis();
    taskEXIT_CRITICAL(&robotStateMux);

    if (speed != 0 || steer != 0) {
        PA_LOG_INFO("DRIVE", "[%s] Drive command: speed=%d steer=%d", commandSourceToString(src),
                    speed, steer);
    }
}

// -----------------------------------------------------------------------------
// loadConfigToState() — load NVS config into robotState.cfg_* fields
// Called once at boot before tasks start.
// -----------------------------------------------------------------------------
void loadConfigToState() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    ConfigSnapshot snap;
    configLoad(prefs, &snap);
    prefs.end();

    // Copy snapshot fields into robotState.cfg_*
    // (No mutex needed here - called before tasks start)
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
    robotState.cfg_aux1_type = snap.aux1_type;
    robotState.cfg_aux2_type = snap.aux2_type;
    robotState.cfg_aux3_type = snap.aux3_type;

    robotState.cfg_aux1_open_us = snap.aux1_open_us;
    robotState.cfg_aux1_close_us = snap.aux1_close_us;
    robotState.cfg_aux2_open_us = snap.aux2_open_us;
    robotState.cfg_aux2_close_us = snap.aux2_close_us;
    robotState.cfg_aux3_open_us = snap.aux3_open_us;
    robotState.cfg_aux3_close_us = snap.aux3_close_us;

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

    // All component toggles default OFF — operator must explicitly enable each
    // connected peripheral via the Setup page. NVS overrides the default once
    // a value has been saved.
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
    robotState.activeMood = prefs.getUChar("last_mood", 0);

    // RC bindings loaded via configLoad
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

    // Tier 2 Trigger/Button bindings
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

    robotState.cfg_speedLimitMax =
        constrain(robotState.cfg_speedLimitMax, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    robotState.cfg_speedPresetSlow =
        constrain(robotState.cfg_speedPresetSlow, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    robotState.cfg_speedPresetNormal =
        constrain(robotState.cfg_speedPresetNormal, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    robotState.cfg_speedPresetTurbo =
        constrain(robotState.cfg_speedPresetTurbo, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
    // Legacy migration handled by configLoad (defaults speedPresetActive to Normal if key not found)
    robotState.cfg_sbusTimeoutMs =
        constrain(robotState.cfg_sbusTimeoutMs, (uint32_t)50, (uint32_t)5000);
    robotState.cfg_webDriveTimeoutMs =
        constrain(robotState.cfg_webDriveTimeoutMs, (uint32_t)100, (uint32_t)5000);
    robotState.cfg_audioVolume = constrain(robotState.cfg_audioVolume, (uint8_t)0, (uint8_t)30);

    robotState.cfg_arm1_open_us =
        constrain(robotState.cfg_arm1_open_us, (uint16_t)500, (uint16_t)2500);
    robotState.cfg_arm1_close_us =
        constrain(robotState.cfg_arm1_close_us, (uint16_t)500, (uint16_t)2500);
    robotState.cfg_arm2_open_us =
        constrain(robotState.cfg_arm2_open_us, (uint16_t)500, (uint16_t)2500);
    robotState.cfg_arm2_close_us =
        constrain(robotState.cfg_arm2_close_us, (uint16_t)500, (uint16_t)2500);

    if (robotState.cfg_arm1_type > SERVO_COMP_RGB)
        robotState.cfg_arm1_type = SERVO_COMP_MG996R;
    if (robotState.cfg_arm2_type > SERVO_COMP_RGB)
        robotState.cfg_arm2_type = SERVO_COMP_MG996R;
    if (robotState.cfg_aux1_type > SERVO_COMP_RGB)
        robotState.cfg_aux1_type = SERVO_COMP_NONE;
    if (robotState.cfg_aux2_type > SERVO_COMP_RGB)
        robotState.cfg_aux2_type = SERVO_COMP_NONE;
    if (robotState.cfg_aux3_type > SERVO_COMP_RGB)
        robotState.cfg_aux3_type = SERVO_COMP_NONE;

    robotState.cfg_aux1_open_us =
        constrain(robotState.cfg_aux1_open_us, (uint16_t)500, (uint16_t)2500);
    robotState.cfg_aux1_close_us =
        constrain(robotState.cfg_aux1_close_us, (uint16_t)500, (uint16_t)2500);
    robotState.cfg_aux2_open_us =
        constrain(robotState.cfg_aux2_open_us, (uint16_t)500, (uint16_t)2500);
    robotState.cfg_aux2_close_us =
        constrain(robotState.cfg_aux2_close_us, (uint16_t)500, (uint16_t)2500);
    robotState.cfg_aux3_open_us =
        constrain(robotState.cfg_aux3_open_us, (uint16_t)500, (uint16_t)2500);
    robotState.cfg_aux3_close_us =
        constrain(robotState.cfg_aux3_close_us, (uint16_t)500, (uint16_t)2500);
    if (robotState.cfg_dome_min_speed < 0.0f)
        robotState.cfg_dome_min_speed = 0.0f;
    if (robotState.cfg_dome_max_speed > 1.0f)
        robotState.cfg_dome_max_speed = 1.0f;

    if (robotState.cfg_seq_open_ms < 100)
        robotState.cfg_seq_open_ms = 100;
    if (robotState.cfg_seq_open_ms > 5000)
        robotState.cfg_seq_open_ms = 5000;
    if (robotState.cfg_seq_close_ms < 100)
        robotState.cfg_seq_close_ms = 100;
    if (robotState.cfg_seq_close_ms > 5000)
        robotState.cfg_seq_close_ms = 5000;

    robotState.cfg_dome_neutral_us =
        constrain(robotState.cfg_dome_neutral_us, (uint16_t)1000, (uint16_t)2000);
    robotState.cfg_dome_min_pulse_us =
        constrain(robotState.cfg_dome_min_pulse_us, (uint16_t)1000, (uint16_t)2000);
    robotState.cfg_dome_max_pulse_us =
        constrain(robotState.cfg_dome_max_pulse_us, (uint16_t)1000, (uint16_t)2000);
    robotState.cfg_dome_speed_limit_pct =
        constrain(robotState.cfg_dome_speed_limit_pct, (uint8_t)0, (uint8_t)100);
    if (robotState.cfg_rc_input_mode > RC_INPUT_DUAL_SBUS) {
        robotState.cfg_rc_input_mode = RC_INPUT_DUAL_SBUS;
    }
    if (!auxLedPinSettingValid(robotState.cfg_aux_led_pin)) {
        robotState.cfg_aux_led_pin = AUX_LED_PIN_DISABLED;
    }
    robotState.cfg_aux_led_count =
        constrain(robotState.cfg_aux_led_count, AUX_LED_COUNT_DEFAULT, AUX_LED_COUNT_MAX);
    if (robotState.cfg_dome_wifi_peer_ip[0] != '\0') {
        IPAddress parsedPeerIp;
        if (!parsedPeerIp.fromString(robotState.cfg_dome_wifi_peer_ip)) {
            robotState.cfg_dome_wifi_peer_ip[0] = '\0';
        }
    }

    RcBindingConfig* bindings[] = {
        &robotState.cfg_rc_pwm_drive_speed,  &robotState.cfg_rc_pwm_drive_steer,
        &robotState.cfg_rc_pwm_arm1,         &robotState.cfg_rc_pwm_arm2,
        &robotState.cfg_rc_pwm_sound,        &robotState.cfg_rc_sbus_drive_speed,
        &robotState.cfg_rc_sbus_dome_speed,  &robotState.cfg_rc_sbus_arm1,
        &robotState.cfg_rc_sbus_arm2,        &robotState.cfg_rc_sbus_sound,
    };
    const RcBindingConfig defaults[] = {
        defaultPwmBinding(1),
        defaultPwmBinding(2),
        defaultPwmBinding(3),
        defaultPwmBinding(4),
        defaultPwmBinding(5),
        defaultPwmBinding(6),
        defaultSbusBinding(RC_BINDING_SBUS1, 1),
        defaultSbusBinding(RC_BINDING_SBUS1, 2),
        defaultSbusBinding(RC_BINDING_SBUS2, 1),
        defaultSbusBinding(RC_BINDING_SBUS2, 2),
        defaultSbusBinding(RC_BINDING_SBUS2, 3),
        disabledRcBinding(),
    };
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
        if (!rcBindingIsValid(*bindings[i])) {
            *bindings[i] = defaults[i];
        }
    }

    // Validate Tier 2 Trigger bindings
    RcTriggerBinding* triggerBindings[] = {
        &robotState.cfg_rc_arm1,   &robotState.cfg_rc_arm2,  &robotState.cfg_rc_aux1,
        &robotState.cfg_rc_aux2,   &robotState.cfg_rc_aux3,  &robotState.cfg_rc_sound,
        &robotState.cfg_rc_opmode, &robotState.cfg_rc_free0, &robotState.cfg_rc_free1,
        &robotState.cfg_rc_free2,  &robotState.cfg_rc_free3,
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

    // Initialize runtime state from config
    robotState.stationary = robotState.cfg_stationary;
}

bool saveConfigToNvs() {
    ConfigSnapshot snap;

    // Capture current config under critical section
    taskENTER_CRITICAL(&robotStateMux);
    snap.speedLimitMax = robotState.cfg_speedLimitMax;
    snap.speedPresetSlow = robotState.cfg_speedPresetSlow;
    snap.speedPresetNormal = robotState.cfg_speedPresetNormal;
    snap.speedPresetTurbo = robotState.cfg_speedPresetTurbo;
    snap.speedPresetActive = robotState.cfg_speedPresetActive;
    snap.sbusTimeoutMs = robotState.cfg_sbusTimeoutMs;
    snap.webDriveTimeoutMs = robotState.cfg_webDriveTimeoutMs;
    snap.audioVolume = robotState.cfg_audioVolume;
    snap.logLevel = robotState.cfg_logLevel;
    snap.snd_scream = robotState.cfg_snd_scream;
    snap.snd_faint = robotState.cfg_snd_faint;
    snap.snd_leia = robotState.cfg_snd_leia;
    snap.snd_cantina_s = robotState.cfg_snd_cantina_s;
    snap.snd_sw_theme = robotState.cfg_snd_sw_theme;
    snap.snd_imp_march = robotState.cfg_snd_imp_march;
    snap.snd_cantina_l = robotState.cfg_snd_cantina_l;
    snap.snd_startup = robotState.cfg_snd_startup;
    snap.snd_doodoo = robotState.cfg_snd_doodoo;
    snap.snd_failure = robotState.cfg_snd_failure;
    snap.snd_disco = robotState.cfg_snd_disco;
    snap.snd_mahna = robotState.cfg_snd_mahna;
    snap.snd_inlove = robotState.cfg_snd_inlove;
    snap.snd_macho = robotState.cfg_snd_macho;
    snap.snd_gangnam = robotState.cfg_snd_gangnam;
    snap.snd_uptown = robotState.cfg_snd_uptown;
    snap.snd_celebr = robotState.cfg_snd_celebr;
    snap.snd_stayin = robotState.cfg_snd_stayin;
    snap.snd_harlem = robotState.cfg_snd_harlem;
    snap.snd_pbjtime = robotState.cfg_snd_pbjtime;
    snap.snd_sys_boot = robotState.cfg_snd_sys_boot;
    snap.snd_sys_mode_n = robotState.cfg_snd_sys_mode_n;
    snap.snd_sys_mode_s = robotState.cfg_snd_sys_mode_s;
    snap.snd_sys_mode_t = robotState.cfg_snd_sys_mode_t;
    snap.snd_sys_drv_on = robotState.cfg_snd_sys_drv_on;
    snap.snd_sys_dome_on = robotState.cfg_snd_sys_dome_on;
    snap.snd_rand_min = robotState.cfg_snd_rand_min;
    snap.snd_rand_max = robotState.cfg_snd_rand_max;
    snap.snd_int_quiet = robotState.cfg_snd_int_quiet;
    snap.snd_int_mid = robotState.cfg_snd_int_mid;
    snap.snd_int_full = robotState.cfg_snd_int_full;
    snap.snd_int_awake = robotState.cfg_snd_int_awake;
    snap.snd_moodcat_quiet = robotState.cfg_snd_moodcat_quiet;
    snap.snd_moodcat_mid = robotState.cfg_snd_moodcat_mid;
    snap.snd_moodcat_full = robotState.cfg_snd_moodcat_full;
    snap.snd_moodcat_awakeplus = robotState.cfg_snd_moodcat_awakeplus;
    snap.snd_cat_gen_lo = robotState.cfg_snd_cat_gen_lo;
    snap.snd_cat_gen_hi = robotState.cfg_snd_cat_gen_hi;
    snap.snd_cat_chat_lo = robotState.cfg_snd_cat_chat_lo;
    snap.snd_cat_chat_hi = robotState.cfg_snd_cat_chat_hi;
    snap.snd_cat_hap_lo = robotState.cfg_snd_cat_hap_lo;
    snap.snd_cat_hap_hi = robotState.cfg_snd_cat_hap_hi;
    snap.snd_cat_proc_lo = robotState.cfg_snd_cat_proc_lo;
    snap.snd_cat_proc_hi = robotState.cfg_snd_cat_proc_hi;
    snap.snd_cat_sad_lo = robotState.cfg_snd_cat_sad_lo;
    snap.snd_cat_sad_hi = robotState.cfg_snd_cat_sad_hi;
    snap.snd_cat_sent_lo = robotState.cfg_snd_cat_sent_lo;
    snap.snd_cat_sent_hi = robotState.cfg_snd_cat_sent_hi;
    snap.snd_cat_hum_lo = robotState.cfg_snd_cat_hum_lo;
    snap.snd_cat_hum_hi = robotState.cfg_snd_cat_hum_hi;
    snap.snd_cat_scrm_lo = robotState.cfg_snd_cat_scrm_lo;
    snap.snd_cat_scrm_hi = robotState.cfg_snd_cat_scrm_hi;
    snap.snd_cat_ooh_lo = robotState.cfg_snd_cat_ooh_lo;
    snap.snd_cat_ooh_hi = robotState.cfg_snd_cat_ooh_hi;
    snap.snd_cat_alrm_lo = robotState.cfg_snd_cat_alrm_lo;
    snap.snd_cat_alrm_hi = robotState.cfg_snd_cat_alrm_hi;
    snap.snd_cat_snarky_lo = robotState.cfg_snd_cat_snarky_lo;
    snap.snd_cat_snarky_hi = robotState.cfg_snd_cat_snarky_hi;
    snap.snd_cat_whis_lo = robotState.cfg_snd_cat_whis_lo;
    snap.snd_cat_whis_hi = robotState.cfg_snd_cat_whis_hi;
    snap.arm1_open_us = robotState.cfg_arm1_open_us;
    snap.arm1_close_us = robotState.cfg_arm1_close_us;
    snap.arm2_open_us = robotState.cfg_arm2_open_us;
    snap.arm2_close_us = robotState.cfg_arm2_close_us;
    snap.arm1_type = robotState.cfg_arm1_type;
    snap.arm2_type = robotState.cfg_arm2_type;
    snap.aux1_type = robotState.cfg_aux1_type;
    snap.aux2_type = robotState.cfg_aux2_type;
    snap.aux3_type = robotState.cfg_aux3_type;
    snap.aux1_open_us = robotState.cfg_aux1_open_us;
    snap.aux1_close_us = robotState.cfg_aux1_close_us;
    snap.aux2_open_us = robotState.cfg_aux2_open_us;
    snap.aux2_close_us = robotState.cfg_aux2_close_us;
    snap.aux3_open_us = robotState.cfg_aux3_open_us;
    snap.aux3_close_us = robotState.cfg_aux3_close_us;
    snap.dome_min_speed = robotState.cfg_dome_min_speed;
    snap.dome_max_speed = robotState.cfg_dome_max_speed;
    snap.dome_neutral_us = robotState.cfg_dome_neutral_us;
    snap.dome_min_pulse_us = robotState.cfg_dome_min_pulse_us;
    snap.dome_max_pulse_us = robotState.cfg_dome_max_pulse_us;
    snap.dome_speed_limit_pct = robotState.cfg_dome_speed_limit_pct;
    snap.dome_rnd_enable = robotState.cfg_dome_rnd_enable;
    snap.dome_rnd_speed_pct = robotState.cfg_dome_rnd_speed_pct;
    snap.dome_rnd_pause_min = robotState.cfg_dome_rnd_pause_min;
    snap.dome_rnd_pause_max = robotState.cfg_dome_rnd_pause_max;
    snap.dome_rnd_move_ms = robotState.cfg_dome_rnd_move_ms;
    snap.seq_open_ms = robotState.cfg_seq_open_ms;
    snap.seq_close_ms = robotState.cfg_seq_close_ms;
    snap.rc_input_mode = robotState.cfg_rc_input_mode;
    snap.enable_arm1 = robotState.cfg_enable_arm1;
    snap.enable_arm2 = robotState.cfg_enable_arm2;
    snap.enable_aux1 = robotState.cfg_enable_aux1;
    snap.enable_aux2 = robotState.cfg_enable_aux2;
    snap.enable_aux3 = robotState.cfg_enable_aux3;
    snap.enable_dome = robotState.cfg_enable_dome;
    snap.enable_rc_ch1 = robotState.cfg_enable_rc_ch1;
    snap.enable_rc_ch2 = robotState.cfg_enable_rc_ch2;
    snap.enable_rc_ch3 = robotState.cfg_enable_rc_ch3;
    snap.enable_rc_ch4 = robotState.cfg_enable_rc_ch4;
    snap.enable_rc_ch5 = robotState.cfg_enable_rc_ch5;
    snap.enable_rc_ch6 = robotState.cfg_enable_rc_ch6;
    snap.single_sbus_use_ch2 = robotState.cfg_single_sbus_use_ch2;
    snap.enable_s1_hoverboard = robotState.cfg_enable_s1_hoverboard;
    snap.enable_s2_sound = robotState.cfg_enable_s2_sound;
    snap.enable_s3_dome_ctrl = robotState.cfg_enable_s3_dome_ctrl;
    snap.stationary = robotState.cfg_stationary;
    snap.aux_led_pin = robotState.cfg_aux_led_pin;
    snap.aux_led_count = robotState.cfg_aux_led_count;
    snprintf(snap.dome_wifi_peer_ip, sizeof(snap.dome_wifi_peer_ip), "%s",
             robotState.cfg_dome_wifi_peer_ip);
    snap.rc_pwm_drive_speed = robotState.cfg_rc_pwm_drive_speed;
    snap.rc_pwm_drive_steer = robotState.cfg_rc_pwm_drive_steer;
    snap.rc_pwm_dome_speed = robotState.cfg_rc_pwm_dome_speed;
    snap.rc_pwm_arm1 = robotState.cfg_rc_pwm_arm1;
    snap.rc_pwm_arm2 = robotState.cfg_rc_pwm_arm2;
    snap.rc_pwm_sound = robotState.cfg_rc_pwm_sound;
    snap.rc_sbus_drive_speed = robotState.cfg_rc_sbus_drive_speed;
    snap.rc_sbus_drive_steer = robotState.cfg_rc_sbus_drive_steer;
    snap.rc_sbus_dome_speed = robotState.cfg_rc_sbus_dome_speed;
    snap.rc_sbus_arm1 = robotState.cfg_rc_sbus_arm1;
    snap.rc_sbus_arm2 = robotState.cfg_rc_sbus_arm2;
    snap.rc_sbus_sound = robotState.cfg_rc_sbus_sound;
    snap.rc_arm1 = robotState.cfg_rc_arm1;
    snap.rc_arm2 = robotState.cfg_rc_arm2;
    snap.rc_aux1 = robotState.cfg_rc_aux1;
    snap.rc_aux2 = robotState.cfg_rc_aux2;
    snap.rc_aux3 = robotState.cfg_rc_aux3;
    snap.rc_sound = robotState.cfg_rc_sound;
    snap.rc_opmode = robotState.cfg_rc_opmode;
    snap.rc_free0 = robotState.cfg_rc_free0;
    snap.rc_free1 = robotState.cfg_rc_free1;
    snap.rc_free2 = robotState.cfg_rc_free2;
    snap.rc_free3 = robotState.cfg_rc_free3;
    taskEXIT_CRITICAL(&robotStateMux);

    // Persist snapshot via configSave
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    bool ok = configSave(prefs, snap);
    prefs.end();
    return ok;
}

void requestSystemRestart(uint32_t delayMs) {
    taskENTER_CRITICAL(&restartMux);
    restartRequested = true;
    restartAtMs = millis() + delayMs;
    taskEXIT_CRITICAL(&restartMux);
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(false);
    delay(200);

    // Audio module state: 0xFF = "unknown/none" until AudioTask runs its init
    // query. Zero-init would show "USB" (0x00) before any query succeeds.
    robotState.audio_module_device = 0xFF;
    robotState.audio_module_play_state = 0xFF;
    // Pre-init log level to compile-time default so any log calls added before
    // loadConfigToState() in the future are not silently dropped (cfg_logLevel
    // is zero-initialized by robotState = {} which would suppress all output).
    robotState.cfg_logLevel = PA_LOG_LEVEL;

    // Load config from NVS — may override cfg_logLevel with the user's saved value.
    loadConfigToState();
    logBootHealth();

    // Layer 4: Initialize Task Watchdog Timer
    // IDF 5.x: esp_task_wdt_init() takes a config struct (timeout_ms, idle_core_mask,
    // trigger_panic). IDF 4.x took (timeout_seconds, trigger_panic) directly.
    // idle_core_mask=0: do not subscribe idle tasks; only DriveTask subscribes itself.
    const esp_task_wdt_config_t twdt_config = {
        .timeout_ms    = WATCHDOG_TIMEOUT_S * 1000U,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_task_wdt_init(&twdt_config);

    // Initialize FailsafeGate before task creation
    failsafeInit(&robotStateMux);

    // Safety: boot with drive locked until SBUS confirmed
    // Use FailsafeGate's SBUS_WATCHDOG layer; RcInputTask will clear when frames arrive
    failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);

    // Detect TWDT reset from previous boot — set estop so robot does not move
    // until operator explicitly clears via POST /api/estop/clear
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (resetReason == ESP_RST_TASK_WDT) {
        failsafeTrigger(FailsafeLayer::TWDT_RESET);
        PA_LOG_ERROR("main", "task watchdog reset detected - estop set");
    }

    // Create command queues
    servoCmdQueue = xQueueCreate(8, sizeof(ServoCommand));
    domeCmdQueue = xQueueCreate(8, sizeof(DomeCommand));
    audioCmdQueue = xQueueCreate(8, sizeof(AudioCommand));
    domeTxQueue = xQueueCreate(16, sizeof(DomeTxCmd));

    // ServoTask owns LEDC hardware init and applies AUX LED channel skip policy.
    servoTaskInit();
    domeTaskInit();
    bool auxLedTaskReady = auxLedTaskInit();
    if (!auxLedTaskReady) {
        PA_LOG_WARN("main", "aux LED task init failed; AUX LED API will report unavailable");
    }

    // Launch real-time tasks on Core 1
    // DriveTask: 50 Hz hoverboard frames, feeds TWDT, Layer 3 web timeout
    // RcInputTask: ~200 Hz RC poll (all modes), Layer 1+2 failsafe
    // ServoTask: 50 Hz servo PWM updates
    // DomeTask: 50 Hz ESC PWM updates
    xTaskCreatePinnedToCore(driveTask, "DriveTask", 2560, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(rcInputTask, "RCInputTask", 4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(
        servoTask, "ServoTask", 3072, nullptr, 4, nullptr,
        1);  // HWM: ~728 B used; was 5120 (oversized for string formatting assumption)
    xTaskCreatePinnedToCore(domeTask, "DomeTask", 3072, nullptr, 4, nullptr,
                            1);  // T24 R1: profiler HWM reached 108 B free at 2048 B.

    // AudioTask: Core 0 (non-RT) — software bit-bang TX blocks ~6 ms per command;
    // keeping off Core 1 avoids any interaction with DriveTask / ServoTask timing.
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, nullptr, 3, nullptr, 0);

    // AuxLedTask: Core 0 (non-RT) - WS2812B effects and API-driven color/effect updates.
    // Runs independently of Core 1 control loops.
    if (auxLedTaskReady) {
        xTaskCreatePinnedToCore(auxLedTask, "AuxLedTask", 3072, nullptr, 2, nullptr, 0);
    }

    // DomeLinkTask: Core 1 — bidirectional Marcduino serial to AstroPixelsPlus.
    // UART2 TX/RX are non-blocking hardware operations; Core 1 at priority 3.
    // 4096: profiler measured 988 B free at 3072 B without WiFi fallback active;
    // HTTPClient call-chain in sendCommandOverWifi needs 3 KB+ of stack headroom.
    xTaskCreatePinnedToCore(domeLinkTask, "DomeLinkTask", 4096, nullptr, 3, nullptr, 1);

    // SafetyMonitorTask: 10 Hz audit on Core 0 (non-RT, low priority).
    // HWM first-iteration: 476 B free — WARN path allocates 128 B format buffer +
    // printf; bumped to 3072 to ensure adequate headroom for all log paths.
    xTaskCreatePinnedToCore(safetyMonitorTask, "SafetyMonitor", 3072, nullptr, 2, nullptr, 0);

    // Restore last mood — audio component only.
    // - Dome link is not yet established at boot, so dome TX is intentionally skipped.
    // - We apply audio directly here rather than via applyMood() to avoid writing
    //   last_mood back to NVS (we just read it; the value has not changed).
    if (robotState.activeMood != 0) {
        const char* bootAudioCmd = moodAudioCommand(robotState.activeMood);
        if (bootAudioCmd) {
            audioQueueDollar(bootAudioCmd, SRC_INTERNAL);
            PA_LOG_INFO("main", "boot mood restore: SE%u -> %s", (unsigned)robotState.activeMood,
                        bootAudioCmd);
        }
    }

    // Start WiFi AP and web server
    webServerInit();

    uint16_t bootTrack = 0;
    taskENTER_CRITICAL(&robotStateMux);
    bootTrack = robotState.cfg_snd_sys_boot;
    taskEXIT_CRITICAL(&robotStateMux);
    if (bootTrack != 0) {
        if (audioQueuePlaySlot(AUDIO_SLOT_SYS_BOOT, SRC_INTERNAL)) {
            PA_LOG_INFO("main", "system boot sound queued");
        } else {
            PA_LOG_WARN("main", "system boot sound queue full");
        }
    }

    PA_LOG_INFO("main", "init complete");
    Serial.flush();
}

void loop() {
    bool shouldRestart = false;

    taskENTER_CRITICAL(&restartMux);
    if (restartRequested && (int32_t)(millis() - restartAtMs) >= 0) {
        shouldRestart = true;
    }
    taskEXIT_CRITICAL(&restartMux);

    if (shouldRestart) {
        PA_LOG_INFO("main", "restarting controller");
        Serial.flush();
        delay(100);
        ESP.restart();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
}
