// =============================================================================
// src/config_write_lock.cpp
//
// The config write lock (include/config_write_lock.h). ADR 0011, amended
// 2026-09-24.
// =============================================================================

#include "config_write_lock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

// Static storage and no init call: xSemaphoreCreateMutexStatic() takes no
// heap, and a static FreeRTOS mutex may be created before the scheduler
// starts, which is where a namespace-scope initializer runs. Nothing in
// setup() has to remember to create it - which matters because the Write
// Windows that take it live beside their Apply Cores in several files and
// share no init point.
StaticSemaphore_t s_configWriteMutexStorage;
SemaphoreHandle_t s_configWriteMutex = xSemaphoreCreateMutexStatic(&s_configWriteMutexStorage);

// The bound a contended take waits before answering busy. One second is long
// enough to cover another Write Window including its NVS write, and short
// enough that a browser POST answers rather than hangs.
const TickType_t kConfigWriteLockTimeoutTicks = pdMS_TO_TICKS(1000);

}  // namespace

ConfigWriteLock::ConfigWriteLock() : held_(false) {
    if (s_configWriteMutex == nullptr) {
        // Cannot happen with static creation above; kept as the same
        // defensive single-threaded-boot fallback src/seq_store.cpp's lock()
        // takes, so a future move of the creation point cannot turn config
        // writes into a hard failure.
        held_ = true;
        return;
    }
    held_ = (xSemaphoreTake(s_configWriteMutex, kConfigWriteLockTimeoutTicks) == pdTRUE);
}

ConfigWriteLock::~ConfigWriteLock() {
    if (held_ && s_configWriteMutex != nullptr) {
        xSemaphoreGive(s_configWriteMutex);
    }
}
