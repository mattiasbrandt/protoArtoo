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
