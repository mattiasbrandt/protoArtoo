# #398 prototype - the Surface Anatomy on three surfaces

Three static mockups of the shape every operator surface will follow (ADR 0066,
`CONTEXT.md` **Surface Anatomy**): the Operator Shell's chrome with
**Dashboard**, **Parts** with both projections and the calibration dial in its
place, and guided **Setup** with one question step and the finish receipt. Built
so the look can be picked by reacting to something rather than by argument.
**One artifact, deliberately - one look on three surfaces, not three palettes to
choose between.**

Open `dashboard.html`, `parts.html` or `setup.html` in a browser, straight from
the file. `setup.html#receipt` opens on the finish receipt. Nothing here talks
to a controller: every value is fictional, every name is real.

## What it is made of

| Source | What is taken from it |
|---|---|
| `CONTEXT.md` **Surface Anatomy**, **Operator Shell**, **Activity Group**, **Status Plate**, **Status Colour**, **Availability Family** | the shape, the nav's groups and members, the plate's eight cells in order, the two reserved colours, the settled-no treatment |
| the thirteen patterns on #398 | every section head, voice, card, table, dial and empty state below |
| `tasks/operator-interface-mocks/` (the Codex study, 2026-09-14/15, not selected) | the first draft of the identity: dark plates, the blue as the one accent, the line-drawn droid on a grid, the brandmark, the MDI icon subset, *Planned* for later waves |
| `data/shell.js` | the Status Plate's cell order, labels and values (`ESTOP CLEAR`, `DRIVE ARMED`, `SPD 600`), the estop's fixed consequence line, the nav's group hints |
| `data/dashboard.html`, `data/parts.html`, `data/setup.html` | Dashboard's controls and health signals, Parts' two tables and their columns, Setup's product families |
| `docs/droid-parts.yaml` | every Part name, shorthand and group on Parts: 58 rows, none hidden |
| `include/component_registry.inc` | the Sound family's four products and which are supported |
| `data/asset-sets/legacy/_product_art.html` | the four Sound drawings on Setup's cards, one hand across the set (ADR 0065) |
| `docs/ui-copy-voice.md` | every sentence: a heading carries a count, a consequence sits beside the control, no hover-only explanation |

## From the Codex study: kept, changed, dropped

The study is the closest thing to the decided identity that existed and the
operator asked for it to be continued. What follows is what this prototype does
with each of its parts, and why.

| Codex study | Here | Why |
|---|---|---|
| Dark navy and charcoal plates, cobalt as the one accent | **Kept**, re-tokened: four grounds, two seams, three inks, one blue | The identity ADR 0066 decided. The palette shrinks to what the anatomy needs so a colour has one job |
| Silver as a second hue for the droid drawing | **Dropped**; the drawing is blue strokes with the secondary ink for the body | "the droid's blue as the single accent" - a silver stroke is a second accent by another name |
| Numbered mono section labels (`01 / DROID IDENTITY`) | **Dropped as a general rule; kept where the content is a sequence** - the Setup step rail is numbered, sections are not | A number encodes order. Dashboard's three plates and Parts' two tables are not in an order, and a numbered label on a surface with one section is ornament. The mono uppercase title with a bottom rule stays (pattern 1) |
| Left nav rail, groups as `<details>` | **Kept as a rail at computer resolution; becomes a top strip at 1100 px and below**, groups always open | See "Rail or strip" below |
| Status Plate: eight cells, fixed at the bottom | **Kept**, recessed into a well with one freshness cell | #324's substance re-drawn; fixed so it is read by muscle memory at every scroll position |
| Estop as chrome with its consequence line | **Kept**; the state line and the consequence line are two fixed lines beside the button | ADR 0048, `docs/ui-copy-voice.md` rule 12 |
| Line-drawn R2 on a grid as identity | **Kept** on Dashboard; the grid is drawn from the seam token so it is the dome's panel lines, not a texture | The one place this look spends its boldness |
| Brandmark | **Kept** | Original artwork, not MDI |
| MDI 7.4.47 SVG subset, 29 paths | **Kept as the mechanism**; 22 paths taken, one inline `<symbol>` sprite injected by `chrome.js` | An external sprite cannot be reached from a page opened from a file, so it is inlined; the list is below |
| Calibration dial in a modal (`calibration.png`) | **Changed: a panel below the table** that follows the row you press Calibrate on, every control present at once | Pattern 5 says never a modal; the reference project's `.calpanel` is amber-bordered and sits under the table |
| Parts: `By part` / `By output` as a toggle with one table visible | **Changed: both projections on the page**, one under the other | They are two projections of one mapping (`CONTEXT.md` **Parts**) and today's page already shows both; a toggle hides the half a builder is not looking at, which is the class of defect #296 undid |
| Parts: an abstract body arrangement drawing beside the table | **Dropped** | The Body View is C4's and inherits pattern 8; a numbered-box arrangement is not that drawing and would be re-drawn |
| Parts: 14 body rows | **Changed: all 58 catalog rows**, body groups first, each group heading carrying its count | No row is ever hidden (#296); a mockup that hides 44 rows teaches nothing about the density decision |
| Setup: picker cards with generic "equipment" art, `Selected in example` / `Supported product` pills, a "Something else" card | **Changed: the legacy set's own drawings**, pills `supported` / `fitted` / `roadmap` / `your answer`, no "Something else" | ADR 0065 already drew the products; the fourth card kind is #369's and is not decided here |
| Setup: a rail of steps beside the nav rail, both visible | **Changed: the step rail takes the nav rail's column** | Setup is a takeover, not a destination, and its rail carries the answered questions and nothing else (`CONTEXT.md` **Setup**). Two rails side by side was the second door #288 closed |
| Astromech Terminal collapsed on Dashboard | **Kept as a collapsed disclosure, named Controller Console** | One name per concept: it is the Controller Console (`docs/console-client.md`) |
| `LOCAL MOCK · EXAMPLE DATA` notice strip above the chrome | **Dropped** | The README says it once; a strip above the topbar changes the chrome being judged |
| Battery voltage readout | **Dropped** | Nothing on the controller measures it; a readout may only print what something measured |
| Green for a live plate cell | **Changed to the blue** | #327 gives colour two meanings. Green was a third; a cell doing its job is the accent, like a selected row |

### Rail or strip

ADR 0048 decided grouped nav and left its placement open. **A left rail at
computer resolution, a top strip at 1100 px and below**, drawn from one list:

- There are thirteen entries in four groups and two of them appear twice. A
  single strip cannot show the groups and the members on one row at 1440 px
  without the second row today's nav already has, which is what reads as a
  generic tab bar.
- The rail's border is a panel seam: the frame gets a vertical line the way the
  dome has them, and the current surface lights a two-pixel length of it.
- The reference project's top strip serves four workspaces, not thirteen
  destinations; its `04-sidebar.css` is where its own section head lives.
- Below 1100 px the same list wraps into a strip with the group labels inline
  and the hints hidden, so the tablet Dashboard (#327's one tablet surface)
  spends its width on the controls.

## The anatomy's parts, on each mockup

| Pattern | Dashboard | Parts | Setup |
|---|---|---|---|
| 1 Section head: one builder, subtitle a count / state / provenance / purpose | `This droid · artoo · artoo.local`, `Controls · Driving · Mid-Awake · awake`, `Readouts · what the plate leaves off`, `Health · 7 signals · all ok`, `Perform from the stand · planned` | `Parts · 58 parts · 4 on an output · 54 not wired`, `Calibrating · Upper utility arm · ARM1 (ledc:0) · held`, `Outputs · 5 outputs · 3 driving parts · ...`; every group row carries its count | `Sound modules · Component Registry · 3 supported · 1 roadmap`, `Answers · 8 answered · 1 skipped · 3 wait for a restart` |
| 2 Three voices, one reading measure | `.hint` under every control, `.prose` for Sleep, no note | `.hint` cue lines, `.note-act` in the dial (amber: act on this) | `.prose` for the why, `.note-info` consequence (blue: information), `.note-act` on the receipt (restart) |
| 3 Title, then the question it answers | `Dashboard` / *What is the droid doing right now, and is it well?* | `Parts` / *Which output moves each part, and what does each output move?* | `Set your droid up · Sound` / *Where does the droid get its voice?* / two sentences |
| 4 Cards: picture, name, pill, sentences; consequence line under the choice | - | - | five cards, one settled and inert; *This picks which module the body talks to, not which sounds it has* |
| 5 Dial as a panel below the table, never a modal | - | `.calpanel` amber-bordered, `.calbody` flex gap 22, dial 240 px, slider, nudges, µs field, three captures, hint block, action bar with state controls left | - |
| 6 Table answers which; the form follows the clicked row and says so | - | *This panel follows whichever row you press Calibrate on*; the row shown carries the blue seam on both tables; ticked rows are checkboxes; in-use rows are plain | the step rail is the table, the question is the form |
| 7 Rows that move hardware sit apart; the count rides in the label | - | *Apply this release to all 2 ticked outputs* on one row; *Centre all 3 wired outputs* / *Switch off all 5 outputs* on their own row | - |
| 8 A drawing and its list are one selection model | - | not drawn: the Body View is C4's and inherits this | - |
| 9 Empty state is `<b>Nothing X.</b>` plus the act | - | an unwired row reads `– not wired –` and carries *Find by moving*; an unfitted row carries *Fit it* | the roadmap card: *Not built yet. Nothing to do here.* and no act, because a settled no has none |
| 10 Dialog title is a question, buttons are verbs | - | (the move dialog is not mocked; Parts' shipped one already does this) | - |
| 11 Status chips: one recessed plate, verb-free, fixed positions, no telemetry | the plate on every page; uptime, heap and signal are Dashboard's **Readouts** | same plate | same plate |
| 12 A moved thing leaves a forwarding address | - | *Looking for Servos? ... Calibrate on each row here* under the title | the receipt says where each answer is changed from now on |
| 13 Density is the bench's | button labels two words, sentence case; no emoji | 40 px rows, mono 12 px addresses, hints 10 px | pills 9 px mono, card copy two sentences |

Find by Moving appears on the Dataport door row mid-run - *nudging AUX2 · spare
output 1 of 2 · did the dataport door move?* with *That one* / *Next* / *Stop*
- and as a plain *Find by moving* act on every other unwired, fitted row.

## Numbers the research does not give

Decided here, used on all three surfaces, inherited by the sweep. Each is a
token at the top of `anatomy.css`.

| Number | Value | Why |
|---|---|---|
| Table row height | **40 px** | The bench's density: a 13 px name and a 12 px mono address fit with 12 px above and below, and 40 px is a finger's target on the bench tablet |
| Group row in a table | 32 px | A heading, not a target |
| Gutter (main's side padding) | **24 px** at computer resolution, 16 px at 1100 and below | One step of the ladder either side of the content |
| Spacing ladder | 4 / 8 / 12 / 16 / 24 / 32 px | A rule that wants a step between two of these picks one |
| Nav rail | **200 px** | The longest member name (*Body servo controller* on the Setup rail) at 13 px plus a 16 px icon and 12 px padding |
| Status Plate height | **56 px** one row of eight; 100 px as two rows of four at 900 px and below | A 9 px label over a 12 px value with 8 px above and below |
| Topbar | 60 px | Holds a 44 px estop with its two lines beside it |
| Type scale | 10 hint / 11 section / 13 cell / 14 body / 16 h2 / 22 h1 / 28 readout, all px | Each step carries a job; nothing else is a size |
| Reading measure | 70 ch, on `.prose` only | Pattern 2 |
| Breakpoints | **1100 px**: rail becomes a strip, the Dashboard bay goes to two columns. **900 px**: bays and grids stack, the plate folds. 600 px: the mood and planned-target grids go to one column | 1440 and 820 both land in a stable layout; 820 is the tablet width #327 names |
| Sticky columns | The first column (the Part's or Output's name) is `position: sticky` when a table scrolls sideways | `docs/ui-copy-voice.md` rule 4's example: the name never leaves the screen |
| Dial | 240 px | The reference's `.caldial`; a 28 px readout and its unit fit inside the hub |
| Corner radius | 2 px plates, 3 px buttons | An instrument plate is chamfered, not rounded; the estop keeps 3 px so it reads as a button |
| Print | Chrome, rail, plate and acts hidden; tokens re-pointed at paper inside `@media print` | A printed bench sheet is ink on paper. It is not a light theme (#327 stands) - nothing is switchable on screen |
| Fonts | System stacks only: `system-ui` and `ui-monospace` with fallbacks | A page opened from a file has no network; the shipped image has no room for a face either |

## Side by side

Today's surfaces were captured from the staged image
(`.pio/build/artoo_esp32/fsdata_gz`, served gzip with `/api` stubs) at 1440 x
1000, on 2026-09-16 at `1b37b1fd`. The mockups were captured from these files
at the same size.

### Dashboard

| Today | Mockup |
|---|---|
| ![Today's Dashboard](today-dashboard.png) | ![Mock Dashboard](mock-dashboard-1440.png) |

### Parts

| Today | Mockup |
|---|---|
| ![Today's Parts](today-parts.png) | ![Mock Parts](mock-parts-1440.png) |

The calibration panel and the output-first table sit below the parts table:

![Mock Parts, the dial and the outputs](mock-parts-dial-1440.png)

### Setup

Today's Setup is the configuration page; guided Setup is C3a's (#351) and
does not exist yet, so the comparison is against the surface a builder sets a
droid up on today.

| Today | Mockup, one question step |
|---|---|
| ![Today's Setup](today-setup.png) | ![Mock Setup](mock-setup-1440.png) |

The finish receipt:

![Mock Setup receipt](mock-setup-receipt-1440.png)

### 820 px

| Dashboard | Parts | Setup |
|---|---|---|
| ![Dashboard at 820](mock-dashboard-820.png) | ![Parts at 820](mock-parts-820.png) | ![Setup at 820](mock-setup-820.png) |

## What it deliberately does not do

- No sweep: nothing under `data/`, `src/` or `include/` changes. The mockups
  propose; the sweep the operator's pick mints is what records.
- Servos' fate (#364), the fourth card kind (#369) and the nav's members (#288,
  decided) are not reopened. Servos is not in the rail because `CONTEXT.md`
  **Activity Group** does not list it; the forwarding line on Parts is where
  the anatomy keeps the answer either way.
- No Body View (C4), no move dialog, no light theme, no second density, and no
  tablet layout beyond what the breakpoints give every surface for free.
- Nothing moves. Every act is a button with nothing behind it; the two pieces
  of script draw the tables and the dial from data and switch Setup's two views.

## Verification

Both checks are in this directory and were run on 2026-09-16.

- `check.py`: no colour literal outside `:root` in `anatomy.css`, no
  pictographic character in any source, every icon `<use>` resolves to a
  symbol `chrome.js` defines.
- `check.js`: pasted into the browser on each page at 1440 and 820 px wide -
  no document-level overflow, eight plate cells, the estop and the plate inside
  the viewport, one visible title, no emoji in the rendered text.
- The `check.js` runs were made in a real, headed Chromium through the
  Playwright MCP, which refuses `file:` URLs, so the pages were served from
  this directory over a scratch HTTP server for those runs. The `mock-*.png`
  captures were made by headless Chromium opening the same files from
  `file://` with no server at all, which is the ticket's opening condition.
  Nothing in the pages depends on which; they fetch nothing.

## Icons

Material Design Icons **7.4.47** (Pictogrammers), unmodified SVG paths from
`@mdi/svg`, Apache License 2.0. The licence is `Apache-2.0.txt` and the
package notice is `MDI-LICENSE.txt`, both beside this file. The twenty-two
paths taken, by MDI name, all in `chrome.js`:

`view-dashboard-outline`, `steering`, `rotate-360`, `volume-high`,
`controller-classic-outline`, `timeline-outline`, `tune-variant`,
`puzzle-outline`, `connection`, `wrench-outline`, `wifi`, `chip`,
`stop-circle-outline`, `power-sleep`, `restart`, `console-line`,
`chevron-right`, `robot-outline`, `arrow-left`, `arrow-right`, `play-outline`,
`pause`.

The brandmark and the droid drawing are original artwork from the Codex study,
not MDI. The four product drawings on Setup are the project's own legacy asset
set (ADR 0065).

## Files

| File | What it is |
|---|---|
| `dashboard.html`, `parts.html`, `setup.html` | the three mockups |
| `anatomy.css` | the one stylesheet; tokens once in `:root` |
| `chrome.js` | the shell's chrome drawn once: icon sprite, topbar, rail, plate |
| `check.py`, `check.js` | the checks above |
| `today-*.png`, `mock-*.png` | the side-by-side captures |
| `MDI-LICENSE.txt`, `Apache-2.0.txt` | the icon set's notice and licence |
