// =============================================================================
// include/config_write_window_test_hooks.h
//
// Native tests only: a test standing in for a Write Window (#418).
//
// A suite that arms the holder check (include/config_write_window_check.h)
// counts every config write made outside a Write Window. The config state a
// test seeds mid-test is not such a write - on the droid it would be the boot
// load or an earlier request - and a test that drives a Commit Step directly is
// standing where its Write Window would. Holding one of these for the scope of
// that write says so, so the count is only ever the code under test.
//
//     {
//         const ConfigWriteWindowForTest seed;
//         configCacheReplace(snap);
//     }
//
// It is the config write lock itself, so it takes the lock like a real window
// does: a test that has made the lock look held elsewhere cannot seed inside
// one, and the check reports it.
//
// The miss count lives here too: the droid only logs a miss, and the suites
// that arm the check assert on the count in tearDown(). It is defined in
// src/config_write_lock.cpp beside the check itself.
// =============================================================================
#pragma once

#include <stdint.h>

#include "config_write_lock.h"

using ConfigWriteWindowForTest = ConfigWriteLock;

// Writes made outside a Write Window since the check was last armed.
uint32_t configWriteWindowMisses();
