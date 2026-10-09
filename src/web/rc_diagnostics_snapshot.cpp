// =============================================================================
// src/web/rc_diagnostics_snapshot.cpp
//
// RC diagnostics snapshot capture + JSON serialization for /api/rc and SSE rc.
// =============================================================================

#include "../../include/rc_diagnostics_snapshot.h"

#include <Arduino.h>

#include <cmath>

#include "../../include/config_cache.h"
#include "../../include/rc_diagnostics.h"
#include "../../include/rc_map_rules.h"  // rcReceiverReads(), rcRuleStoredCue(), rcStoredMapConflicts()
#include "../../include/reaction_evaluator.h"  // ReactionAvailability and its two spellings
#include "../../include/robot_state.h"

namespace {

struct RcActionBindingSpec {
    const char* name;
    RcBindingConfig binding;
};

void loadModeBindingSpecs(RcInputMode mode,
                          RcActionBindingSpec specs[RC_DIAGNOSTICS_CHANNEL_CAPACITY]) {
    const char* names[RC_DIAGNOSTICS_CHANNEL_CAPACITY] = {
        "driveSpeed", "driveSteer", "domeSpeed", "arm1", "arm2", "sound"};
    for (size_t i = 0; i < RC_DIAGNOSTICS_CHANNEL_CAPACITY; ++i) {
        specs[i].name = names[i];
    }

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    if (mode == RC_INPUT_STANDARD_PWM) {
        specs[0].binding = cfg.system.rc_pwm_drive_speed;
        specs[1].binding = cfg.system.rc_pwm_drive_steer;
        specs[2].binding = cfg.system.rc_pwm_dome_speed;
        specs[3].binding = cfg.system.rc_pwm_arm1;
        specs[4].binding = cfg.system.rc_pwm_arm2;
        specs[5].binding = cfg.system.rc_pwm_audio;
    } else {
        specs[0].binding = cfg.system.rc_sbus_drive_speed;
        specs[1].binding = cfg.system.rc_sbus_drive_steer;
        specs[2].binding = cfg.system.rc_sbus_dome_speed;
        specs[3].binding = cfg.system.rc_sbus_arm1;
        specs[4].binding = cfg.system.rc_sbus_arm2;
        specs[5].binding = cfg.system.rc_sbus_audio;
    }
}

uint32_t rcSourceAgeMs(uint32_t nowMs, uint32_t lastSeenMs) {
    if (lastSeenMs == 0) {
        return 0;
    }
    return nowMs - lastSeenMs;
}

uint32_t currentMillis() {
#ifdef ARDUINO
    return millis();
#else
    return 0;
#endif
}

float roundTo3(float value) {
    return std::round(value * 1000.0f) / 1000.0f;
}

}  // namespace

void captureRcDiagnosticsSnapshot(RcDiagnosticsSnapshot* out) {
    if (out == nullptr) {
        return;
    }

    RcDiagnosticsSnapshot snap = {};
    const uint32_t nowMs = currentMillis();

    RcInputMode rcInputMode;
    uint32_t timeoutMs;
    RcReceiverSetup receivers = {};
    bool sbusSignalLost, sbus2SignalLost, sbusHwFailsafe, sbus2HwFailsafe;
    uint32_t lastPwmMs, lastSbus1Ms, lastSbus2Ms;
    uint32_t sbus1LostFrameCount, sbus2LostFrameCount;
    uint16_t pwmPulseUs[RC_DIAGNOSTICS_PWM_RAW_CAPACITY];
    bool pwmPulseValid[RC_DIAGNOSTICS_PWM_RAW_CAPACITY];
    uint16_t sbus1Raw[RC_DIAGNOSTICS_SBUS_RAW_CAPACITY];
    uint16_t sbus2Raw[RC_DIAGNOSTICS_SBUS_RAW_CAPACITY];
    bool sbus1Digital[2];
    bool sbus2Digital[2];

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    RcInputActiveConfig activeRc = {};
    configCacheReadActiveRcInput(&activeRc);
    rcInputMode = static_cast<RcInputMode>(activeRc.mode);
    timeoutMs = cfg.drive.sbusTimeoutMs;
    receivers.mode = rcInputMode;
    for (size_t i = 0; i < 6; ++i) {
        receivers.enableRc[i] = activeRc.enableRc[i];
    }
    receivers.useCh2 = activeRc.useCh2;
    taskENTER_CRITICAL(&robotStateMux);
    sbusSignalLost = robotState.sbusSignalLost;
    sbus2SignalLost = robotState.sbus2SignalLost;
    sbusHwFailsafe = robotState.sbusHwFailsafe;
    sbus2HwFailsafe = robotState.sbus2HwFailsafe;
    lastPwmMs = robotState.lastPwmMs;
    lastSbus1Ms = robotState.lastSbus1Ms;
    lastSbus2Ms = robotState.lastSbus2Ms;
    sbus1LostFrameCount = robotState.sbus1LostFrameCount;
    sbus2LostFrameCount = robotState.sbus2LostFrameCount;
    for (size_t i = 0; i < RC_DIAGNOSTICS_PWM_RAW_CAPACITY; ++i) {
        pwmPulseUs[i] = robotState.rcPwmPulseUs[i];
        pwmPulseValid[i] = robotState.rcPwmPulseValid[i];
    }
    for (size_t i = 0; i < RC_DIAGNOSTICS_SBUS_RAW_CAPACITY; ++i) {
        sbus1Raw[i] = robotState.rcSbus1Raw[i];
        sbus2Raw[i] = robotState.rcSbus2Raw[i];
    }
    sbus1Digital[0] = robotState.rcSbus1Digital[0];
    sbus1Digital[1] = robotState.rcSbus1Digital[1];
    sbus2Digital[0] = robotState.rcSbus2Digital[0];
    sbus2Digital[1] = robotState.rcSbus2Digital[1];
    taskEXIT_CRITICAL(&robotStateMux);

    snap.mode = rcInputModeToString(rcInputMode);
    snap.updatedMs = lastPwmMs;
    if (lastSbus1Ms > snap.updatedMs) {
        snap.updatedMs = lastSbus1Ms;
    }
    if (lastSbus2Ms > snap.updatedMs) {
        snap.updatedMs = lastSbus2Ms;
    }

    snap.sources[0] = {"sbus1",
                       rcReceiverReads(RC_BINDING_SBUS1, receivers),
                       false,
                       rcSourceAgeMs(nowMs, lastSbus1Ms),
                       sbus1LostFrameCount,
                       sbusHwFailsafe};
    snap.sources[1] = {"sbus2",
                       rcReceiverReads(RC_BINDING_SBUS2, receivers),
                       false,
                       rcSourceAgeMs(nowMs, lastSbus2Ms),
                       sbus2LostFrameCount,
                       sbus2HwFailsafe};
    snap.sources[2] = {
        "pwm",
        rcReceiverReads(RC_BINDING_PWM, receivers),
        false,
        rcSourceAgeMs(nowMs, lastPwmMs),
        0,
        false};
    snap.sourceCount = RC_DIAGNOSTICS_SOURCE_CAPACITY;

    snap.sources[0].linked = snap.sources[0].enabled && lastSbus1Ms > 0 && !sbusSignalLost &&
                             snap.sources[0].ageMs <= timeoutMs;
    snap.sources[1].linked = snap.sources[1].enabled && lastSbus2Ms > 0 && !sbus2SignalLost &&
                             snap.sources[1].ageMs <= timeoutMs;
    snap.sources[2].linked =
        snap.sources[2].enabled && lastPwmMs > 0 && snap.sources[2].ageMs <= timeoutMs;

    RcActionBindingSpec specs[RC_DIAGNOSTICS_CHANNEL_CAPACITY] = {};
    loadModeBindingSpecs(rcInputMode, specs);

    for (size_t i = 0; i < RC_DIAGNOSTICS_CHANNEL_CAPACITY; ++i) {
        snap.mappingChannels[snap.mappingCount].name = specs[i].name;
        snap.mappingChannels[snap.mappingCount].binding = specs[i].binding;
        snap.mappingCount++;

        const RcBindingConfig& binding = specs[i].binding;
        const char* sourceName = rcDiagnosticsSourceName(binding.source);
        bool sourceEnabled = rcReceiverReads(binding.source, receivers);

        if (rcBindingSupportsAnalog(binding)) {
            int raw = 0;
            bool hasValue = false;
            if (binding.source == RC_BINDING_PWM && binding.channel >= 1 && binding.channel <= 6) {
                raw = pwmPulseUs[binding.channel - 1];
                hasValue = sourceEnabled && pwmPulseValid[binding.channel - 1];
            } else if (binding.source == RC_BINDING_SBUS1 && binding.channel >= 1 &&
                       binding.channel <= 16) {
                raw = sbus1Raw[binding.channel - 1];
                hasValue = sourceEnabled && lastSbus1Ms > 0;
            } else if (binding.source == RC_BINDING_SBUS2 && binding.channel >= 1 &&
                       binding.channel <= 16) {
                raw = sbus2Raw[binding.channel - 1];
                hasValue = sourceEnabled && lastSbus2Ms > 0;
            }

            bool inDeadband = false;
            float mapped = hasValue ? applyRcAnalogCalibration(raw, binding, &inDeadband) : 0.0f;
            RcDiagnosticsAnalogChannel& channel = snap.analogChannels[snap.analogCount++];
            channel.id = (uint8_t)snap.analogCount;
            channel.name = specs[i].name;
            channel.activeSource = sourceName;
            channel.bindingChannel = binding.channel;
            channel.raw = hasValue ? raw : 0;
            channel.rawUs = hasValue ? rcDiagnosticsRawToPulseUs(raw, binding) : 1500;
            channel.normalized = hasValue ? rcDiagnosticsNormalizeRaw(raw, binding) : 0.0f;
            channel.mapped = mapped;
            channel.inDeadband = hasValue ? inDeadband : false;
            channel.reverse = binding.reverse;
        } else if (rcBindingIsDigital(binding)) {
            bool pressed = false;
            bool hasValue = false;
            if (binding.source == RC_BINDING_SBUS1) {
                pressed = sbus1Digital[binding.channel == 18 ? 1 : 0];
                hasValue = sourceEnabled && lastSbus1Ms > 0;
            } else if (binding.source == RC_BINDING_SBUS2) {
                pressed = sbus2Digital[binding.channel == 18 ? 1 : 0];
                hasValue = sourceEnabled && lastSbus2Ms > 0;
            }

            RcDiagnosticsDigitalChannel& channel = snap.digitalChannels[snap.digitalCount++];
            channel.name = specs[i].name;
            channel.activeSource = sourceName;
            channel.bindingChannel = binding.channel;
            channel.pressed = hasValue ? pressed : false;
        }
    }

    if (lastSbus1Ms > 0) {
        snap.hasRawSbus1 = true;
        for (size_t i = 0; i < RC_DIAGNOSTICS_SBUS_RAW_CAPACITY; ++i) {
            snap.rawSbus1[i] = sbus1Raw[i];
        }
    }

    if (lastSbus2Ms > 0) {
        snap.hasRawSbus2 = true;
        for (size_t i = 0; i < RC_DIAGNOSTICS_SBUS_RAW_CAPACITY; ++i) {
            snap.rawSbus2[i] = sbus2Raw[i];
        }
    }

    if (lastPwmMs > 0) {
        snap.hasRawPwm = true;
        for (size_t i = 0; i < RC_DIAGNOSTICS_PWM_RAW_CAPACITY; ++i) {
            snap.rawPwm[i] = pwmPulseValid[i] ? pwmPulseUs[i] : 0;
        }
    }

    *out = snap;
}

bool populateRcDiagnosticsJson(JsonDocument& doc, const RcDiagnosticsSnapshot& snap) {
    if (snap.mode == nullptr) {
        return false;
    }

    doc.clear();

    JsonObject root = doc.to<JsonObject>();
    if (root.isNull()) {
        return false;
    }

    root["mode"] = snap.mode;
    root["updatedMs"] = snap.updatedMs;

    JsonObject sources = root["sources"].to<JsonObject>();
    if (sources.isNull()) {
        return false;
    }

    const size_t sourceCount = (snap.sourceCount <= RC_DIAGNOSTICS_SOURCE_CAPACITY)
                                   ? snap.sourceCount
                                   : RC_DIAGNOSTICS_SOURCE_CAPACITY;
    const size_t analogCount = (snap.analogCount <= RC_DIAGNOSTICS_CHANNEL_CAPACITY)
                                   ? snap.analogCount
                                   : RC_DIAGNOSTICS_CHANNEL_CAPACITY;
    const size_t digitalCount = (snap.digitalCount <= RC_DIAGNOSTICS_CHANNEL_CAPACITY)
                                    ? snap.digitalCount
                                    : RC_DIAGNOSTICS_CHANNEL_CAPACITY;
    const size_t mappingCount = (snap.mappingCount <= RC_DIAGNOSTICS_CHANNEL_CAPACITY)
                                    ? snap.mappingCount
                                    : RC_DIAGNOSTICS_CHANNEL_CAPACITY;

    for (size_t i = 0; i < sourceCount; ++i) {
        const RcDiagnosticsSourceSnapshot& source = snap.sources[i];
        JsonObject sourceObj = sources[source.key].to<JsonObject>();
        if (sourceObj.isNull()) {
            return false;
        }
        sourceObj["enabled"] = source.enabled;
        sourceObj["linked"] = source.linked;
        sourceObj["ageMs"] = source.ageMs;
        sourceObj["lostFrames"] = source.lostFrames;
        sourceObj["failsafe"] = source.failsafe;
    }

    JsonArray channels = root["channels"].to<JsonArray>();
    if (channels.isNull()) {
        return false;
    }

    for (size_t i = 0; i < analogCount; ++i) {
        const RcDiagnosticsAnalogChannel& channel = snap.analogChannels[i];
        JsonObject channelObj = channels.add<JsonObject>();
        if (channelObj.isNull()) {
            return false;
        }
        channelObj["id"] = channel.id;
        channelObj["name"] = channel.name;
        channelObj["type"] = "analog";
        channelObj["activeSource"] = channel.activeSource;
        channelObj["bindingChannel"] = channel.bindingChannel;
        channelObj["raw"] = channel.raw;
        channelObj["rawUs"] = channel.rawUs;
        channelObj["normalized"] = roundTo3(channel.normalized);
        channelObj["mapped"] = roundTo3(channel.mapped);
        channelObj["inDeadband"] = channel.inDeadband;
        channelObj["reverse"] = channel.reverse;
    }

    JsonObject digital = root["digital"].to<JsonObject>();
    if (digital.isNull()) {
        return false;
    }

    for (size_t i = 0; i < digitalCount; ++i) {
        const RcDiagnosticsDigitalChannel& channel = snap.digitalChannels[i];
        JsonObject digitalObj = digital[channel.name].to<JsonObject>();
        if (digitalObj.isNull()) {
            return false;
        }
        digitalObj["activeSource"] = channel.activeSource;
        digitalObj["bindingChannel"] = channel.bindingChannel;
        digitalObj["pressed"] = channel.pressed;
    }

    JsonObject mappingProfile = root["mappingProfile"].to<JsonObject>();
    if (mappingProfile.isNull()) {
        return false;
    }
    mappingProfile["version"] = 1;

    JsonObject mappingChannels = mappingProfile["channels"].to<JsonObject>();
    if (mappingChannels.isNull()) {
        return false;
    }

    for (size_t i = 0; i < mappingCount; ++i) {
        const RcDiagnosticsMappingChannel& channel = snap.mappingChannels[i];
        JsonObject mappingObj = mappingChannels[channel.name].to<JsonObject>();
        if (mappingObj.isNull()) {
            return false;
        }
        mappingObj["source"] = rcDiagnosticsSourceName(channel.binding.source);
        mappingObj["channel"] = channel.binding.channel;
        mappingObj["min"] = channel.binding.min;
        mappingObj["center"] = channel.binding.center;
        mappingObj["max"] = channel.binding.max;
        mappingObj["deadband"] = channel.binding.deadband;
        mappingObj["reverse"] = channel.binding.reverse;
    }

    JsonObject raw = root["raw"].to<JsonObject>();
    if (raw.isNull()) {
        return false;
    }

    if (snap.hasRawSbus1) {
        JsonArray sbus1 = raw["sbus1"].to<JsonArray>();
        if (sbus1.isNull()) {
            return false;
        }
        for (size_t i = 0; i < RC_DIAGNOSTICS_SBUS_RAW_CAPACITY; ++i) {
            sbus1.add(snap.rawSbus1[i]);
        }
    }

    if (snap.hasRawSbus2) {
        JsonArray sbus2 = raw["sbus2"].to<JsonArray>();
        if (sbus2.isNull()) {
            return false;
        }
        for (size_t i = 0; i < RC_DIAGNOSTICS_SBUS_RAW_CAPACITY; ++i) {
            sbus2.add(snap.rawSbus2[i]);
        }
    }

    if (snap.hasRawPwm) {
        JsonArray pwm = raw["pwm"].to<JsonArray>();
        if (pwm.isNull()) {
            return false;
        }
        for (size_t i = 0; i < RC_DIAGNOSTICS_PWM_RAW_CAPACITY; ++i) {
            pwm.add(snap.rawPwm[i]);
        }
    }

    return !doc.overflowed();
}

// The Reactions: the droid-condition bindings, and for each whether it is
// armed and how often it fired or was held back. Present and empty on a droid
// with none, so a reader never has to tell "none" from "not sent".
bool appendRcReactionsJson(JsonDocument& doc) {
    ReactionStatus statuses[REACTION_STATUS_SLOTS];
    taskENTER_CRITICAL(&robotStateMux);
    for (size_t i = 0; i < REACTION_STATUS_SLOTS; ++i) {
        statuses[i] = robotState.reactions[i];
    }
    taskEXIT_CRITICAL(&robotStateMux);

    JsonArray reactions = doc["reactions"].to<JsonArray>();
    if (reactions.isNull()) {
        return false;
    }
    for (const ReactionStatus& status : statuses) {
        const RcBindingSource source = (RcBindingSource)status.source;
        if (!rcBindingSourceIsDroidCondition(source)) {
            continue;
        }
        const ReactionAvailability availability = (ReactionAvailability)status.availability;
        JsonObject reaction = reactions.add<JsonObject>();
        if (reaction.isNull()) {
            return false;
        }
        reaction["source"] = rcBindingSourceToString(source);
        reaction["channel"] = status.channel;
        reaction["state"] = reactionAvailabilityFamily(availability);
        if (const char* reason = reactionAvailabilityReason(availability)) {
            reaction["reason"] = reason;
        }
        reaction["fires"] = status.fires;
        reaction["refusedWhileDriving"] = status.refusals;
    }
    return !doc.overflowed();
}

bool appendRcTaskStateJson(JsonDocument& doc) {
    taskENTER_CRITICAL(&robotStateMux);
    const bool driveAwaitingCentre = robotState.rcDriveAwaitingCentre;
    const uint16_t rate[2] = {robotState.sbus1FramesPerSecond, robotState.sbus2FramesPerSecond};
    const uint32_t fails[2] = {robotState.sbus1DecodeFails, robotState.sbus2DecodeFails};
    const bool digital[2][2] = {{robotState.rcSbus1Digital[0], robotState.rcSbus1Digital[1]},
                                {robotState.rcSbus2Digital[0], robotState.rcSbus2Digital[1]}};
    taskEXIT_CRITICAL(&robotStateMux);

    doc["driveAwaitingCentre"] = driveAwaitingCentre;
    // Beside each SBUS source's own entry in `sources`, which carries its link.
    static const char* const kKeys[2] = {"sbus1", "sbus2"};
    for (size_t i = 0; i < 2; ++i) {
        JsonObject source = doc["sources"][kKeys[i]];
        if (source.isNull()) {
            continue;
        }
        source["framesPerSecond"] = rate[i];
        source["decodeFails"] = fails[i];
    }
    // CH17 and CH18 of each SBUS receiver whose raw channels are reported, on
    // or off, whatever binds them: the RC page's grid and Detect read them
    // here, since `digital` covers only the six named bindings (#389).
    JsonObject raw = doc["raw"];
    for (size_t i = 0; i < 2; ++i) {
        if (raw.isNull() || raw[kKeys[i]].isNull()) {
            continue;
        }
        JsonArray pair = doc["rawDigital"][kKeys[i]].to<JsonArray>();
        pair.add(digital[i][0]);
        pair.add(digital[i][1]);
    }
    return !doc.overflowed();
}

// Whether the droid reads each RC Channel a cue is bound to as pressed (ADR
// 0070): judged as the input processor judges it - CH17/CH18 by their on/off
// bit, any other channel by its binding's own calibration - so the RC page
// need not decide it from a threshold of its own. A level, not a firing: a
// one-shot cue fires on each confirmed change of it. Only a cue the droid reads
// (rcRuleStoredCue() on the receiver type it runs) on a receiver it has heard
// and that conflicts with no other (rcStoredMapConflicts()) is said; a
// puppet string is a stick, not a press, and a Reaction has no RC
// Channel. Its own frame, like appendRcReactionsJson(): the eleven slots are a
// local here, never on the SSE task's root frame.
bool appendRcCuePressedJson(JsonDocument& doc) {
    RcTriggerBinding slots[RC_TRIGGER_SLOT_COUNT];
    const size_t count = configCacheReadRcTriggerSlots(slots, RC_TRIGGER_SLOT_COUNT);
    RcInputActiveConfig active = {};
    configCacheReadActiveRcInput(&active);
    const RcInputMode mode = static_cast<RcInputMode>(active.mode);
    RcStoredMap stored = {};
    configCacheReadRcAxes(active.mode, &stored.driveSpeed, &stored.driveSteer, &stored.domeSpeed);
    stored.cues = slots;
    stored.cueCount = count;
    const uint32_t conflicts = rcStoredMapConflicts(stored, mode);

    uint16_t raw[2][RC_DIAGNOSTICS_SBUS_RAW_CAPACITY];
    bool digital[2][2];
    bool heard[2];
    taskENTER_CRITICAL(&robotStateMux);
    for (size_t i = 0; i < RC_DIAGNOSTICS_SBUS_RAW_CAPACITY; ++i) {
        raw[0][i] = robotState.rcSbus1Raw[i];
        raw[1][i] = robotState.rcSbus2Raw[i];
    }
    digital[0][0] = robotState.rcSbus1Digital[0];
    digital[0][1] = robotState.rcSbus1Digital[1];
    digital[1][0] = robotState.rcSbus2Digital[0];
    digital[1][1] = robotState.rcSbus2Digital[1];
    // Heard and not lost: a receiver the watchdog has given up on holds its
    // last frame, which is not where the switch is now.
    heard[0] = robotState.lastSbus1Ms > 0 && !robotState.sbusSignalLost;
    heard[1] = robotState.lastSbus2Ms > 0 && !robotState.sbus2SignalLost;
    taskEXIT_CRITICAL(&robotStateMux);

    JsonObject pressed = doc["pressed"].to<JsonObject>();
    if (pressed.isNull()) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const RcTriggerBinding& binding = slots[i];
        const bool sbus1 = binding.source == RC_BINDING_SBUS1;
        if ((!sbus1 && binding.source != RC_BINDING_SBUS2) || binding.target == SERVO_ACTION_PUPPET_PART ||
            !rcRuleStoredCue(binding, mode).ok() || (conflicts & rcStoredCueBit(i)) != 0) {
            continue;
        }
        const size_t receiver = sbus1 ? 0 : 1;
        if (!heard[receiver]) {
            continue;
        }
        bool on = false;
        if (binding.channel > RC_DIAGNOSTICS_SBUS_RAW_CAPACITY) {
            on = digital[receiver][binding.channel == 18 ? 1 : 0];
        } else {
            on = rcTriggerToSwitchState(raw[receiver][binding.channel - 1], binding) == RC_SWITCH_HIGH;
        }
        char key[12];
        snprintf(key, sizeof(key), "%s:%u", rcBindingSourceToString(binding.source),
                 (unsigned)binding.channel);
        pressed[key] = on;
    }
    return !doc.overflowed();
}
