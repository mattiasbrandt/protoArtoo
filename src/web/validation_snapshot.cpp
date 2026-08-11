// =============================================================================
// src/web/validation_snapshot.cpp
//
// Validation snapshot capture + JSON serialization for /api/validation.
// =============================================================================

#include "../../include/validation_snapshot.h"

#include <Arduino.h>

#include "../../include/config_cache.h"
#include "../../include/robot_state.h"

namespace {

const char* rcInputModeLabel(RcInputMode mode) {
    switch (mode) {
        case RC_INPUT_STANDARD_PWM:
            return "standard_pwm";
        case RC_INPUT_SINGLE_SBUS:
            return "single_sbus";
        case RC_INPUT_DUAL_SBUS:
        default:
            return "dual_sbus";
    }
}

bool rcSourceEnabledForMode(RcBindingSource source, RcInputMode mode, bool enableRcCh1, bool enableRcCh2,
                            bool anyPwmEnabled, bool useCh2) {
    switch (source) {
        case RC_BINDING_PWM:
            return mode == RC_INPUT_STANDARD_PWM && anyPwmEnabled;
        case RC_BINDING_SBUS1:
            if (mode == RC_INPUT_SINGLE_SBUS) return !useCh2 && enableRcCh1;
            return mode == RC_INPUT_DUAL_SBUS && enableRcCh1;
        case RC_BINDING_SBUS2:
            if (mode == RC_INPUT_SINGLE_SBUS) return useCh2 && enableRcCh2;
            return mode == RC_INPUT_DUAL_SBUS && enableRcCh2;
        case RC_BINDING_NONE:
        default:
            return false;
    }
}

uint32_t currentMillis() {
#ifdef ARDUINO
    return millis();
#else
    return 0;
#endif
}

uint32_t sourceAgeMs(uint32_t nowMs, uint32_t lastSeenMs) {
    if (lastSeenMs == 0) {
        return 0;
    }
    return nowMs - lastSeenMs;
}

}  // namespace

void captureValidationSnapshot(ValidationSnapshot* out) {
    if (out == nullptr) {
        return;
    }

    ValidationSnapshot snap = {};
    const uint32_t nowMs = currentMillis();

    FailsafeDiagnostics diag = {};
    bool sbus2SignalLost;
    bool sbus2HwFailsafe;

    bool enableS3DomeCtrl;
    uint32_t domeHbRx;
    uint32_t bodyHbTx;
    uint32_t domeLastSeenMs;

    bool enableS2Sound;
    bool audioActive;
    uint8_t activeMood;
    uint16_t randMin;
    uint16_t randMax;
    uint16_t intQuiet;
    uint16_t intMid;
    uint16_t intFull;
    uint16_t intAwake;

    RcInputMode rcMode;
    uint32_t timeoutMs;
    bool enableRcCh1;
    bool enableRcCh2;
    bool enableRcCh3;
    bool enableRcCh4;
    bool enableRcCh5;
    bool enableRcCh6;
    bool sbusUseCh2;
    uint32_t lastPwmMs;
    uint32_t lastSbus1Ms;
    uint32_t lastSbus2Ms;

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    RcInputActiveConfig activeRc = {};
    configCacheReadActiveRcInput(&activeRc);

    taskENTER_CRITICAL(&robotStateMux);
    copyFailsafeDiagnosticsLocked(&diag);
    sbus2SignalLost = robotState.sbus2SignalLost;
    sbus2HwFailsafe = robotState.sbus2HwFailsafe;

    enableS3DomeCtrl = cfg.system.enable_s3_dome_ctrl;
    domeHbRx = robotState.domeHbRx;
    bodyHbTx = robotState.bodyHbTx;
    domeLastSeenMs = robotState.domeLastSeenMs;

    enableS2Sound = cfg.system.enable_s2_sound;
    audioActive = robotState.audioActive;
    activeMood = robotState.activeMood;
    randMin = cfg.audio.snd_rand_min;
    randMax = cfg.audio.snd_rand_max;
    intQuiet = cfg.audio.snd_int_quiet;
    intMid = cfg.audio.snd_int_mid;
    intFull = cfg.audio.snd_int_full;
    intAwake = cfg.audio.snd_int_awake;

    rcMode = static_cast<RcInputMode>(activeRc.mode);
    timeoutMs = cfg.drive.sbusTimeoutMs;
    enableRcCh1 = activeRc.enableRc[0];
    enableRcCh2 = activeRc.enableRc[1];
    enableRcCh3 = activeRc.enableRc[2];
    enableRcCh4 = activeRc.enableRc[3];
    enableRcCh5 = activeRc.enableRc[4];
    enableRcCh6 = activeRc.enableRc[5];
    sbusUseCh2 = activeRc.useCh2;
    lastPwmMs = robotState.lastPwmMs;
    lastSbus1Ms = robotState.lastSbus1Ms;
    lastSbus2Ms = robotState.lastSbus2Ms;
    taskEXIT_CRITICAL(&robotStateMux);

    snap.updatedMs = nowMs;

    snap.drive.estop = diag.estop;
    snap.drive.webDriveExpired = diag.webDriveExpired;
    snap.drive.sbusSignalLost = diag.sbusSignalLost;
    snap.drive.sbusHwFailsafe = diag.sbusHwFailsafe;
    snap.drive.failsafeSource = diag.failsafeSource;
    snap.drive.failsafeCount = diag.failsafeTriggerCount;
    snap.drive.triggerMs = diag.failsafeLastTriggerMs;
    snap.drive.zeroMs = diag.failsafeLastZeroOutputMs;
    snap.drive.triggerToZeroMs = diag.failsafeLastTriggerToZeroMs;
    snap.drive.watchdogMs = diag.failsafeLastWatchdogMs;
    snap.drive.triggerSource = diag.failsafeLastTriggerSource;

    snap.domeLink.hbTx = bodyHbTx;
    snap.domeLink.hbRx = domeHbRx;
    if (!enableS3DomeCtrl) {
        snap.domeLink.state = "disabled";
        snap.domeLink.lastRxMs = -1;
    } else if (domeLastSeenMs == 0) {
        snap.domeLink.state = "not_seen";
        snap.domeLink.lastRxMs = -1;
    } else {
        uint32_t ageMs = nowMs - domeLastSeenMs;
        snap.domeLink.state = ageMs < 5000UL ? "connected" : "lost";
        snap.domeLink.lastRxMs = (int32_t)ageMs;
    }

    snap.audio.enabled = enableS2Sound;
    snap.audio.active = audioActive;
    snap.audio.activeMood = activeMood;
    snap.audio.randomMin = randMin;
    snap.audio.randomMax = randMax;
    snap.audio.intervalQuietS = intQuiet;
    snap.audio.intervalMidS = intMid;
    snap.audio.intervalFullS = intFull;
    snap.audio.intervalAwakeS = intAwake;

    const bool anyPwmEnabled =
        enableRcCh1 || enableRcCh2 || enableRcCh3 || enableRcCh4 || enableRcCh5 || enableRcCh6;

    const uint32_t sbus1Age = sourceAgeMs(nowMs, lastSbus1Ms);
    const uint32_t sbus2Age = sourceAgeMs(nowMs, lastSbus2Ms);
    const uint32_t pwmAge = sourceAgeMs(nowMs, lastPwmMs);

    snap.rc.mode = rcInputModeLabel(rcMode);
    snap.rc.timeoutMs = timeoutMs;
    snap.rc.sourceCount = VALIDATION_RC_SOURCE_CAPACITY;

    ValidationRcSourceSnapshot& sbus1 = snap.rc.sources[0];
    sbus1.key = "sbus1";
    sbus1.enabled = rcSourceEnabledForMode(RC_BINDING_SBUS1, rcMode, enableRcCh1, enableRcCh2, anyPwmEnabled, sbusUseCh2);
    sbus1.linked = sbus1.enabled && lastSbus1Ms > 0 && !diag.sbusSignalLost && sbus1Age <= timeoutMs;
    sbus1.signalLost = sbus1.enabled ? diag.sbusSignalLost : false;
    sbus1.failsafe = sbus1.enabled ? diag.sbusHwFailsafe : false;
    sbus1.ageMs = sbus1Age;

    ValidationRcSourceSnapshot& sbus2 = snap.rc.sources[1];
    sbus2.key = "sbus2";
    sbus2.enabled = rcSourceEnabledForMode(RC_BINDING_SBUS2, rcMode, enableRcCh1, enableRcCh2, anyPwmEnabled, sbusUseCh2);
    sbus2.linked = sbus2.enabled && lastSbus2Ms > 0 && !sbus2SignalLost && sbus2Age <= timeoutMs;
    sbus2.signalLost = sbus2.enabled ? sbus2SignalLost : false;
    sbus2.failsafe = sbus2.enabled ? sbus2HwFailsafe : false;
    sbus2.ageMs = sbus2Age;

    ValidationRcSourceSnapshot& pwm = snap.rc.sources[2];
    pwm.key = "pwm";
    pwm.enabled = rcSourceEnabledForMode(RC_BINDING_PWM, rcMode, enableRcCh1, enableRcCh2, anyPwmEnabled, sbusUseCh2);
    pwm.linked = pwm.enabled && lastPwmMs > 0 && pwmAge <= timeoutMs;
    pwm.signalLost = pwm.enabled && lastPwmMs > 0 && pwmAge > timeoutMs;
    pwm.failsafe = false;
    pwm.ageMs = pwmAge;

    *out = snap;
}

bool populateValidationJson(JsonDocument& doc, const ValidationSnapshot& snap) {
    if (snap.rc.mode == nullptr || snap.domeLink.state == nullptr) {
        return false;
    }

    doc.clear();

    JsonObject root = doc.to<JsonObject>();
    if (root.isNull()) {
        return false;
    }

    root["updatedMs"] = snap.updatedMs;

    JsonObject drive = root["drive"].to<JsonObject>();
    if (drive.isNull()) {
        return false;
    }
    drive["estop"] = snap.drive.estop;
    drive["webDriveExpired"] = snap.drive.webDriveExpired;
    drive["sbusSignalLost"] = snap.drive.sbusSignalLost;
    drive["sbusHwFailsafe"] = snap.drive.sbusHwFailsafe;
    drive["failsafeSource"] = (int)snap.drive.failsafeSource;
    drive["failsafeCount"] = snap.drive.failsafeCount;
    drive["triggerMs"] = snap.drive.triggerMs;
    drive["zeroMs"] = snap.drive.zeroMs;
    drive["triggerToZeroMs"] = snap.drive.triggerToZeroMs;
    drive["watchdogMs"] = snap.drive.watchdogMs;
    drive["triggerSource"] = (int)snap.drive.triggerSource;

    JsonObject domeLink = root["domeLink"].to<JsonObject>();
    if (domeLink.isNull()) {
        return false;
    }
    domeLink["state"] = snap.domeLink.state;
    domeLink["hbTx"] = snap.domeLink.hbTx;
    domeLink["hbRx"] = snap.domeLink.hbRx;
    domeLink["lastRxMs"] = snap.domeLink.lastRxMs;

    JsonObject audio = root["audio"].to<JsonObject>();
    if (audio.isNull()) {
        return false;
    }
    audio["enabled"] = snap.audio.enabled;
    audio["active"] = snap.audio.active;
    audio["activeMood"] = snap.audio.activeMood;
    audio["randomMin"] = snap.audio.randomMin;
    audio["randomMax"] = snap.audio.randomMax;
    audio["intervalQuietS"] = snap.audio.intervalQuietS;
    audio["intervalMidS"] = snap.audio.intervalMidS;
    audio["intervalFullS"] = snap.audio.intervalFullS;
    audio["intervalAwakeS"] = snap.audio.intervalAwakeS;

    JsonObject rc = root["rc"].to<JsonObject>();
    if (rc.isNull()) {
        return false;
    }
    rc["mode"] = snap.rc.mode;
    rc["timeoutMs"] = snap.rc.timeoutMs;

    JsonObject sources = rc["sources"].to<JsonObject>();
    if (sources.isNull()) {
        return false;
    }

    const size_t sourceCount = (snap.rc.sourceCount <= VALIDATION_RC_SOURCE_CAPACITY)
                                   ? snap.rc.sourceCount
                                   : VALIDATION_RC_SOURCE_CAPACITY;
    for (size_t i = 0; i < sourceCount; ++i) {
        const ValidationRcSourceSnapshot& source = snap.rc.sources[i];
        JsonObject sourceObj = sources[source.key].to<JsonObject>();
        if (sourceObj.isNull()) {
            return false;
        }
        sourceObj["enabled"] = source.enabled;
        sourceObj["linked"] = source.linked;
        sourceObj["signalLost"] = source.signalLost;
        sourceObj["failsafe"] = source.failsafe;
        sourceObj["ageMs"] = source.ageMs;
    }

    return true;
}
