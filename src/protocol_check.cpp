// =============================================================================
// src/protocol_check.cpp
//
// Protocol Check implementation (issue #2 slice 3, ADR 0006). See header for
// the contract. Pure: depends only on the sequence model, the catalog lookup
// (for retrain rules), and the audio policy enums (for category bounds).
// =============================================================================

#include "protocol_check.h"

#include <string.h>

#include "audio_playback_policy.h"   // AUDIO_CATEGORY_COUNT, AUDIO_SLOT_COUNT
#include "sequence_dispatcher.h"     // sequenceCatalogFind()

// Result constructors (pcOk/pcFail/pcFailAt) are shared inlines in the header.

// -----------------------------------------------------------------------------
// Small parsers (no libc locale surprises; pure ASCII)
// -----------------------------------------------------------------------------
static bool isDigit(char c) { return c >= '0' && c <= '9'; }
static bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
static bool isAlnum(char c) {
    return isDigit(c) || isUpper(c) || (c >= 'a' && c <= 'z');
}
static bool isPrintable(char c) { return c >= 0x20 && c <= 0x7E; }

// Parse a run of decimal digits starting at *p into out; advance *p. Returns the
// digit count (0 => no number present, out untouched).
static int parseUint(const char** p, uint32_t& out) {
    const char* s = *p;
    uint32_t v = 0;
    int n = 0;
    while (isDigit(*s)) {
        v = v * 10 + (uint32_t)(*s - '0');
        ++s;
        ++n;
        if (n > 9) break;  // overflow guard; field bounds reject below anyway
    }
    if (n > 0) {
        out = v;
        *p = s;
    }
    return n;
}

// Percent-decode a string (similar to decodeURIComponent in JS).
// Returns the decoded length, or -1 on error (invalid escape, bad hex).
// Rejects: carriage return (%0D), literal colons (confuse field parsing).
// Decoded output goes into 'out' buffer (must be at least decodedLenMax+1).
static int percentDecode(const char* encoded, size_t encodedLen,
                          char* out, size_t decodedLenMax) {
    if (out == nullptr || decodedLenMax == 0) {
        return -1;
    }
    size_t outLen = 0;
    for (size_t i = 0; i < encodedLen; ++i) {
        if (outLen >= decodedLenMax) {
            // Decoded text would exceed maximum length
            return -1;
        }
        if (encoded[i] == '%') {
            // Must have at least 2 more chars for hex digits
            if (i + 2 >= encodedLen) {
                return -1;
            }
            char h1 = encoded[i + 1];
            char h2 = encoded[i + 2];
            // Convert hex digits to value (case-insensitive)
            int hex = -1;
            if (isDigit(h1) && isDigit(h2)) {
                hex = (h1 - '0') * 16 + (h2 - '0');
            } else if ((h1 >= 'A' && h1 <= 'F') && (h2 >= '0' && h2 <= '9')) {
                hex = (h1 - 'A' + 10) * 16 + (h2 - '0');
            } else if ((h1 >= 'A' && h1 <= 'F') && (h2 >= 'A' && h2 <= 'F')) {
                hex = (h1 - 'A' + 10) * 16 + (h2 - 'A' + 10);
            } else if (isDigit(h1) && (h2 >= 'A' && h2 <= 'F')) {
                hex = (h1 - '0') * 16 + (h2 - 'A' + 10);
            } else if ((h1 >= 'a' && h1 <= 'f') && (h2 >= '0' && h2 <= '9')) {
                hex = (h1 - 'a' + 10) * 16 + (h2 - '0');
            } else if ((h1 >= 'a' && h1 <= 'f') && (h2 >= 'a' && h2 <= 'f')) {
                hex = (h1 - 'a' + 10) * 16 + (h2 - 'a' + 10);
            } else if (isDigit(h1) && (h2 >= 'a' && h2 <= 'f')) {
                hex = (h1 - '0') * 16 + (h2 - 'a' + 10);
            } else if ((h1 >= 'a' && h1 <= 'f') && isDigit(h2)) {
                hex = (h1 - 'a' + 10) * 16 + (h2 - '0');
            } else {
                // Invalid hex
                return -1;
            }
            // Reject carriage return (%0D)
            if (hex == 0x0D) {
                return -1;
            }
            out[outLen++] = (char)hex;
            i += 2;
        } else if (encoded[i] == ':') {
            // Literal colon is not allowed in encoded text (confuses field parsing).
            return -1;
        } else {
            // Literal character (must be printable)
            if (!isPrintable(encoded[i])) {
                return -1;
            }
            out[outLen++] = encoded[i];
        }
    }
    out[outLen] = '\0';
    return (int)outLen;
}

// Extract the next colon-separated field from *p and advance *p past the delimiter.
// Returns a pointer to the start of the field, or nullptr if delimiter not found.
// The returned field is NOT NUL-terminated; use returned length.
static const char* parseField(const char** p, size_t& len) {
    const char* start = *p;
    const char* end = strchr(start, ':');
    if (end == nullptr) {
        len = strnlen(start, PC_CMD_MAX + 1);
        *p = start + len;
        return start;
    }
    len = (size_t)(end - start);
    *p = end + 1;
    return start;
}

// Bounded printable-charset check for a NUL-terminated command payload.
static bool charsetOk(const char* s) {
    size_t len = strnlen(s, PC_CMD_MAX + 1);
    if (len == 0 || len > PC_CMD_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!isPrintable(s[i])) {
            return false;
        }
    }
    return true;
}

enum PanelTargetGroup : uint8_t {
    PANEL_TARGET_NONE = 0,
    PANEL_TARGET_RING,
    PANEL_TARGET_PIE,
    PANEL_TARGET_ALL,
};

struct PanelIntent {
    bool valid;
    char action;  // O=open, C=close, F=flutter
    PanelTargetGroup group;
    char target[3];
};

static bool isAllowedRingTarget(const char* t) {
    return strcmp(t, "01") == 0 || strcmp(t, "02") == 0 ||
           strcmp(t, "03") == 0 || strcmp(t, "04") == 0 ||
           strcmp(t, "07") == 0 || strcmp(t, "11") == 0 ||
           strcmp(t, "13") == 0;
}

static bool isAllowedPieTarget(const char* t) {
    return t[0] == 'P' && t[1] >= '1' && t[1] <= '6' && t[2] == '\0';
}

static PanelIntent parsePanelIntent(const char* cmd) {
    PanelIntent pi = { false, 0, PANEL_TARGET_NONE, "" };
    if (cmd == nullptr || cmd[0] != ':' ||
        (strncmp(cmd + 1, "OP", 2) != 0 &&
         strncmp(cmd + 1, "CL", 2) != 0 &&
         strncmp(cmd + 1, "OF", 2) != 0)) {
        return pi;
    }

    pi.action = cmd[2];  // P, L, or F
    const char* t = cmd + 3;
    const size_t len = strnlen(t, 4);
    if (len != 2) {
        return pi;
    }
    strncpy(pi.target, t, sizeof(pi.target) - 1);
    pi.target[sizeof(pi.target) - 1] = '\0';

    if (strcmp(t, "00") == 0) {
        pi.group = PANEL_TARGET_ALL;
        pi.valid = true;
    } else if (strcmp(t, "14") == 0) {
        pi.group = PANEL_TARGET_PIE;
        pi.valid = true;
    } else if (strcmp(t, "15") == 0) {
        pi.group = PANEL_TARGET_RING;
        pi.valid = true;
    } else if (isAllowedRingTarget(t)) {
        pi.group = PANEL_TARGET_RING;
        pi.valid = true;
    } else if (isAllowedPieTarget(t)) {
        pi.group = PANEL_TARGET_PIE;
        pi.valid = true;
    }
    return pi;
}

static bool panelCloseCleansFlutter(const PanelIntent& flutter,
                                    const PanelIntent& close) {
    if (!flutter.valid || !close.valid || close.action != 'L') {
        return false;
    }
    if (strcmp(close.target, "00") == 0) {
        return true;
    }
    if (strcmp(flutter.target, close.target) == 0) {
        return true;
    }
    if (flutter.group == PANEL_TARGET_PIE && strcmp(close.target, "14") == 0) {
        return true;
    }
    if (flutter.group == PANEL_TARGET_RING && strcmp(close.target, "15") == 0) {
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Toggle group
// -----------------------------------------------------------------------------
bool protocolCheckToggleGroupValid(SeqToggleGroup g) {
    switch (g) {
        case TOGGLE_NONE:
        case TOGGLE_PIES:
        case TOGGLE_LOW:
        case TOGGLE_ALL:
        case TOGGLE_USER1:
        case TOGGLE_USER2:
        case TOGGLE_USER3:
        case TOGGLE_USER4:
            return true;
        default:
            return false;
    }
}

// -----------------------------------------------------------------------------
// Name: ^DM:[A-Z0-9_]{1,18}$
// -----------------------------------------------------------------------------
static bool nameValid(const char* name) {
    if (name == nullptr) return false;
    if (strncmp(name, "DM:", 3) != 0) return false;
    const char* body = name + 3;
    size_t len = strnlen(body, PC_NAME_BODY_MAX + 1);
    if (len == 0 || len > PC_NAME_BODY_MAX) return false;
    for (size_t i = 0; i < len; ++i) {
        char c = body[i];
        if (!(isUpper(c) || isDigit(c) || c == '_')) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Dome command whitelist + effect-class inference.
//   :OP/:CL/:OF<target>          panel intent -> FX_PANEL except close-all reset
//   :SE<dd>                      dome sequence -> FX_DOME_SEQUENCE
//   @0T1 / @0P1                 logic/PSI reset -> FX_NONE
//   @<d>{T,P,M}...              logic/PSI/text  -> FX_LOGIC_PSI
//   @HP...                      holo            -> FX_HOLO
//   *ST00                       holo reset      -> FX_NONE
//   *...                        holo            -> FX_HOLO
// Inference is conservative: an over-tag only causes an extra idempotent reset.
// -----------------------------------------------------------------------------
static ProtocolCheckResult classifyDome(const char* label, uint8_t idx,
                                        const char* cmd, uint8_t& fxOut) {
    fxOut = FX_NONE;

    if (!charsetOk(cmd)) {
        return pcFailAt(label, idx, "cmd", "empty, too long, or non-printable");
    }

    if (strncmp(cmd, "DM:", 3) == 0) {
        return pcFailAt(label, idx, "cmd", "DM:* is a sequence trigger, not a step");
    }

    // DV:<NAME> — dome visual preset (logic/PSI/holo only; issue #2 task #5). The
    // name set is closed and owned by the dome; persisted/replayable sequences may
    // only carry a known preset so an unknown name can never reach the wire. DV:
    // is visual-only and does not participate in panel cleanup semantics.
    if (strncmp(cmd, "DV:", 3) == 0) {
        static const char* const kDvPresets[] = {
            "ROCKMARCH", "VADER", "ALARM", "LEIA", "HEART", "CANTINA",
            "SCREAM", "OVERLOAD", "HELLO", "RESET_VISUALS",
        };
        const char* name = cmd + 3;
        const uint8_t kDvCount = (uint8_t)(sizeof(kDvPresets) / sizeof(kDvPresets[0]));
        for (uint8_t i = 0; i < kDvCount; ++i) {
            if (strcmp(name, kDvPresets[i]) == 0) {
                fxOut = (uint8_t)(FX_LOGIC_PSI | FX_HOLO);  // visual reset at term
                return pcOk();
            }
        }
        return pcFailAt(label, idx, "cmd", "unknown DV: visual preset");
    }

    // DL:<target>:<mode>[:<color>[:<durationSec>]] — Logic/PSI Mode (issue #11).
    // Structured control for dome logic/PSI animations. Mirrors client validation
    // in data/seq_protocol_check.js exactly. Grammar enforces uppercase tokens,
    // full-string match, known enums, and command length <= 63.
    if (strncmp(cmd, "DL:", 3) == 0) {
        static const char* const kDlTargets[] = {
            "FLD", "RLD", "LOGIC", "FPSI", "RPSI", "PSI", "ALL",
        };
        static const char* const kDlModes[] = {
            "NORMAL", "ALARM", "FAILURE", "LEIA", "MARCH", "FLASHCOLOR",
            "REDALERT", "RAINBOW", "LIGHTSOUT",
        };
        static const char* const kDlColors[] = {
            "DEFAULT", "RED", "BLUE", "GREEN", "WHITE", "YELLOW", "ORANGE", "PURPLE",
        };
        const uint8_t kDlTargetCount = (uint8_t)(sizeof(kDlTargets) / sizeof(kDlTargets[0]));
        const uint8_t kDlModeCount = (uint8_t)(sizeof(kDlModes) / sizeof(kDlModes[0]));
        const uint8_t kDlColorCount = (uint8_t)(sizeof(kDlColors) / sizeof(kDlColors[0]));

        // Parse using same field-splitting logic as client: split by ':' and check bounds.
        // Grammar: DL:target:mode[:color[:duration]]
        const char* p = cmd + 3;
        size_t fieldLen = 0;

        // Parse target (required)
        const char* targetStart = parseField(&p, fieldLen);
        bool targetOk = false;
        for (uint8_t i = 0; i < kDlTargetCount; ++i) {
            size_t tlen = strlen(kDlTargets[i]);
            if (fieldLen == tlen && strncmp(targetStart, kDlTargets[i], tlen) == 0) {
                targetOk = true;
                break;
            }
        }
        if (!targetOk) {
            return pcFailAt(label, idx, "cmd", "unknown DL: target");
        }
        if (*p == '\0') {
            return pcFailAt(label, idx, "cmd", "DL: requires target:mode[:color[:duration]]");
        }

        // Parse mode (required)
        const char* modeStart = parseField(&p, fieldLen);
        bool modeOk = false;
        for (uint8_t i = 0; i < kDlModeCount; ++i) {
            size_t mlen = strlen(kDlModes[i]);
            if (fieldLen == mlen && strncmp(modeStart, kDlModes[i], mlen) == 0) {
                modeOk = true;
                break;
            }
        }
        if (!modeOk) {
            return pcFailAt(label, idx, "cmd", "unknown DL: mode");
        }

        // Parse color (optional) — if present, validate against whitelist
        if (*p != '\0') {
            const char* colorStart = parseField(&p, fieldLen);
            bool colorOk = false;
            for (uint8_t i = 0; i < kDlColorCount; ++i) {
                size_t clen = strlen(kDlColors[i]);
                if (fieldLen == clen && strncmp(colorStart, kDlColors[i], clen) == 0) {
                    colorOk = true;
                    break;
                }
            }
            if (!colorOk) {
                return pcFailAt(label, idx, "cmd", "unknown DL: color");
            }
        }

        // Parse duration (optional, 0..99)
        if (*p != '\0') {
            const char* durationStart = parseField(&p, fieldLen);
            if (fieldLen == 0) {
                return pcFailAt(label, idx, "cmd", "invalid DL: duration");
            }
            // Parse as uint and verify we consumed exactly fieldLen digits
            uint32_t duration = 0;
            const char* durationPtr = durationStart;
            int digitsConsumed = parseUint(&durationPtr, duration);
            if (digitsConsumed != (int)fieldLen || *durationPtr != '\0') {
                return pcFailAt(label, idx, "cmd", "DL: duration must be 0-99");
            }
            if (duration > 99) {
                return pcFailAt(label, idx, "cmd", "DL: duration must be 0-99");
            }
        }

        // Check for extra fields (too many colons)
        if (*p != '\0') {
            return pcFailAt(label, idx, "cmd", "DL: too many fields");
        }

        fxOut = (uint8_t)(FX_LOGIC_PSI | FX_HOLO);  // visual effect at term
        return pcOk();
    }

    // DT:<target>:<color>:<durationSec>:<speed>:<encodedText> — Logic Text (issue #11).
    // Multi-line text display on FLD/RLD. Text is percent-encoded; only valid escapes
    // are %0A (newline), %25 (%), %3A (:). Encoded text <= 40 chars; decoded <= 32;
    // max one newline; reject if final command length > 63. Mirrors client validation
    // in data/seq_protocol_check.js exactly.
    if (strncmp(cmd, "DT:", 3) == 0) {
        static const char* const kDtTargets[] = {
            "FLD", "RLD", "LOGIC",
        };
        static const char* const kDtColors[] = {
            "DEFAULT", "RED", "BLUE", "GREEN", "WHITE", "YELLOW", "ORANGE", "PURPLE",
        };
        const uint8_t kDtTargetCount = (uint8_t)(sizeof(kDtTargets) / sizeof(kDtTargets[0]));
        const uint8_t kDtColorCount = (uint8_t)(sizeof(kDtColors) / sizeof(kDtColors[0]));

        // Parse DT:<target>:<color>:<durationSec>:<speed>:<encodedText>
        const char* p = cmd + 3;
        size_t fieldLen = 0;

        // Parse target (required)
        const char* targetStart = parseField(&p, fieldLen);
        bool targetOk = false;
        for (uint8_t i = 0; i < kDtTargetCount; ++i) {
            size_t tlen = strlen(kDtTargets[i]);
            if (fieldLen == tlen && strncmp(targetStart, kDtTargets[i], tlen) == 0) {
                targetOk = true;
                break;
            }
        }
        if (!targetOk) {
            return pcFailAt(label, idx, "cmd", "unknown DT: target");
        }
        if (*p == '\0') {
            return pcFailAt(label, idx, "cmd", "DT: requires target:color:duration:speed:text");
        }

        // Parse color (required)
        const char* colorStart = parseField(&p, fieldLen);
        bool colorOk = false;
        for (uint8_t i = 0; i < kDtColorCount; ++i) {
            size_t clen = strlen(kDtColors[i]);
            if (fieldLen == clen && strncmp(colorStart, kDtColors[i], clen) == 0) {
                colorOk = true;
                break;
            }
        }
        if (!colorOk) {
            return pcFailAt(label, idx, "cmd", "unknown DT: color");
        }
        if (*p == '\0') {
            return pcFailAt(label, idx, "cmd", "DT: requires target:color:duration:speed:text");
        }

        // Parse duration (required, 0..99)
        const char* durationStart = parseField(&p, fieldLen);
        if (fieldLen == 0) {
            return pcFailAt(label, idx, "cmd", "invalid DT: duration");
        }
        uint32_t duration = 0;
        const char* durationPtr = durationStart;
        int digitsConsumed = parseUint(&durationPtr, duration);
        // parseUint must consume exactly all fieldLen chars (field is not NUL-terminated)
        if (digitsConsumed != (int)fieldLen) {
            return pcFailAt(label, idx, "cmd", "DT: duration must be 0-99");
        }
        if (duration > 99) {
            return pcFailAt(label, idx, "cmd", "DT: duration must be 0-99");
        }
        if (*p == '\0') {
            return pcFailAt(label, idx, "cmd", "DT: requires target:color:duration:speed:text");
        }

        // Parse speed (required, 0..9)
        const char* speedStart = parseField(&p, fieldLen);
        if (fieldLen == 0) {
            return pcFailAt(label, idx, "cmd", "invalid DT: speed");
        }
        uint32_t speed = 0;
        const char* speedPtr = speedStart;
        int speedDigits = parseUint(&speedPtr, speed);
        // parseUint must consume exactly all fieldLen chars (field is not NUL-terminated)
        if (speedDigits != (int)fieldLen) {
            return pcFailAt(label, idx, "cmd", "DT: speed must be 0-9");
        }
        if (speed > 9) {
            return pcFailAt(label, idx, "cmd", "DT: speed must be 0-9");
        }

        // Parse encoded text (everything after the fifth colon)
        // Note: p is already pointing to the start of the text field after the last
        // parseField call for speed. We need to get what remains.
        const char* encodedStart = p;
        size_t encodedLen = strnlen(encodedStart, PC_CMD_MAX + 1);
        if (encodedLen == 0) {
            return pcFailAt(label, idx, "cmd", "DT: text cannot be empty");
        }
        if (encodedLen > 40) {
            return pcFailAt(label, idx, "cmd", "DT: encoded text too long (max 40)");
        }

        // Decode the text and validate
        char decodedBuf[33];  // max 32 chars + NUL
        int decodedLen = percentDecode(encodedStart, encodedLen, decodedBuf, 32);
        if (decodedLen < 0) {
            return pcFailAt(label, idx, "cmd", "DT: invalid percent-encoding");
        }
        if (decodedLen == 0) {
            return pcFailAt(label, idx, "cmd", "DT: text cannot be empty");
        }

        // Count newlines
        int newlineCount = 0;
        for (int i = 0; i < decodedLen; ++i) {
            if (decodedBuf[i] == '\n') {
                newlineCount++;
            }
        }
        if (newlineCount > 1) {
            return pcFailAt(label, idx, "cmd", "DT: max one newline");
        }

        // Verify final command length <= 63
        if (strnlen(cmd, PC_CMD_MAX + 1) > PC_CMD_MAX) {
            return pcFailAt(label, idx, "cmd", "DT: command too long (max 63)");
        }

        fxOut = (uint8_t)(FX_LOGIC_PSI | FX_HOLO);  // visual effect at term
        return pcOk();
    }

    // DH:<target>:<effect>[:<color>[:<durationOrCount>]] — Holo Effect (issue #11).
    // Holoprojector effects: OFF, ON, RESET, RANDOM, WAG, NOD, PULSE, RAINBOW, FLASH,
    // SHORTCIRCUIT, SOLID. Targets: F (front), R (rear), T (top), A (all). Color and
    // duration are effect-dependent: validation enforces the AstroPixelsPlus dome's
    // effect/color + duration matrix (docs/dome-visual-authoring-contract.md) so
    // unsupported combos (e.g. DH:A:RAINBOW:RED) are rejected before send. Command
    // length must be <= 63. Mirrors client validation in data/seq_protocol_check.js.
    if (strncmp(cmd, "DH:", 3) == 0) {
        static const char* const kDhTargets[] = {
            "F", "R", "T", "A",
        };
        static const char* const kDhEffects[] = {
            "OFF", "ON", "RESET", "RANDOM", "WAG", "NOD", "PULSE", "RAINBOW",
            "FLASH", "SHORTCIRCUIT", "SOLID",
        };
        static const char* const kDhColors[] = {
            "DEFAULT", "RED", "BLUE", "GREEN", "WHITE", "YELLOW", "ORANGE", "PURPLE", "RANDOM",
        };
        // Per-effect color + duration policy, aligned index-for-index with kDhEffects.
        // Color: 0 = DEFAULT only, 1 = DEFAULT or RANDOM, 2 = DEFAULT/WHITE/RED (FLASH),
        // 3 = any color. Duration: 0 = none (omit or 0), 1 = range 0..99.
        enum { DH_C_DEFAULT_ONLY = 0, DH_C_DEFAULT_RANDOM = 1, DH_C_FLASH = 2, DH_C_ANY = 3 };
        enum { DH_D_NONE = 0, DH_D_RANGE = 1 };
        static const uint8_t kDhColorPolicy[] = {
            DH_C_DEFAULT_ONLY,    // OFF
            DH_C_ANY,             // ON
            DH_C_DEFAULT_ONLY,    // RESET
            DH_C_DEFAULT_ONLY,    // RANDOM
            DH_C_DEFAULT_ONLY,    // WAG
            DH_C_DEFAULT_ONLY,    // NOD
            DH_C_DEFAULT_RANDOM,  // PULSE
            DH_C_DEFAULT_ONLY,    // RAINBOW
            DH_C_FLASH,           // FLASH
            DH_C_DEFAULT_RANDOM,  // SHORTCIRCUIT
            DH_C_ANY,             // SOLID
        };
        static const uint8_t kDhDurPolicy[] = {
            DH_D_NONE,   // OFF
            DH_D_NONE,   // ON
            DH_D_NONE,   // RESET
            DH_D_NONE,   // RANDOM
            DH_D_RANGE,  // WAG
            DH_D_RANGE,  // NOD
            DH_D_NONE,   // PULSE
            DH_D_NONE,   // RAINBOW
            DH_D_RANGE,  // FLASH
            DH_D_NONE,   // SHORTCIRCUIT
            DH_D_NONE,   // SOLID
        };
        const uint8_t kDhTargetCount = (uint8_t)(sizeof(kDhTargets) / sizeof(kDhTargets[0]));
        const uint8_t kDhEffectCount = (uint8_t)(sizeof(kDhEffects) / sizeof(kDhEffects[0]));
        const uint8_t kDhColorCount = (uint8_t)(sizeof(kDhColors) / sizeof(kDhColors[0]));

        // Parse using same field-splitting logic as client: split by ':' and check bounds.
        // Grammar: DH:target:effect[:color[:durationOrCount]]
        const char* p = cmd + 3;
        size_t fieldLen = 0;

        // Parse target (required)
        const char* targetStart = parseField(&p, fieldLen);
        bool targetOk = false;
        for (uint8_t i = 0; i < kDhTargetCount; ++i) {
            size_t tlen = strlen(kDhTargets[i]);
            if (fieldLen == tlen && strncmp(targetStart, kDhTargets[i], tlen) == 0) {
                targetOk = true;
                break;
            }
        }
        if (!targetOk) {
            return pcFailAt(label, idx, "cmd", "unknown DH: target");
        }
        if (*p == '\0') {
            return pcFailAt(label, idx, "cmd", "DH: requires target:effect[:color[:durationOrCount]]");
        }

        // Parse effect (required)
        const char* effectStart = parseField(&p, fieldLen);
        uint8_t effectIdx = 0;
        bool effectOk = false;
        for (uint8_t i = 0; i < kDhEffectCount; ++i) {
            size_t elen = strlen(kDhEffects[i]);
            if (fieldLen == elen && strncmp(effectStart, kDhEffects[i], elen) == 0) {
                effectIdx = i;
                effectOk = true;
                break;
            }
        }
        if (!effectOk) {
            return pcFailAt(label, idx, "cmd", "unknown DH: effect");
        }

        // Parse color (optional) — if present, validate against whitelist. Omitted
        // color defaults to DEFAULT (colorIdx 0), which passes every effect's matrix.
        uint8_t colorIdx = 0;  // 0 == DEFAULT
        if (*p != '\0') {
            const char* colorStart = parseField(&p, fieldLen);
            bool colorOk = false;
            for (uint8_t i = 0; i < kDhColorCount; ++i) {
                size_t clen = strlen(kDhColors[i]);
                if (fieldLen == clen && strncmp(colorStart, kDhColors[i], clen) == 0) {
                    colorIdx = i;
                    colorOk = true;
                    break;
                }
            }
            if (!colorOk) {
                return pcFailAt(label, idx, "cmd", "unknown DH: color");
            }
        }

        // Effect-specific color matrix (mirrors the dome). DEFAULT (colorIdx 0) is
        // always allowed; otherwise the effect's policy gates which colors are valid.
        if (colorIdx != 0) {
            const uint8_t cp = kDhColorPolicy[effectIdx];
            bool colorAllowed = false;
            if (cp == DH_C_ANY) {
                colorAllowed = true;
            } else if (cp == DH_C_DEFAULT_RANDOM) {
                colorAllowed = (colorIdx == 8);  // RANDOM
            } else if (cp == DH_C_FLASH) {
                colorAllowed = (colorIdx == 4 || colorIdx == 1);  // WHITE or RED
            }  // DH_C_DEFAULT_ONLY: only DEFAULT (already excluded above)
            if (!colorAllowed) {
                return pcFailAt(label, idx, "cmd", "DH: color not supported for this effect");
            }
        }

        // Parse durationOrCount (optional, 0..99)
        if (*p != '\0') {
            const char* durationStart = parseField(&p, fieldLen);
            if (fieldLen == 0) {
                return pcFailAt(label, idx, "cmd", "invalid DH: durationOrCount");
            }
            // Parse as uint and verify we consumed exactly fieldLen digits
            uint32_t duration = 0;
            const char* durationPtr = durationStart;
            int digitsConsumed = parseUint(&durationPtr, duration);
            if (digitsConsumed != (int)fieldLen || *durationPtr != '\0') {
                return pcFailAt(label, idx, "cmd", "DH: durationOrCount must be 0-99");
            }
            if (duration > 99) {
                return pcFailAt(label, idx, "cmd", "DH: durationOrCount must be 0-99");
            }
            // Effect-specific duration matrix: effects with no timed behavior take no
            // duration/count (must be omitted or 0). Only WAG/NOD/FLASH accept non-zero.
            if (kDhDurPolicy[effectIdx] == DH_D_NONE && duration != 0) {
                return pcFailAt(label, idx, "cmd", "DH: this effect takes no duration/count");
            }
        }

        // Check for extra fields (too many colons)
        if (*p != '\0') {
            return pcFailAt(label, idx, "cmd", "DH: too many fields");
        }

        fxOut = (uint8_t)(FX_LOGIC_PSI | FX_HOLO);  // visual effect at term
        return pcOk();
    }

    // :SM is manual diagnostic/calibration only. Learned sequences, clone JSON,
    // imports, editor raw steps, and saved actions must not persist it.
    if (strncmp(cmd, ":SM", 3) == 0) {
        return pcFailAt(label, idx, "cmd", ":SM is diagnostic only");
    }

    const PanelIntent panel = parsePanelIntent(cmd);
    if (panel.valid) {
        fxOut = (strcmp(cmd, ":CL00") == 0) ? FX_NONE : FX_PANEL;
        return pcOk();
    }

    // :SE<dd> — exactly two digits (the Marcduino zero-padded form, e.g.
    // :SE09), so every dome sequence has a single canonical spelling.
    if (strncmp(cmd, ":SE", 3) == 0) {
        const char* p = cmd + 3;
        uint32_t n = 0;
        if (parseUint(&p, n) != 2 || *p != '\0') {
            return pcFailAt(label, idx, "cmd", ":SE want exactly 2 digits");
        }
        fxOut = FX_DOME_SEQUENCE;
        return pcOk();
    }

    // @... logic / PSI / text / holo
    if (cmd[0] == '@') {
        if (strcmp(cmd, "@0T1") == 0 || strcmp(cmd, "@0P1") == 0) {
            fxOut = FX_NONE;  // explicit reset
            return pcOk();
        }
        if (cmd[1] == 'H' && cmd[2] == 'P') {
            fxOut = FX_HOLO;  // @HP... holo
            return pcOk();
        }
        if (isDigit(cmd[1]) &&
            (cmd[2] == 'T' || cmd[2] == 'P' || cmd[2] == 'M')) {
            fxOut = FX_LOGIC_PSI;
            return pcOk();
        }
        return pcFailAt(label, idx, "cmd", "unrecognised @ command");
    }

    // *... holo (with *ST00 as the reset)
    if (cmd[0] == '*') {
        fxOut = (strcmp(cmd, "*ST00") == 0) ? FX_NONE : FX_HOLO;
        return pcOk();
    }

    return pcFailAt(label, idx, "cmd", "unknown command prefix");
}

// $<1..6 alnum>
static ProtocolCheckResult classifyAudio(const char* label, uint8_t idx,
                                         const char* cmd) {
    size_t len = strnlen(cmd, 8);
    if (cmd[0] != '$' || len < 2 || len > 7) {
        return pcFailAt(label, idx, "cmd", "audio want $ + 1-6 chars");
    }
    for (size_t i = 1; i < len; ++i) {
        if (!isAlnum(cmd[i])) {
            return pcFailAt(label, idx, "cmd", "audio chars must be alphanumeric");
        }
    }
    return pcOk();
}

// -----------------------------------------------------------------------------
// Metadata + retrain rules
// -----------------------------------------------------------------------------
ProtocolCheckResult protocolCheckMeta(const char* name, uint32_t suppressMs,
                                      SeqToggleGroup toggleGroup,
                                      uint32_t endTimeMs) {
    if (!nameValid(name)) {
        return pcFail("name", "must match DM:[A-Z0-9_]{1,18}");
    }
    if (!protocolCheckToggleGroupValid(toggleGroup)) {
        return pcFail("toggleGroup", "unknown toggle group");
    }
    if (toggleGroup >= TOGGLE_USER1 && toggleGroup <= TOGGLE_USER4) {
        // The engine's branch-pick/latch switches are not wired for the user
        // latches yet — such a toggle would run open-branch-only and never
        // latch. Reject on save so a Learned toggle cannot execute silently
        // wrong; lift this when the engine gains user-latch state.
        return pcFail("toggleGroup", "user toggle groups are not supported yet");
    }
    if (suppressMs < PC_SUPPRESS_MIN_MS || suppressMs > PC_SUPPRESS_MAX_MS) {
        return pcFail("suppressMs", "out of range (1000..120000)");
    }
    if (suppressMs < endTimeMs) {
        return pcFail("suppressMs", "must be >= sequence end time");
    }

    // Retrain (shadowing) rules: a Learned Sequence bearing a Factory name must
    // keep the factory's toggle semantics coherent (ADR 0006 / issue #2 grill 4).
    const SequenceEntry* factory = sequenceCatalogFind(name);
    if (factory != nullptr) {
        if (factory->toggleGroup != TOGGLE_NONE) {
            if (toggleGroup != factory->toggleGroup) {
                return pcFail("toggleGroup",
                            "retraining a factory toggle requires the same group");
            }
        } else if (toggleGroup != TOGGLE_NONE) {
            return pcFail("toggleGroup",
                        "retraining a factory non-toggle requires group none");
        }
    }
    return pcOk();
}

// -----------------------------------------------------------------------------
// Branch validation + effect-class stamping
// -----------------------------------------------------------------------------
ProtocolCheckResult protocolCheckBranch(const char* label, SeqStep* steps,
                                        uint8_t count) {
    if (steps == nullptr || count == 0) {
        return pcFail(label, "branch is empty");
    }
    if (count > PC_MAX_STEPS) {
        return pcFail(label, "too many steps (max 96)");
    }
    // Exactly one terminal STEP_END, and it must be last.
    for (uint8_t i = 0; i < count; ++i) {
        if (steps[i].type == STEP_END && i != (uint8_t)(count - 1)) {
            return pcFailAt(label, i, "type", "STEP_END only allowed as last step");
        }
    }
    if (steps[count - 1].type != STEP_END) {
        return pcFailAt(label, (uint8_t)(count - 1), "type",
                      "branch must end with STEP_END");
    }

    // Mark loop-body members and validate loop structure (no nesting).
    bool inBody[PC_MAX_STEPS] = { false };
    for (uint8_t i = 0; i < count; ++i) {
        if (steps[i].type != STEP_LOOP) continue;
        const SeqStepParams& lp = steps[i].params;
        if (lp.bodyCount == 0) {
            return pcFailAt(label, i, "bodyCount", "loop body is empty");
        }
        uint16_t last = (uint16_t)i + lp.bodyCount;  // last body index
        if (last >= count || (uint16_t)(last) >= PC_MAX_STEPS) {
            return pcFailAt(label, i, "bodyCount", "loop body overruns the branch");
        }
        if (lp.periodMs < PC_LOOP_PERIOD_MIN || lp.periodMs > PC_LOOP_PERIOD_MAX) {
            return pcFailAt(label, i, "periodMs", "out of range (100..60000)");
        }
        if (lp.durationMs == 0 || lp.durationMs > PC_LOOP_DUR_MAX) {
            return pcFailAt(label, i, "durationMs", "out of range (1..120000)");
        }
        for (uint8_t j = (uint8_t)(i + 1); j <= (uint8_t)last; ++j) {
            if (steps[j].type == STEP_LOOP) {
                return pcFailAt(label, j, "type", "nested loops are not allowed");
            }
            inBody[j] = true;
        }
    }

    // Per-step validation + effect-class stamping + monotonic t (top level only).
    uint32_t prevT = 0;
    PanelIntent pendingFlutter[PC_MAX_STEPS];
    uint8_t pendingFlutterCount = 0;
    for (uint8_t i = 0; i < count; ++i) {
        SeqStep& s = steps[i];

        if (!inBody[i]) {
            if (s.tMs < prevT) {
                return pcFailAt(label, i, "t", "t must be non-decreasing");
            }
            prevT = s.tMs;
        }

        switch (s.type) {
            case STEP_DOME_CMD: {
                uint8_t fx = FX_NONE;
                ProtocolCheckResult r = classifyDome(label, i, s.payload, fx);
                if (!r.ok) return r;
                if (strncmp(s.payload, ":SE", 3) == 0 && inBody[i]) {
                    return pcFailAt(label, i, "cmd", ":SE not allowed inside loops");
                }
                const PanelIntent panel = parsePanelIntent(s.payload);
                if (panel.valid) {
                    if (panel.action == 'F') {
                        if (pendingFlutterCount >= PC_MAX_STEPS) {
                            return pcFailAt(label, i, "cmd", "too many panel flutter steps");
                        }
                        pendingFlutter[pendingFlutterCount++] = panel;
                    } else if (panel.action == 'L') {
                        uint8_t write = 0;
                        for (uint8_t p = 0; p < pendingFlutterCount; ++p) {
                            if (!panelCloseCleansFlutter(pendingFlutter[p], panel)) {
                                pendingFlutter[write++] = pendingFlutter[p];
                            }
                        }
                        pendingFlutterCount = write;
                    }
                }
                s.effectClass = fx;
                break;
            }
            case STEP_AUDIO: {
                ProtocolCheckResult r = classifyAudio(label, i, s.payload);
                if (!r.ok) return r;
                s.effectClass = FX_AUDIO;  // stop on abnormal termination
                break;
            }
            case STEP_AUDIO_CATEGORY: {
                if (s.params.audioCategory >= AUDIO_CATEGORY_COUNT) {
                    return pcFailAt(label, i, "category", "unknown audio category");
                }
                if (s.params.audioFallbackSlot >= AUDIO_SLOT_COUNT) {
                    return pcFailAt(label, i, "fallback", "unknown fallback slot");
                }
                s.effectClass = FX_AUDIO;
                break;
            }
            case STEP_RANDOM: {
                const SeqStepParams& p = s.params;
                if (p.slotSet > SLOTSET_HOLD) {
                    return pcFailAt(label, i, "set", "unknown slot set");
                }
                if (p.pulseMin > RAND_CLOSE || p.pulseMax != 0) {
                    return pcFailAt(label, i, "mode", "unknown random panel mode");
                }
                if (p.moveMs < PC_SM_MOVE_MIN || p.moveMs > PC_SM_MOVE_MAX) {
                    return pcFailAt(label, i, "moveMs", "random move out of range");
                }
                if (p.jitterMs > PC_RAND_JITTER_MAX) {
                    return pcFailAt(label, i, "jitterMs", "jitter too large (max 2000)");
                }
                s.effectClass = FX_PANEL;
                break;
            }
            case STEP_DOME_ROTATE: {
                const SeqStepParams& p = s.params;
                // speedPct must be in -100..100
                if (p.speedPct < -100 || p.speedPct > 100) {
                    return pcFailAt(label, i, "speedPct", "must be -100..100");
                }
                // durationMs must be positive, EXCEPT the explicit neutral stop
                // (speedPct == 0 && durationMs == 0 is the only valid zero case)
                if (p.speedPct == 0 && p.durationMs == 0) {
                    // Explicit neutral stop — valid
                } else if (p.durationMs == 0) {
                    // Non-zero speed with zero duration — reject
                    return pcFailAt(label, i, "durationMs",
                                  "must be positive (or both speedPct and durationMs must be 0 for neutral stop)");
                } else if (p.speedPct == 0 && p.durationMs > 0) {
                    // Zero speed with positive duration — reject (ambiguous intent)
                    return pcFailAt(label, i, "speedPct",
                                  "non-zero speed required when durationMs > 0 (or use speedPct=0, durationMs=0 for neutral stop)");
                }
                s.effectClass = FX_NONE;
                break;
            }
            case STEP_LOOP:
                s.effectClass = FX_NONE;  // body steps carry their own classes
                break;
            case STEP_END:
                s.effectClass = FX_NONE;
                break;
            default:
                return pcFailAt(label, i, "type", "unknown step type");
        }
    }
    if (pendingFlutterCount > 0) {
        return pcFail(label, ":OF requires a later matching :CL in the same branch");
    }
    return pcOk();
}

// -----------------------------------------------------------------------------
// Full draft check
// -----------------------------------------------------------------------------
ProtocolCheckResult protocolCheck(SeqDraft& draft) {
    if (draft.steps == nullptr || draft.stepCount == 0) {
        return pcFail("steps", "main branch is empty");
    }
    // End time = terminal STEP_END of the main branch.
    uint32_t endTimeMs = draft.steps[draft.stepCount - 1].tMs;

    ProtocolCheckResult r =
        protocolCheckMeta(draft.name, draft.suppressMs, draft.toggleGroup, endTimeMs);
    if (!r.ok) return r;

    r = protocolCheckBranch("steps", draft.steps, draft.stepCount);
    if (!r.ok) return r;

    // A toggle group requires a close branch; a non-toggle must not carry one.
    const bool isToggle = (draft.toggleGroup != TOGGLE_NONE);
    const bool hasClose = (draft.closeSteps != nullptr && draft.closeStepCount > 0);
    if (isToggle && !hasClose) {
        return pcFail("closeSteps", "toggle sequence needs a close branch");
    }
    if (!isToggle && hasClose) {
        return pcFail("closeSteps", "non-toggle sequence must not have a close branch");
    }
    if (hasClose) {
        r = protocolCheckBranch("closeSteps", draft.closeSteps, draft.closeStepCount);
        if (!r.ok) return r;
    }
    return pcOk();
}
