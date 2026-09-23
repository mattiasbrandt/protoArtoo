---
name: wrap-up
description: Close out protoArtoo work by updating status records, preserving MemPalace memory, and reporting verification state. Use when the user says "wrap up", "wrap this up", "wrap-up", "close this out", or asks to finish the session/status handoff; interpret trailing context like "for the night", "it's late", or "pick up tomorrow" as a resumable handoff, and mid-implementation/context-window wording as a cue to suggest the handoff skill.
---

# Wrap Up

Use this skill whenever the user asks to wrap up or close out the current work.
The user should not need to explain the checklist again.

Interpret extra wording after the trigger as close-out intent. For example:

- "wrap up for the night, it's late" means prioritize a tomorrow-ready handoff.
- "wrap up, I need to leave" means state what is safe to stop and what remains open.
- "wrap up and make sure the issue is current" means emphasize the issue ledger.
- "wrap up, context is getting full" during active implementation means complete normal bookkeeping and suggest or invoke the `handoff` skill so the next session has a reusable temporary prompt for volatile in-progress context.

## Checklist

1. Inspect state:
   - Run `git status --short --branch`.
   - Review the relevant diff, recent commits, and verification outputs from the session.
   - Identify the active issue/task if one was used.

2. Update status records:
   - If an active GitHub issue is known, add a concise issue comment with completed work, commit refs, verification evidence, and remaining risk.
   - If no issue is known, update `docs/status.md` or `docs/goal.md` only when the public planning baseline changed.
   - Keep `tasks/**` local-only and do not commit it.
   - Public docs must not mention agent/tool/model wording.
   - Ensure the next session can restart from a durable source of truth: a formal task record such as a GitHub issue, or MemPalace status/search entries.

3. Preserve memory:
   - MemPalace writes are refused while the daemon holds the writer lease (AGENTS.md "Memory (MemPalace)"). Put significant outcomes, decisions, constraints and unresolved risks on the active issue, in `CONTEXT.md` or in `docs/adr/`, and note once that a write was refused.
   - When a write does succeed, file it in `wing_protoartoo`: `hall_events` for session milestones, `hall_discoveries` for findings, `hall_facts` for confirmed decisions.

4. Clean up repo state:
   - Commit completed verified slices as `type(scope): summary` (AGENTS.md "Commit scope format").
   - Leave unrelated user changes untouched.
   - Report any remaining uncommitted, unverified, or blocked work explicitly.

5. Final response:
   - State the verification label: `software-verified`, `controller-upload-verified`, `full-hardware-verified`, `partial`, or `full-hardware-required`.
   - Say which issue/docs/memory records were updated.
   - List remaining next steps or blockers.
   - If the user indicates they cannot continue now, include the first command to run or file to open when resuming.
   - If the user indicates context-window pressure, a fresh-session transfer, or a mid-implementation pause, suggest or use the `handoff` skill and report the temporary handoff path.
   - State the intended resume source: GitHub issue, docs path, MemPalace room/query, or temporary handoff path.

## Minimal Issue Comment Shape

```md
Wrap-up:
- Completed: ...
- Commits: ...
- Verification: ...
- Remaining: ...
- Memory/docs: ...
- Resume with: ...
- Resume source: ...
```
