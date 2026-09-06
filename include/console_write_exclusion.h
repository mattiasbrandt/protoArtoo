// =============================================================================
// include/console_write_exclusion.h
//
// The Console's write-exclusion rule (#227, docs/console-protocol.md s.4.1)
// and the two consequences it has outside the executor that refuses the
// write: a write-excluded key is never OFFERED by Tab completion, and a line
// that assigns one is never KEPT in an operator-recallable history.
//
// ONE RULE, BOTH ADAPTERS. The authority is the catalog's `write_excluded`
// flag reached through the operation descriptor (include/console_catalog.h,
// generated from docs/action-registry.yaml's `write_excluded: true`) - not a
// list of key spellings and not a "password" pattern, so a field the registry
// marks tomorrow is excluded here with no further edit. The browser adapter
// applies the same rule over the same flag, which reaches it as the
// `write-excluded` disposition in `help <op>`'s params field
// (src/console/console_module.cpp emits it, data/app.js reads it).
//
// KNOWN RESIDUAL, deliberately not closed here. The rule is per-operation:
// it can only recognise a key that the operation named by the line's first
// token declares write-excluded. Two shapes therefore still reach history,
// both of which the executor refuses with `secret-not-settable` because its
// own refusal (console_module.cpp's consoleArgKeyNamesASecret()) matches the
// key NAME instead: a password key typed against a misspelled operation
// name, and a password-named key that no operation declares at all
// (`wifi.config.settings my-password=...`). Closing either would mean either
// a second, name-shaped vocabulary here, or a catalog-wide key scan the
// browser adapter cannot mirror (it can fetch one operation's parameters,
// never all 190+), and the two adapters refusing different lines is the
// worse defect. Reported on #227 rather than fixed by divergence.
//
// HEADER-ONLY DELIBERATELY, for the reason include/console_args.h and
// include/console_completion.h give: platformio.ini's [env:native]
// `build_src_filter` is an explicit allowlist of src/*.cpp translation units
// and is fenced on this ticket, so a new src/console/*.cpp would have no way
// into the native test binary. `inline` functions in a header need no filter
// entry. Pure C++ - no Arduino, no FreeRTOS, no allocation - so this runs on
// the host exactly as it runs on the device.
// =============================================================================
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "console_catalog.h"

// The `index`-th parameter of `params` that the Console will accept a value
// for, or NULL when there is no such parameter.
//
// `index` is a DENSE counter over the OFFERED parameters, not a subscript
// into the descriptor array. That distinction is the whole point: a caller
// enumerating candidates walks index 0, 1, 2 ... and stops at the first NULL
// (embedded-cli's completion scan does exactly this - see
// EmbeddedCli::getCompletionCandidate), so skipping an excluded parameter by
// returning NULL at its index would truncate the enumeration there and drop
// every parameter after it. That is the "reduced completion catalog" the epic
// forbids (#206), not a skip.
inline const ConsoleParamDescriptor* consoleOfferedParamAt(const ConsoleParamDescriptor* params,
                                                           uint16_t index) {
    if (params == nullptr) {
        return nullptr;
    }
    uint16_t offered = 0;
    for (const ConsoleParamDescriptor* p = params; p->name != nullptr; ++p) {
        if (p->write_excluded) {
            // Never offered. The Console refuses a value for this parameter
            // (docs/console-protocol.md s.4.1), so completing the key would
            // invite the operator to type a line that can only be refused -
            // and would put the secret they then type through the editor
            // buffer and this ticket's history path on the way there.
            // It stays visible in `help`, which is where a documented-but-
            // unsettable field belongs.
            continue;
        }
        if (offered == index) {
            return p;
        }
        ++offered;
    }
    return nullptr;
}

// Case-insensitive equality between the key spelled by [keyBegin, keyLen) -
// a slice of a raw command line, not NUL-terminated - and a catalog parameter
// name.
//
// Case-insensitive on purpose, though every other key lookup in the Console
// is exact (consoleArgsFind(), consoleValidateArgsAgainstSchema()): the
// executor's own secret refusal is case-insensitive
// (console_module.cpp's consoleStartsWithIgnoringCase()), so an exact match
// here would make the history rule NARROWER than the refusal it backs -
// `sta-Password=hunter2` would be refused and then kept. A case-insensitive
// match can only ever refuse more lines than an exact one, never fewer, and
// the extra lines it refuses are lines that cannot execute anyway (an
// exact-match lookup never resolves them to a settable parameter).
inline bool consoleKeyMatchesIgnoringCase(const char* keyBegin, size_t keyLen, const char* name) {
    if (keyBegin == nullptr || name == nullptr) {
        return false;
    }
    size_t i = 0;
    for (; i < keyLen; ++i) {
        if (name[i] == '\0') {
            return false;
        }
        if (tolower((unsigned char)keyBegin[i]) != tolower((unsigned char)name[i])) {
            return false;
        }
    }
    return name[i] == '\0';
}

// Whether `line` - a raw command line exactly as the operator typed it -
// assigns a value to a parameter the Console will not accept one for.
//
// Callers use this to keep such a line out of anything that can hand it back
// later: the serial editor's Up-arrow ring (lib/embedded-cli's Patch 6) and
// the browser's persisted command history (data/app.js applies the same rule
// in JS). The decision is made BEFORE the line is stored, never by storing it
// and removing it afterwards - a secret that reaches a ring has been in a
// buffer that outlives the decision, and Up-arrow is not that buffer's only
// reader.
//
// Reads `line` in place and mutates nothing: the operation name is the only
// text copied out (it is never a secret), and a value's bytes are only ever
// walked to find where the value ends, never compared or copied.
inline bool consoleLineAssignsWriteExcludedValue(const char* line) {
    if (line == nullptr) {
        return false;
    }

    const char* p = line;
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;

    const char* nameBegin = p;
    while (*p != '\0' && !isspace((unsigned char)*p)) ++p;
    const size_t nameLen = (size_t)(p - nameBegin);

    // 64 B holds the longest catalog name with margin, the same bound and the
    // same measurement include/console_completion.h sizes its operation-name
    // buffer from ("dome.action.droid-sequence-beep-cantina", 39 chars).
    char opName[64];
    if (nameLen == 0 || nameLen >= sizeof(opName)) {
        // An empty line, or a first token no catalog name can match: nothing
        // resolves, so no parameter of it can be known write-excluded (the
        // per-operation residual documented in this file's header).
        return false;
    }
    memcpy(opName, nameBegin, nameLen);
    opName[nameLen] = '\0';

    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(opName);
    if (entry == nullptr || entry->params == nullptr) {
        return false;
    }

    // Walks the argument tokens the way consoleParseArgs() does
    // (include/console_args.h): a key up to '=', then a value that is either
    // a quoted run - with \" and \\ escapes - or a bare run to the next
    // space. Honouring the quoting is what keeps a legitimate line storable:
    // an SSID may contain a space and an '=' (`sta-ssid="lab sta-password=x"`
    // is one argument, not two), and mistaking a value's own text for a later
    // key would refuse a line carrying no secret at all.
    while (*p != '\0') {
        while (*p != '\0' && isspace((unsigned char)*p)) ++p;
        if (*p == '\0') {
            break;
        }

        const char* keyBegin = p;
        while (*p != '\0' && *p != '=' && !isspace((unsigned char)*p)) ++p;
        const size_t keyLen = (size_t)(p - keyBegin);

        if (*p != '=' || keyLen == 0) {
            // A bare word, or an empty key ("=value"): not a key=value pair
            // at all, and the parser rejects the whole line for it. Skip to
            // the next token rather than stopping - a later token on the same
            // line can still be the excluded assignment, and a malformed line
            // is exactly the one whose refusal names no field.
            while (*p != '\0' && !isspace((unsigned char)*p)) ++p;
            continue;
        }
        ++p;  // past '='

        for (const ConsoleParamDescriptor* param = entry->params; param->name != nullptr; ++param) {
            if (param->write_excluded &&
                consoleKeyMatchesIgnoringCase(keyBegin, keyLen, param->name)) {
                return true;
            }
        }

        if (*p == '"') {
            const char* quotedStart = p;
            bool closed = false;
            ++p;
            while (*p != '\0') {
                if (*p == '\\' && p[1] != '\0') {
                    p += 2;
                    continue;
                }
                if (*p == '"') {
                    ++p;
                    closed = true;
                    break;
                }
                ++p;
            }
            if (!closed) {
                // UNTERMINATED quote. Walking to end-of-string here would
                // swallow the rest of the line and with it any later
                // assignment - including a write-excluded one, which is then
                // never examined and the line is stored. That is the exact
                // fail-OPEN this rule cannot afford, so the unterminated run
                // is rescanned as an ordinary unquoted value instead: back to
                // the opening quote, forward to the next space, and carry on
                // reading keys.
                //
                // It costs the operator nothing. consoleParseArgs() rejects an
                // unterminated quote outright (CONSOLE_ARGS_PARSE_MALFORMED),
                // so a line reaching this branch can never execute; the only
                // lines this can newly refuse to remember are lines that were
                // never runnable. A CLOSED quoted value keeps its exact
                // meaning - "lab sta-password=x" is one SSID, not an
                // assignment, and stays storable.
                p = quotedStart;
                while (*p != '\0' && !isspace((unsigned char)*p)) ++p;
            }
        } else {
            while (*p != '\0' && !isspace((unsigned char)*p)) ++p;
        }
    }

    return false;
}
