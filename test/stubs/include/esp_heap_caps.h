// =============================================================================
// test/stubs/include/esp_heap_caps.h
// ESP32 heap capabilities stub for native host builds.
// =============================================================================
#pragma once

#include <stddef.h>

// Heap capability flags
#define MALLOC_CAP_EXEC     (1 << 0)
#define MALLOC_CAP_32BIT    (1 << 1)
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_DMA      (1 << 3)
#define MALLOC_CAP_SPIRAM   (1 << 4)

// Get the largest free block in a heap
// For native tests, return a fixed large value
inline size_t heap_caps_get_largest_free_block(uint32_t caps) {
    (void)caps;  // Unused
    return 262144;  // Stub value: 256KB
}
