// =============================================================================
// include/robot_state.h
//
// Shared robot state structure for protoArtoo.
// All inter-task communication goes through this struct + FreeRTOS primitives.
//
// Thread safety: All fields accessed under robotStateMux (portMUX_TYPE).
// Queues: driveQueue for drive commands from any source.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "config.h"
#include "drive_speed_preset.h"
#include "rc_mapping.h"

// -----------------------------------------------------------------------------
// Enums
// -----------------------------------------------------------------------------

enum FailsafeSource : uint8_t {
    FS_NONE = 0,
    FS_SBUS_TIMEOUT,    // Layer 2: SBUS watchdog expired
    FS_SBUS_HW,         // Layer 1: SBUS receiver hardware failsafe flag
    FS_SBUS2_TIMEOUT,   // Dome-spin receiver lost (dome stops, drive continues)
    FS_WEB_TIMEOUT,     // Layer 3: Web API drive command expired
    FS_ESTOP_CMD,       // Explicit POST /api/estop
    FS_WATCHDOG_RESET,  // Layer 4: TWDT fired, detected on reboot
};

enum CommandSource : uint8_t {
    SRC_NONE = 0,
    SRC_SBUS,      // RC transmitter via SBUS receiver
    SRC_WEB_API,   // Browser / REST API
    SRC_INTERNAL,  // Internal (safety zeroing, boot defaults)
};

inline const char* commandSourceToString(CommandSource src) {
    switch (src) {
        case SRC_SBUS:
            return "SBUS";
        case SRC_WEB_API:
            return "WEB";
        case SRC_INTERNAL:
            return "INT";
        default:
            return "?";
    }
}

enum RcInputMode : uint8_t {
    RC_INPUT_STANDARD_PWM = 0,
    RC_INPUT_SINGLE_SBUS,
    RC_INPUT_DUAL_SBUS,
};

enum DomeLinkTransport : uint8_t {
    DOME_LINK_TRANSPORT_DISCONNECTED = 0,
    DOME_LINK_TRANSPORT_UART,
    DOME_LINK_TRANSPORT_WIFI,
};

enum ServoComponentType : uint8_t {
    SERVO_COMP_NONE = 0,    // Nothing connected / unassigned
    SERVO_COMP_MG996R = 1,  // Standard hobby servo, 1000-2000 µs range
    SERVO_COMP_MG90S = 2,   // Micro servo, 500-2500 µs range
    SERVO_COMP_RGB = 3,     // RGB LED strip (no servo PWM calibration)
};

enum AuxLedEffect : uint8_t {
    AUX_LED_EFFECT_OFF = 0,
    AUX_LED_EFFECT_SOLID,
    AUX_LED_EFFECT_BLINK,
    AUX_LED_EFFECT_PULSE,
};

struct AuxLedState {
    uint8_t pin;  // 0 when disabled; otherwise active GPIO number
    uint8_t r;
    uint8_t g;
    uint8_t b;
    AuxLedEffect effect;
    bool available;  // false when RMT/driver init failed
};

// -----------------------------------------------------------------------------
// Drive command message (sent via driveQueue)
// -----------------------------------------------------------------------------
struct DriveCommand {
    int16_t speed;  // -SPEED_LIMIT_MAX .. +SPEED_LIMIT_MAX
    int16_t steer;  // -SPEED_LIMIT_MAX .. +SPEED_LIMIT_MAX
    CommandSource source;
    uint32_t timestampMs;
};

// -----------------------------------------------------------------------------
// Servo command message (sent via servoCmdQueue)
// -----------------------------------------------------------------------------
enum ServoCommandType : uint8_t {
    SERVO_CMD_POSITION,
    SERVO_CMD_OPEN,
    SERVO_CMD_CLOSE,
    SERVO_CMD_SEQUENCE,
};

struct ServoCommand {
    uint8_t armId;          // 0=ARM1, 1=ARM2, 2=AUX1, 3=AUX2, 4=AUX3, 255=broadcast (ARM1+ARM2)
    ServoCommandType type;  // Command type
    uint16_t positionUs;    // Target pulse width (µs) for POSITION type
    uint8_t sequenceId;     // Sequence ID 30-36 for SEQUENCE type
    CommandSource source;
    uint32_t timestampMs;
};

// -----------------------------------------------------------------------------
// Dome command message (sent via domeCmdQueue)
// -----------------------------------------------------------------------------
struct DomeCommand {
    float speed;  // -1.0 (full reverse) .. +1.0 (full forward), 0 = stop
    CommandSource source;
    uint32_t timestampMs;
};

// -----------------------------------------------------------------------------
// RobotState — shared state, all access under robotStateMux
// -----------------------------------------------------------------------------
struct RobotState {
    // --- Drive output (written by SBUSInputTask/WebAPI, read by DriveTask) ---
    int16_t driveSpeed;
    int16_t driveSteer;
    float domeTargetSpeed;  // -1.0 .. +1.0

    // --- Subsystem state ---
    bool audioActive;

    // Audio module state — populated by AudioTask from DY-SV5W query responses.
    // link_ok: false until the module responds to at least one query.
    // play_state: 0=stop 1=playing 2=paused 0xFF=unknown
    // device:     0=USB  1=SD/TF  2=FLASH  0xFF=unknown/none
    bool audio_module_link_ok;
    uint8_t audio_module_play_state;
    uint8_t audio_module_device;
    uint16_t audio_module_total_tracks;
    uint16_t audio_module_current_track;

    bool armOpen[2];
    uint16_t arm1TargetUs;
    uint16_t arm2TargetUs;
    float dome_speed;
    bool sleepMode;
    uint32_t sleepSinceMs;
    // Pending body->dome sleep sync frame (#PASL/#PAWU). Set by local sleep
    // state transitions and consumed by DomeLinkTask when transport is available.
    bool domeSleepSyncPending;
    bool domeSleepSyncSleepMode;

    AuxLedState auxLed;
    // --- Failsafe state ---
    bool estop;
    bool sbusSignalLost;
    bool sbus2SignalLost;
    bool sbusHwFailsafe;
    bool stationary;
    bool webDriveExpired;
    FailsafeSource failsafeSource;
    uint32_t failsafeTriggerCount;
    uint32_t queueOverflowCount;
    uint32_t sbus1LostFrameCount;  // cumulative lost_frame events (not failsafe)
    uint32_t sbus2LostFrameCount;  // cumulative lost_frame events (not failsafe)
    bool sbus2HwFailsafe;
    uint32_t failsafeLastTriggerMs;        // millis() when last failsafe trigger latched
    uint32_t failsafeLastWatchdogMs;       // millis() when last SBUS watchdog trigger fired
    uint32_t failsafeLastZeroOutputMs;     // millis() when DriveTask first asserted zero output
    uint32_t failsafeLastTriggerToZeroMs;  // latency from trigger to first zero output (ms)
    FailsafeSource failsafeLastTriggerSource;
    // --- Timing state ---
    uint32_t lastPwmMs;
    uint32_t lastSbus1Ms;
    uint32_t lastSbus2Ms;
    uint32_t lastDriveCommandMs;
    uint32_t domeLastSeenMs;
    uint32_t domeLastSeenUartMs;
    uint32_t domeLastSeenWifiMs;
    CommandSource lastDriveSource;

    uint16_t rcPwmPulseUs[6];
    bool rcPwmPulseValid[6];
    uint16_t rcSbus1Raw[16];
    uint16_t rcSbus2Raw[16];
    bool rcSbus1Digital[2];
    bool rcSbus2Digital[2];

    // --- Dome link diagnostics / transport state ---
    uint32_t domeHbRx;
    uint32_t bodyHbTx;
    uint32_t domeRxOverflowCount;
    uint32_t domeRxUnknownCount;
    DomeLinkTransport domeActiveTransport;
    bool domeUartOwned;
    bool     domeSeqActive;    // true while a dome sequence is running
    uint32_t domeSeqUntilMs;   // safety timeout: auto-clear domeSeqActive at this millis()

    // --- Mood ---
    // Active mood SE1x index: 10=Quiet, 11=Full-Awake, 13=Mid-Awake, 14=Awake+.
    // 0 = unset (no mood applied this session). NVS key: "last_mood".
    uint8_t activeMood;

    // --- Web control state ---
    bool webControlEnabled;
    bool rcDebugMode;  // Enable verbose RC/SBUS logging when RC page is active

    // --- NVS-backed config (loaded at boot, written via web API) ---
    int16_t cfg_speedLimitMax;            // Default: SPEED_LIMIT_MAX
    int16_t cfg_speedPresetSlow;          // Default: SPEED_PRESET_SLOW
    int16_t cfg_speedPresetNormal;        // Default: SPEED_PRESET_NORMAL
    int16_t cfg_speedPresetTurbo;         // Default: SPEED_PRESET_TURBO
    SpeedPresetId cfg_speedPresetActive;  // Default: Normal
    uint32_t cfg_sbusTimeoutMs;           // Default: SBUS_TIMEOUT_MS
    uint32_t cfg_webDriveTimeoutMs;       // Default: WEB_DRIVE_TIMEOUT_MS
    uint8_t cfg_audioVolume;              // Default: 20 (0-30)
    uint8_t cfg_logLevel;  // Runtime log verbosity: 1=Error 2=Info 3=Debug. NVS: log_level

    // NVS-backed named sound track indices (mirror AudioNamedTracks defaults).
    // NVS keys: snd_scream, snd_faint, snd_leia, snd_cantina_s, snd_sw,
    //           snd_march, snd_cantina_l, snd_startup, snd_rand_min, snd_rand_max
    uint16_t cfg_snd_scream;     // $S  default 126
    uint16_t cfg_snd_faint;      // $F  default 128
    uint16_t cfg_snd_leia;       // $L  default 151
    uint16_t cfg_snd_cantina_s;  // $c  default 176
    uint16_t cfg_snd_sw_theme;   // $W  default 177
    uint16_t cfg_snd_imp_march;  // $M  default 178
    uint16_t cfg_snd_cantina_l;  // $C  default 180
    uint16_t cfg_snd_startup;    // $B  default 255
    // T08 named tracks without $ aliases (default 0 = unset/silent).
    uint16_t cfg_snd_doodoo;
    uint16_t cfg_snd_failure;
    uint16_t cfg_snd_disco;
    uint16_t cfg_snd_mahna;
    uint16_t cfg_snd_inlove;
    uint16_t cfg_snd_macho;
    uint16_t cfg_snd_gangnam;
    uint16_t cfg_snd_uptown;
    uint16_t cfg_snd_celebr;
    uint16_t cfg_snd_stayin;
    uint16_t cfg_snd_harlem;
    uint16_t cfg_snd_pbjtime;
    // T09 system sound events (default 0 = silent/no-op).
    uint16_t cfg_snd_sys_boot;
    uint16_t cfg_snd_sys_mode_n;
    uint16_t cfg_snd_sys_mode_s;
    uint16_t cfg_snd_sys_mode_t;
    uint16_t cfg_snd_sys_drv_on;
    uint16_t cfg_snd_sys_dome_on;
    uint16_t cfg_snd_rand_min;   // random pool start  default 1
    uint16_t cfg_snd_rand_max;   // random pool end    default 100
    uint16_t cfg_snd_int_quiet;  // random interval Quiet mode (s)     default 0
    uint16_t cfg_snd_int_mid;    // random interval Mid-Awake mode (s) default 30
    uint16_t cfg_snd_int_full;   // random interval Full-Awake mode (s) default 20
    uint16_t cfg_snd_int_awake;  // random interval Awake+ mode (s)    default 10
    // T11 mood-category bitmasks (12-bit; one bit per sound category).
    uint16_t cfg_snd_moodcat_quiet;
    uint16_t cfg_snd_moodcat_mid;
    uint16_t cfg_snd_moodcat_full;
    uint16_t cfg_snd_moodcat_awakeplus;

    // RC-bindable random sound category ranges (NVS-backed).
    // A range is inactive when lo==0 or lo>hi (silent no-op).
    uint16_t cfg_snd_cat_gen_lo;
    uint16_t cfg_snd_cat_gen_hi;
    uint16_t cfg_snd_cat_chat_lo;
    uint16_t cfg_snd_cat_chat_hi;
    uint16_t cfg_snd_cat_hap_lo;
    uint16_t cfg_snd_cat_hap_hi;
    uint16_t cfg_snd_cat_proc_lo;
    uint16_t cfg_snd_cat_proc_hi;
    uint16_t cfg_snd_cat_sad_lo;
    uint16_t cfg_snd_cat_sad_hi;
    uint16_t cfg_snd_cat_sent_lo;
    uint16_t cfg_snd_cat_sent_hi;
    uint16_t cfg_snd_cat_hum_lo;
    uint16_t cfg_snd_cat_hum_hi;
    uint16_t cfg_snd_cat_scrm_lo;
    uint16_t cfg_snd_cat_scrm_hi;
    uint16_t cfg_snd_cat_ooh_lo;
    uint16_t cfg_snd_cat_ooh_hi;
    uint16_t cfg_snd_cat_alrm_lo;
    uint16_t cfg_snd_cat_alrm_hi;
    uint16_t cfg_snd_cat_snarky_lo;
    uint16_t cfg_snd_cat_snarky_hi;
    uint16_t cfg_snd_cat_whis_lo;
    uint16_t cfg_snd_cat_whis_hi;
    // Servo config
    uint16_t cfg_arm1_open_us;
    uint16_t cfg_arm1_close_us;
    uint16_t cfg_arm2_open_us;
    uint16_t cfg_arm2_close_us;

    // Servo component types (what is physically connected to each output)
    ServoComponentType cfg_arm1_type;  // arm1_type — default: SERVO_COMP_MG996R
    ServoComponentType cfg_arm2_type;  // arm2_type — default: SERVO_COMP_MG996R
    ServoComponentType cfg_aux1_type;  // aux1_type — default: SERVO_COMP_NONE
    ServoComponentType cfg_aux2_type;  // aux2_type — default: SERVO_COMP_NONE
    ServoComponentType cfg_aux3_type;  // aux3_type — default: SERVO_COMP_NONE

    // AUX servo calibration (NVS-backed, only meaningful when type is a servo type)
    uint16_t cfg_aux1_open_us;
    uint16_t cfg_aux1_close_us;
    uint16_t cfg_aux2_open_us;
    uint16_t cfg_aux2_close_us;
    uint16_t cfg_aux3_open_us;
    uint16_t cfg_aux3_close_us;

    // AUX LED strip config (single selectable AUX header; NVS-backed)
    uint8_t cfg_aux_led_pin;    // aux_led_pin   0=disabled, 1=AUX1, 2=AUX2, 3=AUX3
    uint8_t cfg_aux_led_count;  // aux_led_count 1..255 (default 1)

    // Dome config
    float cfg_dome_min_speed;
    float cfg_dome_max_speed;
    uint16_t cfg_dome_neutral_us;
    uint16_t cfg_dome_min_pulse_us;
    uint16_t cfg_dome_max_pulse_us;
    uint8_t cfg_dome_speed_limit_pct;
    char cfg_dome_wifi_peer_ip[16];  // dome_wip — fallback IPv4 peer for WiFi dome link

    // Random dome idle rotation (NVS-backed)
    bool     cfg_dome_rnd_enable;      // dome_rnd_en   default false
    uint8_t  cfg_dome_rnd_speed_pct;   // dome_rnd_spd  default 30 (% of max speed)
    uint8_t  cfg_dome_rnd_pause_min;   // dome_rnd_pmin default 6 (seconds)
    uint8_t  cfg_dome_rnd_pause_max;   // dome_rnd_pmax default 12 (seconds)
    uint16_t cfg_dome_rnd_move_ms;     // dome_rnd_ms   default 2500 (ms per random move)

    // Sequence timing (ms)
    uint16_t cfg_seq_open_ms;
    uint16_t cfg_seq_close_ms;

    // Feature toggles (enable/disable entire subsystems)
    // NVS key names are the config source of truth; GPIO assignments from docs/pin_map.md.
    //
    // Servo / ESC outputs (LEDC PWM):
    bool cfg_enable_arm1;  // en_arm1  — ARM1 (GPIO 23) — Utility arm servo #1 — Top / Left arm
    bool cfg_enable_arm2;  // en_arm2  — ARM2 (GPIO 5)  — Utility arm servo #2 — Bottom / Right arm
    bool cfg_enable_aux1;  // en_aux1  — AUX1 / ARM3 (GPIO 19) — Spare servo / AUX output #1
    bool cfg_enable_aux2;  // en_aux2  — AUX2 / ARM4 (GPIO 18) — Spare servo / AUX output #2
    bool cfg_enable_aux3;  // en_aux3  — AUX3 / ARM5 (GPIO 32) — Spare servo / AUX output #3
    bool cfg_enable_dome;  // en_dome  — DOME (GPIO 25) — Dome rotation ESC
    //
    // RC receiver inputs:
    RcInputMode cfg_rc_input_mode;
    bool cfg_single_sbus_use_ch2;  // sbus_recv_ch2 — false=SBUS1 (GPIO15), true=SBUS2 (GPIO13);
                                   // single_sbus mode only
    bool cfg_enable_rc_ch1;  // en_rc_ch1 — CH1 (GPIO 15) — SBUS #1 (drive) OR Standard PWM CH1
    bool cfg_enable_rc_ch2;  // en_rc_ch2 — CH2 (GPIO 13) — SBUS #2 (dome) OR Standard PWM CH2
    bool
        cfg_enable_rc_ch3;  // en_rc_ch3 — CH3 (GPIO 2)  — Standard PWM CH3 (strapping pin; dormant)
    bool cfg_enable_rc_ch4;  // en_rc_ch4 — CH4 (GPIO 4)  — Standard PWM CH4 (dormant)
    bool
        cfg_enable_rc_ch5;  // en_rc_ch5 — CH5 (GPIO 12) — Standard PWM CH5 (strapping pin; dormant)
    bool cfg_enable_rc_ch6;  // en_rc_ch6 — CH6 (GPIO 27) — Standard PWM CH6 (dormant)
    //
    // Serial links (UART):
    bool cfg_enable_s1_hoverboard;  // en_s1_hoverboard — S1 (GPIO 16/17) — Hoverboard drive (UART1)
    bool cfg_enable_s2_sound;       // en_s2_sound       — S2 (GPIO 26/35) — DY-SV5W audio (UART2)
    bool cfg_enable_s3_dome_ctrl;   // en_s3_dome_ctrl   — S3 (GPIO 33/34) — Marcduino dome link
                                    // (UART2)
    //
    // Operation mode:
    bool cfg_stationary;  // op_mode — true=stationary (performance), false=driving (movement)

    RcBindingConfig cfg_rc_pwm_drive_speed;
    RcBindingConfig cfg_rc_pwm_drive_steer;
    RcBindingConfig cfg_rc_pwm_dome_speed;
    RcBindingConfig cfg_rc_pwm_arm1;
    RcBindingConfig cfg_rc_pwm_arm2;
    RcBindingConfig cfg_rc_pwm_sound;

    RcBindingConfig cfg_rc_sbus_drive_speed;
    RcBindingConfig cfg_rc_sbus_drive_steer;
    RcBindingConfig cfg_rc_sbus_dome_speed;
    RcBindingConfig cfg_rc_sbus_arm1;
    RcBindingConfig cfg_rc_sbus_arm2;
    RcBindingConfig cfg_rc_sbus_sound;

    // Tier 2 Trigger/Button bindings (operator-configurable action targets)
    // Format: source:channel:target:payload:min:center:max:deadband:reverse
    RcTriggerBinding cfg_rc_arm1;
    RcTriggerBinding cfg_rc_arm2;
    RcTriggerBinding cfg_rc_aux1;
    RcTriggerBinding cfg_rc_aux2;
    RcTriggerBinding cfg_rc_aux3;
    RcTriggerBinding cfg_rc_sound;
    RcTriggerBinding cfg_rc_opmode;
    RcTriggerBinding cfg_rc_free0;
    RcTriggerBinding cfg_rc_free1;
    RcTriggerBinding cfg_rc_free2;
    RcTriggerBinding cfg_rc_free3;
    // -------------------------------------------------------------------------
    // Hoverboard controller feedback — populated by DriveTask from UART1 RX.
    // batteryRaw = V × 100; boardTempRaw = °C × 10.
    // currentL/R = A × 100 from Gen2.x firmware only; 0 for FOC firmware.
    // feedbackValid is false until the first valid frame is received.
    // -------------------------------------------------------------------------
    int16_t hb_batteryRaw;
    int16_t hb_boardTempRaw;
    int16_t hb_speedR;
    int16_t hb_speedL;
    int16_t hb_currentL;
    int16_t hb_currentR;
    bool hb_feedbackValid;
    uint32_t hb_lastFeedbackMs;
};

// -----------------------------------------------------------------------------
// Global instances (defined in main.cpp)
// -----------------------------------------------------------------------------
extern RobotState robotState;
extern portMUX_TYPE robotStateMux;
extern QueueHandle_t servoCmdQueue;
extern QueueHandle_t domeCmdQueue;
extern QueueHandle_t audioCmdQueue;
extern QueueHandle_t domeTxQueue;
// -----------------------------------------------------------------------------
// Helper function declarations (defined in main.cpp or a dedicated helpers.cpp)
// -----------------------------------------------------------------------------

// Set drive command under mutex and update lastDriveCommandMs
void setDriveCommand(int16_t speed, int16_t steer, CommandSource src);

// Load NVS config into robotState.cfg_* fields
void loadConfigToState();

bool saveConfigToNvs();

// ----------------------------------------------------------------------------
// Failsafe instrumentation helpers (MUST be called under robotStateMux lock)
// ----------------------------------------------------------------------------
inline void recordFailsafeTriggerLocked(FailsafeSource src, uint32_t nowMs) {
    robotState.failsafeSource = src;
    robotState.failsafeTriggerCount++;
    robotState.failsafeLastTriggerMs = nowMs;
    robotState.failsafeLastTriggerSource = src;
    if (src == FS_SBUS_TIMEOUT || src == FS_SBUS2_TIMEOUT) {
        robotState.failsafeLastWatchdogMs = nowMs;
    }
}

inline void recordFailsafeZeroOutputLocked(uint32_t nowMs) {
    robotState.failsafeLastZeroOutputMs = nowMs;
    if (robotState.failsafeLastTriggerMs == 0) {
        robotState.failsafeLastTriggerToZeroMs = 0;
        return;
    }
    robotState.failsafeLastTriggerToZeroMs = (uint32_t)(nowMs - robotState.failsafeLastTriggerMs);
}

// -----------------------------------------------------------------------------
// Safe read-only accessors for safety-critical state
// Use these instead of direct field access for safety-critical reads
// -----------------------------------------------------------------------------

// Read estop state under mutex
inline bool isEstopActive() {
    bool estop;
    taskENTER_CRITICAL(&robotStateMux);
    estop = robotState.estop;
    taskEXIT_CRITICAL(&robotStateMux);
    return estop;
}

// Read drive speed under mutex
inline int16_t getDriveSpeed() {
    int16_t speed;
    taskENTER_CRITICAL(&robotStateMux);
    speed = robotState.driveSpeed;
    taskEXIT_CRITICAL(&robotStateMux);
    return speed;
}

// Read drive steer under mutex
inline int16_t getDriveSteer() {
    int16_t steer;
    taskENTER_CRITICAL(&robotStateMux);
    steer = robotState.driveSteer;
    taskEXIT_CRITICAL(&robotStateMux);
    return steer;
}

// Read failsafe source under mutex
inline FailsafeSource getFailsafeSource() {
    FailsafeSource source;
    taskENTER_CRITICAL(&robotStateMux);
    source = robotState.failsafeSource;
    taskEXIT_CRITICAL(&robotStateMux);
    return source;
}

// Read SBUS signal lost state under mutex
inline bool isSbusSignalLost() {
    bool lost;
    taskENTER_CRITICAL(&robotStateMux);
    lost = robotState.sbusSignalLost;
    taskEXIT_CRITICAL(&robotStateMux);
    return lost;
}
