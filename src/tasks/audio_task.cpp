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
#include <string.h>
#include <ctype.h>

#include "audio_dollar_parser.h"
#include "audio_driver.h"
#include "config.h"
#include "logging.h"
#include "mood_sound_mapping.h"
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

const ChirpCatalogEntry* audioGetCatalogEntries(uint16_t* count) {
    if (count) {
        *count = driver->getCatalogEntryCount();
    }
    return driver->getCatalogEntries();
}

const ChirpCatalogBank* audioGetCatalogBanks(uint8_t* count) {
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
// dispatchAction()
// Apply a parsed AudioAction to the driver; update volume and randomMode state.
// -----------------------------------------------------------------------------
static constexpr uint32_t DISPATCH_PLAY_MIN_MS = 300;
static constexpr uint32_t AUTO_STATUS_QUERY_INTERVAL_MS = 10000;  // 10 s
static const char* chirpBindingKeyForSlot(AudioPlaybackSlot slot) {
    switch (slot) {
        case AUDIO_SLOT_NAMED_SCREAM: return "chr_scream";
        case AUDIO_SLOT_NAMED_FAINT: return "chr_faint";
        case AUDIO_SLOT_NAMED_LEIA: return "chr_leia";
        case AUDIO_SLOT_NAMED_CANTINA_S: return "chr_cantina_s";
        case AUDIO_SLOT_NAMED_SW_THEME: return "chr_sw_theme";
        case AUDIO_SLOT_NAMED_IMP_MARCH: return "chr_imp_march";
        case AUDIO_SLOT_NAMED_CANTINA_L: return "chr_cantina_l";
        case AUDIO_SLOT_NAMED_STARTUP: return "chr_startup";
        case AUDIO_SLOT_NAMED_DISCO: return "chr_disco";
        case AUDIO_SLOT_SYS_BOOT: return "chr_sys_boot";
        case AUDIO_SLOT_SYS_MODE_NORMAL: return "chr_sys_mode_n";
        case AUDIO_SLOT_SYS_MODE_SLOW: return "chr_sys_mode_s";
        case AUDIO_SLOT_SYS_MODE_TURBO: return "chr_sys_mode_t";
        case AUDIO_SLOT_SYS_DRIVE_ON: return "chr_sys_drv_on";
        case AUDIO_SLOT_SYS_DOME_ON: return "chr_sys_dome_on";
        case AUDIO_SLOT_NONE:
        default:
            return nullptr;
    }
}

static const char* chirpBindingKeyForCategoryIndex(uint8_t categoryIndex) {
    switch (categoryIndex) {
        case 0: return "chr_cat_gen";
        case 1: return "chr_cat_chat";
        case 2: return "chr_cat_hap";
        case 3: return "chr_cat_proc";
        case 4: return "chr_cat_sad";
        case 5: return "chr_cat_sent";
        case 6: return "chr_cat_hum";
        case 7: return "chr_cat_scrm";
        case 8: return "chr_cat_ooh";
        case 9: return "chr_cat_alrm";
        case 10: return "chr_cat_snrk";
        case 11: return "chr_cat_whis";
        default:
            return nullptr;
    }
}

static bool readTrackForSlot(AudioPlaybackSlot slot, uint16_t* trackOut) {
    if (trackOut == nullptr) {
        return false;
    }

    uint16_t track = 0;
    bool known = true;
    taskENTER_CRITICAL(&robotStateMux);
    switch (slot) {
        case AUDIO_SLOT_NAMED_SCREAM:
            track = robotState.cfg_snd_scream;
            break;
        case AUDIO_SLOT_NAMED_FAINT:
            track = robotState.cfg_snd_faint;
            break;
        case AUDIO_SLOT_NAMED_LEIA:
            track = robotState.cfg_snd_leia;
            break;
        case AUDIO_SLOT_NAMED_CANTINA_S:
            track = robotState.cfg_snd_cantina_s;
            break;
        case AUDIO_SLOT_NAMED_SW_THEME:
            track = robotState.cfg_snd_sw_theme;
            break;
        case AUDIO_SLOT_NAMED_IMP_MARCH:
            track = robotState.cfg_snd_imp_march;
            break;
        case AUDIO_SLOT_NAMED_CANTINA_L:
            track = robotState.cfg_snd_cantina_l;
            break;
        case AUDIO_SLOT_NAMED_STARTUP:
            track = robotState.cfg_snd_startup;
            break;
        case AUDIO_SLOT_NAMED_DISCO:
            track = robotState.cfg_snd_disco;
            break;
        case AUDIO_SLOT_SYS_BOOT:
            track = robotState.cfg_snd_sys_boot;
            break;
        case AUDIO_SLOT_SYS_MODE_NORMAL:
            track = robotState.cfg_snd_sys_mode_n;
            break;
        case AUDIO_SLOT_SYS_MODE_SLOW:
            track = robotState.cfg_snd_sys_mode_s;
            break;
        case AUDIO_SLOT_SYS_MODE_TURBO:
            track = robotState.cfg_snd_sys_mode_t;
            break;
        case AUDIO_SLOT_SYS_DRIVE_ON:
            track = robotState.cfg_snd_sys_drv_on;
            break;
        case AUDIO_SLOT_SYS_DOME_ON:
            track = robotState.cfg_snd_sys_dome_on;
            break;
        case AUDIO_SLOT_NONE:
        default:
            known = false;
            break;
    }
    taskEXIT_CRITICAL(&robotStateMux);

    if (!known) {
        return false;
    }
    *trackOut = track;
    return true;
}

static AudioPlaybackSlot slotForDollarCommand(const char* cmd) {
    if (cmd == nullptr || cmd[0] != '$') {
        return AUDIO_SLOT_NONE;
    }
    switch (cmd[1]) {
        case 'S': return AUDIO_SLOT_NAMED_SCREAM;
        case 'F': return AUDIO_SLOT_NAMED_FAINT;
        case 'L': return AUDIO_SLOT_NAMED_LEIA;
        case 'c': return AUDIO_SLOT_NAMED_CANTINA_S;
        case 'C': return AUDIO_SLOT_NAMED_CANTINA_L;
        case 'W': return AUDIO_SLOT_NAMED_SW_THEME;
        case 'M': return AUDIO_SLOT_NAMED_IMP_MARCH;
        case 'B': return AUDIO_SLOT_NAMED_STARTUP;
        case 'D': return AUDIO_SLOT_NAMED_DISCO;
        default:
            return AUDIO_SLOT_NONE;
    }
}

static bool unpackChirpBinding(uint32_t packed, uint8_t* bankOut, char* pageOut,
                               uint16_t* indexOut) {
    if (bankOut == nullptr || pageOut == nullptr || indexOut == nullptr) {
        return false;
    }

    uint8_t bank = (uint8_t)((packed >> 24) & 0xFFu);
    char page = (char)((packed >> 16) & 0xFFu);
    uint16_t index = (uint16_t)(packed & 0xFFFFu);
    if (bank == 0 || index == 0) {
        return false;
    }

    page = (char)toupper((unsigned char)page);
    if (page < 'A' || page > 'Z') {
        return false;
    }

    *bankOut = bank;
    *pageOut = page;
    *indexOut = index;
    return true;
}

static bool unpackChirpCategoryBinding(uint32_t packed, uint8_t* bankOut, char* pageOut) {
    if (bankOut == nullptr || pageOut == nullptr) {
        return false;
    }
    uint8_t bank = (uint8_t)((packed >> 8) & 0xFFu);
    char page = (char)(packed & 0xFFu);
    if (bank == 0) {
        return false;
    }
    page = (char)toupper((unsigned char)page);
    if (page < 'A' || page > 'Z') {
        return false;
    }
    *bankOut = bank;
    *pageOut = page;
    return true;
}

static constexpr uint8_t CHIRP_CATEGORY_BINDING_COUNT = 12u;
static constexpr uint8_t CHIRP_SLOT_BINDING_COUNT = (uint8_t)AUDIO_SLOT_SYS_DOME_ON + 1u;

struct ChirpSlotBindingCache {
    bool valid = false;
    uint8_t bank = 0;
    char page = 'A';
    uint16_t index = 0;
};

struct ChirpCategoryBindingCache {
    bool valid = false;
    uint8_t bank = 0;
    char page = 'A';
};

static ChirpSlotBindingCache s_chirpSlotBindings[CHIRP_SLOT_BINDING_COUNT] = {};
static ChirpCategoryBindingCache s_chirpCategoryBindings[CHIRP_CATEGORY_BINDING_COUNT] = {};

static void clearChirpBindingCache() {
    memset(s_chirpSlotBindings, 0, sizeof(s_chirpSlotBindings));
    memset(s_chirpCategoryBindings, 0, sizeof(s_chirpCategoryBindings));
}

static bool refreshChirpBindingCacheFromNvs() {
    clearChirpBindingCache();

    if ((driver->capabilities() & AudioDriver::AUDIO_CAP_CATALOG) == 0) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        return false;
    }

    for (uint8_t slotIdx = 1; slotIdx < CHIRP_SLOT_BINDING_COUNT; ++slotIdx) {
        const char* nvsKey = chirpBindingKeyForSlot((AudioPlaybackSlot)slotIdx);
        if (nvsKey == nullptr) {
            continue;
        }

        uint8_t bank = 0;
        char page = 'A';
        uint16_t index = 0;
        uint32_t packed = prefs.getUInt(nvsKey, 0);
        if (unpackChirpBinding(packed, &bank, &page, &index)) {
            ChirpSlotBindingCache& entry = s_chirpSlotBindings[slotIdx];
            entry.valid = true;
            entry.bank = bank;
            entry.page = page;
            entry.index = index;
        }
    }

    for (uint8_t categoryIdx = 0; categoryIdx < CHIRP_CATEGORY_BINDING_COUNT; ++categoryIdx) {
        const char* nvsKey = chirpBindingKeyForCategoryIndex(categoryIdx);
        if (nvsKey == nullptr) {
            continue;
        }

        uint8_t bank = 0;
        char page = 'A';
        uint32_t packed = prefs.getUInt(nvsKey, 0);
        if (unpackChirpCategoryBinding(packed, &bank, &page)) {
            ChirpCategoryBindingCache& entry = s_chirpCategoryBindings[categoryIdx];
            entry.valid = true;
            entry.bank = bank;
            entry.page = page;
        }
    }

    prefs.end();
    return true;
}

static bool loadChirpBindingForSlot(AudioPlaybackSlot slot, uint8_t* bankOut, char* pageOut,
                                    uint16_t* indexOut) {
    if (bankOut == nullptr || pageOut == nullptr || indexOut == nullptr) {
        return false;
    }
    uint8_t slotIdx = (uint8_t)slot;
    if (slotIdx >= CHIRP_SLOT_BINDING_COUNT) {
        return false;
    }
    const ChirpSlotBindingCache& entry = s_chirpSlotBindings[slotIdx];
    if (!entry.valid) {
        return false;
    }
    *bankOut = entry.bank;
    *pageOut = entry.page;
    *indexOut = entry.index;
    return true;
}

static bool loadChirpBindingForCategory(uint8_t categoryIndex, uint8_t* bankOut,
                                        char* pageOut) {
    if (bankOut == nullptr || pageOut == nullptr || categoryIndex >= CHIRP_CATEGORY_BINDING_COUNT) {
        return false;
    }
    const ChirpCategoryBindingCache& entry = s_chirpCategoryBindings[categoryIndex];
    if (!entry.valid) {
        return false;
    }
    *bankOut = entry.bank;
    *pageOut = entry.page;
    return true;
}

static void dispatchPlayResolved(uint16_t track, AudioPlaybackSlot slot, CommandSource source,
                                 uint32_t& lastPlayMs, uint32_t& lastRandMs) {
    if (track == 0 && slot == AUDIO_SLOT_NONE) {
        PA_LOG_DEBUG(TAG, "[%s] playback skipped (track=0)", commandSourceToString(source));
        return;
    }

    uint32_t now = millis();
    if ((uint32_t)(now - lastPlayMs) < DISPATCH_PLAY_MIN_MS) {
        PA_LOG_DEBUG(TAG, "[%s] play track %u dropped (anti-spam)",
                     commandSourceToString(source), (unsigned)track);
        return;
    }

    bool playedBanked = false;
    if (slot != AUDIO_SLOT_NONE && (driver->capabilities() & AudioDriver::AUDIO_CAP_CATALOG)) {
        uint8_t bank = 0;
        char page = 'A';
        uint16_t index = 0;
        if (loadChirpBindingForSlot(slot, &bank, &page, &index)) {
            driver->playTrackBanked(index, bank, page);
            playedBanked = true;
            PA_LOG_INFO(TAG, "[%s] play slot=%u bank=%u page=%c index=%u (fallback track=%u)",
                        commandSourceToString(source), (unsigned)slot, (unsigned)bank, page,
                        (unsigned)index, (unsigned)track);
        }
    }

    if (!playedBanked) {
        if (track == 0) {
            PA_LOG_DEBUG(TAG, "[%s] slot playback skipped (slot=%u track=0)",
                         commandSourceToString(source), (unsigned)slot);
            return;
        }
        driver->playTrack(track);
        if (slot == AUDIO_SLOT_NONE) {
            PA_LOG_INFO(TAG, "[%s] play track %u", commandSourceToString(source),
                        (unsigned)track);
        } else {
            PA_LOG_INFO(TAG, "[%s] play slot=%u track=%u", commandSourceToString(source),
                        (unsigned)slot, (unsigned)track);
        }
    }

    lastPlayMs = now;
    lastRandMs = lastPlayMs;
    taskENTER_CRITICAL(&robotStateMux);
    robotState.audioActive = true;
    taskEXIT_CRITICAL(&robotStateMux);
}

static void dispatchAction(const AudioAction& action, uint8_t& vol, bool& randomMode,
                           uint32_t& lastPlayMs, uint32_t& lastRandMs) {
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
            lastRandMs = lastPlayMs;
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
    uint32_t lastAutoQueryMs = 0;  // periodic status poll timer for safe-polling modules
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
            named.disco = robotState.cfg_snd_disco;
            taskEXIT_CRITICAL(&robotStateMux);
            // Soft-UART drivers block for up to ~6 ms per command; AudioTask must run on Core 0.
            configASSERT(xPortGetCoreID() == 0);
            driver->begin(currentVol);
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
                    named.disco = robotState.cfg_snd_disco;
                    taskEXIT_CRITICAL(&robotStateMux);
                    bool wasRandom = randomMode;
                    AudioAction action = parseAudioDollar(cmd.dollar, named);
                    if (action.type == AUDIO_ACTION_PLAY_TRACK) {
                        AudioPlaybackSlot slot = slotForDollarCommand(cmd.dollar);
                        dispatchPlayResolved(action.track, slot, cmd.source, lastPlayMs, lastRandMs);
                    } else {
                        dispatchAction(action, currentVol, randomMode, lastPlayMs, lastRandMs);
                    }
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
                    dispatchPlayResolved(cmd.track, AUDIO_SLOT_NONE, cmd.source, lastPlayMs,
                                         lastRandMs);
                    break;
                }
                case AUDIO_CMD_PLAY_SLOT: {
                    if (sleepMode) {
                        PA_LOG_INFO(TAG, "[%s] slot play ignored — sleep mode active",
                                    commandSourceToString(cmd.source));
                        break;
                    }
                    uint16_t slotTrack = 0;
                    if (!readTrackForSlot(cmd.slot, &slotTrack)) {
                        PA_LOG_WARN(TAG, "[%s] slot play ignored — unknown slot=%u",
                                    commandSourceToString(cmd.source), (unsigned)cmd.slot);
                        break;
                    }
                    dispatchPlayResolved(slotTrack, cmd.slot, cmd.source, lastPlayMs,
                                         lastRandMs);
                    break;
                }

                case AUDIO_CMD_PLAY_TRACK_BANKED: {
                    if (sleepMode) {
                        PA_LOG_INFO(TAG, "[%s] banked play ignored — sleep mode active",
                                    commandSourceToString(cmd.source));
                        break;
                    }
                    uint32_t now = millis();
                    if ((uint32_t)(now - lastPlayMs) < DISPATCH_PLAY_MIN_MS) {
                        PA_LOG_DEBUG(TAG, "[%s] banked play %u/%c/%u dropped (anti-spam)",
                                     commandSourceToString(cmd.source), (unsigned)cmd.banked.bank,
                                     cmd.banked.page, (unsigned)cmd.banked.index);
                        break;
                    }
                    lastPlayMs = now;
                    driver->playTrackBanked(cmd.banked.index, cmd.banked.bank, cmd.banked.page);
                    lastRandMs = lastPlayMs;
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.audioActive = true;
                    taskEXIT_CRITICAL(&robotStateMux);
                    PA_LOG_INFO(TAG, "[%s] play bank=%u page=%c index=%u",
                                commandSourceToString(cmd.source), (unsigned)cmd.banked.bank,
                                cmd.banked.page, (unsigned)cmd.banked.index);
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

                case AUDIO_CMD_REFRESH_CATALOG: {
                    if (!(driver->capabilities() & AudioDriver::AUDIO_CAP_CATALOG)) {
                        PA_LOG_DEBUG(TAG, "[%s] catalog refresh ignored (unsupported backend)",
                                     commandSourceToString(cmd.source));
                        break;
                    }
                    bool ok = driver->refreshCatalog();
                    PA_LOG_INFO(TAG, "[%s] catalog refresh %s", commandSourceToString(cmd.source),
                                ok ? "OK" : "FAILED");
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
            MoodCategoryMaskConfig moodMasks = {robotState.cfg_snd_moodcat_quiet,
                                                robotState.cfg_snd_moodcat_mid,
                                                robotState.cfg_snd_moodcat_full,
                                                robotState.cfg_snd_moodcat_awakeplus};
            SoundCategoryRange categoryRanges[SOUND_CATEGORY_COUNT] = {
                {robotState.cfg_snd_cat_gen_lo, robotState.cfg_snd_cat_gen_hi},
                {robotState.cfg_snd_cat_chat_lo, robotState.cfg_snd_cat_chat_hi},
                {robotState.cfg_snd_cat_hap_lo, robotState.cfg_snd_cat_hap_hi},
                {robotState.cfg_snd_cat_proc_lo, robotState.cfg_snd_cat_proc_hi},
                {robotState.cfg_snd_cat_sad_lo, robotState.cfg_snd_cat_sad_hi},
                {robotState.cfg_snd_cat_sent_lo, robotState.cfg_snd_cat_sent_hi},
                {robotState.cfg_snd_cat_hum_lo, robotState.cfg_snd_cat_hum_hi},
                {robotState.cfg_snd_cat_scrm_lo, robotState.cfg_snd_cat_scrm_hi},
                {robotState.cfg_snd_cat_ooh_lo, robotState.cfg_snd_cat_ooh_hi},
                {robotState.cfg_snd_cat_alrm_lo, robotState.cfg_snd_cat_alrm_hi},
                {robotState.cfg_snd_cat_snarky_lo, robotState.cfg_snd_cat_snarky_hi},
                {robotState.cfg_snd_cat_whis_lo, robotState.cfg_snd_cat_whis_hi},
            };
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
                    uint16_t track = 0;
                    bool usedFlatFallback = false;
                    uint8_t selectedCategory = 0xFF;
                    const bool selected =
                        selectRandomTrackForMood(mood, moodMasks, categoryRanges, SOUND_CATEGORY_COUNT,
                                                 randMin, randMax, esp_random(), &track,
                                                 &usedFlatFallback, &selectedCategory);
                    if (selected) {
                        // Random timer already enforces a multi-second interval so
                        // anti-spam check is redundant here, but update lastPlayMs
                        // so a manual play immediately after a random fire is gated.
                        lastPlayMs = millis();

                        bool playedBanked = false;
                        if (!usedFlatFallback && selectedCategory != 0xFF &&
                            (driver->capabilities() & AudioDriver::AUDIO_CAP_CATALOG)) {
                            uint8_t bank = 0;
                            char page = 'A';
                            if (loadChirpBindingForCategory(selectedCategory, &bank, &page)) {
                                driver->playTrackBanked(track, bank, page);
                                playedBanked = true;
                                PA_LOG_DEBUG(TAG,
                                             "random track %u (mood %u, int %us, category pool bank=%u page=%c)",
                                             (unsigned)track, (unsigned)mood, (unsigned)intSec,
                                             (unsigned)bank, page);
                            }
                        }
                        if (!playedBanked) {
                            driver->playTrack(track);
                            if (usedFlatFallback) {
                                PA_LOG_DEBUG(TAG,
                                             "random track %u (mood %u, int %us, flat fallback)",
                                             (unsigned)track, (unsigned)mood, (unsigned)intSec);
                            } else {
                                PA_LOG_DEBUG(TAG,
                                             "random track %u (mood %u, int %us, category pool)",
                                             (unsigned)track, (unsigned)mood, (unsigned)intSec);
                            }
                        }

                        taskENTER_CRITICAL(&robotStateMux);
                        robotState.audioActive = true;
                        taskEXIT_CRITICAL(&robotStateMux);
                    }
                }
            }
        }

        // ----------------------------------------------------------------
        // Periodic auto-query runs only for modules reporting
        // AUDIO_CAP_QUERY_SAFE_PLAYING at AUTO_STATUS_QUERY_INTERVAL_MS
        // cadence. For modules without that capability (background polling
        // can corrupt some module RX state machines during playback),
        // status is updated only via the manual Poll button
        // ----------------------------------------------------------------
        if ((driver->capabilities() & AudioDriver::AUDIO_CAP_QUERY_SAFE_PLAYING) &&
            ((uint32_t)(millis() - lastAutoQueryMs) >= AUTO_STATUS_QUERY_INTERVAL_MS)) {
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
