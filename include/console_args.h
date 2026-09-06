// =============================================================================
// include/console_args.h
//
// Transport-independent argument contract for the Controller Console (#221,
// ADR 0036, docs/console-protocol.md s.1.2/1.3). This is the seam both
// adapters' commands cross through: a raw "operationName [args...]" line
// splits into a bare operation name plus a fixed-capacity ConsoleArgs table
// of key/value pairs, with quote/escape/UTF-8 handling applied exactly once,
// so every executor - registry action, `operations type=`, and (once #226
// consumes consoleArgsAsParamSource() below) config Apply Cores - sees
// identical keys/values regardless of which adapter the command arrived on.
//
// HEADER-ONLY DELIBERATELY, same reasoning as include/console_completion.h:
// [env:native]'s build_src_filter in platformio.ini is an explicit allowlist
// of src/*.cpp translation units and is fenced on this ticket, so a new
// src/console/console_args.cpp would have no way to be pulled into the
// native test binary. `inline` functions in this header need no
// build_src_filter entry - console_module.cpp and any native test include it
// directly. Pure C/C++ - no Arduino, no FreeRTOS, no heap allocation - so
// every function here is callable and testable on the host exactly as
// written on-device.
// =============================================================================
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "api_param_source.h"
#include "console_catalog.h"

// =============================================================================
// ConsoleArgs: the one fixed-capacity argument representation (criterion 1)
// =============================================================================

// 8 matches the catalog's own current maximum ConsoleParamDescriptor array
// length (4, sound.action.set-mood-map / sound.config.mood-category-map -
// see include/console_completion.h's kArgPoolSize, sized from the same
// measurement) doubled for headroom against a typo-heavy or malicious line;
// well short of the RAM budget this ticket is bound by (an 8-entry array of
// two pointers is 128 B on a 32-bit target, not the "4 KB-class buffer" the
// coordinator warned about).
#define CONSOLE_ARGS_MAX 8

typedef struct {
    const char* key;    // never NULL for an occupied slot
    const char* value;  // never NULL for an occupied slot; "" if `key=` had no value
} ConsoleArg;

typedef struct {
    ConsoleArg items[CONSOLE_ARGS_MAX];
    size_t count;
} ConsoleArgs;

// =============================================================================
// consoleSplitCommandLine: separate the bare operation/meta-command name from
// its (still raw, untokenized) argument remainder.
// =============================================================================

// `line` is mutated in place (the separating whitespace run's first byte is
// overwritten with NUL); both output pointers alias into `line`'s storage,
// so `line` must outlive their use. `*outArgs` is never NULL: it points at
// an empty string when the line carried no arguments.
inline void consoleSplitCommandLine(char* line, char** outName, char** outArgs) {
    static char emptyArgs[1] = {0};
    if (line == nullptr) {
        *outName = nullptr;
        *outArgs = emptyArgs;
        return;
    }

    char* p = line;
    while (*p != '\0' && !isspace((unsigned char)*p)) ++p;

    if (*p == '\0') {
        *outName = line;
        *outArgs = p;  // trailing NUL: empty args, same storage either way
        return;
    }

    *p = '\0';
    ++p;
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    *outName = line;
    *outArgs = p;
}

// =============================================================================
// consoleUtf8Valid: byte-structure UTF-8 validation (docs/console-protocol.md
// s.1.2/1.3 - malformed UTF-8 in a quoted value fails explicitly).
// =============================================================================

// Validates the classic UTF-8 edge cases: overlong 2-byte (C0/C1), overlong
// 3-byte (E0 80-9F), UTF-16 surrogate range (ED A0-BF), overlong 4-byte
// (F0 80-8F), and out-of-Unicode-range 4-byte (F4 90-BF, F5-FF). Safe against
// running past a NUL-terminated buffer's end: every continuation-byte check
// tests `(*p & 0xC0) != 0x80` first, and `'\0' & 0xC0 == 0` always fails that
// test, so a truncated sequence is rejected before this ever reads past the
// terminator.
inline bool consoleUtf8Valid(const char* s) {
    if (s == nullptr) return true;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    while (*p != 0) {
        unsigned char c = *p;
        if (c < 0x80) {
            ++p;
            continue;
        }
        int extra;
        if ((c & 0xE0) == 0xC0) {
            if (c < 0xC2) return false;  // C0/C1: always an overlong encoding
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            if (c > 0xF4) return false;  // beyond U+10FFFF
            extra = 3;
        } else {
            return false;  // stray continuation byte or 0xF8-0xFF lead byte
        }

        unsigned char c1 = p[1];
        if ((c1 & 0xC0) != 0x80) return false;
        if (c == 0xE0 && c1 < 0xA0) return false;   // overlong 3-byte
        if (c == 0xED && c1 > 0x9F) return false;   // UTF-16 surrogate range
        if (c == 0xF0 && c1 < 0x90) return false;   // overlong 4-byte
        if (c == 0xF4 && c1 > 0x8F) return false;   // beyond U+10FFFF

        for (int i = 2; i <= extra; ++i) {
            if ((p[i] & 0xC0) != 0x80) return false;
        }
        p += 1 + extra;
    }
    return true;
}

// =============================================================================
// consoleParseArgs: the quote/escape-aware key=value tokenizer
// (docs/console-protocol.md s.1.2/1.3).
// =============================================================================

typedef enum {
    CONSOLE_ARGS_PARSE_OK = 0,
    // More key=value pairs than CONSOLE_ARGS_MAX can hold. Reuses the
    // existing "line-too-long" wire reason at the call site (an oversized
    // argument list is the same class of "too much for this fixed buffer"
    // as an oversized line, and the protocol's stable reason set has no
    // separate token for it) - not a new reason for a case whose contract
    // reuses the existing token's meaning.
    CONSOLE_ARGS_PARSE_TOO_MANY,
    // A bare word with no '=', an empty key ("=value"), an unterminated or
    // badly escaped quoted value, junk glued directly after a closing
    // quote, or invalid UTF-8 inside a quoted value. Maps to
    // CONSOLE_REASON_MALFORMED_ARGUMENT at the call site.
    CONSOLE_ARGS_PARSE_MALFORMED,
} ConsoleArgParseStatus;

// Parses `args` (the untokenized remainder after the operation/meta-command
// name, `argsLen` bytes not counting a NUL terminator) into `out`, in
// place: `args` is mutated (key/value substrings are NUL-terminated and
// quoted values are unescaped into the same storage, one byte for
// one-or-more input bytes, so it never grows) and every pointer in `out`
// aliases into `args`. `args` must outlive `out`'s use.
//
// `argsLen` is what makes an embedded NUL byte detectable at all (docs/
// console-protocol.md s.1.3: "a NUL byte ... in a quoted value fails
// explicitly"; nothing is silently dropped or "fixed"): if the caller's
// real byte count disagrees with where `args` happens to terminate as a C
// string, there was a 0x00 byte before the end the caller intended -
// rejected outright, the same as any other malformed input. Neither
// shipping adapter can supply a length distinct from strlen() today
// (src/web/api_console.cpp's `command[256]` is populated via `%s`-style
// copies from a form param or an ArduinoJson `const char*`; embedded-cli's
// `cmd->args` is a NUL-terminated C string), so both call this indirectly
// through the length-inferring overload below - reported precisely rather
// than silently: closing that gap for real needs a binary-safe capture
// path on at least one adapter (deeper surgery on api_console.cpp's body/
// JSON handling, or on lib/embedded-cli's fenced byte classification), not
// a tokenizer change. Native tests exercise the real rejection directly,
// with a genuine argsLen/strlen mismatch (test_console_args.cpp).
inline ConsoleArgParseStatus consoleParseArgs(char* args, size_t argsLen, ConsoleArgs* out) {
    out->count = 0;
    if (args == nullptr) {
        return CONSOLE_ARGS_PARSE_OK;
    }
    if (strlen(args) != argsLen) {
        return CONSOLE_ARGS_PARSE_MALFORMED;
    }

    char* p = args;
    for (;;) {
        while (*p != '\0' && isspace((unsigned char)*p)) ++p;
        if (*p == '\0') {
            break;
        }
        if (out->count >= CONSOLE_ARGS_MAX) {
            return CONSOLE_ARGS_PARSE_TOO_MANY;
        }

        // Key: everything up to the first '=', whitespace, or end. A key
        // with no '=' (a bare word) or an empty key ("=value") is not a
        // valid key=value pair per section 1.2.
        char* keyStart = p;
        while (*p != '\0' && *p != '=' && !isspace((unsigned char)*p)) ++p;
        if (*p != '=' || p == keyStart) {
            return CONSOLE_ARGS_PARSE_MALFORMED;
        }
        *p = '\0';
        ++p;

        char* valueStart;
        if (*p == '"') {
            ++p;
            valueStart = p;
            char* w = p;  // unescaping write cursor; w never runs ahead of p
            bool closed = false;
            while (*p != '\0') {
                if (*p == '\\') {
                    char next = p[1];
                    // Only '"' and '\\' are valid escapes inside quotes
                    // (section 1.2); anything else after a backslash is
                    // explicitly rejected rather than silently kept literal.
                    if (next == '"' || next == '\\') {
                        *w++ = next;
                        p += 2;
                        continue;
                    }
                    return CONSOLE_ARGS_PARSE_MALFORMED;
                }
                if (*p == '"') {
                    closed = true;
                    ++p;
                    break;
                }
                *w++ = *p++;
            }
            if (!closed) {
                return CONSOLE_ARGS_PARSE_MALFORMED;  // unterminated quote
            }
            *w = '\0';
            if (*p != '\0' && !isspace((unsigned char)*p)) {
                // Junk immediately after a closing quote with no separating
                // whitespace ("key=\"foo\"bar") - ambiguous, rejected rather
                // than guessed at.
                return CONSOLE_ARGS_PARSE_MALFORMED;
            }
            if (!consoleUtf8Valid(valueStart)) {
                return CONSOLE_ARGS_PARSE_MALFORMED;
            }
        } else {
            valueStart = p;
            while (*p != '\0' && !isspace((unsigned char)*p)) ++p;
            bool atEnd = (*p == '\0');
            *p = '\0';
            if (!atEnd) ++p;
        }

        out->items[out->count].key = keyStart;
        out->items[out->count].value = valueStart;
        ++out->count;
    }
    return CONSOLE_ARGS_PARSE_OK;
}

// Convenience overload for a plain NUL-terminated `args` - both adapters
// (which have no length distinct from strlen()) call this form. Passing
// NULL is the same as passing an empty string: `out->count` is 0, status
// OK.
inline ConsoleArgParseStatus consoleParseArgs(char* args, ConsoleArgs* out) {
    return consoleParseArgs(args, args != nullptr ? strlen(args) : 0, out);
}

// =============================================================================
// Lookup and the ConfigParamSource adapter (#226 reuses this verbatim for
// `value=` and named-key config writes - "consume the shared parser/request
// seam landed by #221", not a second tokenizer).
// =============================================================================

inline const char* consoleArgsFind(const ConsoleArgs& args, const char* key) {
    if (key == nullptr) return nullptr;
    for (size_t i = 0; i < args.count; ++i) {
        if (strcmp(args.items[i].key, key) == 0) {
            return args.items[i].value;
        }
    }
    return nullptr;
}

inline const char* consoleArgsParamGet(void* ctx, const char* name) {
    const ConsoleArgs* args = static_cast<const ConsoleArgs*>(ctx);
    return consoleArgsFind(*args, name);
}

// `args` must outlive the returned ConfigParamSource's use (matches every
// other ConfigParamSource producer - see include/api_param_source.h).
inline ConfigParamSource consoleArgsAsParamSource(const ConsoleArgs& args) {
    ConfigParamSource source;
    source.ctx = const_cast<ConsoleArgs*>(&args);
    source.get = consoleArgsParamGet;
    return source;
}

// =============================================================================
// Schema validation: type, range and enum (criterion 2). Generic over any
// ConsoleParamDescriptor table, so #222/#226's future wiring reuses this
// rather than hand-rolling per-domain checks.
// =============================================================================

typedef enum {
    CONSOLE_ARG_SCHEMA_OK = 0,
    CONSOLE_ARG_SCHEMA_UNKNOWN_KEY,
    CONSOLE_ARG_SCHEMA_MISSING_REQUIRED,
    CONSOLE_ARG_SCHEMA_OUT_OF_RANGE,
} ConsoleArgSchemaStatus;

// Parses `value` as the numeric type `type` names, requiring the WHOLE
// string to be consumed (strtol/strtod's own permissive trailing-garbage
// behavior is not good enough here - "200x" must fail, not silently become
// 200). Returns false for an empty string or any type this function does
// not classify as numeric (string/bool - callers skip those before calling
// this).
inline bool consoleParamParseNumeric(const char* type, const char* value, double* out) {
    if (value == nullptr || value[0] == '\0' || out == nullptr) return false;
    char* end = nullptr;
    if (strcmp(type, CONSOLE_PARAM_TYPE_FLOAT) == 0) {
        double v = strtod(value, &end);
        if (end == value || *end != '\0') return false;
        *out = v;
        return true;
    }
    long v = strtol(value, &end, 10);
    if (end == value || *end != '\0') return false;
    *out = (double)v;
    return true;
}

inline bool consoleParamValueInRange(const ConsoleParamDescriptor& param, double value) {
    if (!param.has_range) return true;
    return value >= param.range_min && value <= param.range_max;
}

inline bool consoleParamValueInEnum(const ConsoleParamDescriptor& param, const char* value) {
    if (param.enum_values == nullptr) return true;
    for (const char* const* v = param.enum_values; *v != nullptr; ++v) {
        if (strcmp(*v, value) == 0) return true;
    }
    return false;
}

// Sweeps every argument the operator supplied against `params` (unknown-key
// pass), then every declared param against what was supplied (missing-
// required, then type/range/enum on whatever is present). `params` may be
// NULL - an operation with no declared schema accepts zero arguments, so any
// supplied key is unknown (this is the fix for the fact-2 bug: a wired
// action no longer silently accepts or ignores arguments its schema never
// named). On failure, `badKeyOut` (if non-NULL) carries the offending key
// name, truncated to fit - "invalid ... with the key named" (docs/console-
// protocol.md s.1.2, #221 criterion 2).
inline ConsoleArgSchemaStatus consoleValidateArgsAgainstSchema(const ConsoleParamDescriptor* params,
                                                                const ConsoleArgs& args,
                                                                char* badKeyOut,
                                                                size_t badKeyOutSize) {
    auto setBadKey = [&](const char* k) {
        if (badKeyOut != nullptr && badKeyOutSize > 0) {
            snprintf(badKeyOut, badKeyOutSize, "%s", k);
        }
    };

    for (size_t i = 0; i < args.count; ++i) {
        bool found = false;
        if (params != nullptr) {
            for (const ConsoleParamDescriptor* p = params; p->name != nullptr; ++p) {
                if (strcmp(p->name, args.items[i].key) == 0) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            setBadKey(args.items[i].key);
            return CONSOLE_ARG_SCHEMA_UNKNOWN_KEY;
        }
    }

    if (params == nullptr) {
        return CONSOLE_ARG_SCHEMA_OK;
    }

    for (const ConsoleParamDescriptor* p = params; p->name != nullptr; ++p) {
        const char* value = consoleArgsFind(args, p->name);
        if (value == nullptr) {
            if (p->required) {
                setBadKey(p->name);
                return CONSOLE_ARG_SCHEMA_MISSING_REQUIRED;
            }
            continue;
        }

        // Enum values are compared as literal strings regardless of the
        // param's declared type - system.action.set-mood's `values: [10,
        // 11, 13, 14]` are numeric in the registry YAML but rendered as
        // strings by the generator (tools/generate_console_catalog.py), so
        // this is the one comparison every enum param needs.
        if (p->enum_values != nullptr) {
            if (!consoleParamValueInEnum(*p, value)) {
                setBadKey(p->name);
                return CONSOLE_ARG_SCHEMA_OUT_OF_RANGE;
            }
            continue;
        }

        if (strcmp(p->type, CONSOLE_PARAM_TYPE_STRING) == 0 ||
            strcmp(p->type, CONSOLE_PARAM_TYPE_BOOL) == 0) {
            // No numeric range applies, and no wired action entry uses
            // type=bool today - shape validation for those is a #226
            // config concern, not invented here.
            continue;
        }

        double numeric = 0.0;
        if (!consoleParamParseNumeric(p->type, value, &numeric) ||
            !consoleParamValueInRange(*p, numeric)) {
            setBadKey(p->name);
            return CONSOLE_ARG_SCHEMA_OUT_OF_RANGE;
        }
    }

    return CONSOLE_ARG_SCHEMA_OK;
}
