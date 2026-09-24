// =============================================================================
// include/config_write_lock.h
//
// The config write lock, for Write Window implementations only (ADR 0011,
// amended 2026-09-24; CONTEXT.md "Write Window").
//
// A Write Window is the one guarded span of a config write: take this lock,
// read the cache into the caller's Working Snapshot, run the Apply Core, run
// the Commit Step, release. There is one per write operation, kept beside its
// Apply Core, and every adapter - the REST routes and the Controller Console -
// calls the Write Window instead of taking this lock itself. So include this
// header from the file that implements a Write Window and from nowhere else:
// an adapter that holds the lock is the copy of correctness the Write Window
// exists to remove, and #417 had to add it to nineteen such copies one by one.
//
// The window starts at the cache read, not at the commit, because that is
// where two writers lose an update - each reads the cache, applies its own
// fields, and the second write-back silently reverts the first's fields
// before either reaches NVS.
//
// A blocking FreeRTOS mutex, not a portMUX critical section: the held window
// performs an NVS write (several ms of flash I/O), and holding interrupts
// disabled for that long is unacceptable even confined to Core 0. Blocking
// one non-realtime adapter task while another's write finishes is fine, and
// no Core 1 task (DriveTask, RCInputTask, ...) ever takes this lock: a blocking
// take in a real-time loop is not allowed.
//
// Core 1 does write config, by field and never to NVS: RCInputTask sets the
// speed preset (configCacheSelectSpeedPreset()) and stationary
// (configCacheSetStationary(), via commandedSetStationary()), each inside one
// configCacheMux section. Those two fields are therefore the ones a holder of
// this lock cannot keep still, and the config Commit Step keeps their live
// value whenever its request did not state them (configCacheApplyKeepingLive(),
// #417).
//
// A take that cannot acquire within its bound reports unavailable rather
// than proceeding: the failure this exists to prevent is silent corruption,
// not delay. A Write Window hands that back as "busy"; the Console answers
// `temporarily-unavailable` and the REST routes a 503 busy body.
//
// RAII, so the give runs on every exit path exactly once: a lock leaked on
// one early return would deadlock every future config write on every
// adapter, a worse defect than the race this closes. Hold it across the
// window only - never across emitting the answer, which on the serial
// Console would put this lock and the serial output mutex in a fixed order
// around a blocking device write. Not recursive: a Write Window never calls
// another.
//
// Whether the caller holds it is what the config writers check
// (include/config_write_window_check.h).
// =============================================================================
#pragma once

class ConfigWriteLock {
public:
    ConfigWriteLock();
    ~ConfigWriteLock();

    // False -> the window was held elsewhere for longer than the bound; the
    // Write Window must answer busy and touch no config state.
    bool acquired() const { return held_; }

    ConfigWriteLock(const ConfigWriteLock&) = delete;
    ConfigWriteLock& operator=(const ConfigWriteLock&) = delete;

private:
    bool held_;
};
