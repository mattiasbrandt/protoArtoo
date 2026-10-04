// =============================================================================
// src/tasks/sequence_dispatcher.cpp
//
// Body-side DM:* sequence coordinator task (ADR 0004, issue #2).
//
// The choreography cursor lives in the pure engine (sequence_engine.cpp);
// catalog and alias tables live in sequence_catalog.cpp. This file owns the
// FreeRTOS wiring: request queue, suppression window, estop transitions, and
// mapping engine actions onto the dome/audio queues.
//
// Dispatch policy: if a downstream queue is full mid-sequence, the engine
// action is retried on the next 10 ms tick (absolute step times  --  no drift).
// During preempt/estop cleanup the reset drain is best-effort instead, so a
// full queue can never stall an abort.
//
// Dispatch decision logic is extracted to sequence_dispatcher_step (ADR 0014):
// a pure decision module that takes a SeqAction and returns the target queue
// and command format. The task adapter executes those decisions. The pure
// functions (sequenceStart, sequenceLookup, sequenceActionToDomeCommand) are
// testable in the native environment without FreeRTOS/Arduino dependencies.
// The task adapter (dispatchAction, sequenceDispatcherTask) compiles with
// native test stubs for FreeRTOS, allowing full system behavior verification.
// =============================================================================

#include <Arduino.h>
#include <string.h>

#include "audio_task.h"
#include "config_cache.h"
#include "console_record.h"   // consoleReasonString() - the Availability Reason token
#include "dome_bearing_act.h"  // domeBearingStepPlan() - what a bearing step does here
#include "dome_turn_calibration.h"  // domeTurnCalibrationOf() - is the full turn recorded
#include "dome_link.h"
#include "logging.h"
#include "rc_puppet.h"   // rcPuppetTargetUs() - a take's sample lands where the string's target did
#include "robot_state.h"
#include "seq_store.h"
#include "sequence_body_step.h"
#include "sequence_bulk_centre.h"
#include "sequence_dispatcher.h"
#include "sequence_dispatcher_step.h"
#include "sequence_engine.h"
#include "sequence_flutter.h"
#include "sequence_gesture.h"
#include "sequence_pose.h"
#include "sequence_run_evidence.h"
#include "servo_motion_ramp.h"  // servoMotionArrivalMs() - how long a flutter's leg takes
#include "servo_task.h"  // servoTaskDrivesOutput() - an undriven Output is passed over (#364)
#include "take_replay.h"  // a run's takes, played beside its steps (#442)
#include "take_store.h"   // takeStoreReadBegin() - a take's file, read on through one open

#if defined(CONFIG_SPIRAM)  // SeqMotionState's allocation, below
#include <esp_heap_caps.h>
#include <new>
#endif

// Platform definition seam  --  hardware vs native test builds.
// This is the irreducible guard needed because queue definition must differ:
// hardware builds define the real queue with xQueueCreate; native builds use
// the stub version from native_test_stubs.cpp to avoid duplicate definitions.
// We also conditionally include esp_task_wdt.h (hardware only) and the
// stub header (native only).
#ifdef PA_NATIVE_TEST_STUBS
#include "esp_task_wdt_stubs.h"   // Native: stub declarations
#else
#include <esp_task_wdt.h>         // Hardware: real ESP-IDF watchdog

QueueHandle_t sequenceQueue = nullptr;

#if defined(CONFIG_SPIRAM)
static void motionStateAllocate();  // with SeqMotionState, below
#endif

void sequenceDispatcherInit() {
    sequenceQueue = xQueueCreate(4, sizeof(SequenceRequest));
#if defined(CONFIG_SPIRAM)
    motionStateAllocate();  // before the task is created: it reads `motion` from its first tick
#endif
}
#endif

static const char* TAG = "SEQ";

bool sequenceActionToDomeCommand(const SeqAction& act, uint32_t nowMs,
                                 DomeCommand& out) {
    if (act.kind != SEQ_ACT_DOME_ROTATE) {
        return false;
    }

    out = {};
    out.speed = (float)act.domeSpeedPct / 100.0f;
    out.durationMs = act.domeDurationMs;
    out.source = SRC_SEQ;
    out.timestampMs = nowMs;
    return true;
}

// -----------------------------------------------------------------------------
// dispatchBodyMove  --  a Body Step reaching the body servo path.
//
// The Part is resolved against the live Servo Output rows HERE, at dispatch,
// every time. Nothing about the answer is cached: wire the arm, let an Output
// record the Part, and the same saved step starts moving it with nothing
// re-authored (include/droid_part_availability.h). The rows are read one at a
// time because that is how the cache hands them out -- one 72-byte row on this
// frame rather than the whole 1682-byte table.
//
// A Part no Output claims is REPORTED and the sequence carries on: an unwired
// Part is the normal state of a build in progress, so the step is inert and the
// rest of the choreography is untouched (#301). Returning true is therefore
// correct -- the action was handled, and only a full queue is a retry.
//
// A FLUTTER IS NOT SENT FROM HERE. It is not one move: the Part swings between
// its closed end and the step's how-far point for the flutter's length and
// ends closed (ADR 0049, amended 2026-10-02; #453). The engine hands the step
// over when it is due and moves on, and the flutter run below performs the
// swing on this task's own tick (flutterStartPart(), flutterOneLeg(),
// include/sequence_flutter.h): each leg is a move through sendBodyPosition(),
// at the Output's own Motion Profile, and legs on different Outputs are held
// apart by the Cadence Floor, because a leg is generated motion. So a routine
// fluttering several Parts at once starts their legs one at a time.
// -----------------------------------------------------------------------------
// The live Servo Output row that claims `part`, read into `row`, or nullptr
// when none does. Shared by a Body Step and a pose's body move, so both find a
// Part's Output the same way.
static const ServoOutputRow* rowForPart(const char* part, ServoOutputRow* row) {
    const uint8_t rowCount = configCacheServoOutputCount();
    for (uint8_t i = 0; i < rowCount; ++i) {
        if (configCacheReadServoOutput(i, row) && servoOutputDrivesPart(*row, part)) {
            return row;
        }
    }
    return nullptr;
}

// What a body move did, for a caller that paces what comes after it: whether
// a command went to ServoTask, the Output it went to and that Output's full
// throw. A pose press is the caller (poseOneCommand() below).
struct BodyMoveOutcome {
    bool     sent;
    ServoOutputAddress output;
    uint16_t throwMs;
};

// The one ServoCommand a planned body move becomes: a Body Step's, a pose's, a
// Gesture's and each leg of a flutter alike. SRC_SEQ, which ServoTask refuses
// in Sleep Mode. False when servoCmdQueue is full: the caller tries again on
// the next tick.
static bool sendBodyPosition(const SeqBodyStepPlan& plan, uint16_t throwMs, uint8_t easing) {
    ServoCommand cmd = {};
    cmd.output = plan.output;
    cmd.type = SERVO_CMD_POSITION;
    cmd.positionUs = plan.targetUs;
    cmd.source = SRC_SEQ;
    cmd.motionThrowMs = throwMs;
    cmd.motionEasing = easing;
    return xQueueSend(servoCmdQueue, &cmd, 0) == pdTRUE;
}

static void takesStepMoved(const char* partId);  // with the takes, below

// `throwMs` and `easing` are a Gesture's own words for this move (0 for each:
// the Output's own Motion Profile); only a Gesture's move passes them (ADR 0049).
static bool dispatchBodyMove(const SeqAction& act, BodyMoveOutcome* outcome = nullptr,
                             uint16_t throwMs = 0, uint8_t easing = 0) {
    ServoOutputRow row = {};
    const ServoOutputRow* driving = rowForPart(act.payload, &row);

    const SeqBodyStepPlan plan = sequenceBodyStepPlan(act, driving);
    if (!plan.drive) {
        PA_LOG_INFO(TAG, "body %s not moved - %s", act.payload,
                    consoleReasonString(plan.reason));
        return true;  // inert step; the sequence carries on
    }

    if (!sendBodyPosition(plan, throwMs, easing)) {
        return false;
    }
    takesStepMoved(act.payload);  // a step's move wins over a take's (include/take_replay.h)
    if (outcome != nullptr) {
        outcome->sent = true;
        outcome->output = plan.output;
        outcome->throwMs = (throwMs != 0) ? throwMs : row.throw_ms;  // the move as asked
    }
    return true;
}

// -----------------------------------------------------------------------------
// dispatchDomeBearing  --  a bearing step reaching DomeTask (#445).
//
// Resolved HERE, when the step runs, every time: the target's bearing from the
// catalog table (a corrected `bearing_deg` reaches every saved step), and the
// dome's belief and calibration as they are now. A step that cannot turn the
// dome - the bearing unknown, the dome not calibrated, the Dome ESC off - is
// REPORTED and the sequence carries on, the shape a Body Step's
// part-not-assigned takes (domeBearingStepPlan(), include/dome_bearing_act.h).
// Returning true is therefore correct; only a full queue is a retry.
//
// What goes out is the target, not a timed turn: DomeTask plans the turn from
// its own belief when the command reaches it, because the dome may have moved
// in between, and asks the calibration and belief questions again.
// -----------------------------------------------------------------------------

// Whether the full turn is recorded, read on its own so dispatchDomeBearing()'s
// frame does not carry a DomeConfig onto the route its log line takes.
static bool __attribute__((noinline)) domeCalibratedNow() {
    DomeConfig dome = {};
    configCacheReadDome(&dome);
    return domeTurnCalibrated(domeTurnCalibrationOf(dome));
}

// Out of line on purpose: dispatchAction() sits on this task's deepest route
// (drainBestEffort -> dispatchAction -> a queue-drop log line), and inlined
// there this plan and its log call grew dispatchAction's frame to 352 B and the
// measured chain by 336 B, past the stack's rule.
static bool __attribute__((noinline)) dispatchDomeBearing(const SeqAction& act) {
    const DomeBearingStepPlan plan =
        domeBearingStepPlan(act.payload, configCacheReadActiveDomeEnabled(), domeCalibratedNow(),
                            domeBearingRead().believed);
    if (!plan.turn) {
        PA_LOG_INFO(TAG, "dome not turned to %s - %s", act.payload,
                    consoleReasonString(plan.reason));
        return true;  // inert step; the sequence carries on
    }
    DomeCommand cmd = {};
    cmd.kind = DOME_CMD_TURN_TO;
    cmd.targetTenths = plan.targetTenths;
    cmd.source = SRC_SEQ;
    cmd.timestampMs = millis();
    return xQueueSend(domeCmdQueue, &cmd, 0) == pdTRUE;
}

// -----------------------------------------------------------------------------
// dispatchBackgroundTrackStart  --  a Background Track step reaching AudioTask
// (ADR 0054).
//
// Sent whatever the fitted module, because AudioTask is the one audio seam that
// refuses a Background Track a module cannot mix (AUDIO_STEP_IGNORE_CANNOT_MIX)
// -- the gate stays in one place. What this adds is the run's own report, in
// the shape a Body Step's part-not-assigned takes: the step was asked for, it
// does not play, and why. The rest of the routine is untouched; only a full
// queue is a retry.
//
// Both are out of line. The report: inline, its log line's frame (320 B) sat
// above the queue helper's queue-drop log line and walked this task's chain
// 256 B past its recorded figure. The dispatch itself: inline, its two reason
// branches grew dispatchAction()'s frame by 16 B, on the route a Body Step's
// log line takes.
// -----------------------------------------------------------------------------
static void __attribute__((noinline)) reportBackgroundTrackNotPlayed(const char* sound,
                                                                   ConsoleReason reason) {
    PA_LOG_INFO(TAG, "Background Track %s not played - %s", sound, consoleReasonString(reason));
}

static bool __attribute__((noinline)) dispatchBackgroundTrackStart(const SeqAction& act) {
    // act.audioCategory carries the volume (SEQ_ACT_BACKGROUND_TRACK_START).
    if (!audioQueueBackgroundTrackStart(act.payload, act.audioCategory, SRC_SEQ)) {
        // A full queue: Protocol Check let only a '$' that fits the queue entry
        // into the step, so this is never the malformed case. Retried on the
        // next tick, and reported once it is sent.
        return false;
    }
    // The true reason, Sound switched off before the module: with Sound off
    // nothing plays at all, mixing or not, and the queue helper accepted the
    // step without sending it (ADR 0027).
    if (!configCacheReadActiveAudioEnabled()) {
        reportBackgroundTrackNotPlayed(act.payload, CONSOLE_REASON_COMPONENT_DISABLED);
    } else if ((audioGetCapabilities() & AudioDriver::AUDIO_CAP_MIXES) == 0) {
        reportBackgroundTrackNotPlayed(act.payload, CONSOLE_REASON_MODULE_CANNOT_MIX);
    }
    return true;
}

// -----------------------------------------------------------------------------
// centreOneOutput  --  one row's turn in a bulk centre sweep or a boot pass
// (#318, #365, #414).
//
// The operator asked once, from the output-first table, or the droid powered
// up; this is the Coordinator expanding that into one Output move, and the
// run's cursor spaces the next one by the Cadence Floor
// (include/sequence_bulk_centre.h). Nothing about the pace is the browser's,
// which is the whole point: a safe cadence a page held could be walked around
// by a hand-edited or imported client.
//
// The row is read from the LIVE table at the moment its turn comes, one row at
// a time because that is how the cache hands them out, so a table saved
// mid-sweep is read as it now stands rather than as it was when the run began.
// Whether the row moves at all is sequenceBulkCentreRowStep()'s answer: every
// row with travel on a press, and on the boot pass only the rows whose boot
// behaviour sends them home.
//
// The Output the last started row moved goes first. Its full-throw time
// (floored) is the earliest the next row is looked at, and the next row starts
// only once ServoTask reports that Output's move over, so no two Outputs are in
// motion together and an overshoot is never cut mid-settle
// (sequenceBulkCentreAwaitCheck()). A release the boot pass owes goes then,
// and the next row waits behind it too. The drive comes off through
// ServoTask's own SERVO_CMD_RELEASE, the one path a pulse comes off a pin by.
// It is sent even when the table shrank under the run, because the Output it
// names was already driven.
//
// Every command a run sends is SRC_SEQ, whoever pressed and whether or not
// anybody did: expanding the press, or the power-up, into moves is the
// Coordinator acting, and SRC_SEQ is what ServoTask refuses in Sleep Mode. So
// a move still on servoCmdQueue when Sleep Mode ends the run is dropped there
// rather than driving an Output the droid has just let go of. `run.src` names
// who asked, in the line that reports the run done.
//
// The cursor is what says whether the turn is over: every path that dealt with
// the row advances it, and the one path that could not - a full servoCmdQueue -
// leaves it alone, so the same row comes round again on the next tick, exactly
// as the staged ring close above holds its index. A row with nothing to centre,
// a limp row on the boot pass, or one this image cannot drive, is counted and
// passed over.
// -----------------------------------------------------------------------------
static void centreOneOutput(SeqBulkCentreRun& run, uint32_t now) {
    const uint8_t rowCount = configCacheServoOutputCount();
    const uint32_t floorMs = configCacheCadenceFloorMs();

    if (run.awaitOutput != SERVO_OUTPUT_NONE) {
        const ServoCommandedPosition at = servoCommandedOf(run.awaitOutput);
        const SeqBulkCentreAwait awaited = sequenceBulkCentreAwaitCheck(run, now, at);
        if (awaited == SEQ_AWAIT_WAIT) {
            return;  // still moving: looked at again on the next tick
        }
        if (awaited == SEQ_AWAIT_RELEASE) {
            ServoCommand cmd = {};
            cmd.output = run.awaitOutput;
            cmd.type = SERVO_CMD_RELEASE;
            cmd.source = SRC_SEQ;
            if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
                return;  // owed still: it comes round again on the next tick
            }
        } else if (awaited == SEQ_AWAIT_DROP) {
            PA_LOG_INFO(TAG, "%s:%u has no pulse left to release",
                        servoOutputDriverToString(run.awaitOutput.driver),
                        (unsigned)run.awaitOutput.channel);
        }
        sequenceBulkCentreAwaitOver(&run, rowCount);
        if (!run.active) {
            return;
        }
    }

    ServoOutputRow row = {};
    if (run.nextRow >= rowCount || !configCacheReadServoOutput(run.nextRow, &row)) {
        // The table shrank under the sweep - a save between two rows. End it
        // here rather than walking past the end of the table.
        sequenceBulkCentreEnd(&run);
        return;
    }

    const SeqBulkCentreRowStep step = sequenceBulkCentreRowStep(run, row);
    if (!step.centre) {
        sequenceBulkCentreAdvance(&run, rowCount, now, /*started=*/false, 0, floorMs);
        return;
    }

    const SeqBodyStepPlan plan = sequenceBodyCentrePlan(row);
    if (!plan.drive) {
        PA_LOG_INFO(TAG, "output %u not centred - %s", (unsigned)run.nextRow,
                    consoleReasonString(plan.reason));
        sequenceBulkCentreAdvance(&run, rowCount, now, /*started=*/false, 0, floorMs);
        return;
    }
    // An Output ServoTask does not drive since boot - a wired tick saved
    // since, or none at all - would drop the move without a word, and the run
    // would still spend a Cadence Floor slot waiting on it. Passed over and
    // counted instead, costing no time (#364). POST /api/servo/centre names
    // these rows in its answer (servoCentreSkipped(), src/web/api_servo.cpp).
    if (!servoTaskDrivesOutput(plan.output)) {
        PA_LOG_INFO(TAG, "%s:%u not centred - restart the droid to use it",
                    servoOutputDriverToString(plan.output.driver), (unsigned)plan.output.channel);
        sequenceBulkCentreAdvance(&run, rowCount, now, /*started=*/false, 0, floorMs);
        return;
    }

    // The same command a Body Step queues, to the same queue, with the same
    // clamp already applied: one motion path, and ServoTask still decides the
    // move's own shape from the Output's Motion Profile (ADR 0052).
    ServoCommand cmd = {};
    cmd.output = plan.output;
    cmd.type = SERVO_CMD_POSITION;
    cmd.positionUs = plan.targetUs;
    cmd.source = SRC_SEQ;
    if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
        return;  // the cursor stays put: this row's turn comes round again
    }
    sequenceBulkCentreAwait(&run, plan.output, step.releaseAfter);
    sequenceBulkCentreAdvance(&run, rowCount, now, /*started=*/true, row.throw_ms, floorMs);
}

// -----------------------------------------------------------------------------
// poseOneCommand  --  one command of a pose press (#440, include/sequence_pose.h).
//
// The plan was worked out once, when the request arrived, from the stored
// steps; this sends its next command, and the run's cursor spaces the one
// after. Sound and light commands go through the same dispatch a run uses. A
// dome panel goes to the dome link as one individual command -- the plan never
// holds a group target. A body Part goes through dispatchBodyMove(), the very
// path a Body Step takes: resolved against the live Output rows when its turn
// comes, and queued as SRC_SEQ, which ServoTask refuses under either halt.
// Nothing here holds a row or a command of its own: this function is inlined
// into the task's root frame, whose size is a measured figure (ADR 0040).
//
// The Output the last body command moved goes first: the next command waits
// until ServoTask no longer reports it moving, so no two Outputs are in motion
// together (the rule centreOneOutput() above keeps). A full queue leaves the
// cursor where it is, so the same command comes round on the next tick. A Part
// nothing can move is reported, passed over and costs no time.
// -----------------------------------------------------------------------------
// The plan is held in SeqMotionState, below.

static bool dispatchAction(const SeqAction& act);  // defined with the task adapter below

// -----------------------------------------------------------------------------
// The Gestures a sequence has fired (#438, include/sequence_gesture.h), on the
// Coordinator's own cursor so they never hold the engine's. Off this task's
// measured stack, as the pose plan is (ADR 0040, SeqMotionState below); so is
// the one body move a turn builds, which would otherwise sit on the root frame.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// The flutters a sequence has fired (#453, include/sequence_flutter.h): a Body
// Step's, and each member's of a body Gesture. On the Coordinator's own cursor,
// like the Gestures, so a two-second shake never holds the engine's. Off this
// task's measured stack (ADR 0040, SeqMotionState below), and so are the row a
// leg is resolved against and the move it is planned as.
//
// A leg keeps the pace a sequence's generated motion keeps, and that pace is
// ONE: the Gesture run's `dueMs` and `awaitOutput`. A Gesture's move and a
// flutter's leg are both this task starting a body Output on its own account,
// so each holds the other off; two paces side by side would let two Outputs
// start together, which is what the Cadence Floor is there to prevent.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// SeqMotionState  --  the pose plan, the Gesture and flutter runs, and the
// scratch row and moves they build, as one object (about 5.1 KB). Every use
// names it through `motion`, read and written from this task only, never an
// ISR.
//
// Where it lives depends on whether the chip has PSRAM:
//   - No PSRAM (artoo-esp32): a static, off this task's measured stack
//     (ADR 0040). `motion` is a constant pointer to it, so every access
//     compiles to the direct one a plain static gets.
//   - PSRAM (the P4): there .bss comes out of the internal heap, which a page
//     load can run down to its last bytes, so sequenceDispatcherInit() takes
//     the object from PSRAM once, before this task runs, and keeps it for the
//     life of the firmware. heap_caps_malloc_prefer() names PSRAM first and
//     internal RAM second, rather than leaving the choice to malloc's size
//     threshold (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL, 4,096 B), the pattern
//     src/take.cpp uses. If neither heap has room, `motion` stays nullptr: the
//     failure is logged once at boot, and from then on a pose press, a
//     Gesture and a flutter are refused without running (motionReady()),
//     while a sequence's other steps, back to centre and the takes go on.
//
// `#if defined` rather than `#if`: the sdkconfig defines CONFIG_SPIRAM only
// where it is set (the P4's), and artoo's custom_sdkconfig leaves it unset
// (platformio.ini, the WiFi buffer block). The native build has no sdkconfig,
// so it takes the static branch.
// -----------------------------------------------------------------------------
struct SeqMotionState {
    SeqPosePlan posePlan;
    SeqGestureRun gestureRun;
    SeqAction gestureMove;
    SeqFlutterRun flutterRun;
    ServoOutputRow flutterRow;
    SeqAction flutterMove;
};

#if defined(CONFIG_SPIRAM)
static SeqMotionState* motion = nullptr;

static void motionStateAllocate() {
    void* storage =
        heap_caps_malloc_prefer(sizeof(SeqMotionState), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (storage == nullptr) {
        PA_LOG_ERROR(TAG,
                     "no memory for the pose, Gesture and flutter state (%u B) - "
                     "poses, Gestures and flutters will be refused",
                     (unsigned)sizeof(SeqMotionState));
        return;
    }
    motion = new (storage) SeqMotionState();  // value-initialised, as the static was
}
#else
static SeqMotionState motionStorage;
static constexpr SeqMotionState* motion = &motionStorage;
#endif

// True when the state above exists: always where it is static, so the tests
// below fold away there; on the P4, when the boot allocation succeeded.
static inline bool motionReady() {
    return motion != nullptr;
}

// Whether a Gesture, or a flutter, is being performed: never, with no state.
static inline bool gesturesActive() {
    return motionReady() && sequenceGestureActive(motion->gestureRun);
}

static inline bool fluttersActive() {
    return motionReady() && sequenceFlutterActive(motion->flutterRun);
}

static_assert(SEQ_FLUTTER_PARTS_MAX >= SEQ_GESTURE_MEMBERS_MAX,
              "every member of one Gesture can flutter at once");

// -----------------------------------------------------------------------------
// The takes the running sequence plays (#442, include/take_replay.h,
// which carries the rules). One heap block, claimed from the store when the
// run starts and freed when it ends; nullptr between runs, which is all a take
// costs while none plays.
//
// Each take's file is read a few samples at a time, through one open a tick
// (takeStoreReadBegin()), which waits on the take store's lock the way a run's
// load waits on the sequence store's. Each sample goes to ServoTask the way a
// puppet string's target does - SERVO_CMD_PUPPET, at the Output's own Motion
// Profile, its Part resolved to an Output as it goes - but as SRC_SEQ, so
// ServoTask refuses it in Sleep Mode as it refuses every sequence move, and
// under the estop as it refuses everything. Like every function on this
// task's measured chain (ADR 0040), each is out of line with its state in the
// heap block, and the log lines sit in functions that only log.
// -----------------------------------------------------------------------------
static TakeReplayRun* takeRun = nullptr;

// servoCmdQueue places a take leaves free: a step's move and a cue always find
// one of the last four. A puppet string stops at the same four
// (rc_input.cpp's kPuppetQueueReserve), so strings and takes share what is
// above them. A target turned away is still owed, so it goes on the next tick.
static constexpr UBaseType_t kTakeQueueReserve = 4;

static __attribute__((noinline)) void takesStepMoved(const char* partId) {
    if (takeRun != nullptr) {
        takeReplayStepMoved(takeRun, droidPartIndexOf(partId), millis());
    }
}

static __attribute__((noinline)) void takeLogPart(const TakeReplay& t, const char* part,
                                                  const char* why) {
    PA_LOG_INFO(TAG, "take %s: body %s not moved - %s", t.id, part, why);
}

static __attribute__((noinline)) void takeLogStopped(const TakeReplay& t, const char* why) {
    PA_LOG_WARN(TAG, "take %s not played on - %s", t.id, why);
}

// The run has just started: the takes it names, if any, are this task's now.
// Those that will not play - past what this board keeps, or with no memory
// for them - are said here, and the steps run anyway. A toggle sequence plays
// its takes on its open half only (operator, 2026-10-03): the half the engine
// has just chosen is the close half when it runs the entry's closeSteps, and
// that half plays its steps alone.
static __attribute__((noinline)) void takesBegin(uint32_t now, const SeqEngineState& engine) {
    SeqStoreTakesUnplayed unplayed = {};
    takeRun = seqStoreClaimRunTakes(&unplayed);
    if (engine.entry->closeSteps != nullptr && engine.steps == engine.entry->closeSteps) {
        if (takeRun != nullptr || unplayed.overCap != 0 || unplayed.noMemory != 0) {
            PA_LOG_INFO(TAG, "takes not played - the close half plays its steps only");
        }
        free(takeRun);
        takeRun = nullptr;
        return;
    }
    if (unplayed.overCap != 0) {
        PA_LOG_WARN(TAG, "%u take(s) not played - this droid plays %u a sequence",
                    (unsigned)unplayed.overCap, (unsigned)TAKE_STORE_CAP);
    }
    if (unplayed.noMemory != 0) {
        PA_LOG_WARN(TAG, "%u take(s) not played - no memory for them; the steps run",
                    (unsigned)unplayed.noMemory);
    }
    if (takeRun != nullptr) {
        takeRun->startMs = now;
        PA_LOG_INFO(TAG, "%u take(s) play with this run", (unsigned)takeRun->count);
    }
}

static __attribute__((noinline)) void takesEnd(const char* why) {
    if (takeRun != nullptr) {
        PA_LOG_INFO(TAG, "takes ended (%s)", why);
        free(takeRun);
        takeRun = nullptr;
    }
}

// A take's start has come: its header, read through one open into its own
// sample buffer so nothing sits on this task's stack - the fixed 16 bytes,
// then each Part id. A Part the catalog does not hold is said now, once for
// this take, and passed over for the run.
// Returns why the take cannot play, or nullptr.
static __attribute__((noinline)) const char* takeOpen(TakeReplay* t) {
    uint8_t* b = (uint8_t*)t->buf;
    TakeFileInfo info = {};
    if (!takeStoreReadBegin(takeRun->owner, t->id, 0)) {
        return "its file could not be read";  // gone, or the store's lock timed out
    }
    const char* stop = nullptr;
    if (takeStoreReadOn(b, TAKE_FILE_FIXED_BYTES) != TAKE_FILE_FIXED_BYTES ||
        !takeFileReadFixed(b, &info)) {
        stop = "its file is not a take this droid can read";
    } else {
        takeReplayBegin(t, info);
        static_assert(sizeof(t->buf) >= TAKE_PART_ID_BYTES, "a Part id is read into the buffer");
        for (uint8_t p = 0; p < t->partCount; ++p) {
            if (takeStoreReadOn(b, TAKE_PART_ID_BYTES) != TAKE_PART_ID_BYTES ||
                !takeFilePartIdValid(b)) {
                stop = "it names a Part this droid cannot read";
                break;
            }
            const size_t part = droidPartIndexOf((const char*)b);
            t->part[p] = (uint8_t)((part < DROID_PART_COUNT) ? part : DROID_PART_COUNT);
            if (part >= DROID_PART_COUNT) {
                // Not part-not-assigned: that says no Output claims a Part the
                // droid knows, and this id is not in its catalog at all.
                takeLogPart(*t, (const char*)b, "not a Part this droid knows");
            }
        }
    }
    takeStoreReadEnd();
    return stop;
}

// The take's samples from its file, read on through one open and each checked
// as it arrives as a restore checks them (takeFileSampleValid()), taken into
// the Parts' targets piece by piece until every sample due by `runMs` is in:
// one piece on most ticks, and at a trimmed take's start every sample before
// its in-point (include/take_replay.h, A TRIMMED TAKE). The file's byte order
// is both chips' own (take_capture.h), so they land in the buffer as they are.
// Returns why the take cannot play on, or nullptr.
static __attribute__((noinline)) const char* takeRefill(TakeReplay* t, uint32_t runMs) {
    const size_t at = takeFileHeaderBytes(t->partCount) + (size_t)t->samplesRead * TAKE_SAMPLE_BYTES;
    // A begin fails on a file that is gone and on the store's lock timing
    // out alike, so it says only that the file could not be read.
    if (!takeStoreReadBegin(takeRun->owner, t->id, at)) {
        return "its file could not be read";
    }
    const TakeFileInfo info = takeReplayInfo(*t);
    const char* stop = nullptr;
    for (;;) {
        const uint16_t left = (uint16_t)(t->sampleCount - t->samplesRead);
        const uint8_t n = (left < TAKE_REPLAY_BUF_SAMPLES) ? (uint8_t)left : TAKE_REPLAY_BUF_SAMPLES;
        const size_t bytes = (size_t)n * TAKE_SAMPLE_BYTES;
        if (takeStoreReadOn((uint8_t*)t->buf, bytes) != bytes) {
            stop = "its file is cut short";
            break;
        }
        uint8_t k = 0;
        while (k < n && takeFileSampleValid(t->buf[k], t->prevTick, info)) {
            t->prevTick = takeSampleTick(t->buf[k]);
            ++k;
        }
        if (k < n) {
            stop = "its samples are out of order or out of range";
            break;
        }
        t->samplesRead = (uint16_t)(t->samplesRead + n);
        t->bufAt = 0;
        t->bufCount = n;
        if (!takeReplayConsume(t, runMs)) break;
    }
    takeStoreReadEnd();
    return stop;
}

// Part `p` of take `i`, if it owes a target: sent unless a later take covers
// the Part, or a step's move is still holding it. The rules are
// include/take_replay.h's.
static __attribute__((noinline)) void takeSendPart(uint8_t i, uint8_t p, uint32_t now) {
    TakeReplay* t = takeReplayAt(takeRun, i);
    const uint8_t part = t->part[p];
    const uint16_t bit = (uint16_t)(1u << p);
    // A take that is over says nothing more: it still holds its last
    // targets, and a later take ending would otherwise have it send one
    // back and jump the Part to where the take left off.
    if (t->state != TAKE_REPLAY_PLAYING || part >= DROID_PART_COUNT ||
        t->cur[p] == TAKE_NO_TARGET) {
        return;
    }
    // Asked before the cur == sent test below, on purpose: the earlier take
    // must be marked owed while it is outranked even when its own target has
    // not changed, or it would not send it again once the later take stops.
    if (takeReplayOutranked(takeRun, i, p)) {
        t->sent[p] = TAKE_NO_TARGET;  // owed again once the later take stops covering it
        return;
    }
    if (t->cur[p] == t->sent[p]) {
        return;
    }
    const char* partId = droidPartIdAt(part);
    ServoOutputAddress address = SERVO_OUTPUT_NONE;
    uint16_t openUs = 0;
    uint16_t closeUs = 0;
    const bool wired = configCacheReadPartOutputEnds(partId, &address, &openUs, &closeUs) &&
                       servoOutputSlotOf(address) != SERVO_OUTPUT_SLOT_NONE;
    if (!wired || !servoTaskDrivesOutput(address)) {
        // Said once a run, whichever take covers the Part; the take asks
        // again only when it moves the Part on, so wiring the Part mid-run
        // picks it up at its next change.
        if (takeReplayTellOnce(takeRun, part)) {
            takeLogPart(*t, partId,
                        wired ? "restart the droid to use its Output"
                              : consoleReasonString(CONSOLE_REASON_PART_NOT_ASSIGNED));
        }
        t->sent[p] = t->cur[p];
        return;
    }
    if ((motionReady() && sequenceFlutterHasPart(motion->flutterRun, part)) ||
        // Signed: the step stamps millis() when it is sent, which can be
        // later than this tick's `now` - on a run's first tick always, since
        // `now` is taken before the load - and an unsigned difference would
        // wrap and let the take's target in behind the step's move.
        ((t->held & bit) != 0 &&
         ((int32_t)(now - takeRun->stepAtMs) < (int32_t)TAKE_REPLAY_STEP_SETTLE_MS ||
          servoCommandedOf(address).moving))) {
        t->sent[p] = t->cur[p];  // the step wins; the take moves the Part on its next change
        return;
    }
    t->held &= (uint16_t)~bit;
    if (uxQueueSpacesAvailable(servoCmdQueue) <= kTakeQueueReserve) {
        return;
    }
    ServoCommand cmd = {};
    cmd.output = address;
    cmd.type = SERVO_CMD_PUPPET;
    cmd.positionUs = rcPuppetTargetUs(openUs, closeUs, t->cur[p]);
    cmd.source = SRC_SEQ;
    if (xQueueSend(servoCmdQueue, &cmd, 0) == pdTRUE) {
        t->sent[p] = t->cur[p];
    }
}

// Once a tick while a run plays takes, after its steps: each take whose start
// has come opens, takes the samples due by now - refilling as it goes - and
// ends when its length runs out; then every Part that owes a target sends it.
static __attribute__((noinline)) void takesTick(uint32_t now) {
    const uint32_t runMs = now - takeRun->startMs;
    for (uint8_t i = 0; i < takeRun->count; ++i) {
        TakeReplay* t = takeReplayAt(takeRun, i);
        const char* stop = nullptr;
        if (t->state == TAKE_REPLAY_WAITING && runMs >= t->atMs) {
            stop = takeOpen(t);
        }
        if (stop == nullptr && t->state == TAKE_REPLAY_PLAYING && takeReplayConsume(t, runMs)) {
            stop = takeRefill(t, runMs);
        }
        if (stop != nullptr) {
            takeLogStopped(*t, stop);
            t->state = TAKE_REPLAY_OVER;
        }
    }
    for (uint8_t i = 0; i < takeRun->count; ++i) {
        for (uint8_t p = 0; p < takeReplayAt(takeRun, i)->partCount; ++p) {
            takeSendPart(i, p, now);
        }
    }
}

// Every path that ends what the Coordinator is doing ends the Gestures and the
// flutters too, where they have got to: a halt, a stop, a later run, a pose,
// back to centre and the run's own end step alike. Nothing is commanded on the
// way out (ADR 0043) -- a Part a flutter left out stays out. The takes end on
// the same paths, and a Part a take moved stays where it was put.
static void generatedEnd(const char* why) {
    if (motionReady()) {
        if (sequenceGestureActive(motion->gestureRun)) {
            PA_LOG_INFO(TAG, "gesture ended (%s) after %u sent, %u skipped", why,
                        (unsigned)motion->gestureRun.sent, (unsigned)motion->gestureRun.skipped);
        }
        sequenceGestureEnd(&motion->gestureRun);
        if (sequenceFlutterActive(motion->flutterRun)) {
            PA_LOG_INFO(TAG, "flutter ended (%s) after %u legs", why,
                        (unsigned)motion->flutterRun.legs);
        }
        sequenceFlutterEnd(&motion->flutterRun);
    }
    takesEnd(why);
}

// The move a flutter's Part is planned with: the Part, by its catalog id, and
// the swing's far end as an open that far. Built in the static scratch action.
static void flutterPlanMove(const char* partId, uint8_t howFar) {
    memset(&motion->flutterMove, 0, sizeof(motion->flutterMove));
    motion->flutterMove.kind = SEQ_ACT_BODY_MOVE;
    strncpy(motion->flutterMove.payload, partId, sizeof(motion->flutterMove.payload) - 1);
    motion->flutterMove.bodyShape = (uint8_t)BODY_SHAPE_OPEN;
    motion->flutterMove.bodyHowFar = howFar;
}

// A later move of a Part ends that Part's flutter; the later move is the one
// performed (#453). A Body Step's move and a Gesture's each call this once the
// move has been sent, so no leg can follow it.
static void flutterEndPart(const char* partId) {
    const size_t part = droidPartIndexOf(partId);
    if (motionReady() && part < DROID_PART_COUNT &&
        sequenceFlutterEndPart(&motion->flutterRun, (uint8_t)part)) {
        PA_LOG_INFO(TAG, "flutter of %s ended - a later move of it", partId);
    }
}

// -----------------------------------------------------------------------------
// flutterStartPart  --  a flutter handed to the run: a Body Step's, or one
// member's of a body Gesture.
//
// Whether anything can move the Part is asked here, the way a Body Step's move
// asks it: a Part no Output claims is reported and starts no flutter, and the
// sequence carries on (#301). So is an Output ServoTask does not drive since
// boot (#364) -- its legs would be dropped without a word while each one still
// held every other Output off.
//
// `speedMs` and `easing` are a Gesture's own words for its legs, 0 for a Body
// Step's: a flutter step carries no speed (ADR 0049). `runEndAtMs` is when the
// run that fired it reaches its end step, 0 where no run bounds it: the flutter
// is closed again by then, because the end step cuts it and commands nothing
// (sequenceFlutterStart()). No leg goes from here; the first waits its turn and
// the pace in flutterOneLeg().
//
// Out of line, like the Gesture's two functions: neither this frame nor the
// log lines' sit on the root frame or on dispatchAction()'s.
// -----------------------------------------------------------------------------
static __attribute__((noinline)) bool flutterStartPart(const char* partId, uint8_t howFar,
                                                       uint16_t flutterMs, uint16_t speedMs,
                                                       uint8_t easing, uint32_t now,
                                                       uint32_t runEndAtMs) {
    if (!motionReady()) {
        return false;  // refused: said once at boot (SeqMotionState)
    }
    flutterPlanMove(partId, howFar);
    const SeqBodyStepPlan plan =
        sequenceBodyStepPlan(motion->flutterMove, rowForPart(partId, &motion->flutterRow));
    // A Part the catalog does not hold is never driven, so past this the index
    // is one; the test is what makes the narrowing below safe to a reader.
    const size_t part = droidPartIndexOf(partId);
    if (!plan.drive || part >= DROID_PART_COUNT) {
        PA_LOG_INFO(TAG, "body %s not moved - %s", partId, consoleReasonString(plan.reason));
        return false;
    }
    if (!servoTaskDrivesOutput(plan.output)) {
        PA_LOG_INFO(TAG, "body %s not fluttered - restart the droid to use %s:%u", partId,
                    servoOutputDriverToString(plan.output.driver), (unsigned)plan.output.channel);
        return false;
    }
    if (!sequenceFlutterStart(&motion->flutterRun, (uint8_t)part, howFar, flutterMs, speedMs,
                              easing, now, runEndAtMs)) {
        PA_LOG_WARN(TAG, "body %s not fluttered - %u Parts are fluttering already", partId,
                    (unsigned)SEQ_FLUTTER_PARTS_MAX);
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// flutterOneLeg  --  one leg of one flutter per tick, when it may go.
//
// The entry whose turn it is waits for its own last leg to be over, and for
// the pace (sequenceFlutterMayGo()). Then its Part is resolved against the
// LIVE Output rows, as every body move is, so a table saved mid-flutter is
// read as it now stands; a Part nothing can move any more ends its flutter.
//
// The two ends of the swing come off that row: the closed end, and how far
// along the Part's own throw (seqBodyTargetUs()). How long each leg takes is
// the row's Motion Profile's answer for that distance, with the Gesture's
// speed and easing where the flutter is a Gesture's -- the very plan ServoTask
// makes for the move (servoMotionArrivalMs()). Nothing here sets a speed.
//
// A full servoCmdQueue leaves the entry as it is: the same leg comes round on
// the next tick.
// -----------------------------------------------------------------------------
static __attribute__((noinline)) void flutterOneLeg(uint32_t now) {
    const int8_t turn = sequenceFlutterTurn(motion->flutterRun);
    if (turn < 0) {
        return;
    }
    const uint8_t idx = (uint8_t)turn;
    const SeqFlutterEntry& e = motion->flutterRun.f[idx];
    const bool paceOpen =
        sequencePaceOpen(motion->gestureRun.dueMs, &motion->gestureRun.awaitOutput,
                         servoCommandedOf(motion->gestureRun.awaitOutput).moving, now);
    if (!sequenceFlutterMayGo(motion->flutterRun, idx, now, servoCommandedOf(e.output).moving,
                              paceOpen, sequenceGestureBodyDue(motion->gestureRun, now))) {
        return;
    }

    flutterPlanMove(droidPartIdAt(e.part), e.howFar);
    const ServoOutputRow* driving = rowForPart(motion->flutterMove.payload, &motion->flutterRow);
    SeqBodyStepPlan plan = sequenceBodyStepPlan(motion->flutterMove, driving);
    if (!plan.drive || driving == nullptr) {
        PA_LOG_INFO(TAG, "flutter of %s ended - %s", motion->flutterMove.payload,
                    consoleReasonString(plan.reason));
        sequenceFlutterDrop(&motion->flutterRun, idx);
        return;
    }
    const uint16_t farUs = plan.targetUs;
    const uint16_t closedUs = seqBodyTargetUs(*driving, BODY_SHAPE_FLUTTER, e.howFar);
    ServoMotionProfile profile = servoMotionProfileOf(*driving);
    servoMotionOverride(&profile, e.speedMs, e.easing);
    const uint32_t floorMs = configCacheCadenceFloorMs();
    const uint16_t outMs =
        sequenceFlutterLegTimeMs(servoMotionArrivalMs(closedUs, farUs, profile), floorMs);
    const uint16_t backMs =
        sequenceFlutterLegTimeMs(servoMotionArrivalMs(farUs, closedUs, profile), floorMs);

    const SeqFlutterLeg leg = sequenceFlutterLeg(e, now, outMs, backMs);
    if (leg == SEQ_FLUTTER_OVER) {
        sequenceFlutterDrop(&motion->flutterRun, idx);  // closed already, and no swing left to fit
        return;
    }
    plan.targetUs = (leg == SEQ_FLUTTER_LEG_OUT) ? farUs : closedUs;
    if (!sendBodyPosition(plan, e.speedMs, e.easing)) {
        return;
    }
    // Every leg holds the takes off the Part, as a step's move does: the run
    // lets go of the entry as the last back leg is sent, and a flutter ends
    // closed, so a take must not retarget the Part while that leg closes it.
    takesStepMoved(motion->flutterMove.payload);
    sequenceFlutterSent(&motion->flutterRun, idx, now, leg, outMs, backMs, plan.output,
                        &motion->gestureRun.dueMs, &motion->gestureRun.awaitOutput, floorMs);
}

// -----------------------------------------------------------------------------
// flutterTakeRequest  --  a flutter asked for by Output
// (sequenceFlutterRequest(), a body-owned Marcduino `:OFnn`).
//
// The request names an Output, as the line does, and a flutter is of a Part:
// the Part is the first one the live row at that address carries, which moves
// every Part on that Output, since they share its servo. An Output with no
// Part on it has nothing to flutter and is reported. From there it is a Body
// Step's flutter at full throw: flutterStartPart(), the same run, the same
// pace, ending closed. No run fired it, so only its own length bounds it.
//
// Refused under either halt, like a pose press, and while a back to centre or
// a pose is still putting Outputs out one at a time on a pace of its own: two
// paces side by side is what the Cadence Floor is there to prevent.
//
// Each step is its own out-of-line function and none calls the next: the
// request is taken, the Part found, the flutter started, one after another
// from flutterTakeRequest(), and each log line sits in a function that only
// logs. A log line's frame under another's is what puts a route on this task's
// measured chain (ADR 0040); nested, this path walked 192 B past it. The
// Part's id is in a static because flutterStartPart() reads rows into
// flutterRow, so the id cannot be left pointing into it.
// -----------------------------------------------------------------------------
static char flutterAskPart[DROID_PART_ID_MAX_LEN + 1];

// The first Part the live row at `output` carries, into flutterAskPart. False
// when no row has that address, or no Part is on it.
static __attribute__((noinline)) bool flutterFindPartOn(ServoOutputAddress output) {
    flutterAskPart[0] = '\0';
    const uint8_t rowCount = configCacheServoOutputCount();
    for (uint8_t i = 0; i < rowCount; ++i) {
        if (configCacheReadServoOutput(i, &motion->flutterRow) &&
            ServoOutputAddress{motion->flutterRow.driver, motion->flutterRow.channel} == output &&
            servoOutputPartCount(motion->flutterRow) > 0) {
            strncpy(flutterAskPart, servoOutputPartAt(motion->flutterRow, 0),
                    sizeof(flutterAskPart) - 1);
            flutterAskPart[sizeof(flutterAskPart) - 1] = '\0';
            return true;
        }
    }
    return false;
}

static __attribute__((noinline)) void flutterLogNoPart(ServoOutputAddress output) {
    PA_LOG_INFO(TAG, "%s:%u not fluttered - no Part is on it",
                servoOutputDriverToString(output.driver), (unsigned)output.channel);
}

static __attribute__((noinline)) void flutterLogRefused(const char* refusal) {
    PA_LOG_WARN(TAG, "flutter request refused - %s", refusal);
}

static __attribute__((noinline)) void flutterTakeRequest(uint32_t now, const char* refusal) {
    bool asked = false;
    ServoOutputAddress output = SERVO_OUTPUT_NONE;
    uint16_t flutterMs = 0;
    taskENTER_CRITICAL(&robotStateMux);
    if (robotState.flutterRequest) {
        asked = true;
        output = robotState.flutterRequestOutput;
        flutterMs = robotState.flutterRequestMs;
        robotState.flutterRequest = false;
    }
    taskEXIT_CRITICAL(&robotStateMux);
    if (!asked) {
        return;
    }
    if (refusal != nullptr) {
        flutterLogRefused(refusal);
        return;
    }
    if (!motionReady()) {
        return;  // refused, request and all: said once at boot (SeqMotionState)
    }
    // The 0/99 broadcast is the board's first two Outputs, each its own
    // flutter, so they take turns like any two.
    const bool both = (output == SERVO_OUTPUT_BOTH_ARMS);
    for (uint8_t n = 0; n < (both ? 2 : 1); ++n) {
        const ServoOutputAddress one = both ? boardOutputAddress(n) : output;
        if (!flutterFindPartOn(one)) {
            flutterLogNoPart(one);
            continue;
        }
        flutterStartPart(flutterAskPart, SEQ_BODY_HOWFAR_MAX, flutterMs, 0, 0, now,
                         /*runEndAtMs=*/0);
    }
}

// A Gesture the engine has just handed over, copied into the run NOW, while
// the engine that fired it is still active: a Learned run's steps are freed
// when the run ends, and the Gesture is read on every pass until then
// (SeqAction, sequence_engine.h).
static __attribute__((noinline)) void gestureStartFromAction(const SeqAction& act) {
    if (!motionReady()) {
        return;  // refused: said once at boot (SeqMotionState)
    }
    // act.domeDurationMs is where the firing run ends: no move of the Gesture
    // goes out at or after it (sequenceGestureNext()).
    if (act.gesture != nullptr &&
        !sequenceGestureStart(&motion->gestureRun, *act.gesture, millis(), act.domeDurationMs)) {
        PA_LOG_WARN(TAG, "gesture %s not performed - nothing to perform, or four already running",
                    act.payload);
    }
}

// One Gesture item per tick, when it is due: a dome Gesture's pass is its `$`
// command to the dome; a body Gesture's move goes down the very path a Body
// Step takes (dispatchBodyMove()), resolved against the live Output rows, and
// the run holds the next body move off by the pace every generated motion
// keeps. A member's flutter is handed to the flutter run instead. A full queue
// leaves the item where it is, to come round next tick.
// Out of line, as gestureStartFromAction() below is, so neither's locals sit
// on the root frame or on dispatchAction()'s: both are on the measured chain.
static __attribute__((noinline)) void gestureOneItem(uint32_t now) {
    const bool moving = servoCommandedOf(motion->gestureRun.awaitOutput).moving;
    SeqGestureNext next = {};
    // A flutter's Part that is out holds a Gesture's body move back: its back
    // leg goes next, so a swing is never split and is back by the time it was
    // fitted to (include/sequence_flutter.h).
    if (!sequenceGestureNext(&motion->gestureRun, now, moving, &next,
                             /*bodyHeld=*/sequenceFlutterPartOut(motion->flutterRun))) {
        return;
    }
    if (next.dome) {
        if (!domeQueueTx(motion->gestureRun.g[next.entry].domeCmd)) {
            return;
        }
        sequenceGestureDone(&motion->gestureRun, next, now, /*started=*/true, 0, SERVO_OUTPUT_NONE,
                            configCacheCadenceFloorMs());
        return;
    }
    // A member's flutter is the flutter run's to perform, with the Gesture's
    // own length, speed and easing (#453). Handing it over starts no motion.
    if (next.shape == BODY_SHAPE_FLUTTER) {
        const bool taken = flutterStartPart(droidPartIdAt(next.part), next.howFar, next.flutterMs,
                                            next.speedMs, next.easing, now, next.endAtMs);
        sequenceGestureFlutterHandedOver(&motion->gestureRun, next, taken);
        return;
    }
    memset(&motion->gestureMove, 0, sizeof(motion->gestureMove));
    motion->gestureMove.kind = SEQ_ACT_BODY_MOVE;
    strncpy(motion->gestureMove.payload, droidPartIdAt(next.part),
            sizeof(motion->gestureMove.payload) - 1);
    motion->gestureMove.bodyShape = (uint8_t)next.shape;
    motion->gestureMove.bodyHowFar = next.howFar;
    // Paced by the Output's own throw unless the Gesture states one: the
    // spacing is how long this move takes.
    BodyMoveOutcome moved = {false, SERVO_OUTPUT_NONE, 0};
    if (!dispatchBodyMove(motion->gestureMove, &moved, next.speedMs, next.easing)) {
        return;
    }
    // The move is the later word over a flutter of the same Part still going --
    // a chase's "the one before goes back" is exactly that -- and it is another
    // Output's motion to every flutter still running.
    flutterEndPart(motion->gestureMove.payload);
    if (moved.sent) {
        sequenceFlutterOtherMotion(&motion->flutterRun);
    }
    sequenceGestureDone(&motion->gestureRun, next, now, moved.sent, moved.throwMs, moved.output,
                        configCacheCadenceFloorMs());
}

static void poseOneCommand(SeqPoseRun& run, uint32_t now) {
    const uint32_t floorMs = configCacheCadenceFloorMs();
    if (run.awaitOutput != SERVO_OUTPUT_NONE) {
        const bool moving = servoCommandedOf(run.awaitOutput).moving;
        if (!sequencePoseAwaitDone(&run, moving)) {
            return;  // still moving: looked at again on the next tick
        }
    }
    if (sequencePoseFinished(run)) {
        sequencePoseEnd(&run);  // the last command's spacing has run: the pose is reached
        return;
    }

    const SeqPoseCmd& cmd = motion->posePlan.cmds[run.next];
    switch (cmd.cls) {
        case SEQ_POSE_PANEL:
            if (!domeQueueTx(cmd.act.payload)) {
                return;
            }
            sequencePoseAdvance(&run, now, cmd.cls, /*started=*/true, 0, SERVO_OUTPUT_NONE, floorMs);
            return;

        case SEQ_POSE_BODY: {
            // A Part nothing can move is reported by dispatchBodyMove() and
            // passed over here, costing the pose no time.
            BodyMoveOutcome moved = {false, SERVO_OUTPUT_NONE, 0};
            if (!dispatchBodyMove(cmd.act, &moved)) {
                return;
            }
            sequencePoseAdvance(&run, now, cmd.cls, moved.sent, moved.throwMs, moved.output, floorMs);
            return;
        }

        case SEQ_POSE_INSTANT:
        default:
            if (!dispatchAction(cmd.act)) {
                return;
            }
            sequencePoseAdvance(&run, now, cmd.cls, /*started=*/true, 0, SERVO_OUTPUT_NONE, floorMs);
            return;
    }
}

// The two acts one run carries, as its log lines name them.
static const char* bulkCentreName(const SeqBulkCentreRun& run) {
    return run.kind == SEQ_BULK_CENTRE_BOOT ? "boot pass" : "back to centre";
}

// =============================================================================
// sequenceStart  --  choke point called from RC and web paths.
// =============================================================================

bool sequenceStart(const char* name, CommandSource src) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }

    SequenceLookupResult r = sequenceLookup(name);

    switch (r.kind) {
        case SEQ_RUNTIME:   // Learned Sequence  --  loaded on demand in the task
        case SEQ_CATALOG: {
            if (sequenceQueue == nullptr) {
                return false;
            }
            // A run asked for after a pose press is the later word: the pose
            // still waiting for the Coordinator is cancelled here, before the
            // run is queued, so no wake can take the older pose after the run
            // and abort it (#440). A pose pressed after this is still the later
            // word and wins, as sequencePoseRequest() writes the slot again.
            taskENTER_CRITICAL(&robotStateMux);
            robotState.poseRequest = SRC_NONE;
            taskEXIT_CRITICAL(&robotStateMux);
            SequenceRequest req = {};
            strncpy(req.name, name, sizeof(req.name) - 1);
            req.name[sizeof(req.name) - 1] = '\0';
            req.src = src;
            bool ok = xQueueSend(sequenceQueue, &req, 0) == pdTRUE;
            if (!ok) {
                PA_LOG_WARN(TAG, "[%s] seq queue full: %s",
                            commandSourceToString(src), name);
            }
            return ok;
        }
        case SEQ_ALIAS:
            PA_LOG_DEBUG(TAG, "[%s] alias %s -> %s",
                         commandSourceToString(src), name, r.aliasTarget);
            return domeQueueTx(r.aliasTarget);

        case SEQ_FALLBACK:
        default:
            if (strncmp(name, "DM:", 3) == 0) {
                // A DM:* name that is neither catalog, runtime, nor alias is
                // almost certainly a deleted Learned Sequence still referenced
                // by an RC binding. The dome ignores it, so make the no-op
                // visible to the operator instead of failing silently.
                PA_LOG_WARN(TAG, "[%s] unknown DM:* (deleted Learned Sequence?) -> dome: %s",
                            commandSourceToString(src), name);
            } else {
                PA_LOG_DEBUG(TAG, "[%s] fallback -> dome: %s",
                             commandSourceToString(src), name);
            }
            return domeQueueTx(name);
    }
}

// =============================================================================
// sequencePoseRequest  --  the pose press's way in (#440).
//
// Only a sequence whose steps the body holds can be posed: a Factory one or a
// Learned one. The request carries the name and the instant and nothing else;
// the pose and its pace are worked out by the task (include/sequence_pose.h).
// =============================================================================

bool sequencePoseRequest(const char* name, uint32_t atMs, CommandSource src) {
    if (name == nullptr || name[0] == '\0' || src == SRC_NONE) {
        return false;
    }
    const SequenceLookupKind kind = sequenceLookup(name).kind;
    if (kind != SEQ_CATALOG && kind != SEQ_RUNTIME) {
        return false;
    }
    taskENTER_CRITICAL(&robotStateMux);
    strncpy(robotState.poseRequestName, name, sizeof(robotState.poseRequestName) - 1);
    robotState.poseRequestName[sizeof(robotState.poseRequestName) - 1] = '\0';
    robotState.poseRequestAtMs = atMs;
    robotState.poseRequest = src;
    taskEXIT_CRITICAL(&robotStateMux);
    return true;
}

// =============================================================================
// sequenceStopRequest  --  the non-latching Stop's one way in (#440).
//
// A Stop is the later word over a pose still waiting for the Coordinator, the
// way sequenceStart() is: the pending pose is cleared under the same lock that
// raises the flag, so no wake can take the pose after the Stop and start it.
// A pose already being reached is ended by the Coordinator when it reads the
// flag.
// =============================================================================

void sequenceStopRequest() {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.poseRequest = SRC_NONE;
    robotState.flutterRequest = false;
    robotState.seqStopRequested = true;
    taskEXIT_CRITICAL(&robotStateMux);
}

// =============================================================================
// sequenceFlutterRequest  --  a body-owned `:OFnn`'s way in (#453).
//
// The line's handler runs on whichever task read the line, so it touches none
// of the flutter run's state: it leaves the Output and the length here, the
// shape a pose press uses, and the Coordinator takes them on its next tick
// (flutterTakeRequest()).
// =============================================================================

void sequenceFlutterRequest(ServoOutputAddress output, uint16_t flutterMs) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.flutterRequestOutput = output;
    robotState.flutterRequestMs = flutterMs;
    robotState.flutterRequest = true;
    taskEXIT_CRITICAL(&robotStateMux);
}

// =============================================================================
// Task Adapter  --  Core 0, priority 3, 10 ms tick.
// Compiles with native FreeRTOS stubs for testing.
// =============================================================================

// Dispatch an action to the appropriate queue using the pure step-core decision.
// Preserves the safety invariants: a full queue causes retry on the next tick
// rather than stalling the engine. In abort/preempt cleanup, failures are
// best-effort and do not stall the drain.
static bool dispatchAction(const SeqAction& act) {
    const SequenceDispatcherStepActions decision = sequenceDispatcherStep(act, millis());

    switch (decision.target) {
        case SEQ_DISPATCH_DOME_CMD:
            // Forward dome text command to dome queue.
            return domeQueueTx(act.payload);

        case SEQ_DISPATCH_DOME_ROTATE: {
            // Dome output is staged at reboot (ADR 0027); when inactive, drop the action.
            if (!configCacheReadActiveDomeEnabled()) {
                return true;
            }
            // Send converted DomeCommand to the dome rotation queue.
            DomeCommand cmd = {};
            if (!sequenceActionToDomeCommand(act, millis(), cmd)) {
                return true;
            }
            return xQueueSend(domeCmdQueue, &cmd, 0) == pdTRUE;
        }

        case SEQ_DISPATCH_DOME_BEARING:
            return dispatchDomeBearing(act);

        case SEQ_DISPATCH_AUDIO_DOLLAR:
            // Forward audio dollar command to audio queue.
            return audioQueueDollar(act.payload, SRC_SEQ);

        case SEQ_DISPATCH_AUDIO_CATEGORY:
            // Route to audio category play with fallback slot.
            return audioQueuePlayCategory((AudioPlaybackCategory)decision.audioCategory.category,
                                          (AudioPlaybackSlot)decision.audioCategory.fallbackSlot,
                                          SRC_SEQ);

        case SEQ_DISPATCH_AUDIO_STOP:
            // Forward to audio stop queue.
            return audioQueueTrackStop(SRC_SEQ);

        case SEQ_DISPATCH_BACKGROUND_TRACK_START:
            return dispatchBackgroundTrackStart(act);

        case SEQ_DISPATCH_BACKGROUND_TRACK_STOP:
            return audioQueueBackgroundTrackStop(SRC_SEQ);

        case SEQ_DISPATCH_BODY_MOVE:
            // A flutter is handed to the flutter run, which performs it beside
            // the engine's cursor; the step itself is dealt with either way.
            // Any other move of a Part ends a flutter of that Part still
            // going, once the move itself has been sent.
            if (act.bodyShape == (uint8_t)BODY_SHAPE_FLUTTER) {
                // act.domeDurationMs is where the firing run ends (resolveStep()).
                flutterStartPart(act.payload, act.bodyHowFar, act.bodyFlutterMs, 0, 0, millis(),
                                 act.domeDurationMs);
                return true;
            }
            if (!dispatchBodyMove(act)) {
                return false;
            }
            flutterEndPart(act.payload);
            return true;

        case SEQ_DISPATCH_GESTURE:
            gestureStartFromAction(act);
            return true;  // handled: the Gesture is the run's now, or reported

        case SEQ_DISPATCH_NONE:
        default:
            // Unknown action: silent success (fail-safe behavior).
            return true;
    }
}

// Current shared body queue-full count (run-evidence baseline/delta).
static uint32_t bodyQueueFullCount() {
    uint32_t c;
    taskENTER_CRITICAL(&robotStateMux);
    c = robotState.queueOverflowCount;
    taskEXIT_CRITICAL(&robotStateMux);
    return c;
}

// A finish that had no room for every terminal action -- a ring close among
// them would leave a panel open -- is said, never silent. Asked once per tick
// from the task loop, which covers a run's own end and every abort's drain.
// noinline, noclone and cold on purpose: as a plain noinline function the
// compiler laid out dispatchAction()'s callees so that this task's deepest
// route, a Body Step's log line, walked 16 B past its recorded figure; marked
// cold (what it reports should never happen), the route is unchanged.
static void __attribute__((noinline, noclone, cold)) reportFinalDrops(SeqEngineState& engine) {
    uint8_t count = 0;
    SeqActionKind first = SEQ_ACT_NONE;
    if (seqEngineTakeFinalDrops(engine, &count, &first)) {
        PA_LOG_WARN(TAG, "cleanup queue full: %u terminal action(s) dropped, first kind %u",
                    (unsigned)count, (unsigned)first);
    }
}

// Best-effort drain of remaining engine actions (abort/preempt cleanup).
// SAFETY INVARIANT: drains regardless of dispatch result so a full queue
// cannot stall an abort or preempt. These are all terminal/abort cleanup,
// so they are recorded as cleanup evidence.
static void drainBestEffort(SeqEngineState& engine, uint32_t now) {
    // Static, off the measured chain (ADR 0040): this frame is on the task's
    // deepest route (drainBestEffort -> dispatchAction -> a log line), and a
    // SeqAction grew by the Gesture pointer it now carries (#438).
    static SeqAction act;
    while (seqEnginePeek(engine, now, esp_random, act)) {
        if (!dispatchAction(act)) {
            PA_LOG_WARN(TAG, "cleanup action dropped (queue full): %s", act.payload);
        }
        seqEvidenceRecordTx(act, /*cleanup=*/true);
        seqEngineCommit(engine);
    }
}

// The dome's visuals put back to their resting state after an estop clears or
// the dome (re)connects: every family in SEQ_DOME_VISUAL_RESETS, the list
// terminal cleanup uses for a run whose visuals it cannot name. Both resyncs
// send it from here, so a family added to the list reaches both; the holos
// were once missing from the two of them (#320, #453). None of these moves a
// servo, so they go at once and are not staged like the ring close.
static void resyncDomeVisuals() {
    for (const char* reset : SEQ_DOME_VISUAL_RESETS) {
        domeQueueTx(reset);
    }
}

static void setSuppression(uint32_t untilMs) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeSeqActive  = true;
    robotState.domeSeqUntilMs = untilMs;
    taskEXIT_CRITICAL(&robotStateMux);
}

static void clearSuppression() {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeSeqActive = false;
    taskEXIT_CRITICAL(&robotStateMux);
}

void sequenceDispatcherTask(void* /*pvParameters*/) {
    esp_task_wdt_add(NULL);

    static SeqEngineState engine;  // static: keep the cursor state off the task stack
    seqEngineInit(engine);

    bool prevEstop = false;
    bool prevDomeConn = false;
    bool retryLogged = false;
    char activeName[24] = "";
    static SequenceEntry runtimeEntry;  // storage for a loaded Learned Sequence

    // Staged ring-only resync close (estop-clear / dome-reconnect): emit one
    // individual ring close per kResyncCloseSpacingMs so single-servo inrush
    // never overlaps. A group :CL15 closes every ring servo at once and browns
    // out the dome from a loaded ring (2026-06-17 hardware finding), so resync
    // must never send it. resyncCloseIdx == SEQ_RESYNC_CLOSE_NONE means no staged
    // close pending. A resync and a pose never share the dome (include/sequence_pose.h).
    const uint32_t kResyncCloseSpacingMs = 500;
    uint8_t        resyncCloseIdx = SEQ_RESYNC_CLOSE_NONE;
    uint32_t       resyncCloseDueMs = 0;
    uint32_t       waitMs = 10;  // task wake timeout; computed at end of each iteration

    // The bulk centre sweep an operator starts from the output-first table
    // (#318, #365), and the boot pass on the same cursor (#414). Static for the
    // same reason the engine above is: the cursor stays off this task's
    // measured stack chain (ADR 0040).
    static SeqBulkCentreRun centreRun;
    centreRun = SeqBulkCentreRun{};
    centreRun.awaitOutput = SERVO_OUTPUT_NONE;

    // The pose press's run (#440), over the static posePlan. Static for the
    // same reason as the sweep's.
    static SeqPoseRun poseRun;
    poseRun = SeqPoseRun{};
    poseRun.awaitOutput = SERVO_OUTPUT_NONE;
    if (motionReady()) {
        motion->gestureRun = SeqGestureRun{};
        motion->gestureRun.awaitOutput = SERVO_OUTPUT_NONE;
        motion->flutterRun = SeqFlutterRun{};
        motion->flutterRun.lastLeg = SEQ_FLUTTER_NONE;
    }
    // The pose request as taken from RobotState, static like the run: its name
    // and instant live across the whole intake below, and on this task's stack
    // they pushed the measured chain past its figure.
    static struct {
        CommandSource src;
        uint32_t atMs;
        char name[sizeof(robotState.poseRequestName)];
    } poseAsk;

    // Power-up: each body Output does what its boot behaviour says (ADR 0052),
    // paced by the Cadence Floor like any sweep this task generates. The estop
    // a TWDT reset latched in setup() is already set by now, and under it - or
    // under Sleep Mode - the pass is refused outright, never held for later.
    {
        bool bootEstop = false;
        bool bootSleep = false;
        taskENTER_CRITICAL(&robotStateMux);
        bootEstop = robotState.estop;
        bootSleep = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);
        if (sequenceBootPassStart(&centreRun, millis(), bootEstop, bootSleep)) {
            PA_LOG_INFO(TAG, "boot pass - %u rows, at least %u ms apart",
                        (unsigned)configCacheServoOutputCount(), (unsigned)configCacheCadenceFloorMs());
        } else {
            PA_LOG_WARN(TAG, "boot pass skipped - %s, every Output stays limp",
                        bootEstop ? "estop latched" : "sleep mode active");
        }
    }

    while (true) {
        esp_task_wdt_reset();
        SequenceRequest req = {};
        const bool haveReq = xQueueReceive(sequenceQueue, &req, pdMS_TO_TICKS(waitMs)) == pdTRUE;
        const uint32_t now = millis();

        // A pose press: send the droid to one instant of a sequence (#440,
        // include/sequence_pose.h). Refused outright under either halt; the
        // route asked the same rule to answer the press, and it is asked again
        // here because a halt can land in between. Otherwise it is the
        // operator's latest word, like back to centre: a running sequence and a
        // sweep end, and the pose is worked out once, from the stored steps.
        //
        // The request is a transient the route sets and this clears, the shape
        // back to centre uses (robotState.poseRequest); the name is copied out
        // under the lock into a static buffer, off this task's stack.
        //
        // It is taken only on a tick with no run request, and waits for the
        // next tick otherwise. That keeps the pose intake and the run intake
        // the two arms of one if/else, which is what lets the compiler lay
        // their staging results in the same stack slot: as two separate ifs
        // the root frame outgrew its measured chain (ADR 0040). Deferring is
        // also the right order: sequenceStart() and sequenceStopRequest()
        // cancel a pending pose, so a pose still waiting on a tick that
        // received a run was pressed after that run, and is the later word.
        poseAsk.src = SRC_NONE;
        taskENTER_CRITICAL(&robotStateMux);
        if (!haveReq && robotState.poseRequest != SRC_NONE) {
            poseAsk.src = robotState.poseRequest;
            poseAsk.atMs = robotState.poseRequestAtMs;
            memcpy(poseAsk.name, robotState.poseRequestName, sizeof(poseAsk.name));
            robotState.poseRequest = SRC_NONE;
        }
        taskEXIT_CRITICAL(&robotStateMux);
        if (poseAsk.src != SRC_NONE) {
            bool poseEstop = false;
            bool poseSleep = false;
            taskENTER_CRITICAL(&robotStateMux);
            poseEstop = robotState.estop;
            poseSleep = robotState.sleepMode;
            taskEXIT_CRITICAL(&robotStateMux);
            const char* refusal = sequencePoseRefusal(poseEstop, poseSleep);
            if (refusal == nullptr && !motionReady()) {
                refusal = "no memory for its plan";  // said once at boot too (SeqMotionState)
            }
            const bool isRuntime = (sequenceLookup(poseAsk.name).kind == SEQ_RUNTIME);
            const SequenceEntry* catalogEntry = isRuntime ? nullptr : sequenceCatalogFind(poseAsk.name);
            bool staged = !isRuntime && catalogEntry != nullptr;
            if (refusal != nullptr) {
                PA_LOG_WARN(TAG, "[%s] pose %s refused - %s", commandSourceToString(poseAsk.src), poseAsk.name,
                            refusal);
                staged = false;
            } else if (isRuntime) {
                // Staged before the engine is touched, as a run is, so a load
                // that fails never costs what is running.
                const ProtocolCheckResult lr = seqStorePrepare(poseAsk.name);
                staged = lr.ok;
                if (!lr.ok) {
                    PA_LOG_WARN(TAG, "[%s] pose load failed %s: %s (%s)",
                                commandSourceToString(poseAsk.src), poseAsk.name, lr.message, lr.field);
                }
            } else if (catalogEntry == nullptr) {
                PA_LOG_WARN(TAG, "pose request not in catalog: %s", poseAsk.name);
            }
            if (staged) {
                if (seqEngineActive(engine)) {
                    PA_LOG_INFO(TAG, "abort %s (pose)", activeName);
                    seqEngineAbort(engine);
                    drainBestEffort(engine, now);  // dome and audio resets, never a body move
                    seqEvidenceEnd(SEQ_RUN_ABORTED, "pose", now, bodyQueueFullCount());
                    seqStoreReleaseRun();
                    clearSuppression();
                    activeName[0] = '\0';
                }
                if (centreRun.active) {
                    PA_LOG_INFO(TAG, "%s ended - a pose took over", bulkCentreName(centreRun));
                    sequenceBulkCentreEnd(&centreRun);
                }
                generatedEnd("a pose took over");
                const SequenceEntry* entry = catalogEntry;
                if (isRuntime) {
                    if (seqStoreCommit(runtimeEntry)) {
                        entry = &runtimeEntry;
                    } else {
                        PA_LOG_WARN(TAG, "[%s] pose %s refused (run-buffer alloc failed)",
                                    commandSourceToString(poseAsk.src), poseAsk.name);
                    }
                }
                if (entry != nullptr) {
                    // The engine runs a toggle's open half while its group is
                    // closed, and leaves that half's ring panels open at the end.
                    const bool toggleOpenHalf =
                        entry->toggleGroup != TOGGLE_NONE && entry->closeSteps != nullptr;
                    sequencePosePlan(entry->steps, entry->stepCount, toggleOpenHalf, poseAsk.atMs,
                                     &motion->posePlan);
                    if (isRuntime) {
                        seqStoreReleaseRun();  // the plan holds its own copies
                    }
                    // A pose that starts supersedes a staged resync close, as a
                    // run does below: one motion owner on the dome at a time.
                    sequencePoseStart(&poseRun, now, poseEstop, poseSleep, motion->posePlan.count,
                                      (uint8_t)poseAsk.src, &resyncCloseIdx);
                    PA_LOG_INFO(
                        TAG, "[%s] pose %s at %u ms - %u commands, motions at least %u ms apart",
                        commandSourceToString(poseAsk.src), poseAsk.name, (unsigned)poseAsk.atMs,
                        (unsigned)motion->posePlan.count, (unsigned)configCacheCadenceFloorMs());
                    if (motion->posePlan.truncated) {
                        PA_LOG_WARN(TAG, "pose %s names more than %u targets; the latest are left out",
                                    poseAsk.name, (unsigned)SEQ_POSE_MAX);
                    }
                }
            }
        } else if (haveReq) {  // Check for a new request (preempt current sequence if one is running).
            const bool isRuntime = (sequenceLookup(req.name).kind == SEQ_RUNTIME);
            const SequenceEntry* catalogEntry =
                isRuntime ? nullptr : sequenceCatalogFind(req.name);
            bool willStart = isRuntime || (catalogEntry != nullptr);

            if (isRuntime) {
                // Stage the Learned Sequence (parse + Protocol Check into a
                // transient heap pair) BEFORE touching the engine, so a load
                // that fails  --  corrupt file, concurrent Memory Wipe  --  never
                // costs the currently running sequence.
                ProtocolCheckResult lr = seqStorePrepare(req.name);
                if (!lr.ok) {
                    PA_LOG_WARN(TAG, "[%s] runtime load failed %s: %s (%s)",
                                commandSourceToString(req.src), req.name,
                                lr.message, lr.field);
                    willStart = false;
                }
            }
            if (willStart) {
                // A later run is the later word over Gestures still repeating
                // and flutters still swinging from the one before, whether or
                // not that one is still running.
                generatedEnd("a later run");
                if (seqEngineActive(engine)) {
                    PA_LOG_INFO(TAG, "preempt %s -> %s", activeName, req.name);
                    seqEngineAbort(engine);
                    drainBestEffort(engine, now);  // drain old run from buffers
                    seqEvidenceEnd(SEQ_RUN_PREEMPTED, "preempt", now, bodyQueueFullCount());
                    seqStoreReleaseRun();  // free the preempted Learned run's buffers
                }
                const SequenceEntry* entry = catalogEntry;
                if (isRuntime) {
                    // Commit copies the staged sequence into freshly allocated,
                    // right-sized run buffers. This can fail on a tight heap, so
                    // refuse the run gracefully rather than start on a stale entry.
                    if (seqStoreCommit(runtimeEntry)) {
                        entry = &runtimeEntry;
                    } else {
                        PA_LOG_WARN(TAG, "[%s] %s run refused (run-buffer alloc failed)",
                                    commandSourceToString(req.src), req.name);
                        entry = nullptr;
                    }
                }
                if (entry != nullptr) {
                    seqEngineStart(engine, entry, now);
                    takesBegin(now, engine);
                    seqEvidenceBegin(req.name, (uint8_t)req.src, now, bodyQueueFullCount());
                    resyncCloseIdx = SEQ_RESYNC_CLOSE_NONE;  // a new run supersedes any staged resync close
                    // ...and so does a bulk centre still sweeping. A sequence
                    // and a sweep both move body Outputs, and two of them
                    // interleaving is the many-at-once shape the Cadence Floor
                    // exists to keep apart. The sequence is the later word.
                    if (centreRun.active) {
                        PA_LOG_INFO(TAG, "%s ended - %s took over", bulkCentreName(centreRun),
                                    req.name);
                        sequenceBulkCentreEnd(&centreRun);
                    }
                    // ...and so does a pose still being reached, for the same reason.
                    if (poseRun.active) {
                        PA_LOG_INFO(TAG, "pose ended - %s took over", req.name);
                        sequencePoseEnd(&poseRun);
                    }
                    strncpy(activeName, req.name, sizeof(activeName) - 1);
                    activeName[sizeof(activeName) - 1] = '\0';
                    retryLogged = false;
                    setSuppression(now + entry->suppressMs);
                    PA_LOG_INFO(TAG, "[%s] start %s suppress=%u ms",
                                commandSourceToString(req.src),
                                entry->name, (unsigned)entry->suppressMs);
                }
            } else if (!isRuntime) {
                PA_LOG_WARN(TAG, "request not in catalog: %s", req.name);
            }
        }

        // SAFETY INVARIANT: Abort on estop; resync the dome to a known safe state on estop-clear.
        // Estop abort is latching: once it fires, it drains all pending actions and
        // prevents new sequences from starting until estop is released and resync completes.
        bool estopActive = false;
        bool sleepActive = false;
        taskENTER_CRITICAL(&robotStateMux);
        estopActive = robotState.estop;
        sleepActive = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);

        // A bulk centre or a boot pass ends on either halt, where it has got to
        // (#365, #414). ADR 0043 has ServoTask release every enabled Output on
        // that same edge and command no position, so a sweep that kept
        // queueing moves would be driving parts the droid has just
        // deliberately let go of. Nothing is commanded on the way out: the
        // release is the whole of what a halt does to an Output.
        if ((estopActive || sleepActive) && centreRun.active) {
            PA_LOG_INFO(TAG, "%s ended (%s) after %u centred, %u skipped",
                        bulkCentreName(centreRun), estopActive ? "estop" : "sleep mode",
                        (unsigned)centreRun.centred, (unsigned)centreRun.skipped);
            sequenceBulkCentreEnd(&centreRun);
        }
        // Gestures and flutters end on either halt the same way: ServoTask has
        // let every Output go, and a Gesture still expanding, or a flutter
        // still swinging, would drive them again.
        if (estopActive || sleepActive) {
            generatedEnd(estopActive ? "estop" : "sleep mode");
        }
        // A pose ends on either halt the same way, where it has got to.
        if ((estopActive || sleepActive) && poseRun.active) {
            PA_LOG_INFO(TAG, "pose ended (%s) after %u sent, %u skipped",
                        estopActive ? "estop" : "sleep mode", (unsigned)poseRun.sent,
                        (unsigned)poseRun.skipped);
            sequencePoseEnd(&poseRun);
        }

        if (estopActive && seqEngineActive(engine)) {
            PA_LOG_INFO(TAG, "abort %s (estop)", activeName);
            seqEngineAbort(engine);
            drainBestEffort(engine, now);
            seqEvidenceEnd(SEQ_RUN_ESTOP, "estop", now, bodyQueueFullCount());
            seqStoreReleaseRun();  // reclaim any Learned-run buffers
            clearSuppression();
            activeName[0] = '\0';
        }
        if (!estopActive && prevEstop) {
            PA_LOG_INFO(TAG, "estop cleared - dome resync (staged ring close)");
            // Stage an individual ring-only close (drained below), never a group
            // :CL15/:CL00: a group close drives every ring servo simultaneously
            // and browns out the dome from a loaded ring (issue #2 hardware
            // finding). Pies are never auto-closed on resync. The logic, PSI
            // and holo resets are non-servo commands, so they stay immediate.
            // A pose taken on this same tick, before the edge was seen, ends
            // here.
            if (sequenceResyncCloseStage(&poseRun, &resyncCloseIdx, &resyncCloseDueMs, now)) {
                PA_LOG_INFO(TAG, "pose ended - dome resync");
            }
            resyncDomeVisuals();
            seqEngineClearLatches(engine);
        }
        prevEstop = estopActive;

        // Web-initiated non-latching stop (POST /api/seq/stop).
        // The flag is transient  --  set by the web handler, cleared here after abort processing.
        // Unlike estop (which latches), a stop does not affect other subsystems or boot state.
        bool stopRequested = false;
        taskENTER_CRITICAL(&robotStateMux);
        stopRequested = robotState.seqStopRequested;
        if (stopRequested) {
            robotState.seqStopRequested = false;  // clear the transient flag
        }
        taskEXIT_CRITICAL(&robotStateMux);

        // A stop stops what the Coordinator is doing, and a bulk centre is that
        // as much as a sequence is: an operator who presses Stop while the
        // droid is sweeping means the sweep.
        if (stopRequested && centreRun.active) {
            PA_LOG_INFO(TAG, "%s ended (web stop) after %u centred, %u skipped",
                        bulkCentreName(centreRun), (unsigned)centreRun.centred,
                        (unsigned)centreRun.skipped);
            sequenceBulkCentreEnd(&centreRun);
        }
        if (stopRequested) {
            generatedEnd("web stop");
        }
        if (stopRequested && poseRun.active) {
            PA_LOG_INFO(TAG, "pose ended (web stop) after %u sent", (unsigned)poseRun.sent);
            sequencePoseEnd(&poseRun);
        }

        if (stopRequested && seqEngineActive(engine)) {
            PA_LOG_INFO(TAG, "abort %s (web stop)", activeName);
            seqEngineAbort(engine);
            drainBestEffort(engine, now);
            // Record as SEQ_RUN_ABORTED with "web stop" reason (distinguishes from
            // estop/preempt/reconnect). Operator can see the reason in GET /api/seq/last-run.
            seqEvidenceEnd(SEQ_RUN_ABORTED, "web stop", now, bodyQueueFullCount());
            seqStoreReleaseRun();  // reclaim any Learned-run buffers
            clearSuppression();
            activeName[0] = '\0';
        }

        // Dome (re)connect resync (ADR 0004 decision 8): panel state on the
        // dome is unknown after boot or a link gap, so assume closed  --  abort any
        // running sequence, end a pose being reached, stage an individual
        // ring-only close (drained below), and clear the latches. Never a group
        // :CL15/:CL00 (see the estop-clear resync above  --  a group close browns
        // out the dome from a loaded ring); pies are never auto-closed on resync.
        const bool domeConn = domeConnected();
        if (domeConn && !prevDomeConn) {
            PA_LOG_INFO(TAG, "dome (re)connected - panel state resync (staged ring close)");
            if (seqEngineActive(engine)) {
                seqEngineAbort(engine);
                drainBestEffort(engine, now);
                seqEvidenceEnd(SEQ_RUN_RECONNECT, "dome reconnect", now, bodyQueueFullCount());
                seqStoreReleaseRun();  // reclaim any Learned-run buffers
                clearSuppression();
                activeName[0] = '\0';
            }
            if (sequenceResyncCloseStage(&poseRun, &resyncCloseIdx, &resyncCloseDueMs, now)) {
                PA_LOG_INFO(TAG, "pose ended - dome resync");
            }
            // The resync owns the dome's panels now, one motion owner at a time.
            generatedEnd("dome resync");
            resyncDomeVisuals();
            seqEngineClearLatches(engine);
        }
        prevDomeConn = domeConn;

        // Drain the staged ring-only resync close: one individual :CLnn per
        // kResyncCloseSpacingMs (best-effort  --  hold the index on a full TX queue
        // and retry next tick). Never a group close; never a pie close.
        if (resyncCloseIdx != SEQ_RESYNC_CLOSE_NONE && (int32_t)(now - resyncCloseDueMs) >= 0) {
            char closeCmd[8];
            if (!seqEngineRingCloseCmd(resyncCloseIdx, closeCmd, sizeof(closeCmd))) {
                resyncCloseIdx = SEQ_RESYNC_CLOSE_NONE;  // defensive: out-of-range index
            } else if (domeQueueTx(closeCmd)) {
                resyncCloseIdx++;
                resyncCloseDueMs = now + kResyncCloseSpacingMs;
                if (resyncCloseIdx >= seqEngineRingPanelCount()) {
                    resyncCloseIdx = SEQ_RESYNC_CLOSE_NONE;  // staged close complete
                }
            }
        }

        // The bulk centre an operator started from the output-first table
        // (#318, #365). The request is a transient flag the web handler sets
        // and this clears, the shape POST /api/seq/stop already uses; the run
        // itself - which Outputs, in what order, how far apart - is this task's
        // and never the browser's.
        CommandSource centreSrc = SRC_NONE;
        taskENTER_CRITICAL(&robotStateMux);
        if (robotState.bulkCentreRequest != SRC_NONE) {
            centreSrc = robotState.bulkCentreRequest;
            robotState.bulkCentreRequest = SRC_NONE;
        }
        taskEXIT_CRITICAL(&robotStateMux);
        if (centreSrc != SRC_NONE) {
            if (estopActive || sleepActive) {
                // Refused rather than queued: the droid has let go of every
                // Output, and a sweep that started here would drive parts
                // nobody is watching the moment the halt cleared.
                PA_LOG_WARN(TAG, "[%s] back to centre refused - %s",
                            commandSourceToString(centreSrc),
                            estopActive ? "estop active" : "sleep mode active");
            } else {
                if (centreRun.active && centreRun.kind == SEQ_BULK_CENTRE_BOOT) {
                    PA_LOG_INFO(TAG, "boot pass ended - back to centre took over");
                }
                if (poseRun.active) {
                    PA_LOG_INFO(TAG, "pose ended - back to centre took over");
                    sequencePoseEnd(&poseRun);
                }
                generatedEnd("back to centre took over");
                // A running sequence ends here, the way a web stop ends one: a
                // sequence starting ends a sweep for the same reason (above),
                // and whichever came later is the operator's word. Left
                // running, its body steps and the sweep's rows would be
                // dispatched together - the many-at-once shape the Cadence
                // Floor exists to keep apart.
                if (seqEngineActive(engine)) {
                    PA_LOG_INFO(TAG, "abort %s (back to centre)", activeName);
                    seqEngineAbort(engine);
                    drainBestEffort(engine, now);  // dome and audio resets, never a body move
                    seqEvidenceEnd(SEQ_RUN_ABORTED, "back to centre", now, bodyQueueFullCount());
                    seqStoreReleaseRun();  // reclaim any Learned-run buffers
                    clearSuppression();
                    activeName[0] = '\0';
                }
                sequenceBulkCentreStart(&centreRun, now, (uint8_t)centreSrc);
                // Rows, not outputs going back: how many of them have anything
                // to centre is only known row by row, and the line at the end
                // of the sweep is where that split is reported.
                PA_LOG_INFO(TAG, "[%s] back to centre - %u rows, at least %u ms apart",
                            commandSourceToString(centreSrc),
                            (unsigned)configCacheServoOutputCount(),
                            (unsigned)configCacheCadenceFloorMs());
            }
        }

        // A flutter asked for by Output (a body-owned `:OFnn`), taken after the
        // halts, the Stop and a back to centre press above have had their say.
        flutterTakeRequest(now, estopActive    ? "estop active"
                                : sleepActive  ? "sleep mode active"
                                : centreRun.active ? "back to centre is running"
                                : poseRun.active   ? "a pose is being reached"
                                                   : nullptr);

        // One row per tick, and only when its turn is due. One servo actuating
        // at a time is the rail rule the Cadence Floor holds; a row the sweep
        // passes over costs it no time, because nothing moved.
        if (sequenceBulkCentreRowDue(centreRun, now)) {
            centreOneOutput(centreRun, now);
            if (!centreRun.active) {
                PA_LOG_INFO(TAG, "[%s] %s done - %u centred, %u skipped",
                            commandSourceToString((CommandSource)centreRun.src),
                            bulkCentreName(centreRun), (unsigned)centreRun.centred,
                            (unsigned)centreRun.skipped);
            }
        }

        // One pose command per tick, and only when its turn is due: motions at
        // least the Cadence Floor apart, one Output moving at a time.
        if (sequencePoseDue(poseRun, now)) {
            poseOneCommand(poseRun, now);
            if (!poseRun.active) {
                PA_LOG_INFO(TAG, "[%s] pose done - %u sent, %u skipped",
                            commandSourceToString((CommandSource)poseRun.src), (unsigned)poseRun.sent,
                            (unsigned)poseRun.skipped);
            }
        }

        // The pace the Gestures and the flutters share lives on between runs,
        // so its due time is kept recent (sequencePaceKeepRecent()).
        if (motionReady()) {
            sequencePaceKeepRecent(&motion->gestureRun.dueMs, now);
        }

        // The Gestures a sequence fired, one item per tick when it is due.
        if (gesturesActive()) {
            gestureOneItem(now);
        }

        // The flutters it fired, one leg per tick when one may go. After the
        // Gestures on purpose: when the pace opens with both waiting, the
        // Gesture's move, which its spread placed in time, goes first.
        if (fluttersActive()) {
            flutterOneLeg(now);
        }

        // SAFETY INVARIANT: Suppression window behavior.
        // Advance the cursor: dispatch due actions, retry on queue-full.
        // If a downstream queue is full mid-sequence, the action is retried
        // on the next tick without advancing the cursor.
        if (seqEngineActive(engine)) {
            SeqAction act;
            while (seqEnginePeek(engine, now, esp_random, act)) {
                // The run has reached its end step: the Gestures and the
                // flutters it fired, and its takes, end here, before terminal
                // cleanup, so cleanup is the last thing the run moves (#438,
                // #453, #442; sequenceGestureNext() holds the same line by
                // time).
                if (seqEngineFinishing(engine) &&
                    (gesturesActive() || fluttersActive() || takeRun != nullptr)) {
                    generatedEnd("end step");
                }
                if (!dispatchAction(act)) {
                    if (!retryLogged) {
                        PA_LOG_WARN(TAG, "queue full, retrying: %s", act.payload);
                        retryLogged = true;
                    }
                    seqEvidenceNoteRetry();
                    break;  // retry same action next tick
                }
                // seqEnginePeek flips to finishing when it hits STEP_END, so the
                // finishing flag here classifies this action as terminal cleanup.
                seqEvidenceRecordTx(act, seqEngineFinishing(engine));
                retryLogged = false;
                seqEngineCommit(engine);
            }
            if (!seqEngineActive(engine)) {
                // An end with no cleanup to send finishes inside one peek;
                // its Gestures and flutters end here all the same.
                generatedEnd("end step");
                PA_LOG_INFO(TAG, "end %s", activeName);
                // No-op if an abort path already finalized this run (guarded on
                // RUNNING); otherwise records the normal completion.
                seqEvidenceEnd(SEQ_RUN_COMPLETED, "", now, bodyQueueFullCount());
                seqStoreReleaseRun();  // reclaim any Learned-run buffers now idle
                clearSuppression();
                activeName[0] = '\0';
            }
        }

        // The run's takes, after its steps, so a take's targets never fill
        // servoCmdQueue ahead of a step's move on the same tick.
        if (takeRun != nullptr) {
            takesTick(now);
        }

        // A finish this tick, a run's own end or an abort's drain, that had no
        // room for every terminal action.
        reportFinalDrops(engine);

        // Safety: if no active sequence but flag is still set and timeout expired.
        if (!seqEngineActive(engine)) {
            uint32_t until = 0;
            taskENTER_CRITICAL(&robotStateMux);
            if (robotState.domeSeqActive) {
                until = robotState.domeSeqUntilMs;
            }
            taskEXIT_CRITICAL(&robotStateMux);
            if (until != 0 && millis() >= until) {
                clearSuppression();
            }
        }

        // Compute wait timeout for next iteration: 10 ms if a sequence is
        // active, a resync close is pending, a bulk centre is sweeping, a
        // pose is being reached, or a Gesture or a flutter is being performed;
        // 250 ms otherwise (task blocks on request queue, wakes on TWDT and edges).
        waitMs = sequence_dispatcher_wait_ms(
            seqEngineActive(engine), resyncCloseIdx != SEQ_RESYNC_CLOSE_NONE,
            centreRun.active || poseRun.active || gesturesActive() || fluttersActive());
    }
}
