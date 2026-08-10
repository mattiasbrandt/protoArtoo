// =============================================================================
// test/test_web/test_issue_117_recovery_visibility.js
//
// Recovery overlay visibility (issue #117): the overlay must sit above every
// other overlay, the page behind it must be dimmed exactly once rather than
// once per nesting level, and competing overlays must be suppressed outright.
//
// These are CSS invariants. There is no JavaScript to execute and no layout
// engine here, so the tests parse the shipped stylesheet into rules and assert
// on the parsed selectors and declarations - which value wins, which
// combinator is used - rather than matching substrings of the file. An earlier
// version asserted `kernelContent.includes(">") && kernelContent.includes(
// "opacity")`, which any stylesheet in the repo would satisfy, and computed
// Math.pow on its own local numbers to "prove" compounding. Issue #146.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const kernelSource = readFileSync(join(dataDir, "_recovery_kernel.html"), "utf-8");
const styleSource = readFileSync(join(dataDir, "style.css"), "utf-8");

// -----------------------------------------------------------------------------
// A small CSS reader: enough to turn a stylesheet into { selectors,
// declarations } records. At-rule bodies (@media, @keyframes) are skipped
// rather than half-parsed, so a nested block cannot be mistaken for a rule.
// -----------------------------------------------------------------------------
const parseRules = (css) => {
  const withoutComments = css.replace(/\/\*[\s\S]*?\*\//g, "");
  const rules = [];
  let index = 0;

  while (index < withoutComments.length) {
    const braceOpen = withoutComments.indexOf("{", index);
    if (braceOpen === -1) break;
    const prelude = withoutComments.slice(index, braceOpen).trim();

    if (prelude.startsWith("@")) {
      // Step into the at-rule body and keep reading rules from inside it.
      index = braceOpen + 1;
      continue;
    }

    const braceClose = withoutComments.indexOf("}", braceOpen);
    if (braceClose === -1) break;
    const body = withoutComments.slice(braceOpen + 1, braceClose);

    const declarations = new Map();
    body
      .split(";")
      .map((part) => part.trim())
      .filter(Boolean)
      .forEach((part) => {
        const colon = part.indexOf(":");
        if (colon === -1) return;
        declarations.set(part.slice(0, colon).trim(), part.slice(colon + 1).trim());
      });

    rules.push({
      selectors: prelude.split(",").map((s) => s.trim()).filter(Boolean),
      declarations,
    });
    index = braceClose + 1;
  }
  return rules;
};

const styleBlocks = (html) =>
  [...html.matchAll(/<style[^>]*>([\s\S]*?)<\/style>/g)].map((m) => m[1]).join("\n");

const kernelRules = parseRules(styleBlocks(kernelSource));
const pageRules = parseRules(styleSource);

// Last declaration wins in CSS, so read the winning value rather than the first.
const declaredValue = (rules, selector, property) => {
  let value = null;
  for (const rule of rules) {
    if (!rule.selectors.includes(selector)) continue;
    if (rule.declarations.has(property)) value = rule.declarations.get(property);
  }
  return value;
};

const zIndexOf = (rules, selector) => {
  const raw = declaredValue(rules, selector, "z-index");
  return raw === null ? null : Number.parseInt(raw, 10);
};

// Every selector, from any rule, that is scoped under the recovery-active body.
const recoveryScopedSelectors = () =>
  kernelRules.flatMap((rule) =>
    rule.selectors
      .filter((selector) => selector.startsWith("body.recovery-active"))
      .map((selector) => ({ selector, declarations: rule.declarations }))
  );

// -----------------------------------------------------------------------------
// Stacking order
// -----------------------------------------------------------------------------

test("The recovery overlay declares a stacking order at all", (t) => {
  const recoveryZIndex = zIndexOf(kernelRules, "#page-recovery-backdrop");

  assert.notEqual(recoveryZIndex, null, "the kernel must give the overlay a z-index");
  assert.ok(Number.isFinite(recoveryZIndex), `z-index must be a number, got ${recoveryZIndex}`);
});

test("The recovery overlay outranks every other overlay on the page", (t) => {
  const recoveryZIndex = zIndexOf(kernelRules, "#page-recovery-backdrop");

  // Any rule in the page stylesheet that creates a stacking context is a
  // candidate to cover the overlay, so compare against all of them rather than
  // a hand-listed few.
  const competitors = pageRules
    .filter((rule) => rule.declarations.has("z-index"))
    .flatMap((rule) =>
      rule.selectors.map((selector) => ({
        selector,
        zIndex: Number.parseInt(rule.declarations.get("z-index"), 10),
      }))
    )
    .filter(({ zIndex }) => Number.isFinite(zIndex));

  assert.ok(competitors.length > 0, "the page stylesheet must declare some stacking order to compare against");

  for (const { selector, zIndex } of competitors) {
    assert.ok(
      recoveryZIndex > zIndex,
      `recovery overlay (${recoveryZIndex}) must outrank ${selector} (${zIndex})`
    );
  }
});

// -----------------------------------------------------------------------------
// Dimming the page behind the overlay
// -----------------------------------------------------------------------------

test("The page behind the overlay is dimmed by exactly one rule", (t) => {
  const dimming = recoveryScopedSelectors().filter(({ declarations }) => declarations.has("opacity"));

  assert.equal(
    dimming.length,
    1,
    `dimming must come from a single rule, found ${dimming.length}: ${dimming.map((d) => d.selector).join(" | ")}`
  );
});

test("The dim rule targets direct children, so opacity cannot compound", (t) => {
  const [dim] = recoveryScopedSelectors().filter(({ declarations }) => declarations.has("opacity"));

  // A descendant combinator would apply the same opacity again at every
  // nesting level - 0.4 four levels deep renders at 0.0256, effectively
  // invisible, which is the bug this issue fixed.
  const afterBody = dim.selector.slice("body.recovery-active".length).trim();
  assert.ok(
    afterBody.startsWith(">"),
    `the dim rule must use a child combinator, got "${dim.selector}"`
  );
  assert.ok(
    !/\s/.test(afterBody.slice(1).trim()),
    `the dim rule must not descend past the first level, got "${dim.selector}"`
  );
});

test("The dim rule exempts the overlay itself", (t) => {
  const [dim] = recoveryScopedSelectors().filter(({ declarations }) => declarations.has("opacity"));

  assert.ok(
    dim.selector.includes(":not(#page-recovery-backdrop)"),
    `dimming the overlay along with the page would make the recovery panel unreadable, got "${dim.selector}"`
  );
});

test("The dim leaves the page behind readable", (t) => {
  const [dim] = recoveryScopedSelectors().filter(({ declarations }) => declarations.has("opacity"));
  const opacity = Number.parseFloat(dim.declarations.get("opacity"));

  assert.ok(opacity > 0, "fully transparent would read as a blank page, not a dimmed one");
  assert.ok(opacity <= 0.6, `the dim must be visible as a dim, got ${opacity}`);
});

// -----------------------------------------------------------------------------
// Suppressing competing overlays
// -----------------------------------------------------------------------------

test("Competing overlays are suppressed while recovery is up", (t) => {
  const suppressed = recoveryScopedSelectors().filter(
    ({ declarations }) =>
      declarations.get("visibility") === "hidden" || declarations.get("display") === "none"
  );

  const covered = suppressed.map(({ selector }) => selector).join(" ");
  for (const overlay of [".sleep-overlay", ".seq-modal"]) {
    assert.ok(
      covered.includes(overlay),
      `${overlay} must be suppressed while the recovery overlay is up, covered: "${covered}"`
    );
  }
});

test("The overlay is hidden until it is made active", (t) => {
  assert.equal(
    declaredValue(kernelRules, "#page-recovery-backdrop", "display"),
    "none",
    "the kernel-created backdrop must not be visible before recovery needs it"
  );
  assert.equal(
    declaredValue(kernelRules, "#page-recovery-backdrop.active", "display"),
    "flex",
    "adding the active class is what shows the overlay"
  );
});
