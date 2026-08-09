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
#include "dome_link.h"
#include "logging.h"
#include "robot_state.h"
#include "seq_store.h"
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

    while (true) {
        esp_task_wdt_reset();
        const uint32_t now = millis();

        // Check for a new request (preempt current sequence if one is running).
        SequenceRequest req = {};
        if (xQueueReceive(sequenceQueue, &req, 0) == pdTRUE) {
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
        taskENTER_CRITICAL(&robotStateMux);
        estopActive = robotState.estop;
        taskEXIT_CRITICAL(&robotStateMux);

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
            PA_LOG_INFO(TAG, "estop cleared — dome resync (staged ring close)");
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
            PA_LOG_INFO(TAG, "dome (re)connected — panel state resync (staged ring close)");
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

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
