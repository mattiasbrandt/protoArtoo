// =============================================================================
// test/stubs/include/freertos/FreeRTOS.h
// FreeRTOS macro and type stubs for native host builds.
// =============================================================================
#pragma once
#include <stdint.h>

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) (void)(mux)
#define taskEXIT_CRITICAL(mux)  (void)(mux)

// Core ID stub for native test builds (always returns 0).
inline int xPortGetCoreID() { return 0; }

// FreeRTOS assert macro stub — no-op in native tests.
#define configASSERT(x) ((void)(x))
