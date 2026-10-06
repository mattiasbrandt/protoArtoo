// =============================================================================
// src/drivers/pca9685.cpp
//
// The PCA9685 body servo expander: its bring-up, the mailbox ServoTask writes
// into, and the Core 0 task that does the bus writes. Contract and rationale:
// include/pca9685.h. Register values: docs/spec-sheets/pca9685-servo-expander.md
// section 6.
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
// pca9685Begin() in setup(), cleared by the sender on the first write - or the
// first idle liveness read - that fails, never set again: a board that dropped off the bus is not trusted to be
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

// MODE1 as the chip holds it: the RESTART bit at bring-up, and the sender's
// proof that an idle board is still there. 0 when it answered, else a Wire
// error code - endTransmission()'s, or 4 ("other error") for a read that
// brought back nothing.
static uint8_t readMode1(uint8_t* value) {
    // The register pointer, then a repeated start and one byte back: Wire
    // sends both as one transaction from requestFrom(), which also releases
    // the bus whatever it returns.
    Wire.beginTransmission(s_address);
    if (Wire.write(PCA9685_REG_MODE1) != 1) {
        Wire.endTransmission();
        return 1;  // the Wire contract's "data too long to fit in transmit buffer"
    }
    const uint8_t err = Wire.endTransmission(false);
    if (err != 0) {
        return err;
    }
    if (Wire.requestFrom(s_address, (size_t)1) != 1 || Wire.available() < 1) {
        return 4;
    }
    *value = (uint8_t)Wire.read();
    return 0;
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

    // The bus, at a clock set here rather than inherited (spec sheet 8.4). This
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
    //      channel full-OFF (spec sheet 6.3), but this controller restarting is
    //      not the expander losing power: after a reboot the chip is still
    //      driving whatever the last session left on it. Writing ALL_LED_OFF_H
    //      before anything else is what makes "limp at start" true across a
    //      restart as well as a power-up. It is also the first write to the
    //      address, which is where the I2C driver adds its device handle.
    //   2. SLEEP, with auto-increment, and ALLCALL and the sub-addresses off.
    //   3. PRE_SCALE, which the chip only takes while asleep (spec sheet 6.4) -
    //      a write while running is silently ignored, which is why this order.
    //   4. MODE2: totem-pole outputs, the chip's own default, stated.
    //   5. Wake, and let the oscillator settle at least 500 us.
    //   6. RESTART, only if the chip says it has PWM to restart (spec sheet
    //      6.6) - never sooner than 500 us after SLEEP went to 0.
    const uint8_t allOff = PCA9685_LED_FULL;
    uint8_t mode1 = 0;
    const bool answered =
        writeRegisters(PCA9685_REG_ALL_LED_OFF_H, &allOff, 1) == 0 &&
        writeRegister(PCA9685_REG_MODE1, PCA9685_MODE1_SLEEP | PCA9685_MODE1_AI) == 0 &&
        writeRegister(PCA9685_REG_PRE_SCALE, PCA9685_PRESCALE) == 0 &&
        writeRegister(PCA9685_REG_MODE2, PCA9685_MODE2_OUTDRV) == 0 && readMode1(&mode1) == 0 &&
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
// An idle board is asked too. With nothing to send for
// PCA9685_LIVENESS_PROBE_MS the sender reads MODE1 once, so an expander
// unplugged while nothing moves is reported unreachable within about a second
// rather than at the next move, and a failed read ends the trust exactly as a
// failed write does.
//
// On that edge, one last ALL_LED_OFF_H before letting go (stopTrusting()). A
// single failed write can be a transient NACK on a chip that is still driving
// the last widths it was given, and from here nothing writes to it again this
// session - the estop included - while every surface says its Outputs are
// limp. One attempt makes the chip match that report whenever it can still
// hear it; whether it was heard is logged, and nothing else changes.
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
// 0 when every write of the batch was acknowledged, else the first failure's
// Wire error code, with `*what` naming what was being written.
static uint8_t sendBatch(const uint16_t* desired, uint16_t dirty, const char** what) {
    bool allOff = true;
    for (uint8_t channel = 0; channel < PCA9685_CHANNEL_COUNT; ++channel) {
        if (desired[channel] != PCA9685_OFF) {
            allOff = false;
            break;
        }
    }
    if (allOff) {
        const uint8_t off = PCA9685_LED_FULL;
        *what = "letting every output go";
        return writeRegisters(PCA9685_REG_ALL_LED_OFF_H, &off, 1);
    }
    for (uint8_t channel = 0; channel < PCA9685_CHANNEL_COUNT; ++channel) {
        if ((dirty & (uint16_t)(1u << channel)) == 0) {
            continue;
        }
        uint8_t frame[PCA9685_CHANNEL_FRAME_BYTES] = {};
        pca9685ChannelFrame(channel, desired[channel], frame);
        const uint8_t err = writeRegisters(frame[0], &frame[1], PCA9685_CHANNEL_FRAME_BYTES - 1);
        if (err != 0) {
            *what = pca9685OutputName(channel);
            return err;
        }
    }
    return 0;
}

// The board stopped answering: trust ends here for the session, and one
// best-effort ALL_LED_OFF_H goes out first (see above). Its result is logged
// on the same line as the failure, and changes nothing either way.
static void stopTrusting(uint8_t err, const char* what) {
    s_answering.store(false);
    const uint8_t off = PCA9685_LED_FULL;
    const bool heard = writeRegisters(PCA9685_REG_ALL_LED_OFF_H, &off, 1) == 0;
    PA_LOG_ERROR(TAG,
                 "board 0x%02X stopped answering (error %u, %s) - %s are unreachable; "
                 "a last all-off write was %s",
                 (unsigned)s_address, (unsigned)err, what, PCA9685_OUTPUT_SPAN,
                 heard ? "acknowledged" : "not acknowledged");
}

static void pca9685SenderTask(void* pvParameters) {
    (void)pvParameters;
    // The sender's own copy of the mailbox, taken under the lock and written
    // from with it released. Static rather than on this frame: there is one
    // sender, and this task's chain - the idle MODE1 read down into the I2C
    // driver's log route - is a measured figure its stack is sized to
    // (ADR 0040), which 32 B on the root frame would push a stack size up.
    static uint16_t desired[PCA9685_CHANNEL_COUNT] = {};
    // The first pass neither waited nor timed out: a change made before this
    // task first ran is still in the mailbox, and is sent now.
    bool idle = false;
    while (true) {
        uint16_t dirty;
        taskENTER_CRITICAL(&s_mailboxMux);
        memcpy(desired, s_desired, sizeof(desired));
        dirty = s_dirty;
        s_dirty = 0;
        taskEXIT_CRITICAL(&s_mailboxMux);

        if (s_answering.load()) {
            const char* what = "";
            uint8_t err = 0;
            if (dirty != 0) {
                err = sendBatch(desired, dirty, &what);
            } else if (idle) {
                uint8_t mode1 = 0;
                err = readMode1(&mode1);
                what = "checking it is still there";
            }
            if (err != 0) {
                stopTrusting(err, what);
            }
        }
        idle = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PCA9685_LIVENESS_PROBE_MS)) == 0;
    }
}

bool pca9685StartSender() {
    // Core 0, like AudioTask. Priority 6 is above the HTTP server's task (5,
    // ESP-IDF's HTTPD_DEFAULT_CONFIG, which initPsychicWebServer() leaves as
    // it is), so a release is never queued behind a page load; it holds the
    // core for at most a batch, ~2.3 ms, per ServoTask frame. Not subscribed
    // to the task watchdog: it sleeps until it is given something to send, or
    // for a liveness read once a second while there is nothing to send.
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
