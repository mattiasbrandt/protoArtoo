// =============================================================================
// include/failsafe_gate.h
//
// FailsafeGate — unified failsafe state machine.
// All failsafe state is owned by this module; external code routes through it.
// Thread-safe: all methods acquire an internal spinlock.
//
// Layers:
//   SBUS_HW: SBUS receiver hardware failsafe bit
//   SBUS_WATCHDOG: software SBUS frame timeout
//   WEB_TIMEOUT: web drive API command stale
//   TWDT_RESET: watchdog-reset boot recovery
//   ESTOP: explicit operator estop (latching)
//
// Design invariants:
//   - ESTOP is latching: failsafeClear(ESTOP) is a no-op.
//   - Only failsafeClearEstop() can clear ESTOP (explicit-intent path).
//   - failsafeTrigger() updates robotState mirror fields for status reporting.
//   - recordFailsafeTriggerLocked() is called internally for diagnostics.
// =============================================================================
#pragma once

#include <cstdint>

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#else
// Native test stubs
typedef int portMUX_TYPE;
#endif

enum class FailsafeLayer : uint8_t {
    SBUS_HW       = 0,   // receiver hardware failsafe bit
    SBUS_WATCHDOG = 1,   // software SBUS timeout
    WEB_TIMEOUT   = 2,   // web drive API stale
    TWDT_RESET    = 3,   // watchdog-reset boot recovery
    ESTOP         = 4,   // explicit operator estop
};

// Thread-safe initialization: must be called once from main.cpp before task creation.
// Passes the robotState mutex for spinlock-based critical sections.
void failsafeInit(portMUX_TYPE* mux);

// Trigger a failsafe layer: set its bit and update robotState mirrors.
// If not already active, calls recordFailsafeTriggerLocked() for diagnostics.
void failsafeTrigger(FailsafeLayer layer);

// Clear a failsafe layer: unset its bit and update robotState mirrors.
// No-op for ESTOP (latching); use failsafeClearEstop() for explicit ESTOP clear.
void failsafeClear(FailsafeLayer layer);

// Check if any failsafe layer is active.
bool failsafeIsActive();

// Return the highest-priority active failsafe layer (lowest enum index).
// Returns SBUS_HW as default if none active (caller should check failsafeIsActive first).
FailsafeLayer failsafeActiveReason();

// Explicit ESTOP clear: only this function can clear the ESTOP layer.
// Called from api_estop.cpp when operator requests estop/clear.
void failsafeClearEstop();
