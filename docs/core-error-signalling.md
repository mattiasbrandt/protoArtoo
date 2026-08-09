# Core Error-Signalling Conventions

## Purpose

This document defines the unified failure-signalling conventions for the firmware core (tasks, drivers, state, config, sequences, RC/SBUS, audio). The web layer (HTTP/REST/SSE) follows a separate contract documented in [api.md](api.md); this document covers the internal boundaries between Core 1 real-time tasks and Core 0 configuration/dispatch paths.

**Goal:** Prevent silent failures, make debugging easier, and establish predictable caller behavior across modules.

## Failure Classes

Failures in the core fall into four classes, each with its own signalling strategy:

### 1. Input Validation Failure
**When:** A parser or validator rejects input (command syntax, config value out of range, unrecognized enum).

**Signalling:** Return an explicit **result struct** with a `type`/`status` field indicating success or the class of failure. Callers must check the discriminant field.

**Logging:** Log a WARN when the failure is unexpected or operator-actionable (e.g., unrecognized MarcDuino command, out-of-range config value). Do NOT log expected/benign failures (e.g., an unrecognized audio $ command from old firmware).

**Why:** Input validation can fail for many reasons; returning the reason lets the caller decide whether to log/count/ignore/retry.

**Example:**
```cpp
struct AudioAction {
    AudioActionType type = AUDIO_ACTION_NONE;  // Discriminant; NONE = parse failed
    uint16_t track = 0;
};
AudioAction action = parseAudioDollar(cmd);
if (action.type == AUDIO_ACTION_NONE && strncmp(cmd, "$", 1) == 0) {
    PA_LOG_WARN("audio", "unrecognized $ command: %s", cmd);  // Only on unexpected
}
```

### 2. Operational Failure (Single Occurrence)
**When:** A synchronous operation can fail for one clear reason (NVS read timeout, schema mismatch, permission denied, resource exhausted).

**Signalling:** Return a **bool** + log the specific reason at WARN or ERROR level.

**Logging:** MUST log at function return or immediately after the operation. Callers must not assume silent success; if no log is visible, the operation either succeeded or the logging is missing.

**Why:** Callers assume bool=true unless told otherwise; silent return values are a trap.

**Checking:** Callers MUST check the return value. If the operation is on a fallback path or the failure is non-fatal, document that explicitly in a comment.

**Example:**
```cpp
bool configLoad(Preferences& prefs, ConfigSnapshot* out) {
    // ... schema check ...
    if (stored > CONFIG_SCHEMA_VERSION) {
        configSnapshotDefaults(out);
        PA_LOG_WARN("config", "unsupported schema version %u, resetting to defaults", stored);
        return false;
    }
    return true;
}
```

### 3. Repeated Failure (High Frequency)
**When:** The same error condition happens many times per second (queue overflow, dropped frames, stale cache hits).

**Signalling:** Return a **bool** + maintain a **counter** + emit rate-limited logs (typically 1 per 5 seconds).

**Architecture:** Separate the pure decision logic (when to log) from the side effects (logging + counter increment).

**Why:** Unbounded logging would drown the logs; counters let the operator see magnitude without spam.

**Example:**
```cpp
// Pure decision function
inline bool queueDropShouldLog(QueueDropRateState& state, uint32_t nowMs) {
    if (!state.everLogged) {
        state.lastLogMs = nowMs;
        state.everLogged = true;
        return true;
    }
    if ((uint32_t)(nowMs - state.lastLogMs) > 5000) {
        state.lastLogMs = nowMs;
        return true;
    }
    return false;
}

// Adapter that calls the pure function, logs, and increments counter
void logQueueDrop(QueueDropId queueId, const char* description) {
    QueueDropRateState& state = dropRateStates[queueId];
    if (queueDropShouldLog(state, millis())) {
        PA_LOG_WARN("queue", "%s dropped message", description);
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
    }
}
```

### 4. Expected/Benign Failure (Log and Continue)
**When:** The failure is expected, non-fatal, and requires no operator action (a task's timeout tick firing with no work to do, an optional feature disabled at compile time).

**Signalling:** No return value needed. Log at DEBUG or INFO level if visibility is useful.

**Logging:** Logs should be sparse and honest about the condition ("no work available" is better than "operation failed").

**Why:** Not all code paths need to signal errors back to callers. Some paths are fire-and-forget, and that is fine.

**Example:**
```cpp
// Audio task receives a command with type AUDIO_ACTION_NONE (parsing failed on a benign input)
// Simply skip it — no log, no error propagation.
if (action.type == AUDIO_ACTION_PLAY_TRACK) {
    // dispatch ...
} else if (action.type == AUDIO_ACTION_STOP) {
    // dispatch ...
}
// If action.type is AUDIO_ACTION_NONE, we reach here and continue.
```

## Web Layer Alignment

The web layer (HTTP REST handlers, SSE) uses a separate wire contract ([api.md](api.md), #133):
- **Wire shape:** `{"ok":false,"error":"<token>"}` with optional `hint` and `field` fields
- **Status codes:** 400 (invalid input), 409 (state conflict), 423 (locked), 500 (server error)

**Alignment rule:** When a core failure (types 1–3 above) propagates through a web route handler, map the result to the appropriate HTTP status code and error token. The internal signalling (return struct, bool + log) crosses the seam at the handler boundary via `webSendJsonError()`.

**Example:**
```cpp
// Core layer: configLoad returns false with logged reason
Preferences prefs;
prefs.begin(NVS_NAMESPACE, false);
ConfigSnapshot snap;
if (!configLoad(prefs, &snap)) {
    // Core layer already logged the reason (schema mismatch, etc.)
    // Web layer maps to HTTP 500
    webSendJsonError(req, 500, "config load failed");
    return;
}
```

## Core 1 Real-Time Loop Constraints

- **No failures beyond "drop and continue."** Core 1 loops (DriveTask, AudioTask, etc.) cannot block on I/O or wait for recovery. If a message arrives malformed, drop it and continue. Log at rate-limited intervals only.
- **No heap allocation in failure paths.** If a Core 1 task detects an error, recovery must not require `malloc()`.
- **No portMAX_DELAY waits.** Use short timeouts (milliseconds) or fire-and-forget queues.

Result structs with discriminant fields (Class 1) are safe; they live on the stack. Bool returns (Class 2) in Core 1 are acceptable only for zero-allocation operations (e.g., checking a flag or returning a cached value).

## Checklist for New Code

When adding a function that can fail:

1. **Identify the failure class** — is it validation, operational, repeated, or benign?
2. **Choose the signalling strategy** — result struct, bool + log, bool + counter + log, or log-only.
3. **Log at the right level** — WARN for unexpected, INFO/DEBUG for benign.
4. **Document return semantics** in the function header.
5. **For Core 1 real-time paths:** Use Class 4 (log-and-continue) or Class 3 (counter + rate-limited log). Avoid blocking or allocation.
6. **For web handlers:** Catch core failures at the seam, map to HTTP status/error, and call `webSendJsonError()`.

## Related

- [api.md](api.md) — REST/HTTP error contract
- ADR 0021 — Project-owned WebRequest seam
- [queue_drop_tracker.h](../include/queue_drop_tracker.h) — Reference implementation of Class 3 (repeated failure)
- [audio_dollar_parser.h](../include/audio_dollar_parser.h) — Reference implementation of Class 1 (input validation)
