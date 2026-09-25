// =============================================================================
// include/servo_output_row.h
//
// A Servo Output as an addressed row (ADR 0041, ADR 0052, #286).
//
// A servo output stops being a hardcoded field set  --  arm1_open_us,
// arm1_close_us, arm1_type and their siblings  --  and becomes a row carrying
// an Output Address, the Part it drives, a directional Endpoint Pair, a Motion
// Profile, an Output Release, a boot behaviour, a component type and a
// `calibrated` bit. Outputs for a body servo expander are added as rows, not as
// another five fields.
//
// Pure: no NVS, no FreeRTOS, no Arduino String. The storage adapter lives in
// src/config_serializer.cpp and the live cache in src/config_store.cpp; this
// header owns the model and every rule about it, so that no consumer has to
// re-derive one.
//
// Four rules that only hold if nothing works around them:
//
//   - Reverse is `open > close`. There is no invert flag here and no consumer
//     may add one; anything needing an ordering calls servoOutputLowUs() /
//     servoOutputHighUs() rather than sorting the pair itself (ADR 0041).
//   - The component type governs the clamp. An MG996R is a 1000-2000 us part,
//     so an MG996R row can neither store nor be driven to 500 us. 500-2500 us
//     is what *a* servo takes, not what *every* servo takes, and naming a
//     component that takes the wide band is the deliberate act that unlocks it
//     (#286: a safe band by default, the full band an unlock).
//   - Overshoot never passes the recorded ends, so on a row whose `calibrated`
//     bit is unset there are none to work within and it degrades to `none`.
//     servoOutputEffectiveEasing() is the only way a move reads the ease
//     (servoMotionProfileOf(), include/servo_motion_ramp.h); the stored value
//     is read directly only to store it and to report what the builder chose
//     (ADR 0052).
//   - Calibrating an output never ticks its boot behaviour. Finding an endpoint
//     must not be the act that makes a panel move at power-up, which is why
//     servoOutputCapture() writes a position and the `calibrated` bit and
//     touches nothing else -- except the centre, which it drags inside the
//     travel when a captured end has swallowed it, and says so.
//
// Speed and acceleration are *times*, in milliseconds  --  how long a full
// throw takes and how long the move spends getting up to that speed. The rate
// is derived from the Endpoint Pair; the time is what is stored (ADR 0052).
// Neither ever defaults to zero: zero time is an instant move, which on a panel
// means it slams.
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_outputs.h"  // boardOutputOnChannel(), boardOutputLabel() - an Output's name
#include "droid_parts.h"  // droidPartIdIsKnown() - the compiled Part vocabulary
#include "ledc_pwm.h"     // LedcChannel, SERVO_PULSE_* / ESC_PULSE_* constants
#include "robot_state.h"  // ServoComponentType (firmware and native alike)
#include "servo_component_helpers.h"  // servoCompTypeToString, parseServoCompType

// -----------------------------------------------------------------------------
// Output Address  --  where the wire physically plugs in
// -----------------------------------------------------------------------------

// An expander adds a driver here and rows to the table; it never adds a field
// to the row. `ledc` is the ESP32 PWM peripheral this controller drives today.
enum ServoOutputDriver : uint8_t {
    SERVO_DRIVER_LEDC = 0,
    SERVO_DRIVER_COUNT = 1,
};

// -----------------------------------------------------------------------------
// Motion Profile  --  the shape of the move (ADR 0052)
// -----------------------------------------------------------------------------
enum ServoEasing : uint8_t {
    SERVO_EASE_NONE = 0,       // stops dead on the number
    SERVO_EASE_SOFT = 1,       // eases the acceleration itself in
    SERVO_EASE_OVERSHOOT = 2,  // aims a little past the target and settles back
    SERVO_EASE_COUNT = 3,
};

// Limp is 0 deliberately: a zero-filled row is a row that does not move at
// power-up while somebody has their hands in the droid.
enum ServoBootBehaviour : uint8_t {
    SERVO_BOOT_LIMP = 0,          // no pulse; the part stays where it was left
    SERVO_BOOT_HOME_HOLD = 1,     // go to centre and hold it
    SERVO_BOOT_HOME_RELEASE = 2,  // go to centre, then cut the drive
    SERVO_BOOT_COUNT = 3,
};

// -----------------------------------------------------------------------------
// Pulse bands  --  two policy constants, each named once
//
// The reference project's calibration dial sweeps the cautious band until the
// builder ticks *wide*; here the deliberate act is naming a component that
// takes the wider band, because the component type already records which one a
// part is (#286).
// -----------------------------------------------------------------------------
struct ServoPulseBand {
    uint16_t lo;
    uint16_t hi;
};

// 1000-2000 us, the cautious sweep, and exactly what an MG996R takes -- the
// same pair servoTypeDefaultClose()/servoTypeDefaultOpen() already record for
// that part. Written out rather than borrowed from ESC_PULSE_MIN_US /
// ESC_PULSE_MAX_US, which carry the same two numbers for an unrelated reason
// (the dome ESC's range) and would couple a servo policy to an ESC fact.
constexpr ServoPulseBand SERVO_BAND_STD = {1000, 2000};
// 500-2500 us, everything a servo will take. This one IS the servo constant.
constexpr ServoPulseBand SERVO_BAND_ABS = {SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US};

// -----------------------------------------------------------------------------
// Motion Profile bounds and defaults
//
// SERVO_THROW_MS_MIN is one 50 Hz ServoTask tick rounded up: a full throw asked
// for in less than that cannot be resolved by the frame period and is a snap,
// not a ramp. SERVO_THROW_MS_DEFAULT is 1000 because that is the number this
// firmware stood in for travel time with before the Motion Profile existed  --
// seq_open_ms / seq_close_ms, the dwell ADR 0041 said should default from the
// computed figure once one existed, and which went when its only reader, the
// body routine state machine, did (#354, #362). SERVO_ACCEL_MS_DEFAULT is a
// quarter of it, so the default
// move ramps up for a quarter, cruises for a half and ramps down for a quarter
// rather than carrying a constant nothing derives.
// -----------------------------------------------------------------------------
constexpr uint16_t SERVO_THROW_MS_MIN = 20;
constexpr uint16_t SERVO_THROW_MS_MAX = 10000;
constexpr uint16_t SERVO_THROW_MS_DEFAULT = 1000;
constexpr uint16_t SERVO_ACCEL_MS_MIN = 1;
constexpr uint16_t SERVO_ACCEL_MS_MAX = 10000;
constexpr uint16_t SERVO_ACCEL_MS_DEFAULT = SERVO_THROW_MS_DEFAULT / 4;

// Output Release: the bounded hold after the output arrives, after which its
// drive is cut. Zero means "no release" and is the default, because that is
// what this firmware does today  --  a released output is a new behaviour a
// builder asks for, never one a default hands them.
constexpr uint16_t SERVO_RELEASE_MS_NEVER = 0;
constexpr uint16_t SERVO_RELEASE_MS_MAX = 60000;

// A Light Type's settings: how many LEDs are on the wire (CONTEXT.md "Light
// Type", ADR 0067). One per Output, beside the type that says what is on it,
// because a droid may have several lit Parts each on its own wire. Read once
// when a strip starts (src/tasks/aux_led.cpp). One is the default because a
// single indicator LED is the smallest honest strip; zero would be a strip
// that renders nothing while reading as configured. 255 is the type's own
// ceiling and the largest chain this driver addresses.
constexpr uint8_t SERVO_LIGHT_LEDS_MIN = 1;
constexpr uint8_t SERVO_LIGHT_LEDS_MAX = 255;
constexpr uint8_t SERVO_LIGHT_LEDS_DEFAULT = 1;

// A channel value that is not an address on any driver. Rows past the table's
// count carry it, so a row nobody has addressed cannot read as channel 0.
constexpr uint8_t SERVO_OUTPUT_CHANNEL_UNSET = 0xFF;

// A catalog part id: `utilUp`, `doorFL`, `pie1`, `other10`. The longest in
// docs/droid-parts.yaml is nine characters; twelve leaves room without making
// the row a place to store a sentence.
constexpr uint8_t SERVO_OUTPUT_PART_ID_MAX = 12;

// How many Parts one Output may drive. A wire Y-harnessed to both breadpan
// doors moves both, and a model that can name only one of them leaves the other
// reading "- not wired -" while it moves anyway, which is a wrong answer rather
// than a missing feature (ADR 0050). Four is the operator's decision of
// 2026-09-10: ADR 0050 says "several" and names no number, and four covers a
// ganged pair with room to spare.
constexpr uint8_t SERVO_OUTPUT_PART_SLOTS = 4;

// ADR 0052 sizes the model at thirteen-to-twenty-three Outputs once an expander
// is fitted. Twenty-four rows covers that with one spare.
//
// Except on artoo-esp32 while LEDC is its only Output driver: LEDC addresses
// five Outputs there (servoOutputChannelIsValid() below, include/ledc_pwm.h),
// so it holds five rows, and every static byte on that board is a heap byte.
// It goes back to twenty-four when an expander driver lands (operator
// decision 2026-09-25, #428).
//
// `#if defined`: PA_CHIP_TARGET_* are presence macros from config.h (included
// through board_outputs.h above), not 0/1 gates.
#if defined(PA_CHIP_TARGET_ESP32)
constexpr uint8_t SERVO_OUTPUT_ROW_MAX = 5;
#else
constexpr uint8_t SERVO_OUTPUT_ROW_MAX = 24;
#endif

// The five LEDC outputs this controller drives today.
constexpr uint8_t SERVO_OUTPUT_ROW_DEFAULT_COUNT = 5;
static_assert(SERVO_OUTPUT_ROW_DEFAULT_COUNT <= SERVO_OUTPUT_ROW_MAX,
              "the table must hold the Outputs this controller ships with");

// -----------------------------------------------------------------------------
// The row
// -----------------------------------------------------------------------------
struct ServoOutputRow {
    ServoOutputDriver driver;  // Output Address, half one
    uint8_t channel;           // Output Address, half two
    // The Parts this output drives, by their Droid Parts Catalog ids, filled
    // slots first and empty slots after. An empty list is legal and means no
    // Part is assigned yet: the droid's own wiring decides what moves, not the
    // catalog. An id that is not in the catalog vocabulary this build compiled
    // is refused at every door (servoOutputPartIdIsValid), so a stored slot
    // always names a Part the firmware can resolve.
    //
    // The multiplicity is asymmetric and both halves matter (ADR 0050). An
    // Output may drive several Parts, so a ganged wire tells the truth about
    // everything it moves. A Part is driven by at most one Output, because
    // "which Output drives this Part" must have exactly one answer or firmware
    // resolves it by whichever row it scans first. The second half is a rule
    // about the whole table, so it is enforced there:
    // servoOutputTableEnforcePartOwnership().
    char parts[SERVO_OUTPUT_PART_SLOTS][SERVO_OUTPUT_PART_ID_MAX + 1];
    uint16_t open_us;     // Endpoint Pair, directional: reverse is open > close
    uint16_t centre_us;   // the third position; not derived from the other two
    uint16_t close_us;    // Endpoint Pair, directional
    uint16_t throw_ms;    // Motion Profile: how long a full throw takes
    uint16_t accel_ms;    // Motion Profile: how long it spends getting up to speed
    // Output Release: how long this output holds after ARRIVING, 0 = never.
    // Stored, defaulted, validated and serialised -- and read by nothing:
    // ServoTask does not schedule a release from arrival today, so the field is
    // a builder's recorded intention and not yet a behaviour (#364 measured
    // this; ADR 0043 describes the target).
    //
    // WHOEVER BUILDS IT: a release must not fire on an output the calibration
    // dial is holding. That is ADR 0064's suppression, and it is the whole
    // reason the dial exists -- a release fires exactly when the builder has
    // stopped moving a Part in order to look at it, and a struggling servo is
    // only audible while it is being driven. The bit to test is the hold in
    // src/tasks/servo_task.cpp; the two bounds there are what replaces the
    // release for as long as the dial has the output.
    uint16_t release_ms;
    ServoEasing easing;   // Motion Profile: the shape of the move
    ServoBootBehaviour boot;        // what this output does at power-up
    ServoComponentType component;   // what is fitted; governs the clamp
    // The Light Type's settings, meaningful only while `component` names one:
    // how many LEDs the wire carries. It is kept on a row whose component is a
    // servo rather than reset, so naming a servo by mistake and naming the
    // Light Type back does not cost the builder the number they typed.
    uint8_t led_count;
    bool calibrated;                // a human measured this against the linkage
};

// The whole table measures 1730 B of static RAM on artoo-esp32 - a 72-byte row
// times twenty-four, plus the count and its padding - read with `nm -S` off the
// linked image rather than projected. It was 1682 B at a 70-byte row until
// #413 put the Light Type's LED count on it, which cost 2 B a row rather than
// the 1 B the field is: the row is 2-byte aligned and had no hole left. The budget in tools/build_budgets.json was raised to
// 112,000 B for exactly this spend; that budget is heap headroom rather than
// spare DRAM, so a row field is not free even though the segment has room.
struct ServoOutputTable {
    uint8_t count;
    ServoOutputRow rows[SERVO_OUTPUT_ROW_MAX];
};

// -----------------------------------------------------------------------------
// Which end of the Endpoint Pair a capture records
// -----------------------------------------------------------------------------
enum ServoOutputEnd : uint8_t {
    SERVO_END_OPEN = 0,
    SERVO_END_CENTRE = 1,
    SERVO_END_CLOSE = 2,
};

// The word a request names a captured position by (#364). Only the three
// positions a dial can capture into, spelled as the Set MIN / Set CENTER /
// Set MAX buttons mean them rather than as the enum is spelled.
inline bool servoParseOutputEnd(const char* raw, ServoOutputEnd* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(raw, "open") == 0) {
        *out = SERVO_END_OPEN;
        return true;
    }
    if (strcmp(raw, "centre") == 0) {
        *out = SERVO_END_CENTRE;
        return true;
    }
    if (strcmp(raw, "close") == 0) {
        *out = SERVO_END_CLOSE;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Repair reporting  --  a value changing under somebody is said out loud
// -----------------------------------------------------------------------------
enum ServoOutputField : uint16_t {
    SERVO_FIELD_DRIVER = 1u << 0,
    SERVO_FIELD_CHANNEL = 1u << 1,
    SERVO_FIELD_PARTS = 1u << 2,
    SERVO_FIELD_OPEN = 1u << 3,
    SERVO_FIELD_CENTRE = 1u << 4,
    SERVO_FIELD_CLOSE = 1u << 5,
    SERVO_FIELD_THROW_MS = 1u << 6,
    SERVO_FIELD_ACCEL_MS = 1u << 7,
    SERVO_FIELD_RELEASE_MS = 1u << 8,
    SERVO_FIELD_EASING = 1u << 9,
    SERVO_FIELD_BOOT = 1u << 10,
    SERVO_FIELD_COMPONENT = 1u << 11,
    SERVO_FIELD_CALIBRATED = 1u << 12,
    SERVO_FIELD_LED_COUNT = 1u << 13,
};

constexpr uint8_t SERVO_OUTPUT_FIELD_COUNT = 14;

// What a repaired row load found, for the one sentence the loader logs. Kept
// small on purpose: it crosses the config load seam by value.
struct ServoOutputRepairReport {
    uint8_t rowsRepaired;     // how many rows needed any repair
    uint8_t firstRow;         // index of the first repaired row
    uint16_t fieldsRepaired;  // how many fields in total across every row
    uint16_t firstRowMask;    // that row's repaired fields, for the receipt
    bool countRepaired;       // the stored row count was out of range
    // The one lit wire a controller stored before #413 was read onto a row this
    // load (configDeserializeServoOutputs()). `litOutput` is that Output's
    // index in include/board_outputs.h's BOARD_OUTPUTS, which is also the index
    // of its wired tick, so the loader can tick it: on `main` the strip was
    // driven from the stored slot alone, and a lit wire that came back unticked
    // would go dark (#417). Meaningless while `litAdopted` is false.
    bool litAdopted;
    uint8_t litOutput;
    // Filled by the edit door (configCacheApplyServoOutputEdits()) only: bit i
    // set when the component band moved row i's open or close end, so the
    // config POST can say which number it stored instead of the one it was
    // sent (#417). A type-only edit sets them too, for the ends it did not
    // name. 32 bits hold SERVO_OUTPUT_ROW_MAX rows.
    uint32_t openMovedRows;
    uint32_t closeMovedRows;
    // The same for a centre a typed edit left outside the band: a restored
    // row's own centre, or one a type change pulled in (ADR 0068). A capture's
    // centre following a captured end is not here - nobody sent that centre.
    uint32_t centreMovedRows;
};

// One table, so no surface types a field name (#286: machine vocabulary
// refused mechanically). Index matches ServoOutputField's bit position.
inline const char* servoOutputFieldName(uint8_t bitIndex) {
    static const char* const kNames[SERVO_OUTPUT_FIELD_COUNT] = {
        "driver", "channel", "parts", "open",   "centre",    "close",      "throw",
        "accel",  "release", "ease",  "boot",   "component", "calibrated", "leds",
    };
    return (bitIndex < SERVO_OUTPUT_FIELD_COUNT) ? kNames[bitIndex] : "";
}

// -----------------------------------------------------------------------------
// servoComponentBand()
// The band a component type takes. The cautious band is the default for
// anything whose part is unstated: a row that does not say what is fitted gets
// the narrower sweep, never the wider one.
// -----------------------------------------------------------------------------
inline ServoPulseBand servoComponentBand(ServoComponentType component) {
    switch (component) {
        case SERVO_COMP_MG90S:
            return SERVO_BAND_ABS;  // micro servo, recorded as a 500-2500 us part
        case SERVO_COMP_MG996R:
        case SERVO_COMP_RGB:
        case SERVO_COMP_NONE:
        default:
            return SERVO_BAND_STD;
    }
}

// -----------------------------------------------------------------------------
// servoOutputClampPulse()
// The authoritative clamp: a pulse width for this row, bounded by what its
// component type takes. Every door onto a row  --  the store, an edit, a drive
// command  --  goes through this, so an MG996R row cannot reach 500 us by any
// route.
// -----------------------------------------------------------------------------
inline uint16_t servoOutputClampPulse(const ServoOutputRow& row, uint16_t pulseUs) {
    const ServoPulseBand band = servoComponentBand(row.component);
    if (pulseUs < band.lo) {
        return band.lo;
    }
    if (pulseUs > band.hi) {
        return band.hi;
    }
    return pulseUs;
}

// -----------------------------------------------------------------------------
// servoOutputLowUs() / servoOutputHighUs()
// The one place that decides which end of the Endpoint Pair is which. A
// reversed linkage is `open > close` and nothing else records that, so a
// consumer that sorts the pair itself is the beginning of the invert flag this
// model refuses (ADR 0041).
// -----------------------------------------------------------------------------
inline uint16_t servoOutputLowUs(const ServoOutputRow& row) {
    return (row.open_us < row.close_us) ? row.open_us : row.close_us;
}

inline uint16_t servoOutputHighUs(const ServoOutputRow& row) {
    return (row.open_us > row.close_us) ? row.open_us : row.close_us;
}

// -----------------------------------------------------------------------------
// servoOutputEffectiveEasing()
// The easing that actually runs. Overshoot aims past the target and settles
// back, and it must never pass the recorded ends  --  so on an output nobody
// has measured there are no ends to work within and it degrades to `none`
// (ADR 0052). A move reads the ease through this and nothing else --
// servoMotionProfileOf() is its caller, and ServoTask plans from that -- so the
// degrade cannot be lost on the way to the pin. Storage and the config API read
// row.easing directly, because what they report is the builder's choice, not
// what runs.
// -----------------------------------------------------------------------------
inline ServoEasing servoOutputEffectiveEasing(const ServoOutputRow& row) {
    if (row.easing == SERVO_EASE_OVERSHOOT && !row.calibrated) {
        return SERVO_EASE_NONE;
    }
    return row.easing;
}

// -----------------------------------------------------------------------------
// servoOutputCapture()
// Record where the builder has just driven this output as one end of its
// Endpoint Pair or as its centre, and mark the row measured. This is what
// Set MIN / Set CENTER / Set MAX do on the firmware side (#291, #364): the dial
// is already standing at a number the servo is holding, so a capture is one
// assignment -- there is nothing to compute, parse or validate.
//
// Boot behaviour is a separate decision from calibration: calibrating must
// never be the act that makes a panel move at power-up, so it is not touched
// here and no caller may touch it on a capture's behalf (#286, ADR 0052).
//
// A captured END drags the centre inside the travel rather than refusing.
// Capturing a close at 1700 on a row whose centre is 1500 and whose open is
// 1900 leaves the centre where it belongs; capturing one at 1600 puts the
// centre outside the travel the builder has just described, and a centre that
// is not between the ends is a number no later move can honour. Refusing the
// capture would refuse the FIRST number of an ordinary calibration, which is
// the wrong half to protect, so the centre follows and the caller is told.
// (r2d2-astromech-simulator v1.79.0, src/js/maestro/setup-hw-cal.js:610's
// pwCentreFollow: "centre moved to N us -- it was outside the travel you just
// captured".)
//
// Capturing the CENTRE never drags anything: the builder is placing that number
// deliberately, and moving it out from under them would undo the act.
//
// returns: the width the centre was dragged to, or 0 when it did not move.
// -----------------------------------------------------------------------------
inline uint16_t servoOutputCapture(ServoOutputRow* row, ServoOutputEnd end, uint16_t pulseUs) {
    if (row == nullptr) {
        return 0;
    }
    const uint16_t clamped = servoOutputClampPulse(*row, pulseUs);
    switch (end) {
        case SERVO_END_OPEN:
            row->open_us = clamped;
            break;
        case SERVO_END_CENTRE:
            row->centre_us = clamped;
            row->calibrated = true;
            return 0;  // placed deliberately; nothing follows it
        case SERVO_END_CLOSE:
            row->close_us = clamped;
            break;
        default:
            return 0;  // nothing recorded, nothing claimed
    }
    row->calibrated = true;

    // Which end is which comes from the one place that decides it, so a
    // reversed linkage drags the same way round as an ordinary one.
    const uint16_t lo = servoOutputLowUs(*row);
    const uint16_t hi = servoOutputHighUs(*row);
    if (row->centre_us >= lo && row->centre_us <= hi) {
        return 0;
    }
    row->centre_us = (row->centre_us < lo) ? lo : hi;
    return row->centre_us;
}

// -----------------------------------------------------------------------------
// servoOutputChannelIsValid()
// Whether an address names a servo output this driver actually has. LEDC's DOME
// channel drives a brushless ESC, not a servo, so it is not addressable as one.
// -----------------------------------------------------------------------------
inline bool servoOutputChannelIsValid(ServoOutputDriver driver, uint8_t channel) {
    switch (driver) {
        case SERVO_DRIVER_LEDC:
            return channel < LEDC_CH_MAX && channel != LEDC_CH_DOME;
        default:
            return false;
    }
}

// -----------------------------------------------------------------------------
// servoOutputPartIdIsValid()
// Shape, then membership: a catalog id is an unquoted identifier, and it has to
// be one this build actually models. An empty part means no Part is assigned
// and stays legal -- the droid's own wiring decides what moves, so an Output
// with nothing on it is an ordinary answer rather than a damaged row.
//
// The membership half is droidPartIdIsKnown()'s, the compiled vocabulary
// include/droid_parts.h generates from docs/droid-parts.yaml (#301, #356). An
// id no build models is refused here rather than stored, so it is reported by
// whichever door was asked -- servoOutputAddPart() returns false and
// servoOutputRowNormalise() drops the slot and raises SERVO_FIELD_PARTS -- and
// never reaches droidPartAvailabilityFromRow() to be answered as if it were
// unwired hardware.
//
// Shape is checked first and it is not decoration: it bounds the string before
// the vocabulary walk compares it, so an id with no terminator inside the row's
// slot cannot be handed to strcmp().
// -----------------------------------------------------------------------------
inline bool servoOutputPartIdIsValid(const char* part) {
    if (part == nullptr) {
        return false;
    }
    size_t len = strnlen(part, SERVO_OUTPUT_PART_ID_MAX + 1);
    if (len > SERVO_OUTPUT_PART_ID_MAX) {
        return false;
    }
    if (len == 0) {
        return true;  // no Part assigned; nothing to look up
    }
    for (size_t i = 0; i < len; ++i) {
        const char c = part[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return droidPartIdIsKnown(part);
}

// -----------------------------------------------------------------------------
// servoOutputPartCount() / servoOutputPartAt() / servoOutputDrivesPart()
// The row's Part list, read the one way. Slots are kept filled-first, so the
// count is where the first empty slot is and a caller never has to skip holes.
// -----------------------------------------------------------------------------
inline uint8_t servoOutputPartCount(const ServoOutputRow& row) {
    uint8_t count = 0;
    while (count < SERVO_OUTPUT_PART_SLOTS && row.parts[count][0] != '\0') {
        ++count;
    }
    return count;
}

inline const char* servoOutputPartAt(const ServoOutputRow& row, uint8_t slot) {
    return (slot < SERVO_OUTPUT_PART_SLOTS) ? row.parts[slot] : "";
}

inline bool servoOutputDrivesPart(const ServoOutputRow& row, const char* partId) {
    if (partId == nullptr || partId[0] == '\0') {
        return false;
    }
    const uint8_t count = servoOutputPartCount(row);
    for (uint8_t i = 0; i < count; ++i) {
        if (strcmp(row.parts[i], partId) == 0) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// servoOutputClearParts() / servoOutputAddPart() / servoOutputRemovePartAt()
// The three writes the list allows. Add refuses an invalid id, a Part the row
// already drives, and a fifth Part; remove closes the gap so the list stays
// filled-first. None of them is a policy decision - which row keeps a contested
// Part is servoOutputTableEnforcePartOwnership()'s.
// -----------------------------------------------------------------------------
inline void servoOutputClearParts(ServoOutputRow* row) {
    if (row == nullptr) {
        return;
    }
    memset(row->parts, 0, sizeof(row->parts));
}

inline bool servoOutputAddPart(ServoOutputRow* row, const char* partId) {
    if (row == nullptr || partId == nullptr || partId[0] == '\0' ||
        !servoOutputPartIdIsValid(partId) || servoOutputDrivesPart(*row, partId)) {
        return false;
    }
    const uint8_t count = servoOutputPartCount(*row);
    if (count >= SERVO_OUTPUT_PART_SLOTS) {
        return false;
    }
    snprintf(row->parts[count], sizeof(row->parts[count]), "%s", partId);
    return true;
}

inline void servoOutputRemovePartAt(ServoOutputRow* row, uint8_t slot) {
    if (row == nullptr || slot >= SERVO_OUTPUT_PART_SLOTS) {
        return;
    }
    for (uint8_t i = slot; i + 1 < SERVO_OUTPUT_PART_SLOTS; ++i) {
        memcpy(row->parts[i], row->parts[i + 1], sizeof(row->parts[i]));
    }
    row->parts[SERVO_OUTPUT_PART_SLOTS - 1][0] = '\0';
}

// -----------------------------------------------------------------------------
// servoOutputRowDefaults()
// A row nobody has configured: addressed, unassigned, uncalibrated, limp at
// power-up, never released, and with an Endpoint Pair spanning what its
// component type takes  --  which for an MG996R is the 2000/1500/1000 us the
// five fixed field sets already default to.
// -----------------------------------------------------------------------------
inline void servoOutputRowDefaults(ServoOutputRow* row, ServoOutputDriver driver, uint8_t channel,
                                   ServoComponentType component) {
    if (row == nullptr) {
        return;
    }
    const ServoPulseBand band = servoComponentBand(component);
    row->driver = driver;
    row->channel = channel;
    servoOutputClearParts(row);
    row->open_us = band.hi;
    row->centre_us = (uint16_t)((band.lo + band.hi) / 2u);
    row->close_us = band.lo;
    row->throw_ms = SERVO_THROW_MS_DEFAULT;
    row->accel_ms = SERVO_ACCEL_MS_DEFAULT;
    row->release_ms = SERVO_RELEASE_MS_NEVER;
    row->easing = SERVO_EASE_NONE;
    row->boot = SERVO_BOOT_LIMP;
    row->component = component;
    row->led_count = SERVO_LIGHT_LEDS_DEFAULT;
    row->calibrated = false;
}

// -----------------------------------------------------------------------------
// servoOutputTableDefaults()
// The five LEDC outputs this controller drives, with the component types the
// five fixed field sets default to. The two tables agree by construction while
// they coexist; the fixed sets go away when the last caller does.
//
// Every row past the count is defaulted too, rather than zero-filled: a row an
// expander has not claimed yet still carries a real travel time and a limp
// boot, so raising the count can never hand a caller a row that means "move
// instantly and hold".
// -----------------------------------------------------------------------------
inline void servoOutputTableDefaults(ServoOutputTable* table) {
    if (table == nullptr) {
        return;
    }
    const struct {
        uint8_t channel;
        ServoComponentType component;
    } kDefaults[SERVO_OUTPUT_ROW_DEFAULT_COUNT] = {
        {LEDC_CH_ARM1, SERVO_COMP_MG996R}, {LEDC_CH_ARM2, SERVO_COMP_MG996R},
        {LEDC_CH_AUX1, SERVO_COMP_NONE},   {LEDC_CH_AUX2, SERVO_COMP_NONE},
        {LEDC_CH_AUX3, SERVO_COMP_NONE},
    };
    table->count = SERVO_OUTPUT_ROW_DEFAULT_COUNT;
    for (uint8_t i = 0; i < SERVO_OUTPUT_ROW_MAX; ++i) {
        const bool addressed = i < SERVO_OUTPUT_ROW_DEFAULT_COUNT;
        servoOutputRowDefaults(&table->rows[i], SERVO_DRIVER_LEDC,
                               addressed ? kDefaults[i].channel : SERVO_OUTPUT_CHANNEL_UNSET,
                               addressed ? kDefaults[i].component : SERVO_COMP_NONE);
    }
}

// -----------------------------------------------------------------------------
// servoOutputTableFindByAddress()
// The row a wire plugs into, found by its Output Address. Returns
// SERVO_OUTPUT_ROW_MAX when no live row is addressed there, so "there is no
// such output" and "row 0" are not the same answer.
//
// Every consumer that knows a channel and wants the row behind it comes here
// rather than assuming the table is in channel order: rows past
// SERVO_OUTPUT_ROW_DEFAULT_COUNT are an expander's to address, and a table that
// has to stay sorted is a rule nothing enforces.
//
// The lowest-numbered match wins, for the same reason
// servoOutputTableEnforcePartOwnership() picks that one: two rows sharing an
// address is a table that should not exist, and a repair has to be
// deterministic rather than depend on scan order.
// -----------------------------------------------------------------------------
inline uint8_t servoOutputTableFindByAddress(const ServoOutputTable& table,
                                             ServoOutputDriver driver, uint8_t channel) {
    const uint8_t count =
        (table.count <= SERVO_OUTPUT_ROW_MAX) ? table.count : SERVO_OUTPUT_ROW_MAX;
    for (uint8_t i = 0; i < count; ++i) {
        if (table.rows[i].driver == driver && table.rows[i].channel == channel) {
            return i;
        }
    }
    return SERVO_OUTPUT_ROW_MAX;
}

// -----------------------------------------------------------------------------
// Token vocabulary  --  the stored form of the three enums
// -----------------------------------------------------------------------------
inline const char* servoOutputDriverToString(ServoOutputDriver driver) {
    switch (driver) {
        case SERVO_DRIVER_LEDC:
        default:
            return "ledc";
    }
}

inline bool servoOutputParseDriver(const char* raw, ServoOutputDriver* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(raw, "ledc") == 0) {
        *out = SERVO_DRIVER_LEDC;
        return true;
    }
    return false;
}

inline const char* servoEasingToString(ServoEasing easing) {
    switch (easing) {
        case SERVO_EASE_SOFT:
            return "soft";
        case SERVO_EASE_OVERSHOOT:
            return "overshoot";
        case SERVO_EASE_NONE:
        default:
            return "none";
    }
}

inline bool servoParseEasing(const char* raw, ServoEasing* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(raw, "none") == 0) {
        *out = SERVO_EASE_NONE;
        return true;
    }
    if (strcmp(raw, "soft") == 0) {
        *out = SERVO_EASE_SOFT;
        return true;
    }
    if (strcmp(raw, "overshoot") == 0) {
        *out = SERVO_EASE_OVERSHOOT;
        return true;
    }
    return false;
}

inline const char* servoBootBehaviourToString(ServoBootBehaviour boot) {
    switch (boot) {
        case SERVO_BOOT_HOME_HOLD:
            return "home-hold";
        case SERVO_BOOT_HOME_RELEASE:
            return "home-release";
        case SERVO_BOOT_LIMP:
        default:
            return "limp";
    }
}

inline bool servoParseBootBehaviour(const char* raw, ServoBootBehaviour* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(raw, "limp") == 0) {
        *out = SERVO_BOOT_LIMP;
        return true;
    }
    if (strcmp(raw, "home-hold") == 0) {
        *out = SERVO_BOOT_HOME_HOLD;
        return true;
    }
    if (strcmp(raw, "home-release") == 0) {
        *out = SERVO_BOOT_HOME_RELEASE;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// servoOutputParseU16()
// The only coercion this model allows: a string of digits, and nothing else.
//
// Every clamp between a target and the wire is a comparison, so a field that
// silently becomes a number the writer never meant switches those comparisons
// off rather than tripping them. An empty token is not zero, "1e3" is not 1000,
// and a leading sign is not a pulse width  --  each of those comes back false
// so the field takes its safe value and says so (#286).
// -----------------------------------------------------------------------------
inline bool servoOutputParseU16(const char* raw, uint16_t* out) {
    if (raw == nullptr || out == nullptr || raw[0] == '\0') {
        return false;
    }
    uint32_t value = 0;
    for (size_t i = 0; raw[i] != '\0'; ++i) {
        if (raw[i] < '0' || raw[i] > '9') {
            return false;
        }
        value = value * 10u + (uint32_t)(raw[i] - '0');
        if (value > 0xFFFFu) {
            return false;
        }
    }
    *out = (uint16_t)value;
    return true;
}

// -----------------------------------------------------------------------------
// servoOutputFormatAddress() / servoOutputParseAddress()
// An Output Address as one token, `ledc:3`: the spelling a request names an
// Output by, and the one GET /api/servo/outputs hands back, so a surface copies
// the address it read rather than composing one. It is the first two fields of
// the stored row record on purpose -- one address, one spelling.
//
// A parse accepts only an address the driver actually has
// (servoOutputChannelIsValid), so `ledc:2` -- the dome ESC -- is not an Output
// however well it is spelled.
// -----------------------------------------------------------------------------
constexpr size_t SERVO_OUTPUT_ADDRESS_STR_MAX = 8;  // "ledc:255"

inline bool servoOutputFormatAddress(char* buf, size_t bufSize, ServoOutputDriver driver,
                                     uint8_t channel) {
    if (buf == nullptr || bufSize == 0) {
        return false;
    }
    const int written = snprintf(buf, bufSize, "%s:%u", servoOutputDriverToString(driver),
                                 (unsigned)channel);
    return written > 0 && (size_t)written < bufSize;
}

inline bool servoOutputParseAddress(const char* raw, ServoOutputDriver* driver, uint8_t* channel) {
    if (raw == nullptr || driver == nullptr || channel == nullptr) {
        return false;
    }
    const char* sep = strchr(raw, ':');
    if (sep == nullptr) {
        return false;
    }
    char driverToken[8] = {};
    const size_t driverLen = (size_t)(sep - raw);
    if (driverLen == 0 || driverLen >= sizeof(driverToken)) {
        return false;
    }
    memcpy(driverToken, raw, driverLen);
    ServoOutputDriver parsedDriver = SERVO_DRIVER_LEDC;
    uint16_t parsedChannel = 0;
    if (!servoOutputParseDriver(driverToken, &parsedDriver) ||
        !servoOutputParseU16(sep + 1, &parsedChannel) || parsedChannel > 0xFF ||
        !servoOutputChannelIsValid(parsedDriver, (uint8_t)parsedChannel)) {
        return false;
    }
    *driver = parsedDriver;
    *channel = (uint8_t)parsedChannel;
    return true;
}

// -----------------------------------------------------------------------------
// servoOutputAddressName()
// What the running board prints beside the Output at this address - ARM3 on
// the Artoo PCB, GPIO 4 on the FireBeetle 2 - read through the one label
// lookup GET /api/config, POST /api/servo and the Console share
// (include/board_outputs.h). It is also the word that moves the Output, typed
// or sent (ADR 0033 Amendment 2026-09-19). "" for an address no board prints -
// an expander's rows - so a surface shows the address rather than a name that
// is printed nowhere.
// -----------------------------------------------------------------------------
inline const char* servoOutputAddressName(ServoOutputDriver driver, uint8_t channel) {
    if (driver != SERVO_DRIVER_LEDC) {
        return "";
    }
    const BoardOutput* output = boardOutputOnChannel(channel);
    if (output == nullptr) {
        return "";
    }
    const char* label = boardOutputLabel(*output);
    return label != nullptr ? label : "";
}

// -----------------------------------------------------------------------------
// servoOutputRowNormalise()
// One validator, for every door onto a row.
//
// Each field that is out of range takes `fallback`'s value and is recorded in
// the returned mask; the row survives, because rejecting it outright would
// throw away twelve good fields to punish one bad one, and a number the builder
// calibrated should not lose to one nobody can read (#286).
//
// `fallback` is what tells the two doors apart. A whole row  --  the store, a
// full restore, where the row IS the output from now on  --  passes the
// defaults, so a bad field takes the default. A partial row  --  one edit
// applied over what is already there  --  passes the row as it stood, so a bad
// field keeps what it had. Only the receipt names the difference, and
// servoOutputRepairNote()'s `whole` flag is where that wording lives.
//
// Resolution order matters in one place: the component type is settled first,
// because it is what decides the band the three pulse widths are clamped into.
// -----------------------------------------------------------------------------
inline uint16_t servoOutputRowNormalise(ServoOutputRow* row, const ServoOutputRow& fallback) {
    if (row == nullptr) {
        return 0;
    }
    uint16_t repaired = 0;

    if ((uint8_t)row->component > (uint8_t)SERVO_COMP_RGB) {
        row->component = fallback.component;
        repaired |= SERVO_FIELD_COMPONENT;
    }

    if ((uint8_t)row->driver >= (uint8_t)SERVO_DRIVER_COUNT) {
        row->driver = fallback.driver;
        repaired |= SERVO_FIELD_DRIVER;
    }
    if (!servoOutputChannelIsValid(row->driver, row->channel)) {
        row->channel = fallback.channel;
        repaired |= SERVO_FIELD_CHANNEL;
    }

    // The Part list is repaired entry by entry rather than replaced whole: an
    // id nobody can read costs its own slot, never the three beside it that a
    // builder assigned. What is left is compacted so the list stays
    // filled-first, and a Part named twice in one row keeps one slot -- driving
    // the same Part twice from one wire is the same wire.
    for (uint8_t slot = 0; slot < SERVO_OUTPUT_PART_SLOTS; ++slot) {
        row->parts[slot][SERVO_OUTPUT_PART_ID_MAX] = '\0';
    }
    for (uint8_t slot = 0; slot < SERVO_OUTPUT_PART_SLOTS;) {
        const char* id = row->parts[slot];
        bool drop = id[0] != '\0' && !servoOutputPartIdIsValid(id);
        for (uint8_t earlier = 0; !drop && earlier < slot; ++earlier) {
            drop = id[0] != '\0' && strcmp(row->parts[earlier], id) == 0;
        }
        // A filled slot after an empty one is a gap; closing it is the same
        // removal, so it lands in the same branch.
        if (!drop && id[0] == '\0' && servoOutputPartAt(*row, (uint8_t)(slot + 1))[0] != '\0') {
            drop = true;
        }
        if (drop) {
            servoOutputRemovePartAt(row, slot);
            repaired |= SERVO_FIELD_PARTS;
            continue;  // the slot now holds what followed it, so re-check it
        }
        ++slot;
    }

    const uint16_t clampedOpen = servoOutputClampPulse(*row, row->open_us);
    if (clampedOpen != row->open_us) {
        row->open_us = clampedOpen;
        repaired |= SERVO_FIELD_OPEN;
    }
    const uint16_t clampedCentre = servoOutputClampPulse(*row, row->centre_us);
    if (clampedCentre != row->centre_us) {
        row->centre_us = clampedCentre;
        repaired |= SERVO_FIELD_CENTRE;
    }
    const uint16_t clampedClose = servoOutputClampPulse(*row, row->close_us);
    if (clampedClose != row->close_us) {
        row->close_us = clampedClose;
        repaired |= SERVO_FIELD_CLOSE;
    }

    // Zero is the dangerous value for a travel time, not the neutral one, so
    // the floor is a real time rather than a lower bound of nothing.
    if (row->throw_ms < SERVO_THROW_MS_MIN || row->throw_ms > SERVO_THROW_MS_MAX) {
        row->throw_ms = fallback.throw_ms;
        repaired |= SERVO_FIELD_THROW_MS;
    }
    if (row->accel_ms < SERVO_ACCEL_MS_MIN || row->accel_ms > SERVO_ACCEL_MS_MAX) {
        row->accel_ms = fallback.accel_ms;
        repaired |= SERVO_FIELD_ACCEL_MS;
    }
    if (row->release_ms > SERVO_RELEASE_MS_MAX) {
        row->release_ms = fallback.release_ms;
        repaired |= SERVO_FIELD_RELEASE_MS;
    }

    if ((uint8_t)row->easing >= (uint8_t)SERVO_EASE_COUNT) {
        row->easing = fallback.easing;
        repaired |= SERVO_FIELD_EASING;
    }
    if ((uint8_t)row->boot >= (uint8_t)SERVO_BOOT_COUNT) {
        row->boot = fallback.boot;
        repaired |= SERVO_FIELD_BOOT;
    }

    // Zero LEDs is the dangerous value here for the same reason zero travel is
    // above: a strip configured to render nothing reads as a strip that is
    // simply off. The ceiling is the field's own type, so only the floor can be
    // crossed.
    if (row->led_count < SERVO_LIGHT_LEDS_MIN) {
        row->led_count = SERVO_LIGHT_LEDS_DEFAULT;
        repaired |= SERVO_FIELD_LED_COUNT;
    }

    return repaired;
}

// -----------------------------------------------------------------------------
// ServoOutputEdit  --  what somebody asked of one addressed row
//
// An edit is addressed rather than indexed, and it carries only the fields the
// request actually named: `fields` is a mask of SERVO_FIELD_OPEN,
// SERVO_FIELD_CENTRE, SERVO_FIELD_CLOSE, SERVO_FIELD_COMPONENT,
// SERVO_FIELD_LED_COUNT, the Motion Profile's SERVO_FIELD_THROW_MS,
// SERVO_FIELD_ACCEL_MS and SERVO_FIELD_EASING, SERVO_FIELD_BOOT,
// SERVO_FIELD_CALIBRATED and SERVO_FIELD_PARTS, and a field not in it keeps
// what the row had. That is the partial-edit door servoOutputRowNormalise()
// describes, given a shape a pure caller can fill.
//
// A typed edit that names the centre, `calibrated` or the Part list is a row
// posted back whole in the shape GET /api/servo/outputs reads it (ADR 0068):
// a restore, saying what was measured and which Parts were where. No page
// types one of those three; a page captures, reverses and moves a Part, and
// those stay acts.
//
// It carries every act on a row, not only a typed value -- one door, not three
// (#364). `kind` says which act, and that is the whole difference between them.
//
// It exists because the Apply Core for POST /api/config is pure and cannot
// reach the live table (ADR 0011): it validates a builder's numbers and records
// them here, and the Commit Step applies them. Nothing stores an endpoint on
// the way -- the row is the only place one lives (#345).
// -----------------------------------------------------------------------------
// What kind of act one edit is. Three things can happen to a row's widths and
// they mean different things, so the door is told which rather than guessing
// from the fields that came with it (#364).
enum ServoOutputEditKind : uint8_t {
    // Somebody typed numbers into a form. The row records them and claims
    // nothing about anybody having measured the part.
    SERVO_EDIT_TYPED = 0,
    // The dial was standing at a width the servo was holding and the builder
    // pressed Set MIN / Set CENTER / Set MAX. Exactly one of SERVO_FIELD_OPEN /
    // _CENTRE / _CLOSE is named, it goes through servoOutputCapture(), and the
    // row is marked measured. That is the difference between a number somebody
    // entered and a position somebody drove a part to.
    SERVO_EDIT_CAPTURE,
    // The builder ticked `reverse`: the linkage runs the other way, so the two
    // ends trade places. No width travels with it, which is the point -- the
    // swap is made on the row from what the row holds, so a page working from a
    // second-old copy of the pair cannot write a stale number back, and a
    // reverse can never be a way to type one.
    SERVO_EDIT_REVERSE,
};

// How many Parts an edit's list names. The Part list is carried as indices into
// the compiled catalog (include/droid_parts.h) rather than as ids: 4 B a row
// instead of 52, on an edit list sized for a whole table.
static_assert(DROID_PART_COUNT < 0xFF, "an edit names a Part by a one-byte catalog index");

struct ServoOutputEdit {
    ServoOutputDriver driver;      // Output Address, half one
    uint8_t channel;               // Output Address, half two
    uint16_t fields;               // which of the widths below the request carried
    uint16_t open_us;
    uint16_t centre_us;            // a capture's, or a restored row's (ADR 0068)
    uint16_t close_us;
    ServoComponentType component;
    uint8_t led_count;             // the Light Type's setting, when the mask names it
    ServoOutputEditKind kind;
    ServoEasing easing;            // Motion Profile, when the mask names each (#414)
    uint16_t throw_ms;
    uint16_t accel_ms;
    ServoBootBehaviour boot;       // what it does at power-up, when the mask names it
    bool calibrated;               // when the mask names it: a restored row's bit
    // The whole Part list, when the mask names it: `partCount` catalog indices,
    // in the order stated. It replaces the row's list.
    uint8_t partCount;
    uint8_t parts[SERVO_OUTPUT_PART_SLOTS];
};

// -----------------------------------------------------------------------------
// servoOutputApplyEdit()
// One edit, applied over the row as it stands, through the one validator.
//
// Two rules the mask does not express, and both are somebody's data:
//
//   - The component type is settled before the pair, because it decides the
//     band the pulse widths are clamped into. An MG996R row cannot take 500 us
//     however that number arrived, and reading the type late would clamp
//     against the wrong band.
//   - Centre follows the ends while the row is unmeasured, and only then. The
//     surfaces that send an Endpoint Pair have never had a centre to send, so
//     halfway between the builder's own two ends is the only honest guess. Once
//     somebody has captured a position on this output the centre is theirs, and
//     a later edit must not compute over it -- without that guard a measured
//     centre would last exactly until the next form POST.
//
// Returns the repair mask, so a number the band moved is reported rather than
// silently lost. SERVO_FIELD_CENTRE in that mask after a capture is the centre
// having followed the ends, which is a value moving under the builder and is
// reported by the same machinery for the same reason.
// -----------------------------------------------------------------------------
inline uint16_t servoOutputApplyEdit(ServoOutputRow* row, const ServoOutputEdit& edit) {
    if (row == nullptr) {
        return 0;
    }
    const ServoOutputRow before = *row;

    // A capture is one position and the `calibrated` bit, through the one
    // function that knows what capturing means. It never carries a component
    // with it: naming what is fitted is a separate act, and settling a new
    // component here would change the band the captured width is clamped into
    // in the same breath as recording it.
    // Reverse is a swap of the pair and nothing else. It is not a capture: a
    // builder saying which way the linkage runs has not measured anything, and
    // it must not claim they have. The centre does not move -- swapping the two
    // ends does not change the travel between them -- and every consumer that
    // wants an ordering still takes servoOutputLowUs() / servoOutputHighUs(),
    // so there is still no invert flag anywhere (ADR 0041).
    if (edit.kind == SERVO_EDIT_REVERSE) {
        const uint16_t wasOpen = row->open_us;
        row->open_us = row->close_us;
        row->close_us = wasOpen;
        return servoOutputRowNormalise(row, before);
    }

    if (edit.kind == SERVO_EDIT_CAPTURE) {
        ServoOutputEnd end = SERVO_END_CENTRE;
        uint16_t pulseUs = edit.centre_us;
        if ((edit.fields & SERVO_FIELD_OPEN) != 0) {
            end = SERVO_END_OPEN;
            pulseUs = edit.open_us;
        } else if ((edit.fields & SERVO_FIELD_CLOSE) != 0) {
            end = SERVO_END_CLOSE;
            pulseUs = edit.close_us;
        } else if ((edit.fields & SERVO_FIELD_CENTRE) == 0) {
            return 0;  // a capture naming no position records nothing
        }
        const uint16_t draggedTo = servoOutputCapture(row, end, pulseUs);
        uint16_t repaired = servoOutputRowNormalise(row, before);
        if (draggedTo != 0) {
            repaired |= SERVO_FIELD_CENTRE;
        }
        return repaired;
    }

    if ((edit.fields & SERVO_FIELD_COMPONENT) != 0) {
        row->component = edit.component;
    }
    if ((edit.fields & SERVO_FIELD_LED_COUNT) != 0) {
        row->led_count = edit.led_count;
    }
    // The Motion Profile is the builder's to set on any row, measured or not:
    // an unmeasured Output keeps what it was given and moves by none of it
    // until it is calibrated (servoMotionPlan() snaps, and
    // servoOutputEffectiveEasing() degrades an overshoot), so nothing typed
    // here waits on the calibration to be kept.
    if ((edit.fields & SERVO_FIELD_THROW_MS) != 0) {
        row->throw_ms = edit.throw_ms;
    }
    if ((edit.fields & SERVO_FIELD_ACCEL_MS) != 0) {
        row->accel_ms = edit.accel_ms;
    }
    if ((edit.fields & SERVO_FIELD_EASING) != 0) {
        row->easing = edit.easing;
    }
    // Boot behaviour is only ever the builder's own act, named on its own: no
    // capture reaches this line (a capture returned above), which is what keeps
    // "calibrating an output never ticks its boot behaviour" true.
    if ((edit.fields & SERVO_FIELD_BOOT) != 0) {
        row->boot = edit.boot;
    }
    if ((edit.fields & SERVO_FIELD_OPEN) != 0) {
        row->open_us = edit.open_us;
    }
    if ((edit.fields & SERVO_FIELD_CLOSE) != 0) {
        row->close_us = edit.close_us;
    }
    // A restored row says whether anybody measured it, and that is taken as
    // said: a restore replaces what the droid holds (ADR 0056), and it is the
    // one write that may say "not measured" of an Output that was. Settled
    // before the centre, because the centre rule below asks it.
    if ((edit.fields & SERVO_FIELD_CALIBRATED) != 0) {
        row->calibrated = edit.calibrated;
    }
    if ((edit.fields & SERVO_FIELD_CENTRE) != 0) {
        // A stated centre is the builder's number, measured or not, and nothing
        // is computed over it.
        row->centre_us = edit.centre_us;
    } else if (!row->calibrated &&
               (edit.fields & (SERVO_FIELD_OPEN | SERVO_FIELD_CLOSE)) != 0) {
        row->centre_us =
            (uint16_t)(((uint32_t)row->open_us + (uint32_t)row->close_us) / 2u);
    }
    // The list as stated, replacing the row's. A Part it names that another
    // row drove has already been taken off that row
    // (servoOutputTableReleaseStatedParts()), so ownership holds.
    if ((edit.fields & SERVO_FIELD_PARTS) != 0) {
        servoOutputClearParts(row);
        for (uint8_t i = 0; i < edit.partCount && i < SERVO_OUTPUT_PART_SLOTS; ++i) {
            (void)servoOutputAddPart(row, droidPartIdAt(edit.parts[i]));
        }
    }
    return servoOutputRowNormalise(row, before);
}

// -----------------------------------------------------------------------------
// servoOutputAdoptFixedPair()
// The bridge a builder's existing calibration crosses (#286, ADR 0041): the two
// numbers a fixed key set still holds in NVS  --  arm1_op and arm1_cl, and the
// same for arm2 and aux1..3  --  become this row's Endpoint Pair. The fields
// those keys used to fill are gone (#345); the stored keys are read once, on a
// row nothing has written, and the names live in
// include/servo_legacy_field_sets.h.
//
// Three deliberate choices, and each of them is somebody's data:
//
//   - The pair keeps its direction. `open` stays `open` whichever of the two is
//     the larger number, so a builder who calibrated a reversed linkage still
//     has a reversed linkage afterwards. Sorting them here is the invert flag
//     ADR 0041 refuses, arriving by the back door.
//   - Centre takes the midpoint of the pair, not the midpoint of the band. The
//     old form had no centre, so there is nothing to carry; halfway between the
//     builder's own two ends is the only honest guess and it is what a linkage's
//     rest position usually is. It is a *default*, so it is re-derived only
//     while the row is unmeasured: once somebody has captured a position on this
//     output, its centre is theirs and a later crossing must not compute over
//     it. This bridge is crossed again on every config write, so without that
//     guard a measured centre would last exactly until the next form POST.
//   - Everything else keeps what it had. The Part list, the Motion Profile, the
//     Output Release, the boot behaviour and the `calibrated` bit are new
//     fields, and a value nobody stored is not one to infer -- least of all the
//     `calibrated` bit, which decides whether overshoot may run past ends it was
//     never given (servoOutputEffectiveEasing()). Guessing it from "these
//     numbers are not the factory defaults" is wrong in both directions: a
//     builder can measure their way back to 2000/1000, and a half-finished
//     calibration can leave one output moved and untrusted. False is the value
//     that degrades overshoot and warns, so false is what an unmeasured bit is.
//
// An adoption is an edit that carries all three fields, so the resolution order
// and the centre rule are servoOutputApplyEdit()'s rather than restated here.
//
// Returns the repair mask, so a number the band moved is reported rather than
// silently lost.
// -----------------------------------------------------------------------------
inline uint16_t servoOutputAdoptFixedPair(ServoOutputRow* row, uint16_t openUs, uint16_t closeUs,
                                          ServoComponentType component) {
    if (row == nullptr) {
        return 0;
    }
    ServoOutputEdit whole = {};
    whole.driver = row->driver;
    whole.channel = row->channel;
    whole.fields = (uint16_t)(SERVO_FIELD_OPEN | SERVO_FIELD_CLOSE | SERVO_FIELD_COMPONENT);
    whole.open_us = openUs;
    whole.close_us = closeUs;
    whole.component = component;
    return servoOutputApplyEdit(row, whole);
}

// -----------------------------------------------------------------------------
// servoOutputRowFormat()
// The stored form: fourteen colon-separated fields, words where the model has
// words. An unassigned Part writes "-" rather than an empty field, so a short
// record is a damaged record rather than an ambiguous one.
//
// THE LED COUNT IS LAST, AND THAT IS LOAD-BEARING. A controller that stored
// its rows before #413 holds thirteen-field records, and servoOutputRowParse()
// reads one as this shape without its final field rather than as damage. Any
// field added later goes after this one for the same reason; inserting one
// would renumber a builder's stored calibration into the wrong members.
// -----------------------------------------------------------------------------
// The longest record a full row can produce: 4 driver + 3 channel + 51 parts
// (four ids and three commas) + 15 endpoints + 15 times + 9 "overshoot" + 12
// "home-release" + 6 "mg996r" + 1 calibrated + 3 leds + 13 separators = 132
// characters. 191 leaves room for a longer word without a format change
// reaching the wire.
constexpr size_t SERVO_OUTPUT_ROW_STR_MAX = 191;

inline bool servoOutputRowFormat(char* buf, size_t bufSize, const ServoOutputRow& row) {
    if (buf == nullptr || bufSize == 0) {
        return false;
    }

    // The Part list is one field, its ids joined by commas. A comma cannot
    // occur in an id (servoOutputPartIdIsValid), so the inner separator can
    // never be mistaken for the outer one.
    char partList[SERVO_OUTPUT_PART_SLOTS * (SERVO_OUTPUT_PART_ID_MAX + 1)] = {};
    const uint8_t partCount = servoOutputPartCount(row);
    if (partCount == 0) {
        snprintf(partList, sizeof(partList), "-");
    } else {
        size_t used = 0;
        for (uint8_t i = 0; i < partCount; ++i) {
            const int n = snprintf(partList + used, sizeof(partList) - used, "%s%s",
                                   i == 0 ? "" : ",", row.parts[i]);
            if (n <= 0 || (size_t)n >= sizeof(partList) - used) {
                return false;
            }
            used += (size_t)n;
        }
    }

    const int written =
        snprintf(buf, bufSize, "%s:%u:%s:%u:%u:%u:%u:%u:%u:%s:%s:%s:%u:%u",
                 servoOutputDriverToString(row.driver), (unsigned)row.channel,
                 partList, (unsigned)row.open_us,
                 (unsigned)row.centre_us, (unsigned)row.close_us, (unsigned)row.throw_ms,
                 (unsigned)row.accel_ms, (unsigned)row.release_ms, servoEasingToString(row.easing),
                 servoBootBehaviourToString(row.boot), servoCompTypeToString(row.component),
                 row.calibrated ? 1u : 0u, (unsigned)row.led_count);
    return written > 0 && (size_t)written < bufSize;
}

// -----------------------------------------------------------------------------
// servoOutputRowParse()
// The storage door. Reads a stored record into `out`, starting from `fallback`,
// and returns the mask of fields it had to repair.
//
// The stored blob is not trusted: a record that is missing, over-long or short
// of fields leaves every field at its fallback and reports all fourteen, rather
// than producing a row that is half somebody's calibration and half zeroes.
//
// ONE OLD SHAPE IS NOT DAMAGE. A record of SERVO_OUTPUT_FIELD_COUNT - 1 fields
// is what a controller stored before the LED count joined the row (#413). Its
// thirteen fields mean exactly what they mean now, so they are read and the
// missing one is left at the fallback, and nothing is reported: a builder's
// calibration surviving an upgrade is not a repair. Any shorter record is
// still damage, because no shape this firmware ever wrote was shorter.
//
// `oldShape`, when given, says which of the two it was. The loader needs it:
// a thirteen-field record was written before the retired light keys were read
// onto rows, so it cannot carry their answer, and a fourteen-field one can
// (#417). It is false for an unreadable record as well as a current one.
// -----------------------------------------------------------------------------
inline uint16_t servoOutputRowParse(const char* raw, const ServoOutputRow& fallback,
                                    ServoOutputRow* out, bool* oldShapeOut = nullptr) {
    if (oldShapeOut != nullptr) {
        *oldShapeOut = false;
    }
    if (out == nullptr) {
        return 0;
    }
    *out = fallback;

    constexpr uint16_t kAllFields = (uint16_t)((1u << SERVO_OUTPUT_FIELD_COUNT) - 1u);

    const bool unreadable = raw == nullptr || raw[0] == '\0' ||
                            strnlen(raw, SERVO_OUTPUT_ROW_STR_MAX + 1) > SERVO_OUTPUT_ROW_STR_MAX;
    if (unreadable) {
        return kAllFields;
    }

    char work[SERVO_OUTPUT_ROW_STR_MAX + 1];
    snprintf(work, sizeof(work), "%s", raw);

    char* fields[SERVO_OUTPUT_FIELD_COUNT] = {};
    uint8_t fieldCount = 0;
    char* cursor = work;
    bool tooManyFields = false;
    for (;;) {
        if (fieldCount == SERVO_OUTPUT_FIELD_COUNT) {
            tooManyFields = true;  // a fourteenth separator: not this record's shape
            break;
        }
        fields[fieldCount++] = cursor;
        char* sep = strchr(cursor, ':');
        if (sep == nullptr) {
            break;
        }
        *sep = '\0';
        cursor = sep + 1;
    }
    // SERVO_OUTPUT_FIELD_COUNT - 1 is the pre-#413 shape; see the header note.
    const bool oldShape = fieldCount == (uint8_t)(SERVO_OUTPUT_FIELD_COUNT - 1);
    if (tooManyFields || (fieldCount != SERVO_OUTPUT_FIELD_COUNT && !oldShape)) {
        return kAllFields;
    }
    if (oldShapeOut != nullptr) {
        *oldShapeOut = oldShape;
    }

    uint16_t repaired = 0;

    // Component type first: it decides the band the three pulse widths below
    // are clamped into, so reading it late would clamp them against the wrong
    // one.
    const ServoComponentType parsedComponent = parseServoCompType(fields[11]);
    if (strcmp(fields[11], servoCompTypeToString(parsedComponent)) != 0) {
        repaired |= SERVO_FIELD_COMPONENT;  // unknown word, not the "none" it maps to
    } else {
        out->component = parsedComponent;
    }

    ServoOutputDriver driver = SERVO_DRIVER_LEDC;
    if (servoOutputParseDriver(fields[0], &driver)) {
        out->driver = driver;
    } else {
        repaired |= SERVO_FIELD_DRIVER;
    }

    uint16_t channel = 0;
    if (servoOutputParseU16(fields[1], &channel) && channel <= 0xFF) {
        out->channel = (uint8_t)channel;
    } else {
        repaired |= SERVO_FIELD_CHANNEL;
    }

    // The Part list: "-" for none, otherwise up to four comma-separated ids.
    // An entry that cannot be read, one the row already drives, and a fifth all
    // cost themselves and nothing else - a row with three good Parts keeps
    // them.
    servoOutputClearParts(out);
    if (strcmp(fields[2], "-") != 0) {
        char* entry = fields[2];
        while (entry != nullptr) {
            char* comma = strchr(entry, ',');
            if (comma != nullptr) {
                *comma = '\0';
            }
            if (!servoOutputAddPart(out, entry)) {
                repaired |= SERVO_FIELD_PARTS;
            }
            entry = (comma != nullptr) ? comma + 1 : nullptr;
        }
    }

    struct NumericField {
        uint8_t index;
        uint16_t ServoOutputRow::*member;
        uint16_t bit;
    };
    const NumericField kNumeric[] = {
        {3, &ServoOutputRow::open_us, SERVO_FIELD_OPEN},
        {4, &ServoOutputRow::centre_us, SERVO_FIELD_CENTRE},
        {5, &ServoOutputRow::close_us, SERVO_FIELD_CLOSE},
        {6, &ServoOutputRow::throw_ms, SERVO_FIELD_THROW_MS},
        {7, &ServoOutputRow::accel_ms, SERVO_FIELD_ACCEL_MS},
        {8, &ServoOutputRow::release_ms, SERVO_FIELD_RELEASE_MS},
    };
    for (size_t i = 0; i < sizeof(kNumeric) / sizeof(kNumeric[0]); ++i) {
        uint16_t value = 0;
        if (servoOutputParseU16(fields[kNumeric[i].index], &value)) {
            out->*(kNumeric[i].member) = value;
        } else {
            repaired |= kNumeric[i].bit;
        }
    }

    ServoEasing easing = SERVO_EASE_NONE;
    if (servoParseEasing(fields[9], &easing)) {
        out->easing = easing;
    } else {
        repaired |= SERVO_FIELD_EASING;
    }

    ServoBootBehaviour boot = SERVO_BOOT_LIMP;
    if (servoParseBootBehaviour(fields[10], &boot)) {
        out->boot = boot;
    } else {
        repaired |= SERVO_FIELD_BOOT;
    }

    if (strcmp(fields[12], "0") == 0) {
        out->calibrated = false;
    } else if (strcmp(fields[12], "1") == 0) {
        out->calibrated = true;
    } else {
        // Anything unreadable leaves the row uncalibrated: a row nobody can
        // read must never claim a human measured it.
        out->calibrated = false;
        repaired |= SERVO_FIELD_CALIBRATED;
    }

    if (!oldShape) {
        uint16_t leds = 0;
        if (servoOutputParseU16(fields[13], &leds) && leds <= SERVO_LIGHT_LEDS_MAX) {
            out->led_count = (uint8_t)leds;
        } else {
            repaired |= SERVO_FIELD_LED_COUNT;
        }
    }

    repaired |= servoOutputRowNormalise(out, fallback);
    return repaired;
}

// -----------------------------------------------------------------------------
// servoOutputTableEnforcePartOwnership()
// The other half of ADR 0050's asymmetry, and the only rule that cannot live on
// a single row: a Part is driven by at most one Output.
//
// It matters because *which Output drives this Part* must have exactly one
// answer. Two rows claiming one Part would leave firmware resolving it by
// whichever row it happens to scan first, so the droid's behaviour would depend
// on table order.
//
// The lowest-numbered row keeps a contested Part and the later ones lose their
// slot, reported rather than refused. That is a repair of a table that should
// never have existed, not the model's reassignment rule: moving a Part to
// another Output is an act at the write door, which clears the old row itself
// (ADR 0050 - "assigning it elsewhere MOVES it"). First-wins is chosen here
// only because a repair has to be deterministic, and scan order is exactly what
// the rule exists to stop mattering.
//
// Returns a bitmask of the rows that lost a Part, bit i for row i.
// -----------------------------------------------------------------------------
static_assert(SERVO_OUTPUT_ROW_MAX <= 32,
              "the ownership pass reports affected rows in a uint32_t bitmask");

inline uint32_t servoOutputTableEnforcePartOwnership(ServoOutputTable* table) {
    if (table == nullptr) {
        return 0;
    }
    const uint8_t count =
        (table->count <= SERVO_OUTPUT_ROW_MAX) ? table->count : SERVO_OUTPUT_ROW_MAX;
    uint32_t affected = 0;
    for (uint8_t i = 1; i < count; ++i) {
        for (uint8_t slot = 0; slot < SERVO_OUTPUT_PART_SLOTS;) {
            const char* id = table->rows[i].parts[slot];
            if (id[0] == '\0') {
                break;  // filled-first, so the rest of this row is empty too
            }
            bool claimed = false;
            for (uint8_t earlier = 0; !claimed && earlier < i; ++earlier) {
                claimed = servoOutputDrivesPart(table->rows[earlier], id);
            }
            if (claimed) {
                servoOutputRemovePartAt(&table->rows[i], slot);
                affected |= (uint32_t)1u << i;
                continue;  // the slot now holds what followed it
            }
            ++slot;
        }
    }
    return affected;
}

// -----------------------------------------------------------------------------
// servoOutputTableFindPart()
// The row that drives a Part, or SERVO_OUTPUT_ROW_MAX when no live row does.
// servoOutputTableEnforcePartOwnership() means there is at most one; were there
// ever two, the lowest-numbered wins here for the same reason it wins there.
// -----------------------------------------------------------------------------
inline uint8_t servoOutputTableFindPart(const ServoOutputTable& table, const char* partId) {
    const uint8_t count =
        (table.count <= SERVO_OUTPUT_ROW_MAX) ? table.count : SERVO_OUTPUT_ROW_MAX;
    for (uint8_t i = 0; i < count; ++i) {
        if (servoOutputDrivesPart(table.rows[i], partId)) {
            return i;
        }
    }
    return SERVO_OUTPUT_ROW_MAX;
}

// -----------------------------------------------------------------------------
// ServoOutputPartMove / servoOutputTableMovePart()
// Putting a Part on an Output, taking it off every Output, or moving it from one
// to another: the act ADR 0050 means by "assigning it elsewhere MOVES it", and
// the write door servoOutputTableEnforcePartOwnership() says is not its job.
//
// A move names BOTH ends -- the Output the Part is being taken from as well as
// the one it is going to -- and a move whose stated origin is not where the Part
// actually is changes nothing. That is what makes announcing a move a rule
// rather than a habit (#347): a surface can only take a Part off an Output it has
// already read the Part on, which is exactly the moment it has everything it
// needs to say so first, and a table that changed underneath it is refused
// rather than silently written over. Every surface meets the same refusal - the
// part-first table, the output-first one, guided Setup and any import.
//
// Every refusal is decided before anything is touched, so a refused move leaves
// the table exactly as it was. The two list writes are servoOutputRemovePartAt()
// and servoOutputAddPart(), the same ones every other door uses.
// -----------------------------------------------------------------------------
struct ServoOutputPartMove {
    char part[SERVO_OUTPUT_PART_ID_MAX + 1];
    bool fromOutput;  // false: the request says the Part is on no Output now
    ServoOutputDriver fromDriver;
    uint8_t fromChannel;
    bool toOutput;  // false: take the Part off every Output
    ServoOutputDriver toDriver;
    uint8_t toChannel;
};

enum ServoPartMoveOutcome : uint8_t {
    SERVO_PART_MOVED = 0,         // the Part is now where the move sent it
    SERVO_PART_ALREADY_THERE,     // nothing to do, and nothing wrong
    SERVO_PART_NOT_WHERE_STATED,  // the table disagrees with the origin the move named
    SERVO_PART_OUTPUT_FULL,       // the destination already drives SERVO_OUTPUT_PART_SLOTS Parts
    SERVO_PART_NO_SUCH_OUTPUT,    // no live row is addressed where the move sends it
    SERVO_PART_NOT_A_PART,        // not an id this build models
};

inline ServoPartMoveOutcome servoOutputTableMovePart(ServoOutputTable* table,
                                                     const ServoOutputPartMove& move) {
    // Shape first, so an id with no terminator inside its slot never reaches
    // the vocabulary walk -- the same order servoOutputPartIdIsValid() keeps.
    if (table == nullptr || strnlen(move.part, sizeof(move.part)) >= sizeof(move.part) ||
        move.part[0] == '\0' || !servoOutputPartIdIsValid(move.part)) {
        return SERVO_PART_NOT_A_PART;
    }

    const uint8_t destination =
        move.toOutput ? servoOutputTableFindByAddress(*table, move.toDriver, move.toChannel)
                      : SERVO_OUTPUT_ROW_MAX;
    if (move.toOutput && destination >= SERVO_OUTPUT_ROW_MAX) {
        return SERVO_PART_NO_SUCH_OUTPUT;
    }

    // An origin naming an Output no live row has is a table the sender did not
    // read, even when the Part happens to be on nothing: both halves of that
    // statement have to hold, not just the comparison below.
    const uint8_t stated =
        move.fromOutput ? servoOutputTableFindByAddress(*table, move.fromDriver, move.fromChannel)
                        : SERVO_OUTPUT_ROW_MAX;
    const uint8_t current = servoOutputTableFindPart(*table, move.part);
    if ((move.fromOutput && stated >= SERVO_OUTPUT_ROW_MAX) || current != stated) {
        return SERVO_PART_NOT_WHERE_STATED;
    }

    if (current == destination) {
        return SERVO_PART_ALREADY_THERE;
    }
    if (destination < SERVO_OUTPUT_ROW_MAX &&
        servoOutputPartCount(table->rows[destination]) >= SERVO_OUTPUT_PART_SLOTS) {
        return SERVO_PART_OUTPUT_FULL;
    }

    if (current < SERVO_OUTPUT_ROW_MAX) {
        const uint8_t count = servoOutputPartCount(table->rows[current]);
        for (uint8_t slot = 0; slot < count; ++slot) {
            if (strcmp(table->rows[current].parts[slot], move.part) == 0) {
                servoOutputRemovePartAt(&table->rows[current], slot);
                break;
            }
        }
    }
    if (destination < SERVO_OUTPUT_ROW_MAX) {
        // Cannot refuse here: the id is valid, the destination is not the row
        // the Part was on, ownership means no other row drove it, and the slot
        // count was checked above.
        (void)servoOutputAddPart(&table->rows[destination], move.part);
    }
    return SERVO_PART_MOVED;
}

// -----------------------------------------------------------------------------
// servoOutputTableReleaseStatedParts()
// The half of a stated Part list that reaches past its own row (ADR 0068).
//
// A row posted whole names every Part on it, and a Part is on at most one
// Output (ADR 0050). So before the edits land, every Part a stated list names
// comes off whichever OTHER live row drives it now - assigning a Part elsewhere
// moves it. The edits themselves then write each list (servoOutputApplyEdit()).
//
// Only edits addressed at a live row count: an edit for an address no row has
// changes nothing (configCacheApplyServoOutputEdits()), and a Part it names
// must not be taken off the Output it is really on for a row that does not
// exist. Two lists naming one Part are refused before this runs, by the Apply
// Core, so the order edits are taken in cannot matter.
//
// Returns a bitmask of the rows that lost a Part, bit i for row i.
// -----------------------------------------------------------------------------
inline uint32_t servoOutputTableReleaseStatedParts(ServoOutputTable* table,
                                                   const ServoOutputEdit* edits, size_t count) {
    if (table == nullptr || edits == nullptr) {
        return 0;
    }
    uint32_t affected = 0;
    for (size_t e = 0; e < count; ++e) {
        const ServoOutputEdit& edit = edits[e];
        if (edit.kind != SERVO_EDIT_TYPED || (edit.fields & SERVO_FIELD_PARTS) == 0) {
            continue;
        }
        const uint8_t target = servoOutputTableFindByAddress(*table, edit.driver, edit.channel);
        if (target >= SERVO_OUTPUT_ROW_MAX) {
            continue;
        }
        for (uint8_t i = 0; i < edit.partCount && i < SERVO_OUTPUT_PART_SLOTS; ++i) {
            const char* id = droidPartIdAt(edit.parts[i]);
            const uint8_t holder = servoOutputTableFindPart(*table, id);
            if (holder >= SERVO_OUTPUT_ROW_MAX || holder == target) {
                continue;
            }
            const uint8_t held = servoOutputPartCount(table->rows[holder]);
            for (uint8_t slot = 0; slot < held; ++slot) {
                if (strcmp(table->rows[holder].parts[slot], id) == 0) {
                    servoOutputRemovePartAt(&table->rows[holder], slot);
                    affected |= (uint32_t)1u << holder;
                    break;
                }
            }
        }
    }
    return affected;
}

// -----------------------------------------------------------------------------
// servoOutputRepairNote()
// One sentence, said the same way by every door. Returns the length written.
// -----------------------------------------------------------------------------
inline size_t servoOutputRepairNote(uint16_t repaired, bool whole, char* buf, size_t bufSize) {
    if (buf == nullptr || bufSize == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (repaired == 0) {
        return 0;
    }
    size_t used = 0;
    for (uint8_t bit = 0; bit < SERVO_OUTPUT_FIELD_COUNT; ++bit) {
        if ((repaired & (uint16_t)(1u << bit)) == 0) {
            continue;
        }
        const int written = snprintf(buf + used, bufSize - used, "%s%s", used == 0 ? "" : ", ",
                                     servoOutputFieldName(bit));
        if (written <= 0 || (size_t)written >= bufSize - used) {
            return strnlen(buf, bufSize);
        }
        used += (size_t)written;
    }
    const int tail = snprintf(buf + used, bufSize - used, "%s",
                              whole ? " took the safe default" : " kept what was there");
    if (tail > 0 && (size_t)tail < bufSize - used) {
        used += (size_t)tail;
    }
    return used;
}
