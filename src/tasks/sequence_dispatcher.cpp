// =============================================================================
// src/tasks/sequence_dispatcher.cpp
//
// Body-side DM:* sequence coordinator (ADR 0004, issue #2).
//
// Slice 1: flat sequences only (VADER, HELLO, NOD) + 24 dome-forward aliases.
// STEP_LOOP and STEP_RANDOM are designed-in enum values; not executed here.
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
#include "sequence_dispatcher.h"

// =============================================================================
// Catalog — step arrays
// =============================================================================

// DM:VADER — Imperial March visual mode (47 s).
// Holos, logics, and PSI set to MARCH mode; reset at sequence end.
static const SeqStep kVaderSteps[] = {
    { 0,     STEP_AUDIO,    FX_NONE,      "$M"            },  // Imperial March
    { 0,     STEP_DOME_CMD, FX_LOGIC_PSI, "@HPA0021|47"   },  // all holos red 47 s
    { 0,     STEP_DOME_CMD, FX_LOGIC_PSI, "@0T11"         },  // MARCH logics
    { 0,     STEP_DOME_CMD, FX_LOGIC_PSI, "@0P11"         },  // MARCH PSI
    { 47000, STEP_DOME_CMD, FX_NONE,      "@0T1"          },  // reset logics
    { 47000, STEP_DOME_CMD, FX_NONE,      "@0P1"          },  // reset PSI
    { 47000, STEP_END,      FX_NONE,      ""              },
};

// DM:HELLO — "Hello There" greeting (4 s).
// Front and rear logic text, then a six-pulse P1 panel wave.
static const SeqStep kHelloSteps[] = {
    {   0, STEP_AUDIO,    FX_NONE,  "$3"              },  // happy/greeting clip
    {   0, STEP_DOME_CMD, FX_NONE,  "@1MHello There"  },  // front logic text
    {   0, STEP_DOME_CMD, FX_NONE,  "@3MGeneral Kenobi" },
    {   0, STEP_DOME_CMD, FX_NONE,  ":SM0,2200,150"   },  // P1 open
    { 160, STEP_DOME_CMD, FX_NONE,  ":SM0,1500,150"   },  // P1 half
    { 320, STEP_DOME_CMD, FX_NONE,  ":SM0,2200,150"   },  // P1 open
    { 480, STEP_DOME_CMD, FX_NONE,  ":SM0,1500,150"   },  // P1 half
    { 640, STEP_DOME_CMD, FX_NONE,  ":SM0,2200,150"   },  // P1 open
    { 800, STEP_DOME_CMD, FX_PANEL, ":SM0,800,150"    },  // P1 close
    { 950, STEP_DOME_CMD, FX_NONE,  ":CL00"           },  // release panels
    { 950, STEP_END,      FX_NONE,  ""                },
};

// DM:NOD — short acknowledgment: sound + logic text + P1 wave.
// Demonstrates sound-to-motion sync from a single body clock (issue #2 § new sequence).
static const SeqStep kNodSteps[] = {
    {   0, STEP_AUDIO,    FX_NONE,  "$3"             },  // ack/happy clip
    {   0, STEP_DOME_CMD, FX_NONE,  "@1MYes"         },  // logic text
    {   0, STEP_DOME_CMD, FX_NONE,  ":SM0,2200,150"  },  // P1 open
    { 150, STEP_DOME_CMD, FX_PANEL, ":SM0,800,150"   },  // P1 close
    { 300, STEP_DOME_CMD, FX_NONE,  ":CL00"          },  // release
    { 300, STEP_END,      FX_NONE,  ""               },
};

// =============================================================================
// Catalog table
// =============================================================================

static const SequenceEntry kCatalog[] = {
    { "DM:VADER", kVaderSteps, 7,  47000 },
    { "DM:HELLO", kHelloSteps, 11, 4000  },
    { "DM:NOD",   kNodSteps,   6,  3000  },
};
static constexpr uint8_t kCatalogSize =
    (uint8_t)(sizeof(kCatalog) / sizeof(kCatalog[0]));

// =============================================================================
// Alias table — DM:* names that forward directly to the dome unchanged or
// mapped to a :SE## / $NNN target. No body execution; dome owns these.
// =============================================================================

struct AliasEntry {
    const char* name;
    const char* target;
};

static const AliasEntry kAliases[] = {
    { "DM:STOP",           ":SE00" },
    { "DM:SESCREAM",       ":SE01" },
    { "DM:WAVE",           ":SE02" },
    { "DM:SMIRKWAVE",      ":SE03" },
    { "DM:OCWAVE",         ":SE04" },
    { "DM:BEEPCANTINA",    ":SE05" },
    { "DM:SHORT",          ":SE06" },
    { "DM:SECANTINA",      ":SE07" },
    { "DM:SELEIA",         ":SE08" },
    { "DM:DISCO",          ":SE09" },
    { "DM:SCREAMNOPANEL",  ":SE50" },
    { "DM:SCREAMPANEL",    ":SE51" },
    { "DM:WAVEPANEL",      ":SE52" },
    { "DM:SMIRKWAVEPANEL", ":SE53" },
    { "DM:OPENWAVE",       ":SE54" },
    { "DM:MARCHINGANTS",   ":SE55" },
    { "DM:FAINT",          ":SE56" },
    { "DM:RYTHMIC",        ":SE57" },
    { "DM:HARLEMSHAKE",    "$815"  },
    { "DM:GIRLONFIRE",     "$821"  },
    { "DM:YODA",           "$720"  },
    { "DM:TOPPANELS",      ":SE12" },
    { "DM:WIGGLE",         ":SE16" },
    { "DM:BYEBYE",         ":SE58" },
};
static constexpr uint8_t kAliasSize =
    (uint8_t)(sizeof(kAliases) / sizeof(kAliases[0]));

// =============================================================================
// Pure routing — no side effects, native-testable.
// =============================================================================

SequenceLookupResult sequenceLookup(const char* name) {
    SequenceLookupResult r = { SEQ_FALLBACK, {} };

    if (name == nullptr || name[0] == '\0') {
        return r;
    }

    for (uint8_t i = 0; i < kCatalogSize; ++i) {
        if (strcmp(kCatalog[i].name, name) == 0) {
            r.kind = SEQ_CATALOG;
            return r;
        }
    }

    for (uint8_t i = 0; i < kAliasSize; ++i) {
        if (strcmp(kAliases[i].name, name) == 0) {
            r.kind = SEQ_ALIAS;
            strncpy(r.aliasTarget, kAliases[i].target, sizeof(r.aliasTarget) - 1);
            r.aliasTarget[sizeof(r.aliasTarget) - 1] = '\0';
            return r;
        }
    }

    return r;  // SEQ_FALLBACK
}

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
// Dispatch helpers — called only from the task.
// =============================================================================

static const char* TAG = "SEQ";

static void emitAutoReset(uint8_t activeFx) {
    if (activeFx & FX_LOGIC_PSI) {
        domeQueueTx("@0T1");
        domeQueueTx("@0P1");
    }
    if (activeFx & FX_PANEL) {
        domeQueueTx(":CL00");
    }
}

static void dispatchStep(const SeqStep& step) {
    switch (step.type) {
        case STEP_DOME_CMD:
            if (!domeQueueTx(step.payload)) {
                PA_LOG_WARN(TAG, "dome TX queue full, dropped: %s", step.payload);
            }
            break;
        case STEP_AUDIO:
            if (!audioQueueDollar(step.payload, SRC_SEQ)) {
                PA_LOG_WARN(TAG, "audio queue full, dropped: %s", step.payload);
            }
            break;
        default:
            break;
    }
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
            PA_LOG_DEBUG(TAG, "[%s] fallback -> dome: %s",
                         commandSourceToString(src), name);
            return domeQueueTx(name);
    }
}

// =============================================================================
// Task — Core 0, priority 3, 10 ms tick.
// Not compiled in native test builds.
// =============================================================================

#ifndef PA_NATIVE_TEST_STUBS

void sequenceDispatcherTask(void* /*pvParameters*/) {
    esp_task_wdt_add(NULL);

    const SequenceEntry* active    = nullptr;
    uint8_t              cursor    = 0;
    uint32_t             seqStart  = 0;
    uint8_t              activeFx  = 0;

    while (true) {
        esp_task_wdt_reset();
        const uint32_t now = millis();

        // Check for a new request (preempt current sequence if one is running).
        SequenceRequest req = {};
        if (xQueueReceive(sequenceQueue, &req, 0) == pdTRUE) {
            if (active != nullptr) {
                PA_LOG_INFO(TAG, "preempt %s -> %s", active->name, req.name);
                emitAutoReset(activeFx);
            }
            active = nullptr;
            activeFx = 0;

            for (uint8_t i = 0; i < kCatalogSize; ++i) {
                if (strcmp(kCatalog[i].name, req.name) == 0) {
                    active   = &kCatalog[i];
                    cursor   = 0;
                    seqStart = now;
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.domeSeqActive  = true;
                    robotState.domeSeqUntilMs = now + active->suppressMs;
                    taskEXIT_CRITICAL(&robotStateMux);
                    PA_LOG_INFO(TAG, "[%s] start %s suppress=%u ms",
                                commandSourceToString(req.src),
                                active->name, (unsigned)active->suppressMs);
                    break;
                }
            }
        }

        // Abort on estop.
        bool estopActive = false;
        taskENTER_CRITICAL(&robotStateMux);
        estopActive = robotState.estop;
        taskEXIT_CRITICAL(&robotStateMux);

        if (active != nullptr && estopActive) {
            PA_LOG_INFO(TAG, "abort %s (estop)", active->name);
            emitAutoReset(activeFx);
            taskENTER_CRITICAL(&robotStateMux);
            robotState.domeSeqActive = false;
            taskEXIT_CRITICAL(&robotStateMux);
            active   = nullptr;
            activeFx = 0;
        }

        // Advance cursor.
        if (active != nullptr) {
            while (cursor < active->stepCount) {
                const SeqStep& step = active->steps[cursor];

                if (step.type == STEP_END) {
                    PA_LOG_INFO(TAG, "end %s", active->name);
                    emitAutoReset(activeFx);
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.domeSeqActive = false;
                    taskEXIT_CRITICAL(&robotStateMux);
                    active   = nullptr;
                    activeFx = 0;
                    break;
                }

                if ((now - seqStart) >= step.tMs) {
                    dispatchStep(step);
                    activeFx |= step.effectClass;
                    cursor++;
                } else {
                    break;  // not time yet; revisit next tick
                }
            }
        }

        // Safety: if no active sequence but flag is still set and timeout expired.
        if (active == nullptr) {
            uint32_t until = 0;
            taskENTER_CRITICAL(&robotStateMux);
            if (robotState.domeSeqActive) {
                until = robotState.domeSeqUntilMs;
            }
            taskEXIT_CRITICAL(&robotStateMux);
            if (until != 0 && millis() >= until) {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.domeSeqActive = false;
                taskEXIT_CRITICAL(&robotStateMux);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

#endif  // PA_NATIVE_TEST_STUBS
