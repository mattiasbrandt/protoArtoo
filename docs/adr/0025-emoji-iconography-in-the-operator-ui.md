# Emoji iconography in the operator UI (issue #112)

A frontend review flagged that the operator UI uses emoji as its icon vocabulary
throughout -- all ten items in the main navigation, the fifteen component status
labels, the mood labels, log-level pills, the servo open/close/stop controls, the
sleep and estop toggles, and the mission snapshot pills. Roughly 360 emoji across
23 served files.

The review raised it as a consistency question rather than a defect, because the
stated design direction for this project is that the UI should read as an
astromech control panel rather than as generic web widgets with emoji icons. The
usage is uniform and clearly deliberate, so it is not drift -- it is a direction
that needed settling before further UI work baked it in deeper.

## Decision

Keep emoji as the icon vocabulary for now. Revisit only as part of a broader
design revamp, not as a piecemeal substitution.

## Why

The current set is coherent and legible, and it does the job an icon set has to
do here: let an operator scan a dense control page and find the row they want.
Replacing it is not a local edit -- it touches nearly every page, and doing it
incrementally would leave the UI in a mixed state that reads worse than either
end point. That cost is only worth paying alongside a wider visual pass, where
the icon language can be designed against the rest of the panel treatment rather
than chosen in isolation.

Asset size favours the same conclusion, though it is not the deciding factor.
The 361 emoji currently in the served assets total 1279 bytes uncompressed, and
close to nothing after gzip, because each is a few UTF-8 bytes reusing a font the
browser already has. An inline SVG icon set would add meaningfully more. That
said, the filesystem has room: assets gzip to roughly 151KB against a 640KB
partition, so a reasonable icon set would fit comfortably. Size is a reason to
prefer emoji at equal value, not a reason that would block icons later.

## Consequences

- Emoji stay. Reviews should not re-raise this as a defect; the deviation from
  the control-panel direction is known and accepted for now.
- New UI should use emoji consistently with the existing vocabulary rather than
  introducing a second icon style alongside it.
- When a broader design revamp happens, iconography is in scope for it, and the
  size headroom above means an SVG set is affordable if that is what the revamp
  wants.
- Emoji carry accessibility obligations that stay in force: anything conveying
  state needs an accessible name, and an emoji must not be the only carrier of
  meaning. Existing controls pair emoji with text labels and should continue to.
