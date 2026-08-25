# MemPalace memory protocol

Long-term project memory for `protoArtoo` lives in MemPalace (MCP server plus a
user-level daemon). **Availability is declared in `AGENTS.md` "Memory"** — when
that section says unavailable, none of this runs. When it is re-enabled, agents
with MCP access follow this protocol.

## Session start

1. Call `mempalace_status` once at the beginning of every session.
   - This loads the memory protocol and AAAK spec into context.
   - It also reveals the palace structure (wings, rooms) for this project.
   - Do not skip this step — the memory protocol is self-taught from the response.

2. If the user's opening message references past decisions, prior conversations,
   or asks "why did we..." / "what was the reason for..." style questions:
   - Call `mempalace_search` with a targeted query before answering.
   - Prefer wing-scoped searches (`--wing protoArtoo` or equivalent wing name
     as revealed by `mempalace_status`) over unscoped global searches.

## During work

- **Search before speculating.** If a design decision, prior constraint, or
  architectural rationale is referenced but not in the current context, search
  before guessing: `mempalace_search "<topic>" --wing protoArtoo`.
- **Search before duplicating.** Before proposing a new approach that might
  conflict with past decisions, check for prior art:
  `mempalace_search "<approach>" --wing protoArtoo`.
- **Do not search for things already in context.** If the relevant file has been
  read or the fact was stated in this session, use the session context — do not
  re-query MemPalace for it.

## Saving memories

- Use `mempalace_add_drawer` to persist significant findings, decisions, or
  constraints discovered during a session.
- Save at natural checkpoints: after resolving a non-obvious bug, after a design
  decision that has cross-task implications, or when the user explicitly confirms
  a conclusion worth keeping.
- Do NOT save routine implementation steps, intermediate errors, or content that
  is already captured verbatim.
- Filing format: use the wing for this project (from `mempalace_status`) and the
  most relevant room (hall) — `hall_facts` for locked decisions, `hall_discoveries`
  for breakthroughs, `hall_events` for notable sessions.

## Knowledge graph

- Use `mempalace_kg_query` when the question is about relationships between
  entities (e.g. which task introduced a constraint, which component owns a pin).
- Use `mempalace_kg_add` to record a new fact when a constraint is confirmed
  (e.g. "UART1 is owned by DriveTask post-T01").
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
- After significant domain work, write a concise AAAK diary entry:
  `mempalace_diary_write("<agent_name>", "<aaak_entry>")`.
- Diary entries are compressed in AAAK — keep them structured and entity-coded
  per the AAAK spec from `mempalace_status`.

