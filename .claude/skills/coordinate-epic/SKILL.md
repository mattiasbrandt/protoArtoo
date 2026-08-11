---
name: coordinate-epic
description: Run a multi-worker remediation epic as planner-orchestrator + critic - create the epic and sub-issues, dispatch workers in isolated worktrees, review as critic, integrate serially, coordinate device verification. Use when the user asks to coordinate an epic, slice findings into parallel sub-issues, or dispatch worker agents on a ticket pool.
---

You are the coordinator: assignment, integration, and ruthless review. You do
not implement tickets yourself - workers do. Arguments name the epic(s) or the
findings to slice; read the named epics in full first (their orchestration and
critic sections bind you), then AGENTS.md.

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
3. Read the full diff (`git diff <base>...HEAD`): scope creep,
   shortcuts, behaviour change in tickets that promise none, comment
   degradation, core guardrails (no heap alloc or blocking on Core 1 paths,
   RobotState via portMUX/zone discipline).
4. Verify the tree, not the narrative: `git log`, clean `git status`, new
   symbols present on disk.
5. Reject by naming concrete defects (file:line, what is wrong, what the bar
   is) on the sub-issue. Do not fix it yourself. Do not lower the bar for
   velocity.
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
- First rejection: back to the SAME worker with the defect list.
- Second rejection with the same failure signature: stop resending prose.
  The bottleneck is usually the worker's ability to judge its own
  verification, not implementation skill. Either supply a ready-made harness
  yourself and require its output pasted verbatim in the report, escalate the
  model or effort for the test-design step, or take the design decision back
  and re-slice. A third variation of a rejected category is never assigned.

## Integration (serialized - you alone)

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
