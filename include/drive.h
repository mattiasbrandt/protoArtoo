// =============================================================================
// include/drive.h
//
// DriveTask public interface.
// Runs on Core 1 — sends 8-byte Gen2.x hoverboard frames at 50 Hz.
// =============================================================================
#pragma once

// -----------------------------------------------------------------------------
// driveTask()
// FreeRTOS task function — pin to Core 1 via xTaskCreatePinnedToCore().
// Stack: 4096 bytes. Priority: 5.
// Feeds TWDT every iteration (esp_task_wdt_add called internally at start).
// -----------------------------------------------------------------------------
void driveTask(void* pvParameters);

// Sync the resolved web-timeout state into FailsafeGate's WEB_TIMEOUT layer.
// Exposed for native tests because driveTask() itself owns UART/FreeRTOS loops.
void driveSyncWebTimeoutFailsafe(bool webTimedOut, bool* webTimeoutLayerActive);
