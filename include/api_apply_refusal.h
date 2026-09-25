// =============================================================================
// include/api_apply_refusal.h
//
// What an Apply Core refused, as data (ADR 0011, amended 2026-09-25; #425).
//
// Every Apply Core has always answered a refusal with a sentence
// ("speedLimitMax must be 0..600"), and both adapters used to dig the rest back
// out of it: the Console by matching the sentence's first word, the browser with
// a prefix match and a regex. Reword one sentence and all of them broke without
// a compile error. So a refusal now carries three things beside the sentence:
//
//   field    - the parameter the refusal is about, in the core's own vocabulary
//              (the POST field name). Empty when no one parameter is to blame.
//   reason   - why, from the small set below. Never None on a refusal: every
//              core's setError() takes one, so an error write cannot leave it
//              unset.
//   accepts  - what the field would have taken, where a range or a set applies:
//              `0..600` for a number (the form the sentences already use), or
//              the accepted words comma-joined. Empty where none does.
//
// The sentence stays exactly what it was; HTTP still answers it as `error`.
// An adapter never reads it for any of the three.
//
// The reason tokens are the Controller Console's own (docs/console-protocol.md
// s.3.3), so a refusal reads the same over HTTP and on the Console; the Console
// maps each reason to its ConsoleReason with consoleReasonFromApplyRefusal()
// (include/console_module.h), and a native test pins the two spellings together.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stdio.h>

enum class ApplyRefusalReason : uint8_t {
    None = 0,
    // The value is not one this field takes: outside its range, not one of its
    // words, the wrong type, or too long.
    OutOfRange,
    // A field this write needs was not sent - a partner of a group that is sent
    // together, or the only field of a request that sent nothing usable. The
    // field named is the one that is missing.
    MissingArgument,
    // The value is fine on its own and clashes with another value, sent or
    // saved: speed presets that are not distinct, dome pulses out of order.
    // Names the field that was sent; `accepts` stays empty, because no one
    // value is what would be taken.
    Conflict,
    // A JSON body that did not parse.
    MalformedArgument,
    // The fitted module cannot do this at all (a catalog binding on a sound
    // module with no catalog) - the Console's existing token for that answer.
    NotInThisBuild,
    // Not a reason: the count, for code that walks every one.
    Count,
};

constexpr size_t APPLY_REFUSAL_FIELD_MAX = 32;
constexpr size_t APPLY_REFUSAL_ACCEPTS_MAX = 48;

struct ApplyRefusal {
    ApplyRefusalReason reason = ApplyRefusalReason::None;
    char field[APPLY_REFUSAL_FIELD_MAX] = {0};
    char accepts[APPLY_REFUSAL_ACCEPTS_MAX] = {0};
};

// The wire token for a reason: the `reason` key of an HTTP refusal, and the
// same spelling the Console prints for the ConsoleReason it maps to.
inline const char* applyRefusalReasonToken(ApplyRefusalReason reason) {
    switch (reason) {
        case ApplyRefusalReason::OutOfRange:
            return "out-of-range";
        case ApplyRefusalReason::MissingArgument:
            return "missing-argument";
        case ApplyRefusalReason::Conflict:
            return "conflict";
        case ApplyRefusalReason::MalformedArgument:
            return "malformed-argument";
        case ApplyRefusalReason::NotInThisBuild:
            return "not-in-this-build";
        case ApplyRefusalReason::None:
        case ApplyRefusalReason::Count:
        default:
            return "none";
    }
}

// Record a refusal. `field` and `accepts` may be nullptr for "none".
inline void applyRefusalSet(ApplyRefusal* refusal, ApplyRefusalReason reason, const char* field,
                            const char* accepts = nullptr) {
    refusal->reason = reason;
    snprintf(refusal->field, sizeof(refusal->field), "%s", field != nullptr ? field : "");
    snprintf(refusal->accepts, sizeof(refusal->accepts), "%s", accepts != nullptr ? accepts : "");
}

// A number outside its range, with the range in the one form every sentence and
// page already reads: `lo..hi`.
inline void applyRefusalSetRange(ApplyRefusal* refusal, const char* field, long lo, long hi) {
    char accepts[APPLY_REFUSAL_ACCEPTS_MAX];
    snprintf(accepts, sizeof(accepts), "%ld..%ld", lo, hi);
    applyRefusalSet(refusal, ApplyRefusalReason::OutOfRange, field, accepts);
}
