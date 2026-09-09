# #289 prototype - DM:CANTINA as a timeline

Read-only horizontal timeline of a real Factory Sequence, built so the
representation question can be answered by reacting to something rather than by
argument. **One artifact, deliberately - not three variants to choose between.**

Open `cantina-timeline.html` in a browser. Nothing here talks to a controller.

## What it is made of

| Source | What is taken from it |
|---|---|
| `src/tasks/sequence_catalog.cpp:242` | the real `DM:CANTINA` steps: `SEQ_LOOP(100, 26, 1846, 14000)` over a 26-step body at relative 0 ms and 923 ms |
| `docs/droid-parts.yaml` | part ids, PP/P/HP shorthand, real bearings, and each part's `control:` state |

1846 ms is four beats at 130.0 BPM and 923 ms is two, so the beat grid is
derived from the sequence rather than typed in.

## What it is for

Flipping **Loop as authored** against **Loop expanded** is the question. The same
fourteen seconds either as the one 4-beat object a builder made, or as the 182
panel commands the droid receives - which is what today's card list shows.

Two further things it demonstrates rather than argues:

- **A block is a span, not an event.** Every panel command is instantaneous;
  the block is derived by pairing `:OP` with `:CL` on the same Part. That
  pairing is the whole case for a timeline.
- **Three kinds of quiet lane** - nothing drives it, driven but untouched here,
  driven and used - kept visually distinct, because collapsing them rebuilds the
  defect #298 undid.

## What it deliberately does not do

Read-only: no drag, no resize, no inspector, no snapping (specific 1 - land
read-only first). No Gesture appears, because `DM:CANTINA` predates ADR 0046 and
is hand-written as 26 individual commands.

Colour follows #327: amber and red stay reserved for *you can act on this* and
*this is stopped or refused*, so neither appears on a block or a lane. Dark only.
