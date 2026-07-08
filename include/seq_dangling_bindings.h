// =============================================================================
// include/seq_dangling_bindings.h
//
// Dangling RC trigger scan for Memory Wipe (DELETE /api/seq). Pure module: no
// Arduino, JSON, or logging, so the report rules are natively testable.
//
// Deleting a Learned Sequence leaves any RC trigger bound to its name a silent
// no-op — unless a Factory Sequence shadows the name, in which case the
// triggers keep resolving and nothing dangles. Both rules live here; the
// DELETE handler is the adapter (snapshot capture, JSON shaping, log replay).
//
// Defined in src/seq_dangling_bindings.cpp.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rc_action_types.h"   // RcTriggerBinding, DOME_ACTION_SEQ
#include "rc_binding_types.h"  // RcBindingSource

struct SeqDanglingBinding {
    RcBindingSource source;
    uint8_t channel;
};

// Scan `slots` for triggers left dangling by deleting the sequence `name`.
// `catalogShadows` = a Factory Sequence exists under the same name (nothing
// dangles). Records are written in slot order; returns the number written
// (at most `cap`).
size_t seqDanglingBindings(const char* name, bool catalogShadows,
                           const RcTriggerBinding* slots, size_t slotCount,
                           SeqDanglingBinding* out, size_t cap);
