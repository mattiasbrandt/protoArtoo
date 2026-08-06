// Structural invariants every served controller page must hold.
//
// These are markup facts, not behavior, so they are checked by reading data/
// rather than by running a page: what a page declares in its <head> decides
// whether it can recover at all, and that is settled before any script runs.
//
// Replaces test_page_loader.js. The loader it covered is gone -- pages now
// carry the inline recovery kernel and let it fetch /page_bootstrap.js with
// retry (ADR 0019) -- but the invariant that page had, that no page opens
// parallel direct script requests, is kept below and tightened.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const root = path.resolve(__dirname, "../..");
const dataDir = path.join(root, "data");

const RECOVERY_KERNEL = "_recovery_kernel.html";
const INCLUDE_RE = /<!--\s*PA:INCLUDE\s+([A-Za-z0-9_.\-/]+)\s*-->/g;

// Partials are inlined into the pages that carry the directive and are never
// imaged themselves, so they are sources rather than served pages.
const servedPages = fs
  .readdirSync(dataDir)
  .filter((name) => name.endsWith(".html") && !name.startsWith("_"))
  .sort();

const read = (name) => fs.readFileSync(path.join(dataDir, name), "utf8");

// Expand exactly as tools/gzip_fsdata.py does at build time: single pass, no
// recursion. What the controller serves is this, not the page source.
const expand = (html) =>
  html.replace(INCLUDE_RE, (_match, target) => read(target));

// A comment that closes early leaves its tail rendering as visible body text
// and its surplus "-->" as literal content. Scanning for a close delimiter
// reached outside a comment is that failure's exact signature.
const orphanCommentClose = (html) => {
  let cursor = 0;
  while (cursor < html.length) {
    const open = html.indexOf("<!--", cursor);
    const close = html.indexOf("-->", cursor);
    if (close === -1) return -1;
    if (open === -1 || close < open) return close;
    cursor = close + 3;
  }
  return -1;
};

test("every served page exists to be checked", () => {
  assert.ok(servedPages.length > 0, "no served pages found in data/");
});

for (const name of servedPages) {
  test(`${name} inlines the Page Recovery View kernel`, () => {
    const html = read(name);
    const includes = [...html.matchAll(INCLUDE_RE)].map((match) => match[1]);
    assert.ok(
      includes.includes(RECOVERY_KERNEL),
      `${name} must carry <!-- PA:INCLUDE ${RECOVERY_KERNEL} --> in its <head>`,
    );
    for (const target of includes) {
      assert.ok(fs.existsSync(path.join(dataDir, target)), `${name} includes missing ${target}`);
    }
  });

  test(`${name} opens no parallel direct script requests`, () => {
    const html = read(name);
    const scriptSources = [...html.matchAll(/<script\s+src="([^"]+)"/g)].map((match) => match[1]);
    // The kernel fetches /page_bootstrap.js itself, with retry, and loads the
    // chain one at a time. Any <script src> here would rejoin the opening
    // burst the kernel exists to keep the page out of.
    assert.deepEqual(scriptSources, [], `${name} must declare no <script src>`);
  });

  test(`${name} declares an existing script chain on <html>`, () => {
    const html = read(name);
    const openingTag = html.match(/<html\b[^>]*>/)?.[0] || "";
    const declared = openingTag.match(/data-scripts="([^"]*)"/)?.[1] || "";
    const sources = declared.split(",").map((source) => source.trim()).filter(Boolean);
    assert.ok(sources.length > 0, `${name} must declare data-scripts on <html>`);
    for (const source of sources) {
      assert.ok(fs.existsSync(path.join(dataDir, source.slice(1))), `${name}: ${source}`);
    }
  });

  test(`${name} links its stylesheet natively`, () => {
    // Render-blocking and fetched once by the browser. Injecting it from JS
    // instead put a stylesheet failure in front of the script chain, where it
    // could stall everything behind it.
    assert.match(read(name), /<link\s+rel="stylesheet"\s+href="\/style\.css">/, name);
  });

  test(`${name} serves no orphaned comment text`, () => {
    const at = orphanCommentClose(expand(read(name)));
    assert.equal(at, -1, `${name}: comment close at byte ${at} is outside any comment`);
  });
}
