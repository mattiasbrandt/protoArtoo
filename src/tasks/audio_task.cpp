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

#include <string.h>

#include <Arduino.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "audio_dollar_parser.h"
#include "audio_driver.h"
#include "logging.h"
#include "robot_state.h"

// -----------------------------------------------------------------------------
// Driver instantiation — one concrete driver per build
// -----------------------------------------------------------------------------
#if PA_AUDIO_DRIVER == AUDIO_SOFT_UART
#include "audio_soft_uart.h"
static AudioDriverSoftUart s_driver;
#elif PA_AUDIO_DRIVER == AUDIO_CHIRP
#include "audio_chirp.h"
static AudioDriverChirp s_driver;
#elif PA_AUDIO_DRIVER == AUDIO_DFPLAYER
#error "AUDIO_DFPLAYER driver not yet implemented — see T15 / T16"
#elif PA_AUDIO_DRIVER == AUDIO_MP3TRIGGER
#error "AUDIO_MP3TRIGGER driver not yet implemented"
#else
#error "PA_AUDIO_DRIVER build flag is not set or has an unknown value"
#endif

static AudioDriver* const driver = &s_driver;

static const char* TAG = "AudioTask";

// -----------------------------------------------------------------------------
// Queue-send helpers (non-blocking, timeout 0)
// -----------------------------------------------------------------------------

bool audioQueueDollar(const char* cmd, CommandSource src) {
    if (!cmd || cmd[0] != '$') {
        return false;
    }
    AudioCommand msg{};
    msg.type   = AUDIO_CMD_DOLLAR;
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
    msg.type   = AUDIO_CMD_PLAY_TRACK;
    msg.source = src;
    msg.track  = track;
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
    msg.type   = AUDIO_CMD_STOP;
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
    msg.type   = AUDIO_CMD_SET_VOLUME;
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

// -----------------------------------------------------------------------------
// dispatchAction()
// Apply a parsed AudioAction to the driver; update volume and randomMode state.
// -----------------------------------------------------------------------------
static void dispatchAction(const AudioAction& action, uint8_t& vol, bool& randomMode) {
    switch (action.type) {
        case AUDIO_ACTION_PLAY_TRACK:
            driver->playTrack(action.track);
            taskENTER_CRITICAL(&robotStateMux);
            robotState.audioActive = true;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_INFO(TAG, "play track %u", (unsigned)action.track);
            break;

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
    bool randomMode        = false;
    uint32_t lastRandMs    = 0;
    uint8_t currentVol     = 20;     // updated from config on first enable

    // Named tracks populated from NVS-backed robotState fields.
    // Updated at runtime via POST /api/audio/tracks.
    // Refreshed on each enable cycle so changes take effect without reboot.
    AudioNamedTracks named{};

    AudioCommand cmd{};

    for (;;) {
        // ----------------------------------------------------------------
        // Read enabled state fresh every iteration.
        // The user can toggle S2 Sound in the Setup page at any time;
        // we must not require a reboot for the change to take effect.
        // ----------------------------------------------------------------
        bool audioEnabled;
        taskENTER_CRITICAL(&robotStateMux);
        audioEnabled = robotState.cfg_enable_s2_sound;
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
            continue;
        }

        // ----------------------------------------------------------------
        // Enabled: initialise driver on first enable.
        // Covers both the boot case (enabled from the start) and the
        // runtime case (user enables audio after boot via Setup page).
        // driver->begin() is safe to call once; it configures GPIO only.
        // ----------------------------------------------------------------
        if (!driverInitialized) {
            taskENTER_CRITICAL(&robotStateMux);
            currentVol      = robotState.cfg_audioVolume;
            named.scream    = robotState.cfg_snd_scream;
            named.faint     = robotState.cfg_snd_faint;
            named.leia      = robotState.cfg_snd_leia;
            named.cantina_s = robotState.cfg_snd_cantina_s;
            named.sw_theme  = robotState.cfg_snd_sw_theme;
            named.imp_march = robotState.cfg_snd_imp_march;
            named.cantina_l = robotState.cfg_snd_cantina_l;
            named.startup   = robotState.cfg_snd_startup;
            taskEXIT_CRITICAL(&robotStateMux);
            driver->begin();
            driver->setVolume(currentVol);
            driverInitialized = true;
            PA_LOG_INFO(TAG, "audio driver init — PA_AUDIO_DRIVER=%d vol=%u",
                        PA_AUDIO_DRIVER, (unsigned)currentVol);
        }

        // ----------------------------------------------------------------
        // Process one command from the queue (500 ms timeout so the random
        // timer below can fire even when the queue is idle).
        // ----------------------------------------------------------------
        if (xQueueReceive(audioCmdQueue, &cmd, pdMS_TO_TICKS(500)) == pdTRUE) {
            switch (cmd.type) {
                case AUDIO_CMD_DOLLAR: {
                    bool wasRandom = randomMode;
                    AudioAction action = parseAudioDollar(cmd.dollar, named);
                    dispatchAction(action, currentVol, randomMode);
                    // Reset timer when random mode first turns on so the
                    // first track fires after a full interval, not immediately.
                    if (randomMode && !wasRandom) {
                        lastRandMs = millis();
                    }
                    break;
                }

                case AUDIO_CMD_PLAY_TRACK:
                    driver->playTrack(cmd.track);
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.audioActive = true;
                    taskEXIT_CRITICAL(&robotStateMux);
                    PA_LOG_INFO(TAG, "[%s] play track %u",
                                commandSourceToString(cmd.source), (unsigned)cmd.track);
                    break;

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
                    PA_LOG_INFO(TAG, "[%s] volume %u",
                                commandSourceToString(cmd.source), (unsigned)currentVol);
                    break;
            }
        }

        // ----------------------------------------------------------------
        // Random playback timer.
        // randomMode is only true when audio is enabled and a $R command
        // was received. audioEnabled is guaranteed true here (we continued
        // at the top of the loop if it was false).
        // ----------------------------------------------------------------
        if (randomMode) {
            uint32_t now = millis();
            if ((uint32_t)(now - lastRandMs) >= AUDIO_RAND_INTERVAL_MS) {
                lastRandMs = now;
                // Read random pool bounds from NVS-backed config (may change at runtime).
                taskENTER_CRITICAL(&robotStateMux);
                uint16_t randMin = robotState.cfg_snd_rand_min;
                uint16_t randMax = robotState.cfg_snd_rand_max;
                taskEXIT_CRITICAL(&robotStateMux);
                if (randMax < randMin) randMax = randMin;  // guard against bad config
                // esp_random() uses the ESP32 hardware RNG — no seeding needed.
                uint32_t range = (uint32_t)(randMax - randMin) + 1;
                uint16_t track = (uint16_t)(randMin + (esp_random() % range));
                driver->playTrack(track);
                PA_LOG_DEBUG(TAG, "random track %u", (unsigned)track);
            }
        }
    }
}
