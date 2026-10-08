// =============================================================================
// src/heap_reading.cpp
//
// Device definitions of the two heap readings (include/heap_reading.h). The
// capability masks are chosen here and appear nowhere else in the firmware.
// Not in [env:native]'s build: src/native_test_stubs.cpp stands in for it.
// =============================================================================

#include "heap_reading.h"

#include <esp_heap_caps.h>

// Both terms of fragmentation come from ONE capability mask, and the mask that
// matters for health is the internal 8-bit data heap. INTERNAL alone would
// count IRAM-only regions malloc cannot return for byte-addressable data
// (artoo-esp32: 42,392 B on the image #427 measured; ESP.getFreeHeap() reads
// MALLOC_CAP_INTERNAL, so it counts them). 8BIT alone includes PSRAM, which on
// the ESP32-P4 made largest (~33 MB) dwarf internal free (~114 KB), so frag
// read -287.92 and the <10 KB fragmentation WARN was structurally dead (#245
// defect 2).
static constexpr uint32_t kInternalDataCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

// Every byte-addressable block, PSRAM included on the ESP32-P4: the pool a
// request handler's malloc actually draws from.
static constexpr uint32_t kBufferCaps = MALLOC_CAP_8BIT;

HeapInternalDataReading heapReadInternalData() {
    HeapInternalDataReading reading = {};
    reading.free = (uint32_t)heap_caps_get_free_size(kInternalDataCaps);
    reading.minEver = (uint32_t)heap_caps_get_minimum_free_size(kInternalDataCaps);
    reading.largest = (uint32_t)heap_caps_get_largest_free_block(kInternalDataCaps);
    return reading;
}

uint32_t heapReadInternalDataFree() {
    return (uint32_t)heap_caps_get_free_size(kInternalDataCaps);
}

uint32_t heapReadBufferLargest() {
    return (uint32_t)heap_caps_get_largest_free_block(kBufferCaps);
}

void heapReadInternalDataInfo(HeapInternalDataInfo* out) {
    if (out == nullptr) {
        return;
    }
    multi_heap_info_t info;
    heap_caps_get_info(&info, kInternalDataCaps);
    out->allocatedBlocks = (uint32_t)info.allocated_blocks;
    out->freeBlocks = (uint32_t)info.free_blocks;
    out->totalBlocks = (uint32_t)info.total_blocks;
    out->minimumFreeBytes = (uint32_t)info.minimum_free_bytes;
}
