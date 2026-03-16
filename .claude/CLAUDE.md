# AI Coding Agent Guidelines — protoArtoo (CLAUDE.md)

> Adapter file for Claude-oriented workflows.
> Canonical cross-agent instructions live in `AGENTS.md` at repository root.
> If a rule here conflicts with `AGENTS.md`, follow `AGENTS.md` unless the user explicitly overrides.

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
- **RC input:** Runtime-selectable receiver modes:
  - `standard_pwm` (CH1-CH6 PWM)
  - `single_sbus` (SBUS on GPIO 15)
  - `dual_sbus` (SBUS on GPIO 15 + GPIO 13)
- **RC mapping:** All mode-specific bindings/calibration are NVS-backed and
  must be configurable from the webpage (Setup + RC diagnostics surfaces)
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

See `AGENTS.md` § "Safety Invariants" for the canonical 6-rule list.
All rules apply unconditionally to every code change in this repository.

---

## System Configuration

### Elevated Commands (sudo)
- Always use `sudo -A` instead of `sudo` for commands requiring elevated privileges.
- The system has `SUDO_ASKPASS` configured with a GUI askpass helper.

### ESP32 Upload and Monitoring

> ⚠️ **Seated-PCB USB upload fails.** When the ESP32 is seated in the Artoo
> Controller PCB, the SBUS receiver on GPIO 15 (`PIN_SBUS1_RX`, a strapping pin)
> can prevent the bootloader from entering download mode. **USB upload only works
> with the ESP32 removed from the PCB socket.** Use OTA for all normal in-PCB flashing.
> Full write-up: `tasks/lessons.md`, `docs/pin_map.md`.

- Default USB upload port (ESP32 unseated): `/dev/ttyUSB0` — never use `/dev/ttyS0`.
- USB flash: `pio run -e protoArtoo --target upload --upload-port /dev/ttyUSB0`
- **OTA firmware (preferred in-PCB path):** `pio run -e protoArtoo_ota --target upload`
- **OTA filesystem:** `pio run -e protoArtoo_ota --target uploadfs`
- `protoArtoo_ota` defaults to `upload_port = 10.0.0.22` (STA client IP); override with `--upload-port <ip>`
- Do **not** use `192.168.4.1` (AP IP) as the default OTA target
- ArduinoOTA starts on Core 0 when WiFi comes up (port 3232)
- Web UI OTA: firmware via `POST /upload/firmware`, filesystem via `POST /upload/filesystem` (`/firmware.html`)

**Serial monitor — always use `scripts/serial_monitor.py`, never craft ad-hoc pyserial snippets.**
- The script holds DTR/RTS low so connecting does NOT reset the ESP32.
- Output goes to stdout; status/errors go to stderr. Exit code 1 on failure.
```bash
# Capture 10s (default), print to stdout:
python3 scripts/serial_monitor.py

# Capture longer on a specific port:
python3 scripts/serial_monitor.py --port /dev/ttyUSB1 --duration 30

# Wait for a known boot message, then exit (preferred for verification tasks):
python3 scripts/serial_monitor.py --until "setup complete" --timeout 20

# Stream continuously (human monitoring only — not for agents):
python3 scripts/serial_monitor.py --stream
```

### Build Commands (Quick Reference)
```bash
pio run -e protoArtoo           # compile firmware
pio run -e protoArtoo -t upload # USB flash (ESP32 unseated from PCB, /dev/ttyUSB0)
pio run -e protoArtoo_ota -t upload    # OTA firmware (in-PCB, 10.0.0.22)
pio run -e protoArtoo_ota -t uploadfs  # OTA filesystem/LittleFS (in-PCB, 10.0.0.22)
pio test -e native              # fast logic tests (no hardware)
pio test -e protoArtoo          # on-device tests (requires ESP32)
pio check                       # static analysis (cppcheck)
```

---

## Workflow Orchestration

### 1. Plan Mode Default
- Enter plan mode for any non-trivial task (3+ steps, multi-file change, architectural decision, safety-impacting behavior).
- Include verification steps in the plan (not as an afterthought).
- If anything goes sideways or new information invalidates the plan: **stop immediately**, update the plan, then continue — do not keep pushing.
- Write a crisp spec first when requirements are ambiguous (inputs/outputs, edge cases, success criteria).
- For hardware-touching changes: specify which GPIO, UART, or peripheral is affected and confirm the pin is traced/confirmed before writing code.
- **Authoritative plan files live in `tasks/`**. Use `tasks/status.md`, `tasks/phase*-tasks.md`, and `docs/goal.md` as the planning source of truth for this repository.
- Also consult phase-specific companion contracts/specs when present
  (for example `tasks/rc_diagnostics_contract.md` for Phase 3 RC diagnostics/mapping).
- Treat `.sisyphus/plans/` as internal agent scratch/history only — do not rely on it as the authoritative project plan when repo-local `tasks/` files exist.

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
- Follow the stage labels used in phase plans and status updates:
  - `bench-tested`
  - `partial`
  - `full-hardware-required`
- If full-hardware checks are deferred, record blockers and explicit closure steps
  in planning/status docs.

Sections 3.1–3.4 (task packets, trust-but-verify, parallelization, working memory)
are defined in `AGENTS.md` § "Execution Model". Follow those rules as-is.

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
- For every feature, test, and finding, explicitly classify verification as one of:
  - `bench-tested` — can be verified with the ESP32 alone over USB/WiFi at the current bench stage
  - `full-hardware-required` — needs the Artoo PCB/peripherals connected and powered
  - `partial` — bench-stage checks are useful, but final verification still needs full hardware
- Never present bench-stage verification as equivalent to full hardware validation.
- When hardware is disconnected or unpowered, explicitly state what the current setup does and does not prove.
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
  - Keep instruction/doc updates aligned with current `docs/goal.md` +
    `tasks/phase*-tasks.md` wording and constraints.
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
  - If verification is not full-hardware, mark result as bench/partial and
    note remaining hardware-required checks.
6. **Capture Lessons**
   - Update `tasks/lessons.md` after corrections or postmortems.

---

## Communication Guidelines (User-Facing)

### 1. Be Concise, High-Signal
- Lead with outcome and impact, not process.
- Reference concrete artifacts:
  - file paths, command names, error messages, GPIO numbers, and what changed.
- Avoid dumping large logs; summarize and point to where evidence lives.

### 2. Reduce Ambiguity via Intent Disambiguation
- **Always employ intent disambiguation through interactive multi-choice clarification questions** when encountering uncertain details, context gaps, or design decisions. Present options as a structured, numbered or lettered list with concise labels, descriptions, tradeoffs, and a recommended default (marked as such). This allows the user to respond with a single selection (e.g., "A" or "2")—avoid scattering multiple open-ended questions or requiring free-form elaboration.
- Format example:

  How should CH8-at-zero behave?

  A) Speed-limit dial only (default—simplest implementation, minimal code changes; recommended for quick setup)

  B) Binary mode-lock (Stationary vs. Drive mode—adds flexibility but increases complexity and potential bugs)

  C) Disable CH8 entirely (always full speed range—removes feature but ensures reliability; use if hardware constraints apply)

- Consolidate related ambiguities into a **single multi-choice query** for efficiency; do not fragment into sequential follow-ups.
- For minor or easily inferable details: explicitly state your assumption and proceed without querying.
- For non-trivial decisions: provide 3-5 concrete, mutually exclusive options with brief pros/cons—never use vague, open-ended prompts like "What do you mean?" or "Tell me more."
- For hardware-specific queries (e.g., "Which GPIO for this peripheral?"): **never assume or guess**—either pose a multi-choice list of viable options or flag as "TBD pending user input."

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

### 6. Web/UI Wording Discipline
- Operator-facing UI copy must avoid internal planning language (phase names,
  roadmap/TODO terminology, internal implementation notes).
- Prefer clear device-state and control language consistent with `tasks/phase2-tasks.md`
  and `tasks/phase5-tasks.md`.

### 7. Non-Blocking Interaction Style
- Prefer progress-through-action over repeated planning chatter.
- Ask questions only when the answer changes architecture, safety, or acceptance criteria.
- When blocked, ask one focused multi-choice question with a recommended default.

---

## Context Management Strategies (Don't Drown the Session)

### 1. Read Before Write
- Before editing:
  - locate the authoritative source of truth (existing module/pattern/tests).
  - Check `docs/goal.md` for the canonical specification of the feature.
- Prefer small, local reads (targeted files) over scanning the whole repo.

### 2. Keep a Working Memory
- Maintain a short running "Working Notes" section in `tasks/todo.md`:
  - key constraints, invariants, decisions, and discovered pitfalls.
- When context gets large:
  - compress into a brief summary and discard raw noise.
- Key reference files to consult:
  - `docs/goal.md` — full firmware specification and protocol specs
  - `tasks/body_dome_serial_link_spec.md` — dome link protocol details
  - `tasks/body_dome_serial_link_astropixel_implementation.md` — dome fork implementation notes
  - `include/config.h` — GPIO assignments (source of truth for pin numbers)
  - `include/robot_state.h` — shared state struct (source of truth for all state fields)

### 3. Minimize Cognitive Load in Code
- Prefer explicit names and direct control flow.
- Follow the project's commenting standard (3-level depth: block purpose → design rationale → implementation detail).
- Every `.cpp` and `.h` file gets a file header comment.
- Every non-trivial function gets a function header comment.
- Preserve useful existing comments by default. Do not remove comments just to
  reduce verbosity or make diffs smaller.
- Only remove/update comments when they are factually incorrect, stale after
  code changes, or superseded by clearer nearby documentation.
- When fixing LSP/lint warnings, change the specific semantic code the warning
  points to; do not remove nearby comments as a workaround.
- Leave code easier to read than you found it.

### 4. Control Scope Creep
- If a change reveals deeper issues:
  - fix only what is necessary for correctness/safety.
  - log follow-ups as TODOs/issues rather than expanding the current task.
- Respect the phased roadmap — do not pull Phase 4 work into Phase 2.
- Extend existing setup/config/status/dashboard surfaces instead of adding
  parallel pages for the same control domain.

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
- REST API endpoints follow the canonical definitions in `docs/goal.md` Section 9.
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
- **UART writes**: Only the owning task writes to each UART (DriveTask → UART1, DomeLinkTask → UART2, AudioTask → Serial2 pins for DY-SV5W). No other task touches them.
- **PWM**: LEDC channels 0–2 assigned in `config.h`. Servos at 50 Hz.
- **Boot sequence**: Follow the exact order in `docs/goal.md` Section 7.6. Hoverboard UART before WiFi. Zero frames immediately after UART1 init.

### 9. AsyncWebServer Rules
- `initAsyncWeb()` only from WiFi event callback — never directly in `setup()`.
- Handlers execute on Core 0 — never touch hardware (UART, GPIO, LEDC) directly. Post to queues.
- Use `ArduinoJson` for all JSON serialization/deserialization — no manual string building.
- LittleFS for web UI assets in `data/` directory.
- SSE for real-time dashboard updates — do not poll `/api/status` from the browser.
- For Phase 3 RC diagnostics/mapping work, align payloads and update cadence with
  `tasks/rc_diagnostics_contract.md` (`GET /api/rc` + `event: rc`).

### 10. Audio Module Rules
- `AudioDriver` is an abstract interface — code against the interface, not the DY-SV5W implementation details.
- Track numbers from `$nnn` commands map directly to SD card files (`001.mp3`, `002.mp3`, etc.).
- `$S` = play random track from configured range (`cfg_sndRandMin`–`cfg_sndRandMax`).
- DY-SV5W command frame: `AA 02 [hi] [lo] AB` — these are raw bytes, not ASCII.
- Volume range 0–30 matches hardware native range.

### 11. RC Mapping + UX Rules
- Maintain default mapping intent parity between `single_sbus` and `standard_pwm`
  as defined in `docs/goal.md` Section 6.5 and `tasks/phase3-tasks.md`.
- Preserve `dual_sbus` split default: receiver #1 drive/speed-limit, receiver #2 dome;
  remaining receiver #2 channels configurable.
- All remap/calibration flows must be editable from webpage and persisted in NVS.
- Mapping editor UX must stay modern/responsive and provide:
  - source badges (`PWM`, `SBUS#1`, `SBUS#2`)
  - inline validation feedback
  - live raw/normalized/mapped preview
  - explicit apply/save success/error feedback

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
- Co-authored-by trailers, attribution footers, or additional author identities unless the user explicitly requested them for that specific commit

### Branch Strategy

- `main` — stable, released state only; updated at phase completion via PM-approved non-fast-forward merge
- `phase/vX.Y.Z` — all work for the active phase (e.g. `phase/v0.4.0`); one active phase at a time
- `exp/<topic>` — disposable experiments; never merged directly to `main`
- `dev`, `feature/<phase>-<what>`, and `fix/<what>` branches are retired as of Phase v0.4.0

Commit scope format (required for all commits in a phase branch):

```
type(phase:vX.Y.Z/T<NN>): summary
```

- `T<NN>` matches the task number in the phase plan (zero-padded: `T01`, `T02`, ...)
- `T00` = phase scaffolding / admin commits
- Slice notation: `type(phase:vX.Y.Z/T<NN>/slice:a): summary`
- Example: `feat(phase:v0.4.0/T03): add AudioTask queue and DY-SV5W driver`

See `tasks/dev-workflow-change-spec.md` for the full workflow specification.

### Semantic Versioning

Aligned with [Conventional Commits](https://www.conventionalcommits.org) and [SemVer](https://semver.org):

- `PATCH` (x.x.+1): `fix` commits — bug fix / safety correction
- `MINOR` (x.+1.0): `feat` commits — new working feature
- `MAJOR` (+1.0.0): `feat!` / `BREAKING CHANGE:` — GPIO pin change, API break, NVS key rename

---

## Definition of Done (DoD)

See `AGENTS.md` § "Verification and Reporting" for the core DoD checklist
(build, test, static analysis, verification classification).

Claude-specific additions:
- For failsafe changes: state which of the 5 layers were tested and how.
- The code follows existing conventions (commenting standard, `portMUX`, queue patterns, TAG logging).
- Ask: "Would a staff engineer approve this diff and the verification story?
  Would they trust this to not injure someone?"

---

## Templates

### Plan Template (Paste into `tasks/todo.md`)
- [ ] Restate goal + acceptance criteria
- [ ] Locate existing implementation / patterns
- [ ] Check `docs/goal.md` for canonical spec of the feature
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
