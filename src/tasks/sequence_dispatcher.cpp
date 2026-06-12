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
// action is retried on the next 10 ms tick (absolute step times — no drift).
// During preempt/estop cleanup the reset drain is best-effort instead, so a
// full queue can never stall an abort.
// =============================================================================

#ifndef PA_NATIVE_TEST_STUBS
#include <Arduino.h>
#include <esp_task_wdt.h>
#endif

#include <string.h>

#include "audio_task.h"
#include "dome_link.h"
#include "logging.h"
#include "robot_state.h"
#include "seq_store.h"
#include "sequence_dispatcher.h"
#include "sequence_engine.h"

static const char* TAG = "SEQ";

// =============================================================================
// Queue
// =============================================================================

#ifndef PA_NATIVE_TEST_STUBS
QueueHandle_t sequenceQueue = nullptr;
#endif

void sequenceDispatcherInit() {
    sequenceQueue = xQueueCreate(4, sizeof(SequenceRequest));
}

// =============================================================================
// sequenceStart — choke point called from RC and web paths.
// =============================================================================

bool sequenceStart(const char* name, CommandSource src) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }

    SequenceLookupResult r = sequenceLookup(name);

    switch (r.kind) {
        case SEQ_RUNTIME:   // Learned Sequence — loaded on demand in the task
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
// Task — Core 0, priority 3, 10 ms tick.
// Not compiled in native test builds.
// =============================================================================

#ifndef PA_NATIVE_TEST_STUBS

static bool dispatchAction(const SeqAction& act) {
    switch (act.kind) {
        case SEQ_ACT_DOME_CMD:
            return domeQueueTx(act.payload);
        case SEQ_ACT_AUDIO_DOLLAR:
            return audioQueueDollar(act.payload, SRC_SEQ);
        case SEQ_ACT_AUDIO_CATEGORY:
            return audioQueuePlayCategory((AudioPlaybackCategory)act.audioCategory,
                                          (AudioPlaybackSlot)act.audioFallbackSlot,
                                          SRC_SEQ);
        case SEQ_ACT_AUDIO_STOP:
            return audioQueueStop(SRC_SEQ);
        default:
            return true;
    }
}

// Best-effort drain of remaining engine actions (abort/preempt cleanup).
// Commits regardless of dispatch result so a full queue cannot stall cleanup.
static void drainBestEffort(SeqEngineState& engine, uint32_t now) {
    SeqAction act;
    while (seqEnginePeek(engine, now, esp_random, act)) {
        if (!dispatchAction(act)) {
            PA_LOG_WARN(TAG, "cleanup action dropped (queue full): %s", act.payload);
        }
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
                // that fails — corrupt file, concurrent Memory Wipe — never
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
                }
                const SequenceEntry* entry = catalogEntry;
                if (isRuntime) {
                    // Copy the staged sequence into the run buffers now that
                    // the previous sequence has been fully drained.
                    seqStoreCommit(runtimeEntry);
                    entry = &runtimeEntry;
                }
                seqEngineStart(engine, entry, now);
                strncpy(activeName, req.name, sizeof(activeName) - 1);
                activeName[sizeof(activeName) - 1] = '\0';
                retryLogged = false;
                setSuppression(now + entry->suppressMs);
                PA_LOG_INFO(TAG, "[%s] start %s suppress=%u ms",
                            commandSourceToString(req.src),
                            entry->name, (unsigned)entry->suppressMs);
            } else if (!isRuntime) {
                PA_LOG_WARN(TAG, "request not in catalog: %s", req.name);
            }
        }

        // Abort on estop; resync the dome to a known safe state on estop-clear.
        bool estopActive = false;
        taskENTER_CRITICAL(&robotStateMux);
        estopActive = robotState.estop;
        taskEXIT_CRITICAL(&robotStateMux);

        if (estopActive && seqEngineActive(engine)) {
            PA_LOG_INFO(TAG, "abort %s (estop)", activeName);
            seqEngineAbort(engine);
            drainBestEffort(engine, now);
            clearSuppression();
            activeName[0] = '\0';
        }
        if (!estopActive && prevEstop) {
            PA_LOG_INFO(TAG, "estop cleared — dome resync");
            domeQueueTx(":CL00");
            domeQueueTx("@0T1");
            domeQueueTx("@0P1");
            seqEngineClearLatches(engine);
        }
        prevEstop = estopActive;

        // Dome (re)connect resync (ADR 0004 decision 8): panel state on the
        // dome is unknown after boot or a link gap, so assume closed — abort
        // any running sequence, close/release everything, clear the latches.
        const bool domeConn = domeConnected();
        if (domeConn && !prevDomeConn) {
            PA_LOG_INFO(TAG, "dome (re)connected — panel state resync");
            if (seqEngineActive(engine)) {
                seqEngineAbort(engine);
                drainBestEffort(engine, now);
                clearSuppression();
                activeName[0] = '\0';
            }
            domeQueueTx(":CL00");
            domeQueueTx("@0T1");
            domeQueueTx("@0P1");
            seqEngineClearLatches(engine);
        }
        prevDomeConn = domeConn;

        // Advance the cursor: dispatch due actions, retry on queue-full.
        if (seqEngineActive(engine)) {
            SeqAction act;
            while (seqEnginePeek(engine, now, esp_random, act)) {
                if (!dispatchAction(act)) {
                    if (!retryLogged) {
                        PA_LOG_WARN(TAG, "queue full, retrying: %s", act.payload);
                        retryLogged = true;
                    }
                    break;  // retry same action next tick
                }
                retryLogged = false;
                seqEngineCommit(engine);
            }
            if (!seqEngineActive(engine)) {
                PA_LOG_INFO(TAG, "end %s", activeName);
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

#endif  // PA_NATIVE_TEST_STUBS
