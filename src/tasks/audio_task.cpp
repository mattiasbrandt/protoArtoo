// =============================================================================
// src/tasks/audio_task.cpp
//
// AudioTask  --  sole writer to the audio serial GPIO (PIN_AUDIO_TX, GPIO 26).
//
// Imperative adapter for the Audio Step Core (audio_task_step, ADR 0014):
//   - Gathers one generation of inputs per loop iteration (config, RobotState).
//   - Calls the step phases in loop order and executes their plain-data actions.
//   - Owns every side effect: driver init and playback calls, dome-UART
//     arbitration, NVS binding-cache refresh, RobotState audio-zone writes.
// Decision logic (lifecycle transitions, '$'/command translation, playback
// policy invocation, volume and random-mode state, status/catalog gating)
// lives in the step core.
//
// Feature toggle: enable_audio is staged at reboot (ADR 0027). The task is
// only spawned when enabled at boot; the audioQueue* helpers gate on the
// boot-latched state and return true (accepted-and-discarded) when disabled.
//
// Core assignment: Core 0 (non-RT).
// Reason: software bit-bang TX blocks for up to ~6 ms per command; keeping
// AudioTask on Core 0 prevents any interaction with DriveTask / ServoTask
// timing on Core 1.
//
// Driver selection: PA_AUDIO_DRIVER build flag in platformio.ini.
// =============================================================================

#include "audio_task.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "audio_config_map.h"
#include "audio_dollar_parser.h"
#include "audio_driver.h"
#include "audio_task_step.h"
#include "config.h"
#include "config_nvsio.h"
#include "config_cache.h"
#include "dome_link.h"
#include "logging.h"
#include "queue_drop_tracker.h"
#include "robot_state.h"
#include "web_server.h"

// -----------------------------------------------------------------------------
// Driver instantiation  --  one concrete driver per build
// -----------------------------------------------------------------------------
#if PA_AUDIO_DRIVER == AUDIO_SOFT_UART
#include "audio_dy_sv5w.h"
static AudioDriverDySv5w s_driver;
#elif PA_AUDIO_DRIVER == AUDIO_CHIRP
#include "audio_chirp.h"
static AudioDriverChirp s_driver;
#elif PA_AUDIO_DRIVER == AUDIO_DFPLAYER
#error "AUDIO_DFPLAYER driver not yet implemented - see T15 / T16"
#elif PA_AUDIO_DRIVER == AUDIO_MP3TRIGGER
#include "audio_mp3trigger.h"
static AudioDriverMp3Trigger s_driver;
#else
#error "PA_AUDIO_DRIVER build flag is not set or has an unknown value"
#endif

static AudioDriver* const driver = &s_driver;

const char* audioGetDriverName() {
    return driver->driverName();
}

uint8_t audioGetCapabilities() {
    return driver->capabilities();
}

const char* audioRxStatusToken(AudioRxStatus status) {
    switch (status) {
        case AUDIO_RX_AVAILABLE:
            return "available";
        case AUDIO_RX_BLOCKED_BY_DOME_UART:
            return "blocked_by_dome_uart";
        case AUDIO_RX_NO_RESPONSE:
            return "no_response";
        case AUDIO_RX_UNKNOWN:
        default:
            return "unknown";
    }
}

const char* audioRxStatusDetail(AudioRxStatus status) {
    switch (status) {
        case AUDIO_RX_AVAILABLE:
            return "Sound module RX is available";
        case AUDIO_RX_BLOCKED_BY_DOME_UART:
            return "Status unavailable: DomeLink is using UART";
        case AUDIO_RX_NO_RESPONSE:
            return "Sound module did not respond on RX";
        case AUDIO_RX_UNKNOWN:
        default:
            return "Sound module RX status unknown";
    }
}

static void setAudioRxStatus(AudioRxStatus status) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.audio_module_rx_status = status;
    taskEXIT_CRITICAL(&robotStateMux);
}

const AudioCatalogEntry* audioGetCatalogEntries(uint16_t* count) {
    if (count) {
        *count = driver->getCatalogEntryCount();
    }
    return driver->getCatalogEntries();
}

const AudioCatalogBank* audioGetCatalogBanks(uint8_t* count) {
    if (count) {
        *count = driver->getCatalogBankCount();
    }
    return driver->getCatalogBanks();
}

bool audioIsCatalogReady() {
    return driver->isCatalogReady();
}

static const char* TAG = "AudioTask";

// Audio output is staged at reboot (ADR 0027); when inactive, commands are
// accepted and discarded so callers (sequence engine, web routes) see the same
// success semantics as the old drain-and-discard task.
static bool audioOutputInactive() {
    return !configCacheReadActiveAudioEnabled();
}

// -----------------------------------------------------------------------------
// Queue-send helpers (non-blocking, timeout 0)
// Queue helpers gate on audioOutputInactive(): when audio is disabled at boot,
// commands return true (accepted) but are not enqueued (staged-at-reboot per ADR 0027).
// -----------------------------------------------------------------------------

bool audioQueueDollar(const char* cmd, CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    if (!cmd || cmd[0] != '$') {
        return false;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_DOLLAR;
    msg.source = src;
    strncpy(msg.dollar, cmd, sizeof(msg.dollar) - 1);
    msg.dollar[sizeof(msg.dollar) - 1] = '\0';
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        logQueueDrop(QUEUE_AUDIO_CMD, "dollar command");
        return false;
    }
    return true;
}

bool audioQueuePlayTrack(uint16_t track, CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    if (track == 0) {
        return false;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_PLAY_TRACK;
    msg.source = src;
    msg.track = track;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN(TAG, "audioCmdQueue full, dropped play track command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueuePlayTrackBanked(uint16_t index, uint8_t bank, char page, CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    if (index == 0 || bank == 0) {
        return false;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_PLAY_TRACK_BANKED;
    msg.source = src;
    msg.banked.index = index;
    msg.banked.bank = bank;
    msg.banked.page = page;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN(TAG, "audioCmdQueue full, dropped play track banked command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueuePlaySlot(AudioPlaybackSlot slot, CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    if (slot == AUDIO_SLOT_NONE) {
        return false;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_PLAY_SLOT;
    msg.source = src;
    msg.slot = slot;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN(TAG, "audioCmdQueue full, dropped play slot command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueuePlayCategory(AudioPlaybackCategory category, AudioPlaybackSlot fallbackSlot,
                            CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    if (!audioPlaybackIsValidCategory(category)) {
        return false;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_PLAY_CATEGORY;
    msg.source = src;
    msg.category.category = category;
    msg.category.fallbackSlot = fallbackSlot;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN(TAG, "audioCmdQueue full, dropped play category command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueStop(CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_STOP;
    msg.source = src;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN(TAG, "audioCmdQueue full, dropped stop command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueTrackStop(CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_TRACK_STOP;
    msg.source = src;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN(TAG, "audioCmdQueue full, dropped track stop command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueSetVolume(uint8_t vol, CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_SET_VOLUME;
    msg.source = src;
    msg.volume = audioClampVolume(vol);
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN(TAG, "audioCmdQueue full, dropped set volume command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueQueryStatus(CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_QUERY_STATUS;
    msg.source = src;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN(TAG, "audioCmdQueue full, dropped query status command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueRefreshCatalog(CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_REFRESH_CATALOG;
    msg.source = src;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN(TAG, "audioCmdQueue full, dropped refresh catalog command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueRefreshBindings(CommandSource src) {
    if (audioOutputInactive()) {
        return true;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_REFRESH_BINDINGS;
    msg.source = src;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        static uint32_t lastWarnMs = 0;
        uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastWarnMs) > 5000) {  // Rate-limit to once per 5s
            PA_LOG_WARN(TAG, "audioCmdQueue full, dropped refresh bindings command");
            lastWarnMs = nowMs;
        }
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Playback policy adapters.
// -----------------------------------------------------------------------------
static AudioBindingCache s_audioBindings = {};

static bool refreshChirpBindingCacheFromNvs() {
    const bool catalogCapable = (driver->capabilities() & AudioDriver::AUDIO_CAP_CATALOG) != 0;
    if (!catalogCapable) {
        s_audioBindings = AudioBindingCache{};
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        s_audioBindings = AudioBindingCache{};
        return false;
    }

    PrefsReader reader(prefs);
    bool ok = audioBindingsRefresh(reader, catalogCapable, &s_audioBindings);
    prefs.end();
    return ok;
}

static const char* noneReasonToString(AudioPlaybackNoneReason reason) {
    switch (reason) {
        case AUDIO_PLAYBACK_NONE_UNKNOWN_SLOT: return "unknown slot";
        case AUDIO_PLAYBACK_NONE_TRACK_ZERO: return "track=0";
        case AUDIO_PLAYBACK_NONE_INVALID_BANKED: return "invalid banked tuple";
        case AUDIO_PLAYBACK_NONE_UNKNOWN_CATEGORY: return "unknown category";
        case AUDIO_PLAYBACK_NONE_CATEGORY_EMPTY: return "category empty";
        case AUDIO_PLAYBACK_NONE_ANTI_SPAM: return "anti-spam";
        case AUDIO_PLAYBACK_NONE_INTERVAL_NOT_READY: return "interval not ready";
        case AUDIO_PLAYBACK_NONE_INTERVAL_ZERO: return "interval zero";
        case AUDIO_PLAYBACK_NONE_DOME_SEQUENCE_ACTIVE: return "dome sequence active";
        case AUDIO_PLAYBACK_NONE_RANDOM_DISABLED: return "random disabled";
        case AUDIO_PLAYBACK_NONE_OK:
        default:
            return "none";
    }
}

static void applyAudioActiveFlags(const AudioPlaybackIntent& intent) {
    if (!intent.markAudioActive && !intent.clearAudioActive) {
        return;
    }
    taskENTER_CRITICAL(&robotStateMux);
    if (intent.clearAudioActive) {
        robotState.audioActive = false;
    }
    if (intent.markAudioActive) {
        robotState.audioActive = true;
    }
    taskEXIT_CRITICAL(&robotStateMux);
}

// Executes a resolved intent on the driver and applies its RobotState flags.
// State effects (randomMode, currentVol, cadence timestamps) are owned by the
// Audio Step Core and already applied by the phase that produced the intent.
static void executePlaybackIntent(const AudioPlaybackIntent& intent, CommandSource source) {
    switch (intent.kind) {
        case AUDIO_PLAYBACK_INTENT_PLAY_FLAT:
            if (intent.track == 0) {
                PA_LOG_WARN(TAG, "[%s] invalid PLAY_FLAT skipped (track=0)",
                            commandSourceToString(source));
                return;
            }
            driver->playTrack(intent.track);
            if (intent.requestKind == AUDIO_PLAYBACK_REQ_RANDOM_TICK) {
                PA_LOG_DEBUG(TAG, "random track %u%s", (unsigned)intent.track,
                             intent.flatFallbackUsed ? " (flat fallback)" : "");
            } else if (intent.slot != AUDIO_SLOT_NONE) {
                PA_LOG_INFO(TAG, "[%s] play slot=%u track=%u", commandSourceToString(source),
                            (unsigned)intent.slot, (unsigned)intent.track);
            } else if (intent.category != AUDIO_CATEGORY_NONE) {
                PA_LOG_INFO(TAG, "[%s] play category=%s track=%u", commandSourceToString(source),
                            audioCategoryToString(intent.category), (unsigned)intent.track);
            } else {
                if (source == SRC_SBUS) {
                    PA_LOG_DEBUG(TAG, "[%s] play track %u", commandSourceToString(source),
                                 (unsigned)intent.track);
                } else {
                    PA_LOG_INFO(TAG, "[%s] play track %u", commandSourceToString(source),
                                (unsigned)intent.track);
                }
            }
            break;

        case AUDIO_PLAYBACK_INTENT_PLAY_BANKED:
            if (intent.index == 0 || intent.bank == 0 || intent.page < 'A' || intent.page > 'Z') {
                PA_LOG_WARN(TAG, "[%s] invalid PLAY_BANKED skipped bank=%u page=%c index=%u",
                            commandSourceToString(source), (unsigned)intent.bank, intent.page,
                            (unsigned)intent.index);
                return;
            }
            driver->playTrackBanked(intent.index, intent.bank, intent.page);
            if (intent.slot != AUDIO_SLOT_NONE) {
                PA_LOG_INFO(TAG, "[%s] play slot=%u bank=%u page=%c index=%u",
                            commandSourceToString(source), (unsigned)intent.slot,
                            (unsigned)intent.bank, intent.page, (unsigned)intent.index);
            } else if (intent.category != AUDIO_CATEGORY_NONE) {
                // RANDOM_TICK = automatic background sound scheduler; log human-readable.
                // Other category plays (e.g. dome cue) keep the raw bank/page/index format.
                if (intent.requestKind == AUDIO_PLAYBACK_REQ_RANDOM_TICK) {
                    PA_LOG_INFO(TAG, "[%s] sound random %s -> bank=%u page=%c index=%u",
                                commandSourceToString(source), audioCategoryToString(intent.category),
                                (unsigned)intent.bank, intent.page, (unsigned)intent.index);
                } else {
                    PA_LOG_INFO(TAG, "[%s] play category=%s bank=%u page=%c index=%u",
                                commandSourceToString(source), audioCategoryToString(intent.category),
                                (unsigned)intent.bank, intent.page, (unsigned)intent.index);
                }
            } else {
                PA_LOG_INFO(TAG, "[%s] play bank=%u page=%c index=%u",
                            commandSourceToString(source), (unsigned)intent.bank, intent.page,
                            (unsigned)intent.index);
            }
            break;

        case AUDIO_PLAYBACK_INTENT_STOP:
            driver->stop();
            PA_LOG_INFO(TAG, "[%s] stop", commandSourceToString(source));
            break;

        case AUDIO_PLAYBACK_INTENT_TRACK_STOP:
            driver->stop();
            PA_LOG_INFO(TAG, "[%s] track stop", commandSourceToString(source));
            break;

        case AUDIO_PLAYBACK_INTENT_SET_VOLUME:
            driver->setVolume(intent.volume);
            PA_LOG_INFO(TAG, "[%s] volume %u", commandSourceToString(source),
                        (unsigned)intent.volume);
            break;

        case AUDIO_PLAYBACK_INTENT_RANDOM_ON:
            PA_LOG_INFO(TAG, "[%s] random mode on", commandSourceToString(source));
            break;

        case AUDIO_PLAYBACK_INTENT_RANDOM_OFF:
            PA_LOG_INFO(TAG, "[%s] random mode off", commandSourceToString(source));
            break;

        case AUDIO_PLAYBACK_INTENT_NONE:
        default:
            PA_LOG_DEBUG(TAG, "[%s] playback skipped (%s)", commandSourceToString(source),
                         noneReasonToString(intent.reason));
            break;
    }

    applyAudioActiveFlags(intent);
}

// One writer for the module-state RobotState block (Audio zone, ADR 0012).
static void writeModuleState(const AudioModuleState& ms, AudioRxStatus rxStatus) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.audio_module_link_ok = ms.linkOk;
    robotState.audio_module_play_state = ms.playState;
    robotState.audio_module_device = ms.device;
    robotState.audio_module_total_tracks = ms.totalTracks;
    robotState.audio_module_current_track = ms.currentTrack;
    robotState.audio_module_rx_status = rxStatus;
    taskEXIT_CRITICAL(&robotStateMux);
}

// Human-readable command names for the step core's ignore-reason logs.
static const char* playCommandName(AudioCommandType type) {
    switch (type) {
        case AUDIO_CMD_DOLLAR:            return "dollar command";
        case AUDIO_CMD_PLAY_TRACK:        return "play track";
        case AUDIO_CMD_PLAY_SLOT:         return "slot play";
        case AUDIO_CMD_PLAY_TRACK_BANKED: return "banked play";
        case AUDIO_CMD_PLAY_CATEGORY:     return "category play";
        case AUDIO_CMD_REFRESH_CATALOG:   return "catalog refresh";
        case AUDIO_CMD_REFRESH_BINDINGS:  return "binding cache refresh";
        default:                          return "command";
    }
}

// -----------------------------------------------------------------------------
// audioTask()
//
// Imperative adapter for the Audio Step Core (ADR 0014): gathers one
// generation of inputs per iteration, calls the step phases in loop order,
// and executes the returned plain-data actions on the driver, the dome-UART
// arbitration, and the RobotState audio zone.
//
// Feature toggle: task is only spawned when audio output is enabled at boot
// (staged at reboot per ADR 0027). When disabled at boot, this task does not run.
// -----------------------------------------------------------------------------
void audioTask(void* pvParameters) {
    (void)pvParameters;

    const bool audioEnabledAtBoot = configCacheReadActiveAudioEnabled();

    AudioStepState step{};
    AudioNamedTracks named{};
    AudioPlaybackConfig playback{};
    AudioCommand cmd{};
    bool hwmLogged = false;

    for (;;) {
        if (!hwmLogged) {
            PA_LOG_DEBUG("AudioTask", "stack HWM: %u bytes free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // ----------------------------------------------------------------
        // Gather this iteration's inputs. Config is read fresh so a Setup
        // page toggle (tracks, moods, volume) takes effect without a reboot;
        // S2 Sound toggle is staged at reboot (ADR 0027). Every step phase
        // sees one generation of inputs.
        // ----------------------------------------------------------------
        ConfigSnapshot cfg = {};
        configCacheRead(&cfg);
        audioConfigMapBuild(cfg, &playback);
        audioConfigMapNamedTracks(playback, &named);
        bool sleepMode;
        taskENTER_CRITICAL(&robotStateMux);
        sleepMode = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);
        const uint8_t caps = driver->capabilities();
        const bool catalogCapable = (caps & AudioDriver::AUDIO_CAP_CATALOG) != 0;

        AudioStepTickInputs tickIn{};
        tickIn.audioEnabled = audioEnabledAtBoot;
        tickIn.sleepMode = sleepMode;
        tickIn.configVolume = cfg.audio.audioVolume;
        const AudioStepTickActions tick = audioStepTick(step, tickIn);

        if (tick.stopDriver) {
            driver->stop();
            if (tick.stopReason == AUDIO_STEP_STOP_DISABLED) {
                PA_LOG_INFO(TAG, "audio disabled - stopping active playback");
            }
        }
        if (tick.stopReason == AUDIO_STEP_STOP_SLEEP_ENTRY) {
            PA_LOG_INFO(TAG, "sleep mode active - audio playback suppressed");
        }
        if (tick.clearAudioActive) {
            taskENTER_CRITICAL(&robotStateMux);
            robotState.audioActive = false;
            taskEXIT_CRITICAL(&robotStateMux);
            if (tick.drainQueue) {
                PA_LOG_INFO(TAG, "audio disabled - random mode cleared");
            }
        }
        if (tick.drainQueue) {
            xQueueReceive(audioCmdQueue, &cmd, pdMS_TO_TICKS(500));
            continue;
        }

        if (tick.initDriver) {
            // Soft-UART drivers block for up to ~6 ms per command; AudioTask must run on Core 0.
            configASSERT(xPortGetCoreID() == 0);
            const bool initOk = driver->begin(step.currentVol);
            const AudioStepInitResultActions ir =
                audioStepInitResult(step, initOk, catalogCapable);
            if (ir.giveUp) {
                PA_LOG_WARN(TAG,
                            "audio driver begin() failed %u times - giving up; driver inoperative",
                            (unsigned)AUDIO_STEP_INIT_MAX_RETRIES);
                setAudioRxStatus(AUDIO_RX_NO_RESPONSE);
            }
            if (ir.skipRestOfTick) {
                if (!ir.giveUp) {
                    PA_LOG_DEBUG(TAG, "audio driver begin incomplete (%u/%u) - will retry",
                                 (unsigned)step.beginRetryCount,
                                 (unsigned)AUDIO_STEP_INIT_MAX_RETRIES);
                }
                vTaskDelay(pdMS_TO_TICKS(AUDIO_STEP_INIT_RETRY_DELAY_MS));
                continue;
            }
            if (ir.refreshBindings) {
                bool cacheLoaded = refreshChirpBindingCacheFromNvs();
                PA_LOG_INFO(TAG, "CHIRP binding cache %s", cacheLoaded ? "loaded" : "load failed");
            }
            PA_LOG_INFO(TAG, "audio driver init - PA_AUDIO_DRIVER=%d vol=%u", PA_AUDIO_DRIVER,
                        (unsigned)step.currentVol);
            if (ir.seedModuleState) {
                // Seed RobotState from getCachedState()  --  begin() runs pre-init
                // queries so m_device and m_totalTracks may already be populated
                // (non-0xFF/0) if the module responded.
                AudioModuleState ms{};
                driver->getCachedState(ms);
                writeModuleState(ms, driver->classifyRxStatus(ms.linkOk));
                PA_LOG_INFO(TAG, "module init cached: link=%s device=0x%02X tracks=%u",
                            ms.linkOk ? "OK" : "NO_DEVICE", (unsigned)ms.device,
                            (unsigned)ms.totalTracks);
            }
        }

        // ----------------------------------------------------------------
        // Process one command from the queue (500 ms timeout so the idle
        // phase below can run even when the queue is quiet).
        // ----------------------------------------------------------------
        if (xQueueReceive(audioCmdQueue, &cmd, pdMS_TO_TICKS(500)) == pdTRUE) {
            AudioStepCommandInputs cmdIn{};
            cmdIn.nowMs = millis();
            cmdIn.sleepMode = sleepMode;
            cmdIn.catalogCapable = catalogCapable;
            cmdIn.playback = &playback;
            cmdIn.named = &named;
            cmdIn.bindings = &s_audioBindings;
            cmdIn.randomValue = esp_random();
            const AudioStepCommandActions ca = audioStepCommand(step, cmdIn, cmd);

            if (ca.ignored == AUDIO_STEP_IGNORE_SLEEP) {
                PA_LOG_INFO(TAG, "[%s] %s ignored - sleep mode active",
                            commandSourceToString(cmd.source), playCommandName(cmd.type));
            } else if (ca.ignored == AUDIO_STEP_IGNORE_UNSUPPORTED_BACKEND) {
                PA_LOG_DEBUG(TAG, "[%s] %s ignored (unsupported backend)",
                             commandSourceToString(cmd.source), playCommandName(cmd.type));
            }
            if (ca.hasIntent) {
                executePlaybackIntent(ca.intent, cmd.source);
            }
            if (ca.refreshCatalog) {
                bool acquired = domeUartAcquire(DOME_UART_AUDIO);
                bool ok = acquired && driver->refreshCatalog();
                if (acquired) {
                    domeUartRelease(DOME_UART_AUDIO);
                    setAudioRxStatus(ok ? AUDIO_RX_AVAILABLE : AUDIO_RX_NO_RESPONSE);
                } else {
                    setAudioRxStatus(AUDIO_RX_BLOCKED_BY_DOME_UART);
                }
                PA_LOG_INFO(TAG, "[%s] catalog refresh %s", commandSourceToString(cmd.source),
                            ok ? "OK" : (acquired ? "FAILED" : "skipped: DomeLink using UART"));
            }
            if (ca.refreshBindings) {
                bool ok = refreshChirpBindingCacheFromNvs();
                PA_LOG_INFO(TAG, "[%s] binding cache refresh %s",
                            commandSourceToString(cmd.source), ok ? "OK" : "FAILED");
            }
            if (ca.queryStatus) {
                // On-demand query triggered by the web UI poll button. The 3-query
                // sequence takes up to ~900 ms; acceptable because it is
                // user-initiated and not in a real-time loop.
                AudioModuleState ms{};
                bool acquired = domeUartAcquire(DOME_UART_AUDIO);
                if (acquired) {
                    bool ok = driver->queryModuleState(ms);
                    domeUartRelease(DOME_UART_AUDIO);
                    writeModuleState(ms, ok ? AUDIO_RX_AVAILABLE : AUDIO_RX_NO_RESPONSE);
                    PA_LOG_INFO(TAG, "[%s] status poll: link=%s device=0x%02X play=0x%02X",
                                commandSourceToString(cmd.source), ok ? "OK" : "NO_RSP",
                                (unsigned)ms.device, (unsigned)ms.playState);
                } else {
                    setAudioRxStatus(AUDIO_RX_BLOCKED_BY_DOME_UART);
                    PA_LOG_INFO(TAG, "[%s] status poll skipped (UART2 owned by dome)",
                                commandSourceToString(cmd.source));
                }
            }
        }

        // ----------------------------------------------------------------
        // Idle phase: random playback tick + periodic status auto-query.
        // Random interval semantics are unchanged (per-mood, NVS-configurable;
        // mood 0 falls back to the Full-Awake interval, 0 s suppresses).
        // Background polling can corrupt some module RX state machines, so
        // the auto-query still runs only for AUDIO_CAP_QUERY_SAFE_PLAYING.
        // ----------------------------------------------------------------
        uint8_t activeMood;
        bool domeSeqActive;
        taskENTER_CRITICAL(&robotStateMux);
        activeMood = robotState.activeMood;
        domeSeqActive = robotState.domeSeqActive;
        taskEXIT_CRITICAL(&robotStateMux);

        AudioStepIdleInputs idleIn{};
        idleIn.nowMs = millis();
        idleIn.sleepMode = sleepMode;
        idleIn.catalogCapable = catalogCapable;
        idleIn.querySafePlayingCapable = (caps & AudioDriver::AUDIO_CAP_QUERY_SAFE_PLAYING) != 0;
        idleIn.webOtaActive = webOtaActive();
        idleIn.activeMood = activeMood;
        idleIn.domeSeqActive = domeSeqActive;
        idleIn.randomValue = esp_random();
        idleIn.playback = &playback;
        idleIn.bindings = &s_audioBindings;
        const AudioStepIdleActions idle = audioStepIdle(step, idleIn);

        if (idle.hasIntent) {
            executePlaybackIntent(idle.intent, SRC_INTERNAL);
        }
        if (idle.autoQuery) {
            AudioModuleState ms{};
            bool acquired = domeUartAcquire(DOME_UART_AUDIO);
            if (acquired) {
                bool ok = driver->queryModuleState(ms);
                domeUartRelease(DOME_UART_AUDIO);
                writeModuleState(ms, ok ? AUDIO_RX_AVAILABLE : AUDIO_RX_NO_RESPONSE);
                PA_LOG_DEBUG(TAG, "auto-query: link=%s play=0x%02X", ok ? "OK" : "no-rsp",
                             (unsigned)ms.playState);
            } else {
                setAudioRxStatus(AUDIO_RX_BLOCKED_BY_DOME_UART);
                PA_LOG_DEBUG(TAG, "auto-query skipped (UART2 owned by dome)");
            }
        }
    }
}

