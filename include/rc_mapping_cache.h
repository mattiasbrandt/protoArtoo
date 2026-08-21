// =============================================================================
// include/rc_mapping_cache.h
//
// Pure cache state for RC mapping config snapshots.
// =============================================================================
#pragma once

#include "rc_channel_mapper.h"

struct RcMappingCache {
    bool valid;
    bool dirty;
    RcInputMode mode;
    RcMappingConfig config;
};

inline void rcMappingCacheInvalidate(RcMappingCache* cache) {
    if (cache != nullptr) {
        cache->dirty = true;
    }
}

inline bool rcMappingCacheIsDirty(const RcMappingCache& cache) {
    return cache.dirty;
}

inline bool rcMappingCacheGet(const RcMappingCache& cache, RcInputMode mode, RcMappingConfig* out) {
    if (!cache.valid || cache.dirty || cache.mode != mode || out == nullptr) {
        return false;
    }
    *out = cache.config;
    return true;
}

inline void rcMappingCacheSet(RcMappingCache* cache, RcInputMode mode,
                              const RcMappingConfig& config) {
    if (cache == nullptr) {
        return;
    }
    cache->valid = true;
    cache->dirty = false;
    cache->mode = mode;
    cache->config = config;
}
