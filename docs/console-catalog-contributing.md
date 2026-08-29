# Contributing to the Console catalog

For anyone adding or changing a `docs/action-registry.yaml` entry that the
Controller Console needs to know about (any entry, since every registry
entry reaches the Console catalog). This page covers the Console-specific
half of that contract; the general "how to add a registry entry" steps
(naming, RC token, drift check) are in `action-registry.yaml`'s own header
comment — read that first.

## Where a registry entry ends up

`tools/generate_console_catalog.py` reads `docs/action-registry.yaml` and
writes `src/console/console_catalog.cpp` (a flash-resident C table, one
`ConsoleCatalogEntry` per operation) and `data/console_help.txt` (the prose
half — description, display name, executor — read from LittleFS at
runtime, addressed by offset and length). Both are generated, checked-in
files: run the generator after editing the registry, and treat the two
generated files as **DO NOT EDIT MANUALLY**, exactly as their own header
comments say. Neither `pio run` nor `make check-action-drift` regenerates
them for you.

## Fields the Console reads that a plain RC/REST entry might not carry

- **`executor`** — the function or core name the Console's `help <op>`
  reply shows as its `executor` field. Every entry needs a matching row in
  `tools/console_inventory/` (below) whose `executor_or_core` names the
  same thing — `make check-action-drift` checks this by name matching
  against that inventory, never against the C++ source directly, so it
  cannot verify the named function actually exists, only that the two data
  sources agree on it.
- **`board_capability`** / **`build_flag`** — each names at most one
  declaration from `include/board_capabilities.inc` /
  `include/build_flags.inc`. Absent means universal for that tier (ADR
  0029). These drive the catalog's `available_on_board` /
  `available_in_build` fields, which `help` and `operations` show — but see
  the caveat below on what actually gets checked at run time.
- **`rc_token`** — if present, it becomes the operation's one Console alias
  automatically (`tools/generate_console_catalog.py`'s
  `build_rc_token_map()`). There is no separate "Console alias" field to
  fill in; one `rc_token` is one alias, generated, not hand-maintained.
- **`params`** — `name`, `type`, `required`, and (for numeric types) `range`,
  or `values` for an enum. These populate the in-image
  `ConsoleParamDescriptor` table `include/console_args.h`'s schema
  validator checks a typed command against — get the type/range/enum right
  here and the Console enforces it with no executor-side code of its own.
- **`fields`** / **`is_query`** — status entries only, and every `type:
  status` entry needs one of three shapes, or `make check-action-drift`
  rejects it (`check_status_query_classification()`,
  `tools/check_action_registry_drift.py`):
  1. **Field-based query** — carries `fields:` (a list of the API JSON keys
     it answers with, verbatim) — a standalone, scalar-answer query like
     `system.status.health`.
  2. **Item-based query** — no `fields:`, but `is_query: true` stated
     explicitly — a standalone query whose answer is a sequence of `item`
     records instead of scalar fields, because it doesn't fit the
     fields:/JSON-key model the first shape assumes (`system.status.logs`
     is the one example today).
  3. **Non-query** — `is_query: false` and no `fields:` — this row only
     describes a field inside another query's aggregate response
     (`system.status.dashboard-health`-style rows); it is metadata, never
     independently executable, and the Console answers it
     `unavailable reason=not-executable` if anyone tries.

  A `type: status` entry with neither `fields:` nor an explicit `is_query:`
  is an error the drift check catches on purpose — it is the one case the
  checker cannot tell apart from an entry someone forgot to finish.

## `tools/console_inventory/*.yaml`

Four files (`dome.yaml`, `sound.yaml`, `system.yaml`,
`drive-servo-aux-rc.yaml`), one row per registry entry in that domain group,
each citing `anchor_kind` (`api` | `rc_internal` | `event` | `config` |
`aggregate-field` | `none`), `executor_or_core`, and file:line `evidence` for
that citation. They were written by hand during the epic's inventory pass
(#208–#212) as a one-time cross-check that every registry entry really does
reach a real executor and not just an HTTP adapter — they do not regenerate
themselves when you edit the registry, so **adding a registry entry means
adding its inventory row by hand in the same change**, in whichever of the
four files matches its domain.

`make check-action-drift` (`tools/check_action_registry_drift.py`) enforces
this both ways, by two separate checks:

- `check_inventory_registry_alignment()` requires a strict one-to-one
  match: every registry entry needs a same-named inventory row citing the
  identical `executor` value in its `executor_or_core` field, and every
  inventory row needs a matching registry entry. A registry entry with no
  inventory row (`"<name> in registry but missing from inventory"`), an
  inventory row with no registry entry, or a name present in more than one
  of the four files, are all reported as drift — this is not an optional
  cross-check, it is the gate that keeps the inventory from going stale the
  moment you add or rename an entry.
- `check_executor_marker_contradiction()` is narrower: it flags an
  inventory row whose `notes` still say `NO-CORE-BELOW-HANDLER` once the
  registry's own `executor` field names a real one — the two must agree
  about whether an executor exists at all, not just about its name.

> Correction to an earlier internal note: `docs/console-anchor-findings.md`
> describes these four files as passing a `console_inventory_check.py`
> script. No such script exists in this tree at this base — the checks
> described above live inside `tools/check_action_registry_drift.py`
> itself, run via `make check-action-drift`.

## A caveat worth knowing before you trust `executor_ready`

`ConsoleCatalogEntry::executor_ready` (`include/console_catalog.h`) is
generated as `true` for **every** entry — `tools/generate_console_catalog.py`
does not yet check whether the Console module actually has a runtime path
wired for that operation (its own comment says so: `# TODO: check if
executor function is actually defined`). Whether an operation is genuinely
dispatchable today is decided at run time, in `src/console/console_module.cpp`
— its status-executor table for `type: status`, its `ACTION_REGISTRY[]`
lookup plus guard for `type: action`, and (at this base) an unconditional
"not ready" for every `type: config` entry regardless of what the catalog
says. Do not read `executor_ready: true` in the catalog, or in a `help`
reply's `executor_ready` field, as proof that running the operation will
actually do something — confirm against `console_module.cpp`'s own dispatch
tables, or just try it and read the `outcome`/`reason` it answers with.
