// =============================================================================
// include/pca9685.h
//
// The PCA9685 body servo expander (#444, from #306): sixteen more Servo
// Outputs on the I2C header the board already breaks out, `pca:0`-`pca:15`.
// The programming contract is docs/spec-sheets/pca9685-servo-expander.md
// section 7, and every register value below is read from it.
//
// ONE BOARD, AT ONE ADDRESS, NO SCAN (#300 decision 3, "expect one board"). The
// address is a stored Setting defaulting to 0x40, and the four addresses a
// PCA9685 answers that are not its own identity are refused at every door
// (pca9685AddressUsable()): a write to one of them lands on every expander on
// the bus at once - a whole-droid simultaneous move, the brownout ADR 0043 was
// written around. A scan is not done because it renumbers a table that has
// names in it.
//
// CHANNELS ARE THE SILKSCREEN, 0-BASED. `pca:3` is the header the board prints
// 3 beside, written down once here. Three numbering conventions exist and our
// own dome is the 1-based one (spec sheet 9.3); the channel number below is the
// silkscreen's and nothing in this firmware adds or subtracts one.
//
// THE CORE 1 LOOP NEVER WAITS ON THE BUS. ServoTask puts a width on an Output
// and takes one off through include/servo_backend.h, and for this member that
// only records what the channel should be: a desired state per channel and a
// bit saying it changed, under one spinlock, then a task notification at the
// end of ServoTask's frame (pca9685Commit()). A task on Core 0
// (pca9685SenderTask, src/drivers/pca9685.cpp) wakes on it and does the I2C
// writes - the precedent is AudioTask, put on Core 0 because its bit-banged TX
// blocks (src/main.cpp). It is a mailbox, not a queue, on purpose: it holds
// the LATEST state of every channel, so it cannot be full and a release can
// never be dropped for want of room - a later write to the same channel can
// only replace it with a newer answer, which is what the channel should be.
//
// FULL-OFF IS THE RELEASE. A channel let go is written with the full-OFF bit
// (LEDn_OFF_H bit 4), never a 0 % duty: "A channel at 0 % duty is still being
// driven; a channel with full-OFF set is not driven at all" (spec sheet 7.3).
// When every channel is to be off - the estop and Sleep Mode release every
// Output in one frame - the sender writes ALL_LED_OFF_H once, which sets that
// bit on all sixteen in one transaction.
//
// A BUS DROP IS REPORTED, NOT ESCALATED (ADR 0043). A write the chip does not
// acknowledge, or one that times out - or, while nothing is being sent, a
// once-a-second read of MODE1 that goes unanswered - marks the expander as not
// answering for the rest of the session, after one best-effort ALL_LED_OFF_H
// so a chip that can still hear lets its Outputs go: ServoTask then reports
// its sixteen Outputs unreachable and drives nothing on them. Nothing here
// touches drive, the estop or the failsafe gate, and nothing latches an estop
// (ADR 0032).
//
// The constants and the arithmetic are pure and compile in the native build;
// the functions at the bottom are the firmware's (src/drivers/pca9685.cpp).
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ledc_pwm.h"  // SERVO_PULSE_MIN_US / SERVO_PULSE_MAX_US - what a servo width may be

// -----------------------------------------------------------------------------
// The chip
// -----------------------------------------------------------------------------
constexpr uint8_t PCA9685_CHANNEL_COUNT = 16;

// The bus clock, set explicitly rather than inherited (spec sheet 9.4, 12.1):
// at 400 kHz one channel's write is ~140 us, so all sixteen fit in ~2.2 ms of
// a 20 ms ServoTask frame (#306, decided 2026-09-30).
constexpr uint32_t PCA9685_I2C_CLOCK_HZ = 400000;

// How long one bus transaction may take before it is a failure. A healthy
// channel write is ~140 us at the clock above, so this is far past any answer
// the chip gives and short enough that the worst case it adds to a release
// (one timeout, then the expander is not answering) stays a few milliseconds.
constexpr uint16_t PCA9685_BUS_TIMEOUT_MS = 5;

// How long the sender waits with nothing to send before it reads MODE1 once to
// prove the board is still there: an expander unplugged while every Output is
// still is reported unreachable within about this long, not at the next move.
// One ~0.1 ms read a second is nothing to the bus.
constexpr uint32_t PCA9685_LIVENESS_PROBE_MS = 1000;

// The oscillator the prescale is computed against: 25 MHz is the datasheet's
// TYPICAL figure, and the chip may run anywhere from about 23 to 27 MHz (spec
// sheet 7.5). No trim (#306, decided 2026-09-30): a calibrated Output's ends
// were measured against the linkage through this same chip, so its error is in
// the numbers the builder recorded. An uncalibrated `safe range` width can be
// up to ~8 % off.
constexpr uint32_t PCA9685_OSC_HZ = 25000000;
constexpr uint32_t PCA9685_FRAME_HZ = 50;

// prescale = round(osc / (4096 * rate)) - 1 (spec sheet 7.4); 121 at 50 Hz.
constexpr uint8_t PCA9685_PRESCALE =
    (uint8_t)((PCA9685_OSC_HZ + (4096u * PCA9685_FRAME_HZ) / 2u) / (4096u * PCA9685_FRAME_HZ) - 1u);
static_assert(PCA9685_PRESCALE == 121, "spec sheet 7.4: the 50 Hz prescale is 121");

// One count of the 12-bit counter: (prescale + 1) oscillator cycles, 4.88 us.
// What the servo backend's catalogue reports as this member's resolution.
constexpr uint16_t PCA9685_TICK_NS =
    (uint16_t)(((uint32_t)PCA9685_PRESCALE + 1u) * 1000000u / (PCA9685_OSC_HZ / 1000u));

// The register map (spec sheet 7.1).
constexpr uint8_t PCA9685_REG_MODE1 = 0x00;
constexpr uint8_t PCA9685_REG_MODE2 = 0x01;
constexpr uint8_t PCA9685_REG_LED0_ON_L = 0x06;  // LEDn_ON_L = 06h + 4n
constexpr uint8_t PCA9685_REG_ALL_LED_OFF_H = 0xFD;
constexpr uint8_t PCA9685_REG_PRE_SCALE = 0xFE;

// MODE1 and MODE2 bits (spec sheet 7.2). ALLCALL is left clear in every MODE1
// this driver writes, so after bring-up the board no longer answers 0x70 at
// all; SUB1-3 stay clear for the same reason.
constexpr uint8_t PCA9685_MODE1_RESTART = 0x80;
constexpr uint8_t PCA9685_MODE1_AI = 0x20;
constexpr uint8_t PCA9685_MODE1_SLEEP = 0x10;
constexpr uint8_t PCA9685_MODE2_OUTDRV = 0x04;  // totem pole, the chip's default

// Bit 4 of LEDn_ON_H / LEDn_OFF_H: full-ON / full-OFF (spec sheet 7.3).
constexpr uint8_t PCA9685_LED_FULL = 0x10;

// The largest count a pulse may end on. 4096 sets the full-OFF bit - the
// channel switches off instead of driving a long pulse, Adafruit
// writeMicroseconds()'s unclamped trap (spec sheet 8.1) - so a count is never
// allowed to reach it.
constexpr uint16_t PCA9685_TICKS_MAX = 4095;

// "The SLEEP bit must be logic 0 for at least 500 us, before a logic 1 is
// written into the RESTART bit" (spec sheet 7.6). The wait is longer than the
// rule by a margin, because it is the oscillator settling and a wait that only
// just meets it is one a slow chip misses.
constexpr uint32_t PCA9685_RESTART_WAIT_US = 1000;
static_assert(PCA9685_RESTART_WAIT_US >= 500, "spec sheet 7.6: at least 500 us before RESTART");

// -----------------------------------------------------------------------------
// The address
// -----------------------------------------------------------------------------
// Slave address is `1 A5 A4 A3 A2 A1 A0` (spec sheet 7.8): base 0x40, plus the
// six jumpers.
constexpr uint8_t PCA9685_ADDRESS_DEFAULT = 0x40;
constexpr uint8_t PCA9685_ADDRESS_FIRST = 0x40;
constexpr uint8_t PCA9685_ADDRESS_LAST = 0x7F;

// The addresses a PCA9685 answers that are NOT its own identity: 0x70 is LED
// All Call, which every PCA9685 on the bus acknowledges from power-up, and
// 0x71-0x73 are the three sub-addresses. The rule is the reference project's
// own (r2d2-astromech-simulator v1.79.0, arduino/MaestroPCA/src/MpcaScan.h:51),
// reimplemented here from the datasheet facts it encodes.
constexpr bool pca9685AddressReserved(uint8_t address) {
    return address == 0x70 || (address >= 0x71 && address <= 0x73);
}

// An address one board may be strapped to and this firmware will talk to.
constexpr bool pca9685AddressUsable(uint8_t address) {
    return address >= PCA9685_ADDRESS_FIRST && address <= PCA9685_ADDRESS_LAST &&
           !pca9685AddressReserved(address);
}
static_assert(pca9685AddressUsable(PCA9685_ADDRESS_DEFAULT), "the default address is a usable one");

// -----------------------------------------------------------------------------
// Widths to counts
// -----------------------------------------------------------------------------
// A channel's desired state as the mailbox holds it: the count its pulse ends
// on, 1..PCA9685_TICKS_MAX, or this for no pulse at all (full-OFF).
constexpr uint16_t PCA9685_OFF = 0;

// A servo width in microseconds as the count its pulse ends on, rounded to the
// nearest. The width is clamped into what a servo takes first, by this driver
// itself (spec sheet section 12, step 5: convert to ticks yourself, and clamp), so no width
// can reach PCA9685_TICKS_MAX - 2500 us is 512 counts - and none rounds down
// to PCA9685_OFF: 500 us is 102.
constexpr uint16_t pca9685PulseUsToTicks(uint16_t pulseUs) {
    const uint32_t us = pulseUs < SERVO_PULSE_MIN_US   ? SERVO_PULSE_MIN_US
                        : pulseUs > SERVO_PULSE_MAX_US ? SERVO_PULSE_MAX_US
                                                       : pulseUs;
    const uint32_t cycles = us * (PCA9685_OSC_HZ / 1000000u);
    const uint32_t per = (uint32_t)PCA9685_PRESCALE + 1u;
    const uint32_t ticks = (cycles + per / 2u) / per;
    return (uint16_t)(ticks > PCA9685_TICKS_MAX ? PCA9685_TICKS_MAX : ticks);
}
static_assert(pca9685PulseUsToTicks(SERVO_PULSE_MAX_US) < PCA9685_TICKS_MAX,
              "a servo's widest pulse is nowhere near the full-OFF bit");
static_assert(pca9685PulseUsToTicks(SERVO_PULSE_MIN_US) != PCA9685_OFF,
              "a servo's narrowest pulse never reads as no pulse");

// The register block one channel's state is written as, register first, with
// auto-increment on: ON_L, ON_H, OFF_L, OFF_H. Every pulse starts at count 0
// and ends at `desired`; PCA9685_OFF is the full-OFF bit and nothing else.
constexpr size_t PCA9685_CHANNEL_FRAME_BYTES = 5;
inline void pca9685ChannelFrame(uint8_t channel, uint16_t desired,
                                uint8_t frame[PCA9685_CHANNEL_FRAME_BYTES]) {
    const uint16_t ticks = desired > PCA9685_TICKS_MAX ? PCA9685_TICKS_MAX : desired;
    frame[0] = (uint8_t)(PCA9685_REG_LED0_ON_L + 4u * channel);
    frame[1] = 0;
    frame[2] = 0;
    frame[3] = desired == PCA9685_OFF ? 0 : (uint8_t)(ticks & 0xFFu);
    frame[4] = desired == PCA9685_OFF ? PCA9685_LED_FULL : (uint8_t)(ticks >> 8);
}

// -----------------------------------------------------------------------------
// The Outputs' names
// -----------------------------------------------------------------------------
// What an expander Output is called anywhere a log line or a refusal names it:
// its address, since no board prints a name for it (include/servo_output_row.h
// servoOutputAddressName()). Static strings, so a caller on ServoTask's measured
// chain passes a pointer and formats nothing into its own frame.
inline const char* pca9685OutputName(uint8_t channel) {
    static const char* const kNames[PCA9685_CHANNEL_COUNT] = {
        "pca:0", "pca:1", "pca:2",  "pca:3",  "pca:4",  "pca:5",  "pca:6",  "pca:7",
        "pca:8", "pca:9", "pca:10", "pca:11", "pca:12", "pca:13", "pca:14", "pca:15",
    };
    return channel < PCA9685_CHANNEL_COUNT ? kNames[channel] : "pca:?";
}

// The span the one board owns, as every surface spells it.
constexpr const char* PCA9685_OUTPUT_SPAN = "pca:0-pca:15";

// -----------------------------------------------------------------------------
// Firmware - src/drivers/pca9685.cpp
// -----------------------------------------------------------------------------

// Bring the bus up at PCA9685_I2C_CLOCK_HZ and the board at `address` to a
// 50 Hz frame with every channel full-OFF. setup() only, before any task that
// uses it exists: this is where the I2C driver's buffers and its handle for
// the address are taken (Wire.begin(), and the driver's first write to an
// address), so nothing on the bus allocates after it. True when every write
// was acknowledged; false - with a log line naming the consequence - when the
// address is refused, the bus does not come up, or the board does not answer.
bool pca9685Begin(uint8_t address);

// Start the Core 0 task that does the bus writes. setup() only, after
// pca9685Begin() answered true; false with a log line when the task cannot be
// created, and the expander is then not answering.
bool pca9685StartSender();

// Whether the expander answered at bring-up and every write and liveness read
// since. Lock-free:
// one byte, written by setup() and then only ever cleared by the sender.
bool pca9685Answering();

// The address the board was brought up at, as stored.
uint8_t pca9685Address();

// ServoTask's three verbs (include/servo_backend.h), each recording what the
// channel should be and nothing more: no bus, no log, no wait. Each answers
// pca9685Answering(), and false for a channel past the board's sixteen.
bool pca9685Write(uint8_t channel, uint16_t pulseUs);
bool pca9685Release(uint8_t channel);
bool pca9685Attach(uint8_t channel);

// Hand whatever this frame changed to the sender. ServoTask calls it once a
// frame, and once straight after a halt releases every Output, so a frame's
// writes go out together and the estop's sixteen releases are one
// ALL_LED_OFF_H. Non-blocking: a task notification, and none when nothing
// changed.
void pca9685Commit();
