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

  **2026-03-18 session additions (T00 polish):**
  - `AudioDriver::driverName()` pure virtual added; `DY-SV5W` / `CHIRP` implementations inline.
    `audioGetDriverName()` accessor. `s2Sound.driver` field in `/api/status`.
    Setup S2 row shows active driver label (bench-tested).
  - Serial page removed; content absorbed into Setup page Serial Status card (live S1/S2/S3,
    GPIO/baud reference, 5 s auto-refresh).
  - Double-emoji feedback bug fixed across `sound.js`, `servo.js`, `setup.js`.
  - Nav active button restored with compact sizing (all 10 pages on one line).
  - Setup Hardware Components one-per-row layout (RX IN/OUT, SERIAL COMMS sections).

- [x] T19 — Config API JSON refactor: ConfigSnapshot + ArduinoJson streaming
  - **Execution coupling note:** T19 and T20 are coupled and must be executed in the
    same implementation session. T20 test work depends directly on the `ConfigSnapshot`
    struct and `populateConfigJson()` introduced by T19.
  - **Session scope boundary:** keep the T19+T20 session self-contained to
    `src/web/api_config.cpp`, `include/api_config_snapshot.h`, and the new native
    test file added by T20.
  - **Background:** `buildConfigJson` in `src/web/api_config.cpp` uses a 55-argument
    `snprintf` into a 2048-byte static buffer. Buffer sizing is manual and has broken
    production twice. The function reads `robotState` under `portMUX` internally, coupling
    it to FreeRTOS and making it structurally untestable. `ArduinoJson@7.4.3` is already
    pinned and already used in `api_rc.cpp` for POST deserialization; not using it for
    GET response building is the wrong tool on the wrong side.
  - **Design:**
    - Define `struct ConfigSnapshot` in `include/api_config_snapshot.h` — all the fields
      currently copied as locals inside `buildConfigJson` promoted to a named struct.
    - `void captureConfigSnapshot(ConfigSnapshot* out)` — takes the `portMUX` critical
      section, copies `robotState.cfg_*` fields into the snapshot. Defined in
      `src/web/api_config.cpp` (requires FreeRTOS; not in the native build).
    - `bool populateConfigJson(JsonDocument& doc, const ConfigSnapshot& snap)` — pure
      function with no global reads; builds the ArduinoJson document field by field.
      Declared in `include/api_config_snapshot.h`, defined in `src/web/api_config.cpp`.
    - RC binding strings still use `formatRcBindingConfig` / `triggerBindingToUiString`
      into temporary 48/96-byte char buffers before being assigned into the doc; failure
      returns false (→ 500) unchanged.
    - GET handler: `captureConfigSnapshot(&snap)` → `JsonDocument doc` →
      `populateConfigJson(doc, snap)` → `req->beginResponseStream("application/json")`
      → `serializeJson(doc, *stream)` → `req->send(stream)`.
    - Static `configJsonBuf[2048]` removed — no buffer, no sizing problem.
  - **Heap allocation note:** `JsonDocument` heap-allocates internally. This is
    acceptable for Core 0 web handlers — already the pattern in `api_rc.cpp` for POST
    deserialization. The "no dynamic allocation after `setup()`" rule applies to Core 1
    real-time tasks. Clarified explicitly in `CLAUDE.md` as part of T23.
  - **Files:**
    - `include/api_config_snapshot.h` — new: `ConfigSnapshot` struct +
      `populateConfigJson` declaration
    - `src/web/api_config.cpp` — `captureConfigSnapshot` + `populateConfigJson`
      implementation + updated GET handler; add `#include <ArduinoJson.h>`
  - **Acceptance criteria:**
    - Mandatory post-implementation code review pass completed for T19
      (focus: hidden regressions, missed edge cases, API parity drift, and code quality concerns)
    - `pio run -e protoArtoo` compiles cleanly; `configJsonBuf` gone from BSS
    - `GET /api/config` returns all fields present in the old response
    - POST handler, NVS save/load, and all validation paths are untouched
    - `pio test -e native` passes (T20 tests cover `populateConfigJson` directly)
  - **Verification classification:** `bench-tested`
  - **Completed 2026-03-19:** `buildConfigJson` and `configJsonBuf` removed.
    `captureConfigSnapshot()` + `populateConfigJson()` implemented outside anonymous
    namespace. Both GET and POST handlers updated to ArduinoJson streaming response.
    `pio run -e protoArtoo` clean; RAM 19.5% (was 20.6%; 2 KB BSS reclaimed).

- [x] T20 — Native tests: populateConfigJson coverage and worst-case size guard
  - **Execution coupling note:** T20 is not a standalone task in practice; execute it
    immediately after T19 in the same session because its tests require the
    `ConfigSnapshot` type and `populateConfigJson()` from T19.
  - **Session scope boundary:** keep T20 within the same self-contained file set:
    `src/web/api_config.cpp`, `include/api_config_snapshot.h`, and
    `test/test_native/test_api_config_json/test_api_config_json.cpp`.
  - **Background:** Zero test coverage exists for the config JSON response shape.
    T19 exposes `populateConfigJson` as a testable function. These tests create a
    permanent guard against the "add a field, silently break the response" class of
    regressions that has already recurred.
  - **Test infrastructure:** `populateConfigJson` is defined in `api_config.cpp`.
    Add `src/web/api_config.cpp` to `build_src_filter` in `[env:native]`, providing
    any required FreeRTOS / Arduino stubs (e.g. `taskENTER_CRITICAL` as no-ops,
    `robotState` / `robotStateMux` as extern-linked test stubs). Test setup complexity
    does not constrain the design; we sort the plumbing to match the architecture.
  - **Test suite:** `test/test_native/test_api_config_json/test_api_config_json.cpp`
  - **Test cases:**
    1. `test_populateConfigJson_typical_valid_json` — default `ConfigSnapshot`;
       serialize to `char[2048]`; assert `n > 0 && n < 2048`; assert output
       starts `{` and ends `}`.
    2. `test_populateConfigJson_worst_case_fits_buffer` — maximally large snapshot:
       all `marcduinoPayload` fields at 15 chars, SBUS2 ch18 bindings with extreme
       calibration values, `rcInputMode = standard_pwm`, all booleans false,
       `logLevel=3`, `webDriveTimeoutMs=UINT32_MAX`; serialize to `char[2048]`;
       assert `n < 2048`.
    3. `test_populateConfigJson_expected_keys_present` — spot-check a representative
       cross-section of key names via `strstr` on the serialized output: at minimum
       `speedLimitMax`, `rcInputMode`, `enableArm1`, `arm1Type`, `domeNeutralUs`,
       `rcPwmDriveSpeed`, `rcSbusDriveSpeed`, `rcArm1`, `rcFree0`, `logLevel`.
    4. `test_populateConfigJson_disabled_trigger_binding_serializes` — all trigger
       slots set to `disabledRcTriggerBinding()`; assert returns true and output
       is valid JSON.
    5. `test_populateConfigJson_overflow_is_measurable` — serialize a fully populated
       snapshot to a deliberately small `char[64]`; assert `serializeJson` returns a
       length > 64, proving silent truncation from the old `snprintf` approach was a
       real risk.
  - **Acceptance criteria:**
    - Mandatory post-implementation code review pass completed for T20
      (focus: test realism, negative-path coverage, and serialized-size guard quality)
    - `pio test -e native` passes with the new suite included
    - All 5 test cases pass; total native test count increases accordingly
  - **Verification classification:** `bench-tested` (native host execution, no hardware)
  - **Completed 2026-03-19:** Stub infrastructure in `test/stubs/include/` provides
    Arduino, FreeRTOS, and ESPAsyncWebServer stubs for native compilation of
    `api_config.cpp`. `src/native_test_stubs.cpp` supplies extern symbol definitions.
    All 5 test cases pass; 442/442 native tests green (up from 437).
    Calibration extremes in worst-case helper constrained by `rcBindingIsValid` /
    `rcTriggerBindingIsValid` deadband rule (`deadband < max - min`); test uses
    `deadband=0` with 5-digit calibration values to maximize string length within
    valid bounds. `measureJson()` used for test 5 (exact byte count, no truncation).

- [ ] T21 — Apply same snapshot + ArduinoJson pattern to buildRcDiagnosticsJson
  - **Background:** `buildRcDiagnosticsJson` in `src/web/web_server.cpp` has the
    identical structural problem: static `rcBuf[3072]`, hand-sized from a live
    measurement, `snprintf`-based, not natively testable. The RC diagnostics JSON
    reaches ~2570 bytes in dual_sbus mode — uncomfortably close to the ceiling for a
    function that grows as RC mapping evolves.
  - **Design:** mirror T19 exactly:
    - `struct RcDiagnosticsSnapshot` in `include/rc_diagnostics_snapshot.h`
    - `void captureRcDiagnosticsSnapshot(RcDiagnosticsSnapshot* out)` — FreeRTOS-gated,
      defined in `src/web/web_server.cpp`
    - `bool populateRcDiagnosticsJson(JsonDocument& doc, const RcDiagnosticsSnapshot& snap)`
      — pure function, defined in `src/web/web_server.cpp`, declared in the header
    - GET handler in `api_rc.cpp`: snapshot → doc → stream; `rcBuf` removed
    - `buildRcDiagnosticsJson` declaration in `include/web_server.h` removed
  - **Files:**
    - `include/rc_diagnostics_snapshot.h` — new: struct + declaration
    - `src/web/web_server.cpp` — replace `buildRcDiagnosticsJson`
    - `src/web/api_rc.cpp` — update GET handler
    - `include/web_server.h` — remove old declaration, add new ones
  - **Native tests:**
    - `test_populateRcDiagnosticsJson_typical_valid` — default snapshot; valid JSON
    - `test_populateRcDiagnosticsJson_dual_sbus_fits_buffer` — dual_sbus mode with all
      channels populated; serialize to `char[3072]`; assert `n < 3072`
    - Add `src/web/web_server.cpp` to `[env:native]` `build_src_filter` as needed
  - **Acceptance criteria:**
    - Mandatory post-implementation code review pass completed for T21
      (focus: response parity, snapshot correctness, and cross-file integration risks)
    - `pio run -e protoArtoo` compiles; `rcBuf` gone from BSS
    - `GET /api/rc` response is identical to pre-refactor output
    - `pio test -e native` passes including the two new RC tests
  - **Verification classification:** `bench-tested`

- [ ] T22 — Grouped /api/config JSON schema (deferred — Phase 5 candidate)
  - **Background:** The flat namespace conflates drive config, RC bindings, component
    toggles, dome params, and system settings into one undifferentiated object. A
    grouped schema is cleaner to extend and easier to consume on the JS side.
  - **Why deferred:** Requires migrating `setup.js`, `rc.js`, `drive.js`, and possibly
    `dome.js` to use nested key paths. After T19 the firmware change is trivial
    (`doc["rc"]["pwm"]["driveSpeed"] = ...`); the JS migration carries real regression
    risk and warrants its own scoped task with full acceptance testing.
  - **Precondition:** T19 must be complete.
  - **Proposed schema sketch:**
    ```json
    {
      "drive":      { "speedLimitMax", "webDriveTimeoutMs", "ch8ModeLock", "stationary" },
      "rc":         { "inputMode", "pwm": { ... }, "sbus": { ... }, "triggers": { ... } },
      "components": { "arm1": { "enabled", "type" }, "dome": { "enabled" }, ... },
      "dome":       { "neutralUs", "minPulseUs", "maxPulseUs", "speedLimitPct" },
      "system":     { "logLevel" }
    }
    ```
  - **When actioned:** update `docs/goal.md §9`; migrate all consuming JS; update
    native tests for new schema shape.
  - **Action:** move to `tasks/phase5-tasks.md` when Phase 5 is scoped; do not
    implement in Phase 4 unless all T11/T12 carryover items are resolved and
    explicit capacity exists.
  - **Additional required verification (when implemented):**
    - Mandatory post-implementation code review pass completed for T22
      (focus: frontend consumer migration correctness, schema consistency, and UX regression risks)
  - **Verification classification:** `bench-tested`

- [ ] T23 — Upload gate: pio test required before upload; fix silent JS error catch
  - **Background:** AGENTS.md / CLAUDE.md advise running `pio test -e native` before
    marking complete but this is not enforced. The `buildConfigJson` bug reached the
    device because the coverage gap went undetected and no gate blocked the upload.
    Two root causes: (1) no test existed for `buildConfigJson`, (2) no process rule
    prevented uploading with untested code.
  - **AGENTS.md changes (§ Verification and Reporting):**
    - Upload gate: "`pio test -e native` MUST pass and all tests must be green before
      issuing any `upload` or `uploadfs` command. A compile-only build does not qualify
      as a pre-upload verification step."
    - JSON test rule: "Any function that builds a JSON API response — whether via
      `snprintf` into a fixed buffer or via `JsonDocument` — MUST have a corresponding
      native test covering the typical case and confirming the serialized output fits
      within its intended size budget."
  - **CLAUDE.md changes:**
    - Add identical upload gate to `§ Verification Before "Done"`.
    - Clarify heap allocation scope under `§ Engineering Best Practices`: "The 'no
      dynamic memory allocation after `setup()`' rule applies to Core 1 real-time tasks
      (DriveTask, SBUSTask, AudioTask, DomeLinkTask, ServoTask). Core 0 web handlers MAY
      use `JsonDocument` (ArduinoJson 7) for request deserialization and response
      building; allocation is temporary, per-request, and bounded. `api_rc.cpp` already
      establishes this pattern for POST bodies."
  - **setup.js fix:**
    - `loadFeatures` catch block discards the error silently (`catch (_error)`).
      Change to `catch (error)` and add
      `console.error("[setup] loadFeatures failed:", error)` before setting the
      user-facing text. The on-screen message stays clean; browser devtools shows the
      actual HTTP status or JS exception for diagnosis.
    - Audit all other `catch (_error)` blocks in `setup.js`; apply the same pattern
      to any that cover a non-trivial operation with a visible failure state.
  - **Acceptance criteria:**
    - AGENTS.md upload gate and JSON test rule present and unambiguous
    - CLAUDE.md upload gate and heap allocation clarification present
    - `setup.js` `loadFeatures` catch logs to `console.error`
    - `pio run -e protoArtoo` + `pio test -e native` pass
  - **Verification classification:** `bench-tested`

- [x] T24 — Regression: WebEvents task crash on SSE client connect (third recurrence)
  - **Symptom:** ESP32 becomes unreachable ~1 minute after boot. Serial monitor shows
    a continuous crash-reboot loop (garbled ROM bootloader output repeating). Device
    is stable until a browser connects to the SSE stream, then crashes within seconds
    of the first `buildRcDiagnosticsJson` call.
  - **Root cause (established):** The `WebEvents` FreeRTOS task stack has been
    hand-tuned incorrectly three times:
    1. **T16** (018387b): 8192 → 4096. Justification: "large buffers moved to
       file-scope statics, not on stack." Partially wrong — those buffers were
       already `static` locals (BSS, not stack) in the original code. The actual
       stack users are `RcDiagnosticsSnapshot snapshot` and
       `RcActionBindingSpec specs[7]` inside `buildRcDiagnosticsJson`, which
       are non-static locals and DO live on the stack. 4096 was adequate because
       measured peak is ~1750 bytes (~2350 B headroom).
    2. **T13 HWM commit** (4b35e4d): 4096 → 2048. HWM was measured at the FIRST
       loop iteration, before any SSE client connected and before
       `buildRcDiagnosticsJson` had ever been called. Measured an idle task,
       not a loaded one. Left ~298 bytes of real margin — less than FreeRTOS
       context-save overhead on preemption. Crash on every client connect.
    3. **ed42c90** (fix): 2048 → 4096. Correctly documented peak at ~1750 bytes.
       Commit says "Verified" but contains NO OTA deploy note. Subsequent commits
       (`52cfba0` T14 rewrite) DO have deploy notes and contain the 2048-byte stack.
  - **Current device state (most likely):** Firmware from `52cfba0` (T14 rewrite,
    deployed to 10.0.0.22 per commit message) with 2048-byte WebEvents stack.
    Fix in `ed42c90` (4096) exists in the repo but was never OTA-flashed.
    Three further commits today (`c75720e`, `dd075ef`, `ecc3924`) also not deployed.
  - **The `hwmLogged` guard is a recurring trap:** `eventStreamTask` logs HWM exactly
    once, at the first loop iteration, before `serverStarted && events.count() > 0`
    is true. The HWM log will ALWAYS show near-empty stack — it never fires under
    SSE load. Any future HWM measurement MUST be taken after a call to
    `buildRcDiagnosticsJson` with at least one active SSE client.
  - **Contributing factors (do not increase stack beyond 4096 at 4096):**
    - `52cfba0` T14: Added `sbus1Raw[16] + sbus2Raw[16] + pwmPulseUs[6]` serialization
      after `formatRcDiagnosticsJson`. These arrays were already on the stack before
      T14 (T14 just serialized them); the additional `rcDiagnosticsAppendf` calls
      use `%u` integer formatting only (no float), minimal stack increase.
    - `dd075ef`: Added `uint32_t heapLargestBlock` to `buildStatusJson`. Called from
      `eventStreamTask` but sequentially with `buildRcDiagnosticsJson` — frames
      do not overlap. Negligible stack impact.
  - **Fix options (in priority order):**
    1. **OTA-deploy current HEAD firmware** (immediate mitigation): HEAD has 4096-byte
       stack (`ed42c90`). Flash `pio run -e protoArtoo_ota --target upload` to replace
       the 2048-byte firmware currently running. Stable until T21 is implemented.
    2. **Restore to 8192** (safe fallback if 4096 proves insufficient): costs 4 KB heap
       but eliminates all uncertainty. Appropriate if any new code is added to
       `buildRcDiagnosticsJson` before T21 is complete.
    3. **T21 (structural fix):** `RcDiagnosticsSnapshot` moves from `buildRcDiagnosticsJson`'s
       stack frame into a captured snapshot struct passed by reference. `populateRcDiagnosticsJson`
       uses ArduinoJson on the heap (per-request, bounded). Stack usage of `eventStreamTask`
       drops to near-zero for the RC diagnostics path. Stack sizing becomes a non-issue.
  - **Interaction with T21:** T21 is already planned and is the correct permanent fix.
    Implementing T21 makes this class of crash structurally impossible regardless of
    how many fields are added to the RC diagnostics JSON in future.
  - **Before implementing any fix:**
    - Confirm device firmware version by checking `/api/status` `version` field.
    - If running pre-ed42c90 firmware: OTA-flash HEAD first, confirm stable.
    - Add a second HWM log point INSIDE the SSE send block (after the first
      `buildRcDiagnosticsJson` call completes) to get an accurate under-load measurement.
  - **Bench validation update (2026-03-19):**
    - Reproduced crash on current firmware in USB bench mode with SSE connect:
      `Guru Meditation Error ... Stack canary watchpoint triggered (async_tcp)`.
    - Implemented fix in `src/web/web_server.cpp`:
      - kept `WebEvents` task stack at 4096 bytes
      - added under-load HWM log in `eventStreamTask` after first successful
        `buildRcDiagnosticsJson` call
      - removed heavy JSON/log backfill work from `events.onConnect` callback;
        callback now only registers connection while periodic send work stays in
        `eventStreamTask` on Core 0
    - Verified on hardware (USB bench mode, serial monitor + SSE client):
      - 10-minute SSE soak captured (`/api/events`) with no panic/reboot
      - `uptimeMs` advanced by `609703 ms` across SSE status events
      - under-load HWM log captured: `[WebEvents] stack HWM under SSE load: 1320 words free`
      - serial panic scan showed no `Guru Meditation`, `Stack canary`, or reboot loop entries after fix
    - Verification commands run:
      - `pio run -e protoArtoo`
      - `pio test -e native` (437/437 passed)
      - `pio run -e protoArtoo --target upload --upload-port /dev/ttyUSB0`
  - **Post-fix follow-on regressions addressed (2026-03-19):**
    - Setup page toggles intermittently failed with `Failed to load component settings`.
      Root cause was AsyncTCP callback stack pressure on `/api/config` GET; serial
      showed `Stack canary watchpoint triggered (async_tcp)` on config fetch.
    - Guardrail applied: `CONFIG_ASYNC_TCP_STACK_SIZE=8192` in `platformio.ini`
      for both `protoArtoo` and `protoArtoo_prod` environments.
    - Operator log-noise complaint addressed: periodic SafetyMonitor heap telemetry
      (`[SafetyMonitor] heap: free=...`) is now `PA_LOG_DEBUG` instead of
      `PA_LOG_INFO`, so log level `1 - Error only` suppresses it as expected.
    - Runtime verification after flash:
      - `/api/config` returns HTTP 200 with full payload again
      - setup page shows `Components loaded ...` and toggles hydrate normally
      - `POST /api/config` with `logLevel=1` persists and reports `"logLevel":1`
  - **Acceptance criteria:**
    - Device remains reachable for >10 minutes with at least one browser tab open on
      any SSE-consuming page (dashboard, setup, RC)
    - Serial monitor shows no crash-reboot pattern during that window
    - HWM measured under load (not at first iteration); value recorded here
    - `pio run -e protoArtoo` + `pio test -e native` pass
  - **Verification classification:** `full-hardware-required` (requires WiFi-connected
    ESP32 with a browser holding an SSE connection)


## Exit Criteria

- [ ] Phase 3 carryover T11 and T12 are complete, or formally re-deferred with updated
  closure checklists
- [x] T13 status.md NVS claim reconciled — stale doc confirmed; all RC bindings NVS-backed
- [x] Bench development stage objectives validated and recorded (T01–T09, T13–T18 complete)
- [ ] API JSON serialization refactor complete: T19, T20, T21, T23 bench-verified;
  T22 formally deferred to Phase 5 with rationale recorded above
- [x] T24 WebEvents crash regression resolved: device stable >10 min under SSE load;
  T21 structural fix is the preferred long-term closure path
- [x] Body-link heartbeat/status visible through dashboard/status system (Serial Status card in Setup)
- [ ] Dome-originated commands trigger body audio/arms correctly
- [ ] Audio works from all intended sources on hardware
- [ ] Build, native tests, and static analysis pass
- [ ] Full body/dome hardware validation is complete, or a deferred validation
  record exists with blockers and explicit closure steps
- [ ] `CHANGELOG.md` receives a real `0.4.0` entry only when Phase 4 is released
