# Board capability gates are compile-time; component toggles stay runtime

Dual-target support (ADR 0028) means features will exist that the artoo-esp32
board can never run — subsystems needing the ESP32-P4 chip's extra UARTs or
PSRAM, and Board Variant capabilities such as the FireBeetle 2's fitted C6 and
Hosted WiFi topology. Hosted WiFi is not an ESP32-P4 chip property. The
artoo-esp32 build (4 MB flash, tight heap floor) must not pay for unavailable
features, and "off via a runtime toggle" is not enough: ADR 0027 made disabled
components inert, but their code is still linked and their flash is still spent.

We decided on a **Board Capability Gate** tier: compile-time `PA_CAP_*`
flags, set by the chip-target and board-variant layers, declaring what a
board's hardware **can** do.

- A feature gated on an absent capability is **not linked** into that board's
  image at all — no code, no tasks, no buffers, no flash.
- The web UI shows three states per gated feature: **not-on-this-board**
  (capability absent), **off**, and **on** (capability present, governed by
  its runtime Component Toggle per ADR 0027).
- The action registry gains a capability field; `make check-action-drift`
  verifies registry entries against the gates.
- Per-env flash/RAM **build-size budgets** are asserted in the verification
  gate and in CI, so capability spill into the artoo-esp32 image fails a PR
  loudly instead of eroding the heap floor silently.

This does not reopen ADR 0027's rejection of compile-time toggle mirrors.
The two tiers answer different questions and neither substitutes for the
other:

- A **Component Toggle** declares what fitted hardware the operator uses. It
  is runtime by requirement — a Public Release Operator configures it from
  the browser — and per-droid.
- A **Board Capability Gate** declares what the board's silicon and pin map
  can support. It is invariant for a given PCB: no browser setting can add a
  fourth UART to an artoo-esp32. Making it compile-time costs operators
  nothing.

Where a capability is present, the Component Toggle contract (off = inert,
staged at reboot) applies unchanged.

## Considered options

- **Runtime-everything** (ship all features on all boards, rely on ADR 0027
  toggles) — spends artoo-esp32 flash and link-time RAM on code that can
  never run there; contradicts the no-spill requirement. Rejected.
- **Ad hoc `#ifdef` per feature** — no single audit surface, no registry
  linkage, and capability drift between code, UI, and docs goes undetected.
  Rejected in favor of named `PA_CAP_*` gates checked by the drift checker.
- **Folding capabilities into the Component Toggle tier** — would force
  toggles to become build flags, which ADR 0027 explicitly rejected for
  operator-configurability reasons. The tiers stay separate. Rejected.

## Consequences

- The three-state UI and registry capability field are new operator-facing
  and contributor-facing surfaces; `check-action-drift` guards them.
- Per-env budgets become part of the standard verification sequence and CI.
- Baseline flash/RAM numbers are recorded per env when the budgets are
  introduced.

## Amended 2026-08-24

The decision stands; its scope broadens, recorded after a grilling session on
#186:

- **Two compile-time tiers, one mechanism.** The registry annotation, the
  image-reported manifest, the drift check, and the UI state also cover the
  developer build-stripping tier that ADR 0027 mentioned without naming — now
  the **Build Feature Flag** (a per-env `PA_*` 0/1 flag such as
  `PA_HEAP_PROFILE`). The tiers stay distinct: a capability is invariant per
  PCB, a build flag is a developer choice per image, and neither is a
  Component Toggle. ADR 0027 is unchanged.
- **Four UI states, not three:** not on this board / not in this build / off
  / on. Unavailable rows stay visible with the reason; nothing is discovered
  by probing endpoints.
- **Registry entries declare their requirements explicitly** — at most one
  board capability and one build flag each; absent means universal. Component
  toggles become registry entries so the registry is the single grain.
- **Flags are always defined as 0 or 1 and tested with `#if`**, enumerated
  from X-macro manifests every board block must satisfy; the same manifests
  feed the drift checker and the manifest the identity resource reports.

The budgets consequence landed with #187. The first consumers are the
existing profiler and admission-trace gates on artoo-esp32, migrated as the
reference; Hosted WiFi's capability is declared by the board layer under #186
and consumed by the network-seam tickets.

Rejected in the session: one `PA_FEATURE_*` namespace for both tiers (erases
the invariant-per-PCB versus per-image distinction and muddies the UI
message); hiding unavailable rows (an operator cannot tell "not fitted" from
"does not exist here"); defined-or-absent `#ifdef` flags (a typo silently
gates a feature out, which the budget gate cannot catch on the P4 side).
