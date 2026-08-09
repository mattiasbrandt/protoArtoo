// =============================================================================
// src/tasks/audio_task_step.cpp
//
// Audio Step Core (ADR 0014) — pure per-tick decisions for audioTask().
// See include/audio_task_step.h for the calling contract.
// =============================================================================

#include "audio_task_step.h"

#include "audio_config_map.h"  // audioSlotForDollar ($-command table, ADR 0013)
#include "logging.h"

// -----------------------------------------------------------------------------
// Intent state effects.
//
// Mirrors the adapter's execution guards: an intent the executor skips as
// invalid (defensive track/banked checks) must not update cadence timestamps
// or mode state either.
// -----------------------------------------------------------------------------
static bool intentExecutable(const AudioPlaybackIntent& intent) {
    if (intent.kind == AUDIO_PLAYBACK_INTENT_PLAY_FLAT && intent.track == 0) {
        return false;
    }
    if (intent.kind == AUDIO_PLAYBACK_INTENT_PLAY_BANKED &&
        (intent.index == 0 || intent.bank == 0 || intent.page < 'A' || intent.page > 'Z')) {
        return false;
    }
    return true;
}

static void applyIntentToState(AudioStepState& state, const AudioPlaybackIntent& intent,
                               uint32_t nowMs) {
    if (!intentExecutable(intent)) {
        return;
    }
    switch (intent.kind) {
        case AUDIO_PLAYBACK_INTENT_STOP:
            state.randomMode = false;
            break;
        case AUDIO_PLAYBACK_INTENT_TRACK_STOP:
            // randomMode intentionally untouched — Track Stop preserves idle mood
            // (ADR 0010); the cadence bump arrives via updateLastPlayMs below.
            break;
        case AUDIO_PLAYBACK_INTENT_SET_VOLUME:
            state.currentVol = intent.volume;
            break;
        case AUDIO_PLAYBACK_INTENT_RANDOM_ON:
            if (!state.randomMode) {
                state.lastRandMs = nowMs;
            }
            state.randomMode = true;
            break;
        case AUDIO_PLAYBACK_INTENT_RANDOM_OFF:
            state.randomMode = false;
            break;
        default:
            break;
    }
    if (intent.updateLastPlayMs) {
        state.lastPlayMs = nowMs;
    }
    if (intent.updateLastRandMs) {
        state.lastRandMs = nowMs;
    }
}

// -----------------------------------------------------------------------------
// Phase 1 — tick
// -----------------------------------------------------------------------------
AudioStepTickActions audioStepTick(AudioStepState& state, const AudioStepTickInputs& in) {
    AudioStepTickActions actions{};

    if (!in.audioEnabled) {
        // Stop active playback on the enabled -> disabled transition only.
        if (state.lastAudioEnabled && state.driverInitialized) {
            actions.stopDriver = true;
            actions.stopReason = AUDIO_STEP_STOP_DISABLED;
        }
        state.lastAudioEnabled = false;
        // randomMode must be cleared here — if it stays true, the random timer
        // would fire the moment audio is re-enabled.
        if (state.randomMode) {
            state.randomMode = false;
            actions.clearAudioActive = true;
        }
        actions.drainQueue = true;
        state.wasSleeping = in.sleepMode;
        return actions;
    }
    state.lastAudioEnabled = true;

    if (!state.driverInitialized) {
        state.currentVol = in.configVolume;
        actions.initDriver = true;
        // Sleep-entry detection resumes next iteration; nothing can be playing
        // and randomMode is false before the first successful init.
        return actions;
    }

    if (in.sleepMode && !state.wasSleeping) {
        actions.stopDriver = true;
        actions.stopReason = AUDIO_STEP_STOP_SLEEP_ENTRY;
        state.randomMode = false;
        actions.clearAudioActive = true;
    }
    state.wasSleeping = in.sleepMode;
    return actions;
}

// -----------------------------------------------------------------------------
// Phase 2 — init result
// -----------------------------------------------------------------------------
AudioStepInitResultActions audioStepInitResult(AudioStepState& state, bool beginOk,
                                               bool catalogCapable) {
    AudioStepInitResultActions actions{};
    if (!beginOk) {
        ++state.beginRetryCount;
        if (state.beginRetryCount >= AUDIO_STEP_INIT_MAX_RETRIES) {
            actions.giveUp = true;
            state.driverInitialized = true;  // inoperative, but stop retrying
            state.beginRetryCount = 0;
        }
        actions.skipRestOfTick = true;
        return actions;
    }
    state.beginRetryCount = 0;
    state.driverInitialized = true;
    actions.refreshBindings = catalogCapable;
    actions.seedModuleState = true;
    return actions;
}

// -----------------------------------------------------------------------------
// Phase 3 — command
// -----------------------------------------------------------------------------
static void resolvePlayback(AudioStepState& state, const AudioStepCommandInputs& in,
                            const AudioPlaybackRequest& request, bool withConfig,
                            AudioStepCommandActions* actions) {
    // Stop/volume requests resolve without config, matching the policy's
    // config-free handling of non-play requests.
    AudioPlaybackContext context{withConfig ? in.playback : nullptr,
                                 withConfig ? in.bindings : nullptr,
                                 withConfig ? in.catalogCapable : false, in.nowMs,
                                 state.lastPlayMs};
    actions->intent = audioPlaybackResolveRequest(context, request);
    actions->hasIntent = true;
    applyIntentToState(state, actions->intent, in.nowMs);
}

AudioStepCommandActions audioStepCommand(AudioStepState& state,
                                         const AudioStepCommandInputs& in,
                                         const AudioCommand& cmd) {
    AudioStepCommandActions actions{};
    switch (cmd.type) {
        case AUDIO_CMD_DOLLAR: {
            if (in.sleepMode) {
                actions.ignored = AUDIO_STEP_IGNORE_SLEEP;
                break;
            }
            AudioAction action = parseAudioDollar(cmd.dollar, *in.named);

            // Log unrecognized $ commands (Class 1: input validation failure).
            // See docs/core-error-signalling.md for convention.
            if (action.type == AUDIO_ACTION_NONE && cmd.dollar[0] == '$' &&
                cmd.dollar[1] != '\0') {
                PA_LOG_WARN("audio", "unrecognized $ command: %s", cmd.dollar);
            }

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
                request.volume = audioClampVolume(state.currentVol + 1);
            } else if (action.type == AUDIO_ACTION_VOLUME_DOWN) {
                request.kind = AUDIO_PLAYBACK_REQ_SET_VOLUME;
                request.volume = (state.currentVol > AUDIO_VOLUME_MIN)
                                     ? (uint8_t)(state.currentVol - 1)
                                     : AUDIO_VOLUME_MIN;
            } else {
                request.kind = AUDIO_PLAYBACK_REQ_NONE;
            }
            resolvePlayback(state, in, request, true, &actions);
            break;
        }

        case AUDIO_CMD_PLAY_TRACK: {
            if (in.sleepMode) {
                actions.ignored = AUDIO_STEP_IGNORE_SLEEP;
                break;
            }
            AudioPlaybackRequest request{};
            request.kind = AUDIO_PLAYBACK_REQ_DIRECT_TRACK;
            request.track = cmd.track;
            resolvePlayback(state, in, request, true, &actions);
            break;
        }

        case AUDIO_CMD_PLAY_SLOT: {
            if (in.sleepMode) {
                actions.ignored = AUDIO_STEP_IGNORE_SLEEP;
                break;
            }
            AudioPlaybackRequest request{};
            request.kind = AUDIO_PLAYBACK_REQ_SLOT;
            request.slot = cmd.slot;
            resolvePlayback(state, in, request, true, &actions);
            break;
        }

        case AUDIO_CMD_PLAY_TRACK_BANKED: {
            if (in.sleepMode) {
                actions.ignored = AUDIO_STEP_IGNORE_SLEEP;
                break;
            }
            AudioPlaybackRequest request{};
            request.kind = AUDIO_PLAYBACK_REQ_DIRECT_BANKED;
            request.banked.index = cmd.banked.index;
            request.banked.bank = cmd.banked.bank;
            request.banked.page = cmd.banked.page;
            resolvePlayback(state, in, request, true, &actions);
            break;
        }

        case AUDIO_CMD_PLAY_CATEGORY: {
            if (in.sleepMode) {
                actions.ignored = AUDIO_STEP_IGNORE_SLEEP;
                break;
            }
            AudioPlaybackRequest request{};
            request.kind = AUDIO_PLAYBACK_REQ_CATEGORY;
            request.categoryRequest.category = cmd.category.category;
            request.categoryRequest.fallbackSlot = cmd.category.fallbackSlot;
            request.categoryRequest.randomValue = in.randomValue;
            resolvePlayback(state, in, request, true, &actions);
            break;
        }

        case AUDIO_CMD_STOP: {
            AudioPlaybackRequest request{};
            request.kind = AUDIO_PLAYBACK_REQ_STOP;
            resolvePlayback(state, in, request, false, &actions);
            break;
        }

        case AUDIO_CMD_TRACK_STOP: {
            AudioPlaybackRequest request{};
            request.kind = AUDIO_PLAYBACK_REQ_TRACK_STOP;
            resolvePlayback(state, in, request, false, &actions);
            break;
        }

        case AUDIO_CMD_SET_VOLUME: {
            AudioPlaybackRequest request{};
            request.kind = AUDIO_PLAYBACK_REQ_SET_VOLUME;
            request.volume = cmd.volume;  // clamped by helper before enqueue
            resolvePlayback(state, in, request, false, &actions);
            break;
        }

        case AUDIO_CMD_REFRESH_CATALOG:
            if (!in.catalogCapable) {
                actions.ignored = AUDIO_STEP_IGNORE_UNSUPPORTED_BACKEND;
                break;
            }
            actions.refreshCatalog = true;
            break;

        case AUDIO_CMD_REFRESH_BINDINGS:
            if (!in.catalogCapable) {
                actions.ignored = AUDIO_STEP_IGNORE_UNSUPPORTED_BACKEND;
                break;
            }
            actions.refreshBindings = true;
            break;

        case AUDIO_CMD_QUERY_STATUS:
            actions.queryStatus = true;
            break;
    }
    return actions;
}

// -----------------------------------------------------------------------------
// Phase 4 — idle
// -----------------------------------------------------------------------------
AudioStepIdleActions audioStepIdle(AudioStepState& state, const AudioStepIdleInputs& in) {
    AudioStepIdleActions actions{};

    if (state.randomMode && !in.sleepMode) {
        AudioPlaybackRandomContext context{in.playback,     in.bindings, in.catalogCapable,
                                           state.randomMode, in.domeSeqActive,
                                           in.nowMs,        state.lastRandMs,
                                           in.activeMood,   in.randomValue};
        actions.intent = audioPlaybackResolveRandomTick(context);
        actions.hasIntent = true;
        applyIntentToState(state, actions.intent, in.nowMs);
    }

    if (!in.webOtaActive && in.querySafePlayingCapable &&
        (uint32_t)(in.nowMs - state.lastAutoQueryMs) >= AUDIO_STEP_AUTO_QUERY_INTERVAL_MS) {
        state.lastAutoQueryMs = in.nowMs;
        actions.autoQuery = true;
    }

    return actions;
}
