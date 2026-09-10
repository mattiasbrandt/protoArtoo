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

// The four Availability Families, in the order CONTEXT.md lists them, each
// with the bare class a later surface wears and the shipped state classes that
// must resolve to the same treatment.
const FAMILIES = {
  "change it here": {
    family: ".availability-change-here",
    states: [".feature-availability-row.feature-state-off", ".card.disabled-card"],
  },
  "change it elsewhere": {
    family: ".availability-change-elsewhere",
    states: [
      ".feature-availability-row.feature-state-not-in-this-build",
      ".feature-availability-row.feature-state-not-on-this-board",
    ],
    dimmed: true,
  },
  "still finding out": {
    family: ".availability-finding-out",
    states: [
      ".feature-availability-row.feature-state-checking",
      ".feature-availability-row.feature-state-identity-unavailable",
    ],
  },
  "settled no": { family: ".availability-settled-no", states: [], dimmed: true },
};

// Every rule whose selector list carries this exact compound selector.
const rulesFor = (selector) =>
  NON_ROOT.filter((rule) => rule.selector.split(",").some((s) => s.trim() === selector));

// The declarations a selector ends up with, later rules winning, values
// resolved through :root. Enough of a cascade for selectors that all sit at
// the same specificity in one block, which is how the family rules are written.
const treatmentOf = (selector) => {
  const out = new Map();
  for (const rule of rulesFor(selector)) {
    for (const { property, value } of rule.declarations) out.set(property, resolve(value));
  }
  return out;
};

const AVAILABILITY_RULES = NON_ROOT.filter((rule) =>
  /(availability-|feature-state|feature-availability)/.test(rule.selector),
);

test("no Availability Family spends a reserved colour", () => {
  // CONTEXT.md "Status Colour": amber is "you can do something about this, and
  // should", red is "something is stopped or refused". A way of saying no is
  // neither, and before #341 four of these states shared one amber hatch.
  const amber = TOKENS.get("--warning");
  const red = TOKENS.get("--danger");
  const spends = [];
  for (const rule of AVAILABILITY_RULES) {
    for (const { property, value } of rule.declarations) {
      const resolved = resolve(value);
      if (resolved.includes(amber) || resolved.includes(red)) {
        spends.push(`${rule.selector} { ${property}: ${value} }`);
      }
    }
  }
  assert.deepEqual(spends, [], "a way of saying no must not wear amber or red");
});

test("the four Availability Families have four treatments and no two share one", () => {
  const treatments = Object.entries(FAMILIES).map(([name, { family }]) => {
    const declared = treatmentOf(family);
    assert.ok(declared.size > 0, `${name} declares no treatment at all`);
    return [name, JSON.stringify([...declared.entries()].sort())];
  });
  for (let i = 0; i < treatments.length; i += 1) {
    for (let j = i + 1; j < treatments.length; j += 1) {
      assert.notEqual(
        treatments[i][1],
        treatments[j][1],
        `${treatments[i][0]} and ${treatments[j][0]} are dressed identically`,
      );
    }
  }
});

test("each shipped availability state is dressed by its own family, not by another", () => {
  for (const [name, { family, states }] of Object.entries(FAMILIES)) {
    for (const state of states) {
      const shared = rulesFor(state).filter((rule) =>
        rule.selector.split(",").some((s) => s.trim() === family),
      );
      assert.ok(
        shared.length > 0,
        `${state} should be dressed by the ${name} rule, so the two cannot drift apart`,
      );
    }
  }
});

test("a dimmed Availability Family lifts on hover and on focus-within", () => {
  // The reference lifts .optgrid.na back to full on hover and focus-within so
  // a dimmed thing can still be inspected. A row nobody can read is not a
  // treatment, it is a deletion.
  for (const [name, { family, dimmed }] of Object.entries(FAMILIES)) {
    if (!dimmed) continue;
    const base = Number(treatmentOf(family).get("opacity"));
    assert.ok(base < 1, `${name} should be dimmed, found opacity ${base}`);
    for (const pseudo of [":hover", ":focus-within"]) {
      const lifted = Number(treatmentOf(`${family}${pseudo}`).get("opacity"));
      assert.ok(
        lifted > base,
        `${name} should lift on ${pseudo}: ${lifted} is not brighter than ${base}`,
      );
    }
  }
});

test("neither reserved colour is spent on a choice or on an answer that has not arrived", () => {
  // CONTEXT.md "Status Colour" avoid-list: amber for "not normal", amber on a
  // transient unknown. Each of these was one of those before #341.
  const shouldNotWearAmber = [
    [".indicator", "a dashboard health lamp before the first status frame"],
    [".mood-btn.quiet.active", "a Mood the operator chose"],
    [".mood-btn.mid.active", "a Mood the operator chose"],
    [".mood-btn.full.active", "a Mood the operator chose"],
    [".mood-btn.awakeplus.active", "a Mood the operator chose"],
    [".opmode-btn.drive.active", "a Commanded Mode the operator chose"],
    [".opmode-btn.stationary.active", "a Commanded Mode the operator chose"],
    ['body[data-page="wifi"] #wifi-posture-card[data-posture="standalone-ap"]', "a valid operator-selected posture"],
  ];
  const amber = TOKENS.get("--warning");
  const red = TOKENS.get("--danger");
  const spends = [];
  for (const [selector, why] of shouldNotWearAmber) {
    for (const [property, value] of treatmentOf(selector)) {
      if (value.includes(amber) || value.includes(red)) {
        spends.push(`${selector} (${why}) { ${property}: ${value} }`);
      }
    }
  }
  assert.deepEqual(spends, [], "a reserved colour on something that is not a call to act");
});

test("no colour literal exists outside :root", () => {
  // The rule that makes "amber means one thing" true rather than aspirational.
  // The reference project shipped two literals - a modal backdrop and a button
  // ground - that only showed up once a second theme put them on a light card.
  const offenders = declarationsOf(({ value }) => COLOUR_LITERAL.test(value)).map(
    ({ rule, property, value }) => `${rule.selector} { ${property}: ${value} }`,
  );
  assert.deepEqual(offenders, [], "a colour literal belongs in :root, not in a rule");
});

test("no token in :root is orphaned", () => {
  const used = new Set([...stripComments(CSS).matchAll(/var\(\s*(--[\w-]+)/g)].map((m) => m[1]));
  const orphans = [...TOKENS.keys()].filter((token) => !used.has(token));
  assert.deepEqual(orphans, [], "a token nothing reads is a decision nobody can find");
});

module.exports = { RULES, ROOT, TOKENS, NON_ROOT, resolve, declarationsOf, stripComments, parseRules };
