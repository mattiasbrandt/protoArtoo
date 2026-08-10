// =============================================================================
// test/test_web/test_pautils_validation.js
//
// PAUtils escaping contract, executed from the shipped data/web_api.js.
//
// Every case below calls window.PAUtils.escapeHtml / escapeAttr as the module
// installed them. Nothing here reimplements the function: an earlier version of
// this file did, and its local copy had already drifted from what ships
// (`if (!value) return ""` versus `String(value ?? "")`, which disagree on 0
// and false), so it could not have caught a regression in either direction.
// Issue #146.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const webApiSrc = readFileSync(join(__dirname, "../../data/web_api.js"), "utf-8");

// Runs the shipped module in a fresh context and hands back what it published
// on window.PAUtils.
const loadPAUtils = () => {
  const windowMock = {
    addEventListener: () => {},
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    location: { origin: "http://device" },
  };
  const context = {
    window: windowMock,
    document: { addEventListener: () => {} },
    console: { warn: () => {}, log: () => {}, error: () => {} },
    AbortController,
    Date,
    URLSearchParams,
    JSON,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    fetch: () => Promise.reject(new Error("no network in this test")),
  };
  context.globalThis = context;
  vm.runInNewContext(webApiSrc, context, { filename: "web_api.js" });
  return windowMock.PAUtils;
};

test("PAUtils publishes the escaping helpers pages rely on", (t) => {
  const PAUtils = loadPAUtils();

  assert.equal(typeof PAUtils?.escapeHtml, "function", "escapeHtml must be exported");
  assert.equal(typeof PAUtils?.escapeAttr, "function", "escapeAttr must be exported");
  assert.equal(typeof PAUtils?.showFeedback, "function", "showFeedback must be exported");
  assert.equal(typeof PAUtils?.debounce, "function", "debounce must be exported");
});

test("escapeHtml neutralises the characters that end a text node", (t) => {
  const { escapeHtml } = loadPAUtils();

  assert.equal(escapeHtml("<div>test</div>"), "&lt;div&gt;test&lt;/div&gt;");
  assert.equal(escapeHtml("a&b"), "a&amp;b");
  assert.equal(escapeHtml('say "hi"'), "say &quot;hi&quot;");
});

test("escapeHtml escapes the ampersand first, so escapes are not double-escaped wrong", (t) => {
  const { escapeHtml } = loadPAUtils();

  // If < were replaced before &, the &lt; it produces would then have its own
  // ampersand escaped, yielding &amp;lt; and rendering as literal "&lt;".
  assert.equal(escapeHtml("<"), "&lt;");
  assert.equal(escapeHtml("&lt;"), "&amp;lt;", "pre-escaped input must round-trip as visible text");
});

test("escapeHtml renders falsy values as themselves, not as empty", (t) => {
  const { escapeHtml } = loadPAUtils();

  // Status values pass through here. A 0 that renders as "" reads as a missing
  // field on the operator's page rather than a real zero.
  assert.equal(escapeHtml(0), "0");
  assert.equal(escapeHtml(false), "false");
  assert.equal(escapeHtml(""), "");
  assert.equal(escapeHtml(null), "", "null has no value to show");
  assert.equal(escapeHtml(undefined), "", "undefined has no value to show");
});

test("escapeHtml leaves ordinary text untouched", (t) => {
  const { escapeHtml } = loadPAUtils();

  assert.equal(escapeHtml("hello"), "hello");
  assert.equal(escapeHtml("dome link: connected"), "dome link: connected");
  assert.equal(escapeHtml(42), "42");
});

test("escapeHtml defuses an injected script and event handler", (t) => {
  const { escapeHtml } = loadPAUtils();

  const escaped = escapeHtml('<img src="x" onerror="alert(\'XSS\')">');

  assert.equal(escaped, "&lt;img src=&quot;x&quot; onerror=&quot;alert('XSS')&quot;&gt;");
  assert.ok(!escaped.includes("<"), "no raw < survives, so no tag can open");
  assert.ok(!escaped.includes(">"), "no raw > survives, so no tag can close");
});

test("escapeAttr closes the quoted-attribute escape hatch", (t) => {
  const { escapeAttr } = loadPAUtils();

  // The double quote is what matters here: it is what would end the attribute
  // and let the rest of the value become new attributes.
  const escaped = escapeAttr('" onmouseover="alert(1)');

  assert.ok(!escaped.includes('"'), "no raw double quote may survive into an attribute");
  assert.equal(escaped, "&quot; onmouseover=&quot;alert(1)");
});

test("escaping covers the double-quote form of attribute, not the single-quote form", (t) => {
  const { escapeAttr } = loadPAUtils();

  // Recording the actual boundary of the shipped contract rather than an
  // assumed one: the single quote passes through, so every attribute built
  // from an escaped value must be written with double quotes. Markup that
  // single-quotes an interpolated attribute is not covered by this helper.
  assert.equal(escapeAttr("it's"), "it's");
});

test("escapeAttr and escapeHtml agree on the same input", (t) => {
  const { escapeHtml, escapeAttr } = loadPAUtils();

  // They are separate entry points so call sites read correctly, but a value
  // that is safe in one context must not be unsafe in the other.
  for (const value of ["<b>", 'a "quoted" &', null, 0, "plain"]) {
    assert.equal(escapeAttr(value), escapeHtml(value), `escaping disagreed on ${JSON.stringify(value)}`);
  }
});
