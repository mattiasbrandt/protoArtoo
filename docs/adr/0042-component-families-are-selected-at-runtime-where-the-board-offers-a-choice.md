# Component families are selected at runtime, where the board offers a choice

Status: accepted (2026-09-07, issue #302). Describes the **target** model; it
lands incrementally, and the code at `abec7b97` implements none of it yet.

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
