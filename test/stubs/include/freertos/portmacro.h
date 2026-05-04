// =============================================================================
// test/stubs/include/freertos/portmacro.h
// FreeRTOS port-level critical section stubs for native host builds.
// audio_soft_uart_tx.h includes this header for portENTER/EXIT_CRITICAL.
// =============================================================================
#pragma once

#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux)  ((void)(mux))
