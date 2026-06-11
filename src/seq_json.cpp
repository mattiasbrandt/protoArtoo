// =============================================================================
// src/seq_json.cpp
//
// Learned Sequence JSON format v1 parse/serialize (issue #2 slice 3, ADR 0006).
// See header for the format. Uses ArduinoJson (bounded per-call document).
// =============================================================================

#include "seq_json.h"

#include <string.h>

#include <ArduinoJson.h>

#include "audio_playback_policy.h"  // AudioPlaybackCategory/Slot + audioCategoryToString

// -----------------------------------------------------------------------------
// Result helpers (mirror protocol_check.cpp so callers get one error shape)
// -----------------------------------------------------------------------------
static ProtocolCheckResult jok() {
    ProtocolCheckResult r = { true, "", "" };
    return r;
}
static ProtocolCheckResult jfail(const char* field, const char* msg) {
    ProtocolCheckResult r = { false, "", "" };
    strncpy(r.field, field, sizeof(r.field) - 1);
    strncpy(r.message, msg, sizeof(r.message) - 1);
    return r;
}
static ProtocolCheckResult jfailAt(const char* label, uint8_t idx,
                                   const char* suffix, const char* msg) {
    char field[24];
    snprintf(field, sizeof(field), "%s[%u].%s", label, (unsigned)idx, suffix);
    return jfail(field, msg);
}

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
// Parse one step object into `s`. `idx` is for error fields.
// -----------------------------------------------------------------------------
static ProtocolCheckResult parseStep(const char* label, JsonObjectConst obj,
                                     uint8_t idx, SeqStep& s) {
    memset(&s, 0, sizeof(s));
    s.effectClass = FX_NONE;  // Protocol Check stamps this

    s.tMs = obj["t"] | 0u;

    const char* type = obj["type"] | (const char*)nullptr;
    if (type == nullptr) {
        return jfailAt(label, idx, "type", "missing type");
    }

    if (strcmp(type, "dome") == 0 || strcmp(type, "audio") == 0) {
        const char* cmd = obj["cmd"] | (const char*)nullptr;
        if (cmd == nullptr) {
            return jfailAt(label, idx, "cmd", "missing cmd");
        }
        if (strnlen(cmd, sizeof(s.payload)) >= sizeof(s.payload)) {
            return jfailAt(label, idx, "cmd", "command too long");
        }
        strncpy(s.payload, cmd, sizeof(s.payload) - 1);
        s.type = (strcmp(type, "dome") == 0) ? STEP_DOME_CMD : STEP_AUDIO;
        return jok();
    }
    if (strcmp(type, "loop") == 0) {
        s.type = STEP_LOOP;
        s.params.bodyCount  = (uint8_t)(obj["body"] | 0);
        s.params.periodMs   = (uint16_t)(obj["periodMs"] | 0);
        s.params.durationMs = (uint32_t)(obj["durationMs"] | 0u);
        return jok();
    }
    if (strcmp(type, "random") == 0) {
        s.type = STEP_RANDOM;
        SeqSlotSet set = SLOTSET_RING;
        const char* setStr = obj["set"] | (const char*)nullptr;
        if (setStr == nullptr || !seqSlotSetFromString(setStr, set)) {
            return jfailAt(label, idx, "set", "missing or unknown slot set");
        }
        s.params.slotSet      = (uint8_t)set;
        s.params.pulseMin     = (uint16_t)(obj["pulseMin"] | 0);
        s.params.pulseMax     = (uint16_t)(obj["pulseMax"] | 0);
        s.params.moveMs       = (uint16_t)(obj["moveMs"] | 0);
        s.params.jitterMs     = (uint16_t)(obj["jitterMs"] | 0);
        s.params.pickDistinct = (obj["distinct"] | false) ? 1 : 0;
        return jok();
    }
    if (strcmp(type, "audioCat") == 0) {
        s.type = STEP_AUDIO_CATEGORY;
        uint8_t cat = 0, fb = AUDIO_SLOT_NONE;
        const char* catStr = obj["category"] | (const char*)nullptr;
        if (catStr == nullptr || !categoryFromString(catStr, cat)) {
            return jfailAt(label, idx, "category", "missing or unknown category");
        }
        const char* fbStr = obj["fallback"] | "none";
        if (!slotFromString(fbStr, fb)) {
            return jfailAt(label, idx, "fallback", "unknown fallback slot");
        }
        s.params.audioCategory     = cat;
        s.params.audioFallbackSlot = fb;
        return jok();
    }
    if (strcmp(type, "end") == 0) {
        s.type = STEP_END;
        return jok();
    }
    return jfailAt(label, idx, "type", "unknown step type");
}

// Parse a JSON steps array into a SeqStep buffer. Sets *outCount.
static ProtocolCheckResult parseBranch(const char* label, JsonArrayConst arr,
                                       SeqStep* buf, uint8_t cap,
                                       uint8_t* outCount) {
    uint8_t n = 0;
    for (JsonVariantConst v : arr) {
        if (n >= cap) {
            return jfail(label, "too many steps");
        }
        ProtocolCheckResult r = parseStep(label, v.as<JsonObjectConst>(), n, buf[n]);
        if (!r.ok) return r;
        ++n;
    }
    *outCount = n;
    return jok();
}

// -----------------------------------------------------------------------------
// Parse
// -----------------------------------------------------------------------------
ProtocolCheckResult seqJsonParse(const char* json,
                                 SeqStep* stepBuf, uint8_t stepCap,
                                 SeqStep* closeBuf, uint8_t closeCap,
                                 SeqDraft& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        return jfail("json", err.c_str());
    }

    int format = doc["format"] | 0;
    if (format != SEQ_JSON_FORMAT) {
        return jfail("format", "unsupported format version");
    }

    const char* name = doc["name"] | (const char*)nullptr;
    if (name == nullptr || strnlen(name, sizeof(out.name)) >= sizeof(out.name)) {
        return jfail("name", "missing or too long");
    }
    memset(&out, 0, sizeof(out));
    strncpy(out.name, name, sizeof(out.name) - 1);

    out.suppressMs = doc["suppressMs"] | 0u;

    SeqToggleGroup grp = TOGGLE_NONE;
    const char* grpStr = doc["toggleGroup"] | "none";
    if (!seqToggleGroupFromString(grpStr, grp)) {
        return jfail("toggleGroup", "unknown toggle group");
    }
    out.toggleGroup = grp;

    if (!doc["steps"].is<JsonArrayConst>()) {
        return jfail("steps", "missing steps array");
    }
    uint8_t stepCount = 0;
    ProtocolCheckResult r = parseBranch("steps", doc["steps"].as<JsonArrayConst>(),
                                        stepBuf, stepCap, &stepCount);
    if (!r.ok) return r;
    out.steps = stepBuf;
    out.stepCount = stepCount;

    out.closeSteps = nullptr;
    out.closeStepCount = 0;
    if (doc["closeSteps"].is<JsonArrayConst>()) {
        JsonArrayConst carr = doc["closeSteps"].as<JsonArrayConst>();
        if (carr.size() > 0) {
            uint8_t closeCount = 0;
            r = parseBranch("closeSteps", carr, closeBuf, closeCap, &closeCount);
            if (!r.ok) return r;
            out.closeSteps = closeBuf;
            out.closeStepCount = closeCount;
        }
    }
    return jok();
}

// -----------------------------------------------------------------------------
// Serialize one branch into a JsonArray.
// -----------------------------------------------------------------------------
static void serializeBranch(JsonArray arr, const SeqStep* steps, uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        const SeqStep& s = steps[i];
        JsonObject o = arr.add<JsonObject>();
        o["t"] = s.tMs;
        switch (s.type) {
            case STEP_DOME_CMD:
                o["type"] = "dome";
                o["cmd"] = s.payload;
                break;
            case STEP_AUDIO:
                o["type"] = "audio";
                o["cmd"] = s.payload;
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
                o["pulseMin"] = s.params.pulseMin;
                o["pulseMax"] = s.params.pulseMax;
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
size_t seqJsonSerialize(const SequenceEntry& entry, const char* source,
                        char* outBuf, size_t outCap) {
    JsonDocument doc;
    doc["format"] = SEQ_JSON_FORMAT;
    doc["name"] = entry.name;
    doc["suppressMs"] = entry.suppressMs;
    doc["toggleGroup"] = seqToggleGroupToString(entry.toggleGroup);

    JsonObject meta = doc["meta"].to<JsonObject>();
    meta["source"] = (source != nullptr) ? source : "factory";
    meta["origin"] = "";
    meta["license"] = "";
    meta["notes"] = "";
    meta["modified"] = false;

    JsonArray steps = doc["steps"].to<JsonArray>();
    serializeBranch(steps, entry.steps, entry.stepCount);

    JsonArray closeArr = doc["closeSteps"].to<JsonArray>();
    if (entry.toggleGroup != TOGGLE_NONE && entry.closeSteps != nullptr) {
        serializeBranch(closeArr, entry.closeSteps, entry.closeStepCount);
    }

    size_t n = serializeJson(doc, outBuf, outCap);
    // serializeJson returns 0 if it could not write (overflow) for a non-empty doc.
    return n;
}
