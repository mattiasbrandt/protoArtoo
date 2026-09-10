// Behavioural tests for the stylesheet's token layer and its Availability
// Family treatments (#341).
//
// There is no DOM here, so these tests read data/style.css the way a browser
// does: comments stripped, rules walked with their at-rule context, and every
// declaration resolved through :root before it is asserted on. That is the
// difference between "the file contains the string --warning" and "this rule
// paints amber", and only the second one is worth a test. The computed-style
// half of the enforcement - a rendered element's edge equals a probe painted
// with the token - needs a browser and lives in test/playwright/setup/.
const test = require("node:test");
const assert = require("node:assert/strict");
const { readFileSync } = require("node:fs");

const CSS = readFileSync("data/style.css", "utf8");

// Everything outside a string or a url() that a comment could hide.
const stripComments = (css) => css.replace(/\/\*[\s\S]*?\*\//g, "");

// Walk the stylesheet into flat rules. A rule carries the selector it was
// written with and the at-rule prelude it sits inside, so a @media override can
// be told apart from the base rule it overrides.
const parseRules = (css) => {
  const rules = [];
  const stack = [];
  let buf = "";
  for (let i = 0; i < css.length; i += 1) {
    const ch = css[i];
    if (ch === "{") {
      stack.push(buf.trim());
      buf = "";
    } else if (ch === "}") {
      const prelude = stack.pop();
      if (prelude !== undefined && !prelude.startsWith("@")) {
        rules.push({
          selector: prelude.replace(/\s+/g, " "),
          context: stack.filter((s) => s.startsWith("@")),
          declarations: buf
            .split(";")
            .map((d) => d.trim())
            .filter(Boolean)
            .map((d) => {
              const at = d.indexOf(":");
              return { property: d.slice(0, at).trim(), value: d.slice(at + 1).trim() };
            })
            .filter((d) => d.property && d.value),
        });
      }
      buf = "";
    } else {
      buf += ch;
    }
  }
  return rules;
};

const RULES = parseRules(stripComments(CSS));
const ROOT = RULES.find((r) => r.selector === ":root");
const TOKENS = new Map(ROOT.declarations.map((d) => [d.property, d.value]));
const NON_ROOT = RULES.filter((r) => r !== ROOT);

// Substitute var(--x) until nothing is left to substitute, so a rule that
// points at a token through another token is still resolved to a colour.
const resolve = (value, depth = 0) => {
  if (depth > 10) return value;
  const next = value.replace(/var\(\s*(--[\w-]+)\s*(?:,([^()]*(?:\([^()]*\)[^()]*)*))?\)/g, (whole, name, fallback) =>
    TOKENS.has(name) ? TOKENS.get(name) : (fallback !== undefined ? fallback : whole),
  );
  return next === value ? next : resolve(next, depth + 1);
};

const COLOUR_LITERAL =
  /#[0-9a-fA-F]{3,8}\b|\brgba?\([^)]*\)|\bhsla?\([^)]*\)|(?<![-\w])(?:white|black|red|green|blue|yellow|orange|gold|silver|gray|grey)(?![-\w])/;

const declarationsOf = (predicate) =>
  NON_ROOT.flatMap((rule) => rule.declarations.map((d) => ({ ...d, rule }))).filter(predicate);

test("the palette declares a scale for spacing, type, radius and elevation", () => {
  const families = {
    spacing: /^--space-/,
    type: /^--fs-/,
    radius: /^--radius-/,
    elevation: /^--elev-/,
  };
  for (const [name, pattern] of Object.entries(families)) {
    const members = [...TOKENS.keys()].filter((token) => pattern.test(token));
    assert.ok(members.length >= 4, `:root should carry a ${name} scale, found ${members.length} step(s)`);
  }
});

test("every var() site resolves to a declared token or to a value the page sets", () => {
  // --pct, --mini-pct, --bar-left and --bar-width are written onto elements by
  // data/rc.js as inline styles, so they are declared at the element rather
  // than in :root and each carries a fallback for the frame before rc.js runs.
  const pageSet = new Set(["--pct", "--mini-pct", "--bar-left", "--bar-width"]);
  const unresolved = [];
  for (const rule of NON_ROOT) {
    for (const { property, value } of rule.declarations) {
      for (const [, name, fallback] of value.matchAll(/var\(\s*(--[\w-]+)\s*(?:,([^()]*))?\)/g)) {
        if (TOKENS.has(name)) continue;
        if (pageSet.has(name) && fallback !== undefined) continue;
        unresolved.push(`${rule.selector} { ${property}: ${value} }`);
      }
    }
  }
  assert.deepEqual(unresolved, [], "a var() naming no declared token renders its fallback, silently");
});

module.exports = { RULES, ROOT, TOKENS, NON_ROOT, resolve, declarationsOf, stripComments, parseRules };
