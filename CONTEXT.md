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

## Relationships

- **Phase 5** can include work that is not yet covered by **Full Hardware Validation**.
- **Full Hardware Validation** is required before claiming complete integrated droid readiness for the covered subsystem.
- **Software Verified** does not imply **Controller Upload Verified**.
- **Controller Upload Verified** does not imply **Full Hardware Verified**.
- **Public Verification Wording** maps to internal verification labels without showing those labels in public docs.
- The **v1.0.0 Release Boundary** allows deferred drive hardware validation only when the gap is clearly documented and not presented as complete integrated validation.
- **Phase 5** closes immediately when `v1.0.0` is tagged; subsequent work moves to the normal branch/PR workflow.
- After release, docs/chore/agent maintenance commits may land directly on `main`; firmware changes should still prefer PRs.
- The **Release Validation Matrix** is the public form of deferred validation tracking for a tagged release.
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

- "bench verified" was used for both clean software verification and actual ESP32 bench upload/testing; resolved by replacing it with **Software Verified**, **Controller Upload Verified**, or **Full Hardware Verified**.
- "dome link" / "dome serial" / "dome WiFi" were used inconsistently across task notes; resolved by using **protoR2link** for the subsystem name and **UART (slip ring)** / **WiFi (fallback)** for transport labels in operator-facing text.
