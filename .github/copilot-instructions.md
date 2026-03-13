# Copilot Instructions — protoArtoo

> Guidelines for AI code completion in the protoArtoo ESP32 body controller firmware.
> Read `claude.md` for the full agent guidelines. This file is the concise,
> code-generation-focused companion.

---

## Project Identity

- **Project:** protoArtoo — open-source ESP32 body controller for MK4 astromech droids
- **Target:** Artoo Controller PCB (ESP32, Arduino framework, PlatformIO)
- **Companion dome firmware:** `mattiasbrandt/AstroPixelsPlus` (separate repo)
- **Language:** C++ (Arduino/ESP-IDF), HTML/JS/CSS for web UI
- **Build system:** PlatformIO with two environments: `protoArtoo` (ESP32) and `native` (desktop tests)

---

## Coding Style

### General

- Minimalism and readability over abstraction. This is embedded firmware, not an enterprise app.
- Prefer explicit names: `sendHoverboardFrame()`, `setDriveCommand()`, `parse_dome_rx()`.
- Use `snake_case` for variables and functions, `PascalCase` for types/structs/enums, `UPPER_SNAKE` for constants and defines.
- Indent with 4 spaces. Line limit 100 chars. See `.clang-format` at repo root.
- No `using namespace std;`. Minimize includes to what is actually used.

### Commenting Standard (Mandatory)

Every file gets a file header:
```cpp
// =============================================================================
// src/tasks/drive.cpp
//
// DriveTask — sends 8-byte Gen2.x frames to the hoverboard motor controller.
// [Architecture note, protocol reference, safety note]
// =============================================================================
```

Every non-trivial function gets a function header:
```cpp
// -----------------------------------------------------------------------------
// sendHoverboardFrame()
// [Params, caller, thread safety, hardware reference]
// -----------------------------------------------------------------------------
```

Inline comments explain *why*, not *what*:
```cpp
// Feed TWDT — if this line is not reached within 3 s, chip resets (intentional safety)
esp_task_wdt_reset();
```

### Error/Debug Logging

Use TAG-prefixed `Serial.printf()`:
```cpp
static const char* TAG = "DriveTask";
Serial.printf("[%s] Failsafe active — source: %d\n", TAG, (int)src);
```

Gate verbose per-frame logging behind build flags:
```cpp
#ifdef PA_VERBOSE_DRIVE
    Serial.printf("[%s] frame — spd:%d str:%d\n", TAG, speed, steer);
#endif
```

---

## Architecture Awareness

### Planning Source of Truth

- The authoritative project plans live in `tasks/`.
- Use `tasks/status.md`, `tasks/phase2-tasks.md`, `tasks/phase3-tasks.md`, `tasks/phase4-tasks.md`, `tasks/phase5-tasks.md`, and `tasks/goal.md` as the planning baseline.
- Do **not** treat `.sisyphus/plans/` as the source of truth for this repository when equivalent plan files exist in `tasks/`.

### FreeRTOS Task Model

| Core | Tasks | Purpose |
|------|-------|---------|
| Core 1 | SBUSInputTask, DriveTask, DomeLinkTask, AudioTask, ServoTask | Real-time control |
| Core 0 | WiFiManagerTask, WebServerTask, OTATask | Network + config |

- Tasks communicate via FreeRTOS queues. Never share raw pointers across cores.
- All `RobotState` field access uses `portMUX` critical sections.
- Use `vTaskDelay(pdMS_TO_TICKS(ms))` — never bare `delay()`.
- Use `xQueueSend(..., 0)` (non-blocking) in real-time tasks — never `portMAX_DELAY`.
- No heap allocation (`new`, `malloc`) after `setup()`. Use static/stack buffers.

### UART Ownership

| UART | Owner Task | Target | Baud |
|------|-----------|--------|------|
| UART0 | — | USB debug (Serial) | 115200 |
| UART1 | DomeLinkTask | AstroPixelsPlus (dome, via slip ring) | 9600 |
| SoftSerial | AudioTask | DY-SV5W audio module (TX-only) | 9600 |
| UART2 | DriveTask | Hoverboard GD32F130 (Gen2.x frames) | 115200 |

Only the owning task writes to a given UART. No exceptions.

### Marcduino Command Routing (Body Side)

When generating code that handles dome→body serial RX:
- `$` → AudioTask queue (body plays sound)
- `:SE30`–`:SE36` → ServoTask queue (body arm sequences)
- `:SE01`–`:SE16` → AudioTask queue (sound component of full-droid sequence)
- `:OP`/`:CL`/`:MV` → ServoTask queue (direct arm position)
- `#` → ConfigTask queue
- `*`, `@`, `%`, `&`, `!` → **discard silently** (dome-only, no slave board)

### Audio Module (DY-SV5W Default)

- Command frame: raw bytes `AA 02 [hi] [lo] AB` — not ASCII.
- Volume: `AA 07 [vol] AB` — range 0–30.
- Track mapping: `$001` → play track 1; `$S` → random from configured range.
- The driver interface is `AudioDriver` — code against the interface, not DY-SV5W specifics.
- `PA_AUDIO_DRIVER` build flag selects the implementation.

### Hoverboard Drive (Gen2.x)

- 8-byte frame: `[0xABCD start][int16 steer][int16 speed][uint16 XOR checksum]`
- Send at 50 Hz continuously. Never go silent — always send zero frames when stopped.
- `SPEED_LIMIT_MAX` (default 600) is the absolute cap applied in DriveTask before every frame.

---

## Safety Rules (Never Violate)

1. **Zero-frame rule**: DriveTask always sends frames, even `speed=0, steer=0`. Silence = hoverboard coasts.
2. **SPEED_LIMIT_MAX cap**: Applied unconditionally in DriveTask, regardless of command source.
3. **SBUS boot default**: `sbusSignalLost = true` — prevents driving before RC is confirmed.
4. **Estop is latching**: Requires explicit `POST /api/estop/clear`. Never auto-clears.
5. **TWDT reboot → estop**: Watchdog-triggered reset sets `estop = true` on next boot.
6. **No `portMAX_DELAY` in control loops**: A blocked real-time task is a safety incident.

When generating failsafe-related code, always check against the 5-layer model:
1. SBUS receiver hardware failsafe
2. SBUS software watchdog (200 ms)
3. Web API drive timeout (500 ms)
4. ESP32 TWDT (3 s)
5. Hoverboard own UART timeout (~500 ms)

---

## What to Generate

- FreeRTOS task functions with proper core pinning, stack sizes, and TWDT registration.
- Queue-based inter-task messaging with `CommandSource` tagging.
- Static buffer line parsers with bounds-checked writes and overflow discard.
- REST API handlers that validate/constrain all input, post to queues, never touch hardware directly.
- Unity test cases in `test/test_native/` for all pure-logic functions.
- `constrain()` on every external input before use.
- TAG-prefixed debug logging.
- File and function header comments per the project standard.

---

## Clarifying Uncertainty via Intent Disambiguation
- **Always employ intent disambiguation through interactive multi-choice clarification questions** when facing ambiguous requirements, context gaps, or design decisions. Present options as a structured, numbered or lettered list with concise labels, descriptions, tradeoffs, and a recommended default (marked as such). This allows the user to respond with a single selection (e.g., "A" or "2")—avoid scattering multiple open-ended questions or requiring free-form elaboration.
- Format example:

  Which audio driver should this task target?

  A) DY-SV5W (default—matches current hardware, easy integration with minimal changes; recommended for compatibility)

  B) DFPlayer Mini (supports more audio formats but requires additional wiring and library setup; good for advanced features)

  C) Abstract AudioDriver only (no concrete implementation—provides flexibility for future drivers but delays immediate functionality; use if prototyping)

- Consolidate related ambiguities into a **single multi-choice query** for efficiency; do not fragment into sequential follow-ups.
- For minor or easily inferable details: explicitly state your assumption and proceed without querying.
- For non-trivial decisions: provide 3-5 concrete, mutually exclusive options with brief pros/cons—never use vague, open-ended prompts like "What do you mean?" or "Tell me more."
- For hardware-specific queries (e.g., "Which GPIO for this peripheral?"): **never assume or guess**—either pose a multi-choice list of viable options or flag as "TBD pending user input."

---

## What to Avoid

- **No dynamic allocation in task loops** — no `new`, `malloc`, `String` concatenation in loops.
- **No direct hardware access from web handlers** — always post to a queue.
- **No guessing GPIO pins** — if a pin is `TBD` in `config.h`, leave it as `TBD` (compile error is intentional).
- **No Reeltwo library** — body firmware uses plain serial parsing, not Reeltwo dispatch.
- **No dome-side sound code** — the dome has no audio module. All sound is body-side.
- **No `%` prefix forwarding** — there is no MarcDuino Slave board.
- **No `delay()` in tasks** — use `vTaskDelay()`.
- **No WiFi credentials in code** — use `src/secrets.h` (gitignored).
- **No unnecessary dependencies** — check approved list in `platformio.ini` first.
- **No ADC2 reads when WiFi is active** — use ADC1 (GPIO 32–39) only.
- **No `ArduinoOTA.handle()` on Core 1** — OTA runs on Core 0.
- **No async web server initialization outside WiFi event callback**.

---

## NVS Pattern

Tasks never call NVS directly. Config lives in `RobotState.cfg_*` fields:
```cpp
// Read: tasks use robotState.cfg_speedLimitMax (populated at boot)
// Write: web API handler writes to NVS, then updates cfg_* under portMUX
```

NVS namespace: `"proto"`. Key naming: `snake_case` (`vol_limit`, `ch8_mode_lock`, `sbus_tmout`).

---

## Web API Pattern

```cpp
// All API handlers follow this structure:
server.on("/api/drive", HTTP_POST, [](AsyncWebServerRequest* req) {},
    NULL, [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, data, len)) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
        return;
    }
    int16_t speed = constrain(doc["speed"] | 0, -SPEED_LIMIT_MAX, SPEED_LIMIT_MAX);
    int16_t steer = constrain(doc["steer"] | 0, -SPEED_LIMIT_MAX, SPEED_LIMIT_MAX);
    setDriveCommand(speed, steer, SRC_WEB_API);
    req->send(200, "application/json", "{\"ok\":true}");
});
```

Key points: validate JSON, constrain values, tag command source, respond with JSON.

---

## Test Pattern

All pure-logic functions get a native test in `test/test_native/`:
```bash
pio test -e native           # run all
pio test -e native -f test_marcduino_rx  # run one file
```

Test file structure:
```cpp
#include <unity.h>
#include "module_under_test.h"

void setUp() { /* reset state */ }
void tearDown() {}

void test_specific_behavior() {
    // Arrange → Act → Assert
    TEST_ASSERT_EQUAL(expected, actual);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_specific_behavior);
    return UNITY_END();
}
```

---

## Verification Reminders

Before considering any change complete:
1. `pio run -e protoArtoo` — compiles with `-Werror`
2. `pio test -e native` — all logic tests pass
3. `pio check` — no high/medium static analysis findings
4. Safety invariants preserved (see Safety Rules above)
5. Conventional commit message with correct scope

When in doubt, consult `claude.md` for the full decision framework and `tasks/goal.md` for the canonical firmware specification.
