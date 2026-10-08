---
name: protor2-choreographer
description: Draft a protoR2 Learned Sequence choreographed to a music track - analyse its tempo, levels and downbeat, interview the builder, write beat-placed steps and repeating Gestures, and check the draft with Protocol Check and the Rehearsal. Also retimes an existing sequence onto a track's beat. Use when asked to choreograph, dance, or time a sequence to music, an MP3 or a Named Track.
---

# protoR2 choreographer

A developer workstation workflow (#14). The output is a **Learned Sequence draft**
for the builder to review, test on the droid and save from the editor: never a
Factory Sequence, never a firmware change. The firmware keeps running milliseconds;
the tempo is authoring data the sequence carries (ADR 0058).

The format's source of truth is `docs/sequence-authoring.md`: step types, panel
targets, Gestures, tempo and beats, the Protocol Check table, the Rehearsal rules.
Read it before drafting; this skill does not repeat it.

## Setup

`ffmpeg` and `node` are required. Beat This! (beats **and downbeats**, MIT) is
optional; without it there is no downbeat proposal and no grid-holding figure,
and the analysis says so. Install it outside the repo:

```bash
uv venv ~/.cache/protor2-choreo/venv
uv pip install --python ~/.cache/protor2-choreo/venv/bin/python --index-url https://download.pytorch.org/whl/cpu torch torchaudio
uv pip install --python ~/.cache/protor2-choreo/venv/bin/python beat-this
```

Run `analyze_track.py` with that venv's `python`; the other scripts need only `python3`.
Scripts live in `scripts/` beside this file and read the editor's own modules from
`data/` of the checkout they sit in.

## Steps

1. **Pick the track.** The local music folder is `tasks/CHIRP-SD/2A_music/`; its
   `zz_*.mp3` files are CHIRP chatter, used only when the developer asks for one.
   Done when one file is named.

2. **Analyse it.** `analyze_track.py <file>`. It prints the tempo the editor's
   analyser hears, the five **levels** (half, two thirds, heard, three halves,
   double) with their strengths, Beat This!'s tempo and first downbeat, and marks
   the level Beat This! agrees with. Done when the levels are on screen.

3. **Interview, one question per turn** (`AskUserQuestion`, recommended option
   first). Done when every item is answered:
   - which **level** is the music's beat (recommend the one Beat This! agrees with;
     Cantina reads 65 / 130 / 194 / 259, and only the builder can say)
   - the **window**: whole track, first N seconds, or a stretch
   - whether the **downbeat** is right (Beat This!'s proposal, or the builder's ms)
   - the sequence **name** (`DM:` + up to 18 of `A-Z 0-9 _`)
   - the **sound step**: the Named Track letter that plays this music on their droid
     (`$C`/`$c` Cantina, `$W` Star Wars theme, `$M` Imperial March, `$D` disco are
     the defaults; their config decides), or none
   - **parts**: ring only, pies too, body doors, or all
   - **feel**: march, disco, playful, dramatic, subtle, chaotic
   - **dome rotation**: none, gentle, lively
   - a **Visual Preset** (`DV:` name from the authoring guide), or none

4. **Re-run the analysis with the answers**:
   `analyze_track.py <file> --bpm <level> [--window S E] [--downbeat-ms MS]`.
   It writes `tasks/choreo/analysis/<track>.json` holding the `tempo` block and
   how far the grid holds. A grid that drifts inside the window is a fact for the
   report and for the builder; shorten the window or accept it, their call. Done
   when the tempo block exists.

5. **Draft** `tasks/choreo/generated/<NAME>.json`: `format: 1`, the `tempo` block
   from step 4, `toggleGroup: "none"`, `closeSteps: []`, every step placed by
   `beat` (write `t: 0`; step 6 fills it). Choreograph in **Gestures** with
   `repeatBeats`/`extentBeats` rather than a step per panel move: one editable
   object per idea is what the editor edits (ADR 0057, ADR 0060). Feel, as a
   starting point the builder will tune:
   - march: `alternate` on `breadpan` or `ring`, every 2 beats
   - disco: `chase` round the `ring`, every beat
   - playful: `pulse` on `pies`, every 2 beats
   - dramatic: `wave` open round the `ring` on a bar, a Visual Preset on bar 1
   - subtle: one panel `:OP`/`:CL` pair on a downbeat every few bars
   - chaotic: a `random` flutter on the ring, spaced a beat apart
   End on an `end` step at the window's last beat, and set `suppressMs` at or
   past that time. Done when the file exists.

6. **Check it.** `check_sequence.py <file> --resolve` writes each `t` from its
   beat and prints Protocol Check's verdict and every Rehearsal finding. Fix and
   re-run until Protocol Check accepts it and every Rehearsal **warning** is
   either fixed or written into the report with the builder's reason to keep it.
   The check runs without a droid: rules needing the droid's Outputs stay silent,
   and the editor on the droid has the last word. Done when it exits 0.

7. **Report** `tasks/choreo/reports/<NAME>.md`, short: the track and its
   fingerprint, the level chosen and the ones rejected, the confidence, the
   downbeat and who set it, how far the grid holds in the window, the Rehearsal
   findings kept, and the safety notes below as they apply. Done when the report
   names every one of those.

## Retiming an existing sequence

`retime_sequence.py <seq.json> --analysis <analysis.json>` puts every step on its
nearest beat through the editor's own retime and says how many landed; a step
inside a loop body never lands. Run steps 2-4 for the analysis first, then
step 6 on the retimed copy, and quote the landed count in the report.

## Safety

Protocol Check refuses `:SM` and malformed steps; these the draft keeps itself:

- Move panels with intent commands (`:OP`, `:CL`, `:OF`) or Gestures.
- Space dome moves at least 200 ms apart and move panels one at a time; group
  targets (`00`, `14`, `15`) move every member at once and browned the dome out.
  The Rehearsal's `dispatch-spacing`, `servo-burst` and `group-panel` say where.
- Close what the draft opens on pies and the body; a flutter (`:OF`, a flutter
  Gesture) ends closed and needs no close.
- Stay within 96 steps and the droid's file cap (12 KB artoo-esp32, 24 KB
  ESP32-P4).
- The draft is reviewed in the editor and tested on the droid before anyone
  relies on it.
