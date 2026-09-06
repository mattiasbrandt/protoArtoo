#pragma once

#include "wifi_boot_decision.h"

// Test instrumentation accessors for network manager seam
int networkManagerGetApplyBootPostureCallCount();
WifiBootPosture networkManagerGetLastPosture();
void networkManagerResetTestState();

