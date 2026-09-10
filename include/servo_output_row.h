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
//     servoOutputEffectiveEasing() is the only reader of the stored value
//     (ADR 0052).
//   - Calibrating an output never ticks its boot behaviour. Finding an endpoint
//     must not be the act that makes a panel move at power-up, which is why
//     servoOutputCapture() writes an endpoint and the `calibrated` bit and
//     touches nothing else.
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

#include "ledc_pwm.h"     // LedcChannel, SERVO_PULSE_* / ESC_PULSE_* constants
#include "robot_state.h"  // ServoComponentType (firmware and native alike)
#include "servo_component_helpers.h"  // servoCompTypeToString, parseServoCompType

// -----------------------------------------------------------------------------
// Output Address  --  where the lead physically plugs in
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
// firmware already stands in for travel time with  --  seq_open_ms /
// seq_close_ms, the dwell ADR 0041 says should default from the computed figure
// once one exists. SERVO_ACCEL_MS_DEFAULT is a quarter of it, so the default
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

// A channel value that is not an address on any driver. Rows past the table's
// count carry it, so a row nobody has addressed cannot read as channel 0.
constexpr uint8_t SERVO_OUTPUT_CHANNEL_UNSET = 0xFF;

// A catalog part id: `utilUp`, `doorFL`, `pie1`, `other10`. The longest in
// docs/droid-parts.yaml is nine characters; twelve leaves room without making
// the row a place to store a sentence.
constexpr uint8_t SERVO_OUTPUT_PART_ID_MAX = 12;

// How many Parts one Output may drive. A lead Y-harnessed to both breadpan
// doors moves both, and a model that can name only one of them leaves the other
// reading "- not wired -" while it moves anyway, which is a wrong answer rather
// than a missing feature (ADR 0050). Four is the operator's decision of
// 2026-09-10: ADR 0050 says "several" and names no number, and four covers a
// ganged pair with room to spare.
constexpr uint8_t SERVO_OUTPUT_PART_SLOTS = 4;

// ADR 0052 sizes the model at thirteen-to-twenty-three Outputs once an expander
// is fitted. Twenty-four rows covers that with one spare.
constexpr uint8_t SERVO_OUTPUT_ROW_MAX = 24;

// The five LEDC outputs this controller drives today.
constexpr uint8_t SERVO_OUTPUT_ROW_DEFAULT_COUNT = 5;

// -----------------------------------------------------------------------------
// The row
// -----------------------------------------------------------------------------
struct ServoOutputRow {
    ServoOutputDriver driver;  // Output Address, half one
    uint8_t channel;           // Output Address, half two
    // The Parts this output drives, by their Droid Parts Catalog ids, filled
    // slots first and empty slots after. An empty list is legal and means no
    // Part is assigned yet: the droid's own wiring decides what moves, not the
    // catalog. The catalog does not reach firmware as a vocabulary until #301,
    // so an id is checked for shape, not for membership.
    //
    // The multiplicity is asymmetric and both halves matter (ADR 0050). An
    // Output may drive several Parts, so a ganged lead tells the truth about
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
    uint16_t release_ms;  // Output Release: hold after arrival, 0 = never
    ServoEasing easing;   // Motion Profile: the shape of the move
    ServoBootBehaviour boot;        // what this output does at power-up
    ServoComponentType component;   // what is fitted; governs the clamp
    bool calibrated;                // a human measured this against the linkage
};

// The whole table measures 1682 B of static RAM on artoo-esp32 - a 70-byte row
// times twenty-four, plus the count - read with `nm -S` off the linked image
// rather than projected. The budget in tools/build_budgets.json was raised to
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
};

constexpr uint8_t SERVO_OUTPUT_FIELD_COUNT = 13;

// What a repaired row load found, for the one sentence the loader logs. Kept
// small on purpose: it crosses the config load seam by value.
struct ServoOutputRepairReport {
    uint8_t rowsRepaired;     // how many rows needed any repair
    uint8_t firstRow;         // index of the first repaired row
    uint16_t fieldsRepaired;  // how many fields in total across every row
    uint16_t firstRowMask;    // that row's repaired fields, for the receipt
    bool countRepaired;       // the stored row count was out of range
};

// One table, so no surface types a field name (#286: machine vocabulary
// refused mechanically). Index matches ServoOutputField's bit position.
inline const char* servoOutputFieldName(uint8_t bitIndex) {
    static const char* const kNames[SERVO_OUTPUT_FIELD_COUNT] = {
        "driver", "channel", "parts", "open",   "centre",    "close",     "throw",
        "accel",  "release", "ease",  "boot",   "component", "calibrated",
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
// servoOutputIsReversed()
// True when the linkage runs the other way. Derived from the pair every time it
// is asked for, so it can never disagree with the numbers beside it.
// -----------------------------------------------------------------------------
inline bool servoOutputIsReversed(const ServoOutputRow& row) {
    return row.open_us < row.close_us;
}

// -----------------------------------------------------------------------------
// servoOutputEffectiveEasing()
// The easing that actually runs. Overshoot aims past the target and settles
// back, and it must never pass the recorded ends  --  so on an output nobody
// has measured there are no ends to work within and it degrades to `none`
// (ADR 0052). This is the only reader of row.easing: reading the stored value
// directly is how the degrade gets lost.
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
// Endpoint Pair, and mark the row measured.
//
// It deliberately writes exactly two things. Boot behaviour is a separate
// decision from calibration: calibrating must never be the act that makes a
// panel move at power-up, so it is not touched here and no caller may touch it
// on a capture's behalf (#286, ADR 0052).
// -----------------------------------------------------------------------------
inline void servoOutputCapture(ServoOutputRow* row, ServoOutputEnd end, uint16_t pulseUs) {
    if (row == nullptr) {
        return;
    }
    const uint16_t clamped = servoOutputClampPulse(*row, pulseUs);
    switch (end) {
        case SERVO_END_OPEN:
            row->open_us = clamped;
            break;
        case SERVO_END_CENTRE:
            row->centre_us = clamped;
            break;
        case SERVO_END_CLOSE:
            row->close_us = clamped;
            break;
        default:
            return;  // nothing recorded, nothing claimed
    }
    row->calibrated = true;
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
// Shape only: a catalog id is an unquoted identifier, and an empty part means
// no Part is assigned. Membership of docs/droid-parts.yaml is checked once the
// catalog reaches firmware as a compiled vocabulary (#301).
// -----------------------------------------------------------------------------
inline bool servoOutputPartIdIsValid(const char* part) {
    if (part == nullptr) {
        return false;
    }
    size_t len = strnlen(part, SERVO_OUTPUT_PART_ID_MAX + 1);
    if (len > SERVO_OUTPUT_PART_ID_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const char c = part[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
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
// The row a lead plugs into, found by its Output Address. Returns
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
    // the same Part twice from one lead is the same lead.
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

    return repaired;
}

// -----------------------------------------------------------------------------
// servoOutputAdoptFixedPair()
// The bridge a builder's existing calibration crosses (#286, ADR 0041): the two
// numbers a fixed field set held  --  arm1_open_us and arm1_close_us, and the
// same for arm2 and aux1..3  --  become this row's Endpoint Pair.
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
//     rest position usually is.
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
// The component type is settled before the pair, because it decides the band the
// pair is clamped into -- an MG996R row cannot take 500 us however it arrived.
// Normalising against the row as it stood is the partial-edit door
// servoOutputRowNormalise() describes: this is one edit applied over a row that
// already exists, so a field this bridge does not carry keeps its value rather
// than taking a default.
//
// Returns the repair mask, so a number the band moved is reported rather than
// silently lost.
// -----------------------------------------------------------------------------
inline uint16_t servoOutputAdoptFixedPair(ServoOutputRow* row, uint16_t openUs, uint16_t closeUs,
                                          ServoComponentType component) {
    if (row == nullptr) {
        return 0;
    }
    const ServoOutputRow before = *row;
    row->component = component;
    row->open_us = openUs;
    row->close_us = closeUs;
    row->centre_us = (uint16_t)(((uint32_t)openUs + (uint32_t)closeUs) / 2u);
    return servoOutputRowNormalise(row, before);
}

// -----------------------------------------------------------------------------
// servoOutputRowFormat()
// The stored form: thirteen colon-separated fields, words where the model has
// words. An unassigned Part writes "-" rather than an empty field, so a short
// record is a damaged record rather than an ambiguous one.
// -----------------------------------------------------------------------------
// The longest record a full row can produce: 4 driver + 3 channel + 51 parts
// (four ids and three commas) + 15 endpoints + 15 times + 9 "overshoot" + 12
// "home-release" + 6 "mg996r" + 1 calibrated + 12 separators = 128 characters.
// 191 leaves room for a longer word without a format change reaching the wire.
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
        snprintf(buf, bufSize, "%s:%u:%s:%u:%u:%u:%u:%u:%u:%s:%s:%s:%u",
                 servoOutputDriverToString(row.driver), (unsigned)row.channel,
                 partList, (unsigned)row.open_us,
                 (unsigned)row.centre_us, (unsigned)row.close_us, (unsigned)row.throw_ms,
                 (unsigned)row.accel_ms, (unsigned)row.release_ms, servoEasingToString(row.easing),
                 servoBootBehaviourToString(row.boot), servoCompTypeToString(row.component),
                 row.calibrated ? 1u : 0u);
    return written > 0 && (size_t)written < bufSize;
}

// -----------------------------------------------------------------------------
// servoOutputRowParse()
// The storage door. Reads a stored record into `out`, starting from `fallback`,
// and returns the mask of fields it had to repair.
//
// The stored blob is not trusted: a record that is missing, over-long or short
// of fields leaves every field at its fallback and reports all thirteen, rather
// than producing a row that is half somebody's calibration and half zeroes.
// -----------------------------------------------------------------------------
inline uint16_t servoOutputRowParse(const char* raw, const ServoOutputRow& fallback,
                                    ServoOutputRow* out) {
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
    if (tooManyFields || fieldCount != SERVO_OUTPUT_FIELD_COUNT) {
        return kAllFields;
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
