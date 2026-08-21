# protoArtoo Claude Agents

Project-scoped Claude Code agents for the protoArtoo ESP32 body-controller
firmware. These are specialist roles, not generic coding personas.

## Agent Inventory

| Agent | Owns | Must not own |
|---|---|---|
| `backend-coder` | Bounded ESP32/Arduino firmware implementation, web API handlers, FreeRTOS task wiring, `RobotState`/queue paths, config/NVS, action-registry plumbing, OTA/upload backend support, LittleFS backend integration, SBUS/RC, dome/audio backend control | UI/UX design, independent review, heap/crash/performance diagnosis |
| `frontend-designer` | Operator-facing web UI, dashboard layout, visual design, copy, accessibility, controls/selectors, desktop/tablet interaction, Playwright validation | Firmware safety logic, backend ownership decisions, generic SaaS/admin styling |
| `performance-optimizer` | Heap/stack/OOM/PANIC/coredump/profiler work, failed allocations, fragmentation, sluggish HTTP, OTA failure diagnosis, SSE pressure, CHIRP catalog memory, Learned-sequence buffers, runtime memory evidence | General feature implementation, UI design, final independent review |
| `code-reviewer` | Fresh review after changes, safety/security/architecture/data-flow/maintainability/stale-comment/regression findings | Implementing fixes, self-approval, speculative broad refactors |

## Routing Rules

- Route firmware/backend implementation to `backend-coder`.
- Route anything visible to operators to `frontend-designer`, including copy,
  dashboard structure, controls, icons, responsive behavior, and browser tests.
- Route crash, heap, profiler, coredump, failed-allocation, fragmentation, or
  runtime performance work to `performance-optimizer`.
- Route completed work needing independent judgment to `code-reviewer`.
- Use Explore/read-only discovery before implementation when ownership,
  architecture, or source-of-truth context is unclear.

When an agent routes away, it should provide a handoff packet:
- trigger for handoff
- relevant files/functions/endpoints/docs
- current evidence and uncertainty
- requested decision/output
- boundaries: what not to touch

## Shared Standards

- Agents should be project-specific: ESP32, Arduino framework, FreeRTOS,
  PlatformIO, LittleFS, OTA, seated-controller constraints, and astromech
  operator workflows matter.
- Prefer durable, behavior-preserving fixes over hacks, symptom patches, broad
  retries, hidden state, or silent fallbacks.
- Automated tests are evidence, not the goal. Use risk-based verification and
  avoid low-value tests that mostly encode implementation details.
- Preserve safety invariants and make future debugging easier.
- Do not add routine session details to MemPalace. Save only significant
  findings, decisions, or constraints.

## Updating Agents

- Keep frontmatter `description` fields trigger-oriented: say when to delegate,
  not just what the agent is.
- Keep roles narrow. If an agent starts doing another agent's job, add a routing
  boundary instead of broadening the role.
- Preserve hook and MCP configuration unless the change is explicitly about
  runtime behavior.
- After changing agent policy, scan `AGENTS.md`, `.claude/verification-playbook.md`,
  and hooks for conflicting instructions.
