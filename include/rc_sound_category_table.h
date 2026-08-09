// =============================================================================
// include/rc_sound_category_table.h
//
// Sound category lookup table — exposed for testing.
// Maps RobotActionId (sound action) to audio category bounds (lo/hi).
// Do not depend on this from client code; it is an implementation seam
// declared solely to enable unit tests of sound action dispatch.
//
// =============================================================================
#pragma once

#include "rc_action_dispatcher.h"

// Sound action category table entry: maps action to category member pointers
struct RcSoundCategoryEntry {
    RobotActionId action;
    uint16_t RcSoundCategorySnapshot::*lo;
    uint16_t RcSoundCategorySnapshot::*hi;
};

// Sound category table: 12 entries mapping each sound action to category bounds
extern const RcSoundCategoryEntry rcSoundCategoryTable[];
extern const size_t rcSoundCategoryTableSize;
