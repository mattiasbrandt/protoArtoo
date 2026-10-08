// =============================================================================
// test/test_web/test_destinations_resolve.js
//
// EVERY DESTINATION A SURFACE NAMES HAS TO RESOLVE.
//
// A "no" on this droid is supposed to name the builder's next move and take
// them to it, so a link pointing at an address the Operator Shell cannot open
// is worse than the bare refusal it replaced: the builder clicks, lands
// nowhere, and the screen has lied to them.
//
// This is a defect this repo has shipped. Three disabled-state cards routed to
// /setup.html after #404 renamed that page, and they only kept working because
// the shell happens to carry a rename alias for it. The next rename has no
// such luck, and nothing on any surface would have failed.
//
// Only the harness can see this: each surface knows its own links and the
// shell knows the address space, and no single page ever holds both. The
// assertion is against the shipped SURFACES table in data/shell.js, read here
// rather than restated, so adding a surface or an alias needs no edit to this
// file.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync, readdirSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");

// The shipped table, evaluated rather than parsed: a row is a JS object
// literal and a regex over it would be its own second reader to keep in step.
const surfaces = () => {
  const src = readFileSync(join(dataDir, "shell.js"), "utf-8");
  const start = src.indexOf("const SURFACES = [");
  assert.notEqual(start, -1, "data/shell.js must declare SURFACES");
  const end = src.indexOf("\n  ];", start);
  assert.notEqual(end, -1, "SURFACES must end with a top-level ];");
  const literal = src.slice(start + "const SURFACES = ".length, end + "\n  ]".length);
  return vm.runInNewContext(`(${literal})`);
};

// Every address the shell can open: a surface's own route, its aliases, its
// document, and the legacy document each alias used to be served from - which
// is how data/shell.js itself builds surfaceForPath.
const addressSpace = () => {
  const addresses = new Set(["/", "/index.html", "#"]);
  surfaces().forEach((surface) => {
    addresses.add(`#${surface.page}`);
    addresses.add(surface.doc);
    (surface.aliases || []).forEach((alias) => {
      addresses.add(`#${alias}`);
      addresses.add(`/${alias}.html`);
    });
  });
  return addresses;
};

const dataFiles = (extension) =>
  readdirSync(dataDir)
    .filter((name) => name.endsWith(extension) && !name.startsWith("_"))
    .map((name) => ({ name, source: readFileSync(join(dataDir, name), "utf-8") }));

// A destination this test judges: an in-product address, written as a hash
// route or as a document path. Anything else a page links to - an external
// URL, a mailto, a fragment into the page itself - is not the shell's to open
// and is left alone.
//
// `#i-...` is excluded because it is not an address at all: it is an <svg><use>
// into the project's own sprite, whose ids all carry that prefix
// (data/shell.js ICON_PATHS, docs/icon-set-provenance.md).
const SPRITE_REFERENCE = /^#i-/;
const IN_PRODUCT = /^(#[a-z0-9-]+|\/[a-z0-9_-]*\.html)$/;

const destinationsIn = (source, pattern) => {
  const found = [];
  for (const match of source.matchAll(pattern)) {
    const value = match[1];
    if (SPRITE_REFERENCE.test(value)) continue;
    if (IN_PRODUCT.test(value)) found.push(value);
  }
  return found;
};

test("every in-product destination a surface links to resolves in the shell", () => {
  const addresses = addressSpace();
  const unresolved = [];

  dataFiles(".html").forEach(({ name, source }) => {
    destinationsIn(source, /href="([^"]+)"/g).forEach((href) => {
      if (!addresses.has(href)) unresolved.push(`${name}: href="${href}"`);
    });
  });

  // A route a module carries as data, which is the shape every "no" that names
  // a next move uses: {href, label} in data/feature_availability.js,
  // data/apply_timing.js, data/component_picker.js and data/web_api.js.
  dataFiles(".js").forEach(({ name, source }) => {
    destinationsIn(source, /href:\s*"([^"]+)"/g).forEach((href) => {
      if (!addresses.has(href)) unresolved.push(`${name}: href: "${href}"`);
    });
  });

  assert.deepEqual(
    unresolved,
    [],
    `a surface names a destination the Operator Shell cannot open:\n  ${unresolved.join("\n  ")}`,
  );
});

test("a route carried as data names a destination and a label, never one alone", () => {
  const offences = [];
  dataFiles(".js").forEach(({ name, source }) => {
    // The two halves are written together on purpose: a href with no label is
    // a link with nothing to click, and a label with no href is the sentence
    // that names a destination the builder then has to go and find.
    for (const match of source.matchAll(/\{\s*href:\s*"([^"]*)"\s*,\s*label:\s*"([^"]*)"\s*\}/g)) {
      if (!match[1] || !match[2]) offences.push(`${name}: {href:"${match[1]}", label:"${match[2]}"}`);
    }
  });
  assert.deepEqual(offences, [], offences.join("\n"));
});
