// =============================================================================
// include/robot_state.h
//
// Shared robot state structure for protoR2.
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
#include "board_outputs.h"  // BOARD_OUTPUT_COUNT - one lit-wire entry per Output
#include "config.h"
#include "dome_bearing.h"  // DomeBearingReading - what domeBearingRead() hands out
#include "dome_link_transport.h"
#include "drive_speed_preset.h"
#include "rc_mapping.h"
#include "reaction_status.h"  // ReactionStatus - one per trigger slot, for the RC page
#include "servo_backend.h"  // ServoOutputAddress, SERVO_OUTPUT_SLOT_COUNT - one mirror entry per Output

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
    // A Reaction (ADR 0053, #450): the droid fired the binding itself, on a
    // condition of its own. Appended, for the reason the two above were.
    SRC_REACTION,
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
        case SRC_REACTION:
            return "REACTION";
        default:
            return "?";
    }
}

enum RcInputMode : uint8_t {
    RC_INPUT_STANDARD_PWM = 0,
    RC_INPUT_SINGLE_SBUS,
    RC_INPUT_DUAL_SBUS,
    // An ELRS receiver (CRSF on the wire) is fitted, and the controller reads
    // no input from it yet (#369). A stored answer, not a decoder: the RC path
    // treats it exactly as no receiver present - rcInputStepStartupPlan()
    // starts no decoder and no RC task for a mode it does not read, so drive
    // stays on DriveTask's own zero frames and the failsafe layers are those of
    // a droid with no radio.
    RC_INPUT_ELRS,
    // No Radio Controller is fitted: a droid driven from the web alone
    // (GLOSSARY.md "Radio Controller", operator 2026-09-29 on #369). The same
    // shape as ELRS above - rcInputStepStartupPlan() starts no decoder and no
    // RC task - so the two radio Failsafe Layers never trigger and the stale
    // web drive command and the estop hold the feet. configApply() makes it
    // one answer: storing it clears the radio and every RC channel. Appended
    // last, because the number is what NVS stores and the four before it must
    // not move.
    RC_INPUT_NOT_FITTED,
};

// The RC receiver modes' words - the one home for them (ADR 0068, amended
// 2026-09-26): the rcInputMode Setting accepts exactly these, and every answer
// that reports a mode reads it from here. nullptr for a number that is no mode.
inline const char* rcInputModeName(uint8_t mode) {
    switch (mode) {
        case RC_INPUT_STANDARD_PWM:
            return "standard_pwm";
        case RC_INPUT_SINGLE_SBUS:
            return "single_sbus";
        case RC_INPUT_DUAL_SBUS:
            return "dual_sbus";
        case RC_INPUT_ELRS:
            return "elrs";
        case RC_INPUT_NOT_FITTED:
            return "not_fitted";
        default:
            return nullptr;
    }
}

// A mode as an answer reports it. The loader repairs an unknown stored number to
// dual_sbus, so that is what a number that is no mode reads as.
inline const char* rcInputModeToString(uint8_t mode) {
    const char* name = rcInputModeName(mode);
    return name != nullptr ? name : "dual_sbus";
}

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

// One lit wire, as the controller last set it. A droid may have several, each
// on its own Output (ADR 0067), so this is what ONE of them is showing and
// never the droid's lights as a whole.
struct AuxLedState {
    // Whether this Output carries a Light Type at all, as the config read at
    // start said. It is the "is there a light here" bit; `available` is the
    // narrower "and its driver started", and a lit wire whose RMT channel
    // failed is lit: false, available: false -- two different answers that a
    // single flag used to blur.
    bool lit;
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
    // positionUs and keep the dial's hold on it. A HOLD is a press: with no
    // hold standing it takes the Output and starts the ten-minute ceiling;
    // on a standing hold it refreshes the short expiry and nothing else, so a
    // page can keep a hold alive but never past the ceiling
    // (include/servo_hold.h). One output per command; 255 is refused.
    SERVO_CMD_HOLD,
    // The dial's keepalive and its own moves (#417): the same drive as a
    // HOLD, honoured only while the hold still stands and dropped otherwise,
    // so nothing but a press ever takes an Output that a bound, the estop or
    // pulses off let go. One output per command; 255 is refused.
    SERVO_CMD_HOLD_REFRESH,
    // Pulses off (ADR 0043, ADR 0064, #364): take the pulse off the pin. The
    // output goes limp where it is -- nothing is commanded -- and any move,
    // nudge or hold on it ends. 255 releases ARM1 and ARM2, as the other
    // broadcasts do.
    SERVO_CMD_RELEASE,
    // A Part run through its travel and back (ADR 0063, #352): out to the
    // recorded open end, across to the recorded close end, and back to the
    // width it started from, run to completion by ServoTask itself. The same
    // three-leg motion as a nudge over a different pair -- it carries no
    // target either, because the ends come off the Output's row
    // (include/servo_travel.h) and no source may name a width for it. Refused
    // on an Output nobody has measured, which has no recorded travel to run.
    // One output per command; 255 is refused.
    SERVO_CMD_TRAVEL,
    // A puppet string's target (#442, include/rc_puppet.h): the same drive as
    // a POSITION, at the Output's own Motion Profile, from the RC stick - and a
    // take's sample replayed by the Sequence Coordinator as SRC_SEQ
    // (include/take_replay.h), so the replay goes through the ramp the
    // performance did. Its own type for one reason: a hold outranks it. An Output the calibration
    // dial or a Find by Moving run holds drops a string's target, because the
    // builder is looking at that Part and a stick bumped on the bench must not
    // move it (processCommand()). One output per command; 255 is refused.
    SERVO_CMD_PUPPET,
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
    // Its Output Release: the row's release time ran out after its move
    // arrived (ADR 0043, #443). Appended, so every value above keeps its number.
    SERVO_LIMP_OUTPUT_RELEASE,
    // Its backend cannot be reached: the PCA9685 it is on did not answer at
    // start, or stopped answering (ADR 0043, "a bus drop is reported, not
    // escalated"; #444). Nothing can put a pulse on it until the droid
    // restarts with the expander answering.
    SERVO_LIMP_UNREACHABLE,
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
        case SERVO_LIMP_OUTPUT_RELEASE:
            return "release";
        case SERVO_LIMP_UNREACHABLE:
            return "unreachable";
        case SERVO_LIMP_OFF:
        default:
            return "off";
    }
}

struct ServoCommand {
    // Which Output, by its Output Address (include/servo_output_address.h), or
    // SERVO_OUTPUT_BOTH_ARMS for the board's first two together. Never an
    // index: an expander's channels are more addresses on this same field
    // (#444).
    ServoOutputAddress output;
    ServoCommandType type;  // Command type
    CommandSource source;
    uint16_t positionUs;    // Target pulse width (us) for POSITION type
    // A Gesture's own words about this one POSITION move (ADR 0049, #438):
    // the easing as ServoEasing + 1 and a full throw's time in ms, each 0 for
    // "the Output's own Motion Profile". Nothing else sets them, so every other
    // command still moves at the pace its Output's row sets.
    uint16_t motionThrowMs;
    uint8_t motionEasing;
};
// The address's second byte came out of a `timestampMs` every sender stamped
// and ServoTask never read, so the command shrank rather than growing every
// sender's frame and servoCmdQueue (#444). Larger than this grows both.
static_assert(sizeof(ServoCommand) == 10,
              "a larger ServoCommand grows servoCmdQueue and every sender's frame");

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
    // A move is still in progress: a ramp, an overshoot's settle back to its
    // target (which passes through `targetUs` on the way out, so equal widths
    // are not "arrived"), or an out-and-back with its dwells. A bulk centre or
    // the boot pass waits for this to fall before it starts the next Output or
    // takes a "go home and release" Output's drive off, so no two Outputs it
    // starts move together and a release never cuts a move short (#414, #417).
    bool moving;
};

// -----------------------------------------------------------------------------
// Dome command message (sent via domeCmdQueue)
// -----------------------------------------------------------------------------
// What a DomeCommand asks of DomeTask. SPEED is every command there was before
// the Dome Bearing (#445), and it is 0, so a sender that zero-fills its command
// and sets a speed keeps meaning exactly what it meant.
enum DomeCommandKind : uint8_t {
    DOME_CMD_SPEED = 0,          // speed and durationMs
    DOME_CMD_FRONT_IS_HERE = 1,  // the builder says the dome points front now
    DOME_CMD_TURN_TO = 2,        // turn the short way to targetTenths of Dome Bearing
};

struct DomeCommand {
    float speed;         // -1.0 (full reverse) .. +1.0 (full forward), 0 = stop
    uint32_t durationMs; // 0 = indefinite (RC/web), >0 = auto-stop after this many ms
    CommandSource source;
    // These two sit in the padding after `source`, so the struct is the size
    // it always was and no sender's frame grows - RC input's measured stack
    // chain among them. The assertion below holds that.
    DomeCommandKind kind;
    int16_t targetTenths;  // TURN_TO: the Dome Bearing to turn to, 0..3599
    uint32_t timestampMs;
};
static_assert(sizeof(DomeCommand) == 16,
              "DomeCommand grew - every domeCmdQueue sender's frame grows with it");

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
    // The boot hold: RC drive is zero until both drive sticks have been at
    // centre once since the RC task started (#389). Reported on /api/rc.
    bool rcDriveAwaitingCentre;
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
    // The Dome Bearing (include/dome_bearing.h, ADR 0051, #445), written by
    // DomeTask alone. A zeroed flag is UNKNOWN: `RobotState robotState = {}`
    // is the boot path, and a boot forgets, so the zero value must never read
    // as a number - least of all as front. Read both through domeBearingRead(),
    // which hands out NaN for an unknown bearing.
    bool domeBearingBelieved;
    float domeBearingDeg;

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
    // Where each output ServoTask drives has been told to be, one entry per
    // slot (include/servo_backend.h) - ServoTask's own index, which nobody
    // else holds. Every reader asks by Output Address through
    // servoCommandedOf() below; every surface that shows a position reads it
    // through captureServoOutputCommanded() (include/api_status.h), and
    // nowhere else (#362, #444).
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
    ServoCommandedPosition servoCommanded[SERVO_OUTPUT_SLOT_COUNT];

    // --- Zone 7: the lit wires ---
    // One per Output, in include/board_outputs.h's order, so an index here and
    // an index there are the same Output and neither end keeps a list of its
    // own. An Output that carries no light holds a zeroed entry, which reads as
    // lit: false -- the honest answer for a wire with nothing on it (#413).
    AuxLedState auxLed[BOARD_OUTPUT_COUNT];

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
    // A pose press (POST /api/seq/pose and the Console, #440): the sequence and
    // the instant to send the droid to, taken and cleared by
    // SequenceDispatcherTask. The same transient shape as bulkCentreRequest,
    // SRC_NONE meaning nobody has asked -- and deliberately not the sequence
    // queue: a wider SequenceRequest grows the frame of every sequenceStart()
    // caller, RC input's measured stack chain among them.
    CommandSource poseRequest;
    uint32_t poseRequestAtMs;
    char poseRequestName[24];
    // A flutter asked for by Output rather than by a sequence: a body-owned
    // Marcduino `:OFnn` (#453). The same transient shape as poseRequest: the
    // handler writes it under robotStateMux on whatever task it runs on, and
    // SequenceDispatcherTask takes and clears it and performs the flutter. The
    // Output is as the line names it, SERVO_OUTPUT_BOTH_ARMS for the 0/99
    // broadcast, and the length is the line's handler's to say. A flag rather
    // than a none-address, because a zeroed RobotState holds a real address.
    bool flutterRequest;
    ServoOutputAddress flutterRequestOutput;
    uint16_t flutterRequestMs;

    // --- Zone 10: Reactions (ReactionTask writes; the RC diagnostics read) ---
    ReactionStatus reactions[REACTION_STATUS_SLOTS];
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
// servoCommandedOf()
// Where ServoTask has told the Output at this address to be (#362), read under
// robotStateMux. An address ServoTask has no slot for - an Output no member of
// this image drives, `both`, or none - answers a zero-filled position: not
// pulsing, limp since boot, nothing moving, which is the honest answer for an
// Output nothing drives.
// -----------------------------------------------------------------------------
inline ServoCommandedPosition servoCommandedOf(ServoOutputAddress output) {
    ServoCommandedPosition commanded = {};
    const uint8_t slot = servoOutputSlotOf(output);
    if (slot >= SERVO_OUTPUT_SLOT_COUNT) {
        return commanded;
    }
    taskENTER_CRITICAL(&robotStateMux);
    commanded = robotState.servoCommanded[slot];
    taskEXIT_CRITICAL(&robotStateMux);
    return commanded;
}

// -----------------------------------------------------------------------------
// domeBearingRead()
// The Dome Bearing, read under robotStateMux: believed with its number, or
// unknown with NaN and no number at all. Every surface that shows the bearing
// and every act that turns to one reads it here.
// -----------------------------------------------------------------------------
inline DomeBearingReading domeBearingRead() {
    taskENTER_CRITICAL(&robotStateMux);
    const bool believed = robotState.domeBearingBelieved;
    const float deg = robotState.domeBearingDeg;
    taskEXIT_CRITICAL(&robotStateMux);
    DomeBearingReading reading = {false, NAN};
    if (believed) {
        reading.believed = true;
        reading.deg = deg;
    }
    return reading;
}

// -----------------------------------------------------------------------------
// Helper function declarations (defined in main.cpp or a dedicated helpers.cpp)
// -----------------------------------------------------------------------------

// Load NVS config into the config cache
void loadConfigToState();

// saveConfigToNvs() is a config store call: include/config_store.h.

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
