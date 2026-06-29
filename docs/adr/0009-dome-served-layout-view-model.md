# Dome-served layout view-model, Coordinator Resolution, and the editor/runtime boundary

Issue #17 makes the dome the runtime source of truth for its own layout (geometry,
canonical identities, capabilities, active/disabled state) served at `GET /api/dome/layout`.
This ADR records how the protoArtoo body consumes that contract without violating
[ADR 0008](0008-body-sequences-use-panel-intent.md). The short version: canonical element
IDs and capabilities become an editor/coordinator *view-model*, not a storage format;
saved sequences stay command-string based; and the body firmware never parses the layout.

## Status

accepted (design); implementation is post-v1.0.0, tracked in issue #17

## Context

The dome side of issue #17 is settled separately (a dome-owned composed read model with
structured per-element geometry, canonical IDs, generic capabilities, and operator-maintained
runtime state). The layout contract deliberately exposes no command strings, servo slots,
PCA9685 channels, or bus addresses. This ADR covers only the **body / protoArtoo** half:
how the editor renders from the connected dome, how authoring resolves to commands, and what
stays unchanged at runtime.

The driving tension: issue #17's first draft said "persist saved sequence steps by canonical
element ID and generic capability." Taken literally that flips the saved-sequence storage
format that ADR 0008 deliberately kept command-string based (`{ "type": "dome", "cmd": ":OP01" }`)
and deferred changing. That flip would force a learned-sequence migration, a Protocol Check
rewrite, and a new firmware translation layer, for no v1 benefit.

## Decision

### 1. Canonical IDs + capabilities are a view-model, not a storage format

Canonical element IDs (`P1`, `PP1`, `HP1`, `FLD`, ...) and generic capabilities
(`open`/`close`/`flutter`, `light`/`aim`/`effect`, ...) are the **editor and coordinator
view-model** used to reason about what exists and what is selectable. They are sourced from
the connected dome's `/api/dome/layout`. They are not persisted.

Saved Learned Sequence dome steps remain Panel Intent Command strings per ADR 0008. The
editor holds a transient structured selection (for example `{ element_id: "P1", capability:
"open" }`) and the coordinator resolves it to a command string before save and run. ADR 0008
storage is unchanged; a structured per-step format stays deferred until a separate protoArtoo
ADR supersedes it.

### 2. Coordinator Resolution uses a body-owned command map

The body owns a static `PANEL_COMMAND_TARGETS` table keyed by canonical panel ID and panel
kind, bounded to the MK4 commandable set:

```
P1..P4, P7, P11, P13  -> ring, numeric target (01, 02, 03, 04, 07, 11, 13)
PP1..PP6              -> pie,  alias target  (P1..P6)
```

`resolvePanelCommand(id, capability)` combines the target with a capability prefix
(`open` -> `:OP`, `close` -> `:CL`, `flutter` -> `:OF`), so `P1 + open -> :OP01` and
`PP1 + open -> :OPP1`. The dome contract never carries command strings, so this map must
live in the body and only in the body.

If the layout marks an element `commandable` that is absent from the map, the editor shows it
as unmapped and non-actionable with a diagnostic, and never authors a step for it. Command
behavior is never derived from aliases; aliases are vocabulary/display/search only.

The body also owns the **inverse** map (command string -> canonical ID), needed to re-highlight a
saved step in the live picker. It must key on command *form*, not alias strings, because the token
`P1` is overloaded: `:OP01` decodes to canonical `P1` (ring), but `:OPP1` decodes to canonical
`PP1` (pie). Decode by target form -- numeric target -> ring panel, `P#` alias target -> pie panel.
This is the ADR 0008 "Marcduino mapping mixes pie/ring identities" hazard; handle it explicitly.

### 3. Picker authors panels; non-panel elements decorate

For v1 the layout-driven picker authors **panel** steps only (`:OP`/`:CL`/`:OF`). Holos,
logic displays, and PSI locations are first-class layout elements rendered for spatial context
and availability, but their authoring stays in the existing `DH:`/`DL:`/`DT:` controls from
issue #11 (hardware-verified). The same `element_id + capability` view-model could later unify
non-panel authoring; that is out of this scope.

The v1 actionable capability set (authored through the layout picker) is panels only: `open`,
`close`, `flutter`. Non-panel capabilities (`light`, `aim`, `center`, `test`, `effect`, ...) are
descriptive/context-only in v1; an arbitrary layout capability must not imply new authoring
behavior.

Panel groups (All/Pie/Ring -> `:OP00`/`:OP14`/`:OP15`) stay a body-owned authoring concept,
not layout elements. Group membership is derived from each element's `panel_kind`. Group
availability stays coarse: offered whenever the picker is available, never blocked by
inactive/disabled members, advisory only when zero members are available.

### 4. Body proxies the layout as a thin byte-relay; the browser parses

The browser calls the body's own endpoint, never the dome directly (consistent with every
existing same-origin fetch; no CORS on the dome). The body firmware, when the dome is reachable
over WiFi, performs an outbound GET and relays the bytes without parsing the geometry. All
view-model building, availability derivation, rendering, and caching happen in the browser
(no firmware heap pressure).

Because the layout payload is too large for the UART slip ring, layout fetch requires WiFi
reachability to the dome. A UART-only protoR2link connection means the layout is unavailable
and the browser uses fallback, even though protoR2link itself is "connected."

The body consumes operator suppression (`disabled`) only through the composed `/api/dome/layout`
and does not fetch `/api/dome/element-status` directly; those endpoints are dome UI / dome API
ownership.

### 5. Layout Fallback Hierarchy separates geometry freshness from runtime freshness

1. Live: proxy `200` + supported `schema_revision` -> geometry and runtime availability.
2. Cached live: live fetch fails (`503`/timeout/invalid JSON) but `localStorage` holds a prior
   live layout with a supported schema -> reuse cached geometry, mark runtime availability
   stale/unverified.
3. Vendored MK4 fallback: no usable cache -> offline MK4 model, runtime availability unverified.
4. Unsupported schema -> vendored fallback plus a visible warning; never partially trust
   geometry or state from an unsupported schema, including anything cached from one.

Geometry may be cached or stale; runtime availability is trusted only when freshly live. The
browser caches by `template_id` + `template_revision` + `schema_revision`. Supported schemas
are an explicit set (initially `{1}`). Refetch on editor open, on dome reconnect, and on a
manual retry; no polling.

### 6. The Editor Availability Gate gates authoring, never saved content

New picker authoring requires `in_layout && commandable && mapped && active && !disabled`.
Otherwise the element is visible but non-actionable, or hidden when `in_layout:false` (surfacing
only as a diagnostic if an existing saved step references it).

Existing saved steps always load, edit, and save. When the connected layout reports a target
as unavailable, the editor shows a non-blocking advisory by severity tier: `inactive`
(advisory), `disabled` (maintenance, operator-suppressed), `in_layout:false` (layout mismatch),
`unmapped` (coordinator cannot author new steps). Save is never blocked by availability --
disabled is local and may be temporary, and blocking save would trap unrelated edits.

### 7. The layout is an editor read-model; runtime is unchanged

The dome layout is an editor-time artifact only. Body firmware does not parse it, and runtime
sequence execution (RC, web, dome RX triggers) does not depend on the layout or the browser
cache. Saved command-string sequences run exactly as authored; an unwired or disabled panel is
no-op'd by the dome. Protocol Check stays purely structural/safety and gains no availability
dependency. If runtime availability awareness is ever needed, it arrives as a small separate
non-geometry status summary, not firmware layout parsing.

### 8. Live picker quality bar

The live-rendered picker is interaction-grade with `data/panels.html` as the quality baseline:
correct hit targets, recognizable MK4 layout, well-placed labels (`label_anchor`), visible
mounted relationships, callouts (`callout`), correct stacking (`render_order`), and clear
inactive/disabled state. It need not reach CSS or pixel parity with the vendored SVG. The
vendored MK4 SVG remains the offline fallback look.

## Considered options

- **Flip saved-sequence storage to structured `{target_id, capability}` now.** Rejected: it
  expands scope well beyond v1, forces a learned-sequence migration and a Protocol Check
  rewrite, and violates ADR 0008's deliberate deferral. The view-model gives the same authoring
  ergonomics without changing the storage contract.
- **Hybrid dual-write (store both cmd string and structured fields).** Rejected: dual-write
  invites reconciliation bugs with no concrete migration need driving it.
- **Browser fetches the dome directly.** Rejected: breaks the browser-to-body-only pattern,
  requires CORS on the dome, makes the browser resolve the dome address, and duplicates fallback
  logic. It also fails whenever the dome is reachable only via the body.
- **Body firmware parses and re-composes the layout.** Rejected: puts the heaviest part of the
  contract (geometry JSON) into the most constrained place (heap-limited firmware with a history
  of OOM/TWDT) for no gain, since the dome already composes runtime state.
- **Derive command targets from layout aliases.** Rejected: re-introduces the hidden coupling
  that the dome contract just removed. Aliases are vocabulary, not command semantics.

## Consequences

- `data/dome_panel_model.js` splits: the durable canonical-id -> command-target map becomes
  the body-owned `PANEL_COMMAND_TARGETS`; the geometry/SVG becomes the offline fallback model.
- The body gains a browser-side layout fetch/cache/view-model module and an SVG renderer that
  builds the picker from structured geometry primitives plus `label_anchor`/`callout`/
  `render_order`.
- The body firmware gains one thin proxy endpoint (relay + reachability handling); no parsing.
- The `tools/check_dome_panel_drift.py` checker is superseded once `/api/dome/layout` is the
  live source; it remains useful only while the vendored copy is the primary path.
- Issue #17's body scope wording is corrected so "persist by canonical element ID" reads as the
  editor/coordinator view-model, not the saved-sequence storage format.
