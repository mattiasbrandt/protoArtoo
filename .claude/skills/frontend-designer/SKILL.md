---
name: frontend-designer
description: Design and refine operator-friendly UI for protoR2 with clear visual hierarchy and non-developer UX language.
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
- NEVER an emoji on an operator surface. ADR 0066 retired it and `tools/check_surface_anatomy.py` fails the build on one. Where a glyph earns its place it is an icon from the project's own SVG sprite, inheriting the text color, with its label still beside it.
- Prefer modern segmented/chip/radio-card option selectors over classic dropdowns when choices are small and known. Sized by how many there are:
  - up to about five: a segmented bar (`segmented()`, `data/output_settings.js`);
  - more than that: small pills that WRAP onto two or three lines, never a wider bar and never a menu;
  - a color: SWATCHES showing the color itself, named once beneath the picked one. A color named in a menu is a word doing a swatch's job.
  - A token with no color of its own (`DEFAULT`) gets a neutral dot. Never invent one for it.

Control scale - the mistake this project makes most:
- `.btn` and `.field` are PAGE- and FORM-scale house classes in `data/style.css`. `python3 tools/css_where.py .btn` prints the banner and the line. Dropped on a card they are always too big, and this has been caught by the operator on three separate reviews. The same lookup finds `.btn-sm`, `.btn-quiet`, `.link-btn`, `.field`, `.opmode-grid`, `.sleep-switch`, `.mood-grid`, `.readout`.
- On a card: `.btn.btn-sm`, `.btn.btn-quiet` for a secondary act, `.btn.link-btn` for navigation to another surface. A page-level primary `.btn` belongs to the page, not to a card.
- `.field input` is `width: 100%`. A numeric input holding one to three digits is constrained to its content, not stretched to the card.
- Before you hand a surface over, walk EVERY control on it - inputs, selects, buttons, labels, sliders - and check each sits at card scale. Do not make the operator find them one at a time.
- **The whole page is yours, not only what you added** (operator, 2026-10-01: *"the workers need to align the page allover that they are working on. only focusing on the new stuff is simply bad work"*). Walk every section of every page you touch, old controls included, and bring it into the same family. His rejection of #438's Sequences editor was of fields the ticket did not add (Name, Interrupt group) sitting beside ones it did.

Control style - no "classic" square buttons (the operator's standing direction, 2026-09-28):
- His words, from three reviews in one evening: *"the classic square buttons style looks way early 2000s web page ... have some modern sleek style or choice of toggles"*; *"the page design is way too cluttered too many simply ugly square boxes allover"*; and a restyle he rejected because it only *"rounded off some squares"*. Rounding corners, recoloring borders or re-spacing the same boxes is NOT a redesign and reads to him as no change.
- A choice is drawn as what it IS, never as a row of bordered buttons:
  - two states (Driving / Stationary): a **sliding pill switch** - a track in `--well`, a raised thumb that moves to the chosen side (`.opmode-grid`, `data/style.css`, #399 slice 6);
  - an on/off: a **toggle switch**, a track and a knob (`.sleep-switch`);
  - an ordered level (Quiet .. Awake+): a **level control** whose steps light up to the chosen one (`.mood-grid` / `.mood-btn`), never four separate buttons;
  - a small set of peers: the segmented bar above, joined, no gaps and no per-option boxes.
- **Acts by weight.** One prominent act per row or card at most. Secondary acts (calibrate, pulses off, settings) are quiet text actions (`.btn-quiet`, `.link-btn`), not boxed buttons; related acts on one thing (drive / open / close / stop) are ONE joined control (Servos, #399 slice 4).
- **Readings are tiles, not bordered boxes of text:** a small quiet label, the state as the loudest thing with its Status Color dot, one quiet detail line; soft surfaces separated by space or seams, no 1px frame around every cell (`.readout`, #399 slice 6). Grey for "not heard / not measured" (#402) is correct, not a defect.
- **Fewer frames.** A page is sections divided by seams; a card inside a card, or a box around every row, is the clutter he rejected on Servos. Put rarely used settings behind a per-row disclosure instead of showing them all at rest.
- **Before you hand a surface over, compare it yourself:** render it at 1440 px before and after, side by side. If a stranger could mistake one for the other, it is not ready. If you restyle a shared primitive (`.btn`, `.seg`, `.status-item`), list every surface it reaches and check each.

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
- Stage the image the controller would serve: `python3 tools/stage_fsdata.py --set default --out DIR --serve PORT` (`legacy` is the artoo-esp32 set). Fixture scripts that need the droid's routes use `make pw-fixture DIR=<folder>`, which is `tools/serve_editor_fixture.py` plus the env in `test/playwright/README.md`. Do not use `python3 -m http.server`: it does not expand `PA:INCLUDE` and it does not gzip.
- Clearly report what was locally verified and which hardware checks remain pending.

When delivering a redesign:
- Explain user impact in non-technical terms first.
- Provide a small, testable change set before broad visual refactors.
- When possible, request Playwright validation evidence for critical interactions.
