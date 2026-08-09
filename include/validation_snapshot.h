// =============================================================================
// include/validation_snapshot.h
//
// ValidationSnapshot + JSON builder contract for hardware-validation evidence.
//
// captureValidationSnapshot(): copies relevant validation state under robotStateMux.
// populateValidationJson(): pure function that builds ArduinoJson payload from
// the captured snapshot.
// =============================================================================
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "robot_state.h"

static constexpr size_t VALIDATION_RC_SOURCE_CAPACITY = 3;

struct ValidationDriveSnapshot {
    bool estop;
    bool webDriveExpired;
    bool sbusSignalLost;
    bool sbusHwFailsafe;
    FailsafeSource failsafeSource;
    uint32_t failsafeCount;
    uint32_t triggerMs;
    uint32_t zeroMs;
    uint32_t triggerToZeroMs;
    uint32_t watchdogMs;
    FailsafeSource triggerSource;
};

struct ValidationDomeLinkSnapshot {
    const char* state;
    uint32_t hbTx;
    uint32_t hbRx;
    int32_t lastRxMs;
};

struct ValidationAudioSnapshot {
    bool enabled;
    bool active;
    uint8_t activeMood;
    uint16_t randomMin;
    uint16_t randomMax;
    uint16_t intervalQuietS;
    uint16_t intervalMidS;
    uint16_t intervalFullS;
    uint16_t intervalAwakeS;
};

struct ValidationRcSourceSnapshot {
    const char* key;
    bool enabled;
    bool linked;
    bool signalLost;
    bool failsafe;
    uint32_t ageMs;
};

struct ValidationRcSnapshot {
    const char* mode;
    uint32_t timeoutMs;
    ValidationRcSourceSnapshot sources[VALIDATION_RC_SOURCE_CAPACITY];
    size_t sourceCount;
};

struct ValidationSnapshot {
    uint32_t updatedMs;
    ValidationDriveSnapshot drive;
    ValidationDomeLinkSnapshot domeLink;
    ValidationAudioSnapshot audio;
    ValidationRcSnapshot rc;
};

void captureValidationSnapshot(ValidationSnapshot* out);
bool populateValidationJson(JsonDocument& doc, const ValidationSnapshot& snap);
