#pragma once

// Test instrumentation accessors for network manager seam
int networkManagerGetApplyBootPostureCallCount();
WifiBootPosture networkManagerGetLastPosture();
void networkManagerResetTestState();

