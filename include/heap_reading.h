// =============================================================================
// include/heap_reading.h
//
// The one place the firmware reads its heap. It offers two named readings
// (CONTEXT.md), and the capability mask behind each is chosen here and nowhere
// else, because both recent heap defects were a caller's own pick of mask:
// #245 paired two masks on the ESP32-P4, and the low-heap warning read a mask
// that counts IRAM until 990dd9b0 moved it.
//
//   Internal Data Heap   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT. What "is the
//                        droid's RAM healthy" is read from: the low-heap
//                        warning, fragmentation, and heapFree / heapMin /
//                        heapLargestBlock on every door, /api/profiler's
//                        figures included.
//   Buffer Reading       the largest free MALLOC_CAP_8BIT block. What a request
//                        handler could be given right now, so admission is
//                        judged by it; published as heapLargest8bit.
//
// Why two readings and not one: on the ESP32-P4 the sdkconfig sets
// CONFIG_SPIRAM_USE_MALLOC=y with CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096, so a
// plain malloc over 4,096 B can come from PSRAM. Admission is right to count
// PSRAM; health must not, or megabytes of PSRAM hide an exhausted internal heap.
//
// The device definitions are src/heap_reading.cpp. The native build links the
// stand-ins in src/native_test_stubs.cpp instead, whose values a test sets
// through include/heap_reading_test_hooks.h.
// =============================================================================
#pragma once

#include <stdint.h>

// One reading of the Internal Data Heap. All three figures come from the same
// mask, which is what makes heapInternalDataFragRatio() below meaningful.
struct HeapInternalDataReading {
    uint32_t free;
    uint32_t minEver;  // low-water mark since boot (or since a profiler window opened)
    uint32_t largest;  // largest free block
};

// The Internal Data Heap's allocator counters (heap_caps_get_info), for
// /api/profiler. minimumFreeBytes is the low-water mark of the open profiler
// window while one is being monitored, and the since-boot mark otherwise.
struct HeapInternalDataInfo {
    uint32_t allocatedBlocks;
    uint32_t freeBlocks;
    uint32_t totalBlocks;
    uint32_t minimumFreeBytes;
};

// The largest-block figure walks every block of the heap (IDF heap_caps.c ->
// multi_heap_get_info -> tlsf_walk_pool); free and minEver are O(1) counters.
HeapInternalDataReading heapReadInternalData();

// The Internal Data Heap's free figure alone, without that walk, for a caller
// that samples per chunk (the upload session's low-water mark).
uint32_t heapReadInternalDataFree();

// The Buffer Reading: the largest free MALLOC_CAP_8BIT block. It walks the
// heap, so a caller on a hot path caches it (include/web_admission.h).
uint32_t heapReadBufferLargest();

void heapReadInternalDataInfo(HeapInternalDataInfo* out);

// -----------------------------------------------------------------------------
// heapInternalDataFragRatio()
// Fragmentation of the Internal Data Heap, from one reading of it.
// 0.0 = all free memory is one contiguous block; toward 1.0 = shattered.
//
// It takes the reading rather than two numbers so a caller cannot pair terms
// from two masks. That pairing is how #245 defect 2 happened on the ESP32-P4:
// free was internal only (~114 KB) while largest included PSRAM (~33 MB),
// producing frag=-287.92 and a fragmentation WARN that could never fire.
//
// The result is clamped at 0: the three reads behind one reading are not
// atomic, so an allocation between them can transiently leave largest > free.
// The clamp keeps a benign race sane; it is not a licence to mix masks.
// -----------------------------------------------------------------------------
inline float heapInternalDataFragRatio(const HeapInternalDataReading& reading) {
    if (reading.free == 0) {
        return 0.0f;
    }
    float ratio = 1.0f - (float)reading.largest / (float)reading.free;
    return (ratio < 0.0f) ? 0.0f : ratio;
}
