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
#include "dome_link.h"
#include "logging.h"
#include "robot_state.h"
#include "seq_store.h"
#include "sequence_body_step.h"
#include "sequence_bulk_centre.h"
#include "sequence_dispatcher.h"
#include "sequence_dispatcher_step.h"
#include "sequence_engine.h"
#include "sequence_gesture.h"
#include "sequence_pose.h"
#include "sequence_run_evidence.h"
#include "servo_task.h"  // servoTaskDrivesOutput() - an undriven Output is passed over (#364)

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

void sequenceDispatcherInit() {
    sequenceQueue = xQueueCreate(4, sizeof(SequenceRequest));
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
// time because that is how the cache hands them out -- one 70-byte row on this
// frame rather than the whole 1682-byte table.
//
// A Part no Output claims is REPORTED and the sequence carries on: an unwired
// Part is the normal state of a build in progress, so the step is inert and the
// rest of the choreography is untouched (#301). Returning true is therefore
// correct -- the action was handled, and only a full queue is a retry.
//
// A flutter resolves to its how-far target, because a flutter ends open
// (ADR 0049). The oscillation on the way there is NOT performed yet: it is
// generated motion, and generated motion is what the Cadence Floor paces. The
// Floor now exists (include/sequence_bulk_centre.h, #365), on the dome's figure
// as an explicitly-labelled stand-in, but it bounds the bulk centre it was
// built for and nothing else - a flutter is a second expansion, with its own
// question about what the oscillation should look like, and nobody has decided
// that yet. Until it is performed, a routine fluttering several Parts at once
// would be emitting exactly the many-at-once shape the Floor is there to hold
// apart.
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
    uint8_t  armId;
    uint16_t throwMs;
};

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

    ServoCommand cmd = {};
    cmd.armId = plan.armId;
    cmd.type = SERVO_CMD_POSITION;
    cmd.positionUs = plan.targetUs;
    cmd.source = SRC_SEQ;
    cmd.motionThrowMs = throwMs;
    cmd.motionEasing = easing;
    cmd.timestampMs = millis();
    if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
        return false;
    }
    if (outcome != nullptr) {
        outcome->sent = true;
        outcome->armId = plan.armId;
        outcome->throwMs = (throwMs != 0) ? throwMs : row.throw_ms;  // the move as asked
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

    if (run.awaitArm != SEQ_BULK_CENTRE_NO_AWAIT) {
        ServoCommandedPosition at = {};
        if (run.awaitArm < SERVO_ARM_COUNT) {
            taskENTER_CRITICAL(&robotStateMux);
            at = robotState.servoCommanded[run.awaitArm];
            taskEXIT_CRITICAL(&robotStateMux);
        }
        const SeqBulkCentreAwait awaited = sequenceBulkCentreAwaitCheck(run, now, at);
        if (awaited == SEQ_AWAIT_WAIT) {
            return;  // still moving: looked at again on the next tick
        }
        if (awaited == SEQ_AWAIT_RELEASE) {
            ServoCommand cmd = {};
            cmd.armId = run.awaitArm;
            cmd.type = SERVO_CMD_RELEASE;
            cmd.source = SRC_SEQ;
            cmd.timestampMs = now;
            if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
                return;  // owed still: it comes round again on the next tick
            }
        } else if (awaited == SEQ_AWAIT_DROP) {
            PA_LOG_INFO(TAG, "arm%u has no pulse left to release", (unsigned)run.awaitArm + 1);
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
        sequenceBulkCentreAdvance(&run, rowCount, now, /*started=*/false, 0);
        return;
    }

    const SeqBodyStepPlan plan = sequenceBodyCentrePlan(row);
    if (!plan.drive) {
        PA_LOG_INFO(TAG, "output %u not centred - %s", (unsigned)run.nextRow,
                    consoleReasonString(plan.reason));
        sequenceBulkCentreAdvance(&run, rowCount, now, /*started=*/false, 0);
        return;
    }
    // An Output ServoTask does not drive since boot - a wired tick saved
    // since, or none at all - would drop the move without a word, and the run
    // would still spend a Cadence Floor slot waiting on it. Passed over and
    // counted instead, costing no time (#364). POST /api/servo/centre names
    // these rows in its answer (servoCentreSkipped(), src/web/api_servo.cpp).
    if (!servoTaskDrivesOutput(plan.armId)) {
        PA_LOG_INFO(TAG, "arm%u not centred - restart the droid to use it",
                    (unsigned)plan.armId + 1);
        sequenceBulkCentreAdvance(&run, rowCount, now, /*started=*/false, 0);
        return;
    }

    // The same command a Body Step queues, to the same queue, with the same
    // clamp already applied: one motion path, and ServoTask still decides the
    // move's own shape from the Output's Motion Profile (ADR 0052).
    ServoCommand cmd = {};
    cmd.armId = plan.armId;
    cmd.type = SERVO_CMD_POSITION;
    cmd.positionUs = plan.targetUs;
    cmd.source = SRC_SEQ;
    cmd.timestampMs = now;
    if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
        return;  // the cursor stays put: this row's turn comes round again
    }
    sequenceBulkCentreAwait(&run, plan.armId, step.releaseAfter);
    sequenceBulkCentreAdvance(&run, rowCount, now, /*started=*/true, row.throw_ms);
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
static SeqPosePlan posePlan;  // static: off this task's measured stack (ADR 0040)

static bool dispatchAction(const SeqAction& act);  // defined with the task adapter below

// -----------------------------------------------------------------------------
// The Gestures a sequence has fired (#438, include/sequence_gesture.h), on the
// Coordinator's own cursor so they never hold the engine's. Static, off this
// task's measured stack, as the pose plan is (ADR 0040); so is the one body
// move a turn builds, which would otherwise sit on the root frame.
// -----------------------------------------------------------------------------
static SeqGestureRun gestureRun;
static SeqAction gestureMove;

// Every path that ends what the Coordinator is doing ends the Gestures too:
// a halt, a stop, a later run, a pose and back to centre alike.
static void gestureEnd(const char* why) {
    if (sequenceGestureActive(gestureRun)) {
        PA_LOG_INFO(TAG, "gesture ended (%s) after %u sent, %u skipped", why,
                    (unsigned)gestureRun.sent, (unsigned)gestureRun.skipped);
    }
    sequenceGestureEnd(&gestureRun);
}

// A Gesture the engine has just handed over, copied into the run NOW, while
// the engine that fired it is still active: a Learned run's steps are freed
// when the run ends, and a Gesture may outlive it (SeqAction, sequence_engine.h).
static __attribute__((noinline)) void gestureStartFromAction(const SeqAction& act) {
    // act.domeDurationMs is where the firing run ends: no pass starts after it.
    if (act.gesture != nullptr &&
        !sequenceGestureStart(&gestureRun, *act.gesture, millis(), act.domeDurationMs)) {
        PA_LOG_WARN(TAG, "gesture %s not performed - nothing to perform, or four already running",
                    act.payload);
    }
}

// One Gesture item per tick, when it is due: a dome Gesture's pass is its `$`
// command to the dome; a body Gesture's move goes down the very path a Body
// Step takes (dispatchBodyMove()), resolved against the live Output rows, and
// the run holds the next body move off by the pace every generated motion
// keeps. A full queue leaves the item where it is, to come round next tick.
// Out of line, as gestureStartFromAction() below is, so neither's locals sit
// on the root frame or on dispatchAction()'s: both are on the measured chain.
static __attribute__((noinline)) void gestureOneItem(uint32_t now) {
    bool moving = false;
    if (gestureRun.awaitArm < SERVO_ARM_COUNT) {
        taskENTER_CRITICAL(&robotStateMux);
        moving = robotState.servoCommanded[gestureRun.awaitArm].moving;
        taskEXIT_CRITICAL(&robotStateMux);
    }
    SeqGestureNext next = {};
    if (!sequenceGestureNext(&gestureRun, now, moving, &next)) {
        return;
    }
    if (next.dome) {
        if (!domeQueueTx(gestureRun.g[next.entry].domeCmd)) {
            return;
        }
        sequenceGestureDone(&gestureRun, next, now, /*started=*/true, 0, SEQ_BULK_CENTRE_NO_AWAIT);
        return;
    }
    memset(&gestureMove, 0, sizeof(gestureMove));
    gestureMove.kind = SEQ_ACT_BODY_MOVE;
    strncpy(gestureMove.payload, droidPartIdAt(next.part), sizeof(gestureMove.payload) - 1);
    gestureMove.bodyShape = (uint8_t)next.shape;
    gestureMove.bodyHowFar = next.howFar;
    // Paced by the Output's own throw unless the Gesture states one: the
    // spacing is how long this move takes.
    BodyMoveOutcome moved = {false, SEQ_BULK_CENTRE_NO_AWAIT, 0};
    if (!dispatchBodyMove(gestureMove, &moved, next.speedMs, next.easing)) {
        return;
    }
    sequenceGestureDone(&gestureRun, next, now, moved.sent, moved.throwMs, moved.armId);
}

static void poseOneCommand(SeqPoseRun& run, uint32_t now) {
    if (run.awaitArm != SEQ_BULK_CENTRE_NO_AWAIT) {
        bool moving = false;
        if (run.awaitArm < SERVO_ARM_COUNT) {
            taskENTER_CRITICAL(&robotStateMux);
            moving = robotState.servoCommanded[run.awaitArm].moving;
            taskEXIT_CRITICAL(&robotStateMux);
        }
        if (!sequencePoseAwaitDone(&run, moving)) {
            return;  // still moving: looked at again on the next tick
        }
    }
    if (sequencePoseFinished(run)) {
        sequencePoseEnd(&run);  // the last command's spacing has run: the pose is reached
        return;
    }

    const SeqPoseCmd& cmd = posePlan.cmds[run.next];
    switch (cmd.cls) {
        case SEQ_POSE_PANEL:
            if (!domeQueueTx(cmd.act.payload)) {
                return;
            }
            sequencePoseAdvance(&run, now, cmd.cls, /*started=*/true, 0, SEQ_BULK_CENTRE_NO_AWAIT);
            return;

        case SEQ_POSE_BODY: {
            // A Part nothing can move is reported by dispatchBodyMove() and
            // passed over here, costing the pose no time.
            BodyMoveOutcome moved = {false, SEQ_BULK_CENTRE_NO_AWAIT, 0};
            if (!dispatchBodyMove(cmd.act, &moved)) {
                return;
            }
            sequencePoseAdvance(&run, now, cmd.cls, moved.sent, moved.throwMs, moved.armId);
            return;
        }

        case SEQ_POSE_INSTANT:
        default:
            if (!dispatchAction(cmd.act)) {
                return;
            }
            sequencePoseAdvance(&run, now, cmd.cls, /*started=*/true, 0, SEQ_BULK_CENTRE_NO_AWAIT);
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
    robotState.seqStopRequested = true;
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

        case SEQ_DISPATCH_BODY_MOVE:
            return dispatchBodyMove(act);

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
    centreRun.awaitArm = SEQ_BULK_CENTRE_NO_AWAIT;

    // The pose press's run (#440), over the static posePlan. Static for the
    // same reason as the sweep's.
    static SeqPoseRun poseRun;
    poseRun = SeqPoseRun{};
    poseRun.awaitArm = SEQ_BULK_CENTRE_NO_AWAIT;
    gestureRun = SeqGestureRun{};
    gestureRun.awaitArm = SEQ_BULK_CENTRE_NO_AWAIT;
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
                        (unsigned)configCacheServoOutputCount(), (unsigned)SEQ_CADENCE_FLOOR_MS);
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
                gestureEnd("a pose took over");
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
                                     &posePlan);
                    if (isRuntime) {
                        seqStoreReleaseRun();  // the plan holds its own copies
                    }
                    // A pose that starts supersedes a staged resync close, as a
                    // run does below: one motion owner on the dome at a time.
                    sequencePoseStart(&poseRun, now, poseEstop, poseSleep, posePlan.count,
                                      (uint8_t)poseAsk.src, &resyncCloseIdx);
                    PA_LOG_INFO(TAG, "[%s] pose %s at %u ms - %u commands, motions at least %u ms apart",
                                commandSourceToString(poseAsk.src), poseAsk.name, (unsigned)poseAsk.atMs,
                                (unsigned)posePlan.count, (unsigned)SEQ_CADENCE_FLOOR_MS);
                    if (posePlan.truncated) {
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
                // from the one before, whether or not that one is still running.
                gestureEnd("a later run");
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
        // Gestures end on either halt the same way: ServoTask has let every
        // Output go, and a Gesture still expanding would drive them again.
        if (estopActive || sleepActive) {
            gestureEnd(estopActive ? "estop" : "sleep mode");
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
            // finding). Pies are never auto-closed on resync. Logic/PSI reset is
            // a single non-servo command, so it stays immediate. A pose taken
            // on this same tick, before the edge was seen, ends here.
            if (sequenceResyncCloseStage(&poseRun, &resyncCloseIdx, &resyncCloseDueMs, now)) {
                PA_LOG_INFO(TAG, "pose ended - dome resync");
            }
            domeQueueTx("@0T1");
            domeQueueTx("@0P1");
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
            gestureEnd("web stop");
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
            gestureEnd("dome resync");
            domeQueueTx("@0T1");
            domeQueueTx("@0P1");
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
                gestureEnd("back to centre took over");
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
                            (unsigned)SEQ_CADENCE_FLOOR_MS);
            }
        }

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

        // The Gestures a sequence fired, one item per tick when it is due.
        if (sequenceGestureActive(gestureRun)) {
            gestureOneItem(now);
        }

        // SAFETY INVARIANT: Suppression window behavior.
        // Advance the cursor: dispatch due actions, retry on queue-full.
        // If a downstream queue is full mid-sequence, the action is retried
        // on the next tick without advancing the cursor.
        if (seqEngineActive(engine)) {
            SeqAction act;
            while (seqEnginePeek(engine, now, esp_random, act)) {
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
                // A Gesture this run fired was bounded by this end when it was
                // handed over (seqGesturePassesBefore()): no pass starts after
                // it, and a pass already under way finishes its moves.
                PA_LOG_INFO(TAG, "end %s", activeName);
                // No-op if an abort path already finalized this run (guarded on
                // RUNNING); otherwise records the normal completion.
                seqEvidenceEnd(SEQ_RUN_COMPLETED, "", now, bodyQueueFullCount());
                seqStoreReleaseRun();  // reclaim any Learned-run buffers now idle
                clearSuppression();
                activeName[0] = '\0';
            }
        }

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
        // active, a resync close is pending, a bulk centre is sweeping or a
        // pose is being reached;
        // 250 ms otherwise (task blocks on request queue, wakes on TWDT and edges).
        waitMs = sequence_dispatcher_wait_ms(seqEngineActive(engine), resyncCloseIdx != SEQ_RESYNC_CLOSE_NONE,
                                             centreRun.active || poseRun.active ||
                                                 sequenceGestureActive(gestureRun));
    }
}
