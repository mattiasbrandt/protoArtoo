// =============================================================================
// include/drive_arbiter.h
//
// Drive Output Arbiter — centralizes drive command arbitration from multiple
// sources (RC / SBUS and Web API).
//
// Design:
//   - submit(): Called from RcInputTask (Core 1) and HTTP handlers (Core 0).
//     Thread-safe via spinlock. Records source, speed, steer, and timestamp.
//   - resolve(): Called only from DriveTask (Core 1), once per control loop.
//     Returns the final drive output: speed, steer, failsafe active, and active source.
//     Pure function of its inputs (no side effects beyond FailsafeGate query).
//
// Arbitration logic:
//   - Most recent source (within timeout window) wins.
//   - Web source times out after cfg_webDriveTimeoutMs.
//   - Speed/steer are clamped to ±speedLimitMax before output.
//   - If any failsafe layer is active, output is zeroed.
//   - Web timeout triggers the FailsafeLayer::WEB_TIMEOUT failsafe layer once.
// =============================================================================
#pragma once

#include <cstdint>

// Drive source enumeration (replaces CommandSource for drive-specific use)
enum class DriveSource : uint8_t {
    RC       = 0,  // RC transmitter via SBUS receiver
    WEB_API  = 1,  // Browser / REST API
};

// Final drive output for this tick
struct DriveOutput {
    int16_t speed;              // -speedLimitMax .. +speedLimitMax
    int16_t steer;              // -speedLimitMax .. +speedLimitMax
    bool failsafeActive;        // true if any failsafe layer is currently active
    DriveSource activeSource;   // which source provided the current output (RC or WEB_API)
    uint32_t activeTimestampMs; // submit timestamp of the winning source (for status mirrors)
};

// Arbiter configuration (read from robotState at each resolve() call)
struct DriveArbiterConfig {
    int16_t speedLimitMax;      // Clamp limit (typically SPEED_LIMIT_MAX)
    uint32_t webDriveTimeoutMs; // Web timeout threshold
};

// Thread-safe initialization: must be called once from main.cpp before task creation.
// Passes the robotState mutex for spinlock-based critical sections.
void driveArbiterInit(void* mux_ptr);

// Reset arbiter state (for testing). Clears all cached commands.
void driveArbiterReset();

// Submit intent from any source (RC or WEB_API).
// Called from RcInputTask (Core 1) and HTTP handler tasks (Core 0).
// Thread-safe: acquires the robotState spinlock internally.
void driveArbiterSubmit(DriveSource src,
                        int16_t speed,
                        int16_t steer,
                        uint32_t timestampMs);

// Resolve final output for this tick.
// Called only from DriveTask (Core 1), once per control loop.
// Pure read of arbiter state and FailsafeGate; no side effects beyond
// triggering WEB_TIMEOUT failsafe on first expiry.
DriveOutput driveArbiterResolve(const DriveArbiterConfig& cfg,
                                uint32_t nowMs);
