# Icon set provenance

The operator surfaces carry no emoji (ADR 0066). Where a glyph earns its place
beside a label, it is an icon from **Material Design Icons 7.4.47**
(Pictogrammers), taken as unmodified SVG path data from the `@mdi/svg` package
and injected as an inline `<symbol>` sprite by `data/shell.js`.

The sprite is inline rather than a served `.svg` file for two reasons: an
external sprite is a second request in the opening burst the controller
deliberately sheds connections in, and the markup is smaller than the request
that would fetch it.

## Licence

Material Design Icons is released by the Pictogrammers group. The icons are
distributed under the **Apache License 2.0**
(<https://www.apache.org/licenses/LICENSE-2.0>). The package's own notice, read
from `@mdi/svg`'s `LICENSE` and reproduced verbatim:

> ## Pictogrammers Free License
>
> This icon collection is released as free, open source, and GPL friendly by
> the [Pictogrammers](http://pictogrammers.com/) icon group. You may use it
> for commercial projects, open source projects, or anything really.
>
> ### Icons: Apache 2.0 (https://www.apache.org/licenses/LICENSE-2.0)
> Some of the icons are redistributed under the Apache 2.0 license. All other
> icons are either redistributed under their respective licenses or are
> distributed under the Apache 2.0 license.
>
> ### Fonts: Apache 2.0 (https://www.apache.org/licenses/LICENSE-2.0)
> All web and desktop fonts are distributed under the Apache 2.0 license. Web
> and desktop fonts contain some icons that are redistributed under the Apache
> 2.0 license. All other icons are either redistributed under their respective
> licenses or are distributed under the Apache 2.0 license.
>
> ### Code: MIT (https://opensource.org/licenses/MIT)
> The MIT license applies to all non-font and non-icon files.

The paths are not covered by this repository's MIT grant; see LICENSE scope
item 5.

## The paths this firmware carries

Only the symbols the chrome and a swept surface actually draw are in the image:
a `<symbol>` nothing references is bytes in a 640 KiB filesystem for no reason.
`tools/check_surface_anatomy.py` fails the build on a reference that resolves to
no symbol, which is the failure this list would otherwise hide.

| MDI name | Where it is drawn |
|---|---|
| `view-dashboard-outline` | Dashboard, in the nav rail |
| `steering` | Foot Drive, in the nav rail |
| `rotate-360` | Dome, in the nav rail |
| `volume-high` | Sound, in the nav rail |
| `controller-classic-outline` | RC Control, in the nav rail |
| `timeline-outline` | Sequences, in the nav rail |
| `tune-variant` | Configuration, in the nav rail |
| `puzzle-outline` | Parts, in the nav rail |
| `connection` | Wiring, in the nav rail |
| `robot-outline` | Servos, in the nav rail |
| `wifi` | WiFi, in the nav rail |
| `chip` | Firmware, in the nav rail |
| `wrench-outline` | Maintenance, in the nav rail |
| `stop-circle-outline` | the Latching Estop, in the topbar |
| `power-sleep` | the Dashboard's Sleep act |
| `restart` | the Dashboard's Restart act |
| `console-line` | reserved for the Controller Console |
| `chevron-right` | a disclosure's open/closed marker |
| `arrow-left`, `arrow-right` | Foot Drive's pad, the pair turned a quarter for forward and reverse |
| `printer-outline` | Wiring's printable wiring sheet act (#411; read from `@mdi/svg` 7.4.47 `svg/printer-outline.svg`, not one of the #398 twenty-two) |
| `lightbulb-outline` | Lights, in the nav rail (#410; read from `@mdi/svg` 7.4.47 `svg/lightbulb-outline.svg`, not one of the #398 twenty-two) |
| `magnify` | Find by Moving, on a Part's row on Wiring (#411; read from `@mdi/svg` 7.4.47 `svg/magnify.svg`, not one of the #398 twenty-two) |
| `play`, `stop` | Play and Stop on a Sequence's row on the Dashboard (#451; read from `@mdi/svg` 7.4.47 `svg/play.svg` and `svg/stop.svg`, not among the #398 twenty-two) |
| `fullscreen`, `fullscreen-exit` | the Dashboard's full-screen posture, in and out (#451; read from `@mdi/svg` 7.4.47 `svg/fullscreen.svg` and `svg/fullscreen-exit.svg`) |
| every act's icon in [One act, one icon](#one-act-one-icon) below | the act's row there names each surface it is drawn on (#460; read from `@mdi/svg` 7.4.47 `svg/<name>.svg`, path data unmodified) |

The #398 prototype (`git show 83acf0db:prototypes/395-surface-anatomy/chrome.js`;
the prototype left the tree for the gitignored `tasks/prototypes/` on
2026-09-18) holds
the full twenty-two paths it took from the same package, including the two this
image does not carry - `play-outline` and `pause`. A slice that needs one copies
its path in beside the others rather than fetching a package to read it again.

`connection` was the first of the ones left out that a slice needed. Wiring
(#350) is the destination the prototype's own nav row named `connection`, so
landing that surface was one path copied in and no package fetched.
`wrench-outline` came in the same way with Maintenance (#404), the row the
prototype drew it on, and Configuration kept the `tune-variant` the Setup page
it split from had worn.

`robot-outline` is the one choice the prototype did not make for us: it left
Servos out of the rail because `CONTEXT.md` **Activity Group** does not list it
and its fate is #364's, so the shipped nav needed an icon the reference had no
row for. It was picked from the twenty-two already taken rather than adding a
twenty-third from an unread source.

## One act, one icon

Every action button on an operator surface wears the icon of its act (#460),
and the same act wears the same icon on every surface: a builder finds an act
by its shape before reading its words. One icon never means two acts. The
table is the map; a new act takes a row here, a name checked against the
`@mdi/svg` 7.4.47 package, and its path copied into `ICONS` beside the others.

An act shows its icon alone, its words kept in the button as its accessible
name and shown by one shared tooltip on hover and keyboard focus. That is a
trial the operator decides at the look (ADR 0066 still says an icon keeps its
label): the class `act-words` on `<html>`, set by `ACT_WORDS` in
`data/shell.js`, puts every act's words back beside its icon. The switch does
not undo the card-scale fix that came with the sweep: about eighteen
page-scale `.btn`s on cards (Sound, WiFi, guided Setup's footer,
Configuration's Retry now) became `.btn-sm`, deliberately, and stay so.

**Acts that keep their words.** An act carries class `act-keeps-words`, and
shows its icon AND its words whatever the switch says, with no tooltip, when
any of these holds (coordinator, 2026-10-04):

- it moves something on the droid and no line on the page explains it
  without hover (ADR 0059): Dome's Go home; Sequences' Move the droid to this
  moment, Test, Test on the droid, Play on the droid, Perform, Perform again;
  Wiring's find by moving; the Dashboard's Play and Stand Down; Parts' Open
  it / Close it; RC Control's Try it; Servos' move, open, close, stop, back to
  centre and Set on ticked outputs. The servo dial's nudges, reverse, test
  sweep and take it again are not here: the dial's own line explains them;
- its words carry a count, a name or a warning: Set on all N ticked outputs,
  Restore N ticked parts, Apply suggestions (N), Remove N steps, Stop <name>;
- it answers a question (#456, "every question names what each answer
  does"): both answers of every `PAOverlay.ask()` question, the move question
  on Servos and Wiring, Maintenance's restore question, and the three
  Sequences dialogs;
- its words are the only place a state in progress is said: Sound's Poll
  status while it reads Polling. RC Control's Detecting has its own banner,
  so Detect channel stays icon-only.

Not acts, so not here: the nav rail, the Latching Estop, Foot Drive's pad,
toggles (Sleep, Full screen, Bulk, safe range, use these ends), segmented
pickers and tabs, swatches and pills, disclosures (More, settings, a wiring
card), and links that go to another surface.

The starter map in #460 named `play-outline` for a run. #451 had already drawn
`play` for Play and Test, so a run wears `play` everywhere and `play-outline`
is not carried.

| Act | Icon | Where it is drawn |
|---|---|---|
| Find by moving | `magnify` | Wiring, on a Part's row; RC Control's Detect channel. One act: both find a thing by moving it - a free Output on the droid, a stick on the radio |
| Save | `content-save-outline` | Configuration's Save; RC Control's Apply and save; Sequences' Save; Sound's Save on a row, Save changes, Save range, Save intervals; WiFi's Save settings; Servos' Set on ticked outputs |
| Download a copy | `download-outline` | Maintenance's Download backup and Save a copy first, then restore; Sequences' Export |
| Load a file onto the droid | `upload-outline` | Maintenance's Choose a file to restore, Restore the ticked parts, Replace; Sequences' Restore backup, Choose file, Restore and edit; Firmware's Upload and flash, Upload the web UI, and Upload it (the question either asks before an upload) |
| Run on the droid | `play` | the Dashboard's Play; Sequences' Test, Test on the droid, Play on the droid; RC Control's Try it; Sound's Play |
| Record a take | `record-circle-outline` | Sequences' Perform and Perform again |
| Stop what is running | `stop` | the Dashboard's Stop; Sequences' Stop; Find by Moving's Stop; RC Control's Stop while detecting; Sound's Stop; Servos' stop |
| Edit | `pencil-outline` | Sequences' Edit and Tune |
| Delete | `delete-outline` | Sequences' Memory Wipe, Wipe it, Remove; Parts' Drop from build; Wiring's remove |
| Re-read | `refresh` | Sound's Poll status and Refresh catalog; Dome's Reload; WiFi's Refresh; Configuration's Retry now |
| Add | `plus` | Parts' Add and Add to build; Servos' + part |
| Undo, redo | `undo`, `redo` | Sequences |
| Duplicate | `content-copy` | Sequences |
| Drop unsaved edits | `restore` | Sequences' Revert and Discard edits; RC Control's Revert draft |
| Keep things as they are | `close` | every question's answer that keeps things (`data/overlay.js`); the move question on Servos and Wiring; Maintenance's Keep what I have; Sequences' Keep what I have, Keep editing, Keep it, and Close (the same button, once a wipe has left RC bindings to read) |
| Done, accept | `check` | Configuration's Done and Finish; Servos' done; Sound's Done; Sequences' Keep and Use; Find by Moving's That one |
| Back | `arrow-u-left-top` | Configuration's guided Setup Back; Sequences' All sequences (it wore `arrow-left`, which is Foot Drive's Left) |
| Next | `arrow-u-right-top` | Configuration's guided Setup Next |
| Leave the run | `exit-to-app` | Configuration's guided Setup Stop here |
| Restart the Body Controller | `restart` | the Dashboard's Restart; Maintenance's Restart the Body Controller and Restart it (its question's answer); Firmware's Reboot; WiFi's Reboot to Apply |
| Wake | `sleep-off` | the Dashboard's sleep overlay |
| Stand Down | `human-handsdown` | the Dashboard |
| Use as Stand Down | `pin-outline` | Sequences |
| Front is here | `target` | Dome |
| Go home | `home-outline` | Dome |
| Web control on, off | `lan-connect`, `lan-disconnect` | Foot Drive |
| Move to a position | `ray-start-arrow` | Servos' move; Sequences' Move the droid to this moment |
| Open, close | `arrow-expand-horizontal`, `arrow-collapse-horizontal` | Servos; Parts' Open it and Close it |
| Calibrate | `ruler` | Servos |
| Pulses off | `power-plug-off-outline` | Servos, on a row and on the dial |
| Back to centre | `format-horizontal-align-center` | Servos |
| Nudge down, up | `minus-circle-outline`, `plus-circle-outline` | Servos' dial |
| Reverse the ends | `swap-horizontal` | Servos' dial |
| Hold it again | `hand-back-right-outline` | Servos' dial, take it again |
| Test sweep | `arrow-left-right` | Servos' dial |
| Tick all, clear ticks | `checkbox-multiple-marked-outline`, `checkbox-multiple-blank-outline` | Servos |
| Map, unmap | `link-variant`, `link-variant-off` | Sound's Map, Map checked and Clear; RC Control's Unmap, Clear all mappings and its questions; Parts' Take it off on Wiring (for a Part off the droid with an Output still mapped); Wiring's take off. Unmapping one and clearing them all are one act on a different reach, told apart by Clear all mappings' danger colour |
| Move a Part or a mapping | `transfer` | the move question on Servos, Wiring and Parts; RC Control's Move it |
| Apply suggestions | `auto-fix` | Sound |
| Random on, off | `shuffle-variant`, `shuffle-disabled` | Sound |
| Share | `share-variant-outline` | Sequences' Share to project |
| Off the beat | `music-note-off-outline` | Sequences |
| Retime to the grid | `grid` | Sequences |
| Tap along, tap | `metronome`, `gesture-tap` | Sequences |
| Analyze a track | `waveform` | Sequences |
| Split into steps | `arrow-split-vertical` | Sequences |
| Run guided Setup again | `compass-outline` | Maintenance |
| Clear a search | `eraser` | RC Control's action search |
| Print | `printer-outline` | Wiring's wiring sheet |
| Timeline | `timeline-outline` | Sequences, on a factory row |
