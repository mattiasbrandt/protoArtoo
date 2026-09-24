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
#include "sequence_run_evidence.h"

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
static bool dispatchBodyMove(const SeqAction& act) {
    const uint8_t rowCount = configCacheServoOutputCount();
    ServoOutputRow row = {};
    const ServoOutputRow* driving = nullptr;
    for (uint8_t i = 0; i < rowCount; ++i) {
        if (configCacheReadServoOutput(i, &row) &&
            servoOutputDrivesPart(row, act.payload)) {
            driving = &row;
            break;
        }
    }

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
    cmd.timestampMs = millis();
    return xQueueSend(servoCmdQueue, &cmd, 0) == pdTRUE;
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
    SeqAction act;
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
    // must never send it. resyncCloseIdx == 0xFF means no staged close pending.
    const uint32_t kResyncCloseSpacingMs = 500;
    uint8_t        resyncCloseIdx = 0xFF;
    uint32_t       resyncCloseDueMs = 0;
    uint32_t       waitMs = 10;  // task wake timeout; computed at end of each iteration

    // The bulk centre sweep an operator starts from the output-first table
    // (#318, #365), and the boot pass on the same cursor (#414). Static for the
    // same reason the engine above is: the cursor stays off this task's
    // measured stack chain (ADR 0040).
    static SeqBulkCentreRun centreRun;
    centreRun = SeqBulkCentreRun{};
    centreRun.awaitArm = SEQ_BULK_CENTRE_NO_AWAIT;

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

        // Check for a new request (preempt current sequence if one is running).
        if (haveReq) {
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
                    resyncCloseIdx = 0xFF;  // a new run supersedes any staged resync close
                    // ...and so does a bulk centre still sweeping. A sequence
                    // and a sweep both move body Outputs, and two of them
                    // interleaving is the many-at-once shape the Cadence Floor
                    // exists to keep apart. The sequence is the later word.
                    if (centreRun.active) {
                        PA_LOG_INFO(TAG, "%s ended - %s took over", bulkCentreName(centreRun),
                                    req.name);
                        sequenceBulkCentreEnd(&centreRun);
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
            // a single non-servo command, so it stays immediate.
            resyncCloseIdx = 0;
            resyncCloseDueMs = now;
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
        // running sequence, stage an individual ring-only close (drained below),
        // and clear the latches. Never a group :CL15/:CL00 (see the estop-clear
        // resync above  --  a group close browns out the dome from a loaded ring);
        // pies are never auto-closed on resync.
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
            resyncCloseIdx = 0;
            resyncCloseDueMs = now;
            domeQueueTx("@0T1");
            domeQueueTx("@0P1");
            seqEngineClearLatches(engine);
        }
        prevDomeConn = domeConn;

        // Drain the staged ring-only resync close: one individual :CLnn per
        // kResyncCloseSpacingMs (best-effort  --  hold the index on a full TX queue
        // and retry next tick). Never a group close; never a pie close.
        if (resyncCloseIdx != 0xFF && (int32_t)(now - resyncCloseDueMs) >= 0) {
            char closeCmd[8];
            if (!seqEngineRingCloseCmd(resyncCloseIdx, closeCmd, sizeof(closeCmd))) {
                resyncCloseIdx = 0xFF;  // defensive: out-of-range index
            } else if (domeQueueTx(closeCmd)) {
                resyncCloseIdx++;
                resyncCloseDueMs = now + kResyncCloseSpacingMs;
                if (resyncCloseIdx >= seqEngineRingPanelCount()) {
                    resyncCloseIdx = 0xFF;  // staged close complete
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
        // active, a resync close is pending or a bulk centre is sweeping;
        // 250 ms otherwise (task blocks on request queue, wakes on TWDT and edges).
        waitMs = sequence_dispatcher_wait_ms(seqEngineActive(engine), resyncCloseIdx != 0xFF,
                                             centreRun.active);
    }
}
