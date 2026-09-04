# protoArtoo Context

This context defines the project language for protoArtoo release planning and validation so public status, internal task notes, and implementation work use the same terms.

## Language

**Phase 5**:
The v1.0.0 release-hardening umbrella that may include validation work, defect fixes, documentation, release packaging, architecture-risk reduction, and useful operator-facing features while hardware validation remains incomplete.
_Avoid_: feature freeze, release candidate

**Full Hardware Validation**:
Integrated confirmation on a complete droid build with the relevant physical peripherals connected.
_Avoid_: bench verification

**Software Verified**:
The relevant firmware build, native tests, and static checks passed without implying upload to an ESP32.
_Avoid_: bench verified, bench tested

**Controller Upload Verified**:
The firmware was uploaded to an ESP32 controller on the bench and basic web, API, or runtime smoke checks passed.
_Avoid_: software verified, bench verified

**Full Hardware Verified**:
The behavior was tested on the integrated droid hardware for the affected subsystem.
_Avoid_: bench verified, controller upload verified

**Verification Label**:
A description of the evidence a piece of work actually has - never a gate on closing
it. Work closes on the strongest evidence the available hardware can produce, and
`full-hardware-required` records the remaining droid-only exposure rather than
blocking. Droid confirmation is a desirable outcome for a hobby-scale project, not a
precondition (operator decision, 2026-09-01; see AGENTS.md "Verification and
Reporting").
_Avoid_: hardware gate, droid gate, blocked pending hardware verification

**Public Verification Wording**:
Short evidence phrasing in public docs that describes what was actually tested without exposing internal process labels.
_Avoid_: software-verified, controller-upload-verified, full-hardware-verified

**v1.0.0 Release Boundary**:
The community release can ship with documented hardware-validation gaps if automated checks pass, no known blocking safety or operational defects remain, and deferred hardware areas have explicit closure checklists.
_Avoid_: fully validated droid release, complete integrated drive validation

**Phase 5 Closure**:
Phase 5 ends when `v1.0.0` is tagged. After that, new work follows the normal `main` branch plus pull-request workflow instead of remaining on the release-hardening branch.
_Avoid_: permanent phase branch, open-ended Phase 5

**Post-Release Main Workflow**:
After `v1.0.0`, docs, chore, and agent-facing maintenance commits may go directly to `main`; substantive firmware work should still use pull requests.
_Avoid_: blanket PR requirement for all changes, phase-branch habits after release

**Post-Release Main Workflow Status**:
This branch policy is defined now as the future `main` workflow, but it remains inactive until `v1.0.0` is tagged and Phase 5 closes.
_Avoid_: applying post-release rules before release cutover

**Issue Labels**:
Use separate domain labels and work-type labels. Domain labels describe the subsystem (`drive`, `rc`, `audio`, `dome`, `config`, `ui`, `safety`). Work-type labels describe the kind of work (`bug`, `verification`, `cleanup`, `release`, `feature request`).
_Avoid_: one mixed label set, developer-only labels

**Issue Triage**:
Keep triage light and honest. New issues should be reviewed normally, labelled clearly, and moved forward without extra intake gates or waiting queues.
_Avoid_: heavy process, strict gating, separate feature-request intake pile

**Issue Rejection**:
If an issue or feature request is rejected, leave an honest written reason in the discussion. Do not close it silently just because it is inconvenient or not personally preferred.
_Avoid_: tag-only closure, silent dismissal, preference-based rejection without explanation

**Issue Submission**:
Keep submissions lightweight. Do not require rigid forms or heavy templates for feature requests or issues unless a specific case truly needs more detail.
_Avoid_: strict intake forms, heavy templates, gatekeeping

**Issue Templates**:
Use a few minimal Markdown templates with a friendly intro and light emoji. Keep them human-readable and informal rather than rigid or form-like.
_Avoid_: strict issue forms, bureaucratic wording, overly long templates

**Release Validation Matrix**:
A public release-note table that states each subsystem's tested evidence and remaining checks in plain language.
_Avoid_: internal verification labels, task checklist dump

**Public Deferred-Check Wording**:
Use plain release-note language such as "drive hardware checks are still to be completed" rather than internal process labels like "deferred" or "bench verified".
_Avoid_: deferred, bench verified, bench-tested

**Public Release Operator**:
A non-developer who installs and runs protoArtoo from downloadable release artifacts and a browser-accessible control surface, without editing firmware source, providing private build-time credentials, or building locally.
_Avoid_: developer user, self-build operator, contributor

**WiFi Provisioning**:
The operator-facing onboarding flow that lets a Public Release Operator reach a newly flashed controller without private build-time credentials and choose the controller's ongoing WiFi mode.
_Avoid_: editing `secrets.h`, CI-provided WiFi credentials, source-build setup

**WiFi Client Mode**:
The recommended ongoing WiFi mode where the controller joins the operator's own WiFi network and is reached as a client on that network.
_Avoid_: compile-time-only STA, developer-only client mode

**Standalone AP Mode**:
An operator-selected WiFi mode where the controller hosts its own WiFi network, either as its normal ongoing posture or as a temporary field posture away from the home network.
_Avoid_: provisioning-only AP, failed setup state, developer-only production build

**Default AP Credential**:
The shared, documented bootstrap password used by public release firmware until the operator changes the controller's AP password.
_Avoid_: secret default, per-build credential, permanent security boundary

**Device WiFi Settings**:
The operator-selected WiFi posture and network credentials retained by the controller after provisioning.
_Avoid_: release-binary credentials, source-code credentials, CI credentials

**Developer WiFi Shortcut**:
A source-build convenience that lets a developer compile preferred WiFi defaults into a local firmware image without defining the public release networking contract.
_Avoid_: public release provisioning, operator setup path, required release credential

**Staged Network Switch**:
An operator-requested WiFi mode change that is saved first and takes effect through an explicit apply/reboot handoff rather than an in-place live toggle.
_Avoid_: live WiFi toggle, automatic fallback, hidden reconnect

**Network Recovery Mode**:
An explicit local recovery posture that temporarily starts WiFi Provisioning so an operator can repair Device WiFi Settings when the normal network path is unreachable.
_Avoid_: automatic STA fallback, guessed credential failure, source-build recovery

**Page Load Recovery**:
The web UI expectation that opening or refreshing a page does not make the controller progressively harder to reach. Normal use includes one or two browser tabs; development and browser testing may briefly use three. If loading takes longer or memory protection rejects work, the page must visibly show that it is loading or retrying, then recover or offer a working retry path without requiring a controller power cycle. A longer wait is acceptable when the UI clearly remains active. While the tab is visible, automatic retries may continue as long as each attempt is shown clearly and a Retry now action remains available; hidden tabs pause that work.
_Avoid_: endless Loading state, activity indicator with no meaningful status, refresh makes it worse, power-cycle recovery, telling the operator to limit normal tab use

**Page Recovery View**:
The minimum operator-visible state available as soon as the first page response arrives. It remains useful while the rest of the page is loading: it identifies the current loading or retry step, retries failed work with increasing pauses, and provides a Retry now action without depending on the remaining page resources having loaded successfully.
_Avoid_: blank page, spinner-only state, recovery controls that require the failed resource, retrying by refreshing the whole page

**Bounded Page Attempt**:
One finite try to load a page and its resources. A visible tab has at most one active attempt. Every request and queued item has a deadline; when the attempt fails, is replaced, or becomes unnecessary, its active and queued work is cancelled and released before a later retry starts. Waiting between retries creates no controller load.
_Avoid_: overlapping retries, abandoned requests, unbounded client queues, holding controller capacity during retry delays, relying on refresh to clean up old work

**Connection Admission**:
The controller-side outcome when a new connection is dropped before any HTTP request has been read. It carries no response, no reason and no retry hint, and it is blind to the URL by construction, so it cannot exempt any path. To a browser it is indistinguishable from an unreachable controller and legitimately surfaces as "No response from controller".
_Avoid_: Immediate Request Refusal, busy response, per-route exemption, Controller busy wording, a promise that safety paths are admitted

**Immediate Request Refusal**:
The controller-side outcome when a page request that has already been read cannot safely start. The controller does not queue or retain the request: it returns the smallest safe busy result and releases the attempt immediately. The Page Recovery View owns the wait and retry, and tells the operator whether the controller reported busy or did not respond.
_Avoid_: Connection Admission, controller-side wait queue, silent failure, indefinite request lifetime, starting expensive work before admission, treating every failed response as proof that the controller is busy

**Page Recovery Status**:
The plain-language reason and next action shown by the Page Recovery View. "Controller busy" is used only after an explicit Immediate Request Refusal; a timeout or connection failure is shown as "No response from controller". Each retrying state shows when the next attempt will start and keeps Retry now available.
_Avoid_: generic spinner, guessing that every failure means busy, raw HTTP or heap details, retry with no visible timing

**Busy Recovery Page**:
The smallest self-contained page returned when the controller can report an Immediate Request Refusal but cannot safely start the requested full page. It provides the Page Recovery View without loading another resource. It is not promised when the controller gives the browser no first response at all. Implemented as one fixed byte buffer (status line, headers, and HTML+inline-script body already concatenated at compile time) written directly to the raw transport, bypassing the normal response-object path; the same buffer is reused for every resource class rather than built per-class (ADR 0016).
_Avoid_: dependency on shared CSS or scripts, full application shell, claiming to handle a missing first response, error page with no retry path, a normal dynamically-assembled response object

**Recovery Capacity**:
A small, fixed, measured controller allowance kept available for one Busy Recovery Page when normal work is refused. Its cost is part of the normal memory baseline and does not grow with failures or retain refused requests. Exactly one reserved slot for the whole controller, shared across every resource class rather than one per class; refusals claim it before falling back to a plain connection abort, and release it on the same disconnect-completion boundary used for ordinary admitted requests (ADR 0016).
_Avoid_: per-failure growth, general request queue, unmeasured reserve, more than one recovery response at a time, one slot per resource class

**Recovery Retry Interval**:
The fixed `Retry-After` value carried on a Busy Recovery Page response, grounded in this board's own measured pressure-recovery time rather than generic web-service overload conventions. Set to 5 seconds against #54's evidence of ~10s observed recovery. The static page's own inline countdown is a literal baked from the same source constant, since browsers do not auto-honor `Retry-After` and the page cannot read its own response headers (ADR 0016).
_Avoid_: copying generic 30-120s rate-limit/maintenance conventions, a countdown value independent of the header value

**Resource Step Recovery**:
The page-loading behavior that keeps completed resource work, pauses dependent work at the first failed required step, and retries only that step. A page is ready only after every required step succeeds.
_Avoid_: whole-page reload for one failed resource, skipping a required resource, continuing dependent work after failure, reporting a partial page as ready

**Section Recovery**:
The page behavior after required resources are ready: each data-backed section reports and retries its own failed request while other successful sections remain usable. Controls that depend on missing or stale data stay unavailable until that section recovers.
_Avoid_: returning the whole page to Loading, erasing successful sections, enabling controls without required data, retrying unrelated requests

**Page Startup Order**:
The common loading order for a controller page: required resources first, then each section's first data attempt, then live updates and other background work. Background work may start once every section is either loaded or visibly waiting to retry; a failed section does not block it forever.
_Avoid_: overlapping resource, data, and live-update bursts, background work before visible page state, waiting forever for every section to succeed

**Browser Request Priority**:
The order in which a page starts controller requests. Latching Estop bypasses queued page work; other user commands go ahead of automatic loading and retries; required page startup goes ahead of background updates. User commands are not automatically retried.
_Avoid_: Estop waiting behind reads, user action waiting behind polling, background retry delaying page startup, automatic replay of a command

**Hidden Tab Pause**:
The quiet state entered when a controller page is no longer visible. The tab starts no new loads, retries, polls, or live-update connections; one active bounded request or already-sent user command may finish within its deadline. When visible again, the page resumes from its unfinished step.
_Avoid_: hidden polling, hidden retry loop, aborting a useful response only because visibility changed, restarting completed work on return

**Background Poll**:
A bootstrap-owned ongoing page work item that repeats on a caller-set cadence after page startup: it pauses while the tab is hidden, runs at most one attempt at a time, applies its caller's retry backoff, and can be stopped by its owner. The footer version display and the setup page's profiler card are Background Polls, not page sections.
_Avoid_: ad hoc interval loops, per-module visibility handling, a second retry mechanism beside the bootstrap

**Section Request Handle**:
The request surface a section loader receives from the Common Page Bootstrap: calls made through it automatically carry the section's cancellation signal and Operation Deadline category, so a loader does not thread the signal into each request by hand. The raw signal remains available for work the handle cannot express.
_Avoid_: manual signal threading in every loader, per-page cancellation ownership, a global abort lane

**Operation Deadline**:
The bounded lifetime assigned to one kind of controller request. Ordinary page work uses a short measured deadline; known longer operations use an appropriate longer deadline and visible progress. A deadline extends only when measurable forward progress occurs.
_Avoid_: one timeout for every operation, indefinite request, extending on meaningless activity, stopping healthy long work while progress is visible

**Response-Phase Watchdog**:
The bounded safety guard for an admitted, ordinary non-SSE HTTP response. It covers the response phase after application admission; it does not promise coverage of connection acceptance, request parsing or upload receipt, or intentionally long-lived live updates. The response phase begins at the first byte the controller writes, not at admission, so time spent building a body or receiving an upload is outside it. Implemented as a session send override, the one point every response byte passes through on this stack, so it covers the controller's own responses, the web library's, and static assets alike; a breach drops the connection and releases the request's in-flight slot (ADR 0024, superseding ADR 0020).
_Avoid_: end-to-end request watchdog, upload watchdog, SSE timeout, a guard that only covers routes the controller writes by hand, a breach that closes the socket but keeps the slot

**Page Load Memory Recovery**:
The controller condition after page-loading activity stops: request and connection counts return to their resting values, usable heap settles within a measured warmed range, failed allocations stop increasing, and the controller remains responsive without a panic, reboot, or power cycle. The concrete pass/fail envelope (heap/largest-block range, cooldown timing, resting-count definitions, failed-allocation rule, stop conditions) is locked in ADR 0017, scoped to production builds only; the rapid-refresh/3-tab-burst and Mobile Safari scenario classes remain explicitly open pending further evidence rather than carrying invented numbers.
_Avoid_: judging only whether a page appeared, comparing only with cold boot, accepting a lower heap level after every cycle, hiding recovery behind a restart, applying the same envelope to the profiler build

**Page Failure**:
An operator page is unusable or fails to finish loading while the controller's diagnostic HTTP endpoint still responds. Diagnostic reachability does not make the page healthy or recovered.
_Avoid_: recovered page, healthy control surface, HTTP blackout

**HTTP Blackout**:
The operator UI and diagnostic HTTP endpoint are both unreachable while the controller still responds at the network layer. It is a stop condition, not successful pressure shedding or Page Load Memory Recovery.
_Avoid_: page failure, network outage, self-recovery

**Survival Path**:
The serial Console: the one operator surface that still answers when HTTP admission refuses everything. It depends on no network, no heap admission decision and no browser, which is what makes it a guarantee rather than a concession. Distinct from the diagnostic HTTP endpoint, which is an HTTP concession under pressure and still subject to a floor.
_Avoid_: survival set, /api/health as a survival surface, "the health endpoint always answers", diagnostic HTTP endpoint

**Diagnostic Floor**:
The lower largest-free-block threshold applied to the small set of read-only diagnostic paths, so they keep answering under pressure that already refuses ordinary requests. The set is an exact-match list, not a pattern; membership is a deliberate decision per path, and an endpoint's payload being small does not put it in the set.
_Avoid_: admission floor, health endpoint exemption, "small responses are exempt"

**Power-Cycle Recovery**:
Restoring controller HTTP service by physically removing and restoring controller power. It is the only recovery demonstrated so far after an HTTP Blackout and is evidence of failed self-recovery, not an acceptable recovery mechanism.
_Avoid_: browser retry, page refresh, self-recovery

**Refresh Resilience**:
The expectation that a normal browser refresh completes or visibly recovers, while rapid repeated refreshes may shed intermediate attempts without harming the controller. After refreshing stops, the final page recovers and the controller returns to Page Load Memory Recovery.
_Avoid_: promising every overload attempt completes, retaining abandoned refreshes, crash or reboot under refresh pressure, requiring a power cycle after the pressure ends

**Common Page Bootstrap**:
The single shared loading and recovery behavior used by every controller page. Each page declares its required resources and sections, while the bootstrap provides the same Page Recovery View, ordering, retry, and visibility rules without requesting a required resource more than once. Validated state model, page rollout order, Operation Deadline categories, generalization gates, and stop/rollback rules are locked in `docs/page-load-recovery-architecture.md` and ADR 0019.
_Avoid_: page-specific loader copy, external-only recovery dependency, duplicated stylesheet request, different recovery behavior between pages

**Browser Load Profile**:
The expected controller web workload: primarily one visible Firefox tab, with a second ordinary tab supported; development may add a parallel Playwright Chromium session and briefly reach three tabs. Mobile Safari is a focused WiFi recovery check while the controller is serving its own AP, not the general browser-test baseline.
_Avoid_: treating mobile Safari as the common client, testing only Chromium, requiring operators to keep exactly one tab, unbounded browser concurrency

**Supported ESP32 Board**:
The dual-header ESP32 D1 Mini clone required by the current Artoo Controller PCB — canonically the **artoo-esp32** build target. Firmware and web reliability must work within this board's memory limits; ESP32-P4 Target support does not relax the current requirement.
_Avoid_: official Wemos board, temporary development board, waiting for newer hardware

**Web Server Library**:
A replaceable implementation choice, not a compatibility promise. It may be patched or replaced when needed to provide Page Load Recovery and protect memory on the Supported ESP32 Board.
_Avoid_: preserving ESPAsyncWebServer at the expense of reliability, treating the current library as part of the public API

**Live Page Updates**:
The status, RC, and log updates delivered through `/api/events`, over a stream this project owns rather than one a web stack supplies. A client that cannot keep up within the send deadline is dropped so the others keep receiving; the stream never queues or blocks on a slow reader, because the reader stalling it is usually the single operator who needs it (ADR 0030). Serving the endpoint is not the same as providing Live Page Updates: a vendor EventSource class delivers the same URL while blocking every viewer behind one stalled socket. Keep this interface working while server and transport alternatives are measured. It may change only when tests on the Supported ESP32 Board show that a replacement improves memory behavior and Page Load Recovery without losing equivalent page behavior or silently breaking integrations.
_Avoid_: removing SSE on suspicion, replacing one long-lived connection with rapid polling, breaking the endpoint without measured benefit, a vendor EventSource class, a stream that queues or blocks on a slow client, treating endpoint reachability as proof the stream is this one

**Unprovisioned Controller**:
A controller that has no valid Device WiFi Settings and therefore cannot yet choose its ongoing WiFi posture.
_Avoid_: fresh public release, first-time binary, factory firmware

**protoR2link**:
The dome-body link subsystem. The canonical operator-facing name for the connection between the body controller and the dome controller. Used in the web UI component label and in task/issue language.
_Avoid_: dome link, dome serial, dome wifi, dome connection

**protoR2link Primary Transport**:
The UART slip ring connection (physical serial over GPIO33/34) is the intended primary channel for dome-body communication. When the slip ring is connected and the dome is reachable, the system uses this path.
_Operator label_: "UART (slip ring)"
_Avoid_: UART2, serial transport, preferred transport

**protoR2link Fallback Transport**:
WiFi UDP is the fallback channel used when the UART slip ring is unavailable or the dome is unreachable over serial. The firmware probes periodically for the slip ring and promotes it back to primary when recovered.
_Operator label_: "WiFi (fallback)"
_Avoid_: WiFi transport, primary WiFi, preferred WiFi

**protoR2link Transport Visibility**:
The active transport (UART slip ring or WiFi fallback) must be surfaced to the operator in the main dashboard dome status badge and the protoR2link component panel. The transport field is available in the `/api/status` `dome_link.transport` response field.
_Avoid_: logging-only transport indication, setup-page-only visibility

**protoR2link Arbiter**:
The body-side decision module that owns protoR2link transport selection: it promotes the UART slip ring, falls back to WiFi, schedules slip-ring probes, gates the heartbeat cadence, and decides sleep-sync sends. Pure logic — it consumes time and link-liveness inputs and emits transport actions; the concrete transports execute those actions. Named after the drive arbiter convention (a module that picks one winner among competing sources).
_Avoid_: transport manager, link state machine, connection manager

**DM:* Sequence**:
A named, time-ordered choreography (for example DM:VADER, DM:CANTINA) that combines sound, dome rotation, body and dome panel motion, and dome light/logic effects under one timeline.
_Avoid_: macro, script, dome animation (a dome animation is one effect inside a sequence, not the whole sequence)

**Sequence Coordinator**:
The body-side owner of DM:* choreography. It applies lookup precedence, advances the timing cursor, and dispatches each step; dome panel steps use calibrated panel-intent commands rather than raw servo-slot pulses.
_Avoid_: dome sequencer, raw-pulse panel choreography

**Catalog Authority**:
The principle that the body owns the operator-facing DM:* namespace, sequence definitions, trigger routing, timing, and Learned Sequence override precedence, while the dome owns calibrated execution of panel intent.
_Avoid_: raw servo pulse authority, dome owns all routing

**Panel Intent Command**:
A high-level dome panel command (`:OP`, `:CL`, or `:OF`) that addresses a logical panel or group and lets the dome apply its calibrated movement behavior.
_Avoid_: raw servo pulse command, `:SM` sequence authoring

**Suppression Window**:
The interval during an active sequence in which the body holds its own idle-random behavior (random dome rotation and random audio) without changing those subsystems' configured modes. Replaces the dome's former seqon/seqoff signalling.
_Avoid_: random disable, seqon/seqoff

**Sequence Preemption**:
The rule that a new DM:* request cancels the active sequence (with minimal safety cleanup) and starts the new one immediately. Estop always aborts.
_Avoid_: queueing sequences, ignore-while-busy

**Track Stop**:
Stopping the current audio playback only, preserving the configured random/idle mood and bumping the idle cadence so chatter resumes after the normal anti-spam beat. The stop semantics for every non-mood surface: sequence terminal/abort cleanup, the web stop action, and the dome BD:RESET cue (ADR 0010).
_Avoid_: stop everything, bare "stop"

**Quiet (mood stop)**:
Stopping playback and disabling random/idle mood, owned exclusively by the mood system (`$s`, SE10). The only surface allowed to change the configured idle mood; never used for sequence cleanup.
_Avoid_: sequence terminal `$s` authoring, hard stop

**Bounded Audio**:
A sequence audio step whose track is Track-Stopped at any sequence termination, normal or abnormal (`FX_AUDIO_BOUNDED`). Distinguishes long named tracks (bounded to the show) from short category vocalizations, whose ring-out past sequence end is preserved. Factory catalog opts in per step; Learned Sequence audio steps default to bounded with a JSON opt-out (`boundAudio: false`).
_Avoid_: global stop-on-end audio semantics, clipping category ring-out

**Named Track (authoring)**:
A canonical, config-backed sound role (the AudioNamedTracks namespace, for example scream, leia, cantina) used as the authoring surface for a sequence's sound steps, so a sequence references a sound by role and follows the operator's configured track number. The Marcduino $NNN and $-letter dialect stays valid at command boundaries for interoperability.
_Avoid_: raw track-number authoring, dollar-command-only authoring

**Factory Sequence**:
A built-in DM:* sequence compiled into the firmware (the C++ catalog). The trusted, PR-reviewed expert surface; exempt from Protocol Check's meta rules.
_Avoid_: default sequence, stock sequence

**Learned Sequence**:
A DM:* sequence defined as a JSON file on the controller filesystem (/seq/), created or edited without reflashing, accepted only after passing Protocol Check, and executed by the same coordinator engine as a Factory Sequence.
_Avoid_: custom sequence, user macro, script

**Retrained Sequence**:
A Learned Sequence bearing a Factory Sequence's name. It shadows the factory one on every trigger path (RC, web, dome RX) via runtime-first lookup precedence (runtime -> catalog -> alias -> fallback).
_Avoid_: override sequence, patched sequence

**Memory Wipe**:
Deleting a Retrained Sequence, after which the Factory Sequence programming returns instantly (its catalog entry resurfaces in lookup).
_Avoid_: reset, restore, revert

**Migrated Sequence**:
A Factory Sequence translated from another community project (a nod to the R2 Builders Guild) and committed into the C++ catalog via the GitHub issue -> PR migration workflow. Not an operator runtime import (see ADR 0007).
_Avoid_: Guild Sequence (the retired runtime-import tier), imported macro, ported sequence

**Lineage**:
The provenance of a Migrated Sequence -- source project, origin URL/commit, and license -- recorded in a catalog code comment and docs/sequence-credits.md. Not a runtime meta block.
_Avoid_: runtime meta block, metadata, credits

**Protocol Check**:
The pure safety validator every Learned Sequence passes on save: name/command/structure bounds, retrain coherence, and conservative effect-class inference. Estop, suppression, and auto-reset remain engine-level invariants the format cannot express a bypass for.
_Avoid_: linter, schema check, sanitizer

**Dome Layout View Model**:
The canonical element IDs and generic capabilities the body editor and Sequence Coordinator use to reason about what exists on the connected dome and what an operator may select. Sourced from the dome's `/api/dome/layout` when connected, with the vendored MK4 model as offline fallback. It is a reasoning and rendering surface, not a saved-sequence storage format.
_Avoid_: panel model storage, structured step format, persisted canonical IDs

**Saved-Sequence Storage**:
Learned Sequence dome steps persist as Panel Intent Command strings (`{ "type": "dome", "cmd": ":OP01" }`) per ADR 0008. The Dome Layout View Model does not change this: canonical element IDs and capabilities are resolved to command strings before save and run. Structured per-step storage stays deferred until a separate protoArtoo ADR supersedes ADR 0008.
_Avoid_: persist by canonical element ID, structured step JSON, dual-write storage

**Coordinator Resolution**:
The body-owned translation between the Dome Layout View Model (canonical element ID + capability, for example `P1 + open`) and the persisted and executed Panel Intent Command string (for example `:OP01`). The dome layout contract never exposes command strings, servo slots, or channels; the command mapping lives only in the protoArtoo coordinator.
_Avoid_: dome owns command mapping, raw command strings in the layout contract

**Panel Command Target**:
The body-owned value (ring numeric such as `01`, or pie alias such as `P1`) held in the static `PANEL_COMMAND_TARGETS` map keyed by canonical panel ID and panel kind. Combined with a capability prefix (`:OP`/`:CL`/`:OF`) it forms a Panel Intent Command. The map is bounded to the MK4 commandable set; a layout element marked commandable but absent from the map is shown as unmapped, non-actionable, with a diagnostic, and never authored.
_Avoid_: deriving command targets from aliases, guessing unmapped commands

**Element Alias**:
An alternate name for a dome element (for example `FHP` for `HP1`) used only for vocabulary, display, and search. Aliases never carry command semantics; deriving any behavior from an alias re-introduces the hidden coupling removed from the dome layout contract.
_Avoid_: alias as command source, alias-driven behavior

**Layout Read-Model Boundary**:
The Dome Layout is an editor-time read-model artifact: it is parsed, validated, and cached only in the browser. The body firmware relays its bytes without parsing, and runtime sequence execution (RC, web, dome RX triggers) never depends on the layout or the browser cache. Saved sequences stay command-string based and run unchanged whether or not a layout was ever fetched. Runtime availability awareness, if ever needed, is a future compact non-geometry status summary, not firmware layout parsing.
_Avoid_: firmware parses layout geometry, runtime gated on layout cache, layout as a control protocol

**Editor Availability Gate**:
The browser-editor rule that the connected dome layout gates new authoring but never invalidates existing saved content. New picker authoring requires `in_layout && commandable && mapped && active && !disabled`; otherwise the element is visible-but-not-actionable, or hidden when `in_layout:false`. Existing saved steps always load, edit, and save, carrying non-blocking advisory warnings by severity tier: `inactive` (advisory, not currently commandable), `disabled` (maintenance, operator-suppressed), `in_layout:false` (layout mismatch), `unmapped` (coordinator cannot author new steps). Protocol Check never gains an availability dependency.
_Avoid_: hard-block save on unavailable target, availability inside Protocol Check, disabled invalidates saved step

**Panel Group**:
A body-owned coarse authoring concept (All, Pie, Ring) that resolves to group Panel Intent Commands (`:OP00`/`:OP14`/`:OP15` and their `:CL`/`:OF` forms) and drives the `piesOpen`/`ringOpen` latches from ADR 0008. Groups are not Dome Layout elements; the picker derives visible group membership from each element's `panel_kind`. Group availability stays coarse: controls are offered whenever the panel picker is available, are never blocked by inactive or disabled members, and show an advisory only when zero members are currently available.
_Avoid_: group as a layout element, per-group safety gating, dome-owned groups

**Layout Fallback Hierarchy**:
The browser's ordered choice of which Dome Layout to render, separating geometry freshness from runtime-state freshness. (1) Live: body proxy returns `200` with a supported `schema_revision` -> use geometry and runtime availability. (2) Cached live: live fetch fails (`503`, timeout, invalid JSON) but `localStorage` holds a prior live layout with a supported schema -> reuse the cached geometry but mark runtime availability stale/unverified. (3) Vendored MK4 fallback: no usable cache -> render the offline MK4 model with runtime availability unverified. (4) Unsupported schema -> vendored fallback plus a visible warning; never partially trust geometry or state from an unsupported schema, including anything cached from one. Geometry may be cached or stale; runtime availability is trusted only when freshly live.
_Avoid_: trusting stale runtime availability, partial trust of unsupported-schema data

**Apply Core**:
A pure module behind a write path: it reads parameters through a Param Source (a function-pointer name lookup), validates and applies them onto a working snapshot, and returns field-level errors, an applied-fields record, and plain-data actions. Its side effects live in one Commit Step shared by every adapter; the HTTP handler and the Controller Console are transport adapters over the pair and own only their own response rendering (ADR 0011, amended by ADR 0034). The write-path counterpart of the pure GET JSON builders; cores: `api_config_apply`, `api_rc_map_apply`, `api_audio_apply`.
_Avoid_: handler helper, inline lambda validation, validation util, handler-owned side effects

**Commit Step**:
The transport-free side-effect sequence that completes an Apply Core's operation - runtime-state synchronization, Commanded Mode setters, persistence where required, the canonical log and result effects - kept beside its Apply Core (one per core, never a global commit function), owning the serialization of that operation so two adapters cannot interleave a write, and called identically by the HTTP handler and the Controller Console, so that no adapter carries its own copy of the effects or of the lock (ADR 0034; ADR 0011 amended 2026-09-04). It answers with a plain outcome and refreshes the caller's Working Snapshot rather than returning a second one.
_Avoid_: post-apply block, handler tail, per-adapter persistence, global commit function, adapter-held lock, snapshot-returning outcome

**Working Snapshot**:
The one per-request copy of the configuration that a write acts on: the Apply Core validates and applies onto it, the Commit Step commits it and refreshes it with the committed state, and the adapter renders from it. A write makes exactly one; nothing below the seam makes another. Distinct from a Zone Snapshot, which is a read of live state (ADR 0011).
_Avoid_: by-value snapshot, post-commit copy, "the snapshot" without saying which

**State Zone**:
A commented block of `RobotState` fields with exactly one owning writer (a task or the failsafe gate). The owner writes its fields directly; every multi-field read crosses the seam through the zone's snapshot (ADR 0012). Zones make the shared struct navigable: to change a field, find its zone; to read related fields consistently, capture its snapshot.
_Avoid_: global blackboard access, ad hoc multi-field reads

**Commanded Mode**:
A `RobotState` field legitimately written from multiple surfaces (RC binding, web page, Controller Console, dome cue, boot init): stationary, sleep, active mood, Non-RC Control. Commanded Modes are written only through `commanded_modes` setter helpers, which own the transition rules (for example the stationary-release drive-on cue) and the config-cache sync. Live toggles sync the cache by field, never NVS and never a whole-snapshot round trip.
_Avoid_: inline mode writes, per-surface transition rules, snapshot round trip for one field

**Zone Snapshot**:
The atomic multi-field read for a State Zone: a plain struct plus `copy<Zone>Locked()` (caller holds the mux) and `capture<Zone>()` (takes the mux). Consumer captures compose several zone copies inside one critical section, so a page response reads one generation of state. First instance: `FailsafeDiagnostics` (ADR 0012).
_Avoid_: field-by-field reads across separate critical sections

**Audio Config Map**:
The canonical home of the config-to-audio schema knowledge: the ConfigSnapshot to AudioPlaybackConfig mapping, named-track projection, chirp NVS-key tables, `$`-command table, binding unpackers, and the ConfigReader-seamed binding refresh (ADR 0013). Both the audio task middle and the api_audio Apply Core consume it; adding a sound slot or category starts here. The playback policy stays config-free behind it.
_Avoid_: per-surface mapping copies, mapping tables in task files or handlers

**Step Core**:
A pure `step(state, inputs) -> actions` module that owns a task's per-tick decisions: the task loop gathers inputs, calls the step, and executes the returned plain-data actions. Decisions live in the core; execution outcomes stay in the loop adapter. Instances: the protoR2link Arbiter (ADR 0005) and the Audio Step Core.
_Avoid_: state machine class, task helper, manager

**Audio Step Core**:
The audio task's Step Core (`audio_task_step`): it owns the enable/disable/sleep/init-retry lifecycle transitions, command-to-playback-request translation with sleep gating and relative volume, playback-policy invocation, and the gating of status/catalog work. The task loop is its adapter and owns driver calls, dome-UART arbitration, and RobotState audio-zone writes.
_Avoid_: audio lifecycle manager, audio coordinator, dispatch switch

**Component Toggle**:
A runtime `components.*` setting declaring whether a hardware subsystem is fitted and in use. Off means inert, not merely unconstructed: the disabled subsystem performs no recurring per-tick decision work, no recurring writes to shared safety state, no recurring queue sends or log emission, and spends no ongoing CPU or memory on its behalf. One-time transition work at boot is allowed. A toggle change is a Staged Component Switch: it is saved immediately but takes effect at the next boot, so tasks read their toggles once at startup rather than every iteration.
_Avoid_: unconstructed-only off, construction-gate-only toggle, live per-tick toggle reads, disabled subsystem reporting signal events

**Epic Branch**:
A rare long-lived branch (`epic/<name>`) holding all work of one multi-ticket epic issue. Sub-issue slices are committed directly to it; it reaches `main` only through a PM-approved PR at epic closure or an explicitly PM-called milestone merge. Scoped to one epic issue and coexisting with ordinary short-lived branches — not a development phase.
_Avoid_: phase branch, dev branch, per-ticket PRs inside an epic

**artoo-esp32**:
The canonical name for the build target pairing the classic-generation ESP32 D1 Mini clone with the artoo.uk Artoo Controller PCB (env/variant id `artoo_esp32`). A fully supported, first-class target.
_Avoid_: classic, legacy board, clone build

**ESP32-P4 Target**:
The chip-level build target covering any ESP32-P4-based controller board. It owns chip-wide facts, including the lack of a native radio and need for an external network-backend seam, but not a particular companion chip or transport (ADR 0028).
_Avoid_: firebeetle target, Hosted WiFi as a chip property, naming the chip layer after one board

**Board Variant**:
The per-physical-board layer under a chip target: pin map, fitted devices, transport/reset wiring, lifecycle requirements, quirks, and one build environment. Adding a variant costs a pin map, a build environment, and a size-budget entry that also tells the toolchain which chip the environment builds for (ADR 0028).
_Avoid_: board port, per-board fork

**firebeetle2**:
The Board Variant for the DFRobot FireBeetle 2 ESP32-P4 development board, including its fitted ESP32-C6, C6-over-SDIO transport, reset wiring, and co-processor lifecycle. The name and those topology facts refer only to that physical board.
_Avoid_: firebeetle2 for chip-wide concepts, throwaway mule

**Board Component Label**:
A per-Board-Variant display string, declared in `include/component_labels.inc`, naming where a Component Toggle's subsystem is physically connected on that specific board — e.g., artoo_esp32 shows "S1" for Drive. Shown to the operator as supplementary detail (such as a tooltip), never as the toggle's canonical name; a board may omit the label where no established legend exists (ADR 0033).
_Avoid_: component name, toggle name, PCB silkscreen text as the toggle's identity

**Builder Recommendation**:
The project's purchase advice about a Board Variant: whether the developer docs tell a builder to buy that board. Purely a statement in builder-facing documentation — it changes no code, no Board Capability Gate, and no support level, and a board carries the same support whichever way it reads. It turns on things a Board Capability Gate deliberately cannot attest (ADR 0029): retail unit consistency, whether a defective unit has a realistic recovery path, and what that recovery costs a builder who is not an equipped expert.
_Avoid_: recommended board (collides with the recommended ongoing WiFi mode), pass tier, support tier, certification

**Board Capability Gate**:
A compile-time `PA_CAP_*` declaration of what topology a board's fitted hardware can support — a single yes/no fact, or (per ADR 0029's 2026-08-26 amendment) a set of mutually-exclusive supported options with one default, such as a Board Variant's drive backend. It controls linking and not-on-this-board UI state; it does not attest successful co-processor provisioning, boot, initialization, or runtime reachability (ADR 0029).
_Avoid_: compile-time component toggle, feature flag, capability as a runtime setting, runtime-ready signal

**Build Feature Flag**:
A compile-time `PA_*` flag, chosen per build environment and always defined as 0 or 1, declaring whether this firmware image was built with an optional feature such as the heap profiler. It is a developer choice about the image — not a fact about the board, and not an operator setting; a feature built out is absent from the image, not merely off.
_Avoid_: build-stripping flag, developer toggle, compile-time component toggle, feature flag (unqualified)

**Feature Availability**:
The compile-time answer to whether a feature exists in the running image, derived from the Board Capability Gate and the Build Feature Flag the feature requires: not on this board, not in this build, or present. A present feature that has a Component Toggle is then off or on; a present feature without one is simply included — it has no on or off, and not every feature visibly inhabits all four states. Operators see "not in this build" as *Not included*, because a builder reads "build" as the droid. Availability is declared by the image and reported to the browser once; it is never discovered by probing endpoints, and it says nothing about whether fitted hardware is reachable at runtime.
_Avoid_: endpoint probing, feature detection, available (unqualified — that word names runtime reachability), hidden feature, on/off for a feature without a Component Toggle, "build" in operator-facing copy

**Framework Envelope**:
The set of framework (ESP-IDF / Arduino core) facilities a build environment compiles out at the `platformio.ini` boundary — declared per environment, every switch revert-ready in place. It is invisible to Feature Availability and reported nowhere: not a Board Capability Gate (the silicon may well have the facility), not a Build Feature Flag (no `PA_*` flag, nothing the project built in or out), and not a Component Toggle (the operator cannot change it). What an operator can notice from it belongs in the operator docs in plain language.
_Avoid_: capability envelope, pruning, stripped features, capability (unqualified), feature flag (unqualified)

**Hosted WiFi**:
A network backend in which a separate wireless co-processor serves WiFi through ESP-Hosted. On firebeetle2 it uses the board's fitted ESP32-C6 over SDIO; it is not intrinsic to ESP32-P4, and its capability gate does not prove runtime readiness.
_Avoid_: ESP32-P4's native WiFi, universal P4 C6/SDIO topology, runtime-ready capability

**Network-Optional Operation**:
The droid's defined functions — RC drive, dome, sound, servos, and every safety path — never depend on a network backend being fitted, configured, or reachable. A Board Variant may declare no network backend at all. A network that is absent or down removes only the web UI and web-only operations; it never restarts the controller and never degrades a droid function. Persistent network failure is announced by the droid itself (sound, dome text, serial log), not only through the web UI (ADR 0032).
_Avoid_: network as a safety dependency, automatic controller restart on network failure, counting the web UI as a droid function, mandatory network backend per board

**Bench-Mode**:
The development posture where a controller board is powered by the computer's USB cable with **nothing else connected to it** — no droid hardware and no test gear. It exercises what the board can show over that cable: boot, task startup, the serial log, HTTP, SSE, OTA, configuration persistence, and whatever the firmware can report about itself.

Attaching anything — a jumper, a probe, a meter — is a **special case, not standard practice**: an exceptional measure the operator calls in a dire situation. It is never a routine capability, never something a ticket may plan around, and **never a route an agent proposes to unblock work**. If a ticket cannot proceed without a measurement, the answer is that the work belongs to the droid gate — not that someone should attach a jumper. So a check that needs a signal on a pin (SBUS input, a UART lane, I2C, WS2812B output, PWM levels or edge quality) **is not scoped as Bench-Mode work**: it belongs to the droid gate, and a criterion that assumes gear on the bench is mis-written. A posture, not a verification status: evidence gathered in Bench-Mode is at most Controller Upload Verified.
_Avoid_: bench verified, bench tested, bench-attachable peripherals, scoping pin-level electrical checks as bench work, treating an exceptional measurement as a routine one, using bench work as integrated-hardware evidence

**Bench Runbook**:
The replayable sheet for one Board Variant, `tools/bench_rows/<board>.txt`: named `@row` blocks of Console Client directives, `pause` lines where a human must act, commands only. It is a file, not a ticket. The epic's Closing Ticket points at the sheet, states what each row must show, and receives the dated evidence comment a replay leaves. (Redefined 2026-09-04, operator; until then a Bench Runbook was one ticket per board, which is how epic #206 grew two runbook tickets beside its audit ticket.)
_Avoid_: a runbook ticket per board, copying rows from the sheet into a ticket, runbook as the acceptance record, per-ticket bench sessions

**Closing Ticket**:
The one sub-issue that carries an epic's whole verification tail: the bench rows to replay, the handful of measurements not on a sheet, the audit lines that decide closure, and the closure PR. Sized by AGENTS.md "Verification Scale": rows that earn their place, no criterion the bench cannot measure today, no bookkeeping checkboxes, a visible "Cut on purpose" table. Every box ticked closes it.
_Avoid_: runbook ticket, audit ticket, integration-readiness ticket, gathering ticket, a verification tail spread across several sub-issues

**Estop**:
The latched safe state in which the droid refuses to drive until an operator explicitly clears it. Set by an operator request or by a failsafe layer; never cleared automatically, and never cleared by the condition that set it going away.
_Avoid_: emergency stop mode, safety pause, drive disable

**Latching Estop**:
The property that estop, once set, stays set across the condition ending and across a reboot until an operator clears it via `POST /api/estop/clear`. The latch is the point: a droid that recovers on its own hides the fault that caused it.
_Avoid_: auto-clearing estop, momentary estop, transient stop

**Failsafe Layer**:
One named cause that can independently hold the droid out of drive. Layers are tracked as a bitmask, not a single state, so several may be active at once and the droid stays out of drive until every one has cleared. Today: receiver hardware failsafe, SBUS timeout, stale web drive command, watchdog-reset boot recovery, and operator estop.
_Avoid_: failsafe mode, failsafe state, safety flag

**Watchdog Reset**:
Any reboot caused by a watchdog expiring - task watchdog, interrupt watchdog, or the RTC and super watchdogs that act as backstops. Treated as one class deliberately: the distinctions are a property of the chip rather than of how dangerous the crash was, and one chip protoArtoo targets cannot report them apart at all (ADR 0031).
_Avoid_: TWDT reset, task watchdog reset (when the broader class is meant)

**Controller Console**:
The one command language, Operation Catalog, validation, availability and safety rules, result meanings and help shared by the browser Live Logs console and a physical serial terminal; both are Console Adapters over it and neither owns behaviour of its own (ADR 0034).
_Avoid_: serial console (when the shared thing is meant), CLI, debug shell, recovery console, command subset

**Console Adapter**:
A transport binding of the Controller Console - the browser Live Logs console over one endpoint, or a serial terminal over the embedded line editor - that only translates operator input into an Operation and renders Console Records, never carrying command rules of its own. The serial adapter is also the sole writer of the serial wire once it has bound: every other task's log line reaches serial through the Log Ring, never directly (ADR 0037).
_Avoid_: frontend, shell, REPL, second backend, shared serial writer, log mutex

**Log Ring**:
The in-memory, size-bounded record of every log line the firmware emits, read by the log endpoint and by the serial adapter's drain. It is the authoritative copy of a log line; a line is lost only when newer lines evict it before a reader reaches it, and the serial drain marks such a gap on the wire. A transport never drops a log line on its own.
_Avoid_: serial log, log buffer (when the ring is meant), best-effort serial copy

**Measured Chain**:
A task's worst-case static call depth from its entry function, walked from the linked image with the recipe that names its roots and stitches its indirect calls, recorded per chip as a lower bound. It is what a task stack is sized from and floored against; a high-water mark is not a substitute, because it reports only the paths that happened to run (ADR 0038).
_Avoid_: high-water mark (for sizing), stack usage, "sized with margin" without the chain

**Console Client**:
A host program that carries an operator's or an agent's lines to one Console Adapter and renders the Console Records it answers - the Live Logs page for the browser adapter, the first-party serial terminal for the serial adapter - owning no command rules, completion or readiness claims of its own; listening to the serial line without sending is the same program with nothing to say.
_Avoid_: serial monitor (for the program), bench driver, test harness, the console (for the host side), terminal (when the program rather than the operator's shell is meant)

**Operation**:
A canonical registry entry of type action, status or config that the Controller Console can execute or query, resolved from its name or an accepted alias to exactly one transport-free callable; events are output, not Operations, and meta-commands such as `help` are Console vocabulary, not entries.
_Avoid_: command (for the catalog entry), token, route

**Operation Catalog**:
The flash-resident runtime table of every Operation - name, type, aliases, help, argument schema, availability metadata, executor reference - generated from the action registry and drift-checked against it, from which help and completion derive identically on every board.
_Avoid_: command table, `ACTION_REGISTRY` (the RC-bindable subset), a second copy of the registry, curated per-board list

**Console Record**:
One line of a Console result - a single `result`, or a `begin` / `field` / `item` / `end` group - carrying a Request ID, kebab-case outcome and Availability Reason tokens, and field names that are the API's JSON keys verbatim; rendered as key=value text on serial and parsed as the same model in the browser, never JSON.
_Avoid_: response body, console message, JSON result, log line (for a result)

**Request ID**:
The firmware-assigned monotonic number, shared across both Console Adapters, that ties every Console Record of one request together so results stay attributable when logs and events interleave; operators never type it.
_Avoid_: operator-supplied correlation id, per-adapter sequence number

**Console Source**:
The provenance of a command that entered through a Console Adapter - serial console or web console - carried as a Command Source so logs and state can distinguish it from RC, REST forms, sequences and internal writes.
_Avoid_: session, user, client

**Availability Reason**:
The stable token a Known-but-unavailable Operation reports - `not-in-this-build` and `not-on-this-board` (the Feature Availability states), `component-disabled`, `blocked-by-state`, `temporarily-unavailable` - re-evaluated at execution, never only at discovery.
_Avoid_: error code, `not_included`, hidden operation

**Known-but-unavailable**:
An Operation that stays listed, completable and describable while its Availability Reason says it cannot run now, so operators discover what exists instead of guessing what is missing.
_Avoid_: unknown command, hidden feature, silently dropped

**Non-RC Control**:
The Commanded Mode by which an operator consents to a non-RC source - browser, Controller Console, sequence - commanding motion while the RC link is unhealthy and the failsafe would otherwise hold the droid; it gates exactly that motion scenario and nothing else.
_Avoid_: web control, network authentication, console unlock, blanket gate

## Relationships

- **Phase 5** can include work that is not yet covered by **Full Hardware Validation**.
- **Full Hardware Validation** is required before claiming complete integrated droid readiness for the covered subsystem.
- **Software Verified** does not imply **Controller Upload Verified**.
- **Controller Upload Verified** does not imply **Full Hardware Verified**.
- **Public Verification Wording** maps to internal verification labels without showing those labels in public docs.
- The **v1.0.0 Release Boundary** allows deferred drive hardware validation only when the gap is clearly documented and not presented as complete integrated validation.
- **Phase 5** closes immediately when `v1.0.0` is tagged; subsequent work moves to the normal branch/PR workflow.
- After release, docs/chore/agent maintenance commits may land directly on `main`; firmware changes should still prefer PRs.
- The **protoR2link Arbiter** decides when the **protoR2link Primary Transport** is promoted and when the **protoR2link Fallback Transport** carries traffic; the transports execute its actions but make no selection decisions of their own.
- The **Release Validation Matrix** is the public form of deferred validation tracking for a tagged release.
- A **Public Release Operator** reaches a newly flashed controller through **WiFi Provisioning** before choosing **WiFi Client Mode** or **Standalone AP Mode** as the ongoing network posture.
- **WiFi Client Mode** is the recommended ongoing mode, but **Standalone AP Mode** remains a valid operator-selected mode beyond onboarding.
- **Device WiFi Settings** are created or changed through **WiFi Provisioning**, not baked into public release artifacts.
- An **Unprovisioned Controller** enters **WiFi Provisioning**; a firmware upgrade with valid **Device WiFi Settings** preserves the existing WiFi posture.
- Operators may switch between **WiFi Client Mode** and **Standalone AP Mode** for different operating contexts, such as home network use versus field use.
- A **Staged Network Switch** is a normal operator workflow for moving between **WiFi Client Mode** and **Standalone AP Mode**.
- A **Default AP Credential** bootstraps access for public releases and should be replaceable through **Device WiFi Settings**.
- A **Developer WiFi Shortcut** may prefill local source builds, but public releases rely on **Device WiFi Settings**.
- **Network Recovery Mode** is entered by explicit local action, not by interpreting ordinary **WiFi Client Mode** connection trouble.
- **Page Load Recovery** covers ordinary page opens and refreshes; heap protection may delay or reject work, but the UI must visibly remain active, show automatic retry attempts, and not require a controller power cycle.
- The **Page Recovery View** provides the operator-visible part of **Page Load Recovery** as soon as the first page response arrives, before the rest of the page is available.
- A **Page Recovery View** runs one **Bounded Page Attempt** at a time; it releases failed work before waiting or retrying so recovery does not create the pressure it is responding to.
- **Immediate Request Refusal** keeps rejected work off the controller while the **Page Recovery View** explains the outcome and controls later retries.
- **Page Recovery Status** distinguishes a confirmed **Immediate Request Refusal** from a request that received no response.
- A **Busy Recovery Page** carries the **Page Recovery View** when a full page is refused; no custom recovery page is possible when the browser receives no first response.
- **Recovery Capacity** keeps one **Busy Recovery Page** available without allowing failure handling to create unbounded memory pressure.
- **Resource Step Recovery** lets a **Bounded Page Attempt** retry one failed requirement without repeating completed work or exposing an incomplete page.
- **Section Recovery** contains API failures within the affected page section after required resources are ready.
- **Page Startup Order** delays **Live Page Updates** until required resources and first section attempts have reached a stable visible state.
- **Browser Request Priority** prevents background page work from delaying operator commands and lets latching Estop bypass queued requests.
- **Hidden Tab Pause** stops new controller work without abandoning the one bounded request or user command already in progress.
- Every request in a **Bounded Page Attempt** has an **Operation Deadline** suited to its work and visible progress.
- A **Background Poll** starts only after **Page Startup Order** allows background work and obeys **Hidden Tab Pause**.
- A **Section Request Handle** carries a section's cancellation and **Operation Deadline** into every request its loader makes during a **Bounded Page Attempt**.
- **Page Load Recovery** includes **Page Load Memory Recovery** after repeated page and retry activity on the **Supported ESP32 Board**.
- **Refresh Resilience** may shed excess rapid attempts, but the final page must regain **Page Load Recovery** and the controller must regain **Page Load Memory Recovery**.
- The **Common Page Bootstrap** applies the agreed loading and recovery behavior consistently to every controller page.
- The **Browser Load Profile** defines the normal and development concurrency used to verify **Page Load Recovery**; Mobile Safari checks the AP-only WiFi recovery path.
- The **Web Server Library** may change to meet **Page Load Recovery** within the limits of the **Supported ESP32 Board**; future controller plans do not defer that requirement.
- **Live Page Updates** keep their current `/api/events` contract while alternatives are measured; changing it requires controller evidence of better memory behavior and recovery with equivalent operator behavior.
- The release matrix should split **Drive command and safety logic** from **Hoverboard motor integration**.
- The release matrix should split **RC decoding and diagnostics** from **RC-to-action dispatch and live controls**.
- The release matrix should split **Audio backend and control logic** from **audible playback on real sound modules**, and sound-module families may have different support levels.
- The release matrix should split **Dome serial/control logic**, **Dome motion/ESC**, and **protoR2link integration**.
- The release matrix should split **Servo command logic**, **Servo setup/persistence**, and **physical servo actuation**; AUX outputs are secondary capability, not the primary servo surface.
- The release matrix should split **Network connectivity**, **Web API/UI**, and **Firmware/filesystem update flow**.
- The release matrix should split **Drive failsafe**, **Estop**, **Watchdog recovery**, and **Boot safety defaults**.
- The release matrix should split **Configuration read/write persistence**, **Runtime application**, and **Reboot survival**.
- The release matrix should split **Automated software checks**, **Controller upload smoke checks**, and **Integrated hardware checks**.
- The release matrix should split **Page load**, **Live updates**, and **User action save/apply** for UI surfaces.
- Public release notes should describe missing validation in plain language, for example "drive hardware checks are still to be completed".
- Issue labels should distinguish domain from work type, and `feature request` can cover both user-requested and team-identified improvements.
- Issue triage should stay lightweight and honest rather than process-heavy or gate-driven.
- If an issue is rejected, the reason should be written out honestly instead of relying on a tag-only close.
- Issue submission should stay lightweight rather than forcing rigid forms or templates.
- Issue templates should be minimal Markdown with a friendly tone and light emoji.
- An **Apply Core** carries the write path the way the pure JSON builders carry the read path; both exist so the web API surface is natively testable (ADR 0011).
- A **Commanded Mode** is written through its setter; a **State Zone** is written by its owner; every multi-field read uses a **Zone Snapshot** (ADR 0012).
- The **Audio Config Map** is the single schema home consumed by both the audio task and the api_audio **Apply Core** (ADR 0013); the playback policy stays config-free behind it.
- A **Step Core** decides, its task-loop adapter executes; the **Audio Step Core** calls the playback policy internally, so the policy stays its own tested module behind the step seam.
- A **Component Toggle** and the safety machinery are independent in both directions: a toggle never gates estop latching or the failsafe gate, and estop/safety handling never overrides a toggle or the settings functions — a disabled subsystem stays inert even during estop, since an inert component has no output to stop.
- A **Component Toggle** is runtime by requirement: a **Public Release Operator** must be able to declare fitted hardware from the browser, so component toggles are never compile-time build flags; a **Build Feature Flag** is a separate tier, not a mirror of the toggles.
- A **Board Capability Gate** answers what the board's silicon can do; a **Build Feature Flag** answers what this image was built with; a **Component Toggle** answers what fitted hardware the operator uses. Where a required gate or flag is absent the toggle question never arises — the feature's **Feature Availability** is not-on-this-board or not-in-this-build; where both are present, ADR 0027 toggle semantics apply unchanged.
- **Feature Availability** is declared once per image and read by every page: each registered action, status, event, or config entry names at most one **Board Capability Gate** and one **Build Feature Flag** it requires, and an entry naming neither is universal (ADR 0029).
- A **Framework Envelope** is a fourth tier below the three above and outside Feature Availability: a **Board Capability Gate** says what the silicon can do, a **Build Feature Flag** what the project built in, a **Component Toggle** what the operator uses, and the Framework Envelope what the framework was allowed to bring along. Nothing in the image, the registry, or the browser reads it; the build budget and the operator docs are its only surfaces.
- **firebeetle2** and **artoo_esp32** are **Board Variants**; the **ESP32-P4 Target** and the classic-generation chip target above them own chip-wide facts. ESP32-P4 supplies the external network-backend seam, while firebeetle2 owns its fitted C6/SDIO/reset topology (ADR 0028). **artoo-esp32** remains fully supported alongside any ESP32-P4 board.
- A **Component Toggle**'s struct field, NVS key, registry name, and JSON API key are generic project vocabulary and never encode one Board Variant's own labeling; a **Board Component Label** is the only per-Board-Variant naming surface (ADR 0033, amended 2026-08-26 to include the API surface).
- **protoR2link** is the Component Toggle covering the entire dome-body link task, both transports together; **Dome ESC** is a separate Component Toggle for the body's own dome-rotation actuator. The two are independent and never share a group or a label (ADR 0033).
- On the **ESP32-P4 Target**, the **protoR2link Primary Transport** stays UART — carried on a dedicated P4 UART — with the **protoR2link Fallback Transport** unchanged (ADR 0003).
- Evidence gathered in **Bench-Mode** maps to **Software Verified** or **Controller Upload Verified**, never directly to **Full Hardware Verified**.
- An **Epic Branch** is the documented exception to short-lived feature branches; the **Post-Release Main Workflow** still governs how it reaches `main` (a PM-approved PR at closure or milestone).

- The **Controller Console** has exactly two **Console Adapters**; every **Operation** behaves identically through both, on every board.
- An **Operation** of type config is an **Apply Core** plus its **Commit Step**; of type status, a **Zone Snapshot** rendered as **Console Records**; of type action, the existing dispatch core returning an outcome instead of nothing.
- Every **Console Record** carries one **Request ID**; a write that entered through a **Console Adapter** carries its **Console Source** as its Command Source.
- A **Console Client** drives exactly one **Console Adapter** per session and adds no behaviour to the **Controller Console**; a scripted client may address either adapter, which is what makes a parity transcript a one-program job.
- A **Bench Runbook** row is replayed by a **Console Client** script that carries commands only; what the row expects stays on the epic's **Closing Ticket**, and each replay still leaves one dated evidence comment there.
- A **Known-but-unavailable** Operation reports exactly one **Availability Reason**; `not-in-this-build` and `not-on-this-board` are the two **Feature Availability** states, and `component-disabled` is a **Component Toggle** that is off.
- **Non-RC Control** is a **Commanded Mode**; the **Controller Console** can set it but is never gated by it for queries, configuration or non-motion actions.
- A **Commit Step** refreshes exactly one **Working Snapshot** and serializes every writer of its configuration path, adapters and Commanded Mode setters alike
- Every log line is written once, to the **Log Ring**; the serial **Console Adapter** is its only serial reader and the only writer of the serial wire after it binds
- Every project-created task has one **Measured Chain** recipe per chip, and its stack is never below its chain; the sizing margin above the chain is a per-chip judgement recorded beside the constant

## Example Dialogue

> **Dev:** "Can we mark the AUX LED feature as bench verified after `pio test` and `pio check` pass?"
> **Domain expert:** "No — that is **Software Verified**. It becomes **Controller Upload Verified** after an ESP32 bench upload and smoke check, and **Full Hardware Verified** only after the LED strip is tested on the integrated droid hardware."
>
> **Dev:** "Should `docs/status.md` say `software-verified`?"
> **Domain expert:** "No — public docs should say what happened, such as 'Automated checks are passing.'"
>
> **Dev:** "Can `v1.0.0` ship before hoverboard drive hardware is available?"
> **Domain expert:** "Yes, if the release clearly states the drive hardware validation gap and includes a closure checklist."
>
> **Dev:** "Should the release notes include our internal verification labels?"
> **Domain expert:** "No — use a **Release Validation Matrix** with plain evidence wording by subsystem."
>
> **Dev:** "Should drive be one row in the matrix?"
> **Domain expert:** "No — split the firmware-side drive logic from the physical hoverboard integration."
>
> **Dev:** "Should RC be one row in the matrix?"
> **Domain expert:** "No — split receiver/diagnostic evidence from live dispatch and control evidence."
>
> **Dev:** "Should audio be one row in the matrix?"
> **Domain expert:** "No — split firmware-side audio control from real module playback, because module support and evidence vary."
>
> **Dev:** "Should dome be one row in the matrix?"
> **Domain expert:** "No — serial control, motion control, and body-link integration are separate verification surfaces."
>
> **Dev:** "Should servos and AUX share a row?"
> **Domain expert:** "No — treat servo control and actuation as the primary surface, and keep AUX as secondary capability."
>
> **Dev:** "Should WiFi, web UI, and OTA be one row?"
> **Domain expert:** "No — connectivity, interface behavior, and update/recovery flow need separate evidence."
>
> **Dev:** "Should safety be one row?"
> **Domain expert:** "No — drive failsafe, estop, watchdog recovery, and boot defaults are different safety proofs."
>
> **Dev:** "Should configuration be one row?"
> **Domain expert:** "No — saving, applying, and surviving reboot are separate proofs."
>
> **Dev:** "Should build and test checks be one row?"
> **Domain expert:** "No — public release notes should describe the evidence in plain language, not assume PlatformIO terminology."
>
> **Dev:** "Should UI be one row?"
> **Domain expert:** "No — loading, live updates, and saving/applying user actions are different proofs."

## Flagged Ambiguities
- "bench" was read as *"USB plus whatever test gear you can attach"* — a spare receiver, a signal generator, a loopback, a breakout, a bench servo/ESC, a scope or logic analyser were all listed as in scope. That is not feasible or practical on this project's bench, where a board is powered by the computer's USB cable and nothing else is connected. Resolved by defining **Bench-Mode** as USB-only with nothing attached. Connecting a jumper or probe is possible but is an exceptional measure the operator calls in a dire situation — never a routine capability, and never something a ticket may plan around — so a pin-level electrical check is scoped as droid-gate work by definition rather than by argument. The wrong reading had propagated from this glossary into six tickets and repeatedly produced bench tickets that quietly required hardware nobody could attach.
- "recommended" named two unrelated things: the **WiFi Client Mode** posture and whether the project tells a builder to buy a board; resolved by keeping **recommended** for the WiFi mode and naming the second one a **Builder Recommendation**. The #184 pass-tier vocabulary ("FULL PASS" / "DEVELOPER-ONLY PASS") is retired — it read as a test verdict on the board when it is a documentation decision about purchase advice.

- "bench verified" was used for both clean software verification and actual ESP32 bench upload/testing; resolved by replacing it with **Software Verified**, **Controller Upload Verified**, or **Full Hardware Verified**.
- "dome link" / "dome serial" / "dome WiFi" were used inconsistently across task notes; resolved by using **protoR2link** for the subsystem name and **UART (slip ring)** / **WiFi (fallback)** for transport labels in operator-facing text.
- "AP mode" was used for both first-boot onboarding and ongoing hotspot operation; resolved by using **WiFi Provisioning** for onboarding and **Standalone AP Mode** for the ongoing operator-selected posture.
- "Fresh public release" blurred download source with controller state; resolved by using **Unprovisioned Controller** for the no-settings state.
- "Switch WiFi from the setup page" is resolved as a **Staged Network Switch**, not a fragile live toggle.
- "capability" names two things: a panel verb in the **Dome Layout View Model** (`P1 + open`) and a board topology fact in a **Board Capability Gate**; resolved by qualifying every use — "panel capability" in dome-layout text, **Board Capability Gate** for the compile-time tier.
- "capability envelope" was drafted for the framework facilities compiled out of the artoo image; that would have been a third meaning of "capability", so it is a **Framework Envelope** — the silicon keeps the capability, the image simply does not carry the framework code for it.
- "feature flag" was used loosely for all three tiers; resolved by naming them **Board Capability Gate**, **Build Feature Flag**, and **Component Toggle**, and never using "feature flag" unqualified.
- "dome" was used at once for the body's dome-rotation actuator, the **protoR2link** communications link, and the dome's panel/sequence system; resolved by keeping **Dome ESC** (the actuator), **protoR2link** (the link), and **Dome Layout View Model** (the panel read-model) as separate terms, never grouped under a bare "Dome" label (ADR 0033).
- Component Toggle identifiers (`arm1`/`s1_hoverboard`/etc.) were named after the artoo.uk PCB's own silkscreen legend; resolved by making the identifier generic project vocabulary and moving the board-specific text into a **Board Component Label** (ADR 0033).
- "web control" read as a network-authentication gate; resolved as **Non-RC Control**, a motion-consent **Commanded Mode** - the `webControlEnabled` identifier and page copy follow in the Controller Console epic.
- "action registry" holds status, config and event rows too; resolved by making **Operation** the umbrella for what the **Controller Console** can run or query while "action" stays a registry type, and by keeping rows that only describe a field inside an aggregate response as metadata rather than standalone Operations.
- The Console plan drafted `not_included` / `unsupported_on_board` beside the browser's `not-in-this-build` / `not-on-this-board`; resolved by reusing the browser tokens and one kebab-case convention for every token the protocol defines - field names are carried from the API's JSON keys and are not tokens.
- "the Apply Core contract" was used (2026-09-04, #226) to mean the calling convention of the seam - resolved: the contract is the response bytes and the plain outcome; how a **Working Snapshot** crosses the seam is not part of it
- "one seam" (2026-09-04, #268) was used to mean one function every serial byte passes through - resolved: ownership of the serial wire is by task; the function seam is a consequence, not the invariant
