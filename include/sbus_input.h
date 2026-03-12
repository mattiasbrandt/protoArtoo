// =============================================================================
// include/sbus_input.h
//
// SBUSInputTask public interface.
// Runs on Core 1 — decodes dual SBUS receivers at ~200 Hz poll rate.
// =============================================================================
#pragma once

// -----------------------------------------------------------------------------
// sbusInputTask()
// FreeRTOS task function — pin to Core 1 via xTaskCreatePinnedToCore().
// Stack: 4096 bytes. Priority: 5.
// Implements Layers 1 (hardware failsafe) and 2 (software watchdog) failsafe.
// -----------------------------------------------------------------------------
void sbusInputTask(void* pvParameters);
