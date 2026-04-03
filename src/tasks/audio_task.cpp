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
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

#include "audio_dollar_parser.h"
#include "audio_driver.h"
#include "logging.h"
#include "robot_state.h"

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

bool audioQueueSetVolume(uint8_t vol, CommandSource src) {
    AudioCommand msg{};
    msg.type = AUDIO_CMD_SET_VOLUME;
    msg.source = src;
    msg.volume = (vol > AUDIO_VOLUME_MAX) ? AUDIO_VOLUME_MAX : vol;
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

// -----------------------------------------------------------------------------
// dispatchAction()
// Apply a parsed AudioAction to the driver; update volume and randomMode state.
// -----------------------------------------------------------------------------
static constexpr uint32_t DISPATCH_PLAY_MIN_MS = 300;

static void dispatchAction(const AudioAction& action, uint8_t& vol, bool& randomMode,
                           uint32_t& lastPlayMs) {
    switch (action.type) {
        case AUDIO_ACTION_PLAY_TRACK: {
            uint32_t now = millis();
            if ((uint32_t)(now - lastPlayMs) < DISPATCH_PLAY_MIN_MS) {
                PA_LOG_DEBUG(TAG, "play track %u dropped (anti-spam %lu ms)",
                             (unsigned)action.track, (unsigned long)(now - lastPlayMs));
                break;
            }
            lastPlayMs = now;
            driver->playTrack(action.track);
            taskENTER_CRITICAL(&robotStateMux);
            robotState.audioActive = true;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_INFO(TAG, "play track %u", (unsigned)action.track);
            break;
        }

        case AUDIO_ACTION_STOP:
            driver->stop();
            randomMode = false;
            taskENTER_CRITICAL(&robotStateMux);
            robotState.audioActive = false;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_INFO(TAG, "stop + random off");
            break;

        case AUDIO_ACTION_RANDOM_ON:
            randomMode = true;
            PA_LOG_INFO(TAG, "random mode on");
            break;

        case AUDIO_ACTION_RANDOM_OFF:
            randomMode = false;
            PA_LOG_INFO(TAG, "random mode off");
            break;

        case AUDIO_ACTION_VOLUME_SET:
            vol = action.volume;  // already 0–30 from parser
            driver->setVolume(vol);
            PA_LOG_INFO(TAG, "volume set %u", (unsigned)vol);
            break;

        case AUDIO_ACTION_VOLUME_UP:
            if (vol < AUDIO_VOLUME_MAX) {
                vol++;
                driver->setVolume(vol);
                PA_LOG_INFO(TAG, "volume up -> %u", (unsigned)vol);
            }
            break;

        case AUDIO_ACTION_VOLUME_DOWN:
            if (vol > AUDIO_VOLUME_MIN) {
                vol--;
                driver->setVolume(vol);
                PA_LOG_INFO(TAG, "volume down -> %u", (unsigned)vol);
            }
            break;

        case AUDIO_ACTION_NONE:
        default:
            break;
    }
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

    bool driverInitialized = false;  // becomes true on first enable
    bool randomMode = false;
    uint32_t lastRandMs = 0;
    uint32_t lastPlayMs = 0;  // anti-spam: last playTrack() timestamp
    uint32_t lastAutoQueryMs = 0;  // 2 s status poll timer for safe-polling modules
    uint8_t currentVol = 20;  // updated from config on first enable
    bool wasSleeping = false;

    // Minimum interval between successive playTrack() calls (ms).
    // Application-level anti-bounce guard for rapid-fire play commands
    // (e.g. UI double-tap, RC switch bounce). Driver-level timing
    // constraints, if any, are enforced inside the driver's
    // playTrack() implementation.
    // The constant is defined near dispatchAction() (DISPATCH_PLAY_MIN_MS)
    // and also used for direct AUDIO_CMD_PLAY_TRACK below.

    // Named tracks populated from NVS-backed robotState fields.
    // Updated at runtime via POST /api/audio/tracks.
    // Refreshed on each enable cycle so changes take effect without reboot.
    AudioNamedTracks named{};

    AudioCommand cmd{};
    bool hwmLogged = false;

    for (;;) {
        if (!hwmLogged) {
            PA_LOG_INFO("AudioTask", "stack HWM: %u words free",
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
        taskENTER_CRITICAL(&robotStateMux);
        audioEnabled = robotState.cfg_enable_s2_sound;
        sleepMode = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);

        if (!audioEnabled) {
            // Disabled: drain any queued commands silently, clear runtime state.
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

        // ----------------------------------------------------------------
        // Enabled: initialise driver on first enable.
        // Covers both the boot case (enabled from the start) and the
        // runtime case (user enables audio after boot via Setup page).
        // driver->begin(vol) is safe to call once; it configures GPIO and sets volume.
        // ----------------------------------------------------------------
        if (!driverInitialized) {
            taskENTER_CRITICAL(&robotStateMux);
            currentVol = robotState.cfg_audioVolume;
            named.scream = robotState.cfg_snd_scream;
            named.faint = robotState.cfg_snd_faint;
            named.leia = robotState.cfg_snd_leia;
            named.cantina_s = robotState.cfg_snd_cantina_s;
            named.sw_theme = robotState.cfg_snd_sw_theme;
            named.imp_march = robotState.cfg_snd_imp_march;
            named.cantina_l = robotState.cfg_snd_cantina_l;
            named.startup = robotState.cfg_snd_startup;
            taskEXIT_CRITICAL(&robotStateMux);
            driver->begin(currentVol);
            driverInitialized = true;
            PA_LOG_INFO(TAG, "audio driver init — PA_AUDIO_DRIVER=%d vol=%u", PA_AUDIO_DRIVER,
                        (unsigned)currentVol);

            // Seed RobotState from getCachedState() — begin() now runs
            // pre-init queries so m_device and m_totalTracks may already
            // be populated (non-0xFF/0) if the module responded.
            {
                AudioModuleState ms{};
                driver->getCachedState(ms);
                taskENTER_CRITICAL(&robotStateMux);
                robotState.audio_module_link_ok = ms.linkOk;
                robotState.audio_module_play_state = ms.playState;
                robotState.audio_module_device = ms.device;
                robotState.audio_module_total_tracks = ms.totalTracks;
                robotState.audio_module_current_track = ms.currentTrack;
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
                    // Refresh named tracks from RobotState so runtime changes
                    // via POST /api/audio/tracks take effect immediately without
                    // requiring a disable/enable cycle.
                    taskENTER_CRITICAL(&robotStateMux);
                    named.scream = robotState.cfg_snd_scream;
                    named.faint = robotState.cfg_snd_faint;
                    named.leia = robotState.cfg_snd_leia;
                    named.cantina_s = robotState.cfg_snd_cantina_s;
                    named.sw_theme = robotState.cfg_snd_sw_theme;
                    named.imp_march = robotState.cfg_snd_imp_march;
                    named.cantina_l = robotState.cfg_snd_cantina_l;
                    named.startup = robotState.cfg_snd_startup;
                    taskEXIT_CRITICAL(&robotStateMux);
                    bool wasRandom = randomMode;
                    AudioAction action = parseAudioDollar(cmd.dollar, named);
                    dispatchAction(action, currentVol, randomMode, lastPlayMs);
                    // Reset timer when random mode first turns on so the
                    // first track fires after a full interval, not immediately.
                    if (randomMode && !wasRandom) {
                        lastRandMs = millis();
                    }
                    break;
                }

                case AUDIO_CMD_PLAY_TRACK: {
                    if (sleepMode) {
                        PA_LOG_INFO(TAG, "[%s] play track ignored — sleep mode active",
                                    commandSourceToString(cmd.source));
                        break;
                    }
                    uint32_t now = millis();
                    if ((uint32_t)(now - lastPlayMs) < DISPATCH_PLAY_MIN_MS) {
                        PA_LOG_DEBUG(TAG, "[%s] play track %u dropped (anti-spam)",
                                     commandSourceToString(cmd.source), (unsigned)cmd.track);
                        break;
                    }
                    lastPlayMs = now;
                    driver->playTrack(cmd.track);
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.audioActive = true;
                    taskEXIT_CRITICAL(&robotStateMux);
                    PA_LOG_INFO(TAG, "[%s] play track %u", commandSourceToString(cmd.source),
                                (unsigned)cmd.track);
                    break;
                }

                case AUDIO_CMD_STOP:
                    driver->stop();
                    randomMode = false;
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.audioActive = false;
                    taskEXIT_CRITICAL(&robotStateMux);
                    PA_LOG_INFO(TAG, "[%s] stop", commandSourceToString(cmd.source));
                    break;

                case AUDIO_CMD_SET_VOLUME:
                    currentVol = cmd.volume;  // clamped by helper before enqueue
                    driver->setVolume(currentVol);
                    PA_LOG_INFO(TAG, "[%s] volume %u", commandSourceToString(cmd.source),
                                (unsigned)currentVol);
                    break;

                case AUDIO_CMD_QUERY_STATUS: {
                    // On-demand query triggered by the web UI poll button.
                    // Runs only when explicitly requested — never automatically.
                    // The 3-query sequence takes up to ~900 ms; this is acceptable
                    // since it is user-initiated and not in a real-time loop.
                    AudioModuleState ms{};
                    bool ok = driver->queryModuleState(ms);
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.audio_module_link_ok = ms.linkOk;
                    robotState.audio_module_play_state = ms.playState;
                    robotState.audio_module_device = ms.device;
                    robotState.audio_module_total_tracks = ms.totalTracks;
                    robotState.audio_module_current_track = ms.currentTrack;
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
            taskENTER_CRITICAL(&robotStateMux);
            uint8_t mood = robotState.activeMood;
            uint16_t randMin = robotState.cfg_snd_rand_min;
            uint16_t randMax = robotState.cfg_snd_rand_max;
            uint16_t intSec;
            switch (mood) {
                case 10:
                    intSec = robotState.cfg_snd_int_quiet;
                    break;
                case 13:
                    intSec = robotState.cfg_snd_int_mid;
                    break;
                case 14:
                    intSec = robotState.cfg_snd_int_awake;
                    break;
                default:
                    intSec = robotState.cfg_snd_int_full;
                    break;  // SE11 + unset
            }
            taskEXIT_CRITICAL(&robotStateMux);
            if (intSec == 0) {
                // Interval of 0 suppresses random playback for this mood.
                // Advance the timer so a mood change doesn't fire immediately.
                lastRandMs = millis();
            } else {
                uint32_t intervalMs = (uint32_t)intSec * 1000u;
                uint32_t now = millis();
                if ((uint32_t)(now - lastRandMs) >= intervalMs) {
                    lastRandMs = now;
                    if (randMax < randMin)
                        randMax = randMin;  // guard against bad config
                    // esp_random() uses the ESP32 hardware RNG — no seeding needed.
                    uint32_t range = (uint32_t)(randMax - randMin) + 1;
                    uint16_t track = (uint16_t)(randMin + (esp_random() % range));
                    // Random timer already enforces a multi-second interval so
                    // anti-spam check is redundant here, but update lastPlayMs
                    // so a manual play immediately after a random fire is gated.
                    lastPlayMs = millis();
                    driver->playTrack(track);
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.audioActive = true;
                    taskEXIT_CRITICAL(&robotStateMux);
                    PA_LOG_DEBUG(TAG, "random track %u (mood %u, int %us)", (unsigned)track,
                                 (unsigned)mood, (unsigned)intSec);
                }
            }
        }

        // ----------------------------------------------------------------
        // Periodic auto-query runs only for modules reporting
        // AUDIO_CAP_QUERY_SAFE_PLAYING. For modules without that capability
        // (background polling can corrupt some module RX state machines during
        // playback), status is updated only via the manual Poll button
        // (AUDIO_CMD_QUERY_STATUS).
        // ----------------------------------------------------------------
        if ((driver->capabilities() & AudioDriver::AUDIO_CAP_QUERY_SAFE_PLAYING) &&
            ((uint32_t)(millis() - lastAutoQueryMs) >= 2000u)) {
            lastAutoQueryMs = millis();
            AudioModuleState ms{};
            bool ok = driver->queryModuleState(ms);
            taskENTER_CRITICAL(&robotStateMux);
            robotState.audio_module_link_ok = ms.linkOk;
            robotState.audio_module_play_state = ms.playState;
            robotState.audio_module_device = ms.device;
            robotState.audio_module_total_tracks = ms.totalTracks;
            robotState.audio_module_current_track = ms.currentTrack;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_DEBUG(TAG, "auto-query: link=%s play=0x%02X", ok ? "OK" : "no-rsp",
                         (unsigned)ms.playState);
        }
    }
}
