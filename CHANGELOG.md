# Changelog

All notable changes to `protoArtoo` are documented here.

Format follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning 2.0.0](https://semver.org/).

This changelog is tag-based. Entries are added only when a version is actually
released and tagged in git.

Every semantic version release belongs here:
- patch releases for bug fixes
- minor releases for new backwards-compatible features
- major releases for breaking changes

## [Unreleased]

## [1.2.0] - 2026-09-06

A command console for the droid, in the dashboard and on a serial cable. Every
operation the controller can perform is available in both places, in the same
words, and answers come back the same way. The console is not a second way in
past the safety layers — it runs a command through exactly the code the web
buttons run it through, so an estop or a stationary lock refuses a typed
command the way it refuses a tapped one, and says which one stopped it.
On the FireBeetle 2 it runs over the board's own USB port and comes back after
you unplug and replug the cable. When the controller is low on memory and the
web pages have stopped answering, the serial console still tells you how the
board is doing.

### Added
- **The Controller Console.** Type a command into the dashboard's Live Logs
  panel or over a serial cable and the droid does the same thing either way.
  Commands are the operation's own name and its values —
  `drive.action.move speed=200 steer=0` — so what you read in the action list
  is what you type.
- Answers come back as one line per field with a number on each, not as a
  paragraph of text: you can tell which reply belongs to which command even
  while log lines keep scrolling past. Every command ends in a plain word for
  what happened — `queued`, `applied`, `blocked`, `unavailable`, `invalid` —
  and a refusal always names its reason.
- **A serial console on both boards.** The artoo-esp32 over its usual serial
  cable, the FireBeetle 2 over the board's own USB port. Arrow keys, history
  and Tab completion work as they do in a terminal; Tab finishes operation
  names *and* their setting names.
- Help and Tab completion are built from the droid's own list of what it can
  do, so they cannot drift from reality — a command the firmware does not have
  cannot appear, and one it does have cannot be missing.
- **The console keeps answering when the web stops.** Drive the controller
  short of memory and the pages go quiet; `system.status.health` still comes
  back in full over the serial cable, which is when you most need it.
- An operation the board cannot run says so and why — a profiling command on a
  build without it answers `not-in-this-build` rather than failing silently.
- **A console tool for your computer** (`tools/console_client.py`, and
  `make console`). Capture a boot log, type at the droid, or replay a written
  sheet of commands and keep the transcript. The sheets in `tools/bench_rows/`
  are the ones used to check each board.
- When something on your computer stops reading the cable and the droid has to
  drop console output, it says so in the log — once when it starts and once
  when it clears, with how many lines were lost. It never refuses a command
  because of it.

### Changed
- **Reboot the WiFi module from the console.** `system.action.reboot-wifi-module`
  holds the module's enable line low for a tenth of a second and lets it come
  back, so you can watch the controller notice the link go and bring it back on
  its own. Nothing else on the droid pauses while it happens - driving, dome,
  servos and sound do not go through the WiFi module. Run it from the serial
  cable: over WiFi the answer would travel on the link the command takes down.
  An artoo-esp32 has no WiFi module fitted and says so rather than failing.
- Setting a password from the console is refused with a clear reason rather
  than half-accepted; passwords are set on the WiFi page or in AP mode.
- Log lines and command replies can no longer land inside each other on the
  serial cable. One part of the firmware owns the cable and everything else
  hands its lines over, so a log line arriving mid-command no longer chops the
  reply in half.
- `system.status.health` now also reports how long the board has been up and
  why it last restarted, so a board that rebooted while you were not looking
  says so.
- The status page now publishes its failed-allocation and dropped-command
  counters, so a memory problem shows as a number instead of a guess.
- The dashboard remembers what you typed between visits, and its command box
  completes with Tab like the serial one.

### Fixed
- **The FireBeetle 2's console went permanently silent after you unplugged and
  replugged its USB cable.** The board stayed up and still ran what you typed —
  you just never saw a prompt, an echo or an answer, which is worth knowing
  because a droid whose console looks dead is a droid you are tempted to type
  the same drive command into twice. The console now comes back on replug, with
  the half-typed line cleared.
- An over-long line is refused with `line-too-long` instead of running a
  chopped-off version of what you typed.
- Listing every operation streams to the end instead of stopping silently part
  way through.
- A long log history no longer errors the browser when you ask for it, and a
  log fetch that fails now says so in the panel instead of leaving it blank.
- Settings whose values are words like `on` and `off` are no longer turned into
  `true` and `false` on their way out of the action list.
- Console output on the FireBeetle 2 waits for room on the USB port rather than
  arriving in pieces, and a long line keeps its ending.
- **`help <operation>` was showing the wrong operation's description.** Not only
  after an update - on shipped images, for 104 of the 194 operations. The help
  text is addressed by counting bytes into a file, and the counting was done in
  characters; twenty-three descriptions contain a dash that takes three bytes to
  write, so every entry after the first one was read from slightly the wrong
  place. The text that came back was another operation's, and looked perfectly
  normal. Counting is fixed, and all 194 now read their own entry.
- If the help file ever does stop matching the firmware - you updated one and not
  the other - `help` now says it cannot read the entry instead of confidently
  showing you somebody else's.

### Still to verify
- **Nothing here has been exercised on an assembled droid.** Both boards were
  checked on the bench, unseated, with no droid components attached: no RC
  receiver, no drive or dome lanes, no servos, no LEDs. Everything reachable
  over the web and over the cable was confirmed on both boards.
- Values that contain spaces or quotation marks are sent unquoted by the
  firmware, though the console's own written protocol says they should be
  quoted. The dashboard hides this because it re-applies the rule when it draws
  the line; a tool reading the serial cable directly sees it.
- On the FireBeetle 2, a single console line longer than 256 characters is cut
  short by the USB port rather than being sent in pieces.
- **The WiFi module reboot has not been watched working on a board.** The command
  is wired and the enable line is the one the board's schematic names, but the
  two writes reporting success is not the same as seeing the module go and come
  back. Watching the controller notice and recover is still to do.

## [1.1.0] - 2026-09-06

A second controller board. The full protoArtoo feature set now builds for and
runs on the DFRobot FireBeetle 2 ESP32-P4 beside the artoo-esp32, reaching WiFi
through the board's fitted ESP32-C6 WiFi module. The FireBeetle 2 is supported
for developers and ships as a ready-to-flash download; it is not yet a
recommendation to buy — one of the two units bought for this work arrived with
a misprovisioned radio, and the spec sheet says what that means before you
order one (`docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md`, "Before you
buy one"). The artoo-esp32 image behaves as it did in `v1.0.0` and stays inside
its flash and RAM budgets, which every pull request now enforces.

### Added
- **FireBeetle 2 ESP32-P4 support.** Web control, live status, RC input, the
  dome link, audio, servos, the safety layers, backups and firmware updates
  all build for and run on the FireBeetle 2 (`firebeetle2` build
  environment). A board is a pin map plus a build environment on top of one
  shared chip layer, so the next ESP32-P4 board costs a pin map, not a port.
- Tagged releases now also publish `firebeetle2-firmware.bin` and
  `firebeetle2-filesystem.bin`, built for the DY-SV5W audio module, next to
  the artoo-esp32 images per audio module.
- A FireBeetle 2 answers at `firebeetle2.local` and an artoo-esp32 keeps
  `artoo.local`, so two boards can share one network without a wrong-board
  update. `make ota` now refuses to push an image at a controller that
  answers as the other board.
- Firmware updates over WiFi work on the FireBeetle 2 (`firebeetle2_ota`).
- On the FireBeetle 2 the audio module has its own hardware serial port in
  both directions, so audio and the dome link no longer take turns on one
  port.
- WiFi module supervision on the FireBeetle 2: when the WiFi module stops
  answering, the controller runs a bounded recovery of the link and, if that
  fails, settles into a degraded state in which the droid keeps working
  without a web UI and announces the outage itself — a sound cue, a dome
  logic-text message and the serial log. A network fault never restarts the
  controller and never touches a droid function.
- The Setup page shows which board it is talking to, with each board's own
  connector label (the artoo PCB's "S1", for example) as a hint beside the
  generic component name.
- A feature that is not part of the running image shows a status lamp
  reading "Not included" or "Not on this board" instead of a switch that does
  nothing.
- `GET /api/identity` reports the board (`artoo_esp32` or `firebeetle2`) and
  its compile-time capabilities, so tooling and the dashboard can tell the
  boards apart.
- A soak harness, `tools/soak.py`, holds a controller under continuous
  dashboard load for as long as you ask and answers with one verdict and one
  exit code (`docs/soak.md`). The FireBeetle 2 passed a three-hour live-update
  soak and a reconnect storm on it; the artoo-esp32 ran four and a half hours
  under web load without a reboot or a refused page.
- Memory profiler builds for the FireBeetle 2 (`firebeetle2_profiler`), and a
  crash-dump partition on it.
- Build-size budgets per build environment (`tools/build_budgets.json`,
  `make check-build-budgets`) fail a pull request whose image passes its flash
  or RAM budget, so a FireBeetle 2 feature cannot spill into the artoo-esp32's
  4 MB flash unnoticed.
- `make` selects the right toolchain for each board and takes the
  machine-wide build lock itself, so two builds on one machine no longer
  corrupt each other's output.
- Developer documentation for the board: a hardware spec sheet with the chip
  revision, pin allocation and known issues
  (`docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md`), FireBeetle 2
  sections in `docs/pin_map.md`, and architecture decisions 0028 to 0035
  under `docs/adr/`.

### Changed
- Component switches are named for what they control — Drive, Audio,
  protoR2link, Dome ESC, Utility Arm 1 and 2, AUX 1 to 3 — instead of the
  artoo PCB's S1/S2/S3 silkscreen. Saved settings migrate on the first boot
  of this version. RC-trigger bindings and the component keys in
  `/api/status` follow the new names, so a script that reads the old keys
  needs updating.
- Task stacks, the log ring depth, the learned-sequence capacity and the
  sequence evidence ring are sized per chip from measured call chains. The
  FireBeetle 2 uses its larger memory; the artoo-esp32 keeps exactly the
  values it shipped with.
- The emergency stop now arms at boot after any watchdog reset, not only a
  task-watchdog one. An interrupt that stopped returning is not a gentler
  failure than a task that stopped feeding its watchdog.
- `make ota` pins the host-side port the controller connects back to
  (`OTA_HOST_PORT`, default 32320), so an update works behind a default-deny
  firewall with one documented rule instead of failing with "No response
  from device".

### Fixed
- The second SBUS receiver could not start on the ESP32-P4, whose receive
  memory is laid out differently; the block budget is now derived per chip.
  The artoo-esp32 keeps its previous allocation.
- A serial monitor that stops reading the FireBeetle 2's USB port could hold
  the dome task long enough to trip the task watchdog. Log writes to USB are
  now best-effort and never block a task; the in-memory log behind
  `/api/logs` still records every line.
- The heap-fragmentation warning could never fire on the FireBeetle 2, and
  the stack high-water mark in the log was labelled in words when it is
  bytes.
- ESP32-P4 bring-up uses the board variant's built-in LED definition instead
  of duplicating the FireBeetle 2 GPIO number.
- The Setup page's board panel recovers when the identity request succeeds on
  a retry instead of staying blank.
- The web test suite stays parseable on Node 26.

### Still to verify
- **The FireBeetle 2 has not driven a droid.** Everything reachable over the
  web — pages, live status, updates, safety-layer arming, drive-frame timing,
  the watchdog and the RC-loss boot posture — was confirmed on the board over
  USB. Nothing that needs a signal on a pin has been: RC receiver input, the
  drive and dome serial lanes, servo pulses, I2C and the LED strip.
- The WiFi module's "degraded" announcement has never been seen firing. It
  needs five consecutive module failures, which the shipping image cannot
  provoke on purpose; a deliberate module reset did recover without a
  reboot.
- GPIO 48 to 52 on the FireBeetle 2 sit on regulator rails that are
  unmeasured under load; a problem there would show as servo jitter or dome
  ESC throttle, never as a log line (`docs/pin_map.md`, Known Issue).
- Updating the WiFi module's own firmware: a factory module refuses the
  wire-free route, and the wired procedure is marginal. Both are recorded in
  the spec sheet's known issues.
- Moving a droid's saved settings from an artoo-esp32 to a FireBeetle 2 has
  not been specified or tested.
- The artoo-esp32 soak runs used an image built with Bluetooth compiled out
  of the framework, a configuration this project does not ship; the release
  configuration passes the same build, budget and test checks but has not
  had a soak of its own.

## [1.0.0] - 2026-08-21

First stable release. Feature-complete for day-to-day operation — audio, RC
control, dome control, servos, the web control panel, backups, and firmware
updates are all confirmed working on real hardware. Full drive-motor
(hoverboard) validation on a completely assembled droid is tracked as
follow-up work, not a blocker for this release (see `docs/status.md`).

### Added
- Runtime action registry now powers RC bindings and the action picker in the web API/UI.
- Sleep mode now keeps body and dome state in sync, with wake and sleep control from the web UI and RC bindings.
- CHIRP playback got broader sound coverage: category ranges, named tracks, banked playback, and system sound mapping.
- Full-droid sequence actions and dome sequence controls now support more complex show playback.
- AUX LED strip support now includes configurable header selection and setup-page controls.
- The setup page now supports NVS backup and restore.
- Shell controls now include estop and reboot actions.
- Random dome rotation now has web configuration and RC control support.
- Coredump partition and HTTP retrieval (`/api/coredump`) lets operators download
  crash dumps for offline analysis alongside the live serial log.
- Static web assets are now gzip-compressed at build time, reducing dashboard load size.
- The sequence editor's dome panel picker is now an SVG rendering of real dome
  geometry, matching the live dome render on the home page, with pie panels
  directly selectable.
- The home page now has quick-access controls for dome commands and recently-used sequences.
- New `POST /api/seq/stop` provides a non-latching way to abort a running
  sequence, distinct from estop.
- The dashboard now shows which dome link transport (WiFi or serial UART) is active.
- Dome visual commands (Holo Effect, Logic/PSI, Logic Text, Visual Preset) are
  now validated server-side before dispatch, catching malformed dome commands
  before they reach the dome.
- The sequence editor now supports authoring `DV:<name>` visual presets and
  Holo Effect / Logic-PSI / Logic-Text steps, completing the dome sequence vocabulary.
- Every built-in sequence now shows its intended use on the Factory card.
- Operators can now set a custom droid name from the web UI, shown on the status page and in logs.
- GitHub Actions now gate pull requests with build, test, static-analysis, and
  dependency-review checks.
- Tagged releases now publish ready-to-flash firmware and filesystem images for
  each sound backend (CHIRP, MP3 Trigger, and DY-SV5W), so operators can update
  a controller without installing a build toolchain.
- WiFi setup no longer requires building from source. A controller flashed with
  a downloaded release build starts its own setup hotspot on first boot; joining
  it opens the WiFi page, where the droid is either pointed at an existing
  network (WiFi Client Mode) or kept on its own hotspot (Standalone AP Mode).
  WiFi settings live on the controller and survive firmware updates. Compiled-in
  credentials (`src/secrets.h`) remain available as a self-build developer
  shortcut only.
- An explicit Network Recovery Mode reopens the setup hotspot with a documented
  default credential, so a controller whose saved WiFi settings point at an
  unreachable network can always be repaired without reflashing.
- Servos, dome, and audio each have an enable toggle: a component switched off
  is fully inert (nothing is driven on its pins), and toggle changes are staged
  to take effect at the next reboot like other reboot-scoped settings.
- When the controller is too busy to serve a page, it now answers with a plain
  "controller busy" recovery page that explains what happened and offers a
  retry, instead of the browser showing a generic connection error. A refused
  page load is now visibly different from an unreachable controller.
- Every dashboard page now loads its data through a shared bootstrap that
  reports each section's state individually. A section that fails to load shows
  its own retry control and an explanation, instead of leaving the page blank or
  silently stale, and controls backed by data that never arrived are disabled
  rather than acting on stale values.
- `/api/status` now reports web-server load evidence — live and peak concurrent
  request depth, peak live-status client count, and how many connections were
  refused and why — so a busy or degraded controller can be diagnosed from the
  dashboard instead of from a serial log.

### Changed
- Drive and RC control paths now use a shared output arbiter, safer source handling, and latching safety behavior.
- Configuration flows now separate save/apply, runtime use, and reboot survival through clearer state handling.
- Audio now uses a shared driver interface and playback policy, with cleaner catalog ownership and volume handling.
- Dome handling is now split into serial control, motion control, and body-link coordination.
- Web and dashboard surfaces now have clearer live feedback, more reliable status derivation, and better log handling.
- The public status page (`docs/status.md`) was rewritten for a plain-language
  operator audience, with per-subsystem evidence and a clear list of what's
  still unverified.
- CHIRP audio and the dome serial link now coexist reliably on a shared UART,
  instead of intermittently dropping audio or dome commands.
- Sequence and CHIRP catalog memory is now allocated based on actual data size
  instead of static worst-case buffers, reducing baseline heap usage.
- The controller's web server was replaced. It now runs on the ESP32's own
  built-in HTTP server (via PsychicHttp) instead of the previous asynchronous
  stack, which could not be made reliable under the memory pressure a dashboard
  page load creates. Connections are now reused across requests rather than
  reopened per response, and a request that stalls mid-response is closed
  instead of holding the connection open indefinitely.
- The controller now decides whether it can serve a request before accepting it,
  based on actual free memory at that moment, and always answers — with the
  recovery page above — rather than dropping the connection. Live-status
  streams are budgeted separately from ordinary page loads, so a dashboard left
  open no longer starves a new page load.
- The dome panel picker UX is more direct (fewer confirmation steps for pie
  panels) and more readable (contrast, label sizing) for low-light operation.
- OTA update timeouts were extended for reliability over slow connections.
- Log verbosity now has four tiers (Errors, Warnings, Info, Debug) instead of
  three, selectable from the Setup page. Log lines are timestamped, and the
  in-memory log buffer is sized at boot from the saved level, so quieter levels
  keep more history.
- The `LICENSE` and `README` now state plainly what the MIT grant covers (this
  repository's own firmware, web UI, docs, and tooling) and what it cannot
  (third-party libraries, the Artoo Controller PCB design, the MK4 droid
  design), with a Lucasfilm/Disney non-affiliation disclaimer.

### Fixed
- Audio control and playback regressions across supported backends.
- RC mapping, config apply, and UI synchronization bugs.
- Status and diagnostics issues that could hide the actual runtime state.
- A heap-exhaustion crash (OOM) affecting CHIRP and Learned Sequences under
  larger catalogs/libraries — memory is now right-sized on demand instead of
  over-allocated up front.
- CHIRP audio could silently fail to initialize when the dome serial link held
  the shared UART; it now retries instead of falling back permanently.
- Dome link UART recovery — reconnects now clear stale errors and no longer
  hang indefinitely contending for the shared UART.
- Dashboard pages that would fail to load, load partially, or hang the
  controller when several pages or tabs were opened at once, or when the
  controller was low on memory. Page loads now succeed or refuse cleanly with an
  explanation, and the controller stays reachable throughout.
- Server-side crashes and connection issues under live-status (SSE) load, and
  under general API load. A live-status client that stops reading is now dropped
  instead of blocking the controller.
- Large web assets could be truncated mid-transfer when memory was tight,
  producing a partly-rendered dashboard; they are now sent in chunks the
  connection can accept, and a starved write is retried instead of cut short.
- A failed or incomplete firmware/filesystem upload could leave the controller
  rebooting into a half-written image; an upload that delivers no usable image
  is now rejected and the update aborted, with an accurate error message.
- Dome panel availability messages now correctly explain *why* a panel is
  unavailable instead of a generic "not reachable".
- The dome panel picker no longer renders blank on first load.
- Pie panel label contrast now meets accessibility guidelines for low-light use.
- A spurious failsafe trigger in single-SBUS mode with a secondary channel enabled.
- Sequence save no longer drops the sequence body, fixing a save/round-trip regression.
- A saved Learned Sequence could be written but not marked usable, so it never
  appeared as playable; saved sequences are now indexed correctly and sized to
  the sequence actually submitted.
- An RC input disabled in settings is now parked and inert from boot instead of
  still being read, the boot-active RC configuration is reported truthfully in
  the UI, and RC setting changes clearly mark that a restart is needed before
  they apply.
- Drive commands from an RC source that has gone quiet now expire in the output
  arbiter instead of being held and replayed.

### Still to verify
- Drive-motor (hoverboard) behavior on a completely assembled droid. Drive control,
  safety logic, and failsafe are implemented and tested, but live wheel response
  has not yet been confirmed on a finished build. This is planned as follow-up
  work after `v1.0.0` and will be documented when complete.
- The MP3 Trigger audio module (an alternative to CHIRP) is implemented but has
  not been re-confirmed on hardware for this release.
- The failed-upload abort guard is implemented and confirmed in software, but the
  failure itself has not been reproduced on hardware for this release.
- Web-server behavior is confirmed under bench load and induced memory pressure,
  but not yet over a long continuous session; sustained multi-hour dashboard use
  has not been soak-tested.
- Drive hardware checks are still to be completed on the hoverboard and complete droid hardware. This is the remaining hardware verification item before release.
- RC input decision logic (mode changes, watchdog and receiver-failsafe transitions,
  zero-frame behavior on signal loss) was reworked for this release and is covered by
  automated tests, but the reworked code has not yet been run on a controller. Checking
  it on hardware, including a signal-loss drill with a live receiver, is still to be
  completed.
- First-boot WiFi Provisioning on a factory-fresh (unprovisioned) controller has
  not been exercised live end to end. The boot-posture decision is covered by
  automated tests, and release builds are now compiled without any developer
  WiFi shortcut, but the literal "flash, join the setup hotspot, open the WiFi
  page" flow still needs one live pass.

## [0.4.0] - 2026-03-29

### Added
- **Audio system** — pluggable audio module support with three backends: DY-SV5W binary-frame
  driver (confirmed on hardware), CHIRP Audio Trigger ASCII driver, and SparkFun MP3 Trigger
  binary driver. All audio routes through a single `AudioTask` queue accepting commands from
  RC, web API, and dome serial. Backend selected at compile time via `PA_AUDIO_DRIVER`.
- **Sound page** — dedicated `/sound.html` with volume slider, named sound triggers (scream,
  Leia message, Short Circuit, Cantina variants, Star Wars theme, Imperial March, startup),
  direct track play by number, random chatter pool range, mood sound intervals, and a live
  Audio Module Status card showing link state, device type, play state, and track count.
- **Per-mood random sound intervals** — each mood state has its own configurable chatter
  interval: Quiet = 0 s (silent), Mid-Awake = 30 s, Full-Awake = 20 s, Awake+ = 10 s.
  All four are configurable from the Sound page and persisted across reboots.
- **Dome serial link** — body sends `#PAHB` heartbeat to the dome at 1 Hz; receives and
  dispatches Marcduino commands from the dome (sounds, arm sequences, mood triggers).
  Dome link health shown on the dashboard.
- **Mood presets** — mood selection dispatches the audio command locally and forwards the
  dome lighting sequence over the serial link when the dome is connected. Last active mood
  is restored on reboot (audio component only; dome TX deferred until link is up).
- **Runtime log verbosity selector** — log level adjustable from the Setup page (Error only,
  Info, Debug) without reflashing; persisted across reboots; takes effect immediately.
- **Audio Module Status card** — Sound page shows confirmed live state from the module
  itself (link OK / no response, device type, play state, total tracks, current track);
  badge distinguishes unreachable module from idle module.
- **`/api/validation` endpoint** — single-call hardware validation snapshot covering drive,
  dome link, audio, and RC source health; useful for scripted validation sessions.
- **Failsafe timing telemetry** — trigger-to-zero latency fields added to `/api/status`
  (`failsafeTriggerMs`, `failsafeZeroMs`, `failsafeTriggerToZeroMs`, `failsafeTriggerSource`);
  makes the 200 ms safety requirement measurable rather than inferred.
- **Hardware validation script** — `tools/phase4_hw_check.py` runs a repeatable validation
  pass against a connected robot and writes machine-readable and human-readable reports.
- **Heap fragmentation monitoring** — `heapLargestBlock` tracked in `SafetyMonitorTask`
  and exposed in `/api/status` and `/api/health`; catches fragmentation before total-free
  looks healthy but WiFi cannot allocate a contiguous block.
- **`protoArtoo_prod` build environment** — AP-only WiFi mode (`PA_ENABLE_STA_WIFI=0`) for
  field deployment; saves ∼7 KB steady-state heap vs the default STA client mode.

### Changed
- **`/api/config` response is now grouped** — fields organized into `drive`, `rc`,
  `components`, `dome`, and `system` objects; all frontend pages updated accordingly.
- **Platform upgraded to IDF 5.5.2 / arduino-esp32 3.3.7** via
  `pioarduino/platform-espressif32@55.03.37`. Zero source changes required. Brings the
  IDF 5.x WiFi stack, improved power management, and 2026 toolchain improvements.
- **AsyncWebServer and AsyncTCP migrated to `ESP32Async/` maintained fork**
  (`ESPAsyncWebServer@3.10.3`, `AsyncTCP@3.4.10`). The `me-no-dev/` namespace has had no
  activity since 2022; the `ESP32Async` fork carries SSE memory-leak fixes, proper TCP
  buffer teardown, and `SSE_MAX_QUEUED_MESSAGES` queue-cap support.
- **WiFi mode is now a build-time decision** — AP and STA modes are mutually exclusive;
  the prior AP+STA simultaneous mode with AP-fallback logic is removed. `PA_ENABLE_STA_WIFI`
  selects the mode at compile time; a `#error` guard fires if credentials are missing.
- **Soft-UART TX is now interrupt-protected** — each byte is wrapped in a portMUX critical
  section, preventing FreeRTOS tick and WiFi radio ISRs from stretching bit periods and
  corrupting DY-SV5W frames.
- **Single SBUS receiver is now selectable at runtime** — in `single_sbus` mode, the active
  physical receiver (`sbus1` / `sbus2`) can be changed from the RC page and persists across
  reboots without modifying any existing channel mappings.
- **Dashboard live status now uses SSE** — target pages subscribe to the existing
  `/api/events` stream instead of polling; reconnects automatically on visibility restore.
- **Shared web API client** — all pages use `PAApi` (`data/web_api.js`) for consistent
  error handling, timeout behaviour, and operator feedback.
- **Shared page chrome** — navigation and footer rendered from a single source (`shell.js`,
  `footer.js`); one edit point for all nav changes.
- **Footer shows both firmware and web bundle versions** — helps detect stale UI after a
  firmware flash or filesystem update.
- **`/api/config` JSON serialized via ArduinoJson** — eliminates the hand-sized 2 KB static
  buffer (`configJsonBuf`) that had caused production truncation twice; 2 KB BSS reclaimed.
- **RC diagnostics JSON also migrated to ArduinoJson** — eliminates `rcBuf[3072]` and the
  associated `WebEvents` task stack pressure that caused SSE-connect crashes.

### Fixed
- **DY-SV5W audio regression** — driver was sending wrong play opcode (`0x06` next-track
  instead of `0x07` play-specified), a self-cancelling stop sequence, and dual-dialect frame
  wrapping producing four frames per command. All corrected with source-verified opcodes from
  the DYPlayer/BetterDuino reference. `switchDrive` now uses the device the module reports,
  not a hardcoded SD/TF value (FLASH modules were silently breaking status queries on boot).
- **Volume not persisted** — `POST /api/audio action=volume` now writes to NVS; the Sound
  page slider hydrates from the persisted value on load instead of defaulting to 20.
- **WebEvents task crash under SSE load** — task stack was sized against an idle measurement
  taken before any SSE client connected, giving a false low-water mark. Corrected to 4096
  bytes; under-load measurement confirmed 1320 words free.
- **AsyncTCP stack canary faults on `/api/config`** — `CONFIG_ASYNC_TCP_STACK_SIZE=8192`
  in build flags prevents stack overflow when the config handler runs under connection load.
- **RC mode and channel changes now apply without reboot** — `RcInputTask` re-reads config
  each loop; mode and channel assignment changes from the web interface take effect
  immediately.
- **AP security in AP-only builds** — AP mode now requires `PA_AP_PASSWORD` from
  `src/secrets.h` with a minimum-length compile-time assertion; open AP removed.
- **Status payload overflow** — `buildStatusJson` returns failure on overflow and emits
  explicit fallback JSON instead of silently truncating; buffer sizes increased to 3072 bytes.
- **Optimistic mode/mood state** — mode and mood controls no longer flip state before API
  confirmation; on failure the UI rolls back to the last confirmed state.
- **`board_upload.before_reset` placement** — moved from `upload_flags` to `board_upload.*`
  in `platformio.ini`; esptool 4.x ignores `--before` when appended after `write_flash`.

### Hardware Validated
- DY-SV5W audio output on Artoo PCB: named sounds, random chatter, play/stop/volume,
  module status card, volume NVS persistence confirmed
- Dome ESC GPIO25 PWM path: spins correctly at 50/70/90% command from web API (unloaded and
  loaded ring tests); ESC baseline parameters confirmed and locked

### Deferred
- Dome serial link end-to-end (requires slip-ring connection to AstroPixelsPlus board)
- Audio edge cases: S2 enable/disable toggle, boot mood restore, mood interval NVS persistence
- Drive, hoverboard, and SBUS failsafe validation (hoverboard not yet connected)
- RC mapping and upload UX with physical transmitter/receiver

## [0.3.0] - 2026-03-16

### Added
- **ServoTask** for ARM1/ARM2 utility arms via LEDC PWM at 50 Hz
- **ServoComponentType** enum (NONE/MG996R/MG90S/RGB) with per-type calibration defaults
- **DomeTask** for dome motor ESC control (tested: ISDT ESC70) via LEDC PWM
- **RC diagnostics surface** with live channel visualization and SSE streaming
- **Three RC input modes**: `standard_pwm`, `single_sbus`, `dual_sbus` (runtime selectable)
- **Modular API architecture**: split into focused route handlers (api_config, api_drive, api_estop, api_rc, api_servo, api_status, api_system)
- **New web pages**: drive.html, dome.html, servo.html, rc.html, firmware.html
- **Dashboard operation mode** card: Driving ⇔ Stationary toggle via `/api/mode`
- **Dashboard mood selector** card: Quiet, Mid-Awake, Full-Awake, Awake+ buttons
- **CH17/CH18 digital channel decoding** from SBUS flags byte
- **SBUS flag parsing helpers** (`sbus_flags.h`, `sbus_unpack.h`)
- **Marcduino RX parser** (`:OP`, `:CL`, `:MV`, `:SE30-:SE36`) for dome→body commands
- **RC channel binding model**: backbone bindings (fixed role) and trigger bindings (configurable action)
- **NVS-backed RC calibration**: min/center/max/deadband/reverse per channel
- **Version extraction script** (`tools/extract_version.py`): inject PA_VERSION from CHANGELOG at build
- **Project favicon** (`r2d2body-favicon.png`) on all HTML pages
- **Artoo Controller PCB photo** on setup page for reference
- **Terminology glossary** (`docs/terminology.md`)
- **336 native unit tests** covering LEDC math, dome math, servo helpers, SBUS flags, RC diagnostics, Marcduino helpers

### Changed
- Extended `RobotState` with servo, dome, and RC diagnostics fields
- Reduced API handler buffer sizes for memory optimization (heap 135KB free, 127KB min)
- Updated task initialization to create ServoTask and DomeTask on Core 1
- Refactored web_server.cpp into modular API route files
- Updated docs/api.md with new servo, dome, RC endpoints
- Updated docs/pin_map.md with servo/dome GPIO assignments
- Updated docs/failsafe.md with 5-layer safety model

### Fixed
- **SBUS flag bit positions**: `lost_frame` corrected to bit 2 (0x04), `failsafe` to bit 3 (0x08)
- **TWDT crash**: removed SSE onConnect log sync loop causing watchdog timeout
- **Memory optimization**: consolidated API buffers, reduced SSE body sizes
- **Button underline CSS**: exclude buttons from `[title]` selector
- **Live logs SSE**: restored log streaming via `copyNewLogLinesSince()`
- **OTA environment**: removed duplicate `lib_deps` override in `protoArtoo_ota`

### Hardware Validated
- Dual SBUS live data confirmed on real hardware
- Arm servos respond via SBUS CH4/CH5 triggers
- RC diagnostics page working with real transmitter input

### Deferred
- Dome ESC response validation (requires full wiring harness)
- SBUS Layer 1/2 failsafe deliberate tests
- Full hoverboard drive path validation

## [0.2.0] - 2026-03-13

### Added
- Bench-tested Phase 2 web stack with LittleFS-served `Home`, `Setup`, `WiFi`, `Firmware`, and `Serial` pages
- Expanded HTTP API with config, WiFi, serial, health, logs, manual-command, reboot, and OTA firmware upload endpoints
- Dashboard health surfaces including heap status, WiFi quality, movement status, live log console, and manual command controls
- Explicit browser-control mode for web drive commands when SBUS is unavailable
- NVS-backed config persistence for web-configurable settings, surviving reboot
- OTA firmware upload flow and browser-triggered reboot support on the bench controller
- Native test coverage for web/API helper parsing, log buffer behavior, and JSON formatter helpers

### Changed
- Switched the Phase 2 bench controller board definition to `wemos_d1_mini32` with a reliable upload workflow at `115200`
- Added leveled USB debug logging with clearer boot health, WiFi bring-up, and web-server bring-up output
- Refined the multi-page UI toward a more coherent operator-facing control panel with less internal planning language
- Moved visible planning/status source-of-truth files into `tasks/`

### Fixed
- Corrected API/static route ordering so `/api/*` requests are no longer intercepted by the LittleFS static handler
- Fixed estop-clear route behavior on the live board
- Fixed dashboard hydration issues caused by removed `Live Feed` dependencies in the page script
- Fixed malformed status payload metadata used by firmware version/uptime footer rendering

## [0.1.0] - 2026-03-12

### Added
- PlatformIO project scaffold for the ESP32 firmware, including `.clang-format`,
  OTA partitions, and build/test environments
- Core firmware headers for shared state, config, Marcduino constants, task
  interfaces, and web server declarations
- Hoverboard Gen2.x drive transport and `DriveTask` with 50 Hz command output,
  zero-frame behavior, and watchdog feeding
- Custom RMT-based dual SBUS input stack with mapping helpers and
  `SBUSInputTask` support for HOTRC SBUS-A receivers
- `SafetyMonitorTask` boot/runtime orchestration and native unit tests for
  hoverboard frame packing and SBUS math
- Phase 1 WiFi AP and minimal REST API with `POST /api/estop`,
  `POST /api/estop/clear`, and `GET /api/status`
- Phase 1 documentation covering API behavior, failsafes, traced PCB pin map,
  topology, project setup, and body-dome serial link references

### Changed
- Confirmed traced UART routing so hoverboard drive uses `UART1` on PCB header
  `S1`, while dome serial is reserved for `UART2` on header `S3`
- Replaced earlier UART-based SBUS assumptions with the in-repo RMT decoder
- Pinned `ESPAsyncWebServer` to `3.6.0` and `AsyncTCP` to `3.3.2`

### Fixed
- Prevented `Serial` logging while holding `robotStateMux` in the SBUS watchdog path
- Removed handler-time dynamic allocation from the `/api/status` response path
- Corrected dome heartbeat helper logic so "connected" requires a real heartbeat
