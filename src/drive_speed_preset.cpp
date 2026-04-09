#include "drive_speed_preset.h"

#include "audio_task.h"
#include "config.h"
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

    taskENTER_CRITICAL(&robotStateMux);
    slow = robotState.cfg_speedPresetSlow;
    normal = robotState.cfg_speedPresetNormal;
    turbo = robotState.cfg_speedPresetTurbo;
    taskEXIT_CRITICAL(&robotStateMux);

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

    taskENTER_CRITICAL(&robotStateMux);
    robotState.cfg_speedLimitMax = value;
    robotState.cfg_speedPresetActive = preset;
    taskEXIT_CRITICAL(&robotStateMux);

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
    taskENTER_CRITICAL(&robotStateMux);
    previousLimit = robotState.cfg_speedLimitMax;
    previousPreset = robotState.cfg_speedPresetActive;
    robotState.cfg_speedLimitMax = targetValue;
    robotState.cfg_speedPresetActive = preset;
    taskEXIT_CRITICAL(&robotStateMux);

    if (!saveConfigToNvs()) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.cfg_speedLimitMax = previousLimit;
        robotState.cfg_speedPresetActive = previousPreset;
        taskEXIT_CRITICAL(&robotStateMux);
        PA_LOG_WARN(TAG, "Failed to persist speed preset change; runtime reverted");
        return false;
    }

    audioQueuePlaySlot(slot, SRC_INTERNAL);
    return true;
}
