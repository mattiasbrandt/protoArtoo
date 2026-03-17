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
            PA_LOG_INFO(TAG, "play track %u", (unsigned)action.track);
            break;

        case AUDIO_ACTION_STOP:
            driver->stop();
            randomMode = false;
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
// -----------------------------------------------------------------------------
void audioTask(void* pvParameters) {
    (void)pvParameters;

    // Read boot config under mutex
    uint8_t bootVol;
    bool audioEnabled;
    taskENTER_CRITICAL(&robotStateMux);
    bootVol      = robotState.cfg_audioVolume;  // 0–30, already clamped at load
    audioEnabled = robotState.cfg_enable_s2_sound;
    taskEXIT_CRITICAL(&robotStateMux);

    if (!audioEnabled) {
        PA_LOG_INFO(TAG, "audio disabled (en_s2=false) — task idle");
        // Drain queue silently so senders do not stall; never touch hardware.
        AudioCommand cmd{};
        for (;;) {
            xQueueReceive(audioCmdQueue, &cmd, pdMS_TO_TICKS(5000));
        }
    }

    // Initialise driver hardware
    driver->begin();
    driver->setVolume(bootVol);
    PA_LOG_INFO(TAG, "started — driver PA_AUDIO_DRIVER=%d vol=%u", PA_AUDIO_DRIVER,
                (unsigned)bootVol);

    uint8_t  currentVol  = bootVol;
    bool     randomMode  = false;
    uint32_t lastRandMs  = 0;

    // Named tracks: use compile-time defaults for T02.
    // T07 will populate these from NVS-backed robotState fields.
    const AudioNamedTracks named{};

    AudioCommand cmd{};
    for (;;) {
        // Block up to 500 ms so the random playback timer can fire
        // even when the command queue is idle.
        const TickType_t waitTicks = pdMS_TO_TICKS(500);
        if (xQueueReceive(audioCmdQueue, &cmd, waitTicks) == pdTRUE) {
            // Re-read audio enabled flag in case it changed via web API
            taskENTER_CRITICAL(&robotStateMux);
            audioEnabled = robotState.cfg_enable_s2_sound;
            taskEXIT_CRITICAL(&robotStateMux);

            if (!audioEnabled) {
                continue;  // drop commands while disabled
            }

            switch (cmd.type) {
                case AUDIO_CMD_DOLLAR: {
                    bool wasRandom = randomMode;
                    AudioAction action = parseAudioDollar(cmd.dollar, named);
                    dispatchAction(action, currentVol, randomMode);
                    // If random mode just turned on, reset the timer so the
                    // first random track fires after a full interval, not
                    // immediately (lastRandMs was 0 or stale before this).
                    if (randomMode && !wasRandom) {
                        lastRandMs = millis();
                    }
                    break;
                }
                case AUDIO_CMD_PLAY_TRACK:
                    driver->playTrack(cmd.track);
                    PA_LOG_INFO(TAG, "[%s] play track %u",
                                commandSourceToString(cmd.source), (unsigned)cmd.track);
                    break;

                case AUDIO_CMD_STOP:
                    driver->stop();
                    randomMode = false;
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

        // Random playback timer — fire a random track at the configured interval
        if (randomMode && audioEnabled) {
            uint32_t now = millis();
            if ((uint32_t)(now - lastRandMs) >= AUDIO_RAND_INTERVAL_MS) {
                lastRandMs = now;
                // Uniform random in [AUDIO_RAND_TRACK_MIN, AUDIO_RAND_TRACK_MAX].
                // esp_random() uses the ESP32 hardware RNG — no seeding needed.
                uint32_t range = AUDIO_RAND_TRACK_MAX - AUDIO_RAND_TRACK_MIN + 1;
                uint16_t track = (uint16_t)(AUDIO_RAND_TRACK_MIN + (esp_random() % range));
                driver->playTrack(track);
                PA_LOG_DEBUG(TAG, "random track %u", (unsigned)track);
            }
        }
    }
}
