// =============================================================================
// include/audio_playback_policy.h
//
// Pure playback decision helpers for AudioTask.
// No driver, FreeRTOS, RobotState, NVS, logging, or ESP random dependencies.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mood_sound_mapping.h"

static constexpr uint32_t AUDIO_PLAYBACK_ANTI_SPAM_MS = 300;

enum AudioPlaybackSlot : uint8_t {
    AUDIO_SLOT_NONE = 0,
    AUDIO_SLOT_NAMED_SCREAM,
    AUDIO_SLOT_NAMED_FAINT,
    AUDIO_SLOT_NAMED_LEIA,
    AUDIO_SLOT_NAMED_CANTINA_S,
    AUDIO_SLOT_NAMED_SW_THEME,
    AUDIO_SLOT_NAMED_IMP_MARCH,
    AUDIO_SLOT_NAMED_CANTINA_L,
    AUDIO_SLOT_NAMED_STARTUP,
    AUDIO_SLOT_NAMED_DISCO,
    AUDIO_SLOT_NAMED_HAPPY,
    AUDIO_SLOT_SYS_BOOT,
    AUDIO_SLOT_SYS_MODE_NORMAL,
    AUDIO_SLOT_SYS_MODE_SLOW,
    AUDIO_SLOT_SYS_MODE_TURBO,
    AUDIO_SLOT_SYS_DRIVE_ON,
    AUDIO_SLOT_SYS_DOME_ON,
    AUDIO_SLOT_COUNT,
};

enum AudioPlaybackCategory : uint8_t {
    AUDIO_CATEGORY_GENERAL = 0,
    AUDIO_CATEGORY_CHATTY,
    AUDIO_CATEGORY_HAPPY,
    AUDIO_CATEGORY_PROCESSING,
    AUDIO_CATEGORY_SAD,
    AUDIO_CATEGORY_SENTIMENTAL,
    AUDIO_CATEGORY_HUMMING,
    AUDIO_CATEGORY_SCREAM,
    AUDIO_CATEGORY_SURPRISED,
    AUDIO_CATEGORY_ALERT,
    AUDIO_CATEGORY_SNARKY,
    AUDIO_CATEGORY_WHISTLE,
    AUDIO_CATEGORY_COUNT,
    AUDIO_CATEGORY_NONE = 0xFF,
};

inline const char* audioCategoryToString(AudioPlaybackCategory cat) {
    switch (cat) {
        case AUDIO_CATEGORY_GENERAL:     return "general";
        case AUDIO_CATEGORY_CHATTY:      return "chatty";
        case AUDIO_CATEGORY_HAPPY:       return "happy";
        case AUDIO_CATEGORY_PROCESSING:  return "processing";
        case AUDIO_CATEGORY_SAD:         return "sad";
        case AUDIO_CATEGORY_SENTIMENTAL: return "sentimental";
        case AUDIO_CATEGORY_HUMMING:     return "humming";
        case AUDIO_CATEGORY_SCREAM:      return "scream";
        case AUDIO_CATEGORY_SURPRISED:   return "surprised";
        case AUDIO_CATEGORY_ALERT:       return "alert";
        case AUDIO_CATEGORY_SNARKY:      return "snarky";
        case AUDIO_CATEGORY_WHISTLE:     return "whistle";
        default:                         return "unknown";
    }
}

enum AudioPlaybackRequestKind : uint8_t {
    AUDIO_PLAYBACK_REQ_NONE = 0,
    AUDIO_PLAYBACK_REQ_DIRECT_TRACK,
    AUDIO_PLAYBACK_REQ_DIRECT_BANKED,
    AUDIO_PLAYBACK_REQ_SLOT,
    AUDIO_PLAYBACK_REQ_CATEGORY,
    AUDIO_PLAYBACK_REQ_STOP,
    AUDIO_PLAYBACK_REQ_TRACK_STOP,  // Track Stop (ADR 0010): stop current playback
                                    // only, preserve randomMode  --  every non-mood
                                    // stop surface uses this instead of REQ_STOP.
    AUDIO_PLAYBACK_REQ_SET_VOLUME,
    AUDIO_PLAYBACK_REQ_RANDOM_ON,
    AUDIO_PLAYBACK_REQ_RANDOM_OFF,
    AUDIO_PLAYBACK_REQ_RANDOM_TICK,
};

enum AudioPlaybackIntentKind : uint8_t {
    AUDIO_PLAYBACK_INTENT_NONE = 0,
    AUDIO_PLAYBACK_INTENT_PLAY_FLAT,
    AUDIO_PLAYBACK_INTENT_PLAY_BANKED,
    AUDIO_PLAYBACK_INTENT_STOP,
    AUDIO_PLAYBACK_INTENT_TRACK_STOP,  // Track Stop (ADR 0010): driver->stop() only,
                                       // randomMode left untouched.
    AUDIO_PLAYBACK_INTENT_SET_VOLUME,
    AUDIO_PLAYBACK_INTENT_RANDOM_ON,
    AUDIO_PLAYBACK_INTENT_RANDOM_OFF,
};

enum AudioPlaybackNoneReason : uint8_t {
    AUDIO_PLAYBACK_NONE_OK = 0,
    AUDIO_PLAYBACK_NONE_UNKNOWN_SLOT,
    AUDIO_PLAYBACK_NONE_TRACK_ZERO,
    AUDIO_PLAYBACK_NONE_INVALID_BANKED,
    AUDIO_PLAYBACK_NONE_UNKNOWN_CATEGORY,
    AUDIO_PLAYBACK_NONE_CATEGORY_EMPTY,
    AUDIO_PLAYBACK_NONE_ANTI_SPAM,
    AUDIO_PLAYBACK_NONE_INTERVAL_NOT_READY,
    AUDIO_PLAYBACK_NONE_INTERVAL_ZERO,
    AUDIO_PLAYBACK_NONE_DOME_SEQUENCE_ACTIVE,
    AUDIO_PLAYBACK_NONE_RANDOM_DISABLED,
};

struct AudioChirpSlotBinding {
    bool valid = false;
    uint8_t bank = 0;
    char page = 'A';
    uint16_t index = 0;
};

struct AudioChirpCategoryBinding {
    bool valid = false;
    uint8_t bank = 0;
    char page = 'A';
};

struct AudioBindingCache {
    AudioChirpSlotBinding slots[AUDIO_SLOT_COUNT] = {};
    AudioChirpCategoryBinding categories[AUDIO_CATEGORY_COUNT] = {};
};

struct AudioPlaybackConfig {
    uint16_t slotTracks[AUDIO_SLOT_COUNT] = {};
    uint16_t randMin = 0;
    uint16_t randMax = 0;
    uint16_t intervalQuietS = 0;
    uint16_t intervalMidS = 0;
    uint16_t intervalFullS = 0;
    uint16_t intervalAwakeS = 0;
    MoodCategoryMaskConfig moodMasks{};
    SoundCategoryRange categoryRanges[AUDIO_CATEGORY_COUNT] = {};
};

struct AudioPlaybackContext {
    const AudioPlaybackConfig* config = nullptr;
    const AudioBindingCache* bindings = nullptr;
    bool catalogCapable = false;
    uint32_t nowMs = 0;
    uint32_t lastPlayMs = 0;
};

struct AudioPlaybackRequest {
    AudioPlaybackRequestKind kind = AUDIO_PLAYBACK_REQ_NONE;
    union {
        uint16_t track;
        uint8_t volume;
        AudioPlaybackSlot slot;
        struct {
            uint16_t index;
            uint8_t bank;
            char page;
        } banked;
        struct {
            AudioPlaybackCategory category;
            AudioPlaybackSlot fallbackSlot;
            uint32_t randomValue;
        } categoryRequest;
    };
};

struct AudioPlaybackRandomContext {
    const AudioPlaybackConfig* config = nullptr;
    const AudioBindingCache* bindings = nullptr;
    bool catalogCapable = false;
    bool randomMode = false;
    bool domeSeqActive = false;
    uint32_t nowMs = 0;
    uint32_t lastRandMs = 0;
    uint8_t activeMood = 0;
    uint32_t randomValue = 0;
};

struct AudioPlaybackIntent {
    AudioPlaybackIntentKind kind = AUDIO_PLAYBACK_INTENT_NONE;
    AudioPlaybackNoneReason reason = AUDIO_PLAYBACK_NONE_OK;
    AudioPlaybackRequestKind requestKind = AUDIO_PLAYBACK_REQ_NONE;
    AudioPlaybackSlot slot = AUDIO_SLOT_NONE;
    AudioPlaybackCategory category = AUDIO_CATEGORY_NONE;
    bool fallbackSlotUsed = false;
    bool flatFallbackUsed = false;
    uint16_t track = 0;
    uint16_t index = 0;
    uint8_t bank = 0;
    char page = 'A';
    uint8_t volume = 0;
    bool markAudioActive = false;
    bool clearAudioActive = false;
    bool updateLastPlayMs = false;
    bool updateLastRandMs = false;
};

bool audioPlaybackNormalizeBanked(uint16_t index, uint8_t bank, char page,
                                  AudioPlaybackIntent* out);
bool audioPlaybackIsValidCategory(AudioPlaybackCategory category);
uint16_t audioPlaybackIntervalForMood(const AudioPlaybackConfig& config, uint8_t mood);
AudioPlaybackIntent audioPlaybackResolveRequest(const AudioPlaybackContext& context,
                                                const AudioPlaybackRequest& request);
AudioPlaybackIntent audioPlaybackResolveRandomTick(const AudioPlaybackRandomContext& context);
