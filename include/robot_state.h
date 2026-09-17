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

#include "audio_rx_status.h"
#include "config.h"
#include "dome_link_transport.h"
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
    SRC_SBUS,      // RC radio via SBUS receiver
    SRC_WEB_API,   // Browser / REST API
    SRC_INTERNAL,  // Internal (safety zeroing, boot defaults)
    SRC_SEQ,       // Sequence coordinator (SequenceDispatcherTask)
    // Controller Console provenance (ADR 0036, #220). Appended rather than
    // interleaved so no existing numeric value shifts. Distinct from
    // SRC_WEB_API: the Console's browser adapter (POST /api/console) and the
    // REST endpoints are different command surfaces even though both arrive
    // over HTTP.
    SRC_SERIAL_CONSOLE,  // Physical serial terminal (embedded-cli adapter)
    SRC_WEB_CONSOLE,     // Browser Live Logs command box (POST /api/console)
};

inline const char* commandSourceToString(CommandSource src) {
    switch (src) {
        case SRC_SBUS:
            return "RC";
        case SRC_WEB_API:
            return "WEB";
        case SRC_INTERNAL:
            return "INT";
        case SRC_SEQ:
            return "SEQ";
        case SRC_SERIAL_CONSOLE:
            return "SERIAL_CONSOLE";
        case SRC_WEB_CONSOLE:
            return "WEB_CONSOLE";
        default:
            return "?";
    }
}

enum RcInputMode : uint8_t {
    RC_INPUT_STANDARD_PWM = 0,
    RC_INPUT_SINGLE_SBUS,
    RC_INPUT_DUAL_SBUS,
};

enum DomeUartOwner : uint8_t {
    DOME_UART_NONE = 0,
    DOME_UART_DOME,
    DOME_UART_AUDIO,
};

enum ServoComponentType : uint8_t {
    SERVO_COMP_NONE = 0,    // Nothing connected / unassigned
    SERVO_COMP_MG996R = 1,  // Standard hobby servo, 1000-2000 us range
    SERVO_COMP_MG90S = 2,   // Micro servo, 500-2500 us range
    SERVO_COMP_RGB = 3,     // RGB LED strip (no servo PWM calibration)
};

enum AuxLedEffect : uint8_t {
    AUX_LED_EFFECT_OFF = 0,
    AUX_LED_EFFECT_SOLID,
    AUX_LED_EFFECT_BLINK,
    AUX_LED_EFFECT_PULSE,
};

struct AuxLedState {
    // 0 when disabled; otherwise the active GPIO number. This is the RESOLVED
    // pin, not the operator's choice: ServoConfig::aux_led_pin is the AUX slot
    // selection 0..3 that auxLedSelectionToGpio() turns into this.
    uint8_t pin;
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
    // Find by Moving (ADR 0050, #363): a small, bounded twitch about wherever
    // the output already is, out one way, across to the other, and back, run
    // to completion by ServoTask itself. It carries no target: the pair is
    // computed from the width on the pin (include/servo_nudge.h), so no source
    // can ask for a big one. One output per command; 255 is refused.
    SERVO_CMD_NUDGE,
    // The calibration dial's hold (ADR 0064, #364): drive one output to
    // positionUs and keep the dial's hold on it. The first HOLD takes the
    // Output and starts the ten-minute ceiling; every HOLD after it refreshes
    // the short expiry and nothing else, so a page can keep a hold alive but
    // never past the ceiling (include/servo_hold.h). One output per command;
    // 255 is refused.
    SERVO_CMD_HOLD,
    // Pulses off (ADR 0043, ADR 0064, #364): take the pulse off the pin. The
    // output goes limp where it is -- nothing is commanded -- and any move,
    // nudge or hold on it ends. 255 releases ARM1 and ARM2, as the other
    // broadcasts do.
    SERVO_CMD_RELEASE,
};

// Why an output has no pulse on it (#364). Read only while
// ServoCommandedPosition::pulsing is false: a pulsing output's reason is
// whatever was last recorded and is not handed on. SERVO_LIMP_OFF is 0 so a
// zero-filled mirror -- an output nothing has driven since boot -- reads as
// exactly that.
enum ServoLimpReason : uint8_t {
    SERVO_LIMP_OFF = 0,   // no pulse since boot: switched off, or never driven
    SERVO_LIMP_RELEASED,  // pulses off: a release command let go of it
    SERVO_LIMP_EXPIRED,   // the dial's hold commands stopped arriving (SERVO_HOLD_EXPIRY_MS)
    SERVO_LIMP_CEILING,   // the dial held it for SERVO_HOLD_CEILING_MS
    SERVO_LIMP_ESTOP,     // the estop released every output (ADR 0043)
    SERVO_LIMP_SLEEP,     // Sleep Mode released every output (ADR 0043)
};

// The token a surface reads for it: GET /api/servo/outputs' `limp` value and
// the Console's, one spelling.
inline const char* servoLimpReasonToString(ServoLimpReason reason) {
    switch (reason) {
        case SERVO_LIMP_RELEASED:
            return "pulses-off";
        case SERVO_LIMP_EXPIRED:
            return "expiry";
        case SERVO_LIMP_CEILING:
            return "ceiling";
        case SERVO_LIMP_ESTOP:
            return "estop";
        case SERVO_LIMP_SLEEP:
            return "sleep";
        case SERVO_LIMP_OFF:
        default:
            return "off";
    }
}

struct ServoCommand {
    uint8_t armId;          // 0=ARM1, 1=ARM2, 2=AUX1, 3=AUX2, 4=AUX3, 255=broadcast (ARM1+ARM2)
    ServoCommandType type;  // Command type
    uint16_t positionUs;    // Target pulse width (us) for POSITION type
    CommandSource source;
    uint32_t timestampMs;
};

// The outputs ServoTask drives, one per armId above.
constexpr uint8_t SERVO_ARM_COUNT = 5;

// Where ServoTask has told one output to be (#362). Commanded, all of it:
// nothing reads a servo back.
struct ServoCommandedPosition {
    uint16_t nowUs;     // the width on the pin, part way through a move too
    uint16_t targetUs;  // where the move in progress ends; nowUs when none is
    bool pulsing;       // false until ServoTask has put a pulse on the pin
    // How many Find by Moving nudges have ENDED on this output since boot
    // (#363): returned on their own, cut short by an estop or a later command,
    // or refused before they began. A count rather than a flag because a whole
    // nudge can fall between two of the bench feed's one-second reads, so a
    // "nudging" bit could be missed; a count that has gone up cannot be.
    // Wraps, and that is fine: a reader compares it with what it read before
    // it asked, never with an absolute.
    uint8_t nudgesDone;
    // Whether the calibration dial has this output (#364, ADR 0064): both of
    // its firmware bounds are armed, and the pulse stays on until one fires,
    // the builder lets go, or the halt edge releases it.
    bool held;
    // Why there is no pulse, read only while `pulsing` is false.
    ServoLimpReason limp;
};

// -----------------------------------------------------------------------------
// Dome command message (sent via domeCmdQueue)
// -----------------------------------------------------------------------------
struct DomeCommand {
    float speed;         // -1.0 (full reverse) .. +1.0 (full forward), 0 = stop
    uint32_t durationMs; // 0 = indefinite (RC/web), >0 = auto-stop after this many ms
    CommandSource source;
    uint32_t timestampMs;
};

// -----------------------------------------------------------------------------
// RobotState  --  shared state, all access under robotStateMux
// -----------------------------------------------------------------------------
struct RobotState {
    // --- Zone 1: Drive output + drive backend feedback + failsafe gate (DriveTask) ---
    int16_t driveOutputSpeed;
    int16_t driveOutputSteer;
    CommandSource driveOutputSource;
    uint32_t driveOutputCommandMs;
    bool estop;
    bool sbusSignalLost;
    bool sbusHwFailsafe;
    bool webDriveExpired;
    FailsafeSource failsafeSource;
    uint32_t failsafeTriggerCount;
    uint32_t failsafeLastTriggerMs;        // millis() when last failsafe trigger latched
    uint32_t failsafeLastWatchdogMs;       // millis() when last SBUS watchdog trigger fired
    uint32_t failsafeLastZeroOutputMs;     // millis() when DriveTask first asserted zero output
    uint32_t failsafeLastTriggerToZeroMs;  // latency from trigger to first zero output (ms)
    FailsafeSource failsafeLastTriggerSource;
    // Drive backend feedback, filled from DriveFeedback (include/drive_backend.h)
    // where the fitted backend reports at all -- not every controller does.
    // Named for the direction rather than for a controller, beside the
    // driveOutput* fields that carry the other direction: A6 (#339) put the
    // foot drive behind a seam and DriveTask no longer knows what a hoverboard
    // is, and these were the last place in a generic path that said one
    // (#304 resolution 5+8, renamed under #346).
    //
    // Only true while frames keep arriving: driveFeedbackIsStale() below is
    // the rule, and DriveTask applies it.
    int16_t driveFeedbackBatteryRaw;
    int16_t driveFeedbackBoardTempRaw;
    int16_t driveFeedbackSpeedR;
    int16_t driveFeedbackSpeedL;
    int16_t driveFeedbackCurrentL;
    int16_t driveFeedbackCurrentR;
    bool driveFeedbackValid;
    uint32_t driveFeedbackAtMs;

    // --- Zone 2: RC input (RcInputTask) ---
    uint16_t rcPwmPulseUs[6];
    bool rcPwmPulseValid[6];
    uint16_t rcSbus1Raw[16];
    uint16_t rcSbus2Raw[16];
    bool rcSbus1Digital[2];
    bool rcSbus2Digital[2];
    uint32_t lastPwmMs;
    uint32_t lastSbus1Ms;
    uint32_t lastSbus2Ms;
    uint32_t sbus1LostFrameCount;  // cumulative lost_frame events (not failsafe)
    uint32_t sbus2LostFrameCount;  // cumulative lost_frame events (not failsafe)
    bool sbus2SignalLost;
    bool sbus2HwFailsafe;

    // --- Zone 3: Commanded Modes (multi-writer by design; ADR 0012) ---
    bool stationary;
    bool sleepMode;
    uint32_t sleepSinceMs;
    uint8_t activeMood;
    bool webControlEnabled;
    bool rcDebugMode;

    // --- Zone 4: Dome link (DomeLinkTask) ---
    float domeTargetSpeed;  // -1.0 .. +1.0
    float dome_speed;
    uint32_t domeHbRx;
    uint32_t bodyHbTx;
    uint32_t domeRxOverflowCount;
    uint32_t domeRxUnknownCount;
    DomeLinkTransport domeActiveTransport;
    DomeUartOwner domeUartOwner;
    uint32_t domeLastSeenMs;
    uint32_t domeLastSeenUartMs;
    uint32_t domeLastSeenWifiMs;

    // --- Zone 5: Audio (AudioTask) ---
    bool audioActive;
    bool audio_module_link_ok;
    uint8_t audio_module_play_state;
    uint8_t audio_module_device;
    uint16_t audio_module_total_tracks;
    uint16_t audio_module_current_track;
    uint16_t audio_module_missing_track;
    AudioRxStatus audio_module_rx_status;

    // --- Zone 6: Servo (ServoTask) ---
    // Where each output ServoTask drives has been told to be, indexed by armId
    // (ServoCommand::armId, 0=ARM1 .. 4=AUX3). Every surface that shows a
    // position reads it here, through captureServoOutputCommanded()
    // (include/api_status.h), and nowhere else (#362).
    //
    // Both widths are COMMANDED. Nothing on this droid reads a servo back -- no
    // encoder, no feedback path -- so neither is where the horn actually is,
    // and no surface may present one as measured.
    //
    // It replaced arm1TargetUs / arm2TargetUs, which covered ARM1 and ARM2 only
    // and left AUX1-3 with no mirror at all. And still no open/closed bit beside
    // the widths: there was one -- armOpen[2] -- deriving "open" from
    // `pulseUs > SERVO_PULSE_NEUTRAL_US`, which is wrong on any reversed
    // Endpoint Pair. Which end an output is at is derived from these widths and
    // the pair on its Servo Output row, where the direction is recorded (#345).
    ServoCommandedPosition servoCommanded[SERVO_ARM_COUNT];

    // --- Zone 7: Aux LED ---
    AuxLedState auxLed;

    // --- Zone 8: Sequence dispatcher (SequenceDispatcherTask writes; DomeLinkTask writes for coordination) ---
    bool domeSeqActive;    // true while a dome sequence is running (written by SequenceDispatcherTask and DomeLinkTask)
    uint32_t domeSeqUntilMs;   // safety timeout: auto-clear domeSeqActive at this millis() (written by SequenceDispatcherTask and DomeLinkTask)

    // --- Zone 9: Shared telemetry / documented handshake flags (multi-writer by design) ---
    uint32_t queueOverflowCount;  // shared telemetry counter, incremented by many tasks
    bool rcConfigDirty;  // Set by config apply, cleared by RcInputTask after rebuild
    bool seqStopRequested;  // Non-latching web stop signal (POST /api/seq/stop), cleared by SequenceDispatcherTask
    // A bulk centre the operator asked for (POST /api/servo/centre, #318 #365),
    // cleared by SequenceDispatcherTask when it starts the sweep. The same
    // transient-flag shape as seqStopRequested above, and for the same reason:
    // a web handler validates and signals, and the Coordinator owns the run.
    //
    // A CommandSource rather than a bool, with SRC_NONE meaning nobody has
    // asked, so the log line the sweep leaves names who pressed - the Console
    // and the browser both reach this and they are different surfaces.
    CommandSource bulkCentreRequest;
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
extern QueueHandle_t sequenceQueue;
// -----------------------------------------------------------------------------
// Helper function declarations (defined in main.cpp or a dedicated helpers.cpp)
// -----------------------------------------------------------------------------

// Load NVS config into the config cache
void loadConfigToState();

bool saveConfigToNvs();

// ----------------------------------------------------------------------------
// driveFeedbackIsStale()
//
// Whether the driveFeedback* mirror above has stopped being true. Feedback is
// a reading, not a setting: a backend that has gone quiet leaves the last
// numbers sitting in RobotState, and a surface showing them cannot tell them
// from live ones. So DriveTask invalidates the mirror rather than letting it
// go on being published.
//
// Pure, so the rule is testable away from the 50 Hz loop that applies it --
// the same shape as pwmSignalLostCheck() (include/rc_pwm_helpers.h).
//
// params: lastFeedbackMs - millis() when the last frame was stored (0 = never)
//         nowMs          - current timestamp (millis())
//         staleMs        - how long a reading stays true
// returns: true when the mirror must not be presented as live
// ----------------------------------------------------------------------------
inline bool driveFeedbackIsStale(uint32_t lastFeedbackMs, uint32_t nowMs, uint32_t staleMs) {
    if (lastFeedbackMs == 0) {
        return true;  // No frame has ever arrived, which is stale by definition
    }
    // Unsigned subtraction handles millis() overflow correctly
    return (uint32_t)(nowMs - lastFeedbackMs) > staleMs;
}

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
// FailsafeDiagnostics  --  canonical multi-field zone snapshot (ADR 0012)
// -----------------------------------------------------------------------------
struct FailsafeDiagnostics {
    bool estop;
    bool sbusSignalLost;
    bool sbusHwFailsafe;
    bool webDriveExpired;
    FailsafeSource failsafeSource;
    uint32_t failsafeTriggerCount;
    uint32_t failsafeLastTriggerMs;
    uint32_t failsafeLastZeroOutputMs;
    uint32_t failsafeLastTriggerToZeroMs;
    uint32_t failsafeLastWatchdogMs;
    FailsafeSource failsafeLastTriggerSource;
};

// Caller already holds robotStateMux (e.g. composing several zones in one
// critical section). Does not take/release the mutex itself.
inline void copyFailsafeDiagnosticsLocked(FailsafeDiagnostics* out) {
    out->estop = robotState.estop;
    out->sbusSignalLost = robotState.sbusSignalLost;
    out->sbusHwFailsafe = robotState.sbusHwFailsafe;
    out->webDriveExpired = robotState.webDriveExpired;
    out->failsafeSource = robotState.failsafeSource;
    out->failsafeTriggerCount = robotState.failsafeTriggerCount;
    out->failsafeLastTriggerMs = robotState.failsafeLastTriggerMs;
    out->failsafeLastZeroOutputMs = robotState.failsafeLastZeroOutputMs;
    out->failsafeLastTriggerToZeroMs = robotState.failsafeLastTriggerToZeroMs;
    out->failsafeLastWatchdogMs = robotState.failsafeLastWatchdogMs;
    out->failsafeLastTriggerSource = robotState.failsafeLastTriggerSource;
}

// Standalone capture: takes robotStateMux itself. Do not call while already
// holding the mutex.
inline void captureFailsafeDiagnostics(FailsafeDiagnostics* out) {
    taskENTER_CRITICAL(&robotStateMux);
    copyFailsafeDiagnosticsLocked(out);
    taskEXIT_CRITICAL(&robotStateMux);
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

// Read resolved drive output speed under mutex
inline int16_t getDriveSpeed() {
    int16_t speed;
    taskENTER_CRITICAL(&robotStateMux);
    speed = robotState.driveOutputSpeed;
    taskEXIT_CRITICAL(&robotStateMux);
    return speed;
}

// Read resolved drive output steer under mutex
inline int16_t getDriveSteer() {
    int16_t steer;
    taskENTER_CRITICAL(&robotStateMux);
    steer = robotState.driveOutputSteer;
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
