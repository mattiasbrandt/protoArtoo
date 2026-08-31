// =============================================================================
// include/heap_health.h
//
// Pure-logic heap health arithmetic  --  no hardware, no FreeRTOS.
// Extracted for testability. Used by SafetyMonitorTask and native tests.
// =============================================================================
#pragma once
#include <stdint.h>

// -----------------------------------------------------------------------------
// heapFragRatio()
// Fragmentation ratio for a heap sampled as (free bytes, largest free block).
// 0.0 = all free memory is one contiguous block; toward 1.0 = shattered.
//
// Contract: both samples MUST come from the same heap_caps capability mask.
// Mixing masks is how #245 defect 2 happened on the ESP32-P4: free was internal
// only (~114 KB) while largest included PSRAM (~33 MB), producing frag=-287.92
// and a fragmentation WARN that could never fire.
//
// The result is clamped at 0: even with a same-mask pair the two reads are not
// atomic, so an allocation between them can transiently leave largest > free.
// The clamp keeps a benign race sane; it is not a licence to mix masks.
// -----------------------------------------------------------------------------
inline float heapFragRatio(uint32_t freeBytes, uint32_t largestBytes) {
    if (freeBytes == 0) {
        return 0.0f;
    }
    float ratio = 1.0f - (float)largestBytes / (float)freeBytes;
    return (ratio < 0.0f) ? 0.0f : ratio;
}
