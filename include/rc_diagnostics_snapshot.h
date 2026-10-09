// =============================================================================
// include/rc_diagnostics_snapshot.h
//
// RcDiagnosticsSnapshot + RC diagnostics JSON builder contract.
//
// captureRcDiagnosticsSnapshot(): copies RC diagnostics state under robotStateMux.
// populateRcDiagnosticsJson(): pure function that builds ArduinoJson payload from
// the captured snapshot.
// =============================================================================
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "rc_mapping.h"

enum RcInputMode : uint8_t;

static constexpr size_t RC_DIAGNOSTICS_SOURCE_CAPACITY = 3;
static constexpr size_t RC_DIAGNOSTICS_CHANNEL_CAPACITY = 6;
static constexpr size_t RC_DIAGNOSTICS_SBUS_RAW_CAPACITY = 16;
static constexpr size_t RC_DIAGNOSTICS_PWM_RAW_CAPACITY = 6;

struct RcDiagnosticsSourceSnapshot {
    const char* key;
    bool enabled;
    bool linked;
    uint32_t ageMs;
    uint32_t lostFrames;
    bool failsafe;
};

struct RcDiagnosticsAnalogChannel {
    uint8_t id;
    const char* name;
    const char* activeSource;
    uint8_t bindingChannel;
    int raw;
    uint16_t rawUs;
    float normalized;
    float mapped;
    bool inDeadband;
    bool reverse;
};

struct RcDiagnosticsDigitalChannel {
    const char* name;
    const char* activeSource;
    uint8_t bindingChannel;
    bool pressed;
};

struct RcDiagnosticsMappingChannel {
    const char* name;
    RcBindingConfig binding;
};

struct RcDiagnosticsSnapshot {
    const char* mode;
    uint32_t updatedMs;

    RcDiagnosticsSourceSnapshot sources[RC_DIAGNOSTICS_SOURCE_CAPACITY];
    size_t sourceCount;

    RcDiagnosticsAnalogChannel analogChannels[RC_DIAGNOSTICS_CHANNEL_CAPACITY];
    size_t analogCount;

    RcDiagnosticsDigitalChannel digitalChannels[RC_DIAGNOSTICS_CHANNEL_CAPACITY];
    size_t digitalCount;

    RcDiagnosticsMappingChannel mappingChannels[RC_DIAGNOSTICS_CHANNEL_CAPACITY];
    size_t mappingCount;

    uint16_t rawSbus1[RC_DIAGNOSTICS_SBUS_RAW_CAPACITY];
    uint16_t rawSbus2[RC_DIAGNOSTICS_SBUS_RAW_CAPACITY];
    uint16_t rawPwm[RC_DIAGNOSTICS_PWM_RAW_CAPACITY];
    bool hasRawSbus1;
    bool hasRawSbus2;
    bool hasRawPwm;
};

void captureRcDiagnosticsSnapshot(RcDiagnosticsSnapshot* out);
bool populateRcDiagnosticsJson(JsonDocument& doc, const RcDiagnosticsSnapshot& snap);

// Adds `reactions` to a document populateRcDiagnosticsJson() built: what each
// Reaction is doing (#450), as ReactionTask last published it in RobotState.
// Its own step, reading its own copy in its own frame, rather than a field of
// RcDiagnosticsSnapshot: the snapshot is a local of the SSE task's root frame,
// and eleven more entries there walked that task's chain 80 B past its
// recorded one. False when the document has no room.
bool appendRcReactionsJson(JsonDocument& doc);

// Adds the RC task's own state to a document populateRcDiagnosticsJson() built,
// read in its own frame for the same reason as appendRcReactionsJson():
//   driveAwaitingCentre  the boot hold (#389): RC drive is zero until both
//                        drive sticks have been at centre once since boot.
//   sources.sbus1/sbus2  framesPerSecond (decoded in the last second) and
//                        decodeFails (since boot), beside the link fields.
// False when the document has no room.
bool appendRcTaskStateJson(JsonDocument& doc);

// Exposed for unit testing  --  determines whether a source is active in a given mode.
bool rcSourceEnabledForMode(RcBindingSource source, RcInputMode mode, bool enableRcCh1,
                            bool enableRcCh2, bool anyPwmEnabled, bool useCh2);
