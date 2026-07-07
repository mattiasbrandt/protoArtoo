// =============================================================================
// src/tasks/audio_task.cpp
//
// AudioTask — sole writer to the audio serial GPIO (PIN_AUDIO_TX, GPIO 26).
//
// Responsibilities:
//   - Initialise the compiled-in AudioDriver backend on boot.
//   - Drain audioCmdQueue and dispatch each command to the driver.
//   - Parse '$' command strings via parseAudioDollar().
//   - Manage random playback mode timer.
//   - Track and clamp current volume (0–30).
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
#include "config.h"
#include "config_nvsio.h"
#include "config_store.h"
#include "dome_link.h"
#include "logging.h"
#include "robot_state.h"
#include "web_server.h"

// -----------------------------------------------------------------------------
// Driver instantiation — one concrete driver per build
// -----------------------------------------------------------------------------
#if PA_AUDIO_DRIVER == AUDIO_SOFT_UART
#include "audio_dy_sv5w.h"
static AudioDriverDySv5w s_driver;
#elif PA_AUDIO_DRIVER == AUDIO_CHIRP
#include "audio_chirp.h"
static AudioDriverChirp s_driver;
#elif PA_AUDIO_DRIVER == AUDIO_DFPLAYER
#error "AUDIO_DFPLAYER driver not yet implemented — see T15 / T16"
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

// -----------------------------------------------------------------------------
// Queue-send helpers (non-blocking, timeout 0)
// -----------------------------------------------------------------------------

bool audioQueueDollar(const char* cmd, CommandSource src) {
    if (!cmd || cmd[0] != '$') {
        return false;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_DOLLAR;
    msg.source = src;
    strncpy(msg.dollar, cmd, sizeof(msg.dollar) - 1);
    msg.dollar[sizeof(msg.dollar) - 1] = '\0';
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueuePlayTrack(uint16_t track, CommandSource src) {
    if (track == 0) {
        return false;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_PLAY_TRACK;
    msg.source = src;
    msg.track = track;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueuePlayTrackBanked(uint16_t index, uint8_t bank, char page, CommandSource src) {
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
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueuePlaySlot(AudioPlaybackSlot slot, CommandSource src) {
    if (slot == AUDIO_SLOT_NONE) {
        return false;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_PLAY_SLOT;
    msg.source = src;
    msg.slot = slot;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueuePlayCategory(AudioPlaybackCategory category, AudioPlaybackSlot fallbackSlot,
                            CommandSource src) {
    if (!audioPlaybackIsValidCategory(category)) {
        return false;
    }
    AudioCommand msg{};
    msg.type = AUDIO_CMD_PLAY_CATEGORY;
    msg.source = src;
    msg.category.category = category;
    msg.category.fallbackSlot = fallbackSlot;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueStop(CommandSource src) {
    AudioCommand msg{};
    msg.type = AUDIO_CMD_STOP;
    msg.source = src;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueTrackStop(CommandSource src) {
    AudioCommand msg{};
    msg.type = AUDIO_CMD_TRACK_STOP;
    msg.source = src;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueSetVolume(uint8_t vol, CommandSource src) {
    AudioCommand msg{};
    msg.type = AUDIO_CMD_SET_VOLUME;
    msg.source = src;
    msg.volume = audioClampVolume(vol);
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueQueryStatus(CommandSource src) {
    AudioCommand msg{};
    msg.type = AUDIO_CMD_QUERY_STATUS;
    msg.source = src;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueRefreshCatalog(CommandSource src) {
    AudioCommand msg{};
    msg.type = AUDIO_CMD_REFRESH_CATALOG;
    msg.source = src;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        return false;
    }
    return true;
}

bool audioQueueRefreshBindings(CommandSource src) {
    AudioCommand msg{};
    msg.type = AUDIO_CMD_REFRESH_BINDINGS;
    msg.source = src;
    if (xQueueSend(audioCmdQueue, &msg, 0) != pdTRUE) {
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
static constexpr uint32_t AUTO_STATUS_QUERY_INTERVAL_MS = 10000;  // 10 s
static AudioBindingCache s_audioBindings = {};

static void readPlaybackConfig(AudioPlaybackConfig* playback, AudioNamedTracks* named = nullptr) {
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    audioConfigMapBuild(cfg, playback);
    if (playback != nullptr && named != nullptr) {
        audioConfigMapNamedTracks(*playback, named);
    }
}

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

static void executePlaybackIntent(const AudioPlaybackIntent& intent, CommandSource source,
                                  uint32_t now, uint32_t& lastPlayMs, uint32_t& lastRandMs,
                                  uint8_t& currentVol, bool& randomMode) {
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
            randomMode = false;
            PA_LOG_INFO(TAG, "[%s] stop", commandSourceToString(source));
            break;

        case AUDIO_PLAYBACK_INTENT_TRACK_STOP:
            driver->stop();
            // randomMode intentionally untouched — Track Stop preserves idle mood (ADR 0010)
            PA_LOG_INFO(TAG, "[%s] track stop", commandSourceToString(source));
            break;

        case AUDIO_PLAYBACK_INTENT_SET_VOLUME:
            currentVol = intent.volume;
            driver->setVolume(currentVol);
            PA_LOG_INFO(TAG, "[%s] volume %u", commandSourceToString(source), (unsigned)currentVol);
            break;

        case AUDIO_PLAYBACK_INTENT_RANDOM_ON:
            if (!randomMode) {
                lastRandMs = now;
            }
            randomMode = true;
            PA_LOG_INFO(TAG, "[%s] random mode on", commandSourceToString(source));
            break;

        case AUDIO_PLAYBACK_INTENT_RANDOM_OFF:
            randomMode = false;
            PA_LOG_INFO(TAG, "[%s] random mode off", commandSourceToString(source));
            break;

        case AUDIO_PLAYBACK_INTENT_NONE:
        default:
            PA_LOG_DEBUG(TAG, "[%s] playback skipped (%s)", commandSourceToString(source),
                         noneReasonToString(intent.reason));
            break;
    }

    if (intent.updateLastPlayMs) {
        lastPlayMs = now;
    }
    if (intent.updateLastRandMs) {
        lastRandMs = now;
    }
    applyAudioActiveFlags(intent);
}

// -----------------------------------------------------------------------------
// audioTask()
//
// Unified single-loop design — audioEnabled is read at the top of every
// iteration so the task responds immediately to enable/disable changes from
// the Setup page without requiring a reboot.
//
// Disabled state: drains the queue silently, clears randomMode, never touches
// the audio GPIO. The driver is re-initialized on the first iteration after
// being enabled.
// -----------------------------------------------------------------------------
void audioTask(void* pvParameters) {
    (void)pvParameters;

    bool driverInitialized = false;  // becomes true on first enable or after retry ceiling hit
    uint8_t beginRetryCount = 0;
    static constexpr uint8_t kBeginMaxRetries = 20;  // ~5 s at 250 ms/retry
    bool lastAudioEnabled = true;   // tracks previous iteration's enabled state for stop-on-disable
    bool randomMode = false;
    uint32_t lastRandMs = 0;
    uint32_t lastPlayMs = 0;  // anti-spam: last playTrack() timestamp
    uint32_t lastAutoQueryMs = 0;  // periodic status poll timer for safe-polling modules
    uint8_t currentVol = 20;  // updated from config on first enable
    bool wasSleeping = false;

    // Minimum interval between successive playTrack() calls (ms).
    // Application-level anti-bounce guard for rapid-fire play commands
    // (e.g. UI double-tap, RC switch bounce). Driver-level timing
    // constraints, if any, are enforced inside the driver's
    // playTrack() implementation.
    // The constant is owned by audio_playback_policy.h so command-driven play
    // requests share one anti-spam rule.

    // Named tracks populated from NVS-backed robotState fields.
    // Updated at runtime via POST /api/audio/tracks.
    // Refreshed on each enable cycle so changes take effect without reboot.
    AudioNamedTracks named{};

    AudioCommand cmd{};
    bool hwmLogged = false;

    for (;;) {
        if (!hwmLogged) {
            PA_LOG_DEBUG("AudioTask", "stack HWM: %u words free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // ----------------------------------------------------------------
        // Read enabled state fresh every iteration.
        // The user can toggle S2 Sound in the Setup page at any time;
        // we must not require a reboot for the change to take effect.
        // ----------------------------------------------------------------
        bool audioEnabled;
        bool sleepMode;
        ConfigSnapshot cfg = {};
        configCacheRead(&cfg);
        audioEnabled = cfg.system.enable_s2_sound;
        taskENTER_CRITICAL(&robotStateMux);
        sleepMode = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);

        if (!audioEnabled) {
            // Disabled: stop active playback on the enabled→disabled transition,
            // then drain queued commands silently and clear runtime state.
            if (lastAudioEnabled && driverInitialized) {
                driver->stop();
                PA_LOG_INFO(TAG, "audio disabled — stopping active playback");
            }
            lastAudioEnabled = false;
            // randomMode must be cleared here — if it stays true, the random
            // timer would fire the moment audio is re-enabled.
            if (randomMode) {
                randomMode = false;
                taskENTER_CRITICAL(&robotStateMux);
                robotState.audioActive = false;
                taskEXIT_CRITICAL(&robotStateMux);
                PA_LOG_INFO(TAG, "audio disabled — random mode cleared");
            }
            xQueueReceive(audioCmdQueue, &cmd, pdMS_TO_TICKS(500));
            wasSleeping = sleepMode;
            continue;
        }
        lastAudioEnabled = true;

        // ----------------------------------------------------------------
        // Enabled: initialise driver on first enable. If a driver reports a
        // transient init failure, retry through a delay instead of spinning.
        // ----------------------------------------------------------------
        if (!driverInitialized) {
            ConfigSnapshot cfg = {};
            configCacheRead(&cfg);
            currentVol = cfg.audio.audioVolume;
            AudioPlaybackConfig playback = {};
            audioConfigMapBuild(cfg, &playback);
            audioConfigMapNamedTracks(playback, &named);
            // Soft-UART drivers block for up to ~6 ms per command; AudioTask must run on Core 0.
            configASSERT(xPortGetCoreID() == 0);
            const bool initOk = driver->begin(currentVol);
            if (!initOk) {
                ++beginRetryCount;
                if (beginRetryCount >= kBeginMaxRetries) {
                    PA_LOG_WARN(TAG,
                                "audio driver begin() failed %u times — giving up; driver inoperative",
                                (unsigned)beginRetryCount);
                    setAudioRxStatus(AUDIO_RX_NO_RESPONSE);
                    driverInitialized = true;
                    beginRetryCount = 0;
                } else {
                    PA_LOG_DEBUG(TAG, "audio driver begin incomplete (%u/%u) — will retry",
                                 (unsigned)beginRetryCount, (unsigned)kBeginMaxRetries);
                }
                vTaskDelay(pdMS_TO_TICKS(250));
                continue;
            }
            beginRetryCount = 0;
            if (driver->capabilities() & AudioDriver::AUDIO_CAP_CATALOG) {
                bool cacheLoaded = refreshChirpBindingCacheFromNvs();
                PA_LOG_INFO(TAG, "CHIRP binding cache %s", cacheLoaded ? "loaded" : "load failed");
            }
            driverInitialized = true;
            PA_LOG_INFO(TAG, "audio driver init — PA_AUDIO_DRIVER=%d vol=%u", PA_AUDIO_DRIVER,
                        (unsigned)currentVol);

            // Seed RobotState from getCachedState() — begin() now runs
            // pre-init queries so m_device and m_totalTracks may already
            // be populated (non-0xFF/0) if the module responded.
            {
                AudioModuleState ms{};
                driver->getCachedState(ms);
                AudioRxStatus rxStatus = driver->classifyRxStatus(ms.linkOk);
                taskENTER_CRITICAL(&robotStateMux);
                robotState.audio_module_link_ok = ms.linkOk;
                robotState.audio_module_play_state = ms.playState;
                robotState.audio_module_device = ms.device;
                robotState.audio_module_total_tracks = ms.totalTracks;
                robotState.audio_module_current_track = ms.currentTrack;
                robotState.audio_module_rx_status = rxStatus;
                taskEXIT_CRITICAL(&robotStateMux);
                PA_LOG_INFO(TAG, "module init cached: link=%s device=0x%02X tracks=%u",
                            ms.linkOk ? "OK" : "NO_DEVICE", (unsigned)ms.device,
                            (unsigned)ms.totalTracks);
            }
        }

        if (sleepMode && !wasSleeping) {
            if (driverInitialized) {
                driver->stop();
            }
            randomMode = false;
            taskENTER_CRITICAL(&robotStateMux);
            robotState.audioActive = false;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_INFO(TAG, "sleep mode active — audio playback suppressed");
        }
        wasSleeping = sleepMode;

        // ----------------------------------------------------------------
        // Process one command from the queue (500 ms timeout so the random
        // timer below can fire even when the queue is idle).
        // ----------------------------------------------------------------
        if (xQueueReceive(audioCmdQueue, &cmd, pdMS_TO_TICKS(500)) == pdTRUE) {
            switch (cmd.type) {
                case AUDIO_CMD_DOLLAR: {
                    if (sleepMode) {
                        PA_LOG_INFO(TAG, "[%s] dollar command ignored — sleep mode active",
                                    commandSourceToString(cmd.source));
                        break;
                    }
                    AudioPlaybackConfig playback = {};
                    readPlaybackConfig(&playback, &named);
                    AudioAction action = parseAudioDollar(cmd.dollar, named);
                    AudioPlaybackRequest request{};
                    if (action.type == AUDIO_ACTION_PLAY_TRACK) {
                        AudioPlaybackSlot slot = audioSlotForDollar(cmd.dollar);
                        if (slot != AUDIO_SLOT_NONE) {
                            request.kind = AUDIO_PLAYBACK_REQ_SLOT;
                            request.slot = slot;
                        } else {
                            request.kind = AUDIO_PLAYBACK_REQ_DIRECT_TRACK;
                            request.track = action.track;
                        }
                    } else if (action.type == AUDIO_ACTION_STOP) {
                        request.kind = AUDIO_PLAYBACK_REQ_STOP;
                    } else if (action.type == AUDIO_ACTION_RANDOM_ON) {
                        request.kind = AUDIO_PLAYBACK_REQ_RANDOM_ON;
                    } else if (action.type == AUDIO_ACTION_RANDOM_OFF) {
                        request.kind = AUDIO_PLAYBACK_REQ_RANDOM_OFF;
                    } else if (action.type == AUDIO_ACTION_VOLUME_SET) {
                        request.kind = AUDIO_PLAYBACK_REQ_SET_VOLUME;
                        request.volume = action.volume;
                    } else if (action.type == AUDIO_ACTION_VOLUME_UP) {
                        request.kind = AUDIO_PLAYBACK_REQ_SET_VOLUME;
                        request.volume = audioClampVolume(currentVol + 1);
                    } else if (action.type == AUDIO_ACTION_VOLUME_DOWN) {
                        request.kind = AUDIO_PLAYBACK_REQ_SET_VOLUME;
                        request.volume = (currentVol > AUDIO_VOLUME_MIN) ? (uint8_t)(currentVol - 1)
                                                                          : AUDIO_VOLUME_MIN;
                    } else {
                        request.kind = AUDIO_PLAYBACK_REQ_NONE;
                    }
                    uint32_t now = millis();
                    AudioPlaybackContext context{&playback, &s_audioBindings,
                                                 (driver->capabilities() &
                                                  AudioDriver::AUDIO_CAP_CATALOG) != 0,
                                                 now, lastPlayMs};
                    AudioPlaybackIntent intent = audioPlaybackResolveRequest(context, request);
                    executePlaybackIntent(intent, cmd.source, now, lastPlayMs, lastRandMs,
                                          currentVol, randomMode);
                    break;
                }

                case AUDIO_CMD_PLAY_TRACK: {
                    if (sleepMode) {
                        PA_LOG_INFO(TAG, "[%s] play track ignored — sleep mode active",
                                    commandSourceToString(cmd.source));
                        break;
                    }
                    AudioPlaybackConfig playback = {};
                    readPlaybackConfig(&playback);
                    AudioPlaybackRequest request{};
                    request.kind = AUDIO_PLAYBACK_REQ_DIRECT_TRACK;
                    request.track = cmd.track;
                    uint32_t now = millis();
                    AudioPlaybackContext context{&playback, &s_audioBindings,
                                                 (driver->capabilities() &
                                                  AudioDriver::AUDIO_CAP_CATALOG) != 0,
                                                 now, lastPlayMs};
                    AudioPlaybackIntent intent = audioPlaybackResolveRequest(context, request);
                    executePlaybackIntent(intent, cmd.source, now, lastPlayMs, lastRandMs,
                                          currentVol, randomMode);
                    break;
                }
                case AUDIO_CMD_PLAY_SLOT: {
                    if (sleepMode) {
                        PA_LOG_INFO(TAG, "[%s] slot play ignored — sleep mode active",
                                    commandSourceToString(cmd.source));
                        break;
                    }
                    AudioPlaybackConfig playback = {};
                    readPlaybackConfig(&playback);
                    AudioPlaybackRequest request{};
                    request.kind = AUDIO_PLAYBACK_REQ_SLOT;
                    request.slot = cmd.slot;
                    uint32_t now = millis();
                    AudioPlaybackContext context{&playback, &s_audioBindings,
                                                 (driver->capabilities() &
                                                  AudioDriver::AUDIO_CAP_CATALOG) != 0,
                                                 now, lastPlayMs};
                    AudioPlaybackIntent intent = audioPlaybackResolveRequest(context, request);
                    executePlaybackIntent(intent, cmd.source, now, lastPlayMs, lastRandMs,
                                          currentVol, randomMode);
                    break;
                }

                case AUDIO_CMD_PLAY_TRACK_BANKED: {
                    if (sleepMode) {
                        PA_LOG_INFO(TAG, "[%s] banked play ignored — sleep mode active",
                                    commandSourceToString(cmd.source));
                        break;
                    }
                    AudioPlaybackConfig playback = {};
                    readPlaybackConfig(&playback);
                    AudioPlaybackRequest request{};
                    request.kind = AUDIO_PLAYBACK_REQ_DIRECT_BANKED;
                    request.banked.index = cmd.banked.index;
                    request.banked.bank = cmd.banked.bank;
                    request.banked.page = cmd.banked.page;
                    uint32_t now = millis();
                    AudioPlaybackContext context{&playback, &s_audioBindings,
                                                 (driver->capabilities() &
                                                  AudioDriver::AUDIO_CAP_CATALOG) != 0,
                                                 now, lastPlayMs};
                    AudioPlaybackIntent intent = audioPlaybackResolveRequest(context, request);
                    executePlaybackIntent(intent, cmd.source, now, lastPlayMs, lastRandMs,
                                          currentVol, randomMode);
                    break;
                }

                case AUDIO_CMD_PLAY_CATEGORY: {
                    if (sleepMode) {
                        PA_LOG_INFO(TAG, "[%s] category play ignored — sleep mode active",
                                    commandSourceToString(cmd.source));
                        break;
                    }
                    AudioPlaybackConfig playback = {};
                    readPlaybackConfig(&playback);
                    AudioPlaybackRequest request{};
                    request.kind = AUDIO_PLAYBACK_REQ_CATEGORY;
                    request.categoryRequest.category = cmd.category.category;
                    request.categoryRequest.fallbackSlot = cmd.category.fallbackSlot;
                    request.categoryRequest.randomValue = esp_random();
                    uint32_t now = millis();
                    AudioPlaybackContext context{&playback, &s_audioBindings,
                                                 (driver->capabilities() &
                                                  AudioDriver::AUDIO_CAP_CATALOG) != 0,
                                                 now, lastPlayMs};
                    AudioPlaybackIntent intent = audioPlaybackResolveRequest(context, request);
                    executePlaybackIntent(intent, cmd.source, now, lastPlayMs, lastRandMs,
                                          currentVol, randomMode);
                    break;
                }

                case AUDIO_CMD_STOP: {
                    AudioPlaybackRequest request{};
                    request.kind = AUDIO_PLAYBACK_REQ_STOP;
                    uint32_t now = millis();
                    AudioPlaybackContext context{nullptr, nullptr, false, now, lastPlayMs};
                    AudioPlaybackIntent intent = audioPlaybackResolveRequest(context, request);
                    executePlaybackIntent(intent, cmd.source, now, lastPlayMs, lastRandMs,
                                          currentVol, randomMode);
                    break;
                }

                case AUDIO_CMD_TRACK_STOP: {
                    AudioPlaybackRequest request{};
                    request.kind = AUDIO_PLAYBACK_REQ_TRACK_STOP;
                    uint32_t now = millis();
                    AudioPlaybackContext context{nullptr, nullptr, false, now, lastPlayMs};
                    AudioPlaybackIntent intent = audioPlaybackResolveRequest(context, request);
                    executePlaybackIntent(intent, cmd.source, now, lastPlayMs, lastRandMs,
                                          currentVol, randomMode);
                    break;
                }

                case AUDIO_CMD_SET_VOLUME: {
                    AudioPlaybackRequest request{};
                    request.kind = AUDIO_PLAYBACK_REQ_SET_VOLUME;
                    request.volume = cmd.volume;  // clamped by helper before enqueue
                    uint32_t now = millis();
                    AudioPlaybackContext context{nullptr, nullptr, false, now, lastPlayMs};
                    AudioPlaybackIntent intent = audioPlaybackResolveRequest(context, request);
                    executePlaybackIntent(intent, cmd.source, now, lastPlayMs, lastRandMs,
                                          currentVol, randomMode);
                    break;
                }

                case AUDIO_CMD_REFRESH_CATALOG: {
                    if (!(driver->capabilities() & AudioDriver::AUDIO_CAP_CATALOG)) {
                        PA_LOG_DEBUG(TAG, "[%s] catalog refresh ignored (unsupported backend)",
                                     commandSourceToString(cmd.source));
                        break;
                    }
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
                    break;
                }

                case AUDIO_CMD_REFRESH_BINDINGS: {
                    if (!(driver->capabilities() & AudioDriver::AUDIO_CAP_CATALOG)) {
                        PA_LOG_DEBUG(TAG, "[%s] binding cache refresh ignored (unsupported backend)",
                                     commandSourceToString(cmd.source));
                        break;
                    }
                    bool ok = refreshChirpBindingCacheFromNvs();
                    PA_LOG_INFO(TAG, "[%s] binding cache refresh %s",
                                commandSourceToString(cmd.source), ok ? "OK" : "FAILED");
                    break;
                }

                case AUDIO_CMD_QUERY_STATUS: {
                    // On-demand query triggered by the web UI poll button.
                    // Runs only when explicitly requested — never automatically.
                    // The 3-query sequence takes up to ~900 ms; this is acceptable
                    // since it is user-initiated and not in a real-time loop.
                    AudioModuleState ms{};
                    bool acquired = domeUartAcquire(DOME_UART_AUDIO);
                    if (!acquired) {
                        setAudioRxStatus(AUDIO_RX_BLOCKED_BY_DOME_UART);
                        PA_LOG_INFO(TAG, "[%s] status poll skipped (UART2 owned by dome)",
                                    commandSourceToString(cmd.source));
                        break;
                    }
                    bool ok = driver->queryModuleState(ms);
                    domeUartRelease(DOME_UART_AUDIO);
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.audio_module_link_ok = ms.linkOk;
                    robotState.audio_module_play_state = ms.playState;
                    robotState.audio_module_device = ms.device;
                    robotState.audio_module_total_tracks = ms.totalTracks;
                    robotState.audio_module_current_track = ms.currentTrack;
                    robotState.audio_module_rx_status = ok ? AUDIO_RX_AVAILABLE : AUDIO_RX_NO_RESPONSE;
                    taskEXIT_CRITICAL(&robotStateMux);
                    PA_LOG_INFO(TAG, "[%s] status poll: link=%s device=0x%02X play=0x%02X",
                                commandSourceToString(cmd.source), ok ? "OK" : "NO_RSP",
                                (unsigned)ms.device, (unsigned)ms.playState);
                    break;
                }
            }
        }

        // ----------------------------------------------------------------
        // Random playback timer.
        // randomMode is only true when audio is enabled and a $R command
        // was received. audioEnabled is guaranteed true here (we continued
        // at the top of the loop if it was false).
        //
        // Interval is per-mood and NVS-configurable (T18). AudioTask reads
        // activeMood + cfg_snd_int_* on every timer check, so a mood change
        // takes effect on the next tick after the current interval expires.
        // Mood 0 (unset) falls back to the Full-Awake interval.
        // An interval of 0 s suppresses random playback for that mood.
        // ----------------------------------------------------------------
        if (randomMode && !sleepMode) {
            AudioPlaybackConfig playback = {};
            readPlaybackConfig(&playback);
            taskENTER_CRITICAL(&robotStateMux);
            uint8_t mood = robotState.activeMood;
            bool domeSeqActive = robotState.domeSeqActive;
            taskEXIT_CRITICAL(&robotStateMux);
            uint32_t now = millis();
            AudioPlaybackRandomContext context{&playback,
                                               &s_audioBindings,
                                               (driver->capabilities() &
                                                AudioDriver::AUDIO_CAP_CATALOG) != 0,
                                               randomMode,
                                               domeSeqActive,
                                               now,
                                               lastRandMs,
                                               mood,
                                               esp_random()};
            AudioPlaybackIntent intent = audioPlaybackResolveRandomTick(context);
            executePlaybackIntent(intent, SRC_INTERNAL, now, lastPlayMs, lastRandMs, currentVol,
                                  randomMode);
        }

        // ----------------------------------------------------------------
        // Periodic auto-query runs only for modules reporting
        // AUDIO_CAP_QUERY_SAFE_PLAYING at AUTO_STATUS_QUERY_INTERVAL_MS
        // cadence. For modules without that capability (background polling
        // can corrupt some module RX state machines during playback),
        // status is updated only via the manual Poll button
        // ----------------------------------------------------------------
        if (!webOtaActive() &&
            (driver->capabilities() & AudioDriver::AUDIO_CAP_QUERY_SAFE_PLAYING) &&
            ((uint32_t)(millis() - lastAutoQueryMs) >= AUTO_STATUS_QUERY_INTERVAL_MS)) {
            lastAutoQueryMs = millis();
            AudioModuleState ms{};
            bool acquired = domeUartAcquire(DOME_UART_AUDIO);
            if (!acquired) {
                setAudioRxStatus(AUDIO_RX_BLOCKED_BY_DOME_UART);
                PA_LOG_DEBUG(TAG, "auto-query skipped (UART2 owned by dome)");
                continue;
            }
            bool ok = driver->queryModuleState(ms);
            domeUartRelease(DOME_UART_AUDIO);
            taskENTER_CRITICAL(&robotStateMux);
            robotState.audio_module_link_ok = ms.linkOk;
            robotState.audio_module_play_state = ms.playState;
            robotState.audio_module_device = ms.device;
            robotState.audio_module_total_tracks = ms.totalTracks;
            robotState.audio_module_current_track = ms.currentTrack;
            robotState.audio_module_rx_status = ok ? AUDIO_RX_AVAILABLE : AUDIO_RX_NO_RESPONSE;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_DEBUG(TAG, "auto-query: link=%s play=0x%02X", ok ? "OK" : "no-rsp",
                         (unsigned)ms.playState);
        }
    }
}
