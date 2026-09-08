# A reaction is a binding the droid triggers itself

Status: accepted (2026-09-08, issue #332). Describes the **target** model.
Nothing in it ships today.

## Context

The moments people remember about a droid are the ones nobody fired. protoArtoo
is the only component positioned to produce them: it has the drive state, the RC
input, the audio and the dome link in one place at 50 Hz. The dome cannot see the
sticks; the sound module cannot see the feet. Nothing on this map had asked for
it.

**Two of the three moments the ticket opens with cannot be built on any droid
protoArtoo runs on**, and recording that is half this decision's value.

*A dome that turns when you walk past* needs a proximity sensor. Nothing fits
one, and #322 established there is no IMU, compass, gyro or accelerometer either
— so tilt, bump and pickup are equally out of reach.

*Logic displays that flicker with the music* is not obtainable.
`AudioModuleState` (`include/audio_driver.h:66-72`) is the whole of what a
backend reports: `linkOk`, `playState`, `device`, `totalTracks`, `currentTrack`.
There is no level, no envelope, no amplitude. Audio is a track index on an SD
card behind a UART and the body never holds the waveform — the same fact ADR 0046
recorded from the other side when it ruled out browser-side beat detection. A
light can change *when a sound starts*; it can never follow one.

*Panels that flutter when it accelerates* is obtainable, and the drive path
already carries speed and steer at 50 Hz with nothing reading it.

**One thing the ticket underrated.** It asks whether being handled is detectable
"without a sensor nobody fits", and on a hoverboard it is:
`src/tasks/drive.cpp:171-185` writes per-wheel speed **and current** into
`RobotState` behind a validity flag that clears on loss. A pushed droid
back-drives its wheels; a held one loads its motors.

**And there is a live gap.** Resting behaviour is gated on
`!sleepMode && !estop && !domeSeqActive` (`src/tasks/dome_task.cpp:250`) — not on
whether the droid is driving. Today a random dome movement can begin while the
droid is rolling through a crowd. That is an omission rather than a builder's
choice.

## Decision

**A Reaction is a trigger binding whose source is a droid condition rather than a
radio channel.**

`RcTriggerBinding` (`include/rc_action_types.h:68-78`) already carries
`{source, channel, target, marcduinoPayload, calibration}` — the first half says
where the signal comes from, the second says what to fire. A Reaction swaps the
first and keeps the second, so the action registry, every **Sequence** and the
place a builder authors bindings are all unchanged. What is genuinely new is the
list of things that may be a source.

**A Reaction may react to what the droid is doing and to what its drive feels.**
Drive speed and steer at 50 Hz, a hard stop, going stationary, a track starting;
and per-wheel speed and current where the drive reports them. **A source that
needs drive feedback reports its own Availability Family** on a drive that cannot
provide it, rather than silently never firing (#327).

**A Reaction cannot perceive the room, and the model says so** rather than
leaving it to be discovered.

**Each Reaction carries how long it stays quiet after firing, and one droid-wide
floor separates any two.** The audio path's existing anti-spam beat is the thing
to reuse, not a second idea of restraint.

**While the droid is driving, Resting Behaviour is suppressed, and a Reaction may
light, sound and turn the dome but may not drive a body Part open.**

**The body owns every Reaction.** Only the body can see the triggers; the action
reaches the dome as a command exactly as a **Mood** already does, so this is the
same coordination answer resting behaviour got rather than a second one.

## Why

**The interesting half was never the editor.** A reaction is a trigger and an
action, and protoArtoo has had the second half for a long time — 194 registry
actions, every Sequence, a payload field. Building a third authoring system beside
Sequences and bindings would have spent the session on the half that was already
solved. Swapping one field in a shape that exists costs nothing a builder has to
learn.

**Recording what cannot be sensed is worth as much as deciding what can.** Two of
the three canonical examples are unobtainable, and both would otherwise be
re-proposed every time somebody reads the same list of memorable droid moments.
The audio one is the more dangerous of the two, because it *sounds* achievable —
the body plays the sound, so surely it knows the sound — and the answer is that it
knows an index, not a waveform.

**Feeling the drive is the best character-per-cost on the board.** A droid that
grumbles when shoved is the most alive thing in this ticket, and on a hoverboard
it is free. The cost — it works on one builder's droid and not another's — is
exactly the shape #302 and #303 built capability reporting for, so it is a known
cost with an existing answer rather than a new problem.

**The driving rule is a safety statement, and it closes a real gap.** A charge bay
at knee height opening into a crowd at speed is different in kind from a dome
panel at head height, which is what an audience is watching anyway. Suppressing
resting behaviour while driving costs nothing a builder wanted and fixes an
omission that is in the code today.

## Considered and rejected

**Reacting only to what the droid is doing**, with nothing read back from the
drive. Every reaction would then behave identically on every droid, a shared
routine would mean one thing everywhere, and #304's driver contract would stay
`send(speed, steer)` with nothing reading back. Rejected: it leaves the most
characterful reaction available — being pushed or held — on the table for a
uniformity the **Availability Family** machinery already handles honestly.

**Designing for sensing the room now**, so a walk-past reaction lands the day
somebody fits a sensor. Rejected: nothing would work on any droid today, and the
framework would be shaped by hardware nobody has fitted.

**A fixed set of reactions we ship, each with a tuning knob.** Every droid would
behave recognisably and a support conversation could assume what a reaction does.
Rejected: the set becomes ours to grow one request at a time, and a builder cannot
make the reaction their own droid wants.

**A new authored kind with its own editor** — conditions, thresholds,
combinations, priorities. The only shape that expresses *"only while moving, and
only if it has been quiet a while"*. Rejected: a third authoring system to build,
learn, validate and migrate, for the half that was already solved.

**Reactions unrestricted while driving**, consistent with how a Sequence fired
while driving is already treated. Rejected: it leaves the charge-bay-in-a-crowd
case to the builder, and the ticket names it as a safety question rather than a
taste one.

**Nothing gated at all.** Rejected: it also leaves today's gap open, and that gap
is an omission rather than a choice.

**One droid-wide rate limit and nothing per reaction.** One number to understand
and tune. Rejected: a near-instant reaction and a rare one would share a limit, so
tuning for either breaks the other.

**No limits at all, with the trigger's own thresholds doing the work.** Rejected:
a first attempt at a threshold is exactly where a builder gets it wrong, and the
failure mode is the car alarm.

## Consequences

- **`RcBindingSource` gains droid-condition kinds**, which is a stored-format
  change to a binding a builder may already have. Nothing about the action half
  moves.
- **The drive path gains its first reader.** Speed and steer at 50 Hz, and the
  hoverboard feedback already in `RobotState`, stop being write-only.
- **Resting Behaviour is suppressed while driving**, closing the
  `dome_task.cpp:250` gap.
- **"Audio-reactive" is a closed question, not an open one.** Anything that must
  follow a waveform is authored timing in a **Sequence**, or it is an analogue tap
  nobody has a pin for.
- **#329's answer holds unchanged.** Resting behaviour still never changes on its
  own; a Reaction is the only thing that changes what the droid is doing without
  being asked, and it does so because something happened rather than because time
  passed.
