// =============================================================================
// include/console_completion.h
//
// Catalog-driven Tab completion candidate source for the serial adapter's
// embedded-cli instance (ADR 0034, #238).
//
// Bound as EmbeddedCli::getCompletionCandidate (lib/embedded-cli's Patch 5,
// see lib/embedded-cli/VENDORED.md) from src/tasks/console_task.cpp. Reads
// the catalog directly (console_catalog.h) rather than copying entries into
// embedded-cli bindings - registering 175 catalog entries as
// CliCommandBindings would cost 20 B each (~3.5 KB), most of the artoo-esp32
// board's remaining static RAM headroom after #233 (9 100 B free); this file
// enumerates the flash-resident catalog in place instead.
//
// HEADER-ONLY DELIBERATELY: platformio.ini's native `build_src_filter` is
// fenced for this ticket (#238's pinned coordinator comment), so a new
// `src/tasks/*.cpp` translation unit would have no way to be pulled into the
// native test env. Defining the logic as `inline` functions here means the
// native test just #includes this header directly - no build_src_filter
// entry needed, since there is no separate .cpp to compile. console_task.cpp
// (Arduino-only, already outside native test scope) includes it the same
// way for production wiring. `console_catalog.cpp` (consoleCatalogGetEntries
// / consoleCatalogFindByName) is already part of every native test binary
// via the existing filter entry, so this header links against it for free.
// =============================================================================

#pragma once

extern "C" {
#include "embedded_cli.h"
}

#include "console_catalog.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace pa_console_completion {

// Sized from the catalog's own current maximum ConsoleParamDescriptor array
// length (4, measured 2026-08-29 from src/console/console_catalog.cpp:
// g_params_sound_action_set_mood_map and g_params_sound_action_set_category_range
// each carry 4 params; every other array carries fewer), plus margin.
//
// Each pool slot is addressed by the SAME `index` the embedded-cli scan
// passes to consoleCompletionCandidate() (see the field doc on
// EmbeddedCli::getCompletionCandidate in embedded_cli.h): index N always
// lands in slot N, so distinct indices never alias the same buffer within
// one scan. That is required, not incidental - the scan keeps the FIRST
// matching candidate's pointer alive and dereferences it against every
// later candidate to compute their shared prefix, so overwriting an earlier
// index's slot while a later index is still being compared would corrupt
// that comparison. If a future operation's param count exceeds this pool,
// consoleCompletionCandidate() returns NULL beyond it (see below) - that
// operation's later parameters simply stop being completable rather than
// aliasing a slot still referenced elsewhere in the scan.
inline constexpr size_t kArgPoolSize = 8;

// Longest observed param name ("aux_led_count", 13 chars) + "=" + NUL, with
// margin.
inline constexpr size_t kArgCandidateMax = 32;

inline char g_argCandidatePool[kArgPoolSize][kArgCandidateMax];

}  // namespace pa_console_completion

// Bound as embeddedCli->getCompletionCandidate. Enumerates Tab-completion
// candidates for the CURRENT TOKEN of the CLI's command buffer:
//  - No space typed yet (tokenStart == 0): candidates are canonical catalog
//    operation NAMES ONLY, never aliases - "Tab completes canonical
//    operations" is the epic's own acceptance-matrix wording (##206), and
//    aliases stay typable-but-not-completable, matching that.
//  - A space already typed: candidates are the resolved operation's
//    argument KEYS, each returned as "<key>=" (docs/console-protocol.md
//    s.1.2: key=value with no space around "="). The operation is resolved
//    from the line's FIRST token only, regardless of how many argument
//    tokens already follow it - later tokens never change which operation
//    is being completed.
//
// Availability (available_on_board / available_in_build / executor_ready)
// is NOT filtered here: "known-but-unavailable operations remain
// completable" is a named acceptance criterion, and availability is
// re-checked at execution (console_module.cpp), never at discovery or
// completion time.
//
// Quote-aware tokenization of prior argument values (docs/console-protocol.md
// s.1.2) is deliberately NOT implemented here - only a plain space split is
// used to find the first token. include/console_cli_line.h's SCOPE FENCE
// reserves quote-aware argument parsing for #221/#226; this file only needs
// to find the operation name, which is always the first token and never
// itself contains a space, so the plain split is sufficient and correct for
// that narrower purpose.
inline const char *consoleCompletionCandidate(EmbeddedCli *cli, uint16_t index) {
    using namespace pa_console_completion;

    const char *cmdBuffer = embeddedCliGetCmdBuffer(cli);
    if (cmdBuffer == nullptr) {
        return nullptr;
    }

    size_t len = strlen(cmdBuffer);
    size_t firstSpace = len;
    for (size_t i = 0; i < len; ++i) {
        if (cmdBuffer[i] == ' ') {
            firstSpace = i;
            break;
        }
    }

    if (firstSpace == len) {
        // Operation-name position: no space typed yet.
        size_t count = 0;
        const ConsoleCatalogEntry *entries = consoleCatalogGetEntries(&count);
        if (entries == nullptr || index >= count) {
            return nullptr;
        }
        return entries[index].name;
    }

    // Argument-key position: resolve the operation from the first token.
    // 64 B holds the longest catalog name measured 2026-08-29
    // ("dome.action.droid-sequence-beep-cantina", 39 chars) with margin for
    // catalog growth; this buffer dominates this function's stack frame
    // (measured via -fstack-usage, see lib/embedded-cli/VENDORED.md Patch 5),
    // so it is sized deliberately rather than left at a round, larger number.
    char opNameBuf[64];
    size_t opNameLen = firstSpace < sizeof(opNameBuf) - 1 ? firstSpace : sizeof(opNameBuf) - 1;
    memcpy(opNameBuf, cmdBuffer, opNameLen);
    opNameBuf[opNameLen] = '\0';

    const ConsoleCatalogEntry *entry = consoleCatalogFindByName(opNameBuf);
    if (entry == nullptr || entry->params == nullptr) {
        return nullptr;
    }

    if (index >= kArgPoolSize) {
        return nullptr;
    }
    const ConsoleParamDescriptor *param = &entry->params[index];
    if (param->name == nullptr) {
        // NULL-terminated array (catalog contract, console_catalog.cpp):
        // end of this operation's parameter list.
        return nullptr;
    }

    char *slot = g_argCandidatePool[index];
    snprintf(slot, kArgCandidateMax, "%s=", param->name);
    return slot;
}
