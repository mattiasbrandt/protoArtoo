// =============================================================================
// src/drivers/pca9685.cpp
//
// The PCA9685 body servo expander: its bring-up, the mailbox ServoTask writes
// into, and the Core 0 task that does the bus writes. Contract and rationale:
// include/pca9685.h. Register values: docs/spec-sheets/pca9685-servo-expander.md
// section 7.
// =============================================================================

#include "pca9685.h"

#include <Arduino.h>
#include <Wire.h>

#include <string.h>  // memcpy()

#include <atomic>

#include "config.h"   // PIN_I2C_SDA / PIN_I2C_SCL, PCA9685_TASK_STACK_BYTES
#include "logging.h"

static const char* TAG = "PCA9685";

// The board's address, as brought up. Written once in setup().
static uint8_t s_address = PCA9685_ADDRESS_DEFAULT;

// Whether the board has acknowledged everything asked of it. Set by
// pca9685Begin() in setup(), cleared by the sender on the first write that
// fails, never set again: a board that dropped off the bus is not trusted to be
// in the state the mailbox thinks it is, so it stays unreachable until the
// droid restarts (ADR 0043, "a bus drop is reported, not escalated").
static std::atomic<bool> s_answering{false};

// The mailbox (include/pca9685.h): what each channel should be, and which of
// them changed since the sender last looked. Written on Core 1 by ServoTask,
// read and cleared on Core 0 by the sender, so both sides take s_mailboxMux -
// a spinlock across the cores, held for a copy of 34 bytes and never across a
// bus write. Every channel starts as PCA9685_OFF, which is what bring-up leaves
// on the chip.
static portMUX_TYPE s_mailboxMux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_desired[PCA9685_CHANNEL_COUNT] = {};
static uint16_t s_dirty = 0;

static TaskHandle_t s_sender = nullptr;

static_assert(PCA9685_CHANNEL_COUNT <= 16, "s_dirty is one bit per channel");

// -----------------------------------------------------------------------------
// The bus
// -----------------------------------------------------------------------------

// One write transaction: the register, then `count` bytes into it and on
// (MODE1's AI bit makes the chip step the register). The answer is
// endTransmission()'s, every time - 0 is acknowledged, anything else is a
// NACK, a timeout or a bus error (the Wire contract, libraries/Wire/src/
// Wire.cpp) - and a byte the library's buffer would not take is a failure too.
static uint8_t writeRegisters(uint8_t reg, const uint8_t* data, size_t count) {
    Wire.beginTransmission(s_address);
    if (Wire.write(reg) != 1 || Wire.write(data, count) != count) {
        Wire.endTransmission();
        return 1;  // the Wire contract's "data too long to fit in transmit buffer"
    }
    return Wire.endTransmission();
}

static uint8_t writeRegister(uint8_t reg, uint8_t value) {
    return writeRegisters(reg, &value, 1);
}

// MODE1 as the chip holds it, for the RESTART bit. False when it does not
// answer the read.
static bool readMode1(uint8_t* value) {
    // The register pointer, then a repeated start and one byte back: Wire
    // sends both as one transaction from requestFrom(), which also releases
    // the bus whatever it returns.
    Wire.beginTransmission(s_address);
    if (Wire.write(PCA9685_REG_MODE1) != 1 || Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(s_address, (size_t)1) != 1 || Wire.available() < 1) {
        return false;
    }
    *value = (uint8_t)Wire.read();
    return true;
}

// -----------------------------------------------------------------------------
// pca9685Begin()
// -----------------------------------------------------------------------------
bool pca9685Begin(uint8_t address) {
    s_address = address;
    s_answering.store(false);

    // Refused here as well as at the config door (configApply()), because this
    // is the door that would actually write to it.
    if (!pca9685AddressUsable(address)) {
        PA_LOG_ERROR(TAG, "0x%02X is not an address one board may use - %s will not move",
                     (unsigned)address, PCA9685_OUTPUT_SPAN);
        return false;
    }

    // The bus, at a clock set here rather than inherited (spec sheet 9.4). This
    // is where the I2C driver takes its buffers and creates the bus, once.
    if (!Wire.begin((int)PIN_I2C_SDA, (int)PIN_I2C_SCL, PCA9685_I2C_CLOCK_HZ)) {
        PA_LOG_ERROR(TAG, "the I2C bus did not start (SDA %u, SCL %u) - %s will not move",
                     (unsigned)PIN_I2C_SDA, (unsigned)PIN_I2C_SCL, PCA9685_OUTPUT_SPAN);
        return false;
    }
    Wire.setTimeOut(PCA9685_BUS_TIMEOUT_MS);

    // The bring-up, in this order, and each write's answer checked:
    //
    //   1. Every channel full-OFF, FIRST. A fresh power-up already leaves every
    //      channel full-OFF (spec sheet 7.3), but this controller restarting is
    //      not the expander losing power: after a reboot the chip is still
    //      driving whatever the last session left on it. Writing ALL_LED_OFF_H
    //      before anything else is what makes "limp at start" true across a
    //      restart as well as a power-up. It is also the first write to the
    //      address, which is where the I2C driver adds its device handle.
    //   2. SLEEP, with auto-increment, and ALLCALL and the sub-addresses off.
    //   3. PRE_SCALE, which the chip only takes while asleep (spec sheet 7.4) -
    //      a write while running is silently ignored, which is why this order.
    //   4. MODE2: totem-pole outputs, the chip's own default, stated.
    //   5. Wake, and let the oscillator settle at least 500 us.
    //   6. RESTART, only if the chip says it has PWM to restart (spec sheet
    //      7.6) - never sooner than 500 us after SLEEP went to 0.
    const uint8_t allOff = PCA9685_LED_FULL;
    uint8_t mode1 = 0;
    const bool answered =
        writeRegisters(PCA9685_REG_ALL_LED_OFF_H, &allOff, 1) == 0 &&
        writeRegister(PCA9685_REG_MODE1, PCA9685_MODE1_SLEEP | PCA9685_MODE1_AI) == 0 &&
        writeRegister(PCA9685_REG_PRE_SCALE, PCA9685_PRESCALE) == 0 &&
        writeRegister(PCA9685_REG_MODE2, PCA9685_MODE2_OUTDRV) == 0 && readMode1(&mode1) &&
        writeRegister(PCA9685_REG_MODE1, PCA9685_MODE1_AI) == 0;
    if (answered) {
        delayMicroseconds(PCA9685_RESTART_WAIT_US);
    }
    const bool restarted = !answered || (mode1 & PCA9685_MODE1_RESTART) == 0 ||
                           writeRegister(PCA9685_REG_MODE1,
                                         PCA9685_MODE1_RESTART | PCA9685_MODE1_AI) == 0;
    if (!answered || !restarted) {
        // The reference project's boot report, in its shape: the address, the
        // channel span it owns, and the consequence in plain words.
        PA_LOG_ERROR(TAG, "board 0x%02X, outputs %s: not answering - those outputs will not move",
                     (unsigned)address, PCA9685_OUTPUT_SPAN);
        return false;
    }

    s_answering.store(true);
    PA_LOG_INFO(TAG, "board 0x%02X, outputs %s: answering at %u kHz, every output limp",
                (unsigned)address, PCA9685_OUTPUT_SPAN, (unsigned)(PCA9685_I2C_CLOCK_HZ / 1000u));
    return true;
}

bool pca9685Answering() { return s_answering.load(); }

uint8_t pca9685Address() { return s_address; }

// -----------------------------------------------------------------------------
// The sender: Core 0, off ServoTask's loop
//
// One batch per wake: take the mailbox's changes under the lock, then write
// them with the lock released. When every channel is to be off, one
// ALL_LED_OFF_H write covers all sixteen; otherwise each changed channel is
// one transaction of its four registers. The first write that fails ends the
// batch and the session's trust in the board: the rest of the batch would
// only be more timeouts on a bus that has already said no.
//
// Worst case from the halt edge to the full-OFF write: ServoTask releases
// every Output on the frame it sees the estop or Sleep Mode (its loop period,
// 20 ms, is the wait to see it) and commits straight after; this task wakes
// on that notification, at a priority above the web server's on Core 0. If a
// batch is part way through it finishes first - at most sixteen channel
// writes of ~140 us each, ~2.3 ms, or one PCA9685_BUS_TIMEOUT_MS when the bus
// is failing - and the next batch is the one ALL_LED_OFF_H write, ~0.1 ms.
// So about 20 + 2.3 + 0.1 ms, plus whatever the Wi-Fi stack's higher-priority
// tasks hold Core 0 for.
// -----------------------------------------------------------------------------
static bool sendBatch(const uint16_t* desired, uint16_t dirty) {
    bool allOff = true;
    for (uint8_t channel = 0; channel < PCA9685_CHANNEL_COUNT; ++channel) {
        if (desired[channel] != PCA9685_OFF) {
            allOff = false;
            break;
        }
    }
    if (allOff) {
        const uint8_t off = PCA9685_LED_FULL;
        const uint8_t err = writeRegisters(PCA9685_REG_ALL_LED_OFF_H, &off, 1);
        if (err != 0) {
            PA_LOG_ERROR(TAG, "board 0x%02X stopped answering (error %u, letting every output go) - %s are unreachable",
                         (unsigned)s_address, (unsigned)err, PCA9685_OUTPUT_SPAN);
            return false;
        }
        return true;
    }
    for (uint8_t channel = 0; channel < PCA9685_CHANNEL_COUNT; ++channel) {
        if ((dirty & (uint16_t)(1u << channel)) == 0) {
            continue;
        }
        uint8_t frame[PCA9685_CHANNEL_FRAME_BYTES] = {};
        pca9685ChannelFrame(channel, desired[channel], frame);
        const uint8_t err = writeRegisters(frame[0], &frame[1], PCA9685_CHANNEL_FRAME_BYTES - 1);
        if (err != 0) {
            PA_LOG_ERROR(TAG, "board 0x%02X stopped answering (error %u, writing %s) - %s are unreachable",
                         (unsigned)s_address, (unsigned)err, pca9685OutputName(channel),
                         PCA9685_OUTPUT_SPAN);
            return false;
        }
    }
    return true;
}

static void pca9685SenderTask(void* pvParameters) {
    (void)pvParameters;
    uint16_t desired[PCA9685_CHANNEL_COUNT] = {};
    while (true) {
        // A change made before this task first ran is still in the mailbox,
        // so the first pass does not wait for a notification.
        uint16_t dirty;
        taskENTER_CRITICAL(&s_mailboxMux);
        memcpy(desired, s_desired, sizeof(desired));
        dirty = s_dirty;
        s_dirty = 0;
        taskEXIT_CRITICAL(&s_mailboxMux);

        if (dirty != 0 && s_answering.load() && !sendBatch(desired, dirty)) {
            s_answering.store(false);
        }
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

bool pca9685StartSender() {
    // Core 0, like AudioTask. Priority 6 is above the HTTP server's task (5,
    // ESP-IDF's HTTPD_DEFAULT_CONFIG, which initPsychicWebServer() leaves as
    // it is), so a release is never queued behind a page load; it holds the
    // core for at most a batch, ~2.3 ms, per ServoTask frame. Not subscribed
    // to the task watchdog: it sleeps until it is given something to send.
    const BaseType_t created = xTaskCreatePinnedToCore(pca9685SenderTask, "Pca9685Task",
                                                       PCA9685_TASK_STACK_BYTES, nullptr, 6,
                                                       &s_sender, 0);
    if (created != pdPASS) {
        s_sender = nullptr;
        s_answering.store(false);
        PA_LOG_ERROR(TAG, "its task could not start - %s will not move", PCA9685_OUTPUT_SPAN);
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// ServoTask's side: record and nothing more
// -----------------------------------------------------------------------------
static bool record(uint8_t channel, uint16_t desired) {
    if (channel >= PCA9685_CHANNEL_COUNT) {
        return false;
    }
    taskENTER_CRITICAL(&s_mailboxMux);
    if (s_desired[channel] != desired) {
        s_desired[channel] = desired;
        s_dirty |= (uint16_t)(1u << channel);
    }
    taskEXIT_CRITICAL(&s_mailboxMux);
    return s_answering.load();
}

bool pca9685Write(uint8_t channel, uint16_t pulseUs) {
    return record(channel, pca9685PulseUsToTicks(pulseUs));
}

bool pca9685Release(uint8_t channel) { return record(channel, PCA9685_OFF); }

// Nothing to configure: every channel is a register block the chip already
// has, full-OFF until it is written.
bool pca9685Attach(uint8_t channel) {
    return channel < PCA9685_CHANNEL_COUNT && s_answering.load();
}

void pca9685Commit() {
    if (s_sender == nullptr) {
        return;
    }
    bool changed;
    taskENTER_CRITICAL(&s_mailboxMux);
    changed = s_dirty != 0;
    taskEXIT_CRITICAL(&s_mailboxMux);
    if (changed) {
        xTaskNotifyGive(s_sender);
    }
}
