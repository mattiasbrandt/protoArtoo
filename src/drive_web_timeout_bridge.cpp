// =============================================================================
// src/drive_web_timeout_bridge.cpp
//
// DriveTask helper for synchronizing DriveArbiter's resolved web-timeout state
// into FailsafeGate without adding side effects to driveArbiterResolve().
// =============================================================================

#include "drive.h"

#include "failsafe_gate.h"

void driveSyncWebTimeoutFailsafe(bool webTimedOut, bool* webTimeoutLayerActive) {
    if (webTimeoutLayerActive == nullptr) {
        return;
    }

    if (webTimedOut) {
        if (!*webTimeoutLayerActive) {
            failsafeTrigger(FailsafeLayer::WEB_TIMEOUT);
            *webTimeoutLayerActive = true;
        }
    } else if (*webTimeoutLayerActive) {
        failsafeClear(FailsafeLayer::WEB_TIMEOUT);
        *webTimeoutLayerActive = false;
    }
}
