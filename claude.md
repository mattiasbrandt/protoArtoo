# AI Coding Agent Guidelines — protoArtoo (claude.md)

These rules define how an AI coding agent should plan, execute, verify, communicate, and recover when working on **protoArtoo** — an ESP32 body controller firmware for hoverboard-driven MK4 astromech droids. Optimize for correctness, minimalism, and developer experience.

> **This is safety-critical firmware.** protoArtoo controls a 20 kg wheeled robot with hoverboard motors. A bug in drive, failsafe, or SBUS handling can cause physical injury or property damage. Every code change must be evaluated through a safety lens first.

---

## Operating Principles (Non-Negotiable)

- **Correctness over cleverness**: Prefer boring, readable solutions that are easy to maintain. In embedded firmware, clever code kills debugging sessions.
- **Smallest change that works**: Minimize blast radius; don't refactor adjacent code unless it meaningfully reduces risk or complexity. A one-line failsafe fix does not need surrounding cleanup.
- **Leverage existing patterns**: Follow established project conventions before introducing new abstractions or dependencies. protoArtoo uses FreeRTOS tasks, `portMUX` critical sections, and queue-based inter-task messaging — extend these patterns, don't replace them.
- **Prove it works**: "Seems right" is not done. Validate with `pio run`, `pio test -e native`, `pio check`, and/or a reliable manual repro on hardware. For drive/failsafe changes: verify on a real hoverboard with kill switch in hand.
- **Be explicit about uncertainty**: If you cannot verify something (e.g., GPIO assignment still TBD from Phase 0 trace), say so and propose the safest next step to verify. Never guess a pin number.

---

## Project Context (Read This First)

### What protoArtoo Is

- **Target:** Artoo Controller PCB — ESP32 (WROOM-32 or D1 Mini, unconfirmed)
- **Framework:** Arduino on ESP32 via PlatformIO (`espressif32@5.2.0`)
- **Architecture:** FreeRTOS tasks split across dual cores (Core 0: WiFi/web; Core 1: real-time control)
- **Drive:** Hoverboard motors via Gen2.x 8-byte UART frames at 50 Hz (115200 baud, ESP32 UART1 / PCB S1)
- **RC input:** Dual SBUS receivers via RMT peripheral (GPIO 15 = drive, GPIO 13 = dome spin)
- **Audio:** DY-SV5W module (default) via UART 9600 TX-only — body is the **sole audio source** for the entire droid
- **Dome link:** Bidirectional Marcduino serial (9600 baud) over slip ring to `mattiasbrandt/AstroPixelsPlus`
- **Web:** ESPAsyncWebServer with LittleFS-served UI, REST API, SSE live updates
- **Config:** NVS (Preferences) namespace `"proto"` — tasks never call NVS directly; they read `RobotState.cfg_*` fields

### What protoArtoo Is NOT

- Not a MarcDuino Master or Slave node — no Reeltwo library on the body
- Not using SHADOW, PS2, or any classic control stack
- No dome-side sound module — `PREFERENCE_MARCSOUND = kNone` in the dome fork
- No MarcDuino Slave board — `%` prefix commands have no destination
- No dynamic memory allocation after `setup()` — all buffers are static

### Critical Safety Invariants

These must never be violated by any code change:

1. **Zero-frame rule**: `DriveTask` sends `speed=0, steer=0` frames continuously — never goes silent. Silence means the hoverboard coasts on last command for ~500 ms.
2. **SPEED_LIMIT_MAX cap**: Applied unconditionally in `DriveTask` before every frame, regardless of command source. Currently 600 (of 1000 max).
3. **SBUS boot default**: `sbusSignalLost = true` on boot — clears only on first valid SBUS frame. Prevents web API driving before RC is confirmed present.
4. **Estop is latching**: `estop = true` requires explicit `POST /api/estop/clear` — never auto-clears.
5. **TWDT reboot → estop**: If `esp_reset_reason() == ESP_RST_TASK_WDT`, set `estop=true` before anything else.

---

## System Configuration

### Elevated Commands (sudo)
- Always use `sudo -A` instead of `sudo` for commands requiring elevated privileges.
- The system has `SUDO_ASKPASS` configured with a GUI askpass helper.

### ESP32 Upload and Monitoring
- Default USB upload port: `/dev/ttyUSB0` — never use `/dev/ttyS0`.
- Flash: `pio run -e protoArtoo --target upload --upload-port /dev/ttyUSB0`
- OTA: `pio run -e protoArtoo --target upload --upload-port 192.168.4.1`
- Monitor: `pio device monitor` (115200 baud)
- Upload filesystem: `pio run -e protoArtoo --target uploadfs`

### Build Commands (Quick Reference)
```bash
pio run -e protoArtoo           # compile firmware
pio run -e protoArtoo -t upload # flash via USB (/dev/ttyUSB0)
pio test -e native              # fast logic tests (no hardware)
pio test -e protoArtoo          # on-device tests (requires ESP32)
pio check                       # static analysis (cppcheck)
pio run -e protoArtoo -t uploadfs  # upload LittleFS web assets
```

---

## Workflow Orchestration

### 1. Plan Mode Default
- Enter plan mode for any non-trivial task (3+ steps, multi-file change, architectural decision, safety-impacting behavior).
- Include verification steps in the plan (not as an afterthought).
- If anything goes sideways or new information invalidates the plan: **stop immediately**, update the plan, then continue — do not keep pushing.
- Write a crisp spec first when requirements are ambiguous (inputs/outputs, edge cases, success criteria).
- For hardware-touching changes: specify which GPIO, UART, or peripheral is affected and confirm the pin is traced/confirmed before writing code.

### 2. Subagent Strategy (Default Tool for Complexity)
- Subagents are the **default choice** for exploration, research, and parallel analysis — not a last resort.
- Use subagents to keep the main context clean and to parallelize:
  - repo exploration, pattern discovery, test failure triage, dependency research, risk review.
  - ESP-IDF API lookups, PlatformIO config questions, FreeRTOS behavior verification.
  - Cross-referencing dome fork (`mattiasbrandt/AstroPixelsPlus`) implementation details.
- For complex problems: throw more subagent compute at it rather than reasoning through everything in the main context.
- Give each subagent **one focused objective** and a concrete deliverable:
  - "Find where `parse_dome_rx()` handles the `:SE` prefix and list all queue destinations" beats "look around."
- Merge subagent outputs into a short, actionable synthesis before coding.

### 3. Incremental Delivery (Reduce Risk)
- Prefer **thin vertical slices** over big-bang changes.
- Land work in small, verifiable increments:
  - implement → test → verify → then expand.
- Follow the phased roadmap (Phases 0–5). Do not jump ahead — each phase depends on the previous one's verification.
- When feasible, keep changes behind:
  - build flags (`-DPA_ENABLE_*`), NVS config switches, or safe defaults.
- For drive/failsafe changes: verify on hardware with transmitter before marking done.

### 4. Self-Improvement Loop
- After any user correction or a discovered mistake:
  - add a new entry to `tasks/lessons.md` capturing:
    - the failure mode, the detection signal, and a prevention rule.
- Review `tasks/lessons.md` at session start and before major refactors.
- Common protoArtoo-specific pitfalls to watch for:
  - ADC2 + WiFi conflict (use ADC1 only for battery monitoring)
  - GPIO 15 is a strapping pin — boot behavior with SBUS receiver attached
  - `initAsyncWeb()` must only be called from WiFi event callback
  - `portMUX` critical sections required for all cross-core `RobotState` access
  - DY-SV5W command bytes are `AA xx ... AB` — not ASCII

### 5. Verification Before "Done"
- Never mark complete without evidence:
  - `pio run -e protoArtoo` compiles cleanly with `-Werror`
  - `pio test -e native` passes for all logic tests
  - `pio check` has no high/medium findings
  - On-device verification for hardware-touching changes
- For failsafe changes: test all 5 layers independently and document which were exercised.
- Compare behavior baseline vs changed behavior when relevant.
- Ask: "Would a staff engineer approve this diff and the verification story? Would they trust this to not injure someone?"

### 6. Demand Elegance (Balanced)
- For non-trivial changes, pause and ask:
  - "Is there a simpler structure with fewer moving parts?"
- If the fix is hacky, rewrite it the elegant way **if** it does not expand scope materially.
- Do not over-engineer simple fixes; keep momentum and clarity.
- Embedded-specific: prefer static allocation, fixed-size buffers, and explicit state over dynamic/generic patterns.

### 7. Autonomous Bug Fixing (With Guardrails)
- When given a bug report:
  - reproduce → isolate root cause → fix → add regression coverage → verify.
- For hardware bugs: clarify whether the issue is reproducible on-device or only in logic tests before diving in.
- Do not offload debugging work to the user unless truly blocked.
- If blocked, ask for **one** missing detail with a recommended default and explain what changes based on the answer.

---

## Task Management (File-Based, Auditable)

1. **Plan First**
   - Write a checklist to `tasks/todo.md` for any non-trivial work.
   - Include "Verify" tasks explicitly (`pio run`, `pio test -e native`, `pio check`, on-device test).
2. **Verify Plan**
   - Add acceptance criteria (what must be true when done).
   - For safety-impacting tasks: check in with the user before starting implementation. Confirm scope, approach, and any risky assumptions before writing code.
3. **Track Progress**
   - Mark items complete as you go; keep one "in progress" item at a time.
4. **Checkpoint Notes**
   - Capture discoveries, decisions, and constraints as you learn them.
   - Hardware discoveries (GPIO traces, UART behavior, voltage levels) go in `docs/pin_map.md`.
5. **Document Results**
   - Add a short "Results" section: what changed, where, how verified.
6. **Capture Lessons**
   - Update `tasks/lessons.md` after corrections or postmortems.

---

## Communication Guidelines (User-Facing)

### 1. Be Concise, High-Signal
- Lead with outcome and impact, not process.
- Reference concrete artifacts:
  - file paths, command names, error messages, GPIO numbers, and what changed.
- Avoid dumping large logs; summarize and point to where evidence lives.

### 2. Ask Questions to Reduce Ambiguity
- **Always use interactive multi-choice selection** when facing uncertain details, context gaps, or design decisions. Present options as a structured pick-list (lettered or numbered with clear labels) so the user can reply with a single choice — do not dump a flat list of open questions and wait for free-form answers.
- Format example:
  ```
  How should CH8-at-zero behave?
    A) Speed-limit dial only (default — simplest) ← recommended
    B) Binary mode-lock (Stationary vs Drive mode)
    C) Disable CH8 entirely — always full speed range
  ```
- Batch related choices into **one** multi-choice exchange; don't ask one then come back for another.
- For minor/inferable details: state the assumption and proceed rather than asking.
- For non-trivial decisions: always offer concrete options **with tradeoffs and a recommended default** — never pose bare open-ended questions.
- For hardware questions (e.g., "which GPIO for this peripheral?"): **never guess** — ask or flag as TBD.

### 3. State Assumptions and Constraints
- If you inferred requirements, list them briefly.
- If you could not run verification, say why and how to verify.
- If a GPIO pin is still TBD from Phase 0: state this explicitly and use `TBD` placeholder that will cause a compile error.

### 4. Show the Verification Story
- Always include:
  - what you ran (`pio run`, `pio test -e native`, `pio check`), and the outcome.
- For hardware-dependent changes: state what on-device test is needed and provide the steps.
- If you didn't run something, give a minimal command list the user can run.

### 5. Avoid "Busywork Updates"
- Don't narrate every step.
- Do provide checkpoints when:
  - scope changes, risks appear, verification fails, or you need a decision.
- Do provide a high-level summary at the end of each major implementation step:
  - what was done, what changed, and what comes next.

---

## Context Management Strategies (Don't Drown the Session)

### 1. Read Before Write
- Before editing:
  - locate the authoritative source of truth (existing module/pattern/tests).
  - Check `tasks/goal.md` for the canonical specification of the feature.
- Prefer small, local reads (targeted files) over scanning the whole repo.

### 2. Keep a Working Memory
- Maintain a short running "Working Notes" section in `tasks/todo.md`:
  - key constraints, invariants, decisions, and discovered pitfalls.
- When context gets large:
  - compress into a brief summary and discard raw noise.
- Key reference files to consult:
  - `tasks/goal.md` — full firmware plan and protocol specs
  - `body_dome_serial_link_spec.md` — dome link protocol details
  - `body_dome_serial_link_astropixel_implementation.md` — dome fork implementation notes
  - `include/config.h` — GPIO assignments (source of truth for pin numbers)
  - `include/robot_state.h` — shared state struct (source of truth for all state fields)

### 3. Minimize Cognitive Load in Code
- Prefer explicit names and direct control flow.
- Follow the project's commenting standard (3-level depth: block purpose → design rationale → implementation detail).
- Every `.cpp` and `.h` file gets a file header comment.
- Every non-trivial function gets a function header comment.
- Leave code easier to read than you found it.

### 4. Control Scope Creep
- If a change reveals deeper issues:
  - fix only what is necessary for correctness/safety.
  - log follow-ups as TODOs/issues rather than expanding the current task.
- Respect the phased roadmap — do not pull Phase 4 work into Phase 2.

---

## Error Handling and Recovery Patterns

### 1. "Stop-the-Line" Rule
If anything unexpected happens (test failures, build errors, behavior regressions, **hardware anomalies**):
- stop adding features
- preserve evidence (error output, repro steps, serial monitor logs)
- return to diagnosis and re-plan

### 2. Research Before Retrying
- If a fix attempt fails: **do not immediately try a variation of the same fix**.
- Stop and research first: read the full error message, check ESP-IDF docs, read relevant source code, look for known issues.
- Form a clear hypothesis for *why* the previous attempt failed before trying again.
- After 2 failed attempts with no new information: mandatory research pause — subagent or web search before any further attempts.
- Trial-and-error loops are a failure mode, not a debugging strategy.
- For ESP32-specific issues: consult the ESP-IDF Programming Guide and the ESP32 Arduino core issues tracker.

### 3. Triage Checklist (Use in Order)
1. **Reproduce** reliably (test, script, or minimal steps — `pio test -e native` for logic, serial monitor for hardware).
2. **Localize** the failure (which layer: SBUS decode, drive task, web API, dome serial, audio driver, FreeRTOS scheduling).
3. **Reduce** to a minimal failing case (smaller input, fewer steps, single task in isolation).
4. **Fix** root cause (not symptoms).
5. **Guard** with regression coverage (add to `test/test_native/` for logic, `test/test_embedded/` for hardware).
6. **Verify** end-to-end for the original report.

### 4. Safe Fallbacks (When Under Time Pressure)
- Prefer "safe default + warning" over partial behavior.
- Degrade gracefully:
  - motors stop (zero frames), not coast on last command.
  - audio fails silently, not with a crash.
  - dome link shows "Not seen" status, not a hang.
- Avoid broad refactors as "fixes."

### 5. Rollback Strategy (When Risk Is High)
- Keep changes reversible:
  - build flags (`-DPA_ENABLE_*`), NVS config switches, or isolated commits.
- For drive/failsafe changes: test on hardware with kill switch before merging to `dev`.

### 6. Instrumentation as a Tool (Not a Crutch)
- Add logging/metrics only when they:
  - materially reduce debugging time, or prevent recurrence.
- Use TAG-prefixed log lines: `[DriveTask] Failsafe active — source: SBUS_TIMEOUT`
- Gate verbose per-frame logging behind build flags (`-DPA_VERBOSE_DRIVE`, etc.).
- Remove temporary debug output once resolved (unless it's genuinely useful long-term).

---

## Engineering Best Practices (protoArtoo Edition)

### 1. API / Interface Discipline
- REST API endpoints follow the canonical definitions in `tasks/goal.md` Section 9.
- All drive commands go through `setDriveCommand()` with `CommandSource` tagging, never written directly to `RobotState`.
- All audio routes through `AudioTask` queue regardless of source (RC, web API, dome serial `$` RX).
- Dome TX commands go through `domeTxQueue`, never written directly to the UART.
- Keep error semantics consistent: API returns JSON `{ "ok": bool, "error": string }`.

### 2. Testing Strategy
- Add the smallest test that would have caught the bug.
- Prefer:
  - `test/test_native/` for pure logic (parsers, frame builders, channel mapping, state machines) — fast, no hardware.
  - `test/test_embedded/` for hardware-dependent (UART loopback, NVS roundtrip, failsafe timing) — slow, requires ESP32.
- Avoid brittle tests tied to incidental implementation details.
- Always add a native test when implementing a new parser function (`parse_dome_rx`, audio track mapping, hoverboard frame checksum).

### 3. Type Safety and Invariants
- Use `static_assert` for compile-time invariants:
  ```cpp
  static_assert(SPEED_LIMIT_MAX <= 1000, "SPEED_LIMIT_MAX exceeds hoverboard range");
  static_assert(SBUS_TIMEOUT_MS >= 100, "SBUS timeout unreasonably short");
  ```
- Use `constrain()` on all external inputs (SBUS channels, web API parameters, dome serial values) before use.
- Use `portMUX` critical sections for all cross-core `RobotState` field access.
- Never suppress compiler warnings — `-Werror` is enforced.

### 4. Dependency Discipline
- Do not add new dependencies unless:
  - the existing stack cannot solve it cleanly, and the benefit is clear.
- Pinned library versions in `platformio.ini` — no `^` or `~` ranges.
- **No Reeltwo on the body.** It belongs in the dome only.
- Prefer Arduino standard libraries (`Preferences`, `LittleFS`, `ArduinoOTA`) over third-party alternatives.
- Current approved dependencies: `ESPAsyncWebServer@3.6.0`, `AsyncTCP@3.3.2`, `ArduinoJson`, `ESP32Servo`.

### 5. Security and Privacy
- Never introduce WiFi credentials into committed code — use `src/secrets.h` (gitignored).
- AsyncWebServer input validation:
  - Validate and constrain all JSON fields from `POST` bodies before use.
  - Clamp `speed`/`steer` to `[-SPEED_LIMIT_MAX, SPEED_LIMIT_MAX]` server-side.
  - Reject unknown fields or unexpected types with a 400 response.
  - Sanitize any user-provided strings before reflecting in HTML (XSS prevention for web UI).
- NVS keys are write-validated: reject values outside documented ranges (e.g., volume 0–30, timeout ≥ 100 ms).
- WiFi AP uses a password — never run an open AP.
- OTA updates should be gated (at minimum: only from AP network, or with a shared secret).

### 6. Performance (Pragmatic)
- Avoid premature optimization.
- Do fix:
  - blocking calls in real-time tasks (Core 1) — use non-blocking queue sends (`timeout 0`).
  - heap allocation inside task loops — use static buffers.
  - serial port saturation from verbose logging — gate behind build flags.
- Never use `delay()` in FreeRTOS tasks — use `vTaskDelay(pdMS_TO_TICKS(ms))`.
- Measure stack high-water marks with `uxTaskGetStackHighWaterMark()` during development.

### 7. FreeRTOS Rules (Embedded-Specific)
- **Core assignment**: Core 1 = real-time (SBUS, Drive, DomeLink, Audio, Servo); Core 0 = WiFi, web, OTA.
- **Shared state**: ALL reads AND writes to `RobotState` fields use `portMUX` critical sections.
- **Queues**: Use timeout 0 for non-blocking sends from real-time tasks. Never use `portMAX_DELAY` in a control loop.
- **No dynamic allocation after setup()**: All buffers static. No `new`/`malloc` in task loops.
- **Stack sizing**: Measure actual high-water mark; add 25% headroom.
- **TWDT**: Only `DriveTask` is registered. If it hangs, chip resets. On reboot: `estop = true`.

### 8. Hardware Interaction Rules
- **GPIO pins**: Only use values from `include/config.h`. If a pin is `TBD`, leave it as `TBD` — it must cause a compile error, not a silent wrong-pin bug.
- **UART writes**: Only the owning task writes to each UART (DriveTask → UART1, DomeLinkTask → UART2, AudioTask → SoftSerial). No other task touches them.
- **PWM**: LEDC channels 0–2 assigned in `config.h`. Servos at 50 Hz.
- **Boot sequence**: Follow the exact order in `tasks/goal.md` Section 7.6. Hoverboard UART before WiFi. Zero frames immediately after UART1 init.

### 9. AsyncWebServer Rules
- `initAsyncWeb()` only from WiFi event callback — never directly in `setup()`.
- Handlers execute on Core 0 — never touch hardware (UART, GPIO, LEDC) directly. Post to queues.
- Use `ArduinoJson` for all JSON serialization/deserialization — no manual string building.
- LittleFS for web UI assets in `data/` directory.
- SSE for real-time dashboard updates — do not poll `/api/status` from the browser.

### 10. Audio Module Rules
- `AudioDriver` is an abstract interface — code against the interface, not the DY-SV5W implementation details.
- Track numbers from `$nnn` commands map directly to SD card files (`001.mp3`, `002.mp3`, etc.).
- `$S` = play random track from configured range (`cfg_sndRandMin`–`cfg_sndRandMax`).
- DY-SV5W command frame: `AA 02 [hi] [lo] AB` — these are raw bytes, not ASCII.
- Volume range 0–30 matches hardware native range.

---

## Commit and Change Hygiene

### Conventional Commits (Required)

Every commit uses the format:
```
<type>(<scope>): <short description — imperative, lowercase, no period>
```

**Types:** `feat`, `fix`, `refactor`, `test`, `docs`, `chore`, `style`, `perf`

**Scopes:** `drive`, `sbus`, `failsafe`, `dome`, `audio`, `servo`, `web`, `nvs`, `wifi`, `hw`, `plan`, `test`, `ci`

**What never goes in a commit:**
- WiFi credentials
- `config.h` with `TBD` replaced by guesses
- Code that doesn't compile
- A `//TODO` that disables safety logic
- Messages like `"fix stuff"`, `"wip"`, `"update"`

### Branch Strategy
- `main` — always releasable; tagged at every version
- `dev` — integration branch; phase work merges here first
- Feature branches: `feature/<phase>-<what>`
- Fix branches: `fix/<what>`

### Semantic Versioning
- `PATCH`: bug fix / safety correction
- `MINOR`: new working feature
- `MAJOR`: breaking change (GPIO pin change, API break, NVS key rename)

---

## Definition of Done (DoD)

A task is done when:
- Behavior matches acceptance criteria.
- `pio run -e protoArtoo` compiles cleanly with `-Werror`.
- `pio test -e native` passes for all affected logic tests.
- `pio check` has no high/medium findings.
- On-device tests pass for hardware-touching changes.
- Safety invariants are preserved (zero-frame rule, SPEED_LIMIT_MAX cap, estop latching, SBUS boot default).
- The code follows existing conventions (commenting standard, `portMUX`, queue patterns, TAG logging).
- A short verification story exists: "what changed + how we know it works."
- For failsafe changes: state which of the 5 layers were tested and how.

---

## Templates

### Plan Template (Paste into `tasks/todo.md`)
- [ ] Restate goal + acceptance criteria
- [ ] Locate existing implementation / patterns
- [ ] Check `tasks/goal.md` for canonical spec of the feature
- [ ] Design: minimal approach + key decisions
- [ ] Confirm GPIO pins are traced/confirmed (not TBD) if hardware-touching
- [ ] Implement smallest safe slice
- [ ] Add/adjust tests (`test/test_native/` for logic, `test/test_embedded/` for hardware)
- [ ] Run verification (`pio run`, `pio test -e native`, `pio check`)
- [ ] On-device verification if hardware-touching
- [ ] Verify safety invariants preserved
- [ ] Summarize changes + verification story
- [ ] Record lessons (if any) in `tasks/lessons.md`

### Bugfix Template (Use for Reports)
- Repro steps (serial monitor output, SBUS state, web API call):
- Expected vs actual:
- Root cause:
- Fix:
- Regression coverage (native test added?):
- Verification performed (`pio test -e native`, on-device?):
- Safety impact (does this touch drive/failsafe/estop?):
- Risk/rollback notes:
