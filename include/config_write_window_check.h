// =============================================================================
// include/config_write_window_check.h
//
// The holder check: a config writer run outside a Write Window is caught
// rather than remembered (ADR 0011, amended 2026-09-24).
//
// The whole-snapshot config cache writers and the config NVS writers
// (config_store.cpp, config_cache.h) call configWriteWindowExpectHeld() on
// entry. When the check is armed and the calling task does not hold the config
// write lock (include/config_write_lock.h), the write is counted and logged.
// It still goes ahead: the check never resets the board and never refuses a
// write, because the defect it reports is a missing window in the caller, and
// refusing would turn that into a lost write of its own.
//
// Outside the check by design: the one-field setters Core 1 makes
// (configCacheSetStationary(), configCacheSelectSpeedPreset(),
// configCacheSetSpeedLimit()), which never take the lock, and the
// boot-effective configCacheSetActive*() fields.
//
// Armed by setup() once the boot load is done and before any task that writes
// config starts, so the boot load is exempt. In native tests it starts
// disarmed, so a suite seeding state with configCacheApply() is unchanged; the
// adapter and route suites arm it in setUp() and assert in tearDown() that
// nothing missed its window.
// =============================================================================
#pragma once

#include <stdint.h>

// Arm or disarm the check. Arming resets the miss count.
void configWriteWindowArm(bool armed);

// Config writers only: count and log a write the calling task makes without
// holding the config write lock, while the check is armed. `writer` names the
// function, for the log line.
void configWriteWindowExpectHeld(const char* writer);

// Writes made outside a Write Window since the check was last armed.
uint32_t configWriteWindowMisses();
