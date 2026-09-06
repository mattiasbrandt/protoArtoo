// =============================================================================
// include/rc_audio_category_table.h
//
// Audio category lookup table  --  exposed for testing.
// Maps RobotActionId (audio action) to audio category bounds (lo/hi).
// Do not depend on this from client code; it is an implementation seam
// declared solely to enable unit tests of audio action dispatch.
//
// =============================================================================
#pragma once

#include "rc_action_dispatcher.h"

// Audio action category table entry: maps action to category member pointers
struct RcAudioCategoryEntry {
    RobotActionId action;
    uint16_t RcAudioCategorySnapshot::*lo;
    uint16_t RcAudioCategorySnapshot::*hi;
};

// Audio category table: 12 entries mapping each audio action to category bounds
extern const RcAudioCategoryEntry rcAudioCategoryTable[];
extern const size_t rcAudioCategoryTableSize;
