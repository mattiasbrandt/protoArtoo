// =============================================================================
// src/seq_json.cpp
//
// Learned Sequence JSON format v1 parse/serialize (ADR 0006).
// See header for the format. Uses ArduinoJson (bounded per-call document).
// =============================================================================

#include "seq_json.h"

#include <string.h>

#include <ArduinoJson.h>

#include "audio_playback_policy.h"  // AudioPlaybackCategory/Slot + audioCategoryToString
#include "sequence_gesture.h"       // the Gesture vocabulary and its stored layout

// Result constructors (pcOk/pcFail/pcFailAt) are shared inlines in
// protocol_check.h so callers get one error shape.

// -----------------------------------------------------------------------------
// Enum <-> string
// -----------------------------------------------------------------------------
bool seqToggleGroupFromString(const char* s, SeqToggleGroup& out) {
    if (s == nullptr) return false;
    if (strcmp(s, "none") == 0)  { out = TOGGLE_NONE;  return true; }
    if (strcmp(s, "pies") == 0)  { out = TOGGLE_PIES;  return true; }
    if (strcmp(s, "low") == 0)   { out = TOGGLE_LOW;   return true; }
    if (strcmp(s, "all") == 0)   { out = TOGGLE_ALL;   return true; }
    if (strcmp(s, "user1") == 0) { out = TOGGLE_USER1; return true; }
    if (strcmp(s, "user2") == 0) { out = TOGGLE_USER2; return true; }
    if (strcmp(s, "user3") == 0) { out = TOGGLE_USER3; return true; }
    if (strcmp(s, "user4") == 0) { out = TOGGLE_USER4; return true; }
    return false;
}
const char* seqToggleGroupToString(SeqToggleGroup g) {
    switch (g) {
        case TOGGLE_NONE:  return "none";
        case TOGGLE_PIES:  return "pies";
        case TOGGLE_LOW:   return "low";
        case TOGGLE_ALL:   return "all";
        case TOGGLE_USER1: return "user1";
        case TOGGLE_USER2: return "user2";
        case TOGGLE_USER3: return "user3";
        case TOGGLE_USER4: return "user4";
        default:           return "none";
    }
}

bool seqSlotSetFromString(const char* s, SeqSlotSet& out) {
    if (s == nullptr) return false;
    if (strcmp(s, "ring") == 0) { out = SLOTSET_RING; return true; }
    if (strcmp(s, "pie") == 0)  { out = SLOTSET_PIE;  return true; }
    if (strcmp(s, "all") == 0)  { out = SLOTSET_ALL;  return true; }
    if (strcmp(s, "hold") == 0) { out = SLOTSET_HOLD; return true; }
    return false;
}
const char* seqSlotSetToString(SeqSlotSet s) {
    switch (s) {
        case SLOTSET_RING: return "ring";
        case SLOTSET_PIE:  return "pie";
        case SLOTSET_ALL:  return "all";
        case SLOTSET_HOLD: return "hold";
        default:           return "ring";
    }
}

static bool randomModeFromString(const char* s, uint8_t& out) {
    if (s == nullptr) return false;
    if (strcmp(s, "flutter") == 0) { out = RAND_FLUTTER; return true; }
    if (strcmp(s, "open") == 0)    { out = RAND_OPEN;    return true; }
    if (strcmp(s, "close") == 0)   { out = RAND_CLOSE;   return true; }
    return false;
}
static const char* randomModeToString(uint8_t mode) {
    switch (mode) {
        case RAND_OPEN:    return "open";
        case RAND_CLOSE:   return "close";
        case RAND_FLUTTER:
        default:           return "flutter";
    }
}

// Move Shape token -> value. The SAME three words for a servo Part and a light
// Part: the surface names them by Part Kind (open/close/flutter against
// on/off/flash), and the stored token is one either way, which is what lets a
// Gesture spread one shape across a mixed set (ADR 0049).
//
// Walks seqBodyShapeToString() rather than restating the words, the same way
// categoryFromString() below walks audioCategoryToString(), so the vocabulary
// has one home (include/sequence_engine.h) and the two directions cannot drift.
static bool bodyShapeFromString(const char* s, uint8_t& out) {
    if (s == nullptr) return false;
    for (uint8_t shape = 0; shape < BODY_SHAPE_COUNT; ++shape) {
        if (strcmp(seqBodyShapeToString(shape), s) == 0) {
            out = shape;
            return true;
        }
    }
    return false;
}

// Audio category label <-> enum (reuse the canonical audioCategoryToString).
static bool categoryFromString(const char* s, uint8_t& out) {
    if (s == nullptr) return false;
    for (uint8_t c = 0; c < AUDIO_CATEGORY_COUNT; ++c) {
        if (strcmp(audioCategoryToString((AudioPlaybackCategory)c), s) == 0) {
            out = c;
            return true;
        }
    }
    return false;
}

// Named-slot label <-> enum (only the slots usable as an audio fallback).
struct SlotLabel { const char* label; uint8_t slot; };
static const SlotLabel kSlotLabels[] = {
    { "none",      AUDIO_SLOT_NONE },
    { "scream",    AUDIO_SLOT_NAMED_SCREAM },
    { "faint",     AUDIO_SLOT_NAMED_FAINT },
    { "leia",      AUDIO_SLOT_NAMED_LEIA },
    { "cantina_s", AUDIO_SLOT_NAMED_CANTINA_S },
    { "sw_theme",  AUDIO_SLOT_NAMED_SW_THEME },
    { "imp_march", AUDIO_SLOT_NAMED_IMP_MARCH },
    { "cantina_l", AUDIO_SLOT_NAMED_CANTINA_L },
    { "startup",   AUDIO_SLOT_NAMED_STARTUP },
    { "disco",     AUDIO_SLOT_NAMED_DISCO },
    { "happy",     AUDIO_SLOT_NAMED_HAPPY },
};
static const uint8_t kSlotLabelCount =
    (uint8_t)(sizeof(kSlotLabels) / sizeof(kSlotLabels[0]));

static bool slotFromString(const char* s, uint8_t& out) {
    if (s == nullptr) return false;
    for (uint8_t i = 0; i < kSlotLabelCount; ++i) {
        if (strcmp(kSlotLabels[i].label, s) == 0) {
            out = kSlotLabels[i].slot;
            return true;
        }
    }
    return false;
}
static const char* slotToString(uint8_t slot) {
    for (uint8_t i = 0; i < kSlotLabelCount; ++i) {
        if (kSlotLabels[i].slot == slot) return kSlotLabels[i].label;
    }
    return "none";
}

// -----------------------------------------------------------------------------
// Wide reads that saturate rather than wrap
//
// A value past what its field can store is saturated to the field's own
// maximum, which every bound in protocolCheckTempo() refuses -- so an
// out-of-range number is refused with the same words as one just past the
// bound, and never wraps into a plausible value on the way there. A negative
// saturates the same way, because no field here can mean less than zero.
// -----------------------------------------------------------------------------
static uint32_t saturateU32(long long v) {
    return (v < 0 || v > 0xFFFFFFFFLL) ? 0xFFFFFFFFu : (uint32_t)v;
}
static uint16_t saturateU16(long long v) {
    return (v < 0 || v > 0xFFFFLL) ? (uint16_t)0xFFFFu : (uint16_t)v;
}
static uint8_t saturateU8(long long v) {
    return (v < 0 || v > 0xFFLL) ? (uint8_t)0xFFu : (uint8_t)v;
}
// A number scaled to an integer unit (tenths of a BPM, thousandths of a
// confidence), rounded half up and saturated like the three above.
static uint16_t scaledU16(double v, double scale) {
    const double scaled = v * scale + 0.5;
    return (scaled < 0.0 || scaled >= 65536.0) ? (uint16_t)0xFFFFu : (uint16_t)scaled;
}

// A whole number on the wire, or refused. A beat is an index, so 1.5 is not
// one; the browser writes whole numbers and nothing else produces a beat.
static bool wholeNumber(JsonVariantConst v, long long& out) {
    if (!v.is<long long>()) return false;
    out = v.as<long long>();
    return true;
}

// -----------------------------------------------------------------------------
// The tempo block (ADR 0058), or absent. `present` says which: a sequence
// saved before tempos existed has none and parses exactly as it always did.
// A block that is present is judged whole by protocolCheckTempo() before any
// beat is resolved against it.
// -----------------------------------------------------------------------------
static ProtocolCheckResult parseTempo(JsonVariantConst v, SeqTempo& tempo, bool& present) {
    memset(&tempo, 0, sizeof(tempo));
    present = false;
    if (v.isNull()) {
        return pcOk();
    }
    if (!v.is<JsonObjectConst>()) {
        return pcFail("tempo", "tempo must be an object");
    }
    present = true;
    JsonObjectConst obj = v.as<JsonObjectConst>();

    JsonVariantConst bpm = obj["bpm"];
    if (!bpm.is<double>()) {
        return pcFail("tempo.bpm", "bpm must be a number");
    }
    tempo.bpmTenths = scaledU16(bpm.as<double>(), 10.0);

    long long whole = 0;
    JsonVariantConst phase = obj["phase"];
    if (!phase.isNull()) {
        if (!wholeNumber(phase, whole)) return pcFail("tempo.phase", "phase must be whole ms");
        tempo.phaseMs = saturateU32(whole);
    }
    tempo.barLen = SEQ_TEMPO_BAR_LEN_DEFAULT;
    JsonVariantConst barLen = obj["barLen"];
    if (!barLen.isNull()) {
        if (!wholeNumber(barLen, whole)) return pcFail("tempo.barLen", "barLen must be whole beats");
        tempo.barLen = saturateU8(whole);
    }
    JsonVariantConst barPhase = obj["barPhase"];
    if (!barPhase.isNull()) {
        if (!wholeNumber(barPhase, whole)) return pcFail("tempo.barPhase", "barPhase must be a whole beat");
        tempo.barPhase = saturateU8(whole);
    }
    JsonVariantConst duration = obj["duration"];
    if (!duration.isNull()) {
        if (!wholeNumber(duration, whole)) return pcFail("tempo.duration", "duration must be whole ms");
        tempo.durationMs = saturateU32(whole);
    }

    const char* source = obj["source"] | (const char*)nullptr;
    if (source == nullptr || !seqTempoSourceFromString(source, tempo.source)) {
        return pcFail("tempo.source", "source must be typed, tapped or analysed");
    }

    JsonVariantConst confidence = obj["confidence"];
    if (!confidence.is<double>()) {
        return pcFail("tempo.confidence", "confidence must be a number");
    }
    tempo.confidencePermille = scaledU16(confidence.as<double>(), 1000.0);

    // The hash is not kept: firmware never compares it, because the droid
    // never holds the file it was taken from (ADR 0046). Its form is checked
    // so a stored one is something the browser can compare against a dropped
    // copy -- lowercase hex -- and that it came from the analysed route.
    JsonVariantConst hash = obj["hash"];
    if (!hash.isNull()) {
        const char* h = hash.as<const char*>();
        const size_t len = (h != nullptr) ? strnlen(h, SEQ_TEMPO_HASH_MAX_LEN + 1) : 0;
        if (h == nullptr || len == 0 || len > SEQ_TEMPO_HASH_MAX_LEN) {
            return pcFail("tempo.hash", "hash must be 1..64 hex characters");
        }
        for (size_t i = 0; i < len; ++i) {
            const char c = h[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return pcFail("tempo.hash", "hash must be lowercase hex");
            }
        }
        tempo.hasHash = true;
    }
    return protocolCheckTempo(tempo);
}

// -----------------------------------------------------------------------------
// A Gesture (ADR 0046, include/sequence_gesture.h). Its Parts are `set`, one
// token, or `parts`, a list of Part ids -- never both -- and travel as the
// payload. Every other field is optional and absent means its default, so a
// clone reads as the builder authored it. Whether the set is declared, the
// Parts known and on one half, and the numbers inside their bounds are
// Protocol Check's; this is the wire.
// -----------------------------------------------------------------------------
static ProtocolCheckResult parseGestureToken(const char* label, uint8_t idx, JsonObjectConst obj,
                                            const char* key, const char* (*spell)(uint8_t),
                                            uint8_t count, uint8_t& out) {
    const char* token = obj[key] | (const char*)nullptr;
    if (token == nullptr) return pcOk();  // absent: the default, stored as zero
    if (!seqGestureTokenFromString(token, spell, count, out)) {
        return pcFailAt(label, idx, key, "unknown word");
    }
    return pcOk();
}

static ProtocolCheckResult parseGestureMs(const char* label, uint8_t idx, JsonObjectConst obj,
                                         const char* key, uint32_t max, uint32_t& out) {
    JsonVariantConst v = obj[key];
    if (v.isNull()) return pcOk();
    long long whole = 0;
    if (!wholeNumber(v, whole) || whole < 0 || whole > (long long)max) {
        return pcFailAt(label, idx, key, "out of range");
    }
    out = (uint32_t)whole;
    return pcOk();
}

static ProtocolCheckResult parseGestureFields(const char* label, JsonObjectConst obj,
                                             uint8_t idx, SeqStep& s) {
    s.type = STEP_GESTURE;
    const char* set = obj["set"] | (const char*)nullptr;
    JsonVariantConst parts = obj["parts"];
    if ((set != nullptr) == !parts.isNull()) {
        return pcFailAt(label, idx, "set", "give a set or a list of parts");
    }
    if (set != nullptr) {
        if (strnlen(set, sizeof(s.payload)) >= sizeof(s.payload)) {
            return pcFailAt(label, idx, "set", "set too long");
        }
        strncpy(s.payload, set, sizeof(s.payload) - 1);
    } else {
        if (!parts.is<JsonArrayConst>() || parts.as<JsonArrayConst>().size() == 0) {
            return pcFailAt(label, idx, "parts", "parts must be a list of Part ids");
        }
        // Joined with commas into the payload; a list that does not fit is
        // refused rather than cut, so a stored Gesture never loses a Part.
        size_t used = 0;
        for (JsonVariantConst v : parts.as<JsonArrayConst>()) {
            const char* id = v.as<const char*>();
            const size_t len = (id != nullptr) ? strnlen(id, sizeof(s.payload)) : 0;
            if (len == 0 || strchr(id, ',') != nullptr) {
                return pcFailAt(label, idx, "parts", "parts must be a list of Part ids");
            }
            if (used + (used > 0 ? 1 : 0) + len >= sizeof(s.payload)) {
                return pcFailAt(label, idx, "parts", "too many parts to list; use a set");
            }
            if (used > 0) s.payload[used++] = ',';
            memcpy(s.payload + used, id, len);
            used += len;
            s.payload[used] = '\0';
        }
    }

    uint8_t token = 0;
    ProtocolCheckResult r = parseGestureToken(label, idx, obj, "shape", seqBodyShapeToString,
                                              BODY_SHAPE_COUNT, token);
    if (!r.ok) return r;
    s.params.shape = token;
    token = 0;
    r = parseGestureToken(label, idx, obj, "spread", seqGestureSpreadToString, GESTURE_SPREAD_COUNT, token);
    if (!r.ok) return r;
    seqGestureSetSpread(s.params, token);
    token = 0;
    r = parseGestureToken(label, idx, obj, "direction", seqGestureDirectionToString, GESTURE_DIR_COUNT, token);
    if (!r.ok) return r;
    seqGestureSetDirection(s.params, token);
    token = 0;
    r = parseGestureToken(label, idx, obj, "start", seqGestureStartToString, GESTURE_START_COUNT, token);
    if (!r.ok) return r;
    seqGestureSetStart(s.params, token);
    token = 0;
    r = parseGestureToken(label, idx, obj, "easing", seqGestureEasingToString, GESTURE_EASING_COUNT, token);
    if (!r.ok) return r;
    seqGestureSetEasing(s.params, token);

    // howFar as a Body Step has it: zero is how absence is stored, so a stated
    // zero is refused here, the one place that can tell the two apart.
    JsonVariantConst howFar = obj["howFar"];
    if (!howFar.isNull()) {
        long long v = 0;
        if (!wholeNumber(howFar, v) || v < 1 || v > (long long)SEQ_BODY_HOWFAR_MAX) {
            return pcFailAt(label, idx, "howFar", "howFar must be 1..100");
        }
        s.params.howFar = (uint8_t)v;
    }

    // The times, read wide and stored in the member each one borrows. A value
    // past the member's width is refused here, since saturating it would hand
    // Protocol Check a number nobody wrote.
    uint32_t ms = 0;
    r = parseGestureMs(label, idx, obj, "flutterMs", 0xFFFFu, ms);
    if (!r.ok) return r;
    s.params.flutterMs = (uint16_t)ms;
    ms = 0;
    r = parseGestureMs(label, idx, obj, "stepMs", 0xFFFFu, ms);
    if (!r.ok) return r;
    seqGestureSetStepMs(s.params, (uint16_t)ms);
    ms = 0;
    r = parseGestureMs(label, idx, obj, "speedMs", 0xFFFFu, ms);
    if (!r.ok) return r;
    seqGestureSetSpeedMs(s.params, (uint16_t)ms);
    ms = 0;
    r = parseGestureMs(label, idx, obj, "repeatMs", 0xFFFFu, ms);
    if (!r.ok) return r;
    seqGestureSetRepeatMs(s.params, (uint16_t)ms);
    ms = 0;
    r = parseGestureMs(label, idx, obj, "extentMs", 0xFFFFFFFFu, ms);
    if (!r.ok) return r;
    seqGestureSetExtentMs(s.params, ms);
    return pcOk();
}

// -----------------------------------------------------------------------------
// Parse one step object into `s`. `idx` is for error fields.
// -----------------------------------------------------------------------------
static ProtocolCheckResult parseStepFields(const char* label, JsonObjectConst obj,
                                           uint8_t idx, SeqStep& s) {
    memset(&s, 0, sizeof(s));
    s.effectClass = FX_NONE;  // Protocol Check stamps this

    s.tMs = obj["t"] | 0u;

    const char* type = obj["type"] | (const char*)nullptr;
    if (type == nullptr) {
        return pcFailAt(label, idx, "type", "missing type");
    }

    if (strcmp(type, "dome") == 0 || strcmp(type, "audio") == 0) {
        const char* cmd = obj["cmd"] | (const char*)nullptr;
        if (cmd == nullptr) {
            return pcFailAt(label, idx, "cmd", "missing cmd");
        }
        if (strnlen(cmd, sizeof(s.payload)) >= sizeof(s.payload)) {
            return pcFailAt(label, idx, "cmd", "command too long");
        }
        strncpy(s.payload, cmd, sizeof(s.payload) - 1);
        bool isAudio = strcmp(type, "audio") == 0;
        s.type = isAudio ? STEP_AUDIO : STEP_DOME_CMD;
        // A dome panel step's howFar, as a Body Step's: 1..100, absent the
        // whole throw, zero refused here where "said 0" and "said nothing"
        // can still be told apart. Which commands may carry it is Protocol
        // Check's (include/sequence_dome_how_far.h).
        JsonVariantConst howFar = obj["howFar"];
        if (!howFar.isNull()) {
            long long v = 0;
            if (isAudio || !wholeNumber(howFar, v) || v < 1 || v > (long long)SEQ_BODY_HOWFAR_MAX) {
                return pcFailAt(label, idx, "howFar", "howFar must be 1..100 on a panel move");
            }
            s.params.howFar = (uint8_t)v;
        }
        if (isAudio) {
            JsonVariantConst boundAudio = obj["boundAudio"];
            if (!boundAudio.isNull() && !boundAudio.is<bool>()) {
                return pcFailAt(label, idx, "boundAudio", "must be a boolean");
            }
            s.params.audioBounded = (boundAudio.is<bool>() ? boundAudio.as<bool>() : true) ? 1 : 0;
        }
        return pcOk();
    }
    if (strcmp(type, "loop") == 0) {
        s.type = STEP_LOOP;
        s.params.bodyCount  = (uint8_t)(obj["body"] | 0);
        s.params.periodMs   = (uint16_t)(obj["periodMs"] | 0);
        s.params.durationMs = (uint32_t)(obj["durationMs"] | 0u);
        return pcOk();
    }
    if (strcmp(type, "random") == 0) {
        s.type = STEP_RANDOM;
        SeqSlotSet set = SLOTSET_RING;
        const char* setStr = obj["set"] | (const char*)nullptr;
        if (setStr == nullptr || !seqSlotSetFromString(setStr, set)) {
            return pcFailAt(label, idx, "set", "missing or unknown slot set");
        }
        uint8_t mode = RAND_FLUTTER;
        const char* modeStr = obj["mode"] | "flutter";
        if (!randomModeFromString(modeStr, mode)) {
            return pcFailAt(label, idx, "mode", "missing or unknown random mode");
        }
        if (obj["pulseMin"].is<int>() || obj["pulseMax"].is<int>()) {
            return pcFailAt(label, idx, "pulse", "random pulse ranges are not supported");
        }
        s.params.slotSet      = (uint8_t)set;
        s.params.pulseMin     = mode;
        s.params.pulseMax     = 0;
        s.params.moveMs       = (uint16_t)(obj["moveMs"] | 0);
        s.params.jitterMs     = (uint16_t)(obj["jitterMs"] | 0);
        s.params.pickDistinct = (obj["distinct"] | false) ? 1 : 0;
        return pcOk();
    }
    if (strcmp(type, "audioCat") == 0) {
        s.type = STEP_AUDIO_CATEGORY;
        uint8_t cat = 0, fb = AUDIO_SLOT_NONE;
        const char* catStr = obj["category"] | (const char*)nullptr;
        if (catStr == nullptr || !categoryFromString(catStr, cat)) {
            return pcFailAt(label, idx, "category", "missing or unknown category");
        }
        const char* fbStr = obj["fallback"] | "none";
        if (!slotFromString(fbStr, fb)) {
            return pcFailAt(label, idx, "fallback", "unknown fallback slot");
        }
        s.params.audioCategory     = cat;
        s.params.audioFallbackSlot = fb;
        return pcOk();
    }
    if (strcmp(type, "domeRotate") == 0) {
        s.type = STEP_DOME_ROTATE;
        int32_t speedPct = obj["speedPct"] | 0;
        // Read durationMs wide+signed so a malformed negative (or a value past
        // uint32_t) is rejected here instead of silently wrapping to a huge ms.
        long long durationMsSigned = obj["durationMs"] | 0LL;
        if (durationMsSigned < 0 || durationMsSigned > 0xFFFFFFFFLL) {
            return pcFailAt(label, idx, "durationMs", "must be 0..4294967295");
        }
        uint32_t durationMs = (uint32_t)durationMsSigned;

        // Validate speedPct: must be in -100..100
        if (speedPct < -100 || speedPct > 100) {
            return pcFailAt(label, idx, "speedPct", "must be -100..100");
        }

        // Validate durationMs: must be positive, EXCEPT the explicit neutral stop
        // (speedPct == 0 && durationMs == 0 is the only valid zero case)
        if (speedPct == 0 && durationMs == 0) {
            // Explicit neutral stop  --  valid
        } else if (durationMs == 0) {
            // Non-zero speed with zero duration  --  reject
            return pcFailAt(label, idx, "durationMs", "must be positive (or both speedPct and durationMs must be 0 for neutral stop)");
        } else if (speedPct == 0 && durationMs > 0) {
            // Zero speed with positive duration  --  reject (ambiguous intent)
            return pcFailAt(label, idx, "speedPct", "non-zero speed required when durationMs > 0 (or use speedPct=0, durationMs=0 for neutral stop)");
        }

        s.params.speedPct   = (int8_t)speedPct;
        s.params.durationMs = durationMs;
        return pcOk();
    }
    if (strcmp(type, "body") == 0) {
        s.type = STEP_BODY;
        const char* part = obj["part"] | (const char*)nullptr;
        if (part == nullptr) {
            return pcFailAt(label, idx, "part", "missing part");
        }
        if (strnlen(part, sizeof(s.payload)) >= sizeof(s.payload)) {
            return pcFailAt(label, idx, "part", "part too long");
        }
        // The Part id travels in the payload, as the catalog spells it. Whether
        // the catalog declares it is protocolCheck()'s to say, like every other
        // semantic bound in this parser.
        strncpy(s.payload, part, sizeof(s.payload) - 1);

        // Absent shape means the default, and absent is how the default is
        // stored, so there is nothing to write when the key is missing.
        const char* shapeStr = obj["shape"] | (const char*)nullptr;
        if (shapeStr != nullptr) {
            uint8_t shape = 0;
            if (!bodyShapeFromString(shapeStr, shape)) {
                return pcFailAt(label, idx, "shape", "unknown move shape");
            }
            s.params.shape = shape;
        }

        // howFar is 1..100 on the wire. Zero is refused HERE and only here,
        // because this is the one place that can tell "the author said 0" from
        // "the author said nothing" -- and zero in storage is how absence is
        // recorded. Read wide and signed so a negative is rejected rather than
        // wrapping into a plausible percentage.
        JsonVariantConst howFar = obj["howFar"];
        if (!howFar.isNull()) {
            const long long v = howFar | 0LL;
            if (v < 1 || v > (long long)SEQ_BODY_HOWFAR_MAX) {
                return pcFailAt(label, idx, "howFar", "howFar must be 1..100");
            }
            s.params.howFar = (uint8_t)v;
        }

        JsonVariantConst flutterMs = obj["flutterMs"];
        if (!flutterMs.isNull()) {
            const long long v = flutterMs | 0LL;
            if (v < 0 || v > 0xFFFFLL) {
                return pcFailAt(label, idx, "flutterMs", "must be 0..65535");
            }
            s.params.flutterMs = (uint16_t)v;
        }
        return pcOk();
    }
    if (strcmp(type, "gesture") == 0) {
        return parseGestureFields(label, obj, idx, s);
    }
    if (strcmp(type, "end") == 0) {
        s.type = STEP_END;
        return pcOk();
    }
    return pcFailAt(label, idx, "type", "unknown step type");
}

// -----------------------------------------------------------------------------
// A step's beat and its span in beats (ADR 0058, ADR 0060), resolved into the
// milliseconds the engine runs. The beat is what the builder meant and the
// millisecond is what runs, so when a step carries both the beat wins: the
// browser writes `t` resolved from the beat beside it, and a tempo edited
// since is exactly the case where the two disagree and the beat is right.
//
// `tempo` is nullptr when the sequence has none, and a beat without a tempo
// has nothing to count in. `inLoopBody` is whether this step is timed from a
// loop pass rather than from the sequence start: a beat there would count
// from a moment the grid knows nothing about, so it is refused and the loop
// itself goes on the beat instead.
// -----------------------------------------------------------------------------
static ProtocolCheckResult parseStepBeats(const char* label, JsonObjectConst obj,
                                          uint8_t idx, const SeqTempo* tempo,
                                          bool inLoopBody, SeqStep& s) {
    long long whole = 0;
    JsonVariantConst beat = obj["beat"];
    if (!beat.isNull()) {
        if (tempo == nullptr) {
            return pcFailAt(label, idx, "beat", "a beat needs a tempo");
        }
        if (inLoopBody) {
            return pcFailAt(label, idx, "beat", "a step in a loop is timed from its pass");
        }
        if (!wholeNumber(beat, whole) || whole < 0 || whole > (long long)SEQ_TEMPO_BEAT_MAX) {
            return pcFailAt(label, idx, "beat", "beat must be a whole 0..1200");
        }
        s.tMs = seqTempoBeatMs(*tempo, (uint32_t)whole);
    }

    JsonVariantConst span = obj["spanBeats"];
    if (!span.isNull()) {
        if (tempo == nullptr) {
            return pcFailAt(label, idx, "spanBeats", "a span in beats needs a tempo");
        }
        if (!wholeNumber(span, whole) || whole < 1 || whole > (long long)SEQ_TEMPO_BEAT_MAX) {
            return pcFailAt(label, idx, "spanBeats", "spanBeats must be a whole 1..1200");
        }
        const uint32_t ms = seqTempoSpanMs(*tempo, (uint32_t)whole);
        // A span replaces the one duration the step already has; the bounds on
        // that duration are Protocol Check's, applied to the resolved value
        // exactly as they would be to a typed one.
        if (s.type == STEP_DOME_ROTATE) {
            if (s.params.speedPct == 0) {
                return pcFailAt(label, idx, "spanBeats", "a turn with a span needs a speed");
            }
            s.params.durationMs = ms;
        } else if (s.type == STEP_BODY && s.params.shape == BODY_SHAPE_FLUTTER) {
            s.params.flutterMs = (ms > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)ms;
        } else {
            return pcFailAt(label, idx, "spanBeats", "only a turn or a flutter lasts a span");
        }
    }

    if (s.type == STEP_GESTURE) {
        // A Gesture's pace, repeat and extent in beats (ADR 0060), each
        // replacing the millisecond it resolves to. With a tempo and no pace
        // stated, the pace is one beat: "a wave, one panel per beat".
        static const char* const kBeatKeys[] = { "stepBeats", "repeatBeats", "extentBeats" };
        for (uint8_t k = 0; k < 3; ++k) {
            JsonVariantConst v = obj[kBeatKeys[k]];
            if (v.isNull()) continue;
            if (tempo == nullptr) {
                return pcFailAt(label, idx, kBeatKeys[k], "beats need a tempo");
            }
            if (!wholeNumber(v, whole) || whole < 1 || whole > (long long)SEQ_TEMPO_BEAT_MAX) {
                return pcFailAt(label, idx, kBeatKeys[k], "must be a whole 1..1200");
            }
            const uint32_t spanMs = seqTempoSpanMs(*tempo, (uint32_t)whole);
            const uint16_t narrow = (spanMs > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)spanMs;
            if (k == 0) seqGestureSetStepMs(s.params, narrow);
            else if (k == 1) seqGestureSetRepeatMs(s.params, narrow);
            else seqGestureSetExtentMs(s.params, spanMs);
        }
        if (tempo != nullptr && s.params.moveMs == 0) {
            const uint32_t beat = seqTempoSpanMs(*tempo, 1);
            seqGestureSetStepMs(s.params, (uint16_t)(beat > 0xFFFFu ? 0xFFFFu : beat));
        }
    }
    return pcOk();
}

// A repeating Gesture that states no extent runs the length of the track, and
// never past the sequence's own end (ADR 0060): the track's remaining length
// when the tempo knows it, and the time to the end step when that comes first
// or the track's length is unknown. Resolved once the whole branch is read,
// because the end step is its last.
static void resolveGestureExtents(SeqStep* steps, uint8_t count, const SeqTempo* tempo) {
    if (count == 0 || steps[count - 1].type != STEP_END) return;
    const uint32_t endMs = steps[count - 1].tMs;
    for (uint8_t i = 0; i < count; ++i) {
        SeqStep& s = steps[i];
        if (s.type != STEP_GESTURE || seqGestureRepeatMs(s.params) == 0 ||
            seqGestureExtentMs(s.params) != 0) {
            continue;
        }
        uint32_t extent = (endMs > s.tMs) ? endMs - s.tMs : 0;
        if (tempo != nullptr && tempo->durationMs > s.tMs && tempo->durationMs - s.tMs < extent) {
            extent = tempo->durationMs - s.tMs;
        }
        seqGestureSetExtentMs(s.params, extent);
    }
}

static ProtocolCheckResult parseStep(const char* label, JsonObjectConst obj, uint8_t idx,
                                     const SeqTempo* tempo, bool inLoopBody, SeqStep& s) {
    ProtocolCheckResult r = parseStepFields(label, obj, idx, s);
    if (!r.ok) return r;
    return parseStepBeats(label, obj, idx, tempo, inLoopBody, s);
}

// Parse a JSON steps array into a SeqStep buffer. Sets *outCount.
static ProtocolCheckResult parseBranch(const char* label, JsonArrayConst arr,
                                       SeqStep* buf, uint8_t cap,
                                       const SeqTempo* tempo, uint8_t* outCount) {
    uint8_t n = 0;
    // How many of the steps still to come sit inside the last loop header's
    // body. Counted here, as the steps arrive, because this is where a beat
    // inside a loop pass has to be told apart from one on the sequence.
    uint16_t loopBodyLeft = 0;
    for (JsonVariantConst v : arr) {
        if (n >= cap) {
            return pcFail(label, "too many steps");
        }
        const bool inLoopBody = loopBodyLeft > 0;
        ProtocolCheckResult r =
            parseStep(label, v.as<JsonObjectConst>(), n, tempo, inLoopBody, buf[n]);
        if (!r.ok) return r;
        if (inLoopBody) {
            --loopBodyLeft;
        } else if (buf[n].type == STEP_LOOP) {
            loopBodyLeft = buf[n].params.bodyCount;
        }
        ++n;
    }
    resolveGestureExtents(buf, n, tempo);
    *outCount = n;
    return pcOk();
}

// -----------------------------------------------------------------------------
// Parse
// -----------------------------------------------------------------------------
ProtocolCheckResult seqJsonParseVariant(JsonVariantConst root,
                                        SeqStep* stepBuf, uint8_t stepCap,
                                        SeqStep* closeBuf, uint8_t closeCap,
                                        SeqDraft& out) {
    int format = root["format"] | 0;
    if (format != SEQ_JSON_FORMAT) {
        return pcFail("format", "unsupported format version");
    }

    const char* name = root["name"] | (const char*)nullptr;
    if (name == nullptr || strnlen(name, sizeof(out.name)) >= sizeof(out.name)) {
        return pcFail("name", "missing or too long");
    }
    memset(&out, 0, sizeof(out));
    strncpy(out.name, name, sizeof(out.name) - 1);

    out.suppressMs = root["suppressMs"] | 0u;

    SeqToggleGroup grp = TOGGLE_NONE;
    const char* grpStr = root["toggleGroup"] | "none";
    if (!seqToggleGroupFromString(grpStr, grp)) {
        return pcFail("toggleGroup", "unknown toggle group");
    }
    out.toggleGroup = grp;

    // The tempo goes first: every beat below counts in it.
    SeqTempo tempo;
    bool hasTempo = false;
    ProtocolCheckResult r = parseTempo(root["tempo"], tempo, hasTempo);
    if (!r.ok) return r;
    const SeqTempo* tempoIn = hasTempo ? &tempo : nullptr;

    if (!root["steps"].is<JsonArrayConst>()) {
        return pcFail("steps", "missing steps array");
    }
    uint8_t stepCount = 0;
    r = parseBranch("steps", root["steps"].as<JsonArrayConst>(),
                    stepBuf, stepCap, tempoIn, &stepCount);
    if (!r.ok) return r;
    out.steps = stepBuf;
    out.stepCount = stepCount;

    out.closeSteps = nullptr;
    out.closeStepCount = 0;
    if (root["closeSteps"].is<JsonArrayConst>()) {
        JsonArrayConst carr = root["closeSteps"].as<JsonArrayConst>();
        if (carr.size() > 0) {
            uint8_t closeCount = 0;
            r = parseBranch("closeSteps", carr, closeBuf, closeCap, tempoIn, &closeCount);
            if (!r.ok) return r;
            out.closeSteps = closeBuf;
            out.closeStepCount = closeCount;
        }
    }
    return pcOk();
}

// Entries present in one branch array, clamped to PC_MAX_STEPS. A missing key
// or a non-array value yields 0.
static uint8_t branchCap(JsonVariantConst branch) {
    if (!branch.is<JsonArrayConst>()) return 0;
    const size_t n = branch.as<JsonArrayConst>().size();
    return (n > PC_MAX_STEPS) ? PC_MAX_STEPS : (uint8_t)n;
}

void seqJsonStagingCaps(JsonVariantConst root, uint8_t& stepCap, uint8_t& closeCap) {
    stepCap  = branchCap(root["steps"]);
    closeCap = branchCap(root["closeSteps"]);
}

ProtocolCheckResult seqJsonParse(const char* json,
                                 SeqStep* stepBuf, uint8_t stepCap,
                                 SeqStep* closeBuf, uint8_t closeCap,
                                 SeqDraft& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        return pcFail("json", err.c_str());
    }
    return seqJsonParseVariant(doc.as<JsonVariantConst>(),
                               stepBuf, stepCap, closeBuf, closeCap, out);
}

// -----------------------------------------------------------------------------
// Serialize one branch into a JsonArray.
// -----------------------------------------------------------------------------
static void serializeBranch(JsonArray arr, const SeqStep* steps, uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        const SeqStep& s = steps[i];
        // STEP_CLEAR_LATCHES and STEP_AUDIO_STOP are body-internal with no
        // JSON/wire form. Omit them entirely so a cloned/exported sequence is not
        // truncated (the generic unknown-type fallthrough below emits "end").
        if (s.type == STEP_CLEAR_LATCHES || s.type == STEP_AUDIO_STOP) {
            continue;
        }
        JsonObject o = arr.add<JsonObject>();
        o["t"] = s.tMs;
        switch (s.type) {
            case STEP_DOME_CMD:
                o["type"] = "dome";
                o["cmd"] = s.payload;
                if (s.params.howFar != SEQ_BODY_HOWFAR_UNSET) o["howFar"] = s.params.howFar;
                break;
            case STEP_AUDIO:
                o["type"] = "audio";
                o["cmd"] = s.payload;
                o["boundAudio"] = (s.effectClass & FX_AUDIO_BOUNDED) != 0;
                break;
            case STEP_LOOP:
                o["type"] = "loop";
                o["body"] = s.params.bodyCount;
                o["periodMs"] = s.params.periodMs;
                o["durationMs"] = s.params.durationMs;
                break;
            case STEP_RANDOM:
                o["type"] = "random";
                o["set"] = seqSlotSetToString((SeqSlotSet)s.params.slotSet);
                o["mode"] = randomModeToString(s.params.pulseMin);
                o["moveMs"] = s.params.moveMs;
                o["jitterMs"] = s.params.jitterMs;
                o["distinct"] = s.params.pickDistinct != 0;
                break;
            case STEP_AUDIO_CATEGORY:
                o["type"] = "audioCat";
                o["category"] =
                    audioCategoryToString((AudioPlaybackCategory)s.params.audioCategory);
                o["fallback"] = slotToString(s.params.audioFallbackSlot);
                break;
            case STEP_DOME_ROTATE:
                o["type"] = "domeRotate";
                o["speedPct"] = s.params.speedPct;
                o["durationMs"] = s.params.durationMs;
                break;
            case STEP_BODY:
                // Each of the three is written only when it says something the
                // default does not, so a clone reads as the builder authored it
                // rather than as the model spelled out.
                o["type"] = "body";
                o["part"] = s.payload;
                if (s.params.shape != (uint8_t)SEQ_BODY_SHAPE_DEFAULT) {
                    o["shape"] = seqBodyShapeToString(s.params.shape);
                }
                if (s.params.howFar != SEQ_BODY_HOWFAR_UNSET) {
                    o["howFar"] = s.params.howFar;
                }
                if (s.params.flutterMs != 0) {
                    o["flutterMs"] = s.params.flutterMs;
                }
                break;
            case STEP_GESTURE: {
                // Only what differs from a default is written, as for a Body
                // Step, so a clone reads as the builder authored it.
                o["type"] = "gesture";
                if (seqGestureIsList(s.payload)) {
                    JsonArray parts = o["parts"].to<JsonArray>();
                    seqGestureEachListed(s.payload, [&](const char* id) { parts.add(id); });
                } else {
                    o["set"] = s.payload;
                }
                if (s.params.shape != (uint8_t)SEQ_BODY_SHAPE_DEFAULT) {
                    o["shape"] = seqBodyShapeToString(s.params.shape);
                }
                if (seqGestureSpread(s.params) != GESTURE_SPREAD_TOGETHER) {
                    o["spread"] = seqGestureSpreadToString(seqGestureSpread(s.params));
                }
                if (seqGestureDirection(s.params) != GESTURE_DIR_CLOCKWISE) {
                    o["direction"] = seqGestureDirectionToString(seqGestureDirection(s.params));
                }
                if (seqGestureStart(s.params) != GESTURE_START_FRONT) {
                    o["start"] = seqGestureStartToString(seqGestureStart(s.params));
                }
                if (seqGestureEasing(s.params) != GESTURE_EASING_OUTPUT) {
                    o["easing"] = seqGestureEasingToString(seqGestureEasing(s.params));
                }
                if (s.params.howFar != SEQ_BODY_HOWFAR_UNSET) o["howFar"] = s.params.howFar;
                if (s.params.flutterMs != 0) o["flutterMs"] = s.params.flutterMs;
                if (s.params.moveMs != 0) o["stepMs"] = s.params.moveMs;
                if (seqGestureSpeedMs(s.params) != 0) o["speedMs"] = seqGestureSpeedMs(s.params);
                if (seqGestureRepeatMs(s.params) != 0) o["repeatMs"] = seqGestureRepeatMs(s.params);
                if (seqGestureExtentMs(s.params) != 0) o["extentMs"] = seqGestureExtentMs(s.params);
                break;
            }
            case STEP_END:
            default:
                o["type"] = "end";
                break;
        }
    }
}

// -----------------------------------------------------------------------------
// Serialize
// -----------------------------------------------------------------------------
void seqJsonSerializeObject(JsonObject obj, const SequenceEntry& entry,
                            const char* source) {
    obj["format"] = SEQ_JSON_FORMAT;
    obj["name"] = entry.name;
    obj["suppressMs"] = entry.suppressMs;
    obj["toggleGroup"] = seqToggleGroupToString(entry.toggleGroup);

    JsonObject meta = obj["meta"].to<JsonObject>();
    meta["source"] = (source != nullptr) ? source : "factory";
    meta["origin"] = "";
    meta["license"] = "";
    meta["notes"] = "";
    meta["purpose"] = (entry.purpose != nullptr) ? entry.purpose : "";
    meta["modified"] = false;

    JsonArray steps = obj["steps"].to<JsonArray>();
    serializeBranch(steps, entry.steps, entry.stepCount);

    JsonArray closeArr = obj["closeSteps"].to<JsonArray>();
    if (entry.toggleGroup != TOGGLE_NONE && entry.closeSteps != nullptr) {
        serializeBranch(closeArr, entry.closeSteps, entry.closeStepCount);
    }
}

size_t seqJsonSerialize(const SequenceEntry& entry, const char* source,
                        char* outBuf, size_t outCap) {
    JsonDocument doc;
    seqJsonSerializeObject(doc.to<JsonObject>(), entry, source);
    size_t n = serializeJson(doc, outBuf, outCap);
    // serializeJson returns 0 if it could not write (overflow) for a non-empty doc.
    return n;
}
