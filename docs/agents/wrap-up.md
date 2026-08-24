# Wrap-up procedure

When the user says "wrap up", "wrap this up", "wrap-up", or an obvious close-out
variant, treat it as a request to close the active work loop. Interpret trailing
context as instructions for the close-out style. For example, "wrap up for the
night, it's late" means the user cannot continue now, so produce a resumable
handoff with the exact next step for tomorrow. Do not ask the user to restate the
checklist.

If the trailing context indicates the work is paused mid-implementation because of
context-window pressure, compaction risk, or a fresh-session handoff need, suggest
or invoke the community `handoff` skill after the normal wrap-up bookkeeping.
Reserve `handoff` for volatile in-progress context that is not yet captured well
by issues, commits, docs, or MemPalace. The `handoff` skill should create a
temporary prompt/document for the next session and should reference, not
duplicate, durable artifacts such as issues, commits, plans, ADRs, and MemPalace
entries.

Wrap-up means:

1. Inspect current state:
  - `git status --short --branch`
  - relevant recent diff/log context
  - any verification results already produced in the session
2. Update the running record:
  - if an active GitHub issue or task issue is known, add a concise status comment
    with completed work, commit refs, verification evidence, and remaining risk
  - otherwise update `docs/status.md` or `docs/goal.md` only when the public
    planning baseline actually changed
  - keep `tasks/**` as local-only internal context
  - make the next session restartable from a durable source of truth: either a
    formal task record such as a GitHub issue, or MemPalace status/search entries
3. Preserve memory:
  - file significant decisions, outcomes, and unresolved constraints in MemPalace
    under the project wing (`hall_events`, `hall_discoveries`, or `hall_facts`)
  - write the relevant specialist diary entry when a specialist domain was used
4. Leave the repo understandable:
  - commit completed verified slices that are ready to keep
  - do not commit `tasks/**`
  - report uncommitted or unverified work explicitly
5. Final response:
  - include verification status using the approved labels
  - list updated issue/docs/memory targets
  - list remaining next steps or blockers
  - when the user indicates they are stopping for the night/day or must leave,
    include the first command or file to open when resuming
  - when context-window pressure or mid-task interruption is the reason for
    pausing, include the temporary handoff path or explicitly suggest using the
    `handoff` skill
  - state the intended resume source, for example GitHub issue number, docs path,
    MemPalace room/query, or temporary handoff path
