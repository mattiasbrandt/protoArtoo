// =============================================================================
// include/heap_reading_test_hooks.h
//
// Native-test-only values for the heap readings' stand-ins
// (src/native_test_stubs.cpp). src/heap_reading.cpp reads the ESP-IDF heap and
// is not in [env:native]'s build, so the stubs return these instead. Both
// default to 262144, the figure the native tests written before the module
// assumed; a test that sets them puts them back.
//
// Declared once here, for the stub's definitions and every test that sets
// them, rather than each consumer writing its own `extern` (the slice gate
// refuses a new `extern` inside a .cpp file).
// =============================================================================
#pragma once

#include <stdint.h>

#include "heap_reading.h"

extern HeapInternalDataReading g_test_heap_internal_data;
extern uint32_t g_test_heap_buffer_largest;
