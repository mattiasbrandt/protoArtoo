# A tempo is tapped or measured, and the sequence keeps both the beat and the millisecond

Status: accepted (2026-09-09, issue #335). Describes the **target** model; none of
it is implemented yet.

## Context

Nothing musical exists in protoArtoo. The strings `bpm`, `tempo`, `barLen` and
`barPhase` appear nowhere in `src/`, `include/` or `data/` except twice as prose
in one factory sequence. There is no tempo field, no beat unit and no phase
concept anywhere.

The receipt for doing it by hand is `DM:CANTINA`
(`src/tasks/sequence_catalog.cpp:239-280`): 26 body steps for a two-beat
alternation on an 1846 ms period, worked out from 130 BPM with a calculator. The
130 BPM is written in the file twice — a comment at `:239` and the operator-facing
`purpose` string at `:625` — and **nothing machine-readable connects 1846 to
130**. That is the whole problem in one entry.

#331 decided that a sequence *has* a tempo: a step or a Gesture can sit on a beat
rather than at a millisecond, a whole routine retimes by changing one number, and
beats resolve to absolute milliseconds before the Sequence Coordinator schedules
anything. It fixed the posture — a detected tempo is **advisory and always
editable, never a fact** — and left everything underneath to this ticket.

Three facts shape the answer, and none of them applies to the reference project
this design draws on:

- **The droid holds the audio and the browser does not.** A track is an index on
  an SD card behind a 9600-baud serial module.
- **protoArtoo cannot put a file on that card.** Every module owns its own reader
  and the builder loads it by hand (`docs/sound_playback.md:69,100,146,214`). So
  "make the browser the uploader" is not available as a way to pair a grid to a
  file.
- **A sequence names its sound as a role, not a file.** `STEP_AUDIO` carries a
  Marcduino `$` letter and the operator's config supplies the track number
  (`include/audio_dollar_parser.h:44-53`). The audio behind a sequence can change
  with the sequence untouched.

## Decision

**A tempo comes from two built routes, and tapping always works.** A builder
either drops their own copy of the track into the editor to be analysed, or fires
the track on the droid and taps along. #331's typed number stands as a third.
Tapping is the one that always works: it needs no file, no analyser and no
pairing, and it is the only route that uses the audio protoArtoo actually has.

**The analyser is ported, with two fixes, and the notice comes with it.** The
reference's `musicAnalyse` is about 110 lines with no dependencies and no FFT.
Two measured defects are corrected before any grid is stored:

- the autocorrelation is unnormalised — `corr()` is a raw dot product, so a
  shorter lag sums more terms and scores higher for reasons unrelated to the
  music. It reads **-2.0 BPM at 128 and -2.3 at 150**. Divide by `(nF - lag)`.
- the grid sits about **18 ms early** at 44.1 kHz, because an onset's time is
  taken as the analysis window's start. Add `win/(2*sr)` back.

Per #290 a close port carries a per-file MIT notice. That is accepted rather than
avoided: the constants and the estimator are both quoted in full in #335, so
writing them out fresh would produce the same function under a different label.

**The tempo is a top-level optional key at `format: 1`, parsed and validated by
firmware.** It carries `bpm`, `phase`, `barLen`, `barPhase`, `duration`, a hash of
what was analysed, the source that produced it, and a confidence. An optional key
needs no format bump, so no existing sequence is orphaned.

**A step keeps its beat index beside its millisecond.** The millisecond is what
the engine runs — `SeqStep.tMs` and the scheduler are untouched, so #331's
determinism rule holds. The beat is what the builder meant, so changing one
number re-resolves every beat-placed step. Without this, #331's retiming promise
has no representation that survives a reload.

**A hash of the file pairs a stored tempo to its audio.** Only the analysed route
can produce one; a tapped or typed tempo is unanchored by design.

**Every source carries a confidence, and a low one is a Rehearsal Warning.** The
analyser's is `best / mean(corr)`, which the autocorrelation already computes; a
tapped tempo's is the spread between taps. A stale hash is a Rehearsal Warning
too. Both use #287's existing machinery: the finding carries fields so the surface
can offer the fix, and the save never blocks.

**The playhead has one behaviour on every sound module.** The start edge, then
open-loop timing — about ±18 ms over three minutes, under 4% of a beat at 120
BPM. CHIRP's I2S sample counter can be slaved later as an addition, not a
redesign.

## Considered options

- **Analyse a dropped copy only**, as #335 recommended. Rejected: it requires the
  builder to independently hold the file on the machine running the browser, and
  a builder who inherited a card or bought a pre-loaded one never does.
- **Anchor the tempo to the Named Track rather than to a file hash.** #326 already
  built a reconciliation for the same collision one layer up — a Named Track
  pointing at an index whose file changed underneath it. Rejected in favour of the
  hash, which is exact about what was measured, at the cost of leaving the tapped
  and typed routes unanchored.
- **Store the tempo in `meta`, where firmware never looks.** Free — the store
  writes the client's bytes verbatim (`src/seq_store.cpp:426`) and `meta.notes`
  and `meta.purpose` already survive exactly this way. Rejected: it puts a
  load-bearing authored artifact in the same unvalidated, unbounded bucket as free
  text, in a document capped at 12 KB on ESP32.
- **Store only the millisecond**, which is what the research recommends
  ("a snapped sequence is just a sequence — the timing is already baked into the
  step durations") and what the reference does. Rejected: that argument is about
  what crosses the wire to the device, where it is right, and says nothing about
  what the builder's own document should hold. ADR 0046 is directly on point — a
  sequence stores what the builder meant, and a beat-placed step stored only as a
  millisecond is a resolved command.
- **Refuse to draw a grid below a confidence threshold**, as the research
  recommends. Rejected: it makes the analyser an authority that can say no, which
  is the opposite of #331's posture, and every measurement we have came from
  synthesised click tracks — so any threshold would be guessed.
- **A per-module playhead**, as #335 recommended, slaving to CHIRP's sample
  counter where available. Rejected: two mechanisms for about 18 ms, when the
  browser learns whatever clock we pick over HTTP or SSE at some update rate that
  plausibly dwarfs the gain.

## Consequences

- The two analyser defects are a **one-way door**. Grids persist and steps carry
  beat indices, so correcting the 18 ms bias after shipping would silently
  re-resolve every beat-placed step in every saved sequence. They land before the
  first grid is stored or not at all.
- A tempo change needs no undo. A beat-placed step keeps its beat, so re-resolving
  is non-destructive by construction; millisecond-placed steps are untouched.
- Protocol Check gains rules for the tempo block and for a step carrying a beat.
- The per-step beat costs about 10 bytes of JSON, so roughly 1 KB on a 96-step
  branch against a 12 KB per-file cap on ESP32 (`include/seq_store_util.h:60-66`),
  and only on steps actually placed on a beat.
- A per-file MIT notice lands in the analyser source, per #290. The README
  acknowledgement covers everything else this design took.
- **A hazard this decision leans on and does not fix:** the format gate is
  `format != SEQ_JSON_FORMAT` (`src/seq_json.cpp:272-275`), not a comparison, and
  there is no migration anywhere. A rejected file stays on flash, is never
  indexed, and leaves one boot log line (`src/seq_store.cpp:202-207`) — unlike a
  parseable-but-invalid file, which is indexed with `valid = false` specifically so
  the UI can surface it for repair. Choosing an optional key at `format: 1` avoids
  this entirely; anyone who later bumps `format` must deal with it first.
- ADR 0046's evidence table cites `sequence_catalog.cpp:250-290` for `DM:CANTINA`;
  the entry is `239-280` and `282-290` is `DM:ROCKMARCH`. Corrected with this ADR.
