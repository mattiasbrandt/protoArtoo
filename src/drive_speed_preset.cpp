#include "drive_speed_preset.h"

#include "config_write_lock.h"  // applySpeedPresetPersisted() is the persisted preset's Write Window
#include "audio_task.h"
#include "config.h"
#include "config_cache.h"
#include "logging.h"
#include "robot_state.h"

extern bool saveConfigToNvs();

namespace {

static const char* TAG = "DrivePreset";

// The cue a preset announces itself with. False for a value that is not a
// preset, which both callers refuse before touching the cache.
bool speedPresetSlot(SpeedPresetId preset, AudioPlaybackSlot* slotOut) {
    if (slotOut == nullptr) {
        return false;
    }
    switch (preset) {
        case SpeedPresetId::Slow:
            *slotOut = AUDIO_SLOT_SYS_MODE_SLOW;
            return true;
        case SpeedPresetId::Normal:
            *slotOut = AUDIO_SLOT_SYS_MODE_NORMAL;
            return true;
        case SpeedPresetId::Turbo:
            *slotOut = AUDIO_SLOT_SYS_MODE_TURBO;
            return true;
        default:
            return false;
    }
}

}  // namespace

// The RC speed preset, on RCInputTask (Core 1) and from the action-test
// dispatch. Both fields go through one configCacheMux section and nothing
// else is touched: no ConfigWriteLock, which a real-time loop must never take,
// and no whole-snapshot write that could put back fields a config POST has
// just committed (#417).
bool applySpeedPresetRuntime(SpeedPresetId preset) {
    AudioPlaybackSlot slot = AUDIO_SLOT_NONE;
    if (!speedPresetSlot(preset, &slot)) {
        return false;
    }

    configCacheSelectSpeedPreset(preset);

    audioQueuePlaySlot(slot, SRC_INTERNAL);
    return true;
}

// The persisted preset, from POST /api/drive/speed-preset and the Console's
// drive.action.speed-preset-* (Core 0), and its Write Window (ADR 0011,
// amended 2026-09-24): it writes the cache and then the whole of it to NVS,
// so it holds the config write lock across both - the previous pair it may
// have to restore is read inside it too. Both callers call this and hold no
// lock of their own. A lock that cannot be taken is a failed write, which
// both callers already answer as one.
bool applySpeedPresetPersisted(SpeedPresetId preset) {
    AudioPlaybackSlot slot = AUDIO_SLOT_NONE;
    if (!speedPresetSlot(preset, &slot)) {
        return false;
    }

    {
        ConfigWriteLock lock;
        if (!lock.acquired()) {
            PA_LOG_WARN(TAG, "Speed preset not changed: config write busy");
            return false;
        }

        ConfigSnapshot cfg = {};
        configCacheRead(&cfg);
        const int16_t previousLimit = cfg.drive.speedLimitMax;
        const SpeedPresetId previousPreset = cfg.drive.speedPresetActive;

        configCacheSelectSpeedPreset(preset);

        if (!saveConfigToNvs()) {
            configCacheSetSpeedLimit(previousLimit, previousPreset);
            PA_LOG_WARN(TAG, "Failed to persist speed preset change; runtime reverted");
            return false;
        }
    }

    audioQueuePlaySlot(slot, SRC_INTERNAL);
    return true;
}
