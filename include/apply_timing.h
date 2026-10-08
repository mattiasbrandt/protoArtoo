// =============================================================================
// include/apply_timing.h
//
// When a saved value takes effect (ADR 0068, second amendment; the browser's
// vocabulary is data/apply_timing.js). Every Setting and every Record field
// declares one, because the droid is what decides it: a value the running
// droid reads live changes as it is saved, and one read once at start waits
// for the next.
//
//   Immediate       read live, so the droid changes as it is saved
//   AtReboot        saved at once, read once at start; nothing for the builder
//                   to do but wait for the next start
//   RestartRequired saved at once, read once at start, and the droid is driven
//                   on it: the builder restarts it to use the change
//
// A token only, never words: the words live in the browser's one entry per
// field, which states the same token, and tools/check_setting_words.py fails
// when the two differ. Nothing here is served.
// =============================================================================
#pragma once

#include <stdint.h>

enum class ApplyTiming : uint8_t { Immediate, AtReboot, RestartRequired };
