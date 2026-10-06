# Console operation readiness is reported at execution, not at discovery

The generated Console catalog carried an `executor_ready` flag on every entry,
emitted to operators twice: as a field of `help <operation>` and as an
`executor-not-ready` marker in the `operations` listing. It was never true
information. `tools/generate_console_catalog.py` set it with
`executor_ready = True  # All registry entries have executors defined`, beside
its own `# TODO: check if executor function is actually defined`, so all ~192
rows claimed readiness — including the ~44 that dispatch refuses with
`unavailable reason=executor-not-ready`. Discovery said ready, execution said
not ready, and the operator sees discovery. The `operations` marker was
unreachable code for the life of the field.

The two flags beside it are real: `available_on_board` and `available_in_build`
are compile-time expressions (a board capability macro, or the entry's own
`build_flag`), which is why the generator's comment claims compile-time
evaluation for those two and not for this one.

**We decided readiness is not a discovery-time property.** `executor_ready` is
removed from the generated catalog, from `help`'s field list and from the
`operations` annotation. An operation's availability is still advertised, by the
two flags that are true; whether an executor is wired is answered by running it,
which #224 already made truthful.

## Considered options

- **Teach the generator to derive readiness** by parsing the C++ dispatch tables
  — `g_statusExecutors[]` and `g_scalarConfigExecutors[]` in
  `src/console/console_module.cpp`, the six `include/console_direct_action_*.h`
  tables, and `ACTION_REGISTRY[]`. Rejected: it creates a second source of truth
  about readiness, which is the shape that produced this defect, and it breaks
  silently whenever a table moves. Both happened inside one epic — #257 split
  `g_directActionExecutors[]` out of `console_module.cpp`, and #259 added a sixth
  domain header. A generator coupled to those locations would have been wrong
  twice in a week, and wrong in the same invisible way.
- **Leave the field and document that it lies.** Rejected: it keeps a false
  statement in a shipped operator surface, and leaves #206's acceptance row
  "known-but-unavailable operations show a stable reason" false as an advertised
  property.

## Consequences

- An operator cannot ask "is this wired up?" without running the operation. That
  is the intended trade: the answer is then always correct.
- `operations` keeps annotating `not-on-this-board` and `not-in-this-build`,
  which are genuine, so the listing still distinguishes the two availability
  facts that are knowable without executing.
- `CONSOLE_REASON_EXECUTOR_NOT_READY` stays in the reason vocabulary. It is an
  execution-time answer and always was; only the discovery-time claim is gone.
- An audit must not read readiness from `operations` output. The executor-not-ready
  count is measured from the native suite
  (`pio test -e native -f test_native/test_console_module -v | grep '#220 report'`).

## Amendment (2026-10-06, under #321, built in #474): a row that is never on the Console says so up front

Readiness stays out of discovery. A different fact joins it there: some
operations are **never** on the Console - a file transfer, a step that belongs
to the Sequences editor, an act that only orders a browser page, the browser
Console Adapter itself. Fifteen such rows answered `executor-not-ready`, the
same word as the three operations that are simply not wired yet, and the only
record of which was which was a name list kept twice, in
`console_module.cpp`'s dispatch comment and in a native test.

**We decided the registry declares it.** A row that is never on the Console
carries one reason from a fixed list (`file-transfer`, `editor-only`,
`browser-only`, `console-itself`) and the page where the builder does it
instead. The generator emits both into the Operation Catalog row. The Console
answers such a row with the Availability Reason `not-on-console`, in the
**change it elsewhere** family: `operations` lists it the way it lists
`not-on-this-board`, running it answers with it, and `help` names the page.

This does not reopen the decision above. `executor_ready` was removed because
it was a claim the generator could not know and got wrong. A declared
exclusion is written by a person on the registry row and read from there, the
same kind of fact as `not-on-this-board`; a row that is merely unwired still
answers `executor-not-ready`, at execution only.

**Considered:** reusing `not-executable` (rejected - it means a row that only
describes a field of another query, and the operation here does exist on the
droid); a free-text reason (rejected - the drift check cannot hold it, and
grouping is what `help` shows); marking the unwired rows too (rejected - they
are work, not scope, and would hide it).

**Consequences:**

- The Operation Catalog is the one record of Console scope. The test that pinned
  eighteen names asserts an invariant over the catalog instead, and the three
  unwired rows stay named in that test until they are wired.
- The pinned catalog counts give way to counts derived from the catalog, and a
  byte-compare of the regenerated catalog joins `make check-action-drift`, as
  `check-parts-drift` does for the parts catalog.
- ADR 0036's `tools/console_inventory/*.yaml` keeps one row per registry entry,
  holding anchor kind, evidence and notes only; the copied `registry_*` fields
  and the executor-name mirror leave it, because the registry already holds
  them.
