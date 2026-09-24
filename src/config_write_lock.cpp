// =============================================================================
// src/config_write_lock.cpp
//
// The config write lock (include/config_write_lock.h) and the holder check
// the config writers run against it (include/config_write_window_check.h).
// ADR 0011, amended 2026-09-24.
// =============================================================================

#include "config_write_lock.h"
#include "config_write_window_check.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "logging.h"

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

// The task inside a Write Window, or null. Written only by that task, on take
// and on give; read by any task to ask "is it me". A task can only ever read
// its own handle here while it holds the lock, so a stale value another task
// reads can never equal that reader's handle.
volatile TaskHandle_t s_holder = nullptr;

// Set once by setup() before any config-writing task starts, and by native
// suites from setUp(); read by config writers after that.
volatile bool s_armed = false;
volatile uint32_t s_misses = 0;

}  // namespace

ConfigWriteLock::ConfigWriteLock() : held_(false) {
    if (s_configWriteMutex == nullptr) {
        // Cannot happen with static creation above; kept as the same
        // defensive single-threaded-boot fallback src/seq_store.cpp's lock()
        // takes, so a future move of the creation point cannot turn config
        // writes into a hard failure.
        held_ = true;
        s_holder = xTaskGetCurrentTaskHandle();
        return;
    }
    held_ = (xSemaphoreTake(s_configWriteMutex, kConfigWriteLockTimeoutTicks) == pdTRUE);
    if (held_) {
        s_holder = xTaskGetCurrentTaskHandle();
    }
}

ConfigWriteLock::~ConfigWriteLock() {
    if (!held_) {
        return;
    }
    s_holder = nullptr;
    if (s_configWriteMutex != nullptr) {
        xSemaphoreGive(s_configWriteMutex);
    }
}

void configWriteWindowArm(bool armed) {
    s_misses = 0;
    s_armed = armed;
}

void configWriteWindowExpectHeld(const char* writer) {
    if (!s_armed) {
        return;
    }
    if (s_holder != nullptr && s_holder == xTaskGetCurrentTaskHandle()) {
        return;
    }
    s_misses = s_misses + 1;
    PA_LOG_ERROR("config", "%s ran outside its Write Window: a concurrent config write can revert it",
                 writer);
}

uint32_t configWriteWindowMisses() {
    return s_misses;
}
