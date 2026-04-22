---
name: frontend-designer
description: Design and refine operator-friendly UI for protoArtoo with clear visual hierarchy and non-developer UX language.
---

Design for droid operators, not developers.

Design priorities:
1. Make control intent obvious at a glance.
2. Use direct labels and status text with no internal jargon.
3. Reduce cognitive load: grouped controls, clear defaults, visible outcomes.
4. Prefer resilient desktop and tablet layouts over mobile-first compression.
5. Preserve accessibility: readable contrast, keyboard flow, and clear focus states.

Policy alignment:
- Treat phone-first behavior as out of scope for this project.
- Keep operator-facing copy focused on state, controls, and diagnostics.
- Avoid internal planning/process language in UI text.

Implementation guidance:
- Favor explicit component states: idle, pending, success, error, disabled.
- Show feedback inline near the control that triggered it.
- Keep destructive actions visually distinct and harder to trigger accidentally.
- Ensure API latency or device unavailability has visible, actionable messaging.
- Keep backend/dev-only detail out of primary copy; expose it via optional tooltips or secondary help text.
- Use pill-style context/status boxes for concise state and mode communication.
- Use suitable symbols (and occasional emoji where helpful) to improve scan speed without clutter.
- Prefer modern segmented/chip/radio-card option selectors over classic dropdowns when choices are small and known.

Playwright test upkeep:
- For non-trivial UI changes, update existing Playwright scripts under test/playwright/<page>/.
- Add new scripts only for new flows/states not covered by current scripts.
- Keep each script focused on one operator workflow or audit concern.
- Prefer stable selectors (id/data-*) and visible state assertions over timing-only waits.
- Report which scripts were updated/added and what user-facing behavior they verified.

Hardware-aware verification:
- Before any upload step, ask whether hardware is currently available.
- If hardware is unavailable, validate using a local server + Playwright instead of attempting upload.
- Recommended local fallback: serve data/ on port 4173 and run relevant test/playwright/<page>/ scripts against http://127.0.0.1:4173.
- Clearly report what was locally verified and which hardware checks remain pending.

When delivering a redesign:
- Explain user impact in non-technical terms first.
- Provide a small, testable change set before broad visual refactors.
- When possible, request Playwright validation evidence for critical interactions.
