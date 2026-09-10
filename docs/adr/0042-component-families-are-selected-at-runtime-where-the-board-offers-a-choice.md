# Component families are selected at runtime, where the board offers a choice

Status: accepted (2026-09-07, issue #302), amended 2026-09-09. Describes the
**target** model; it lands incrementally, and the code at `abec7b97` implements
none of it yet. **Read the 2026-09-09 amendment at the foot with the Decision:
it retracts one sentence and moves three of the answers above.**

## Context

The config model has three tiers and no answer to "which part is it".

- **Board Capability Gate** (compile time) declares what a board's fitted
  hardware can support. ADR 0029's 2026-08-26 amendment widened it to "a set of
  mutually-exclusive supported options with one default", with drive backend as
  the first consumer, and then **deferred the mechanism**: building the
  `PA_CAP_*` plumbing "waits for a real second backend to design it against".
- **Build Feature Flag** (compile time) declares what this image was built with.
- **Component Toggle** (runtime, staged at reboot) declares that a subsystem is
  fitted and in use.

None of them says *which* member of an interchangeable set is fitted.

Sound is the only category that behaves like a family today: `AudioDriver` is an
abstract base with three concrete drivers and a capability bitmask, so callers
ask what the driver supports rather than which driver it is. But the selection is
compile time — `src/tasks/audio_task.cpp:54-69` picks one behind `#if
PA_AUDIO_DRIVER`, holds it as `static AudioDriver* const`, and `#error`s on a
member that is declared but unbuilt (`AUDIO_DFPLAYER`). A member that does not
exist is a build failure, not a state.

The consumer ADR 0029 was waiting for has now arrived, and it is not a second
backend. The operator-experience epic (#175) requires per-category component
pickers on the Configuration page that are actually selectable, and ADR 0027
already committed to the reason why: "the Public Release Operator model requires
the component toggles to remain browser-configurable in prebuilt release
artifacts". A **Public Release Operator** is defined as someone who never builds
locally. With compile-time family selection, a released binary can drive exactly
one sound module for ever, and the picker is a display, not a control.

The constraint is not uniform, which is why this is not one global answer:

| Board | Category | Set | Selector needed |
|---|---|---|---|
| artoo-esp32 | drive | `{hoverboard}` — PCB traces one UART to S1, no spare | no |
| artoo-esp32 | sound | `{dy-sv5w, chirp, mp3trigger}` — all wire to `PIN_AUDIO_TX = 26` | yes |
| firebeetle2 | drive | wider; ADR 0028 chose the P4 for UART headroom | yes |

## Decision

A **Component Family** is a category of interchangeable hardware whose members
are reached through one interface and asked what they support. A **Component
Member** is the runtime setting naming which member is fitted — a fourth tier,
beside the Component Toggle rather than replacing it: the toggle says the
subsystem is fitted, the member says which part it is. Like a toggle, a change is
staged at reboot.

**A Component Member exists only where the board's Board Capability Gate set has
more than one option.** Where the board's wiring admits exactly one part there is
nothing to choose and no setting — artoo-esp32's drive category has no member,
its sound category does. This keeps the cost proportional to the choice actually
available, and widening a board's set later is an ADR 0029 amendment rather than
a redesign.

**Every family exposes a capability bitmask, and it reaches the browser.** The
sound bitmask already does two jobs: two bits branch firmware behaviour
(`AUDIO_CAP_CATALOG`, `AUDIO_CAP_QUERY_SAFE_PLAYING`) and four are reported so
the browser knows which fields are meaningful (`include/api_audio.h:78`). The
second job is what makes a per-member UI possible without the page hard-coding
member knowledge in JavaScript.

**Firmware owns what the board can be wired for and what this image can drive;
the web owns the roadmap.** Both firmware halves already ship — the identity
X-macro emits `board_capabilities.inc` and `build_flags.inc` to the browser, and
`PA_CAP_DRIVE_BACKEND_HOVERBOARD` is already one of them. A roadmap entry is a
claim about the project that no controller can verify, which #288 established as
a different axis from every state `data/setup.js` resolves. Where the two lists
overlap, **firmware wins**: a part becomes selectable the moment identity reports
it, whatever the web list still calls it.

`AudioDriver` is the template for the *interface* — abstract base plus capability
bitmask. It is not the template for the *instantiation*: the `#if` and the `const`
pointer go wherever a Component Member exists.

## Considered options

- **Keep compile-time selection; the picker displays which member this image
  has.** Zero flash cost on a board with thin headroom, and no new tier. Rejected:
  it makes "selectable" a lie on the Configuration page, and contradicts ADR
  0027's own justification for keeping the operator surface browser-configurable
  in prebuilt artifacts.
- **Extend the Board Capability Gate to name the active member per build.** One
  compile-time mechanism, no NVS key. Rejected for the same reason, plus it
  overloads a *board fact* with an *operator choice* — the distinction CONTEXT.md
  keeps by refusing "capability as a runtime setting".
- **Hybrid: runtime for cheap families, build-time for heavy ones.** Honest about
  real costs. Rejected because ADR 0027 already rejected this shape for toggles —
  "Split contract by class — two contracts to document and audit."
- **Firmware registers roadmap members carrying a state**, so the UI lists them
  without special-casing. Rejected: it puts strings and table entries for
  undrivable parts into an image with 3.5–9% flash headroom, makes every roadmap
  edit a firmware release, and has the controller assert a project fact.
- **Continue deferring the mechanism** per ADR 0029 and ADR 0033's precedent, on
  the ground that no second backend exists. Rejected: the consumer forcing the
  generalization is the operator-experience epic, not a second backend, and
  deferring leaves #303's lineup as a UI promise with nothing behind it.

## Consequences

- ADR 0029's 2026-08-26 deferral of the set/default mechanism ends here. The
  set-with-a-default shape it recorded is unchanged; this ADR decides how a
  member is chosen within that set.
- ADR 0029's "replace, never append" rule still holds: a board declaring a wider
  set declares what a droid could be wired for, never two backends driving output
  concurrently in one running image. A Component Member selects one; it never
  runs two.
- Component Toggle semantics are unchanged. The member is a second, independent
  setting, and a family with one member has a toggle and no member.
- `src/tasks/audio_task.cpp:54-69` loses its `#if` chain and its `const` pointer
  wherever sound has a Component Member. `AUDIO_DFPLAYER`'s `#error` becomes a
  member that is simply absent from the image.
- **Unmeasured risk:** carrying three sound drivers in the artoo-esp32 image has
  not been built or measured. The app partition is 1.625 MB and prior builds land
  1.49–1.57 MB, leaving 57–141 KB depending on framework pool state. If the
  measurement says it does not fit, the fallback is not a new mechanism — it is a
  narrower per-board member set, which this decision already permits.
- The comment in `partitions/partitions_ota.csv` claiming "~25% headroom over the
  ~1.58MB build" is arithmetically wrong: 1.625 MB over 1.58 MB is 2.8%. It is
  the budget this decision leans on, so it is worth correcting.

## Amended 2026-09-09 — a member the project has not built is a row, not an absence

Recorded after a grilling session with the operator on 2026-09-09, which reopened
issue #302 under #175's rule: *"not to re-litigate taste, but when it was argued
from a current limitation rather than from what a builder needs."* The limitation
was the unmeasured flash budget in the Consequences above, and it had been
allowed to decide a question it does not reach.

The distinction the reopen surfaced: **a member the project has not built costs a
row; a member this image cannot carry costs a driver body.** The original pass
merged the two and argued both from the same unknown. A `roadmap` row is an id, a
name, a status and a protocol — nine of them across #305–#313, a few hundred
bytes against a 1.625 MB app partition. That is not the 57–141 KB question.

- **Firmware carries every Component Registry row**, including parts nothing in
  the image drives: id, operator-visible name, category, status, protocol,
  capabilities and board applicability. Drivers are carried only for `supported`
  parts. This reverses the fourth rejected option above.
- **The row carries the operator-visible name.** `make ota` and `make uploadfs`
  are separate steps (`Makefile:170`, `Makefile:188`), so a controller whose
  firmware is newer than its web assets reports parts `data/` has never heard of.
  Identity must be able to name what it reports without help.
- **Identity is the runtime source of the lineup; the `data/` copy is a fallback
  that announces itself.** This replaces "the web owns the roadmap" above. Per
  #298 a settled negative and a transient unknown must not be presented alike, so
  a fallback rendering must say it is one.
- **Capabilities are declared per part.** The Component Family owns the
  vocabulary; each row declares its own bits, a `roadmap` row included. "Every
  family exposes a capability bitmask" above is unchanged in substance — this
  says at what granularity the bits are set.
- **A Component Member setting exists where identity reports more than one
  selectable member**, not where the Board Capability Gate set has more than one
  option. The Gate answers topology; what this image carries is a different
  question, and #303 already parked it as "a separate controller fact the
  identity manifest already reports".
- **Which tier reports a `supported` part missing from an image is decided per
  case with the operator**, at implementation time. It is not pre-committed here.
- **A builder whose `supported` part is not in the image for their board gets
  honesty and nothing more**: identity reports it as not included, with the
  reason. They build firmware or change parts.

### Rejected in the amendment

- **Narrowing the board's member set for flash reasons** — the fallback sentence
  in the Consequences above, now retracted. `include/config.h:167` sets
  `PIN_AUDIO_TX = 26` and `include/audio_chirp.h:7` and
  `include/audio_mp3trigger.h:6` both name that pin, so all three sound modules
  bolt to it. A Board Capability Gate excluding one would state a falsehood about
  topology, and CONTEXT.md already lists *capability as a runtime setting* under
  that Gate's own _Avoid_ line.
- **A release image per member.** `Makefile:218-237` already ships `ota-chirp`,
  `ota-mp3trigger` and `ota-dysv5w`, so this needed no new mechanism. Rejected: it
  makes the download the setting, which is the shape this ADR exists to end.
- **Bounding the lineup by the flash budget** — a part reaching `supported` only
  if it fits alongside the others on every board that can be wired for it.
  Rejected: it lets one board's budget decide the project's lineup, which #303
  refused when it made the lineup not board-bounded.
- **Names in `data/` only, with the drift check refusing a row that has none.**
  Rejected: the check closes the shipped case and not version skew, and skew is
  the case that actually produces a bare id in front of a builder.
- **Capabilities hanging off the Component Protocol**, so a product inherits them
  and two products on one protocol can never drift apart. Rejected: two products
  on one protocol can genuinely differ in what they can be asked.

### Consequences of the amendment

- The fourth rejected option in Considered options above is now the decision. Two
  of the three costs it was rejected for are accepted deliberately: **a roadmap
  edit becomes a firmware release**, and **the controller asserts a project
  fact**. The third — flash for undrivable rows — is answered above.
- The Consequences sentence *"the fallback is not a new mechanism — it is a
  narrower per-board member set, which this decision already permits"* is
  retracted.
- `AUDIO_DFPLAYER`'s `#error` becomes a `roadmap` row **present in the image with
  no driver**, not "a member that is simply absent from the image".
- The **Unmeasured risk** bullet stands as a risk and no longer as an argument:
  it does not support the absent-from-firmware answer, which now rests on the
  rows-versus-driver-bodies distinction. A baselined per-driver measurement needs
  a source change and a build, and is implementation work for the tickets that
  apply this.
- #302's suggested drift-check assertion — *every declared capability has at
  least one consumer* — must be scoped to `supported` rows. A `roadmap` row's
  capabilities have no driver to consume them by construction.
- This deliberately spends ADR 0027's **Public Release Operator** promise for a
  builder whose supported part is cut from their board's image — the same promise
  this ADR's Context cites as its own justification.
- CONTEXT.md's **Component Member** and **Component Registry** entries move, and
  the **Board Capability Gate** _Avoid_ line gains the narrowed-set case.
- #300 cites this ADR only for "`ledc-direct` must remain a selectable member",
  which is unchanged by this amendment.
- **The title is now imprecise and is kept anyway.** "where the board offers a
  choice" states the pre-amendment condition; the condition is what the image
  carries. The filename is linked from #302, #300 and #304, so it is not worth a
  rename — read the title as naming the ADR, not as stating the rule.
