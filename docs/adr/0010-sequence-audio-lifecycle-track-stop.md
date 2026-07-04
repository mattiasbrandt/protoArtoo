# Sequence audio lifecycle: Track Stop semantics and Bounded Audio

Issue #16 surfaced that long named tracks started by bounded Factory sequences outlive
the sequence on normal completion (`DM:VADER`'s Imperial March plays ~3 min past the 47 s
visual reset), and that every stop the audio system currently offers also disables
random/idle mood as a side effect. This ADR records the decided audio-stop semantics and
the bounded-audio lifecycle for sequence-started tracks.

## Status

accepted (design, 2026-07-04); implementation tracked in issue #16

## Context

Three facts drove the design, all code-verified on `phase/v1.0.0`:

1. **`FX_AUDIO` stops on abnormal termination only** (`include/sequence_engine.h`,
   `src/tasks/sequence_engine.cpp`). Normal `SEQ_TERM` completion resets visuals but lets
   the track play out. VADER, LEIA, and CANTINA overrun; ROCKMARCH works around it with an
   authored terminal `$s` step.
2. **Every existing stop path disables random/idle mood.** The single stop intent
   (`AUDIO_PLAYBACK_INTENT_STOP`) does `driver->stop(); randomMode = false`, and
   `randomMode` only re-arms on reboot or an explicit RANDOM_ON. This is reached from the
   sequence engine's abnormal cleanup (`SEQ_ACT_AUDIO_STOP`), ROCKMARCH's `$s` step, the
   web `POST /api/audio action=stop`, and the dome `BD:RESET` cue. All four silently mute
   idle sounds until reboot. This violates the Suppression Window principle (the body
   holds idle-random behavior "without changing those subsystems' configured modes") —
   the abnormal path was never a model to copy.
3. **Category audio steps hard-code `FX_AUDIO`** (`SEQ_AUDIO_CAT` macro), so redefining
   `FX_AUDIO` as "stop on any termination" would hard-cut screams/alarms that naturally
   ring out slightly past `SEQ_TERM`.

The `$s` dollar command's "stop playback and disable random mode" coupling is intentional
and load-bearing for the mood system (Quiet/SE10 maps to `$s`); its semantics must not
change.

## Decision

### 1. Two named stop semantics: Track Stop and Quiet

- **Track Stop**: stop the current playback only. Preserves `randomMode`, and bumps the
  idle cadence timestamp (`lastPlayMs`) so the droid holds a natural anti-spam quiet beat
  before idle chatter resumes. This is a new playback intent distinct from the existing
  stop.
- **Quiet**: stop playback and disable random/idle mood. This is the existing `$s`
  semantics, owned exclusively by the mood system. Unchanged.

### 2. Track Stop replaces the full stop on every non-mood surface

- The sequence engine's `SEQ_ACT_AUDIO_STOP` action (abnormal cleanup today, terminal
  cleanup with Bounded Audio) dispatches a Track Stop.
- `POST /api/audio action=stop` becomes a Track Stop. Disabling idle mood is the mood
  control's job, not the stop button's (the 2026-06-29 live incident: a manual stop
  muted idle sounds until reboot and surprised the operator).
- The dome `BD:RESET` cue's stop becomes a Track Stop. It does not forward a dome-side
  `$s`, and a reset that leaves the droid permanently muted was never "reset to baseline".
- `$s` (mood Quiet) keeps stop-plus-disable semantics.

### 3. Bounded Audio: `FX_AUDIO_BOUNDED` opt-in flag for long named tracks

A new effect flag, sibling to `FX_AUDIO`, meaning "Track Stop this audio on normal
terminal completion as well as abnormal termination". The engine emits the (now
Track Stop) `SEQ_ACT_AUDIO_STOP` at `SEQ_TERM` when the flag is active; the engine stays
pure and composes through the existing action/terminal-cleanup path.

The flag distinguishes **long named tracks** (bounded to the show) from **short category
vocalizations** (always allowed to ring out past `SEQ_TERM`); ring-out is sacred, so
`SEQ_AUDIO_CAT` keeps plain `FX_AUDIO`. The stop is a hard cut, not a fade — a fade would
require driver-side volume ramping and timed state outside the pure engine. Sequences
that want a musically sensible ending author the bound earlier (as ROCKMARCH's 47000 ms
stop before its 48250 ms TERM already does).

Catalog migration in scope: VADER, LEIA, CANTINA, and ROCKMARCH's `$M` steps get
`FX_AUDIO_BOUNDED`; ROCKMARCH's authored `$s` terminal step is removed (it currently
disables idle mood on every normal completion).

### 4. Learned Sequences: default bounded, opt-out, validated now

The lifecycle is exposed in the Learned Sequence JSON step schema in the same change, as
a boolean per audio step (`boundAudio`). Learned named-track steps default to bounded
when the field is omitted; an author may set it `false` to opt out. Protocol Check
accepts both and validates the field's type.

The asymmetry with the Factory catalog (opt-in flag there, default-on here) is
deliberate: Factory sequences are PR-reviewed expert authoring; Learned Sequences are
guarded by defaults so a naive author cannot recreate the VADER overrun by omission.

## Considered options

- **Per-sequence terminal `$s` step** (issue #16 option 1): rejected. `$s` is Quiet;
  ROCKMARCH proves it regresses idle mood on every normal run. Also dollar-command-only
  authoring is a documented glossary anti-pattern.
- **New track-stop-only dollar token** (e.g. `$x`): rejected. Duplicates stop authoring
  per catalog entry and keeps the dollar-authoring anti-pattern.
- **Redefine `FX_AUDIO` globally as stop-on-any-end**: rejected. Category steps
  hard-code `FX_AUDIO`; the change would clip ring-out on screams/alarms.
- **Implicit bounding by step type** (STEP_AUDIO bounded, STEP_AUDIO_CATEGORY not):
  rejected. The lifecycle would be invisible and non-overridable at the authoring
  surface.
- **Fade instead of cut**: rejected for v1; needs driver ramp support and timed state
  outside the pure engine, and preemption-mid-fade semantics.
- **Separate `action=silence` web API**: rejected for v1; the mood system already owns
  the stop-plus-disable surface.

## Consequences

- The Suppression Window principle holds end to end: no sequence, web stop, or dome cue
  can change the configured random/idle mood; only the mood system (Quiet) can.
- Aborting or preempting a sequence no longer mutes idle sounds until reboot (previously
  broken on the abnormal path too, not only normal completion).
- After any Track Stop, idle chatter resumes after the normal anti-spam beat rather than
  instantly (cadence bump) or never (previous behavior).
- Operators who relied on the web stop button to also silence idle mood must use the
  mood control (Quiet) instead; UI copy should make the stop button's scope clear.
- Protocol Check and the Learned Sequence schema grow one boolean field; existing saved
  sequences remain valid (omitted field = bounded default).
