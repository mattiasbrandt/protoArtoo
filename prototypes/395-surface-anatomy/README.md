# #398 prototype - the Surface Anatomy on three surfaces

Three static mockups of the shape every operator surface will follow (ADR 0066,
`CONTEXT.md` **Surface Anatomy**): the Operator Shell's chrome with
**Dashboard**, **Parts** with both projections and the calibration dial in its
place, and guided **Setup** - the Body Controller step, one question step and
the finish receipt. Built so the look can be picked by reacting to something
rather than by argument. **One artifact, deliberately - one look on three
surfaces, not three palettes to choose between**, and since 2026-09-16 that one
look on both of protoArtoo's Body Controllers.

Open `dashboard.html`, `parts.html` or `setup.html` in a browser, straight from
the file. `setup.html#board` opens on the Body Controller step and
`setup.html#receipt` on the finish receipt. Nothing here talks to a controller:
every value is fictional, every name is real.

The topbar carries a **board switch**, and it is the one control here that is
not part of the look: protoArtoo ships two Body Controllers whose images carry
different pictures, so the switch shows the same surfaces on either one. What
it does and does not change is [Both boards, one look](#both-boards-one-look)
below.

## What it is made of

| Source | What is taken from it |
|---|---|
| `CONTEXT.md` **Surface Anatomy**, **Operator Shell**, **Activity Group**, **Status Plate**, **Status Colour**, **Availability Family** | the shape, the nav's groups and members, the plate's eight cells in order, the reserved colours, the settled-no treatment. **Status Colour** as written is amended by the operator's 2026-09-16 decision on the signal lights, below; **Availability Family** is not touched |
| the thirteen patterns on #398 | every section head, voice, card, table, dial and empty state below |
| `tasks/operator-interface-mocks/` (the Codex study, 2026-09-14/15, not selected) | the first draft of the identity: dark plates, the blue as the one accent, the MDI icon subset, *Planned* for later waves. Its two pieces of original artwork, the brandmark and the line-drawn droid, are both dropped - see below |
| `data/shell.js` | the Status Plate's cell order, labels and values (`ESTOP CLEAR`, `DRIVE ARMED`, `SPD 600`), the estop's fixed consequence line, the nav's group hints |
| `data/dashboard.html`, `data/parts.html`, `data/setup.html` | Dashboard's controls and health signals, Parts' two tables and their columns, Setup's product families |
| `docs/droid-parts.yaml` | every Part name, shorthand and group on Parts: 58 rows, none hidden |
| `include/component_registry.inc` | the Sound family's four products and which are supported; the two Body Controllers, and the `PA_BOARD` test that decides which one an image carries |
| `platformio.ini:359`, `:522` | which asset set each board's image is built with - `legacy` on `artoo_esp32`, `default` on the FireBeetle 2 family |
| `data/asset-sets/legacy/_product_art.html` | the Sound drawings and the Artoo PCB drawing on Setup's cards, one hand across the set (ADR 0065) |
| `data/asset-sets/default/*.webp` | the photographs the same cards draw on the FireBeetle 2, referenced where they live rather than copied in |
| `include/config.h:75,80`, `src/tasks/dome_link.cpp:16-25` | `PA_CAP_DEDICATED_AUDIO_UART`, the one Board Capability Gate any copy on these three surfaces turns on |
| `docs/ui-copy-voice.md` | every sentence: a heading carries a count, a consequence sits beside the control, no hover-only explanation |

## From the Codex study: kept, changed, dropped

The study is the closest thing to the decided identity that existed and the
operator asked for it to be continued. What follows is what this prototype does
with each of its parts, and why.

| Codex study | Here | Why |
|---|---|---|
| Dark navy and charcoal plates, cobalt as the one accent | **Kept**, re-tokened: four grounds, two seams, three inks, one blue - and, since 2026-09-16, four signal-light colours that are not the blue | The identity ADR 0066 decided. The palette shrinks to what the anatomy needs so a colour has one job. The blue's job is interaction; a health signal is an LED and gets an LED colour |
| Silver as a second hue for the droid drawing | **Dropped**, and then the drawing with it | "the droid's blue as the single accent" - a silver stroke is a second accent by another name. The drawing itself went on 2026-09-16, the row below |
| Numbered mono section labels (`01 / DROID IDENTITY`) | **Dropped as a general rule; kept where the content is a sequence** - the Setup step rail is numbered, sections are not | A number encodes order. Dashboard's three plates and Parts' two tables are not in an order, and a numbered label on a surface with one section is ornament. The mono uppercase title with a bottom rule stays (pattern 1) |
| Left nav rail, groups as `<details>` | **Kept as a rail at computer resolution; becomes a top strip at 1100 px and below**, groups always open | See "Rail or strip" below |
| Status Plate: eight cells, fixed at the bottom | **Kept**, recessed into a well with one freshness cell | #324's substance re-drawn; fixed so it is read by muscle memory at every scroll position |
| Estop as chrome with its consequence line | **Kept**; the state line and the consequence line are two fixed lines beside the button | ADR 0048, `docs/ui-copy-voice.md` rule 12 |
| Line-drawn R2 on a grid as identity | **Dropped** (operator, 2026-09-16). *This droid* is a readout instead: Droid Build, Body Controller, Firmware, Uptime | Two reasons. **Nothing like it ships** - `data/dashboard.html` has no drawing at all, only the seven health rows this mockup already reproduces, so the silhouette was new decoration in a slot that has never held a picture. And **`CONTEXT.md` "Body View" puts a recognisable droid silhouette in its `_Avoid_` list**: the real drawing of the droid is an *arrangement, not a likeness* (#317, ADR 0063), so a photo-real outline on the surface a builder most expects the real thing teaches the wrong model of what the Body View will be. No replacement picture and no placeholder for one: the Body View is C4's (#352, #372-#374) and is not built |
| Brandmark | **Dropped** (operator, 2026-09-16). The top-left corner is the droid's name and its `R2-D2 Body Controller` line, and nothing else | Two reasons. ADR 0066's identity is the **inside** of the droid, an instrument panel: a portrait of the droid, seen from outside, in the top-left logo slot is precisely the generic-web-app convention this revamp exists to leave - the operator is meant to be inside the thing, not looking at a picture of it. And it did not survive its own size: drawn on a 40 x 44 viewBox and rendered at 26 x 30 px, its three 3-unit body slots and 4-unit dome eye land at about 2 px and read as mud. The name is the part of that corner that is the builder's own anyway |
| MDI 7.4.47 SVG subset, 29 paths | **Kept as the mechanism**; 22 paths taken, one inline `<symbol>` sprite injected by `chrome.js` | An external sprite cannot be reached from a page opened from a file, so it is inlined; the list is below |
| Calibration dial in a modal (`calibration.png`) | **Changed: a panel below the table** that follows the row you press Calibrate on, every control present at once | Pattern 5 says never a modal; the reference project's `.calpanel` is amber-bordered and sits under the table |
| Parts: `By part` / `By output` as a toggle with one table visible | **Changed: both projections on the page**, one under the other | They are two projections of one mapping (`CONTEXT.md` **Parts**) and today's page already shows both; a toggle hides the half a builder is not looking at, which is the class of defect #296 undid |
| Parts: an abstract body arrangement drawing beside the table | **Dropped** | The Body View is C4's and inherits pattern 8; a numbered-box arrangement is not that drawing and would be re-drawn |
| Parts: 14 body rows | **Changed: all 58 catalog rows**, body groups first, each group heading carrying its count | No row is ever hidden (#296); a mockup that hides 44 rows teaches nothing about the density decision |
| Setup: picker cards with generic "equipment" art, `Selected in example` / `Supported product` pills, a "Something else" card | **Changed: the project's own asset set** - the legacy drawings on the Artoo PCB and the default photographs on the FireBeetle 2 - with pills `supported` / `fitted` / `roadmap` / `your answer`, and no "Something else" | ADR 0065 already drew the products and #382 already photographed them; the fourth card kind is #369's and is not decided here |
| Setup: a rail of steps beside the nav rail, both visible | **Changed: the step rail takes the nav rail's column** | Setup is a takeover, not a destination, and its rail carries the answered questions and nothing else (`CONTEXT.md` **Setup**). Two rails side by side was the second door #288 closed |
| Astromech Terminal collapsed on Dashboard | **Kept as a collapsed disclosure, named Controller Console** | One name per concept: it is the Controller Console (`docs/console-client.md`) |
| `LOCAL MOCK · EXAMPLE DATA` notice strip above the chrome | **Dropped** | The README says it once; a strip above the topbar changes the chrome being judged |
| Battery voltage readout | **Dropped** | Nothing on the controller measures it; a readout may only print what something measured |
| Green for a live plate cell | **Restored, and made a system** (operator, 2026-09-16). It was changed to the blue here first; that was wrong and is reversed | See [Signal lights](#signal-lights-a-health-signal-is-a-droid-led). A blue live cell made one hue mean both *you selected this* and *this is alive*, which is the doubling #327 exists to stop; and a droid's own panel does not report health in one colour |

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

## Signal lights: a health signal is a droid LED

**Operator, 2026-09-16.** A health signal reads as a droid LED and takes a real
LED colour. This is the one decision here that **amends `CONTEXT.md` "Status
Colour" as written** (*"Colour carries exactly two meanings"*); the operator is
amending that term, and this prototype does not touch `CONTEXT.md` or any ADR.

It does **not** touch `CONTEXT.md` **Availability Family**: *why* something is
unusable is still treatment and never hue, so no roadmap card, no `checking` and
no refusal is coloured. This is *how a thing is doing right now*, a different
axis.

| State | Token | Means | Where it is |
|---|---|---|---|
| nominal | **`--green`, new: `#3fbe6e`** | clear, armed, linked, mounted, reporting | five plate cells and five health rows |
| attention | `--amber` `#e2a13a` | degraded, reporting but not right, and you can do something | the Memory health row |
| stopped | `--red` `#e05555` | latched, refused, faulted, cut | the estop; no cell is in this state on the mockup |
| not reporting | `--ink-3` dim, unlit | never asked, not fitted, switched off | the Dome ESC health row |
| a chosen posture | no colour at all | `SPD 600`, `SLEEP OFF`, `CONTROL OFF` | three plate cells |

The last row is not new: it is the rule `data/shell.js:402-404` already states
and `chrome.js` already carried - *values, never exceptions; a chosen posture
takes no colour*. Grey there is correct and stays.

**There is no second orange.** `--amber` already is the orange, and its existing
meaning - *you can do something about this* - is what a degraded LED says.
A `--orange` beside it would be one hue under two names.

**The blue goes back to interaction only.** It had been lighting a live plate
cell as well as marking selection, which is one hue carrying two meanings -
the defect #327 exists to kill. After this change nothing that reports a state
is blue: selection, the row shown, `.btn-primary`, the dial arc, the focus ring
and `note-info` are all still the blue, and all of them are interaction.

**Measured, not eyeballed.** `--green` was picked to read as text and not only
as a 7 px dot. Contrast against this palette's three grounds, 2026-09-16:

| Colour | on `--plate` | on `--well` | on `--ground` |
|---|---|---|---|
| `--green` `#3fbe6e` | **7.49:1** | 8.31:1 | 8.05:1 |
| `--amber` `#e2a13a` | 7.99:1 | 8.87:1 | 8.59:1 |
| `--red` `#e05555` | 4.76:1 | 5.29:1 | 5.12:1 |

It clears 4.5:1 everywhere and sits at the same weight as the amber it stands
beside. The text treatment itself is unchanged - the **lights** carry the
colour, which is what the decision is about, and a chip's value stays ink
except for the existing red on `stopped`. The green is legible as text if the
sweep later wants it there, without re-picking the hex.

**The mock shows the system rather than describing it.** All seven health rows
used to read `OK`, so no reader could see a colour scheme at all. Each now
carries the word its evaluator really reports (`data/health_signals.js`: `ok` /
`warn` / `fail` / `off`, each with a `reason` string) instead of a bare label,
and two are off nominal:

- **Memory · Low**, amber. The one signal that disagrees with the readout beside
  it *on purpose*: the evaluator keys on the largest allocatable block, not on
  free heap, because that is what the device's admission control sheds requests
  against (`health_signals.js:86-100`). 173 kB free with a 14.2 kB largest piece
  is fragmentation, and a builder has to be able to read it. The Readouts panel
  now says the same thing in its own voice.
- **Dome ESC · Disabled**, unlit. `evaluateDomeEsc`'s `off` branch, which is
  what a dome ESC that has not been switched on reports.

**No plate cell is amber, and that is a finding rather than an omission.** The
shipped plate has no amber state anywhere in its vocabulary: every chip reader
returns `live`, `stopped`, or no class at all (`data/shell.js:405, 414-470,
541`). Making one amber would have invented a state the product does not have,
and each of the three cells that could honestly go grey - RC LINK, DOME LINK,
SOUND LINK - has a health row reporting the same fact, which would then have to
be made to disagree with it. So the plate shows green and unlit, the health list
adds amber, and red is on the estop beside them. One real pair exists if the
operator wants amber on the plate: `SOUND LINK · DOME HAS BUS` (grey,
`shell.js:541`) with `Sound · Status unavailable` (amber,
`health_signals.js:187`) - the same fact in both vocabularies, reachable only on
`artoo_esp32`, so it would become a fifth thing that follows the board switch.

## Both boards, one look

protoArtoo runs on two Body Controllers, and their images do not carry the same
pictures. The asset set is a **build** flag, one per board: `platformio.ini:359`
gives `artoo_esp32` `custom_asset_set = legacy`, the set of line drawings;
`:522` gives `firebeetle2_bringup` `custom_asset_set = default`, twenty
photographs, and `[env:firebeetle2]` extends it, so the product image carries
them. `tools/gzip_fsdata.py:243-269` resolves the set and flattens it onto the
image root, which is why a page asks for `/dy_sv5w.webp` and never for a set
path.

The three surfaces are drawn for both. The topbar switch flips between them and
**everything that genuinely differs follows it. Nothing else does.**

### What follows the switch

| What | On `artoo_esp32` | On `firebeetle2` | Where that is decided |
|---|---|---|---|
| Every product card's picture | the legacy set's line drawing | the default set's photograph | `platformio.ini:359`, `:522`; the card's own fallback is `data/setup.js:457-466` |
| The board picture | the `art-artoo_pcb` drawing | `firebeetle2.webp` (#382) | the same fallback: the legacy set defines a symbol per product, the default set's partial is deliberately empty of art and says so |
| The Body Controller's name - on Dashboard's identity plate, in Setup's step rail, on its step and on its receipt | *Artoo PCB (artoo.uk)* | *FireBeetle 2 (ESP32-P4)* | `include/component_registry.inc:106-107` - `included` tests `PA_BOARD`, so an image carries exactly one of the two |
| The Sound question's last sentence | *one serial port left over and two things that want it* - while the dome is talking the module cannot be asked what is playing, and the plate says `SOUND LINK · DOME HAS BUS` | *a serial port to spare* - the module can be asked whatever the dome is doing | `PA_CAP_DEDICATED_AUDIO_UART`, 0 and 1 at `include/config.h:75,80`; the handoff itself is `src/tasks/dome_link.cpp:16-25, 864-874` and is compiled out on the P4; the plate's wording is `data/shell.js:541` |

### What does not, and why it must not

A mockup that invents a difference teaches the sweep a false rule, so each of
these was checked rather than assumed.

| Looks board-specific | Is not | Read from |
|---|---|---|
| The five body Outputs, `ARM1` to `AUX3`, and their `ledc:0` - `ledc:5` addresses | identical | `include/ledc_pwm.h:87-93` is outside any `PA_BOARD` test. Only the GPIO behind each name differs (`include/config.h:199-203` against `:319-323`), and no surface shows a GPIO |
| The Sound family's four products and their `supported` / `roadmap` pills | identical | rows 18-21 of the registry declare no gate and the same `included` on both |
| Every other product in the 21-row vocabulary | identical | the only `included` expression naming a gate is the hoverboard's, and `PA_CAP_DRIVE_BACKEND_HOVERBOARD` is 1 on both boards (`include/config.h:73,79`). The two Body Controller rows are the only ones that differ |
| The Status Plate's eight cells | identical | the cell list in `data/shell.js` is not board-gated. `DOME HAS BUS` is a value only the artoo-esp32 can reach, but it is a value of a cell both boards have, and both mockups show the cell reading `OK` |
| Parts as a whole | identical | nothing on it names a product, a picture or a board capability. Flipping the switch on Parts changes the switch and nothing else, which is the honest answer. Dashboard differed in nothing either until 2026-09-16, when the identity plate replaced the droid drawing and gained a Body Controller row |

### The switch is a review convenience

The shipped image has no runtime toggle and cannot have one: it carries the set
it was built with. The switch exists so the two can be compared without two
builds and two flashes, which is what the operator asked for. It lives in the
topbar because that is where the ticket asks for it, and it is drawn with a
dashed border and the word **Mockup** so it is never judged as chrome. The two
lines under each option name the set and what it holds, so the control explains
itself without a hover (`docs/ui-copy-voice.md` rule 12). The Codex study made
the same disclaimer about its own query-string router.

Each option is named with the **board's** own label, `Artoo Controller` and
`FireBeetle 2` (`data/setup.js:396-399`), not with the product name's stem: on
`artoo_esp32` the board is the *Artoo Controller* and the product fitted to it
is the *Artoo PCB (artoo.uk)* (`include/component_registry.inc:106`). The switch
chooses a board, so it takes the board's word; the Body Controller card, which
names a product, takes the registry's.

The state rides in the URL - `?board=firebeetle2` - and `board.js` carries it
across the mockup's links, so walking from Setup to Parts does not quietly land
back on the other board. A capture therefore has an address.

### Dressing a photograph against a drawing

The reference project's finding is that a photograph on a dark plate reads as a
bright slab in a hole unless it gets a white plate behind it. **It does not
apply to our set**, and the reason is worth carrying into the sweep:

- **Our photographs are already cut out on a dark ground.** Measured off the
  outer two-pixel frame of all twenty pictures in `data/asset-sets/default` on
  2026-09-16: every one of them is exactly `#0c1524`. So the plate behind a
  photograph is painted that ground (`--photo-ground`) instead of the well, and
  the picture melts into the plate with only the seam showing. Without it, a
  4:3 picture in a wider plate reads as a lighter rectangle inside a darker one
  - the same defect in miniature. (The legacy sprite's own fallback says
  `#0c1525`, one off in blue; nothing here paints with it.)
- **A photograph cannot take the selection tint.** A drawing inherits
  `currentColor`, so the selected card's art turns blue with its border. A
  photograph does not, so on the default set the selection is carried by the
  card's border and wash alone. It still reads, but the sweep should not add a
  third selection treatment for one set.
- **A picture plate needs a definite height.** `.card-art` centres its item,
  which leaves a replaced element's percentage height unresolved: the
  photograph laid itself out at its own 4:3 from the card's width and spilled
  47 px past the plate, over the pill beneath it. `align-self: stretch` gives
  the height something to resolve against. A drawing never showed this, because
  an inline `<svg>` is not sized that way.
- **One cut-out is rougher than the others.** `dfplayer_mini.webp` carries a
  visible light fringe where it was cut out. Nothing here fixes it: it is the
  asset's, not the layout's.

### Where the board picture lives

Today's Setup surface carries the board picture at its head
(`data/setup.html:27-34`), so the mockup keeps it on Setup - as the art of the
**Body Controller step**, the one step in the run that is identified rather than
asked. That step is `setup.html#board`, and it is a third view of the same file
beside the question step and the receipt.

One card, never two: the registry gates the two products on `PA_BOARD`, so the
image carries one of them and the other is not offered, dimmed or mentioned.
The plate is 300 x 225, the asset set's own 4:3, so a photograph fills it
exactly and the drawing sits on the same frame.

## The anatomy's parts, on each mockup

| Pattern | Dashboard | Parts | Setup |
|---|---|---|---|
| 1 Section head: one builder, subtitle a count / state / provenance / purpose | `This droid · artoo · artoo.local`, `Controls · Driving · Mid-Awake · awake`, `Readouts · what the plate leaves off`, `Health · 7 signals · 5 ok · 1 low · 1 off`, `Perform from the stand · planned` | `Parts · 58 parts · 4 on an output · 54 not wired`, `Calibrating · Upper utility arm · ARM1 (ledc:0) · held`, `Outputs · 5 outputs · 3 driving parts · ...`; every group row carries its count | `Body Controller · Component Registry · 2 boards · this image carries one`, `Sound modules · Component Registry · 3 supported · 1 roadmap`, `Answers · 8 answered · 1 skipped · 3 wait for a restart` |
| 2 Three voices, one reading measure | `.hint` under every control and under the Sleep readout, no `.prose` at all, no note | `.hint` cue lines, `.note-act` in the dial (amber: act on this) | `.prose` for the why, `.note-info` consequence (blue: information), `.note-act` on the receipt (restart) |
| 3 Title, then the question it answers | `Dashboard` / *What is the droid doing right now, and is it well?* | `Parts` / *Which output moves each part, and what does each output move?* | `Set your droid up · Body Controller` / *What is protoArtoo running on?*; `Set your droid up · Sound` / *Where does the droid get its voice?* / two sentences |
| 4 Cards: picture, name, pill, sentences; consequence line under the choice | - | - | five Sound cards, one settled and inert, plus the Body Controller card on its own step - one card, never a choice, because the registry gates it on `PA_BOARD`; *This picks which module the controller talks to, not which sounds it has* |
| 5 Dial as a panel below the table, never a modal | - | `.calpanel` amber-bordered, `.calbody` flex gap 22, dial 240 px, slider, nudges, µs field, three captures, hint block, action bar with state controls left | - |
| 6 Table answers which; the form follows the clicked row and says so | - | *This panel follows whichever row you press Calibrate on*; the row shown carries the blue seam on both tables; ticked rows are checkboxes; in-use rows are plain | the step rail is the table, the question is the form |
| 7 Rows that move hardware sit apart; the count rides in the label | - | *Apply this release to all 2 ticked outputs* on one row; *Centre all 3 wired outputs* / *Switch off all 5 outputs* on their own row | - |
| 8 A drawing and its list are one selection model | - | not drawn: the Body View is C4's and inherits this | - |
| 9 Empty state is `<b>Nothing X.</b>` plus the act | - | an unwired row reads `– not wired –` and carries *Find by moving*; an unfitted row carries *Fit it* | the roadmap card: *We intend to carry it. Not yet.* - the intent and the timing, with no act, no date and no destination, because a settled no has none. The `roadmap` pill above it already says what kind of thing it is and the card is visibly inert, so the copy restates neither (operator, 2026-09-16) |
| 10 Dialog title is a question, buttons are verbs | - | (the move dialog is not mocked; Parts' shipped one already does this) | - |
| 11 Status chips: one recessed plate, verb-free, fixed positions, no telemetry | the plate on every page, its lights green where a signal is nominal and unlit where a posture was chosen; heap and signal are Dashboard's **Readouts**, and the firmware version and uptime are its **This droid** plate | same plate | same plate |
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
| Board picture | **300 x 225 px** | The asset set's own 4:3 (every picture in both sets is 400 x 300), so a photograph fills the plate exactly and the drawing sits on the same frame |
| Health row | 40 px, three columns from `minmax(150px, 1fr)`, **8 px** of side padding and a **4 px** gap | The table row's height, so a signal reads at the same rhythm as a row. The padding and gap come off the ladder's 12 and 8 because a signal now carries its evaluator's word rather than a bare `OK`, and at three columns the longest pair (*RC Receiver* with *Frames ok*) wrapped to two lines and made the grid ragged. The margin is more than the two pixels that took: `system-ui` resolves to different faces in different browsers, and the same row wrapped in one Chromium and not in another at the same width |
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

### Both boards

The same Setup surface, the switch flipped. Only the pictures, the Body
Controller's name and the last sentence of the consequence line differ; the
layout, the copy and the numbers are the same two renders.

| Artoo Controller, the legacy set's drawings | FireBeetle 2, the default set's photographs |
|---|---|
| ![Setup on the Artoo Controller](mock-setup-1440.png) | ![Setup on the FireBeetle 2](mock-setup-firebeetle2-1440.png) |

Dashboard's identity plate, which names the board it is running on:

| Artoo Controller | FireBeetle 2 |
|---|---|
| ![Dashboard on the Artoo Controller](mock-dashboard-1440.png) | ![Dashboard on the FireBeetle 2](mock-dashboard-firebeetle2-1440.png) |

The Body Controller step, which is where the board picture lives:

| Artoo Controller | FireBeetle 2 |
|---|---|
| ![The Body Controller step on the Artoo Controller](mock-setup-board-1440.png) | ![The Body Controller step on the FireBeetle 2](mock-setup-board-firebeetle2-1440.png) |

The finish receipt differs by one line - the Body Controller's name - and is not
captured twice for that.

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
- Nothing moves. Every act is a button with nothing behind it; the script draws
  the tables and the dial from data, switches Setup's three views, and resolves
  each picture against the board the switch is on.
- No change to `CONTEXT.md` or to any ADR, the signal-light decision included.
  The operator is amending **Status Colour** themselves; a prototype proposes.
- No second set of files for the second board, and no difference invented for
  it. Dashboard and Parts are byte-for-byte the same surface on both boards
  apart from the switch's own state, because nothing on either one names a
  product, a picture or a board capability.

## Verification

Both checks are in this directory and were run on 2026-09-16, over both boards.

- `check.py`: no colour literal outside `:root` in `anatomy.css`, no
  pictographic character in any source, every icon `<use>` resolves to a
  symbol `chrome.js` defines, **every product card's picture exists on both
  boards** - a drawing this page defines and a `.webp` in the default set - and
  every `data-board-only` block names a board `board.js` has.
- `check.js`: pasted into the browser on each view at 1440 and 820 px wide and
  on each board - no document-level overflow, eight plate cells, the estop and
  the plate inside the viewport, one visible title, no emoji in the rendered
  text, every picture resolved the same way on one render, and none of the
  other board's copy on the screen.
- The `check.js` runs were made in a real, headed Chromium through the
  Playwright MCP, which refuses `file:` URLs, so the pages were served over a
  scratch HTTP server **rooted at the repository**, since the photographs are
  referenced two directories up. The `mock-*.png` captures were made by headless
  Chromium opening the same files from `file://` with no server at all, which is
  the ticket's opening condition. Nothing in the pages depends on which; the
  photographs are the only thing either of them fetches, and they are local.
- The picture check is proven able to fail rather than assumed: pointing one
  card at a product that has neither a drawing nor a photograph, renaming the
  Artoo PCB symbol, and naming a third board in a `data-board-only` block each
  turn `check.py` red (exit 1) with the offending name printed.

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

Nothing here is original artwork any more: the Codex study's brandmark and its
line-drawn droid were both dropped on 2026-09-16, and every icon is MDI. The
product drawings on Setup - the four Sound modules and the Artoo PCB - are the
project's own legacy asset set (ADR 0065), and the photographs the FireBeetle 2
shows in their place are its default set.

## Files

| File | What it is |
|---|---|
| `dashboard.html`, `parts.html`, `setup.html` | the three mockups |
| `anatomy.css` | the one stylesheet; tokens once in `:root` |
| `chrome.js` | the shell's chrome drawn once: icon sprite, topbar with the board switch, rail, plate |
| `board.js` | which board the mockup is showing, and everything that follows it: the asset-set rule, the copy that turns on a board capability, the switch carried across links |
| `check.py`, `check.js` | the checks above |
| `today-*.png`, `mock-*.png` | the side-by-side captures, including both boards |
| `MDI-LICENSE.txt`, `Apache-2.0.txt` | the icon set's notice and licence |
