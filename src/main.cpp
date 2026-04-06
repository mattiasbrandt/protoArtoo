// =============================================================================
// src/main.cpp
//
// protoArtoo — ESP32 body controller for MK4 astromech droid.
// Boot sequence — Phase 1 stub.
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#include "audio_dollar_parser.h"
#include "audio_task.h"
#include "aux_led.h"
#include "dome_link.h"
#include "dome_task.h"
#include "drive.h"
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
                "config speed_limit_max=%d sbus_timeout_ms=%lu web_timeout_ms=%lu ch8_mode_lock=%s "
                "audio_volume=%u",
                robotState.cfg_speedLimitMax, (unsigned long)robotState.cfg_sbusTimeoutMs,
                (unsigned long)robotState.cfg_webDriveTimeoutMs,
                robotState.cfg_ch8ModeLock ? "true" : "false", robotState.cfg_audioVolume);
    PA_LOG_DEBUG("main", "heap_free=%lu", (unsigned long)ESP.getFreeHeap());
}

}  // namespace

void paLogLine(const char* level, const char* message) {
    char line[LOG_LINE_MAX];
    snprintf(line, sizeof(line), "[%s] %s", level, message);

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
bool domeConnected() {
    taskENTER_CRITICAL(&robotStateMux);
    uint32_t lastSeen = robotState.domeLastSeenMs;
    taskEXIT_CRITICAL(&robotStateMux);
    return lastSeen > 0 && (millis() - lastSeen) < 5000;
}

// -----------------------------------------------------------------------------
// loadConfigToState() — load NVS config into robotState.cfg_* fields
// Called once at boot before tasks start.
// -----------------------------------------------------------------------------
void loadConfigToState() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    robotState.cfg_speedLimitMax = prefs.getShort("spd_max", SPEED_LIMIT_MAX);
    robotState.cfg_sbusTimeoutMs = prefs.getULong("sbus_tmo", SBUS_TIMEOUT_MS);
    robotState.cfg_webDriveTimeoutMs = prefs.getULong("web_tmo", WEB_DRIVE_TIMEOUT_MS);
    robotState.cfg_ch8ModeLock = prefs.getBool("ch8_lock", false);
    robotState.cfg_audioVolume = (uint8_t)prefs.getUChar("aud_vol", 20);
    robotState.cfg_logLevel = (uint8_t)prefs.getUChar("log_level", PA_LOG_LEVEL);
    robotState.cfg_snd_scream = prefs.getUShort("snd_scream", AUDIO_TRACK_SCREAM);
    robotState.cfg_snd_faint = prefs.getUShort("snd_faint", AUDIO_TRACK_FAINT);
    robotState.cfg_snd_leia = prefs.getUShort("snd_leia", AUDIO_TRACK_LEIA);
    robotState.cfg_snd_cantina_s = prefs.getUShort("snd_cantina_s", AUDIO_TRACK_CANTINA_S);
    robotState.cfg_snd_sw_theme = prefs.getUShort("snd_sw", AUDIO_TRACK_SW_THEME);
    robotState.cfg_snd_imp_march = prefs.getUShort("snd_march", AUDIO_TRACK_IMP_MARCH);
    robotState.cfg_snd_cantina_l = prefs.getUShort("snd_cantina_l", AUDIO_TRACK_CANTINA_L);
    robotState.cfg_snd_startup = prefs.getUShort("snd_startup", AUDIO_TRACK_STARTUP);
    robotState.cfg_snd_doodoo = prefs.getUShort("snd_doodoo", 0);
    robotState.cfg_snd_failure = prefs.getUShort("snd_failure", 0);
    robotState.cfg_snd_disco = prefs.getUShort("snd_disco", 0);
    robotState.cfg_snd_mahna = prefs.getUShort("snd_mahna", 0);
    robotState.cfg_snd_inlove = prefs.getUShort("snd_inlove", 0);
    robotState.cfg_snd_macho = prefs.getUShort("snd_macho", 0);
    robotState.cfg_snd_gangnam = prefs.getUShort("snd_gangnam", 0);
    robotState.cfg_snd_uptown = prefs.getUShort("snd_uptown", 0);
    robotState.cfg_snd_celebr = prefs.getUShort("snd_celebr", 0);
    robotState.cfg_snd_stayin = prefs.getUShort("snd_stayin", 0);
    robotState.cfg_snd_harlem = prefs.getUShort("snd_harlem", 0);
    robotState.cfg_snd_pbjtime = prefs.getUShort("snd_pbjtime", 0);
    robotState.cfg_snd_sys_boot = prefs.getUShort("snd_sys_boot", 0);
    robotState.cfg_snd_sys_mode_n = prefs.getUShort("snd_sys_mode_n", 0);
    robotState.cfg_snd_sys_mode_s = prefs.getUShort("snd_sys_mode_s", 0);
    robotState.cfg_snd_sys_mode_t = prefs.getUShort("snd_sys_mode_t", 0);
    robotState.cfg_snd_sys_drv_on = prefs.getUShort("snd_sys_drv_on", 0);
    robotState.cfg_snd_sys_dome_on = prefs.getUShort("snd_sys_dome_on", 0);
    robotState.cfg_snd_rand_min = prefs.getUShort("snd_rand_min", AUDIO_RAND_TRACK_MIN);
    robotState.cfg_snd_rand_max = prefs.getUShort("snd_rand_max", AUDIO_RAND_TRACK_MAX);
    robotState.cfg_snd_int_quiet = constrain(prefs.getUShort("snd_int_quiet", AUDIO_RAND_INT_QUIET),
                                             (uint16_t)0, (uint16_t)3600);
    robotState.cfg_snd_int_mid =
        constrain(prefs.getUShort("snd_int_mid", AUDIO_RAND_INT_MID), (uint16_t)0, (uint16_t)3600);
    robotState.cfg_snd_int_full = constrain(prefs.getUShort("snd_int_full", AUDIO_RAND_INT_FULL),
                                            (uint16_t)0, (uint16_t)3600);
    robotState.cfg_snd_int_awake = constrain(prefs.getUShort("snd_int_awake", AUDIO_RAND_INT_AWAKE),
                                             (uint16_t)0, (uint16_t)3600);
    robotState.cfg_snd_moodcat_quiet =
        constrain(prefs.getUShort("snd_moodcat_q", 0x0048), (uint16_t)0, (uint16_t)0x0FFF);
    robotState.cfg_snd_moodcat_mid =
        constrain(prefs.getUShort("snd_moodcat_m", 0x004F), (uint16_t)0, (uint16_t)0x0FFF);
    robotState.cfg_snd_moodcat_full =
        constrain(prefs.getUShort("snd_moodcat_f", 0x090F), (uint16_t)0, (uint16_t)0x0FFF);
    robotState.cfg_snd_moodcat_awakeplus =
        constrain(prefs.getUShort("snd_moodcat_a", 0x0F8F), (uint16_t)0, (uint16_t)0x0FFF);
    robotState.cfg_snd_cat_gen_lo = prefs.getUShort("snd_cat_gen_lo", 0);
    robotState.cfg_snd_cat_gen_hi = prefs.getUShort("snd_cat_gen_hi", 0);
    robotState.cfg_snd_cat_chat_lo = prefs.getUShort("snd_cat_chat_lo", 0);
    robotState.cfg_snd_cat_chat_hi = prefs.getUShort("snd_cat_chat_hi", 0);
    robotState.cfg_snd_cat_hap_lo = prefs.getUShort("snd_cat_hap_lo", 0);
    robotState.cfg_snd_cat_hap_hi = prefs.getUShort("snd_cat_hap_hi", 0);
    robotState.cfg_snd_cat_proc_lo = prefs.getUShort("snd_cat_proc_lo", 0);
    robotState.cfg_snd_cat_proc_hi = prefs.getUShort("snd_cat_proc_hi", 0);
    robotState.cfg_snd_cat_sad_lo = prefs.getUShort("snd_cat_sad_lo", 0);
    robotState.cfg_snd_cat_sad_hi = prefs.getUShort("snd_cat_sad_hi", 0);
    robotState.cfg_snd_cat_sent_lo = prefs.getUShort("snd_cat_sent_lo", 0);
    robotState.cfg_snd_cat_sent_hi = prefs.getUShort("snd_cat_sent_hi", 0);
    robotState.cfg_snd_cat_hum_lo = prefs.getUShort("snd_cat_hum_lo", 0);
    robotState.cfg_snd_cat_hum_hi = prefs.getUShort("snd_cat_hum_hi", 0);
    robotState.cfg_snd_cat_scrm_lo = prefs.getUShort("snd_cat_scrm_lo", 0);
    robotState.cfg_snd_cat_scrm_hi = prefs.getUShort("snd_cat_scrm_hi", 0);
    robotState.cfg_snd_cat_ooh_lo = prefs.getUShort("snd_cat_ooh_lo", 0);
    robotState.cfg_snd_cat_ooh_hi = prefs.getUShort("snd_cat_ooh_hi", 0);
    robotState.cfg_snd_cat_alrm_lo = prefs.getUShort("snd_cat_alrm_lo", 0);
    robotState.cfg_snd_cat_alrm_hi = prefs.getUShort("snd_cat_alrm_hi", 0);
    robotState.cfg_snd_cat_snarky_lo = prefs.getUShort("snd_cat_snrk_lo", 0);
    robotState.cfg_snd_cat_snarky_hi = prefs.getUShort("snd_cat_snrk_hi", 0);
    robotState.cfg_snd_cat_whis_lo = prefs.getUShort("snd_cat_whis_lo", 0);
    robotState.cfg_snd_cat_whis_hi = prefs.getUShort("snd_cat_whis_hi", 0);

    robotState.cfg_arm1_open_us = prefs.getUShort("arm1_op", 2000);
    robotState.cfg_arm1_close_us = prefs.getUShort("arm1_cl", 1000);
    robotState.cfg_arm2_open_us = prefs.getUShort("arm2_op", 2000);
    robotState.cfg_arm2_close_us = prefs.getUShort("arm2_cl", 1000);

    robotState.cfg_arm1_type = (ServoComponentType)prefs.getUChar("arm1_type", SERVO_COMP_MG996R);
    robotState.cfg_arm2_type = (ServoComponentType)prefs.getUChar("arm2_type", SERVO_COMP_MG996R);
    robotState.cfg_aux1_type = (ServoComponentType)prefs.getUChar("aux1_type", SERVO_COMP_NONE);
    robotState.cfg_aux2_type = (ServoComponentType)prefs.getUChar("aux2_type", SERVO_COMP_NONE);
    robotState.cfg_aux3_type = (ServoComponentType)prefs.getUChar("aux3_type", SERVO_COMP_NONE);

    robotState.cfg_aux1_open_us = prefs.getUShort("aux1_op", 2000);
    robotState.cfg_aux1_close_us = prefs.getUShort("aux1_cl", 1000);
    robotState.cfg_aux2_open_us = prefs.getUShort("aux2_op", 2000);
    robotState.cfg_aux2_close_us = prefs.getUShort("aux2_cl", 1000);
    robotState.cfg_aux3_open_us = prefs.getUShort("aux3_op", 2000);
    robotState.cfg_aux3_close_us = prefs.getUShort("aux3_cl", 1000);

    union {
        float f;
        uint32_t u;
    } dome_conv;
    dome_conv.u = prefs.getULong("dome_min", 0);
    robotState.cfg_dome_min_speed = dome_conv.f;
    dome_conv.u = prefs.getULong("dome_max", 0x3F800000);
    robotState.cfg_dome_max_speed = dome_conv.f;

    robotState.cfg_seq_open_ms = prefs.getUShort("seq_op", 1000);
    robotState.cfg_seq_close_ms = prefs.getUShort("seq_cl", 1000);

    robotState.cfg_dome_neutral_us = prefs.getUShort("dome_neu", 1500);
    robotState.cfg_dome_min_pulse_us = prefs.getUShort("dome_minp", 1000);
    robotState.cfg_dome_max_pulse_us = prefs.getUShort("dome_maxp", 2000);
    robotState.cfg_dome_speed_limit_pct = prefs.getUChar("dome_pct", 100);
    robotState.cfg_rc_input_mode = (RcInputMode)prefs.getUChar("rc_mode", RC_INPUT_DUAL_SBUS);

    // All component toggles default OFF — operator must explicitly enable each
    // connected peripheral via the Setup page. NVS overrides the default once
    // a value has been saved.
    robotState.cfg_enable_arm1 = prefs.getBool("en_arm1", false);
    robotState.cfg_enable_arm2 = prefs.getBool("en_arm2", false);
    robotState.cfg_enable_aux1 = prefs.getBool("en_aux1", false);
    robotState.cfg_enable_aux2 = prefs.getBool("en_aux2", false);
    robotState.cfg_enable_aux3 = prefs.getBool("en_aux3", false);
    robotState.cfg_enable_dome = prefs.getBool("en_dome", false);
    robotState.cfg_enable_rc_ch1 = prefs.getBool("en_rc_ch1", false);
    robotState.cfg_enable_rc_ch2 = prefs.getBool("en_rc_ch2", false);
    robotState.cfg_enable_rc_ch3 = prefs.getBool("en_rc_ch3", false);
    robotState.cfg_enable_rc_ch4 = prefs.getBool("en_rc_ch4", false);
    robotState.cfg_enable_rc_ch5 = prefs.getBool("en_rc_ch5", false);
    robotState.cfg_enable_rc_ch6 = prefs.getBool("en_rc_ch6", false);
    robotState.cfg_single_sbus_use_ch2 = prefs.getBool("sbus_recv_ch2", false);
    robotState.cfg_enable_s1_hoverboard = prefs.getBool("en_s1", false);
    robotState.cfg_enable_s2_sound = prefs.getBool("en_s2", false);
    robotState.cfg_enable_s3_dome_ctrl = prefs.getBool("en_s3", false);
    robotState.cfg_stationary = prefs.getBool("op_mode", false);
    robotState.cfg_aux_led_pin = prefs.getUChar(NVS_KEY_AUX_LED_PIN, AUX_LED_PIN_DISABLED);
    robotState.cfg_aux_led_count = prefs.getUChar(NVS_KEY_AUX_LED_COUNT, AUX_LED_COUNT_DEFAULT);
    robotState.activeMood = prefs.getUChar("last_mood", 0);

    RcBindingNvsSpec bindingSpecs[] = {
        {"rcp_drv", &robotState.cfg_rc_pwm_drive_speed, defaultPwmBinding(1)},
        {"rcp_str", &robotState.cfg_rc_pwm_drive_steer, defaultPwmBinding(2)},
        {"rcp_lim", &robotState.cfg_rc_pwm_drive_limit, disabledRcBinding()},
        {"rcp_dom", &robotState.cfg_rc_pwm_dome_speed, defaultPwmBinding(3)},
        {"rcp_a1", &robotState.cfg_rc_pwm_arm1, defaultPwmBinding(4)},
        {"rcp_a2", &robotState.cfg_rc_pwm_arm2, defaultPwmBinding(5)},
        {"rcp_snd", &robotState.cfg_rc_pwm_sound, defaultPwmBinding(6)},
        {"rcs_drv", &robotState.cfg_rc_sbus_drive_speed, defaultSbusBinding(RC_BINDING_SBUS1, 1)},
        {"rcs_str", &robotState.cfg_rc_sbus_drive_steer, defaultSbusBinding(RC_BINDING_SBUS1, 2)},
        {"rcs_lim", &robotState.cfg_rc_sbus_drive_limit, defaultSbusBinding(RC_BINDING_SBUS1, 8)},
        {"rcs_dom", &robotState.cfg_rc_sbus_dome_speed, defaultSbusBinding(RC_BINDING_SBUS2, 1)},
        {"rcs_a1", &robotState.cfg_rc_sbus_arm1, defaultSbusBinding(RC_BINDING_SBUS2, 2)},
        {"rcs_a2", &robotState.cfg_rc_sbus_arm2, defaultSbusBinding(RC_BINDING_SBUS2, 3)},
        {"rcs_snd", &robotState.cfg_rc_sbus_sound, disabledRcBinding()},
    };

    for (size_t i = 0; i < sizeof(bindingSpecs) / sizeof(bindingSpecs[0]); ++i) {
        loadRcBindingFromPrefs(prefs, bindingSpecs[i].key, bindingSpecs[i].defaultValue,
                               bindingSpecs[i].binding);
    }

    // Tier 2 Trigger/Button bindings
    RcTriggerBindingNvsSpec triggerSpecs[] = {
        {"rc_arm1", &robotState.cfg_rc_arm1,
         makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                              RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX, 0,
                              false)},
        {"rc_arm2", &robotState.cfg_rc_arm2,
         makeRcTriggerBinding(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM2_TOGGLE, nullptr,
                              RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX, 0,
                              false)},
        {"rc_aux1", &robotState.cfg_rc_aux1, disabledRcTriggerBinding()},
        {"rc_aux2", &robotState.cfg_rc_aux2, disabledRcTriggerBinding()},
        {"rc_aux3", &robotState.cfg_rc_aux3, disabledRcTriggerBinding()},
        {"rc_sound", &robotState.cfg_rc_sound, disabledRcTriggerBinding()},
        {"rc_opmode", &robotState.cfg_rc_opmode, disabledRcTriggerBinding()},
        {"rc_free0", &robotState.cfg_rc_free0, disabledRcTriggerBinding()},
        {"rc_free1", &robotState.cfg_rc_free1, disabledRcTriggerBinding()},
        {"rc_free2", &robotState.cfg_rc_free2, disabledRcTriggerBinding()},
        {"rc_free3", &robotState.cfg_rc_free3, disabledRcTriggerBinding()},
    };

    for (size_t i = 0; i < sizeof(triggerSpecs) / sizeof(triggerSpecs[0]); ++i) {
        loadRcTriggerBindingFromPrefs(prefs, triggerSpecs[i].key, triggerSpecs[i].defaultValue,
                                      triggerSpecs[i].binding);
    }

    prefs.end();

    robotState.cfg_speedLimitMax =
        constrain(robotState.cfg_speedLimitMax, (int16_t)0, (int16_t)SPEED_LIMIT_MAX);
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

    RcBindingConfig* bindings[] = {
        &robotState.cfg_rc_pwm_drive_speed,  &robotState.cfg_rc_pwm_drive_steer,
        &robotState.cfg_rc_pwm_drive_limit,  &robotState.cfg_rc_pwm_dome_speed,
        &robotState.cfg_rc_pwm_arm1,         &robotState.cfg_rc_pwm_arm2,
        &robotState.cfg_rc_pwm_sound,        &robotState.cfg_rc_sbus_drive_speed,
        &robotState.cfg_rc_sbus_drive_steer, &robotState.cfg_rc_sbus_drive_limit,
        &robotState.cfg_rc_sbus_dome_speed,  &robotState.cfg_rc_sbus_arm1,
        &robotState.cfg_rc_sbus_arm2,        &robotState.cfg_rc_sbus_sound,
    };
    const RcBindingConfig defaults[] = {
        defaultPwmBinding(1),
        defaultPwmBinding(2),
        disabledRcBinding(),
        defaultPwmBinding(3),
        defaultPwmBinding(4),
        defaultPwmBinding(5),
        defaultPwmBinding(6),
        defaultSbusBinding(RC_BINDING_SBUS1, 1),
        defaultSbusBinding(RC_BINDING_SBUS1, 2),
        defaultSbusBinding(RC_BINDING_SBUS1, 8),
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
                             false),
        makeRcTriggerBinding(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM2_TOGGLE, nullptr,
                             RC_SBUS_DEFAULT_MIN, RC_SBUS_DEFAULT_CENTER, RC_SBUS_DEFAULT_MAX, 0,
                             false),
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
    int16_t speedLimitMax;
    uint32_t sbusTimeoutMs;
    uint32_t webDriveTimeoutMs;
    bool ch8ModeLock;
    uint8_t audioVolume;
    uint8_t logLevel;
    uint16_t sndScream, sndFaint, sndLeia, sndCantinaS, sndSwTheme;
    uint16_t sndImpMarch, sndCantinaL, sndStartup;
    uint16_t sndDoodoo, sndFailure, sndDisco, sndMahna, sndInlove, sndMacho;
    uint16_t sndGangnam, sndUptown, sndCelebr, sndStayin, sndHarlem, sndPbjtime;
    uint16_t sndSysBoot, sndSysModeN, sndSysModeS, sndSysModeT, sndSysDrvOn, sndSysDomeOn;
    uint16_t sndRandMin, sndRandMax;
    uint16_t sndIntQuiet, sndIntMid, sndIntFull, sndIntAwake;
    uint16_t sndMoodcatQuiet, sndMoodcatMid, sndMoodcatFull, sndMoodcatAwakeplus;
    uint16_t sndCatGenLo, sndCatGenHi, sndCatChatLo, sndCatChatHi, sndCatHapLo, sndCatHapHi;
    uint16_t sndCatProcLo, sndCatProcHi, sndCatSadLo, sndCatSadHi, sndCatSentLo, sndCatSentHi;
    uint16_t sndCatHumLo, sndCatHumHi, sndCatScrmLo, sndCatScrmHi, sndCatOohLo, sndCatOohHi;
    uint16_t sndCatAlrmLo, sndCatAlrmHi, sndCatSnarkyLo, sndCatSnarkyHi, sndCatWhisLo, sndCatWhisHi;
    uint16_t arm1Open, arm1Close, arm2Open, arm2Close;
    float domeMin, domeMax;
    uint16_t seqOpenMs, seqCloseMs;
    uint16_t domeNeutralUs, domeMinPulseUs, domeMaxPulseUs;
    uint8_t domeSpeedLimitPct;
    RcInputMode rcInputMode;
    bool enableArm1, enableArm2, enableAux1, enableAux2, enableAux3, enableDome;
    bool enableRcCh1, enableRcCh2, enableRcCh3, enableRcCh4, enableRcCh5, enableRcCh6;
    bool enableS1Hoverboard, enableS2Sound, enableS3DomeCtrl, singleSbusUseCh2;
    bool stationary;
    RcBindingConfig rcPwmDriveSpeed, rcPwmDriveSteer, rcPwmDriveLimit, rcPwmDomeSpeed, rcPwmArm1,
        rcPwmArm2, rcPwmSound;
    RcBindingConfig rcSbusDriveSpeed, rcSbusDriveSteer, rcSbusDriveLimit, rcSbusDomeSpeed,
        rcSbusArm1, rcSbusArm2, rcSbusSound;
    ServoComponentType arm1Type, arm2Type, aux1Type, aux2Type, aux3Type;
    uint16_t aux1Open, aux1Close, aux2Open, aux2Close, aux3Open, aux3Close;
    uint8_t auxLedPin, auxLedCount;

    taskENTER_CRITICAL(&robotStateMux);
    speedLimitMax = robotState.cfg_speedLimitMax;
    sbusTimeoutMs = robotState.cfg_sbusTimeoutMs;
    webDriveTimeoutMs = robotState.cfg_webDriveTimeoutMs;
    ch8ModeLock = robotState.cfg_ch8ModeLock;
    audioVolume = robotState.cfg_audioVolume;
    logLevel = robotState.cfg_logLevel;
    sndScream = robotState.cfg_snd_scream;
    sndFaint = robotState.cfg_snd_faint;
    sndLeia = robotState.cfg_snd_leia;
    sndCantinaS = robotState.cfg_snd_cantina_s;
    sndSwTheme = robotState.cfg_snd_sw_theme;
    sndImpMarch = robotState.cfg_snd_imp_march;
    sndCantinaL = robotState.cfg_snd_cantina_l;
    sndStartup = robotState.cfg_snd_startup;
    sndDoodoo = robotState.cfg_snd_doodoo;
    sndFailure = robotState.cfg_snd_failure;
    sndDisco = robotState.cfg_snd_disco;
    sndMahna = robotState.cfg_snd_mahna;
    sndInlove = robotState.cfg_snd_inlove;
    sndMacho = robotState.cfg_snd_macho;
    sndGangnam = robotState.cfg_snd_gangnam;
    sndUptown = robotState.cfg_snd_uptown;
    sndCelebr = robotState.cfg_snd_celebr;
    sndStayin = robotState.cfg_snd_stayin;
    sndHarlem = robotState.cfg_snd_harlem;
    sndPbjtime = robotState.cfg_snd_pbjtime;
    sndSysBoot = robotState.cfg_snd_sys_boot;
    sndSysModeN = robotState.cfg_snd_sys_mode_n;
    sndSysModeS = robotState.cfg_snd_sys_mode_s;
    sndSysModeT = robotState.cfg_snd_sys_mode_t;
    sndSysDrvOn = robotState.cfg_snd_sys_drv_on;
    sndSysDomeOn = robotState.cfg_snd_sys_dome_on;
    sndRandMin = robotState.cfg_snd_rand_min;
    sndRandMax = robotState.cfg_snd_rand_max;
    sndIntQuiet = robotState.cfg_snd_int_quiet;
    sndIntMid = robotState.cfg_snd_int_mid;
    sndIntFull = robotState.cfg_snd_int_full;
    sndIntAwake = robotState.cfg_snd_int_awake;
    sndMoodcatQuiet = robotState.cfg_snd_moodcat_quiet;
    sndMoodcatMid = robotState.cfg_snd_moodcat_mid;
    sndMoodcatFull = robotState.cfg_snd_moodcat_full;
    sndMoodcatAwakeplus = robotState.cfg_snd_moodcat_awakeplus;
    sndCatGenLo = robotState.cfg_snd_cat_gen_lo;
    sndCatGenHi = robotState.cfg_snd_cat_gen_hi;
    sndCatChatLo = robotState.cfg_snd_cat_chat_lo;
    sndCatChatHi = robotState.cfg_snd_cat_chat_hi;
    sndCatHapLo = robotState.cfg_snd_cat_hap_lo;
    sndCatHapHi = robotState.cfg_snd_cat_hap_hi;
    sndCatProcLo = robotState.cfg_snd_cat_proc_lo;
    sndCatProcHi = robotState.cfg_snd_cat_proc_hi;
    sndCatSadLo = robotState.cfg_snd_cat_sad_lo;
    sndCatSadHi = robotState.cfg_snd_cat_sad_hi;
    sndCatSentLo = robotState.cfg_snd_cat_sent_lo;
    sndCatSentHi = robotState.cfg_snd_cat_sent_hi;
    sndCatHumLo = robotState.cfg_snd_cat_hum_lo;
    sndCatHumHi = robotState.cfg_snd_cat_hum_hi;
    sndCatScrmLo = robotState.cfg_snd_cat_scrm_lo;
    sndCatScrmHi = robotState.cfg_snd_cat_scrm_hi;
    sndCatOohLo = robotState.cfg_snd_cat_ooh_lo;
    sndCatOohHi = robotState.cfg_snd_cat_ooh_hi;
    sndCatAlrmLo = robotState.cfg_snd_cat_alrm_lo;
    sndCatAlrmHi = robotState.cfg_snd_cat_alrm_hi;
    sndCatSnarkyLo = robotState.cfg_snd_cat_snarky_lo;
    sndCatSnarkyHi = robotState.cfg_snd_cat_snarky_hi;
    sndCatWhisLo = robotState.cfg_snd_cat_whis_lo;
    sndCatWhisHi = robotState.cfg_snd_cat_whis_hi;
    arm1Open = robotState.cfg_arm1_open_us;
    arm1Close = robotState.cfg_arm1_close_us;
    arm2Open = robotState.cfg_arm2_open_us;
    arm2Close = robotState.cfg_arm2_close_us;
    domeMin = robotState.cfg_dome_min_speed;
    domeMax = robotState.cfg_dome_max_speed;
    seqOpenMs = robotState.cfg_seq_open_ms;
    seqCloseMs = robotState.cfg_seq_close_ms;
    domeNeutralUs = robotState.cfg_dome_neutral_us;
    domeMinPulseUs = robotState.cfg_dome_min_pulse_us;
    domeMaxPulseUs = robotState.cfg_dome_max_pulse_us;
    domeSpeedLimitPct = robotState.cfg_dome_speed_limit_pct;
    rcInputMode = robotState.cfg_rc_input_mode;
    enableArm1 = robotState.cfg_enable_arm1;
    enableArm2 = robotState.cfg_enable_arm2;
    enableAux1 = robotState.cfg_enable_aux1;
    enableAux2 = robotState.cfg_enable_aux2;
    enableAux3 = robotState.cfg_enable_aux3;
    enableDome = robotState.cfg_enable_dome;
    enableRcCh1 = robotState.cfg_enable_rc_ch1;
    enableRcCh2 = robotState.cfg_enable_rc_ch2;
    enableRcCh3 = robotState.cfg_enable_rc_ch3;
    enableRcCh4 = robotState.cfg_enable_rc_ch4;
    enableRcCh5 = robotState.cfg_enable_rc_ch5;
    enableRcCh6 = robotState.cfg_enable_rc_ch6;
    enableS1Hoverboard = robotState.cfg_enable_s1_hoverboard;
    enableS2Sound = robotState.cfg_enable_s2_sound;
    enableS3DomeCtrl = robotState.cfg_enable_s3_dome_ctrl;
    singleSbusUseCh2 = robotState.cfg_single_sbus_use_ch2;
    stationary = robotState.cfg_stationary;
    rcPwmDriveSpeed = robotState.cfg_rc_pwm_drive_speed;
    rcPwmDriveSteer = robotState.cfg_rc_pwm_drive_steer;
    rcPwmDriveLimit = robotState.cfg_rc_pwm_drive_limit;
    rcPwmDomeSpeed = robotState.cfg_rc_pwm_dome_speed;
    rcPwmArm1 = robotState.cfg_rc_pwm_arm1;
    rcPwmArm2 = robotState.cfg_rc_pwm_arm2;
    rcPwmSound = robotState.cfg_rc_pwm_sound;
    rcSbusDriveSpeed = robotState.cfg_rc_sbus_drive_speed;
    rcSbusDriveSteer = robotState.cfg_rc_sbus_drive_steer;
    rcSbusDriveLimit = robotState.cfg_rc_sbus_drive_limit;
    rcSbusDomeSpeed = robotState.cfg_rc_sbus_dome_speed;
    rcSbusArm1 = robotState.cfg_rc_sbus_arm1;
    rcSbusArm2 = robotState.cfg_rc_sbus_arm2;
    rcSbusSound = robotState.cfg_rc_sbus_sound;

    // Tier 2 Trigger bindings
    RcTriggerBinding rcArm1, rcArm2, rcAux1, rcAux2, rcAux3, rcSound, rcOpmode;
    RcTriggerBinding rcFree0, rcFree1, rcFree2, rcFree3;
    rcArm1 = robotState.cfg_rc_arm1;
    rcArm2 = robotState.cfg_rc_arm2;
    rcAux1 = robotState.cfg_rc_aux1;
    rcAux2 = robotState.cfg_rc_aux2;
    rcAux3 = robotState.cfg_rc_aux3;
    rcSound = robotState.cfg_rc_sound;
    rcOpmode = robotState.cfg_rc_opmode;
    rcFree0 = robotState.cfg_rc_free0;
    rcFree1 = robotState.cfg_rc_free1;
    rcFree2 = robotState.cfg_rc_free2;
    rcFree3 = robotState.cfg_rc_free3;

    arm1Type = robotState.cfg_arm1_type;
    arm2Type = robotState.cfg_arm2_type;
    aux1Type = robotState.cfg_aux1_type;
    aux2Type = robotState.cfg_aux2_type;
    aux3Type = robotState.cfg_aux3_type;
    aux1Open = robotState.cfg_aux1_open_us;
    aux1Close = robotState.cfg_aux1_close_us;
    aux2Open = robotState.cfg_aux2_open_us;
    aux2Close = robotState.cfg_aux2_close_us;
    aux3Open = robotState.cfg_aux3_open_us;
    aux3Close = robotState.cfg_aux3_close_us;
    auxLedPin = robotState.cfg_aux_led_pin;
    auxLedCount = robotState.cfg_aux_led_count;
    taskEXIT_CRITICAL(&robotStateMux);

    // Enforce 12-bit mood-category masks before persisting.
    sndMoodcatQuiet = (uint16_t)(sndMoodcatQuiet & 0x0FFF);
    sndMoodcatMid = (uint16_t)(sndMoodcatMid & 0x0FFF);
    sndMoodcatFull = (uint16_t)(sndMoodcatFull & 0x0FFF);
    sndMoodcatAwakeplus = (uint16_t)(sndMoodcatAwakeplus & 0x0FFF);

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    bool ok = true;
    ok = prefs.putShort("spd_max", speedLimitMax) > 0 && ok;
    ok = prefs.putULong("sbus_tmo", sbusTimeoutMs) > 0 && ok;
    ok = prefs.putULong("web_tmo", webDriveTimeoutMs) > 0 && ok;
    ok = prefs.putBool("ch8_lock", ch8ModeLock) > 0 && ok;
    ok = prefs.putUChar("aud_vol", audioVolume) > 0 && ok;
    ok = prefs.putUChar("log_level", logLevel) > 0 && ok;
    ok = prefs.putUShort("snd_scream", sndScream) > 0 && ok;
    ok = prefs.putUShort("snd_faint", sndFaint) > 0 && ok;
    ok = prefs.putUShort("snd_leia", sndLeia) > 0 && ok;
    ok = prefs.putUShort("snd_cantina_s", sndCantinaS) > 0 && ok;
    ok = prefs.putUShort("snd_sw", sndSwTheme) > 0 && ok;
    ok = prefs.putUShort("snd_march", sndImpMarch) > 0 && ok;
    ok = prefs.putUShort("snd_cantina_l", sndCantinaL) > 0 && ok;
    ok = prefs.putUShort("snd_startup", sndStartup) > 0 && ok;
    ok = prefs.putUShort("snd_doodoo", sndDoodoo) > 0 && ok;
    ok = prefs.putUShort("snd_failure", sndFailure) > 0 && ok;
    ok = prefs.putUShort("snd_disco", sndDisco) > 0 && ok;
    ok = prefs.putUShort("snd_mahna", sndMahna) > 0 && ok;
    ok = prefs.putUShort("snd_inlove", sndInlove) > 0 && ok;
    ok = prefs.putUShort("snd_macho", sndMacho) > 0 && ok;
    ok = prefs.putUShort("snd_gangnam", sndGangnam) > 0 && ok;
    ok = prefs.putUShort("snd_uptown", sndUptown) > 0 && ok;
    ok = prefs.putUShort("snd_celebr", sndCelebr) > 0 && ok;
    ok = prefs.putUShort("snd_stayin", sndStayin) > 0 && ok;
    ok = prefs.putUShort("snd_harlem", sndHarlem) > 0 && ok;
    ok = prefs.putUShort("snd_pbjtime", sndPbjtime) > 0 && ok;
    ok = prefs.putUShort("snd_sys_boot", sndSysBoot) > 0 && ok;
    ok = prefs.putUShort("snd_sys_mode_n", sndSysModeN) > 0 && ok;
    ok = prefs.putUShort("snd_sys_mode_s", sndSysModeS) > 0 && ok;
    ok = prefs.putUShort("snd_sys_mode_t", sndSysModeT) > 0 && ok;
    ok = prefs.putUShort("snd_sys_drv_on", sndSysDrvOn) > 0 && ok;
    ok = prefs.putUShort("snd_sys_dome_on", sndSysDomeOn) > 0 && ok;
    ok = prefs.putUShort("snd_rand_min", sndRandMin) > 0 && ok;
    ok = prefs.putUShort("snd_rand_max", sndRandMax) > 0 && ok;
    ok = prefs.putUShort("snd_int_quiet", sndIntQuiet) > 0 && ok;
    ok = prefs.putUShort("snd_int_mid", sndIntMid) > 0 && ok;
    ok = prefs.putUShort("snd_int_full", sndIntFull) > 0 && ok;
    ok = prefs.putUShort("snd_int_awake", sndIntAwake) > 0 && ok;
    ok = prefs.putUShort("snd_moodcat_q", sndMoodcatQuiet) > 0 && ok;
    ok = prefs.putUShort("snd_moodcat_m", sndMoodcatMid) > 0 && ok;
    ok = prefs.putUShort("snd_moodcat_f", sndMoodcatFull) > 0 && ok;
    ok = prefs.putUShort("snd_moodcat_a", sndMoodcatAwakeplus) > 0 && ok;
    ok = prefs.putUShort("snd_cat_gen_lo", sndCatGenLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_gen_hi", sndCatGenHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_chat_lo", sndCatChatLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_chat_hi", sndCatChatHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_hap_lo", sndCatHapLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_hap_hi", sndCatHapHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_proc_lo", sndCatProcLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_proc_hi", sndCatProcHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_sad_lo", sndCatSadLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_sad_hi", sndCatSadHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_sent_lo", sndCatSentLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_sent_hi", sndCatSentHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_hum_lo", sndCatHumLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_hum_hi", sndCatHumHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_scrm_lo", sndCatScrmLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_scrm_hi", sndCatScrmHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_ooh_lo", sndCatOohLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_ooh_hi", sndCatOohHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_alrm_lo", sndCatAlrmLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_alrm_hi", sndCatAlrmHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_snrk_lo", sndCatSnarkyLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_snrk_hi", sndCatSnarkyHi) > 0 && ok;
    ok = prefs.putUShort("snd_cat_whis_lo", sndCatWhisLo) > 0 && ok;
    ok = prefs.putUShort("snd_cat_whis_hi", sndCatWhisHi) > 0 && ok;
    ok = prefs.putUShort("arm1_op", arm1Open) > 0 && ok;
    ok = prefs.putUShort("arm1_cl", arm1Close) > 0 && ok;
    ok = prefs.putUShort("arm2_op", arm2Open) > 0 && ok;
    ok = prefs.putUShort("arm2_cl", arm2Close) > 0 && ok;
    ok = prefs.putUChar("arm1_type", (uint8_t)arm1Type) > 0 && ok;
    ok = prefs.putUChar("arm2_type", (uint8_t)arm2Type) > 0 && ok;
    ok = prefs.putUChar("aux1_type", (uint8_t)aux1Type) > 0 && ok;
    ok = prefs.putUChar("aux2_type", (uint8_t)aux2Type) > 0 && ok;
    ok = prefs.putUChar("aux3_type", (uint8_t)aux3Type) > 0 && ok;
    ok = prefs.putUShort("aux1_op", aux1Open) > 0 && ok;
    ok = prefs.putUShort("aux1_cl", aux1Close) > 0 && ok;
    ok = prefs.putUShort("aux2_op", aux2Open) > 0 && ok;
    ok = prefs.putUShort("aux2_cl", aux2Close) > 0 && ok;
    ok = prefs.putUShort("aux3_op", aux3Open) > 0 && ok;
    ok = prefs.putUShort("aux3_cl", aux3Close) > 0 && ok;

    union {
        float f;
        uint32_t u;
    } dome_conv;
    dome_conv.f = domeMin;
    ok = prefs.putULong("dome_min", dome_conv.u) > 0 && ok;
    dome_conv.f = domeMax;
    ok = prefs.putULong("dome_max", dome_conv.u) > 0 && ok;
    ok = prefs.putUShort("seq_op", seqOpenMs) > 0 && ok;
    ok = prefs.putUShort("seq_cl", seqCloseMs) > 0 && ok;
    ok = prefs.putUShort("dome_neu", domeNeutralUs) > 0 && ok;
    ok = prefs.putUShort("dome_minp", domeMinPulseUs) > 0 && ok;
    ok = prefs.putUShort("dome_maxp", domeMaxPulseUs) > 0 && ok;
    ok = prefs.putUChar("dome_pct", domeSpeedLimitPct) > 0 && ok;
    ok = prefs.putUChar("rc_mode", (uint8_t)rcInputMode) > 0 && ok;
    ok = prefs.putBool("en_arm1", enableArm1) > 0 && ok;
    ok = prefs.putBool("en_arm2", enableArm2) > 0 && ok;
    ok = prefs.putBool("en_aux1", enableAux1) > 0 && ok;
    ok = prefs.putBool("en_aux2", enableAux2) > 0 && ok;
    ok = prefs.putBool("en_aux3", enableAux3) > 0 && ok;
    ok = prefs.putBool("en_dome", enableDome) > 0 && ok;
    ok = prefs.putBool("en_rc_ch1", enableRcCh1) > 0 && ok;
    ok = prefs.putBool("en_rc_ch2", enableRcCh2) > 0 && ok;
    ok = prefs.putBool("en_rc_ch3", enableRcCh3) > 0 && ok;
    ok = prefs.putBool("en_rc_ch4", enableRcCh4) > 0 && ok;
    ok = prefs.putBool("en_rc_ch5", enableRcCh5) > 0 && ok;
    ok = prefs.putBool("en_rc_ch6", enableRcCh6) > 0 && ok;
    ok = prefs.putBool("sbus_recv_ch2", singleSbusUseCh2) > 0 && ok;
    ok = prefs.putBool("en_s1", enableS1Hoverboard) > 0 && ok;
    ok = prefs.putBool("en_s2", enableS2Sound) > 0 && ok;
    ok = prefs.putBool("en_s3", enableS3DomeCtrl) > 0 && ok;
    ok = prefs.putBool("op_mode", stationary) > 0 && ok;
    ok = prefs.putUChar(NVS_KEY_AUX_LED_PIN, auxLedPin) > 0 && ok;
    ok = prefs.putUChar(NVS_KEY_AUX_LED_COUNT, auxLedCount) > 0 && ok;
    ok = saveRcBindingToPrefs(prefs, "rcp_drv", rcPwmDriveSpeed) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcp_str", rcPwmDriveSteer) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcp_lim", rcPwmDriveLimit) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcp_dom", rcPwmDomeSpeed) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcp_a1", rcPwmArm1) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcp_a2", rcPwmArm2) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcp_snd", rcPwmSound) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcs_drv", rcSbusDriveSpeed) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcs_str", rcSbusDriveSteer) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcs_lim", rcSbusDriveLimit) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcs_dom", rcSbusDomeSpeed) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcs_a1", rcSbusArm1) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcs_a2", rcSbusArm2) && ok;
    ok = saveRcBindingToPrefs(prefs, "rcs_snd", rcSbusSound) && ok;

    ok = saveRcTriggerBindingToPrefs(prefs, "rc_arm1", rcArm1) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, "rc_arm2", rcArm2) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, "rc_aux1", rcAux1) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, "rc_aux2", rcAux2) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, "rc_aux3", rcAux3) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, "rc_sound", rcSound) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, "rc_opmode", rcOpmode) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, "rc_free0", rcFree0) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, "rc_free1", rcFree1) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, "rc_free2", rcFree2) && ok;
    ok = saveRcTriggerBindingToPrefs(prefs, "rc_free3", rcFree3) && ok;

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

    // Safety: boot with drive locked until SBUS confirmed
    robotState.sbusSignalLost = true;
    robotState.estop = false;
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

    // Detect TWDT reset from previous boot — set estop so robot does not move
    // until operator explicitly clears via POST /api/estop/clear
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (resetReason == ESP_RST_TASK_WDT) {
        robotState.estop = true;
        taskENTER_CRITICAL(&robotStateMux);
        recordFailsafeTriggerLocked(FS_WATCHDOG_RESET, millis());
        taskEXIT_CRITICAL(&robotStateMux);
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
    xTaskCreatePinnedToCore(driveTask, "DriveTask", 4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(rcInputTask, "RCInputTask", 4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(
        servoTask, "ServoTask", 3072, nullptr, 4, nullptr,
        1);  // HWM: ~728 B used; was 5120 (oversized for string formatting assumption)
    xTaskCreatePinnedToCore(domeTask, "DomeTask", 2048, nullptr, 4, nullptr,
                            1);  // HWM: ~764 B used

    // AudioTask: Core 0 (non-RT) — software bit-bang TX blocks ~6 ms per command;
    // keeping off Core 1 avoids any interaction with DriveTask / ServoTask timing.
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 3072, nullptr, 3, nullptr, 0);

    // AuxLedTask: Core 0 (non-RT) - WS2812B effects and API-driven color/effect updates.
    // Runs independently of Core 1 control loops.
    if (auxLedTaskReady) {
        xTaskCreatePinnedToCore(auxLedTask, "AuxLedTask", 3072, nullptr, 2, nullptr, 0);
    }

    // DomeLinkTask: Core 1 — bidirectional Marcduino serial to AstroPixelsPlus.
    // UART2 TX/RX are non-blocking hardware operations; Core 1 at priority 3.
    xTaskCreatePinnedToCore(domeLinkTask, "DomeLinkTask", 3072, nullptr, 3, nullptr, 1);

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
        if (audioQueuePlayTrack(bootTrack, SRC_INTERNAL)) {
            PA_LOG_INFO("main", "system boot sound queued: track=%u", (unsigned)bootTrack);
        } else {
            PA_LOG_WARN("main", "system boot sound queue full: track=%u", (unsigned)bootTrack);
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
