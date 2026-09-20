---
name: frontend-designer
description: Design and refine operator-friendly UI for protoArtoo with clear visual hierarchy and non-developer UX language.
---

Design for droid operators, not developers.

Design priorities:
1. Make control intent obvious at a glance.
2. Use direct labels and status text with no internal jargon.
3. Reduce cognitive load: grouped controls, clear defaults, visible outcomes.
4. Prefer resilient desktop PC layouts; treat tablet/mobile as out of scope unless explicitly requested.
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
- NEVER an emoji on an operator surface. ADR 0066 retired it and `tools/check_surface_anatomy.py` fails the build on one. Where a glyph earns its place it is an icon from the project's own SVG sprite, inheriting the text colour, with its label still beside it.
- Prefer modern segmented/chip/radio-card option selectors over classic dropdowns when choices are small and known. Sized by how many there are:
  - up to about five: a segmented bar (`segmented()`, `data/output_settings.js`);
  - more than that: small pills that WRAP onto two or three lines, never a wider bar and never a menu;
  - a colour: SWATCHES showing the colour itself, named once beneath the picked one. A colour named in a menu is a word doing a swatch's job.
  - A token with no colour of its own (`DEFAULT`) gets a neutral dot. Never invent one for it.

Control scale - the mistake this project makes most:
- `.btn` and `.field` are PAGE- and FORM-scale house classes. Dropped on a card they are always too big, and this has been caught by the operator on three separate reviews.
- On a card: `.btn.btn-sm`, `.btn.btn-quiet` for a secondary act, `.btn.link-btn` for navigation to another surface. A page-level primary `.btn` belongs to the page, not to a card.
- `.field input` is `width: 100%`. A numeric input holding one to three digits is constrained to its content, not stretched to the card.
- Before you hand a surface over, walk EVERY control on it - inputs, selects, buttons, labels, sliders - and check each sits at card scale. Do not make the operator find them one at a time.

Copy length - the rule is in `docs/ui-copy-voice.md`, and these are the three that get broken:
- A **subtitle** is a count, a state or a provenance - or a 2-4 word label. Not a sentence.
- **A third sentence is two notes, or it is too long.**
- **No sentence under a section heading explaining what the section is for.** A heading takes a count or a state; the builder can see what the section is. This is the single most common rejection on this project.
- Carry these into the work rather than citing the file. A brief that links the doc does not produce short copy; three iterations of #410 proved it.

How a surface is reviewed here:
- The operator reviews it LIVE, on the staged image, at desktop width. Never a screenshot, never a phone or tablet width.
- Stop after the first working iteration and hand it over. Do not polish, test or gate before the design and the look are approved.
- Their words come back verbatim onto the ticket. Apply the list, then re-read every remaining string and every remaining control against the rules above - the list is a sample, not the whole defect.

Playwright test upkeep:
- For non-trivial UI changes, update existing Playwright scripts under test/playwright/<page>/.
- Add new scripts only for new flows/states not covered by current scripts.
- Keep each script focused on one operator workflow or audit concern.
- Validate primarily at desktop viewport size (for example 1440x900).
- Skip tablet/mobile viewport validation unless explicitly requested by the user.
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
