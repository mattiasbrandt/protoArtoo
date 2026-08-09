// =============================================================================
// test/stubs/include/esp_task_wdt_stubs.h
//
// Stub declarations for ESP-IDF watchdog and random functions used by
// sequence_dispatcher.cpp in native test builds.
//
// These are on the native include path (-I test/stubs/include) and are
// defined in src/native_test_stubs.cpp.
// =============================================================================
#pragma once

#include <stdint.h>

// Watchdog stubs — needed by sequenceDispatcherTask
void esp_task_wdt_add(void*);
void esp_task_wdt_reset();

// Random number generator stub — used by seqEnginePeek() for random selection
uint32_t esp_random();
