// =============================================================================
// include/config_settings_test_hooks.h
//
// Native tests only: every audio Setting, by position (#466).
//
// The droid never walks the audio Settings as a list - each write path finds
// its own by name (audioSettingByName(), include/config_settings.h). A test
// that asks something of EVERY declared Setting, the droid's and the audio
// ones, walks them here, so a Setting added to the table is asked too without
// a hand list to keep in step. Defined in src/config_settings.cpp beside the
// table.
// =============================================================================
#pragma once

#include <stddef.h>

#include "config_settings.h"

size_t audioSettingCount();
const ConfigSetting& audioSettingAt(size_t index);
