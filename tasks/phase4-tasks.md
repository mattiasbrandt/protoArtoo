# Phase 4 - Audio + Full Dome Link (v0.4.0)

Status: Pending Phase 3 completion and validation
Baseline: Phase 2 web/status/config/dashboard stack already exists; Phase 3 should add physical actuation surfaces on top of it
Goal: Audio system and bidirectional dome communication
Milestone: Dome health shows body present; dome sequences trigger body audio + arms; body RC triggers dome animation

## Starting Point from Phases 0-3

- Hardware ground truth remains `docs/pin_map.md` and `include/config.h`
- Bidirectional dome/body architecture is already defined in `tasks/goal.md`
- Dome-side AstroPixelsPlus fork work is complete and should be treated as the integration partner baseline
- Current dashboard/log/status surfaces should be extended for body-link and audio visibility instead of replaced
- USB debug serial and bench-stage web validation are now good enough to support protocol bring-up work

## Execution Stages (Standard Planning Model)

1. Bench Development Stage
- Validate parser/dispatcher logic, API shape, and dashboard/log visibility on bench hardware.

2. Full Hardware Validation Stage
- Validate actual audio output, dome heartbeat, body/dome coordination, and slip-ring
  serial reliability on fully connected hardware.
- Body-link status should be surfaced through the existing dashboard/status model,
  not a separate hidden channel.

3. Deferred Hardware Validation (Allowed)
- If full integration hardware is unavailable, bench scope may proceed, but missing
  physical checks must be recorded as deferred with blockers and closure steps.
- Deferred checks remain release blockers for claims of full hardware readiness.

## Workflow (Phase v0.4.0)

This phase follows the workflow defined in `tasks/dev-workflow-change-spec.md`,
which is effective from v0.4.0 onward. It does **not** apply retroactively to
Phase 0–3 work.

- Use one dedicated integration branch: `phase/v0.4.0`
- All work (features, fixes, docs, chores) lands directly as commits on `phase/v0.4.0` — no sub-branches
- Commit scope format: `type(phase:v0.4.0/T<NN>): summary`
  - `T00` = phase scaffolding / admin commits
  - `T01` = Task 4.1, `T02` = Task 4.2, etc.
  - Example: `feat(phase:v0.4.0/T01): implement AudioDriver interface and DY-SV5W driver`
  - Slice notation: `feat(phase:v0.4.0/T03/slice:a): simplify audio track index mapping`
- Prefer thin-slice commits with verification per slice, not one big batch
- Do not merge `phase/v0.4.0` into `main` until all phase wrap-up criteria are met
- At phase wrap-up: merge to `main` (non-fast-forward), tag `v0.4.0`, publish GitHub release, write changelog entry

### Agent Instruction Alignment (Phase 4 boundary)

The three agent instruction files have been updated as part of v0.4.0 kickoff
to reflect the phase-branch workflow (`tasks/dev-workflow-change-spec.md`):

- `AGENTS.md` — canonical; includes Git Workflow section with phase branch model and commit scope format
- `.claude/CLAUDE.md` — Branch Strategy updated; `dev` branch and `feature/` branches retired
- `.github/copilot-instructions.md` — Git Workflow section added to match
- `CONTRIBUTING.md` — updated with phase-branch model and external contributor guidance

Agents must treat `tasks/dev-workflow-change-spec.md` as the authoritative
workflow reference for all Phase 4+ development.

## Phase 4 Tasks

- [x] T00 — Phase branch setup and workflow alignment (completed at v0.4.0 kickoff)
  - `phase/v0.4.0` branch created as the integration branch for this phase
  - `AGENTS.md`, `.claude/CLAUDE.md`, `.github/copilot-instructions.md` updated with phase-branch workflow
  - `CONTRIBUTING.md` updated with phase-branch model and external contributor guidance
  - `tasks/dev-workflow-change-spec.md` is the authoritative workflow reference

- [x] T01 — AudioDriver interface + soft UART backend
  - `include/audio_driver.h`, `include/audio_soft_uart.h`, `src/drivers/audio_soft_uart.cpp`
  - Software bit-bang UART TX at 9600 baud on GPIO 26; binary frame protocol
  - Build flag: `PA_AUDIO_DRIVER=AUDIO_SOFT_UART`; AUDIO_CHIRP stub planned in T15
  - **Verification: compile-only** — no native tests for frame builder; no bench run
  - Abstract `AudioDriver` interface — see `docs/goal.md §6.3` for driver code shape;
    build flag `PA_AUDIO_DRIVER` selects implementation
- [x] T02 — Implement AudioTask with multi-source queue
  - route all audio requests through one queue path (RC, web, dome serial `$` RX)
  - enforce volume clamp 0–30 before any driver write
  - AudioTask is the sole writer to the audio serial port (GPIO 26); no other task
    touches `PIN_AUDIO_TX`; queue sends are non-blocking (`timeout 0`) from real-time tasks
  - **Verification:**
    - `parseAudioDollar()`: native-tested (26 tests in `test_audio_dollar`)
    - AudioTask, queue helpers, marcduino_rx `$` routing: compile-only — no bench run

- [x] T03 — Implement Marcduino TX path (body → dome)
  - **PCB hardware:** S3 header (PCB label "Dome Control"), `PIN_DOME_TX` = GPIO 33,
    `PIN_DOME_RX` = GPIO 34 (input-only GPIO); UART2 (Serial2), 9600 baud 8N1
  - DomeLinkTask is the sole writer to UART2 TX; all TX goes through `domeTxQueue`
  - Heartbeat TX: send `#PAHB\r` to dome at 1 Hz (body → dome direction)
  - **Dome-side TX implementation is already complete** — see
    `tasks/body_dome_serial_link_astropixel_implementation.md` for dome contract;
    `tasks/body_dome_serial_link_spec.md §2` for the full TX design spec
  - `sendBodyCommand()` and all `:SE01`–`:SE15` body TX calls done on the dome fork;
    body-side only needs to send heartbeats and forwarded web/RC commands
- [x] T04 — Implement Marcduino RX parser/dispatcher (dome → body)
  - reference documents: `docs/marcduino_commands.md`, `docs/goal.md §2`,
    `tasks/body_dome_serial_link_spec.md §3`
  - **PCB hardware:** same UART2 / GPIO 34 RX as T03 (S3 header, 9600 baud 8N1)
  - CR-terminated ASCII lines; 64-byte static receive buffer; overflow → discard + reset
  - **Prefix routing contract** (body parser, `docs/goal.md §2`):
    - `$...` → AudioTask queue (body plays sound)
    - `:SE30`–`:SE36` → ServoTask queue (body arm sequences)
    - `:SE01`–`:SE16` → AudioTask queue (sound component of full-droid sequence)
    - `:OP` / `:CL` / `:MV` → ServoTask queue (direct arm position)
    - `#...` → ConfigTask queue
    - `*`, `@`, `%`, `&`, `!` → **discard silently** (dome-only, no MarcDuino Slave)
    - `#APHB` → update dome-link heartbeat state (do NOT dispatch further)
  - Routing already partially stubbed in `src/drivers/marcduino_rx.cpp`; AudioTask
    queue is the Phase 4 activation step (currently a stub log)
- [x] T05 — Implement DomeLinkTask with heartbeat handling
  - reference documents: `tasks/body_dome_serial_link_spec.md §3`,
    `tasks/body_dome_serial_link_astropixel_implementation.md`
  - **PCB hardware:** S3 header (PCB label "Dome Control"); UART2 (Serial2)
    - TX: GPIO 33 (`PIN_DOME_TX`) — send heartbeats and forwarded commands
    - RX: GPIO 34 (`PIN_DOME_RX`) — input-only GPIO; receives dome heartbeats + commands
    - Baud: 9600 8N1
  - **Heartbeat protocol** (both directions over same UART2 link):
    - Body → dome: `#PAHB\r` at 1 Hz
    - Dome → body: `#APHB\r` at 1 Hz (sent by AstroPixelsPlus fork — already implemented)
    - Connected = `#APHB` received within last 5 s; Lost = was connected, now >5 s silent;
      Not seen = no heartbeat ever received this boot
  - RX line buffer: 64-byte static; CR-terminated; overflow → discard + reset (no heap)
  - Expose `dome_link` block in `/api/status`: `connected`, `hb_tx`, `hb_rx`, `last_rx_ms`
  - Only DomeLinkTask writes to UART2 TX; all TX enqueued via `domeTxQueue` (non-blocking)
- [x] T06 — Extend dashboard/log/status surfaces for body-link and audio visibility
  - extend existing surfaces; do not create parallel setup/config/debug pages
- [x] T07 — Add audio and dome-link web APIs on top of existing web foundation
- [x] T08 — Implement mood preset dual-path execution
  - **Background:** mood has two independent execution paths (see `docs/goal.md §6.8`).
    The body is the sole audio source; it must apply the audio component of each mood
    directly regardless of dome link state. Dome visual effects are a separate concern
    and require an active dome link.
  - Add `activeMood` field (`uint8_t`) to `RobotState`; NVS key `last_mood`; default 0 (unset)
  - Implement `applyMood(uint8_t se1x)` (or equivalent) that atomically:
    1. Dispatches the body-side audio command directly to AudioTask queue:
       - `:SE10` → `$s` (stop chatter)
       - `:SE11`, `:SE13`, `:SE14` → `$R` (resume random chatter)
    2. If dome link is active: enqueues `:SE1x\r` to `domeTxQueue`
    3. Writes `robotState.activeMood` under `portMUX`
    4. Persists to NVS (`last_mood`)
  - Call `applyMood()` from:
    - Web API mood button handler (post from `index.html`)
    - Dome RX parser when `:SE10`/`:SE11`/`:SE13`/`:SE14` arrives from dome serial
    - RC `dome_seq` action target (`SE10`–`SE14`)
  - When `applyMood()` is called from dome RX: apply audio locally but do NOT
    echo `:SE1x` back to dome TX (avoid command loop)
  - Expose `activeMood` in `/api/status` JSON for dashboard poll
  - On boot: restore last mood from NVS and apply audio component only
    (dome link is not yet established at boot; no TX on boot)
  - Native tests: `test_mood_audio_dispatch` — verify correct `$s`/`$R` selection
    for each mood index; verify no dome TX enqueue when dome link flag is off

- [ ] T09 — Add parser/track mapping tests and hardware validation plan
  - include unit tests for `$` command mapping and SBUS-agnostic audio dispatch logic
  - include hardware checklist for active backend: SD card layout, stop/volume
    behaviour, and command byte verification against installed module

- [x] T09 — Parser/track mapping tests and hardware validation plan
  - `parseAudioDollar()`: 26 native tests — full $ command set covered
  - `moodAudioCommand()`: 12 native tests + 5 integration tests vs dollar parser
  - `moodIdFromSeCommand()`: 12 native tests — all :SE cases including non-mood body seqs
  - Soft UART frame bytes: 10 native tests — play/stop/volume frame structure
  - Total Phase 4 native tests: 53 (389→401 overall)
  - **Named command audit (2026-03-18):** Cross-referenced `$` command set against
    MarcDuino, BetterDuino, Reeltwo, SHADOW_MD, Padawan360. One gap identified:
    - `$D` (Disco, standard track 206) — **intentionally omitted.** SE09 triggers
      this from the dome. Non-Star Wars music; not in character for this build.
      Currently a silent no-op (parser returns AUDIO_ACTION_NONE). Documented in
      `docs/sound_playback.md` as a future optional addition.
    - All SE sequences audited: SE00–SE16, SE30–SE36, SE51–SE59 routing is
      correct. SE17–SE19 are SHADOW_MD-only extensions, not in AstroPixelsPlus,
      discarded correctly. No other gaps found.
  - **Hardware validation checklist (T10):**

    Audio (AUDIO_SOFT_UART backend — DY-SV5W or compatible):
    - [x] `POST /api/audio action=play&track=1` → sound plays from speaker
    - [x] `POST /api/audio action=stop` → playback stops
    - [x] `POST /api/audio action=volume&level=5` → audible volume change
    - [x] `POST /api/audio action=dollar&cmd=$S` → scream track plays (track 126)
    - [x] `POST /api/mood mood=10` → random chatter stops (`$s`)
    - [x] `POST /api/mood mood=14` → random chatter starts (`$R`)
    - [x] Random playback fires autonomously every ~10 s in mood=14
    - [ ] Disabling S2 Sound in Setup page stops random playback within 500 ms
        - [ ] Re-enabling S2 Sound starts driver init and accepts commands again
        - [x] SD card layout: FAT32 root directory, zero-padded `NNN.mp3` files with
          contiguous numbering and no gaps (`001.mp3` ... `N.mp3`)
        - [x] Gap check: track 177 (Star Wars) was silent until SD numbering aligned;
          confirmed DY-SV5W misaligns track addressing when numbers are skipped —
          all tracks under test play correctly with contiguous numbering

    Dome serial link (requires AstroPixelsPlus board connected via slip ring):
    - [ ] `#PAHB` heartbeat visible on dome serial monitor at 1 Hz
    - [ ] `#APHB` heartbeat received from dome → `dome_link.state = "connected"` in `/api/status`
    - [ ] `POST /api/mood mood=14` with dome connected → `:SE14` sent to dome over UART2
    - [ ] Dome sends `:SE10` body command → body applies mood (audio stops, no echo back to dome)
    - [ ] Dome sends `$S` → body plays scream sound via AudioTask

    Boot restoration:
    - [ ] Apply mood=11, power cycle → reboot logs `boot mood restore: SE11 -> $R`; random chatter resumes

- [ ] T10 — Full-hardware validation pass (or formal deferral)
  - note: validation applies to whichever backend (`PA_AUDIO_DRIVER`) is active at
    test time; repeat for each backend before marking that backend as validated
  - **Status update (2026-03-17): reopened as in-progress after on-droid DY-SV5W bring-up**
  - **What was validated on hardware:**
    - DY-SV5W is physically wired and powered; UART mode switch confirmed (CON3=ON, CON2/CON1=OFF)
    - TX/RX loopback on S2 (GPIO26↔GPIO35) confirmed ESP32 UART path and frame emission
    - Audible output was achieved on real hardware after wiring/protocol recovery
  - **Operator-reported regressions found during sound-page testing:**
    1. Volume level appeared to reset to default
    2. Named sounds / direct track play behaved randomly or inconsistently
    3. Play/stop behaviour became variant-dependent (including delayed or silent playback in some test builds)
  - **Confirmed fixes during troubleshooting:**
    - `POST /api/audio` volume now persists to NVS (`cfg_audioVolume` + `saveConfigToNvs()`)
    - `GET /api/audio/tracks` now includes `volume`
    - `data/sound.js` now hydrates the slider from persisted `volume` (UI no longer falsely shows 20 after reboot)
    - DY-SV5W SD card numbering requirement documented and applied: root files must
      be contiguous zero-padded `NNN.mp3` with no gaps
    - Specific track playback is confirmed deterministic and aligned to filename
      numbering when the SD sequence is contiguous (no missing indices)
    - Named track playback (`$S/$F/$L/$c/$C/$W/$M/$B`) is confirmed working after
      SD numbering alignment
  - **Protocol/code experiments performed (all hardware-tested, no final sign-off):**
    - Frame dialect trials:
      - end-marker (`AA ... AB`) only
      - checksum (`AA ... CRC`) only
      - dual-wrapper compatibility send (both back-to-back)
    - Command-set trials:
      - legacy/variant opcodes (`play:0x0D/0x06`, `stop:0x03/0x02`, `volume:0x0A/0x13`, `select:0x11/0x10`)
      - DYPlayer/Padawan/BetterDuino-derived opcodes (`play:0x07`, `stop:0x04`, `volume:0x13`)
    - Init-sequence trials:
      - with and without explicit device-select
      - with EQ-normal command (BetterDuino-style)
    - Transport trials:
      - hardware UART2 remap on S2 pins retained
      - soft-UART and mixed variants were tested earlier and superseded
  - **External references reviewed during root-cause analysis:**
    - `docs/sound_playback.md`
    - BetterDuino `MDuinoSound.cpp` (`MDuinoSoundDYPlayer`)
    - AstroPixelsPlus `MarcduinoSound.h`
    - Padawan360 DY-SV5W sketch + bundled `dyplayer-main` library (`DYPlayer.cpp`)
  - **Root cause identified and fixed (2026-03-17):**
    - The driver was sending opcode `0x06` (next track) and `0x0D` (getPlayingSound
      query) instead of `0x07` (playSpecified). Additionally, the dual end-marker +
      checksum frame wrapper produced 4 frames per play command, two of which
      triggered "next track" — explaining the random playback behavior.
    - Stop sent `0x03` (pause) then `0x02` (play/resume), cancelling itself out.
    - The module also needed `delay(100)` between commands (matching BetterDuino
      reference) and a full power cycle to clear confused state from prior
      malformed command sequences.
    - Authoritative command table from DYPlayerArduino library (`DYPlayer.cpp`):
      `0x07`=playSpecified, `0x04`=stop, `0x13`=setVolume, `0x0B`=setPlayingDevice
    - Fix committed: `23957bb` — checksum-only framing, correct opcodes, delay(100)
  - **Anti-spam guard added (`e08b5b5`):**
    - 300 ms minimum interval between successive `playTrack()` calls at the
      AudioTask dispatch level. Rapid-fire commands (web UI clicks, RC bounce)
      are silently dropped. Stop and volume remain unthrottled.
    - Matches BetterDuino approach (delay per command) plus application-level guard
      that no reference project implements but is appropriate for web UI control.
  - **Closure checklist (required before T10 can be checked complete):**
    - [x] Lock one verified command/frame dialect for this exact module revision
    - [x] Verify direct track play determinism (same track request → same audible file, 3/3 repeats confirmed for track 1 and track 2)
    - [x] Verify named sound determinism (`$S/$F/$L/$c/$C/$W/$M/$B`) with
      contiguous no-gap SD numbering (`NNN.mp3` root sequence)
    - [x] Verify stop semantics (stop does not trigger/resume playback)
    - [x] Re-run full T09 hardware checklist items — all audio items confirmed
      `bench-tested`; two items deferred (S2 enable/disable toggle, boot mood
      restore) — require hardware reconnect, tracked below

- [x] T15 — Implement CHIRP Audio Trigger backend (`AUDIO_CHIRP`)
  - **Background:** CHIRP is an RP2350-based multi-stream audio board with an
    ASCII UART command protocol. It is a backend alternative to `AUDIO_SOFT_UART`
    selected by setting `PA_AUDIO_DRIVER=AUDIO_CHIRP` in `platformio.ini`.
    Full CHIRP source: https://github.com/joymonkey/CHIRP
    Protocol reference: `docs/sound_playback.md §2.2`
  - **Files to create:**
    - `include/audio_chirp.h` — `AudioDriverChirp : public AudioDriver`
    - `src/drivers/audio_chirp.cpp` — implementation
  - **Wire protocol:** ASCII text commands, `\n` terminated, over software UART TX
    on GPIO 26 (`PIN_AUDIO_TX`). Same GPIO and soft-uart mechanism as
    `AUDIO_SOFT_UART`; only the command format differs.
  - **Baud rate:** configure CHIRP to 9600 baud via `#BAUD_RATE 9600` in
    `CHIRP.INI` on the SD root. This matches the protoArtoo software UART bit-bang
    rate. Document this as a required setup step.
  - **`AudioDriver` interface mapping:**
    - `begin()` → configure GPIO 26 output, idle HIGH; optionally send `VOL:${boot_vol}\n`
    - `playTrack(n)` → `PLAY:n,1,A\n` (Bank 1, Page A, index n, stream 0)
    - `stop()` → `STOP\n`
    - `setVolume(v)` → `VOL:${(v * 99) / 30}\n` (scale 0–30 → 0–99)
  - **Volume constant:** define `CHIRP_VOL_MAX 99` in the driver; do not hardcode
    the magic number at call sites
  - **Fire-and-forget TX:** no ACK parsing in Phase 4 — responses from CHIRP are
    not read. GPIO 35 (`PIN_AUDIO_RX`) is available for a future RX extension
  - **Soft UART reuse:** `softTxByte()` / `sendAscii()` bit-bang logic is the same
    as `audio_soft_uart.cpp` — extract to a shared internal helper or duplicate
    cleanly; do not pull in `AudioDriverSoftUart` as a dependency
  - **Acceptance criteria:**
    - `pio run -e protoArtoo` with `PA_AUDIO_DRIVER=AUDIO_CHIRP` compiles cleanly
    - `pio test -e native` still passes (driver excluded from native build)
    - Manual bench test: logic analyser or second UART confirms correct ASCII
      frames on GPIO 26 for `playTrack`, `stop`, `setVolume` calls
    - Hardware test: connect CHIRP board (CHIRP.INI set to 9600 baud), trigger
      `$001` from web API → sound plays; `$s` → stops; `$+` / `$-` → volume changes
  - **Verification classification:** `full-hardware-required` — CHIRP board needed
    for final sign-off; bench logic-analyser check is `partial`
  - execute integrated body/dome/audio validation on connected hardware
  - if hardware is unavailable, create deferred validation record with blocker,
    owner, and explicit test checklist for later closure

---

## Phase 3 Carryover — Open Items

The following items were formally deferred at Phase 3 close and are tracked here
so they remain visible inside the active phase. They are preconditions for claiming
the v0.3.0 baseline is fully validated and must be resolved or formally re-deferred
before v0.4.0 merges to `main`.

Reference: `tasks/phase3_hardware_validation_deferral.md`

- [ ] T11 — Phase 3 core hardware validation (safety-critical carryover)
  - Deferred from tasks 3.0 and 3.9
  - **Blocker:** requires Artoo PCB fully wired with hoverboard and receivers
  - Closure checklist:
    - [ ] SBUS Layer 1 failsafe: power off receiver → confirm `sbusSignalLost=true`
          within 200 ms → motors stop (zero frames continue at 50 Hz)
    - [ ] SBUS Layer 2 watchdog: block SBUS frames for 200 ms+ → confirm software
          watchdog fires → motors stop
    - [ ] Failsafe re-validated in `single_sbus`, `dual_sbus`, and `standard_pwm` modes
    - [ ] Full hoverboard drive: wheel response to speed/steer from RC; `SPEED_LIMIT_MAX`
          cap confirmed; estop latches and requires manual clear
    - [ ] Dome ESC response: dome channel input moves dome motor

- [ ] T12 — Phase 3 RC mapping and upload UX hardware carryover
  - Deferred from tasks 3.5 and 3.11
  - **Blocker:** requires real RC transmitter/receiver; also depends on T11 hardware
  - Closure checklist:
    - [ ] Standard PWM: real PWM receiver on CH1-CH6; speed, steer, dome, ARM1/ARM2
          map correctly; calibration persists after reboot
    - [ ] Single-SBUS: live transmitter on RX1; default mapping confirmed; SSE live
          preview values match observed TX stick position; NVS binding changes survive reboot
    - [ ] Dual-SBUS: both receivers active; split mapping confirmed (RX1 drive +
          speed-limit, RX2 dome); RX2 additional channels configurable
    - [ ] RC mapper UX (Task 3.11): all 15 slot rows functional with real TX; live
          preview bar/indicator matches observed TX stick/switch position; keyboard
          ↑/↓ navigation works in slot list; estop slot mapping triggers latching behavior
    - [ ] Upload UX: firmware upload from `/firmware.html` shows progress, triggers
          reboot, displays updated version on reconnect; filesystem upload same flow

- [x] T13 — Reconcile status.md NVS remapping claim
  - **Resolved: stale documentation.** `main.cpp` fully loads all RC channel
    bindings (`loadRcBindingFromPrefs`/`loadRcTriggerBindingFromPrefs`) and saves
    them (`saveRcBindingToPrefs`/`saveRcTriggerBindingToPrefs`) under NVS keys
    `rcp_*`, `rcs_*`, `rc_arm1`, `rc_arm2`, `rc_aux1`–`rc_aux3`, `rc_sound`,
    `rc_opmode`, `rc_free0`–`rc_free3`. All RC binding and calibration state
    survives reboot. No software gap found.
  - `docs/status.md` updated to reflect actual implementation state.
  - **Verification:** code-confirmed (main.cpp NVS load/save audit)

- [x] T14 — RC channel detect mode (was: "learning mode")
  - **Background:** A learning mode was attempted in Phase 3 (highlight the slot
    card matching the channel the user is currently moving on the transmitter).
    The implementation caused an ESP32 crash on hardware and was fully rolled back.
    Orphaned CSS was cleaned up as part of v0.4.0 kickoff. See `tasks/lessons.md`
    (2026-03-16 entry) for the failure record.
  - **This task is non-blocking and low priority** — only attempt after T11/T12
    hardware carryover items are resolved and the bench environment is stable
  - Acceptance criteria for a new attempt:
    - [ ] Diagnose the crash mechanism from the first attempt before writing any code
          (stack high-water mark, heap monitoring, async task saturation analysis)
    - [ ] Instrument the SSE/API path used and verify max event rate ≤ 10 Hz
          under RC input at bench before any hardware deploy
    - [ ] Feature must be gate-able at build time or at runtime; default OFF
    - [ ] Bench-stage software verification fully passes before hardware test
    - [ ] If hardware crash recurs: roll back immediately and document root cause

- [x] T16 — Dedicated Sound page (`sound.html`)
  - **Background:** `index.html` has minimal audio controls (stop, vol up/dn). A dedicated
    sound page provides granular per-command trigger controls, volume management, and
    NVS-backed named-track configuration without cluttering the dashboard.
  - **Dependency:** T07 must expose the NVS track-override API endpoints consumed by this
    page (GET/POST `/api/audio/tracks`). T02 (AudioTask queue) must be active before
    hardware-testing this page.
  - **Files to create:**
    - `data/sound.html` — page markup
    - `data/sound.js` — page logic
  - **Files to modify:**
    - All existing `*.html` files — add `🔊 Sound` nav entry alongside the current nav links
  - **Page sections:**

    1. **Global controls bar** (always visible)
       - Volume slider (range 0–30) with live numeric readout; sends
         `POST /api/audio` `action=volume&level=N` on release
       - Global ⏹ Stop button → `POST /api/audio` `action=stop`
       - Current playback state indicator (idle / playing / random mode) via `/api/status`
         SSE `audio` field

    2. **Named commands table**
       Table rows for every named `$` command defined in `include/audio_dollar_parser.h`.
       One row per command with columns: Label | Command | Track # | ▶ Play

       | Row | Label | Command | Track # (NVS-editable) |
       |-----|-------|---------|------------------------|
       | 1 | Scream | `$S` | 126 (editable) |
       | 2 | Short Circuit | `$F` | 128 (editable) |
       | 3 | Leia Message | `$L` | 151 (editable) |
       | 4 | Short Cantina | `$c` | 176 (editable) |
       | 5 | Star Wars Theme | `$W` | 177 (editable) |
       | 6 | Imperial March | `$M` | 178 (editable) |
       | 7 | Long Cantina | `$C` | 180 (editable) |
       | 8 | Boot Sound | `$B` | 255 (editable) |
       | 9 | Random Chatter On | `$R` | — |
       | 10 | Random Chatter Off | `$O` | — |
       | 11 | Stop All / Chatter Off | `$s` | — |

       - ▶ Play button → `POST /api/audio` `action=dollar&cmd=$X` for named rows;
         for editable-track rows also accepts the current track # field value
       - Track # column: inline number input pre-populated from `/api/audio/tracks`;
         a Save button (or auto-save on blur) sends updated value to
         `POST /api/audio/tracks` with the named-key identifier; NVS-persisted
       - Track # column not shown for mode-control rows (`$R`, `$O`, `$s`)

    3. **Direct track play**
       - Number input (1–999) + ▶ Play button → `POST /api/audio` `action=play&track=N`
       - Use for ad-hoc testing of any track on the SD card

    4. **Random playback range (NVS-backed)**
       - Minimum track: number input; Maximum track: number input
       - Saved independently to NVS (`rand_min`, `rand_max`) via T07 endpoint
       - Stored values shown on page load from `/api/audio/tracks`

  - **API endpoints required (add to T07 scope if not already planned):**
    - `GET /api/audio/tracks` → JSON object with named-track current values and
      random pool bounds; fields mirror `AudioNamedTracks` struct + `rand_min`/`rand_max`
    - `POST /api/audio/tracks` → body `{"key":"scream","track":126}` or equivalent;
      validates range 1–999, writes to NVS, updates `RobotState.cfg_*` under `portMUX`
  - **UI constraints:**
    - Extend existing nav + style.css conventions; no new CSS framework
    - Track number inputs: constrain to 1–999 client-side; reject server-side with 400
    - Feedback element per-row for play result (ok / error message) — clear after 2 s
    - Page must be fully usable when audio hardware is not connected (sends commands,
      shows API response; no JS exceptions on missing SSE field)
    - Do not replicate mood controls here — those stay on `index.html`
  - **Acceptance criteria:**
    - `pio run -e protoArtoo` compiles with `sound.html` + `sound.js` in LittleFS
    - Page loads cleanly in browser; all named-command rows render with correct defaults
    - ▶ Play on each named row returns HTTP 200 `{"ok":true}` with AudioTask active
    - Volume slider sends correct level; Stop button stops playback
    - Track # save roundtrips: edit value → save → reload page → value persists
    - Random range save persists through reboot
    - Nav link `🔊 Sound` appears on all pages and resolves correctly
  - **Verification classification:** `bench-tested` (API + UI logic) + `full-hardware-required`
    (actual sound output from module)
  - **Scope note:** This task covers the v1 foundation. Future iterations may add:
    per-bank browsing, CHIRP-specific multi-stream controls, SD card file listing
    via a firmware API, per-track labels/descriptions editable from the UI, and
    audio driver status/diagnostic readouts. Scope additions require a new task or
    phase amendment — do not expand T16 in-place.

- [x] T17 — Runtime log level selector on Setup page
  - **Background:** `PA_LOG_LEVEL` is currently a compile-time build flag. The
    operator should be able to select the desired log verbosity at runtime from
    the Setup page without reflashing. Log level controls both serial output
    verbosity AND the in-memory ring buffer depth (see `log_buffer.h`).
  - **Design:**
    - Add a `cfg_log_level` field to `RobotState` (uint8_t, 1–3).
    - NVS key: `log_level`; default = `PA_LOG_LEVEL` build flag value.
    - `loadConfigToState()` reads it; logging macros check `robotState.cfg_log_level`
      at runtime instead of the compile-time `PA_LOG_LEVEL` constant.
    - The in-memory ring buffer depth (`LOG_BUFFER_LINES`) remains compile-time —
      it must be sized for the maximum (debug) level so no reallocation is needed
      at runtime. Log level change affects what gets written, not buffer capacity.
    - `GET /api/config` exposes `logLevel` (1/2/3).
    - `POST /api/config` accepts `logLevel=N` (1–3), validates, writes NVS, updates
      `robotState.cfg_log_level` under portMUX. Takes effect immediately — next
      log call reads the new level.
  - **Setup page UI:** dropdown in the Debug / Diagnostics section:
    - `1 — Error only` (faults and crashes)
    - `2 — Info` (normal boot + service messages; recommended for day-to-day use)
    - `3 — Debug` (verbose; use during development or fault diagnosis)
    - Shows current level on page load; updates on change with explicit Save button
      and success/error feedback. No reboot required.
  - **Serial monitor note:** serial output level changes immediately. Dashboard
    live log console reflects the new level within one SSE cycle (~1 s).
  - **Acceptance criteria:**
    - Dropdown renders with correct current value on page load
    - Changing to Info (2) stops debug log lines from appearing in serial monitor
    - Changing to Debug (3) resumes debug lines
    - Selection survives reboot (NVS-persisted)
    - `pio run -e protoArtoo` + `pio test -e native` pass
  - **Verification:** bench-tested (no hardware required beyond ESP32 + WiFi)

- [x] T18 — Per-mood random sound intervals
  - **Discovery:** Identified during T10 hardware testing. All reference projects (MarcDuino,
    BetterDuino, AstroPixelsPlus, SHADOW_MD, Padawan360) treat random chatter as binary
    on/off — all awake moods use a single global interval. In practice this means
    Quiet mode is silent but Mid-Awake, Full-Awake, and Awake+ all sound identical.
    Mood should reflect character energy level — a more awake R2 should chatter more
    frequently.
  - **Design:**
    - Replace single `AUDIO_RAND_INTERVAL_MS` constant with four NVS-backed per-mood
      interval fields in `RobotState`:
      - `cfg_snd_int_quiet`  — default 0 s (silent; Quiet mood disables random)
      - `cfg_snd_int_mid`    — default 30 s (sparse; Mid-Awake)
      - `cfg_snd_int_full`   — default 20 s (normal; Full-Awake)
      - `cfg_snd_int_awake`  — default 10 s (frequent; Awake+)
    - NVS keys: `snd_int_quiet`, `snd_int_mid`, `snd_int_full`, `snd_int_awake`.
    - `loadConfigToState()` / `saveConfigToNvs()` in `main.cpp` load and persist all four.
    - `AudioTask` derives the active random interval from `robotState.activeMood` +
      `cfg_snd_int_*` on every timer reset; no dedicated notification needed.
    - `applyMood()` does not require changes — AudioTask picks up the new mood from
      `robotState.activeMood` on the next random timer expiry.
    - When `activeMood == 0` (unset / boot before first mood apply), AudioTask falls back
      to `cfg_snd_int_full` (20 s) — safe default, not silent.
    - `GET /api/audio/tracks` includes the four interval values.
    - `POST /api/audio/tracks` accepts `snd_int_quiet`, `snd_int_mid`, `snd_int_full`,
      `snd_int_awake` as keys; validates range 0–3600 s; rejects out-of-range with 400.
    - `audioTrackNvsKey()` in `audio_dollar_parser.h` maps the four new keys to their
      NVS strings.
  - **Sound page UI** (`data/sound.html` + `data/sound.js`):
    - New section at the bottom of `sound.html`: **Mood Sound Intervals**.
    - Four labelled number inputs — one per mood — in seconds:
      - Quiet (SE10): 0
      - Mid-Awake (SE13): 30
      - Full-Awake (SE11): 20
      - Awake+ (SE14): 10
    - Loaded on page init from `GET /api/audio/tracks`; saved individually via
      `POST /api/audio/tracks` on blur or explicit Save.
    - Input constraints: min 0, max 3600; step 1; server-side validation also enforced.
    - Inline save feedback per field (ok / error); clears after 2 s.
    - Section note: explains that 0 = silent (no random chatter in that mood).
  - **Acceptance criteria:**
    - [x] `pio run -e protoArtoo` compiles cleanly
    - [x] `pio test -e native` passes; 4 new `audioTrackNvsKey` tests + 15-char limit
      coverage extended; 435/435 total
    - [x] Sound page loads interval values from API and round-trips save correctly
    - [ ] Changing mood while random chatter is active shifts to new interval within one
      timer cycle — requires hardware with audio module connected
    - [ ] All four intervals survive reboot (NVS-persisted) — requires hardware reconnect
  - **Bench validation results (2026-03-18):**
    - OTA firmware + filesystem uploaded; ESP32 on bench (no PCB/audio module)
    - `GET /api/audio/tracks` returns all 4 interval fields with correct defaults
      (quiet=0, mid=30, full=20, awake=10)
    - Sound page Mood Sound Intervals card renders correctly; inputs hydrate from API
    - Save Intervals button: changed mid=45, API confirmed `snd_int_mid: 45`; \u2705 feedback shown
    - Boundary validation: POST 0 → ok, POST 3600 → ok, POST 3601 → 400 error,
      POST -1 → 400 error
    - Client-side validation: value 9999 rejected before API call with inline error message
    - Post-review fix: `constrain()` added to NVS load for all 4 interval fields
      (consistent with project pattern; committed `d28e80c`)
  - **Verification classification:** `bench-tested` (API + UI) +
    `full-hardware-required` (audible chatter rate change per mood; reboot NVS persistence)

## Exit Criteria

- [ ] Phase 3 carryover T11 and T12 are complete, or formally re-deferred with updated
  closure checklists
- [ ] T13 status.md NVS claim reconciled (stale text updated or software gap closed)
- [ ] Bench development stage objectives are validated and recorded
- [ ] Body-link heartbeat/status is visible through the current dashboard/status system
- [ ] Dome-originated commands trigger body audio/arms correctly
- [ ] Audio works from all intended sources on hardware
- [ ] Build, native tests, and static analysis pass
- [ ] Full body/dome hardware validation is complete, or a deferred validation
  record exists with blockers and explicit closure steps
- [ ] `CHANGELOG.md` receives a real `0.4.0` entry only when Phase 4 is released
