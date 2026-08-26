# Component Toggle identity decouples from the artoo.uk PCB silkscreen

Issue #202 (component switch rework) found that all 15 `components.*`
Component Toggles introduced by ADR 0027, plus their RC-trigger-binding
counterparts, name themselves after the artoo.uk Artoo Controller PCB's own
silkscreen legend — `enable_s1_hoverboard`/`en_s1` for the "S1" serial
header, `enable_s2_sound`/`en_s2`, `enable_s3_dome_ctrl`/`en_s3`, plus the
`ARM1-5`/`AUX1-3` labels — baked into `config_store.h` struct fields, NVS
keys, `docs/action-registry.yaml` entry names, and setup-page copy. With a
second Board Variant (firebeetle2, ADR 0028) already declaring a full
parallel pin plan for the same 15 subsystems on different GPIOs, and any
future Board Variant free to use a different physical layout entirely, a
toggle's operator-facing and code-facing identity must not be a fact about
one PCB.

We decided Component Toggle identity is generic project vocabulary,
decoupled from any Board Variant's own labeling scheme. Where a board has a
meaningful physical label for a subsystem (e.g., artoo_esp32's "S1"
silkscreen), that label is declared once per board in a new
`include/component_labels.inc` manifest and shown to the operator as
supplementary detail (a tooltip), never as the toggle's canonical name —
this per-board label is the new **Board Component Label** term (CONTEXT.md).

## Renames

| Old (struct / NVS / registry) | New (struct / NVS / registry) | Display name | Why |
|---|---|---|---|
| `enable_s1_hoverboard` / `en_s1` / `system.config.enable_s1_hoverboard` | `enable_drive` / `en_drive` / `system.config.enable_drive` | Drive | Not "Hoverboard Drive": matches `docs/goal.md`'s stated direction toward protocol-contract compatibility over vendor lock, and the wider R2-builder convention of naming this function by role. |
| `enable_s2_sound` / `en_s2` / `system.config.enable_s2_sound` | `enable_audio` / `en_audio` / `system.config.enable_audio` | Audio | Matches the project's existing Audio Config Map / Audio Step Core vocabulary. |
| `enable_s3_dome_ctrl` / `en_s3` / `system.config.enable_s3_dome_ctrl` | `enable_protor2link` / `en_r2link` / `system.config.enable_protor2link` | protoR2link | The toggle gates the entire `domeLinkTask` (`src/tasks/dome_link.cpp:761`) — both transports die together when it's off. CONTEXT.md already lists "dome link" under `_Avoid_` in favor of this exact term. |
| `enable_dome` / `en_dome` / `system.config.enable_dome` | `enable_dome_esc` / `en_dome_esc` / `system.config.enable_dome_esc` | Dome ESC | Grouped separately from protoR2link — the two are unrelated (one is a body-side motor actuator, the other a communications link) — to stop "dome" doing overloaded duty across rotation, the body-dome link, and dome panels/sequences. |
| `rc_sound` / `rc_pwm_sound` / `rc_sbus_sound` (NVS `rc_sound` / `rcp_snd` / `rcs_snd`) | `rc_audio` / `rc_pwm_audio` / `rc_sbus_audio` (NVS `rc_aud` / `rcp_aud` / `rcs_aud`) | — | Follows the Audio rename in lockstep; the RC-trigger-binding struct mirrors the same component identity one layer down. |

`enable_arm1`/`enable_arm2`, `enable_aux1/2/3`, `enable_rc_ch1..6`, and their
RC-binding counterparts (`rc_arm1`, `rc_arm2`, `rc_aux1-3`) keep their
existing identifiers unchanged — `pin_map.md` already documents `ARM1-5` as
protoArtoo's own labels, not artoo.uk's, so only their display copy changes
(e.g. "ARM1" becomes "Utility Arm 1", numeric rather than positional, since
Top/Left vs Bottom/Right is a per-Board-Variant placement fact).

A one-time boot migration reads any old key present, writes the new key, and
deletes the old key, logged once. This is the only migration path — no
permanent dual-read — because v1.0.0 already shipped the old keys to real
hardware and a permanent second read path would otherwise live in every
reader forever.

The setup page regroups by droid function rather than PCB connector: Drive,
Dome Rotation, protoR2link, Audio, Utility Arms, AUX Outputs, RC Receiver
Channels.

## Considered options

- **Display-only rename** (keep `arm1`/`s1_hoverboard`/etc. as the internal
  identifiers, add labels only) — rejected: the registry's `name:` field is
  "the single grain" per ADR 0029/#186, and leaving it PCB-labeled keeps that
  grain a board fact forever.
- **Permanent dual-read fallback** for old NVS keys — rejected: every future
  reader would carry two key names indefinitely for a one-time upgrade
  event.
- **Positional arm naming** (Left/Right Utility Arm) — rejected: position is
  a per-Board-Variant placement fact (`pin_map.md`'s Top/Left, Bottom/Right),
  not a universal identity; a different build could mount them elsewhere.
- **Board Capability Gates for these 15 now** — rejected: both Board
  Variants' pin plans already assign all 15 subsystems a pin
  (`config.h:110-262`); no board today lacks one, so a not-on-this-board
  state has no current consumer. Revisit only when a variant actually omits
  a subsystem.

## Consequences

- CONTEXT.md gains **Board Component Label**.
- `include/component_labels.inc` is a new manifest, following the X-macro
  precedent `board_capabilities.inc`/`build_flags.inc` set in #186;
  firebeetle2 entries may be absent where no established label exists.
- `make check-action-drift` and every reader of the renamed fields move in
  the same slice as the rename.

## Amended 2026-08-26 — the decision extends to the JSON API surface

This ADR as originally written renamed three surfaces — **struct field, NVS
key, registry name** — and said nothing about the JSON API. That omission was
found during #202 Slice 1's bench verification: with the rename merged, the
firmware spoke the new vocabulary internally while `/api/config` and
`/api/status` still answered `s1Hoverboard`, `s2Sound`, `s3DomeCtrl`.

**The API surface is part of component identity, not plumbing behind it.**
`CONTEXT.md` defines the concept itself in terms of the API path — *"a runtime
`components.*` setting"* — so the JSON key is the concept's operator-facing
name. Leaving it unchanged would not decouple identity from the artoo.uk
silkscreen; it would move the silkscreen one layer outward, onto the most-read
surface the project has.

### Additional renames

Both directions of the wire. Read shape (`/api/config`, `/api/status`
peripherals):

| Old key | New key |
|---|---|
| `components.s1Hoverboard` | `components.drive` |
| `components.s2Sound` | `components.audio` |
| `components.s3DomeCtrl` | `components.protoR2link` |
| `components.dome` | `components.domeEsc` |

Write shape (`POST /api/config` fields):

| Old field | New field |
|---|---|
| `enableS1Hoverboard` | `enableDrive` |
| `enableS2Sound` | `enableAudio` |
| `enableS3DomeCtrl` | `enableProtoR2link` |
| `enableDome` | `enableDomeEsc` |
| `domeNeutralUs` / `domeMinPulseUs` / `domeMaxPulseUs` / `domeSpeedLimitPct` | `domeEsc…` equivalents |
| `domeWifiPeerIp` | moves to protoR2link — see below |

The eleven unrenamed toggles (`arm1`, `arm2`, `aux1-3`, `rcCh1-6`) keep their
API keys, exactly as they kept their identifiers.

### The top-level `dome` config block splits

Tracing consumers showed the block was two components wearing one name:

| Setting | Read by | Belongs to |
|---|---|---|
| `neutralUs`, `minPulseUs`, `maxPulseUs`, `speedLimitPct` | `src/tasks/dome_task.cpp` | the ESC that spins the dome |
| `wifiPeerIp` | **only** `src/tasks/dome_link.cpp:196` | **protoR2link** |

So the block becomes **`domeEsc`** (the four pulse settings) plus a new
**`protoR2link`** block holding the peer IP. Renaming the block wholesale to
`domeEsc` was rejected: it would file a link setting under the motor's name and
state something untrue about the model — a reader of `domeEsc.wifiPeerIp`
reasonably concludes the ESC is network-attached, which it is not.

This also discharges the overloading this ADR's grouping note already
identified, where *"dome" was already overloaded across rotation, the link, and
dome panels*: `components.domeEsc` no longer collides with a top-level `dome`,
and the link's own setting sits under the link's own name.

### Rejected in the amendment

- **Freezing the API names as a compatibility surface** — rejected: there is no
  released API version and no versioning or deprecation convention in this
  project, and no consumer outside this repository. It would also make
  CONTEXT.md's definition of **Component Toggle** inaccurate, since that
  definition is stated in terms of `components.*`.
- **Emitting both old and new keys for a release** — rejected on the same
  reasoning this ticket already rejected a permanent dual-read for the NVS
  keys: "temporary" dual-emission becomes permanent, and `/api/identity` has
  only 85 B of headroom against `IDENTITY_JSON_MAX_BYTES` (which is also why
  Board Component Labels are emitted through `/api/config`, not identity).
- **Renaming only the four `enable*` POST fields** — rejected: the read and
  write shapes would then disagree for precisely the settings being split
  apart, and `domeWifiPeerIp` would keep naming the dome while living under
  protoR2link.

### Consequences of the amendment

- This is a **breaking API change**, taken deliberately and without a
  compatibility window. A browser tab open across the upgrade reads the old
  keys and renders components as absent until reloaded.
- `CONTEXT.md`'s list of surfaces that are generic project vocabulary gains
  **JSON API key**.
- Blast radius: 2 firmware files emitting (`src/web/api_config.cpp`,
  `src/web/web_server.cpp`), 4 UI files consuming (`data/app.js`,
  `data/drive.js`, `data/sound.js`, `data/setup.js`), 7 test files, and
  `docs/api.md`.
- Operator-facing display copy still carrying the old vocabulary outside the
  setup page — `data/app.js` "Hoverboard Drive", "Sound Module" — moves in the
  same slice.
