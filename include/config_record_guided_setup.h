// =============================================================================
// include/config_record_guided_setup.h
//
// Guided Setup's Record (#351; CONTEXT.md "Record"): where the guided run
// stands and which of its steps have been on screen. Its shared interface is
// declared in include/config_records.h; this header adds the storage form,
// pure, for the tests that pin it.
//
// Three keys, outside ConfigSnapshot for the reason the Droid Build is outside
// it, and stored as a run token plus a comma-separated step key list rather than
// as a bitmask over step positions: the step list is the browser's and it grows,
// so a bit index would silently re-point at a different question the day a step
// is inserted (include/guided_setup.h).
//
// An ABSENT visited record is a controller guided Setup has never drawn on. An
// EMPTY one cannot occur, because the writer stores the sentinel instead - which
// is what keeps the absent case readable at all, and the whole reason a droid
// configured before this feature existed is not reported as never asked.
//
// Defined in src/config_record_guided_setup.cpp.
// =============================================================================
#pragma once

#include "config_io.h"
#include "guided_setup.h"

bool configSerializeGuidedSetup(const GuidedSetupConfig& cfg, ConfigWriter& w);

// Fills *out with guidedSetupDefaults() then overwrites with stored values. A
// stored step key whose form this image cannot accept is dropped and counted in
// *report, the way a damaged Servo Output row is; a stored run number it cannot
// name reads as a run that has not ended, and is counted too.
void configDeserializeGuidedSetup(const ConfigReader& r, GuidedSetupConfig* out,
                                  GuidedSetupRepairReport* report);
