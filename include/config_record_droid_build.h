// =============================================================================
// include/config_record_droid_build.h
//
// The Droid Build Record (ADR 0047; CONTEXT.md "Record"): which droid a builder
// says they built, and which Parts are on it. Its shared interface is declared
// in include/config_records.h; this header adds the storage form, pure, for the
// tests that pin it.
//
// On its own keys and outside ConfigSnapshot: the snapshot crosses three nested
// frames on the serial config-write path, and nothing on a real-time path reads
// a Droid Build.
//
// Five keys: a design and a variant for each half, and one holding the Fitted
// Parts as a comma-separated Part id list. The Parts are stored as IDS rather
// than as the bitmap they are held in, because the bitmap's indices are
// emission order and the catalog grows - a Part added in the middle would
// re-point every bit after it at a different Part, silently.
//
// An ABSENT Fitted Parts record is a controller that has never been answered,
// and it takes the default complement. An EMPTY one is a real answer - a droid
// with nothing fitted yet - and is kept. Those two must not read alike: a
// builder who cleared their droid would otherwise have the pre-selected design
// re-fitted under them on the next boot.
//
// Defined in src/config_record_droid_build.cpp.
// =============================================================================
#pragma once

#include "config_io.h"
#include "droid_build.h"

bool configSerializeDroidBuild(const DroidBuildConfig& cfg, ConfigWriter& w);

// Fills *out with droidBuildDefaults() then overwrites with stored values. A
// stored half this image's catalog no longer declares is repaired to the
// default and counted in *report, the way a damaged Servo Output row is; a
// stored Part id it no longer declares is dropped and counted.
void configDeserializeDroidBuild(const ConfigReader& r, DroidBuildConfig* out,
                                 DroidBuildRepairReport* report);
