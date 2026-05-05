#include "audio_playback_policy.h"

#include <ctype.h>

static AudioPlaybackIntent makeNone(AudioPlaybackRequestKind requestKind,
                                    AudioPlaybackNoneReason reason) {
    AudioPlaybackIntent intent{};
    intent.kind = AUDIO_PLAYBACK_INTENT_NONE;
    intent.requestKind = requestKind;
    intent.reason = reason;
    return intent;
}

static bool normalizeBankedValues(uint16_t index, uint8_t bank, char page, uint16_t* indexOut,
                                  uint8_t* bankOut, char* pageOut) {
    if (index == 0 || bank == 0 || indexOut == nullptr || bankOut == nullptr ||
        pageOut == nullptr) {
        return false;
    }
    char normalizedPage = (char)toupper((unsigned char)page);
    if (normalizedPage < 'A' || normalizedPage > 'Z') {
        return false;
    }
    *indexOut = index;
    *bankOut = bank;
    *pageOut = normalizedPage;
    return true;
}

bool audioPlaybackNormalizeBanked(uint16_t index, uint8_t bank, char page,
                                  AudioPlaybackIntent* out) {
    if (out == nullptr) {
        return false;
    }
    uint16_t normalizedIndex = 0;
    uint8_t normalizedBank = 0;
    char normalizedPage = 'A';
    if (!normalizeBankedValues(index, bank, page, &normalizedIndex, &normalizedBank,
                               &normalizedPage)) {
        return false;
    }
    out->index = normalizedIndex;
    out->bank = normalizedBank;
    out->page = normalizedPage;
    return true;
}

bool audioPlaybackIsValidCategory(AudioPlaybackCategory category) {
    return (uint8_t)category < (uint8_t)AUDIO_CATEGORY_COUNT;
}

uint16_t audioPlaybackIntervalForMood(const AudioPlaybackConfig& config, uint8_t mood) {
    switch (mood) {
        case 10:
            return config.intervalQuietS;
        case 13:
            return config.intervalMidS;
        case 14:
            return config.intervalAwakeS;
        default:
            return config.intervalFullS;
    }
}

static bool antiSpamAllows(const AudioPlaybackContext& context) {
    return (uint32_t)(context.nowMs - context.lastPlayMs) >= AUDIO_PLAYBACK_ANTI_SPAM_MS;
}

static AudioPlaybackIntent makeFlat(AudioPlaybackRequestKind requestKind, uint16_t track) {
    AudioPlaybackIntent intent{};
    intent.kind = AUDIO_PLAYBACK_INTENT_PLAY_FLAT;
    intent.requestKind = requestKind;
    intent.track = track;
    intent.markAudioActive = true;
    intent.updateLastPlayMs = true;
    intent.updateLastRandMs = true;
    return intent;
}

static AudioPlaybackIntent makeBanked(AudioPlaybackRequestKind requestKind, uint16_t index,
                                      uint8_t bank, char page) {
    AudioPlaybackIntent intent{};
    intent.kind = AUDIO_PLAYBACK_INTENT_PLAY_BANKED;
    intent.requestKind = requestKind;
    if (!audioPlaybackNormalizeBanked(index, bank, page, &intent)) {
        return makeNone(requestKind, AUDIO_PLAYBACK_NONE_INVALID_BANKED);
    }
    intent.markAudioActive = true;
    intent.updateLastPlayMs = true;
    intent.updateLastRandMs = true;
    return intent;
}

static AudioPlaybackIntent resolveSlotNoGate(const AudioPlaybackContext& context,
                                             AudioPlaybackSlot slot) {
    if (context.config == nullptr || (uint8_t)slot == 0 || (uint8_t)slot >= AUDIO_SLOT_COUNT) {
        return makeNone(AUDIO_PLAYBACK_REQ_SLOT, AUDIO_PLAYBACK_NONE_UNKNOWN_SLOT);
    }

    if (context.catalogCapable && context.bindings != nullptr) {
        const AudioChirpSlotBinding& binding = context.bindings->slots[(uint8_t)slot];
        if (binding.valid) {
            AudioPlaybackIntent banked =
                makeBanked(AUDIO_PLAYBACK_REQ_SLOT, binding.index, binding.bank, binding.page);
            if (banked.kind == AUDIO_PLAYBACK_INTENT_PLAY_BANKED) {
                banked.slot = slot;
                return banked;
            }
        }
    }

    const uint16_t track = context.config->slotTracks[(uint8_t)slot];
    if (track == 0) {
        AudioPlaybackIntent none = makeNone(AUDIO_PLAYBACK_REQ_SLOT, AUDIO_PLAYBACK_NONE_TRACK_ZERO);
        none.slot = slot;
        return none;
    }

    AudioPlaybackIntent flat = makeFlat(AUDIO_PLAYBACK_REQ_SLOT, track);
    flat.slot = slot;
    return flat;
}

static bool selectTrackInCategory(const SoundCategoryRange& range, uint32_t randomValue,
                                  uint16_t* trackOut) {
    if (trackOut == nullptr || range.lo == 0 || range.lo > range.hi) {
        return false;
    }
    const uint32_t span = (uint32_t)range.hi - (uint32_t)range.lo + 1U;
    *trackOut = (uint16_t)((uint32_t)range.lo + (randomValue % span));
    return true;
}

static AudioPlaybackIntent resolveCategoryNoGate(const AudioPlaybackContext& context,
                                                 AudioPlaybackCategory category,
                                                 AudioPlaybackSlot fallbackSlot,
                                                 uint32_t randomValue) {
    if (context.config == nullptr || !audioPlaybackIsValidCategory(category)) {
        return makeNone(AUDIO_PLAYBACK_REQ_CATEGORY, AUDIO_PLAYBACK_NONE_UNKNOWN_CATEGORY);
    }

    uint16_t track = 0;
    if (selectTrackInCategory(context.config->categoryRanges[(uint8_t)category], randomValue,
                              &track) &&
        track != 0) {
        if (context.catalogCapable && context.bindings != nullptr) {
            const AudioChirpCategoryBinding& binding = context.bindings->categories[(uint8_t)category];
            if (binding.valid) {
                AudioPlaybackIntent banked =
                    makeBanked(AUDIO_PLAYBACK_REQ_CATEGORY, track, binding.bank, binding.page);
                if (banked.kind == AUDIO_PLAYBACK_INTENT_PLAY_BANKED) {
                    banked.track = track;
                    banked.category = category;
                    return banked;
                }
            }
        }
        AudioPlaybackIntent flat = makeFlat(AUDIO_PLAYBACK_REQ_CATEGORY, track);
        flat.category = category;
        return flat;
    }

    if (fallbackSlot != AUDIO_SLOT_NONE) {
        AudioPlaybackIntent fallback = resolveSlotNoGate(context, fallbackSlot);
        fallback.requestKind = AUDIO_PLAYBACK_REQ_CATEGORY;
        fallback.category = category;
        fallback.fallbackSlotUsed = true;
        return fallback;
    }

    AudioPlaybackIntent none =
        makeNone(AUDIO_PLAYBACK_REQ_CATEGORY, AUDIO_PLAYBACK_NONE_CATEGORY_EMPTY);
    none.category = category;
    return none;
}

AudioPlaybackIntent audioPlaybackResolveRequest(const AudioPlaybackContext& context,
                                                const AudioPlaybackRequest& request) {
    switch (request.kind) {
        case AUDIO_PLAYBACK_REQ_DIRECT_TRACK:
            if (!antiSpamAllows(context)) {
                return makeNone(request.kind, AUDIO_PLAYBACK_NONE_ANTI_SPAM);
            }
            if (request.track == 0) {
                return makeNone(request.kind, AUDIO_PLAYBACK_NONE_TRACK_ZERO);
            }
            return makeFlat(request.kind, request.track);

        case AUDIO_PLAYBACK_REQ_DIRECT_BANKED:
            if (!antiSpamAllows(context)) {
                return makeNone(request.kind, AUDIO_PLAYBACK_NONE_ANTI_SPAM);
            }
            return makeBanked(request.kind, request.banked.index, request.banked.bank,
                              request.banked.page);

        case AUDIO_PLAYBACK_REQ_SLOT:
            if (!antiSpamAllows(context)) {
                return makeNone(request.kind, AUDIO_PLAYBACK_NONE_ANTI_SPAM);
            }
            return resolveSlotNoGate(context, request.slot);

        case AUDIO_PLAYBACK_REQ_CATEGORY:
            if (!antiSpamAllows(context)) {
                return makeNone(request.kind, AUDIO_PLAYBACK_NONE_ANTI_SPAM);
            }
            return resolveCategoryNoGate(context, request.categoryRequest.category,
                                         request.categoryRequest.fallbackSlot,
                                         request.categoryRequest.randomValue);

        case AUDIO_PLAYBACK_REQ_STOP: {
            AudioPlaybackIntent intent{};
            intent.kind = AUDIO_PLAYBACK_INTENT_STOP;
            intent.requestKind = request.kind;
            intent.clearAudioActive = true;
            return intent;
        }

        case AUDIO_PLAYBACK_REQ_SET_VOLUME: {
            AudioPlaybackIntent intent{};
            intent.kind = AUDIO_PLAYBACK_INTENT_SET_VOLUME;
            intent.requestKind = request.kind;
            intent.volume = request.volume;
            return intent;
        }

        case AUDIO_PLAYBACK_REQ_RANDOM_ON: {
            AudioPlaybackIntent intent{};
            intent.kind = AUDIO_PLAYBACK_INTENT_RANDOM_ON;
            intent.requestKind = request.kind;
            return intent;
        }

        case AUDIO_PLAYBACK_REQ_RANDOM_OFF: {
            AudioPlaybackIntent intent{};
            intent.kind = AUDIO_PLAYBACK_INTENT_RANDOM_OFF;
            intent.requestKind = request.kind;
            return intent;
        }

        case AUDIO_PLAYBACK_REQ_NONE:
        default:
            return makeNone(request.kind, AUDIO_PLAYBACK_NONE_OK);
    }
}

AudioPlaybackIntent audioPlaybackResolveRandomTick(const AudioPlaybackRandomContext& context) {
    if (!context.randomMode) {
        return makeNone(AUDIO_PLAYBACK_REQ_RANDOM_TICK, AUDIO_PLAYBACK_NONE_RANDOM_DISABLED);
    }
    if (context.config == nullptr) {
        return makeNone(AUDIO_PLAYBACK_REQ_RANDOM_TICK, AUDIO_PLAYBACK_NONE_CATEGORY_EMPTY);
    }

    const uint16_t intSec = audioPlaybackIntervalForMood(*context.config, context.activeMood);
    if (intSec == 0) {
        AudioPlaybackIntent intent =
            makeNone(AUDIO_PLAYBACK_REQ_RANDOM_TICK, AUDIO_PLAYBACK_NONE_INTERVAL_ZERO);
        intent.updateLastRandMs = true;
        return intent;
    }

    const uint32_t intervalMs = (uint32_t)intSec * 1000u;
    if ((uint32_t)(context.nowMs - context.lastRandMs) < intervalMs) {
        return makeNone(AUDIO_PLAYBACK_REQ_RANDOM_TICK, AUDIO_PLAYBACK_NONE_INTERVAL_NOT_READY);
    }

    if (context.domeSeqActive) {
        AudioPlaybackIntent intent =
            makeNone(AUDIO_PLAYBACK_REQ_RANDOM_TICK, AUDIO_PLAYBACK_NONE_DOME_SEQUENCE_ACTIVE);
        intent.updateLastRandMs = true;
        return intent;
    }

    uint16_t track = 0;
    bool usedFlatFallback = false;
    uint8_t selectedCategory = 0xFF;
    const bool selected =
        selectRandomTrackForMood(context.activeMood, context.config->moodMasks,
                                 context.config->categoryRanges, AUDIO_CATEGORY_COUNT,
                                 context.config->randMin, context.config->randMax,
                                 context.randomValue, &track, &usedFlatFallback, &selectedCategory);

    if (!selected || track == 0) {
        AudioPlaybackIntent intent =
            makeNone(AUDIO_PLAYBACK_REQ_RANDOM_TICK, AUDIO_PLAYBACK_NONE_CATEGORY_EMPTY);
        intent.updateLastRandMs = true;
        return intent;
    }

    if (!usedFlatFallback && selectedCategory != 0xFF && context.catalogCapable &&
        context.bindings != nullptr) {
        const AudioChirpCategoryBinding& binding = context.bindings->categories[selectedCategory];
        if (binding.valid) {
            AudioPlaybackIntent banked =
                makeBanked(AUDIO_PLAYBACK_REQ_RANDOM_TICK, track, binding.bank, binding.page);
            if (banked.kind == AUDIO_PLAYBACK_INTENT_PLAY_BANKED) {
                banked.track = track;
                banked.category = (AudioPlaybackCategory)selectedCategory;
                return banked;
            }
        }
    }

    AudioPlaybackIntent flat = makeFlat(AUDIO_PLAYBACK_REQ_RANDOM_TICK, track);
    flat.flatFallbackUsed = usedFlatFallback;
    if (selectedCategory != 0xFF) {
        flat.category = (AudioPlaybackCategory)selectedCategory;
    }
    return flat;
}
