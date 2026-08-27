/**
 * P4 RT safety/continuity bench harness — #195 Slice 2
 * Lives in bringup/ (fenced outside src/) and runs alongside the full firmware.
 *
 * Exercises the three device-criteria for #195 (watchdog reset, SBUS failsafe,
 * 50 Hz drive continuity) using synthetic SBUS loopback and serial observation.
 *
 * Self-running, time-phased sequence narrated on serial:
 *   Phase 0: Banner, pin plan, phase timings
 *   Phase 1: SBUS generator ON (jumpered to PIN_SBUS1_RX) - failsafe clears
 *   Phase 2: SBUS generator OFF - failsafe re-arms after sbusTimeoutMs
 *   Phase 3: Drive frame cadence - sample PIN_DRIVE_TX intervals
 *   Phase 4: Watchdog stall - TWDT registered bench task stops feeding, chip resets
 *            Re-arm gate: skip stall if this boot IS a watchdog reset
 *
 * Wiring (jumpers for this bench session):
 *   BENCH_TX_SBUS (GPIO52, free pin) -> PIN_SBUS1_RX (GPIO28)
 *   PIN_DRIVE_TX (GPIO20) -> BENCH_RX_DRIVE (GPIO51, borrowed ARM5_SERVO)
 *
 * SBUS protocol: 25-byte frames, 100 kbaud, 8E2, inverted.
 *   Byte 0: 0x0F (header)
 *   Bytes 1-22: packed 16 x 11-bit channels (LSB-first)
 *   Byte 23: flags (bit0=CH17, bit1=CH18, bit2=frame_lost, bit3=failsafe)
 *   Byte 24: 0x00 (footer)
 * Reference: docs/spec-sheets/sbus-protocol.md §3-5
 */

#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>

// ============================================================================
// Forward declarations
// ============================================================================
void benchPhase0Banner();
void benchPhase1SbusGeneratorOn();
void benchPhase2SbusGeneratorOff();
void benchPhase3DriveFrameCadence();
void benchPhase4WatchdogStall();

// ============================================================================
// Bench GPIO pins
// ============================================================================
// BENCH_TX_SBUS: GPIO52 (the only free pin on FireBeetle 2 after production inventory)
// Output meets RMT RX input (safe direction for borrow).
// Jumper from GPIO52 to GPIO28 (PIN_SBUS1_RX)
static constexpr gpio_num_t BENCH_TX_SBUS = GPIO_NUM_52;

// BENCH_RX_DRIVE: GPIO51 (borrowed from ARM5_SERVO / AUX3, input only)
// Input from GPIO20 (PIN_DRIVE_TX) cannot contend with servo driver.
// Precondition: LEDC outputs must be disabled at boot (servo task skips init).
static constexpr gpio_num_t BENCH_RX_DRIVE = GPIO_NUM_51;

// Bench UART controllers (configured to route to bench GPIO via GPIO matrix)
static constexpr uart_port_t UART_BENCH_SBUS = UART_NUM_2;   // TX on GPIO52 (or any UART via GPIO matrix)
static constexpr uart_port_t UART_BENCH_DRIVE = UART_NUM_4;  // RX on GPIO51

// ============================================================================
// Phase timings (chosen generously to allow full settle between phases)
// ============================================================================
static constexpr uint32_t PHASE_0_SETTLE_MS = 500;     // Boot settle before banner
static constexpr uint32_t PHASE_1_DURATION_MS = 3000;  // SBUS on: 3000 ms at 14ms frames ~= 214 frames
static constexpr uint32_t PHASE_2_SETTLE_MS = 7000;    // SBUS off: allow sbusTimeoutMs (5000) + margin
static constexpr uint32_t PHASE_3_DURATION_MS = 5000;  // Drive frame cadence: 5000 ms at 50 Hz = 250 frames
static constexpr uint32_t PHASE_4_PREP_MS = 1000;      // Phase 4 entry settle

// SBUS frame period (nominal 14 ms = 71 Hz, but system runs at real RC rate)
static constexpr uint32_t SBUS_FRAME_PERIOD_MS = 14;

// ============================================================================
// SBUS frame builder (centered channels, no failsafe)
// ============================================================================
// Exact inverse of sbusUnpackChannels() in include/sbus_unpack.h: that function
// forms a little-endian 24-bit word from data[byteIdx..byteIdx+2] and takes
// (raw >> bitOff) & 0x7FF. Packing therefore shifts LEFT by the same bitOff and
// ORs the three bytes back in the same order. An earlier revision shifted RIGHT,
// which discards each channel's low bits and mis-packs 15 of 16 channels.
static void buildSbusFrame(uint8_t* frame, uint16_t ch1, uint16_t ch2) {
    memset(frame, 0, 25);
    frame[0] = 0x0F;

    uint16_t channels[16];
    for (int i = 0; i < 16; i++) {
        channels[i] = 1024;  // centre
    }
    channels[0] = ch1;
    channels[1] = ch2;

    int bitPos = 0;
    for (int ch = 0; ch < 16; ch++) {
        const int byteIdx = bitPos / 8 + 1;  // +1: the payload starts at frame[1]
        const int bitOff = bitPos % 8;
        const uint32_t v = static_cast<uint32_t>(channels[ch] & 0x7FFU) << bitOff;
        for (int b = 0; b < 3; b++) {
            const int idx = byteIdx + b;
            if (idx <= 22) {  // never touch the flags byte at frame[23]
                frame[idx] |= static_cast<uint8_t>((v >> (8 * b)) & 0xFFU);
            }
        }
        bitPos += 11;
    }

    frame[23] = 0x00;  // flags: no failsafe, no frame loss
    frame[24] = 0x00;  // footer
}

// ============================================================================
// Bench tasks
// ============================================================================
static TaskHandle_t benchMainTaskHandle = NULL;
static TaskHandle_t benchStallTaskHandle = NULL;  // Task handle for TWDT-stall phase

void benchStallTask(void* param) {
    // Task runs on Core 0, registered with TWDT, then stalls to trigger reset.
    // This task stops feeding the watchdog timer.

    Serial.println("[BENCH P4] benchStallTask started, registered with TWDT");
    Serial.flush();

    // Task is registered with TWDT at creation (see benchMainTask).
    // Just yield and never feed the watchdog.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
        // Not calling esp_task_wdt_reset(), so TWDT will expire.
    }
}

void benchMainTask(void* param) {
    // Main bench harness task — runs the phase sequence on Core 1 (or Core 0).
    // Runs alongside the full firmware.

    benchPhase0Banner();
    benchPhase1SbusGeneratorOn();
    benchPhase2SbusGeneratorOff();
    benchPhase3DriveFrameCadence();
    benchPhase4WatchdogStall();

    Serial.println("[BENCH] All phases complete (or re-arm gate triggered)");
    Serial.flush();

    // Exit task (but if Phase 4 stalled, chip resets before this)
    vTaskDelete(NULL);
}

// ============================================================================
// Phase implementations
// ============================================================================

void benchPhase0Banner() {
    Serial.println("\n[BENCH P0] === protoArtoo P4 RT Bench Harness ===");
    Serial.println("[BENCH P0] Wiring: BENCH_TX_SBUS (GPIO52, free pin) -> PIN_SBUS1_RX (GPIO28)");
    Serial.println("[BENCH P0] Wiring: PIN_DRIVE_TX (GPIO20) -> BENCH_RX_DRIVE (GPIO51, borrowed ARM5)");
    Serial.println("[BENCH P0] Precondition: Servo/AUX outputs disabled (check boot log for 'skipping LEDC init')");
    Serial.printf("[BENCH P0] Phase timings: P0=%lu, P1=%lu, P2=%lu, P3=%lu, P4_prep=%lu (ms)\n",
                  PHASE_0_SETTLE_MS, PHASE_1_DURATION_MS, PHASE_2_SETTLE_MS,
                  PHASE_3_DURATION_MS, PHASE_4_PREP_MS);
    Serial.println("[BENCH P0] Phase plan:");
    Serial.println("[BENCH P0]   P0: Banner and setup");
    Serial.println("[BENCH P0]   P1: SBUS generator ON - should clear failsafe");
    Serial.println("[BENCH P0]   P2: SBUS generator OFF - failsafe re-arms after timeout");
    Serial.println("[BENCH P0]   P3: Drive frame cadence observation (skips if servo outputs enabled)");
    Serial.println("[BENCH P0]   P4: Watchdog stall - chip resets");
    Serial.flush();

    vTaskDelay(pdMS_TO_TICKS(PHASE_0_SETTLE_MS));
}

void benchPhase1SbusGeneratorOn() {
    Serial.println("[BENCH P1] SBUS generator starting on GPIO52 TX...");
    Serial.flush();

    // Initialize UART3 for SBUS (100 kbaud, 8E2, inverted TX)
    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate = 100000;
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_EVEN;
    uart_cfg.stop_bits = UART_STOP_BITS_2;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.rx_flow_ctrl_thresh = 0;
    uart_cfg.source_clk = UART_SCLK_RTC;

    esp_err_t ret = uart_param_config(UART_BENCH_SBUS, &uart_cfg);
    if (ret != ESP_OK) {
        Serial.printf("[BENCH P1] ERROR: uart_param_config failed: %s\n", esp_err_to_name(ret));
        Serial.flush();
        return;
    }

    // Set GPIO48 as TX with inversion
    ret = uart_set_pin(UART_BENCH_SBUS, BENCH_TX_SBUS, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        Serial.printf("[BENCH P1] ERROR: uart_set_pin failed: %s\n", esp_err_to_name(ret));
        Serial.flush();
        return;
    }

    // Enable TX inversion for SBUS (standard inverted signaling)
    uart_set_line_inverse(UART_BENCH_SBUS, UART_SIGNAL_TXD_INV);

    // Driver install
    // Vendor constraint (ESP-IDF uart.c:1942): rx_buffer_size must be > UART_HW_FIFO_LEN(uart_num).
    // Even though this UART is TX-only, the driver enforces this check. Allocate 256 bytes
    // (UART FIFO is typically 128 bytes; 256 > 128 satisfies the constraint).
    ret = uart_driver_install(UART_BENCH_SBUS, 256, 1024, 0, NULL, 0);
    if (ret != ESP_OK) {
        Serial.printf("[BENCH P1] ERROR: uart_driver_install failed: %s\n", esp_err_to_name(ret));
        Serial.flush();
        return;
    }

    Serial.println("[BENCH P1] UART3 configured for SBUS TX. Starting frame emission...");
    Serial.flush();

    // Send SBUS frames for PHASE_1_DURATION_MS
    uint8_t sbusFrame[25];
    uint32_t startMs = millis();
    uint32_t frameCount = 0;
    uint32_t lastFrameMs = startMs;

    while (millis() - startMs < PHASE_1_DURATION_MS) {
        // Build centered SBUS frame (buildSbusFrame does memset internally)
        buildSbusFrame(sbusFrame, 1024, 1024);

        // Transmit
        uart_write_bytes(UART_BENCH_SBUS, (const void*)sbusFrame, 25);
        frameCount++;

        uint32_t now = millis();
        uint32_t elapsed = now - lastFrameMs;
        if (elapsed > 100) {
            // Every 100ms, log progress
            Serial.printf("[BENCH P1] Sent %lu frames in %lu ms\n", frameCount, now - startMs);
            Serial.flush();
            lastFrameMs = now;
        }

        // Timing: send next frame at ~14 ms intervals
        vTaskDelay(pdMS_TO_TICKS(SBUS_FRAME_PERIOD_MS));
    }

    Serial.printf("[BENCH P1] Phase 1 complete: sent %lu frames in %lu ms\n",
                  frameCount, millis() - startMs);
    Serial.flush();
}

void benchPhase2SbusGeneratorOff() {
    Serial.println("[BENCH P2] SBUS generator OFF - watching for failsafe re-arm...");
    Serial.flush();

    // Deinitialize UART to stop sending
    uart_driver_delete(UART_BENCH_SBUS);

    // Wait for SBUS watchdog to trigger (sbusTimeoutMs from NVS, typically 5000 ms)
    // Watch for the latching-estop evidence: "failsafe zero output asserted"
    uint32_t startMs = millis();

    while (millis() - startMs < PHASE_2_SETTLE_MS) {
        // Note: failsafe re-arm happens in DriveTask, logged at src/tasks/drive.cpp:133
        // We just wait and observe.
        vTaskDelay(pdMS_TO_TICKS(500));

        uint32_t elapsed = millis() - startMs;
        Serial.printf("[BENCH P2] Elapsed: %lu ms (waiting for sbusTimeoutMs trigger)\n", elapsed);
        Serial.flush();
    }

    Serial.println("[BENCH P2] Phase 2 complete: observation window closed");
    Serial.flush();
}

void benchPhase3DriveFrameCadence() {
    Serial.println("[BENCH P3] Drive frame cadence observation starting...");

    // Precondition check: servo outputs (LEDC PWM) must be disabled.
    // If servo task initialized, it drives GPIO51 (ARM5_SERVO) with PWM.
    // Reading GPIO51 while servo is active would sample PWM noise, not drive frames.
    // Check config the same way servo_task.cpp does (servo_task.cpp:425):
    // LEDC initializes if any of enable_arm1, enable_arm2, enable_aux1, enable_aux2,
    // enable_aux3 are true. Read from NVS directly (bench env doesn't link config_store).
    Preferences nvs;
    nvs.begin("artoo", true);  // read-only namespace "artoo"
    bool anyServoEnabled = nvs.getBool("en_arm1", false) || nvs.getBool("en_arm2", false) ||
                            nvs.getBool("en_aux1", false) || nvs.getBool("en_aux2", false) ||
                            nvs.getBool("en_aux3", false);
    nvs.end();

    if (anyServoEnabled) {
        Serial.println("[BENCH P3] SKIP: Servo outputs (LEDC PWM) enabled in config");
        Serial.println("[BENCH P3] Precondition not met: LEDC must be disabled (NVS: en_arm1/2, en_aux1/2/3)");
        Serial.flush();
        return;
    }

    Serial.println("[BENCH P3] Sampling PIN_DRIVE_TX (GPIO20) received on BENCH_RX_DRIVE (GPIO51)");
    Serial.flush();

    // Initialize UART4 RX on GPIO51 to receive drive frames from GPIO20
    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate = 115200;  // Drive backend (hoverboard) baud
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_DISABLE;
    uart_cfg.stop_bits = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_RTC;

    esp_err_t ret = uart_param_config(UART_BENCH_DRIVE, &uart_cfg);
    if (ret != ESP_OK) {
        Serial.printf("[BENCH P3] ERROR: uart_param_config failed: %s\n", esp_err_to_name(ret));
        Serial.flush();
        return;
    }

    // Set GPIO51 as RX (GPIO20 jumpered in)
    ret = uart_set_pin(UART_BENCH_DRIVE, UART_PIN_NO_CHANGE, BENCH_RX_DRIVE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        Serial.printf("[BENCH P3] ERROR: uart_set_pin failed: %s\n", esp_err_to_name(ret));
        Serial.flush();
        return;
    }

    // Driver install
    ret = uart_driver_install(UART_BENCH_DRIVE, 2048, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        Serial.printf("[BENCH P3] ERROR: uart_driver_install failed: %s\n", esp_err_to_name(ret));
        Serial.flush();
        return;
    }

    Serial.println("[BENCH P3] UART4 configured for drive RX. Sampling intervals...");
    Serial.flush();

    uint32_t startMs = millis();
    uint32_t lastFrameMs = startMs;
    uint32_t frameCount = 0;
    uint32_t minIntervalMs = UINT32_MAX;
    uint32_t maxIntervalMs = 0;
    uint64_t sumIntervalMs = 0;

    while (millis() - startMs < PHASE_3_DURATION_MS) {
        // Try to read from UART4 (looking for 8-byte drive frames)
        uint8_t buf[8];
        int len = uart_read_bytes(UART_BENCH_DRIVE, buf, sizeof(buf), pdMS_TO_TICKS(100));

        if (len > 0) {
            // Frame received (8 bytes of Gen2.x hoverboard protocol)
            uint32_t now = millis();
            uint32_t interval = now - lastFrameMs;

            if (frameCount > 0) {  // Skip first interval (startup transient)
                minIntervalMs = (interval < minIntervalMs) ? interval : minIntervalMs;
                maxIntervalMs = (interval > maxIntervalMs) ? interval : maxIntervalMs;
                sumIntervalMs += interval;
            }

            frameCount++;
            lastFrameMs = now;

            if (frameCount % 10 == 0) {
                Serial.printf("[BENCH P3] Received %lu frames, interval range: %lu-%lu ms\n",
                              frameCount, minIntervalMs, maxIntervalMs);
                Serial.flush();
            }
        }
    }

    // Report statistics
    if (frameCount > 1) {
        uint32_t avgIntervalMs = (uint32_t)(sumIntervalMs / (frameCount - 1));
        Serial.printf("[BENCH P3] cadence stats - count=%lu, min=%lu, max=%lu, avg=%lu ms\n",
                      frameCount, minIntervalMs, maxIntervalMs, avgIntervalMs);
        Serial.printf("[BENCH P3] expected period: 20 ms (50 Hz), observed: %lu ms avg\n",
                      avgIntervalMs);
    } else {
        Serial.println("[BENCH P3] WARNING: No drive frames received during observation window");
    }

    uart_driver_delete(UART_BENCH_DRIVE);
    Serial.println("[BENCH P3] Phase 3 complete");
    Serial.flush();
}

void benchPhase4WatchdogStall() {
    Serial.println("[BENCH P4] Watchdog stall phase starting...");
    Serial.flush();

    // Check if THIS boot is a watchdog reset (re-arm gate).
    // Rationale: Phase 4 causes a reset, which reboots the board.
    // On reboot, this code runs again. To avoid infinite reset loops,
    // we only stall once and skip on subsequent watchdog reboots.
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("[BENCH P4] Current reset reason: 0x%x\n", reason);
    Serial.flush();

    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT) {
        Serial.println("[BENCH P4] GATE: Watchdog reset detected on THIS boot - skipping stall");
        Serial.println("[BENCH P4] Expected behavior: previous phase 4 reset caused this boot");
        Serial.println("[BENCH P4] Next line should show: 'watchdog reset detected - estop set'");
        Serial.flush();
        return;  // Skip stall, allow normal operation
    }

    // Proceed with TWDT stall (only if not already a watchdog reset)
    Serial.println("[BENCH P4] Settling before TWDT task launch...");
    Serial.flush();

    vTaskDelay(pdMS_TO_TICKS(PHASE_4_PREP_MS));

    // Create bench task and subscribe to TWDT
    Serial.println("[BENCH P4] Creating bench task and registering with TWDT...");
    Serial.flush();

    esp_err_t ret = xTaskCreatePinnedToCore(
        benchStallTask,           // Function
        "benchStall",             // Name
        2048,                     // Stack size
        NULL,                     // Parameter
        1,                        // Priority
        &benchStallTaskHandle,    // Handle
        0                         // Core 0
    );

    if (ret != pdPASS) {
        Serial.printf("[BENCH P4] ERROR: Task creation failed: %d\n", (int)ret);
        Serial.flush();
        return;
    }

    // Add task to TWDT
    ret = esp_task_wdt_add(benchStallTaskHandle);
    if (ret != ESP_OK) {
        Serial.printf("[BENCH P4] ERROR: esp_task_wdt_add failed: %s\n", esp_err_to_name(ret));
        Serial.flush();
        return;
    }

    Serial.println("[BENCH P4] TWDT stall initiated. Waiting for watchdog expiry...");
    Serial.println("[BENCH P4] Controller will reset on watchdog timeout (~5 seconds)");
    Serial.flush();

    // Wait for watchdog to trigger (should reset in ~5 seconds)
    vTaskDelay(pdMS_TO_TICKS(10000));

    // Should not reach here (watchdog will reset)
    Serial.println("[BENCH P4] ERROR: Watchdog did not fire! Unexpected state.");
    Serial.flush();
}

// ============================================================================
// Static initializer — starts bench task without modifying src/main.cpp
// ============================================================================
// Bench harness entry point: static initializer creates benchMainTask before
// Arduino setup() runs. This mechanism allows the bench code to start its own
// task without requiring any changes to src/main.cpp (fenced file).
//
// The static object is instantiated during program initialization, before main(),
// and its constructor runs in the context of the FreeRTOS scheduler (which is
// already active by the time static constructors execute). This is the only
// entry point used; the firmware's setup/loop run normally in parallel.

class BenchInitializer {
public:
    BenchInitializer() {
        // Create bench main task on Core 0, after Arduino startup.
        // The scheduler is running by this point.
        esp_err_t ret = xTaskCreatePinnedToCore(
            benchMainTask,        // Function
            "benchMain",          // Name
            4096,                 // Stack size
            NULL,                 // Parameter
            1,                    // Priority
            &benchMainTaskHandle, // Handle
            0                     // Core 0
        );

        if (ret != pdPASS) {
            // Serial might not be initialized yet, but this is failsafe logging
            if (Serial) {
                Serial.printf("[BENCH] ERROR: benchMainTask creation failed: %d\n", (int)ret);
                Serial.flush();
            }
        }
    }
};

// Static initializer object — constructor runs at startup (before main())
static BenchInitializer __benchInit;
