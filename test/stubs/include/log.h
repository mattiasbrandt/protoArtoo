// =============================================================================
// test/stubs/include/log.h
//
// Stub for log.h — minimal interface for native tests.
// =============================================================================
#pragma once

#include <cstdio>

#define PA_LOG_ERROR(tag, fmt, ...) printf("[ERR] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define PA_LOG_WARN(tag, fmt, ...)  printf("[WRN] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define PA_LOG_INFO(tag, fmt, ...)  printf("[INF] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define PA_LOG_DEBUG(tag, fmt, ...)  printf("[DBG] %s: " fmt "\n", tag, ##__VA_ARGS__)
