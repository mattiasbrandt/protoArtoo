#include "drive_speed_preset.h"

#include "audio_task.h"
#include "config.h"
#include "config_cache.h"
#include "logging.h"
#include "robot_state.h"

extern bool saveConfigToNvs();

namespace {

static const char* TAG = "DrivePreset";

bool readSpeedPresetValueAndSlot(SpeedPresetId preset, int16_t* valueOut,
                                AudioPlaybackSlot* slotOut) {
    if (valueOut == nullptr || slotOut == nullptr) {
        return false;
    }

    int16_t slow;
    int16_t normal;
    int16_t turbo;

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    slow = cfg.drive.speedPresetSlow;
    normal = cfg.drive.speedPresetNormal;
    turbo = cfg.drive.speedPresetTurbo;

    if (slow < 0) slow = 0;
    if (normal < 0) normal = 0;
    if (turbo < 0) turbo = 0;
    if (slow > SPEED_LIMIT_MAX) slow = SPEED_LIMIT_MAX;
    if (normal > SPEED_LIMIT_MAX) normal = SPEED_LIMIT_MAX;
    if (turbo > SPEED_LIMIT_MAX) turbo = SPEED_LIMIT_MAX;

    *valueOut = speedPresetValueForId(preset, slow, normal, turbo);
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

bool applySpeedPresetRuntime(SpeedPresetId preset) {
    int16_t value = SPEED_PRESET_NORMAL;
    AudioPlaybackSlot slot = AUDIO_SLOT_NONE;
    if (!readSpeedPresetValueAndSlot(preset, &value, &slot)) {
        return false;
    }

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    cfg.drive.speedLimitMax = value;
    cfg.drive.speedPresetActive = preset;
    configCacheApply(cfg);

    audioQueuePlaySlot(slot, SRC_INTERNAL);
    return true;
}

bool applySpeedPresetPersisted(SpeedPresetId preset) {
    int16_t targetValue = SPEED_PRESET_NORMAL;
    AudioPlaybackSlot slot = AUDIO_SLOT_NONE;
    if (!readSpeedPresetValueAndSlot(preset, &targetValue, &slot)) {
        return false;
    }

    int16_t previousLimit = SPEED_PRESET_NORMAL;
    SpeedPresetId previousPreset = SpeedPresetId::Normal;
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    previousLimit = cfg.drive.speedLimitMax;
    previousPreset = cfg.drive.speedPresetActive;
    cfg.drive.speedLimitMax = targetValue;
    cfg.drive.speedPresetActive = preset;
    configCacheApply(cfg);

    if (!saveConfigToNvs()) {
        configCacheRead(&cfg);
        cfg.drive.speedLimitMax = previousLimit;
        cfg.drive.speedPresetActive = previousPreset;
        configCacheApply(cfg);
        PA_LOG_WARN(TAG, "Failed to persist speed preset change; runtime reverted");
        return false;
    }

    audioQueuePlaySlot(slot, SRC_INTERNAL);
    return true;
}
