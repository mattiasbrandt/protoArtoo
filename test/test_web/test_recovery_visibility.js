// =============================================================================
// test/test_web/test_recovery_visibility.js
//
// Recovery overlay visibility (issue #117): the overlay must not be hidden
// behind the page it is reporting on, the page behind it must be dimmed
// exactly once rather than once per nesting level, and competing overlays must
// be suppressed outright.
//
// Rewritten for #359. The first version read "the overlay must sit above every
// other overlay" and compared its z-index against every z-index in the
// stylesheet, the Operator Shell's chrome included - so it required the Page
// Recovery View to cover the Latching Estop, and the suite enforced the hole
// the reopened #359 was about. ADR 0048 had already decided the other way:
// this view renders INSIDE the content region, and the chips and the estop
// stay live behind a surface that failed to load. #117's own concern survives
// unchanged, and is the first section below: nothing the failed surface
// renders may come out on top of the panel reporting on it.
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

// The Operator Shell's frame: the regions that are NOT a surface, and that
// carry the Latching Estop between them. Nothing about them is ranked against
// the recovery view -- they are outside the work area's stacking context -- so
// they are excluded from the comparison rather than compared and excused.
const CHROME_SELECTORS = new Set([
  "#shell-top",
  "#shell-status",
  "#shell-content",
  ".shell-estop",
  ".status-plate-region",
]);

const declaredZIndexes = () =>
  pageRules
    .filter((rule) => rule.declarations.has("z-index"))
    .flatMap((rule) =>
      rule.selectors.map((selector) => ({
        selector,
        zIndex: Number.parseInt(rule.declarations.get("z-index"), 10),
      }))
    )
    .filter(({ zIndex }) => Number.isFinite(zIndex));

// -----------------------------------------------------------------------------
// Stacking order
// -----------------------------------------------------------------------------

test("The recovery overlay outranks everything a surface can render", (t) => {
  const recoveryZIndex = zIndexOf(kernelRules, "#page-recovery-backdrop");

  // Any rule in the page stylesheet that creates a stacking context is a
  // candidate to cover the overlay, so compare against all of them rather than
  // a hand-listed few -- except the chrome, which is not inside the work area
  // and is covered by the rule below instead.
  const competitors = declaredZIndexes().filter(
    ({ selector }) => !CHROME_SELECTORS.has(selector)
  );

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

test("Nothing about recovery reaches the chrome that carries the estop", (t) => {
  // The defect the reopened #359 was about: a rule reading
  // `body.recovery-active > *:not(#page-recovery-backdrop)` matched #shell-top
  // and #shell-status, so the press and the Tab were eaten before stacking
  // order mattered - on every first surface load, not only on a fault.
  const reaching = recoveryScopedSelectors().filter(({ selector }) => {
    const afterBody = selector.slice("body.recovery-active".length).trim();
    const bodyChildren = afterBody.startsWith(">");
    const namesChrome = [...CHROME_SELECTORS].some(
      (chrome) => chrome !== "#shell-content" && selector.includes(chrome)
    );
    return bodyChildren || namesChrome;
  });

  assert.deepEqual(
    reaching.map(({ selector }) => selector),
    [],
    "a recovery rule that matches a body child dims and disables the Latching Estop"
  );
});

// -----------------------------------------------------------------------------
// Suppressing competing overlays
// -----------------------------------------------------------------------------

