// =============================================================================
// include/drive.h
//
// DriveTask public interface.
// Runs on Core 1  --  feeds the Foot Drive backend one frame at 50 Hz, every
// tick. What that frame contains is the backend's (include/drive_backend.h);
// that one goes out at all is this task's, and unconditional.
// =============================================================================
#pragma once

// -----------------------------------------------------------------------------
// driveTask()
// FreeRTOS task function  --  pin to Core 1 via xTaskCreatePinnedToCore().
// Stack: DRIVE_TASK_STACK_BYTES (chip-target specific; include/config.h carries
// the measured chain behind it). Priority: 5.
// Feeds TWDT every iteration (esp_task_wdt_add called internally at start).
// -----------------------------------------------------------------------------
void driveTask(void* pvParameters);
