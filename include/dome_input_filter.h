// =============================================================================
// include/dome_input_filter.h
//
// Pure repeated-sample filter for dome SBUS analog input.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stdlib.h>

struct DomeInputFilter {
    bool initialized;
    int lastAcceptedRaw;
    int pendingRaw;
    uint8_t pendingCount;
};

struct DomeInputFilterResult {
    bool accepted;
    int raw;
};

inline DomeInputFilterResult domeInputFilterUpdate(DomeInputFilter* filter, int raw, int center,
                                                   int neutralBand, int stableBand,
                                                   uint8_t confirmFrames) {
    DomeInputFilterResult result = {};
    result.raw = raw;

    if (filter == nullptr) {
        result.accepted = true;
        return result;
    }

    if (!filter->initialized) {
        filter->initialized = true;
        filter->lastAcceptedRaw = raw;
        filter->pendingRaw = raw;
        filter->pendingCount = 0;
        result.accepted = true;
        return result;
    }

    const bool wasNearNeutral = abs(filter->lastAcceptedRaw - center) <= neutralBand;
    const bool nowNearNeutral = abs(raw - center) <= neutralBand;
    if (wasNearNeutral && !nowNearNeutral) {
        if (filter->pendingCount == 0 || abs(raw - filter->pendingRaw) > stableBand) {
            filter->pendingRaw = raw;
            filter->pendingCount = 1;
            return result;
        }

        filter->pendingCount++;
        if (filter->pendingCount < confirmFrames) {
            return result;
        }
    }

    filter->pendingCount = 0;
    filter->pendingRaw = raw;
    filter->lastAcceptedRaw = raw;
    result.accepted = true;
    return result;
}
