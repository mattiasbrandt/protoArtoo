# MemPalace memory protocol

Long-term project memory for `protoArtoo` lives in MemPalace (MCP server plus a
user-level daemon), in one wing: **`wing_protoartoo`**. The convention on this
machine is `wing_<project>`; the bare `protoartoo` is not a wing, and
`protoArtoo` case-sensitively matches nothing and answers "No results found"
rather than failing - so a wing-scoped search against it reads as *no prior art*
when it means *no such wing*. The older `protoartoo` and `wing_wt_*` wings were
merged into `wing_protoartoo` on 2026-09-17. Agents with MCP access follow this
protocol.

**Two things stop the protocol, and both are answered the same way - say so once
and carry on.** If `mempalace_status` errors, skip every step below for that
session. If a *write* is refused - `add_drawer`, `update_drawer`,
`diary_write` or `kg_add` returning JSON-RPC `-32001` *"Peer MCP writer active;
this server is read-only for mutating tools"* - that is the daemon holding the
palace's single writer lease, which is palace-wide and therefore binds every
worktree. It is expected, not a fault of yours: reads and the logstream tools
still work, and hook auto-save still works because it routes through the
daemon's queue. Do not retry, do not shell out to the CLI, do not work around
it. Put what must survive on the sub-issue, in `CONTEXT.md` or in `docs/adr/`.

## Session start

1. Call `mempalace_status` once at the beginning of every session.
   - This loads the memory protocol and AAAK spec into context.
   - It also reveals the palace structure (wings, rooms) for this project.
   - Do not skip this step — the memory protocol is self-taught from the response.

2. If the user's opening message references past decisions, prior conversations,
   or asks "why did we..." / "what was the reason for..." style questions:
   - Call `mempalace_search` with a targeted query before answering.
   - Prefer wing-scoped searches (`--wing wing_protoartoo`, the wing for this
     repository and every worktree of it (AGENTS.md "Memory (MemPalace)")) over unscoped global searches.

## During work

- **Search before speculating.** If a design decision, prior constraint, or
  architectural rationale is referenced but not in the current context, search
  before guessing: `mempalace_search "<topic>" --wing wing_protoartoo`.
- **Search before duplicating.** Before proposing a new approach that might
  conflict with past decisions, check for prior art:
  `mempalace_search "<approach>" --wing wing_protoartoo`.
- **Do not search for things already in context.** If the relevant file has been
  read or the fact was stated in this session, use the session context — do not
  re-query MemPalace for it.

## Saving memories

> [!IMPORTANT]
> **Expect this to be refused.** While the daemon runs, every mutating tool
> returns `-32001` (see above). The advice below is what to save *when a write
> succeeds* - it is not a step to retry until it does, and a refusal is not a
> reason to keep the finding out of the issue, `CONTEXT.md` or `docs/adr/`.

- Use `mempalace_add_drawer` to persist significant findings, decisions, or
  constraints discovered during a session.
- Save at natural checkpoints: after resolving a non-obvious bug, after a design
  decision that has cross-task implications, or when the user explicitly confirms
  a conclusion worth keeping.
- Do NOT save routine implementation steps, intermediate errors, or content that
  is already captured verbatim.
- Filing format: use `wing_protoartoo` (AGENTS.md "Memory (MemPalace)") and the
  most relevant room (hall) — `hall_facts` for locked decisions, `hall_discoveries`
  for breakthroughs, `hall_events` for notable sessions.

## Knowledge graph

- Use `mempalace_kg_query` when the question is about relationships between
  entities (e.g. which task introduced a constraint, which component owns a pin).
- When a write succeeds (see "Saving memories"), `mempalace_kg_add` records a
  confirmed constraint (e.g. "UART1 is owned by DriveTask").
- Use `mempalace_kg_timeline` to reconstruct the history of a component or
  decision when debugging a regression.

## Specialist agents

MemPalace supports specialist agents — each with its own wing and diary in the
palace. Agent definitions live in `~/.mempalace/agents/`; do not embed agent
role or focus definitions in `AGENTS.md` or `CLAUDE.md` — the palace is the
agent memory layer.

- Discover specialist agents from the wings and diaries `mempalace_status`
  reveals (the current server exposes no agent-listing tool).
- If a relevant agent exists for the domain being worked on (e.g. a reviewer,
  architect, or ops agent), read its recent diary before starting:
  `mempalace_diary_read("<agent_name>", last_n=10)`.
- After significant domain work, when a write succeeds (see "Saving memories"),
  write a concise AAAK diary entry: `mempalace_diary_write("<agent_name>", "<aaak_entry>")`.
- Diary entries are compressed in AAAK — keep them structured and entity-coded
  per the AAAK spec from `mempalace_status`.

