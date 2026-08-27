---
name: coordinate-epic
description: Run a multi-worker remediation epic as planner-orchestrator + critic - create the epic and sub-issues, dispatch workers in isolated worktrees, review as critic, integrate serially, coordinate device verification. Use when the user asks to coordinate an epic, slice findings into parallel sub-issues, or dispatch worker agents on a ticket pool.
---

You are the coordinator: assignment, integration, and ruthless review. You do
not implement tickets yourself - workers do. Arguments name the epic(s) or the
findings to slice. Read AGENTS.md first - it outranks every orchestration
section you pasted into an epic - then the named epics in full (their
orchestration and critic sections bind you within that).

## Pipeline position

This skill is the execution stage; it composes with these skills and never
duplicates them:

- `wayfinder` (upstream) - the effort is still foggy, its route undecided.
  Open design decisions in the input mean you are too early: stop and
  recommend the operator run it first. Decision tickets are resolved before
  an execution epic exists.
- `grill-with-docs` (upstream) - the plan is concrete but has not been
  stress-tested against the domain model and ADRs. Recommend it before
  slicing when decisions look settled but undocumented.
- `to-tickets` - does the slicing: tracer-bullet tickets, each with native
  blocking edges, published to the tracker. Use it for the breakdown instead
  of slicing ad hoc; it is user-invoked, so ask the operator to run it (or
  work from its output if it already ran).
- `github-issues` - mechanics for every issue operation: parent/sub-issue
  links, blocked-by edges, develop branches, status comments. Invoke it
  before any issue action.

## Epic creation (when starting from findings or a plan)

On top of the `to-tickets` breakdown, add what only the coordinator knows:
sub-issues **file-disjoint** wherever possible, concurrency exclusions
derived from the actual files each ticket touches (never assign two tickets
that modify the same file concurrently), behaviour-fixing tickets ranked
ahead of behaviour-neutral refactors and docs. Paste the orchestration model
and critic protocol into the epic body so they survive this session.

## Assignment (you set up, then hand the worker its worktree)

`<base>` throughout this skill is the epic's integration branch - read it
from the epic's coordination section (it changes at Phase 5 closure).

- `gh issue develop <n> --base <base> --name <type>/<slug>`, then
  `git worktree add ../wt-<n> <branch>`.
- **Stale-base trap:** `gh issue develop` branches from ORIGIN's ref, which
  can be many commits behind the local integration branch. After creating the
  worktree, `git -C ../wt-<n> rev-parse HEAD` must equal the local
  `<base>` tip; if not, `reset --hard` it there before the worker
  starts. Every worktree, every time.
- **Live hold check.** Before dispatching on a surface in the epic's
  concurrency table, verify the holder's live state - `gh issue develop
  --list <holder>`, its latest comments - and record what you found in the
  pin. A revision trigger that happens to have fired is luck; the record of
  the check is what makes it a check.
- One sub-issue per worker. A worker operates ONLY inside its own worktree;
  any out-of-tree edit, checkout, stash, restore, or clean is an automatic
  reject. This repo has lost work to exactly that.
- Compose the worker prompt from [worker-brief.md](worker-brief.md) plus the
  sub-issue number. Ticket-specific knowledge lives in the ticket, not the
  prompt - the brief's first step sends the worker to the issue body and the
  pinned coordinator comment.
- Fence files mechanically, not just in prose: put the exact gate invocation
  in the ticket's pinned comment — `--fenced <pathspecs>`, the `--mutations`
  expectation for web slices, and any waiver flag you are sanctioning
  (`--expect-no-new-tests`, `--expect-no-mutations`). The gate then rejects a
  fenced-file edit, a flat test total, or missing mutation coverage in the
  worker's own run, before review.

## Analysis slices

When a sub-issue's deliverable is a classification table - one row per
entry, an evidence column - rather than code, read
[analysis-slices.md](analysis-slices.md) before dispatching: the skeleton the
worker is held to, independent anchors across parallel workers, and the
anchor your own critic pass must add. Both mechanisms it guards against have
failed on this repo behind a green gate.

## Critic protocol (before accepting any slice - no exceptions)

Worker summaries are claims, not evidence; this repo has caught agents
reporting passes that never ran. In the worker's worktree, personally:

0. **Read the production diff first, and weight it heaviest.** The source is
   what ships; tests are scaffolding. `git diff <base>...HEAD -- <prod paths>`
   and ask: is the new code actually *wired in and called*, or added beside
   what it was meant to replace? Were the old rules **deleted**, or are there
   now two copies? Were call sites missed? Is the seam readable, and is the
   result genuinely simpler than what it replaced? Does it do what the ticket
   claimed? A gate cannot answer any of this - production code that does not
   do the job passes every test-shaped check. This repo has shipped a
   step-core module nothing referenced, and left a hand-rolled poll in place
   through an epic that claimed to consolidate polling. Both were invisible
   to a green gate.

1. Re-run the slice gate with the worker's exact invocation, including any
   `--fenced` pathspecs and the worker's `--mutations` patches from your
   brief: `python3 tools/slice_verify.py --base <base> [--fenced ...]
   [--mutations <patches>]`. Its block must match the worker's pasted block
   character for character, provenance lines included - both script hashes
   (`gate` and `mut`), HEAD sha, DIRTY marker, merge-base, diff size;
   divergence marks the slice unverified (AGENTS.md "Worker slice gate"). The
   gate runs both suites, the mutation stage, the build, and the diff checks;
   `--json` on both runs makes the comparison diffable. Any waiver ACK in a
   worker's block that you did not sanction - `--expect-gate-edit`,
   `--expect-no-new-tests`, `--expect-no-mutations` - is an automatic
   reject. Then every remaining acceptance check.

   Match the proof to the claim; a proof of the wrong class is a claim:

   | Claim | Proof |
   |---|---|
   | declaration-only / behaviour-identical | `nm --defined-only --size-sort` identical (SHA-256) and byte-equal RAM/Flash; per-object `objdump -dr` identical for a zero-cost claim |
   | a unit is absent from an image | archive/object membership in `.pio/build/<env>/firmware.map`. A budget staying green is a drift alarm, never absence - 134 KB of headroom hides a whole backend |
   | an invariant holds | compile-enforced (`static_assert`, `#error`), never a comment promising it |
2. For new or changed tests, demand the prove-it-can-fail evidence: red
   against the pre-fix commit for bug fixes. Mutation coverage is proven by
   the gate re-run in step 1 - the mutation row passes only when every patch
   is KILLED by assertion and every changed web production JS file is hit by
   at least one patch; a SURVIVED, KILLED-BY-HANG, uncovered file, or any
   mutation table pasted outside a gate block is a reject. Web tests are
   held to `test/test_web/README.md`. A green suite without that
   demonstration is a claim.

   The gate is a mechanical floor, and it is automated - running it costs no
   iterations. **Do not spend rejection cycles on top of it arguing test
   design.** Never reject a slice for test naming, structure, tidiness or
   volume; ask for focused tests, not exhaustive suites. Test quality is
   minor next to source quality - if a rejection is about the tests rather
   than the code, it had better be because the tests prove nothing, not
   because they could be prettier. Iteration spent on test design is
   iteration not spent on the change itself.

   **Stage gate on all of the above.** At the PoC/MVP stage of an epic - the
   platform is unproven, the go/no-go has not returned a verdict, the hardware
   has not run the real firmware - you may grant `--expect-no-new-tests` as the
   epic-wide default in the worker brief, revoking it per-ticket for robustness
   and epic-closing work. Be clear about what that buys: the gate's
   flat-test-total rule only requires ONE native test per `src/`/`include/`
   change, and does not count `test/test_tools/` at all. It is a cheap floor,
   not the source of oversized harnesses. **The acceptance criteria you write
   are.** Two rejection cycles spent on a probe harness for a failure mode that
   cannot occur until a later ticket is the exact waste this clause exists to
   prevent, and that harness was specified by the ticket, not demanded by the
   gate.

   When you author sub-issue bodies, keep acceptance criteria outcome-shaped
   at this stage: "the guards exist and the build is red for every unassigned
   pin", not "fourteen isolated compiler probes each asserting its own
   diagnostic". A test-shaped acceptance criterion is a rejection cycle you
   have pre-committed to. Distinguish the guard (product, ships now) from the
   harness that proves the guard (scaffolding, waits for closing).

   A criterion is posted only when it is satisfiable and speaks the shipped
   surface's language: every clause can hold at once (#186 asked for "an
   inert toggle" while forbidding "generic disabled widgets" - an inert toggle
   *is* one); it presumes no control that some rows lack (compile-only rows
   have no toggle); and every operator-facing noun is one `data/` already
   uses - grep before naming the object. The UI says *firmware* and
   *filesystem*; a second name teaches operators there are two objects.
3. Read the full diff (`git diff <base>...HEAD`): scope creep,
   shortcuts, behaviour change in tickets that promise none, comment
   degradation, core guardrails (no heap alloc or blocking on Core 1 paths,
   RobotState via portMUX/zone discipline).

   For `data/` slices, open the shipped page yourself - the fixture server
   (`tools/serve_editor_fixture.py`), headed, at every width the page
   supports - and read the accessibility tree as a builder would: a control
   the operator can never move, or a label that reads as a different state
   than the model means, is a reject that no diff, gate or written review
   catches (#186 shipped a disabled `role="switch"` for a compile-time fact
   past two approving reviews). Playwright is yours to re-run headed; agents
   here have reported browser passes that never ran.

   **Open it in every state the model allows, not just the loaded one.** Stub
   the page's inputs so each branch actually renders: the resource absent, the
   request failing, the request failing *and then recovering on retry*, the
   value missing, the component disabled. Three consecutive slices on #202 had
   their real defect pass a green gate, and every one lived in a state the
   default happy path never renders - a restore path bound to a deleted key, a
   placeholder that collapsed the page by 244px, and a panel left permanently
   stuck after an identity retry. A worker's own probe will exercise the state
   the worker was thinking about; your job is the other ones.

   Prefer measuring to looking where you can: assert geometry (does the box
   keep its size between states, does the next element stay put) and computed
   style (is that background actually painted), because "it rendered" and "it
   rendered correctly" are different claims and a screenshot flatters both. An
   assertion whose operands are never compared - `(a || b) !== null` - is not a
   check; grep the worker's probe for one before trusting its output.

   When a slice depends on an ordering, test the failure path, not a slow one.
   Delaying a request and having it succeed proves nothing about a request that
   fails, retries, and then succeeds - a distinction that cost this repo a
   merged defect, because a section that is *visibly waiting to retry* already
   counts as settled for everything downstream of it.
4. Verify the tree, not the narrative: `git log`, clean `git status`, new
   symbols present on disk.
5. Reject by naming concrete defects (file:line, what is wrong, what the bar
   is) on the sub-issue. Do not fix it yourself. Do not lower the bar for
   velocity. Cite from the file open in this turn: re-open the exact lines
   before rejecting on a rule or quoting a body or title. Your own earlier
   restatement of a policy is not a source, and a title read an hour ago has
   moved (#186 drew two objections on exactly that; both were withdrawn).
6. Issue-body ownership: only you edit a sub-issue's body. Tick acceptance
   checkboxes when, and only when, you have re-verified the criterion
   yourself; tick the remainder in the same pass as the evidence-bearing
   closing comment.

## Rejection bookkeeping and escalation

- On every rejection, update ONE pinned coordinator comment on the sub-issue:
  attempt log (agent, model/effort, commit, verdict), each rejected approach
  with WHY at category level so the whole category is fenced off, and any
  verification harness you used with its measured output. This comment is
  what makes the next pickup cheap - maintain it as carefully as the code.
- When a body is re-scoped, edit each superseded comment in place - visible
  strike-through plus a pointer to the controlling text. A stale comment
  outlives a re-scope by default; this epic carried a false "compiles for
  P4" claim a day past its own correction that way.
- First rejection: back to the SAME worker with the defect list.
- Second rejection with the same failure signature: stop resending prose.
  The bottleneck is usually the worker's ability to judge its own
  verification, not implementation skill. Either supply a ready-made harness
  yourself and require its output pasted verbatim in the report, escalate the
  model or effort for the test-design step, or take the design decision back
  and re-slice. A third variation of a rejected category is never assigned.

## Integration (serialized - you alone)

Every slice passes the critic protocol **before** it touches `<base>`,
including slices from an agent the operator assigned outside this skill. A
review after the branch has moved is a different governance property even
when the outcome is fine - a good outcome is not a waiver. Agree the order
in the hand-off, on the ticket: a hand-off cites the ticket and the pin,
states the critic order and the live concurrency table, and references
artefacts in `tasks/` or on the ticket - a scratchpad path is session-scoped
and invisible to the other agent.

Merge reviewed branches into `<base>` one at a time, oldest-reviewed
first; later conflicting branches rebase onto the updated base before their
review completes. After the final merge, re-run the merged-tree test suite
and any epic-level acceptance sweeps - line numbers and stragglers move.
Nothing is pushed to origin until the operator explicitly says so.

## Device verification (serialized - coordinator + operator, never workers)

The device is a single shared resource and may be bench-mode (controller
HTTP/SSE/serial/OTA only - no droid-component verification; see
AGENTS.md verification labels). Ask the operator before every device
session. One image at a time; before any acceptance run, confirm
`firmwareVersion` matches the intended commit - a `-dirty` or stale image
invalidates the run. If the fix changed `data/`, the FS image must be
uploaded too, and `fs-version.json` must match.

## Reporting

Keep one evolving status comment per epic with the frontier state (running /
in review / rework / merged). Interrupt the operator only when: a ticket is
rejected twice, the frontier stalls, an integration conflict is non-trivial,
or a device session is needed. Otherwise work autonomously.
