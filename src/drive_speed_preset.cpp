#include "drive_speed_preset.h"

#include "audio_task.h"
#include "config.h"
#include "logging.h"
#include "robot_state.h"
#include "system_sounds.h"

extern bool saveConfigToNvs();

namespace {

static const char* TAG = "DrivePreset";

bool readSpeedPresetValueAndTrack(SpeedPresetId preset, int16_t* valueOut, uint16_t* trackOut) {
    if (valueOut == nullptr || trackOut == nullptr) {
        return false;
    }

    int16_t slow;
    int16_t normal;
    int16_t turbo;
    uint16_t modeSlowTrack = 0;
    uint16_t modeNormalTrack = 0;
    uint16_t modeTurboTrack = 0;

    taskENTER_CRITICAL(&robotStateMux);
    slow = robotState.cfg_speedPresetSlow;
    normal = robotState.cfg_speedPresetNormal;
    turbo = robotState.cfg_speedPresetTurbo;
    modeSlowTrack = robotState.cfg_snd_sys_mode_s;
    modeNormalTrack = robotState.cfg_snd_sys_mode_n;
    modeTurboTrack = robotState.cfg_snd_sys_mode_t;
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
            *trackOut = modeSlowTrack;
            return true;
        case SpeedPresetId::Normal:
            *trackOut = modeNormalTrack;
            return true;
        case SpeedPresetId::Turbo:
            *trackOut = modeTurboTrack;
            return true;
        default:
            return false;
    }
}

}  // namespace

bool applySpeedPresetRuntime(SpeedPresetId preset) {
    int16_t value = SPEED_PRESET_NORMAL;
    uint16_t track = 0;
    if (!readSpeedPresetValueAndTrack(preset, &value, &track)) {
        return false;
    }

    taskENTER_CRITICAL(&robotStateMux);
    robotState.cfg_speedLimitMax = value;
    robotState.cfg_speedPresetActive = preset;
    taskEXIT_CRITICAL(&robotStateMux);

    queueSystemSoundTrack(track, audioQueuePlayTrack, SRC_INTERNAL);
    return true;
}

bool applySpeedPresetPersisted(SpeedPresetId preset) {
    int16_t targetValue = SPEED_PRESET_NORMAL;
    uint16_t targetTrack = 0;
    if (!readSpeedPresetValueAndTrack(preset, &targetValue, &targetTrack)) {
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

    queueSystemSoundTrack(targetTrack, audioQueuePlayTrack, SRC_INTERNAL);
    return true;
}
