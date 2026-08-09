# Worker brief template

Compose the worker prompt from this template. Fill only {ISSUE} and
{WORKTREE}; everything ticket-specific stays in the ticket.

---

You are implementing sub-issue #{ISSUE} in the worktree {WORKTREE}.

READ FIRST, IN FULL: issue #{ISSUE} - body, acceptance criteria, and the
pinned coordinator comment (attempt log, rejected approaches, verification
harness). Rejected approaches are out of scope: do not attempt a variation of
a rejected category. Then read AGENTS.md.

BOUNDARIES
- Operate ONLY inside {WORKTREE}. Never edit, checkout, stash, restore, or
  clean anything outside it. Out-of-tree touches are an automatic reject.
- Quality fixes only: no suppressions, no test-only ifdefs in production
  code, no copy-paste where the ticket demands consolidation, no deleted or
  degraded comments, no scope creep beyond the sub-issue. If the clean fix
  reveals a deeper issue, report it on the issue - do not expand scope.

SLICE WORKFLOW (AGENTS.md, binding)
- implement -> verify -> commit immediately (explicit per-file git add,
  type(scope): summary, no co-author trailers) -> record
  "Slice N - <short SHA> <subject> - verified <how>" in ONE evolving status
  comment, updated in place as follows.
- Status comment mechanics (all agents share one GitHub identity, so
  --edit-last can overwrite the coordinator's comments - never use it):
  1. Before your first slice, create your status comment with a marker first
     line: gh issue comment {ISSUE} --body "<!-- worker-status-{ISSUE} -->..."
  2. Find its id once: gh api repos/{owner}/{repo}/issues/{ISSUE}/comments
     --jq '.[] | select(.body | startswith("<!-- worker-status-{ISSUE} -->")) | .id'
  3. Update that id thereafter: gh api -X PATCH
     repos/{owner}/{repo}/issues/comments/<id> -f body="..."
- The issue BODY belongs to the coordinator: never edit it and never tick
  acceptance checkboxes - the critic ticks them on verified acceptance.
- Never leave a green slice uncommitted.

VERIFICATION (software-verified cap)
- pio run build + pio test -e native + the ticket's acceptance checks.
- NEVER flash, never run make ota, never run pio test concurrently with any
  OTA anywhere.
- Tests you add or change must be PROVEN ABLE TO FAIL before you report
  green: for bug fixes, run them against the pre-fix commit and show red;
  then mutate the production code you fixed and show red. Web tests follow
  test/test_web/README.md exactly. Paste the calibration and mutation output
  in your status comment - a green run alone will be rejected.
- If the ticket's pinned comment provides a verification harness, run it and
  paste its output verbatim; do not substitute your own summary of it.

REPORT
Final status comment: slices with SHAs, verification evidence (including the
red runs above), AGENTS.md verification label, and anything you could not
prove with the reason.
