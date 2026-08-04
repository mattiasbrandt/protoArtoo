const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const root = path.resolve(__dirname, "../..");
const loaderSource = fs.readFileSync(path.join(root, "data/page_loader.js"), "utf8");

const runStylesheetHarness = () => {
  const activeStylesheets = new Set();
  const stylesheets = [];
  const appendedScripts = [];
  const events = [];
  const timers = [];
  const diagnostics = [];
  let maxActiveStylesheets = 0;

  const document = {
    currentScript: { dataset: { scripts: "/status_stream.js" } },
    readyState: "complete",
    createElement(tagName) {
      const element = { tagName };
      element.remove = () => activeStylesheets.delete(element);
      return element;
    },
    querySelectorAll() {
      return [];
    },
    head: {
      appendChild(stylesheet) {
        assert.equal(stylesheet.href, "/style.css");
        activeStylesheets.add(stylesheet);
        stylesheets.push(stylesheet);
        maxActiveStylesheets = Math.max(maxActiveStylesheets, activeStylesheets.size);
      },
    },
    body: {
      appendChild(script) {
        appendedScripts.push(script.src);
        script.onload();
      },
    },
  };

  const window = {
    addEventListener() {},
    dispatchEvent(event) {
      events.push(event.type);
    },
    setTimeout(callback, delay) {
      timers.push({ callback, delay });
    },
  };

  vm.runInNewContext(loaderSource, {
    console: {
      warn() {
        diagnostics.push("warn");
      },
      error() {
        diagnostics.push("error");
      },
    },
    document,
    Event: class Event {
      constructor(type) {
        this.type = type;
      }
    },
    window,
  });

  return {
    activeStylesheets,
    stylesheets,
    appendedScripts,
    events,
    timers,
    diagnostics,
    window,
    get maxActiveStylesheets() {
      return maxActiveStylesheets;
    },
  };
};

test("page loader fetches dependencies strictly in order", () => {
  const appended = [];
  const events = [];
  const deferredImage = { dataset: { deferredSrc: "/image.jpg" } };
  const document = {
    currentScript: { dataset: { scripts: "/a.js,/b.js,/c.js" } },
    readyState: "complete",
    createElement(tagName) {
      return { tagName };
    },
    querySelectorAll() {
      return [deferredImage];
    },
    head: {
      appendChild(element) {
        assert.equal(element.href, "/style.css");
        element.onload();
      },
    },
    body: {
      appendChild(script) {
        appended.push(script.src);
        script.onload();
      },
    },
  };

  vm.runInNewContext(loaderSource, {
    console,
    document,
    Event: class Event {
      constructor(type) {
        this.type = type;
      }
    },
    window: {
      addEventListener() {},
      dispatchEvent(event) {
        events.push(event.type);
      },
    },
  });

  assert.deepEqual(appended, ["/a.js", "/b.js", "/c.js"]);
  assert.equal(deferredImage.src, "/image.jpg");
  assert.deepEqual(events, ["pa:assets-ready"]);
});

test("stylesheet failures retry without duplicates and cannot abandon scripts", () => {
  const harness = runStylesheetHarness();

  assert.equal(harness.stylesheets.length, 1);
  harness.stylesheets[0].onerror();
  assert.equal(harness.activeStylesheets.size, 0);
  assert.deepEqual(harness.timers.map(({ delay }) => delay), [400]);
  assert.deepEqual(harness.appendedScripts, []);

  harness.timers.shift().callback();
  assert.equal(harness.stylesheets.length, 2);
  harness.stylesheets[1].onerror();
  assert.equal(harness.activeStylesheets.size, 0);
  assert.deepEqual(harness.timers.map(({ delay }) => delay), [800]);
  assert.deepEqual(harness.appendedScripts, []);

  harness.timers.shift().callback();
  assert.equal(harness.stylesheets.length, 3);
  harness.stylesheets[2].onerror();

  assert.equal(harness.activeStylesheets.size, 0);
  assert.equal(harness.maxActiveStylesheets, 1);
  assert.deepEqual(harness.timers, []);
  assert.deepEqual(harness.appendedScripts, ["/status_stream.js"]);
  assert.deepEqual(harness.events, ["pa:assets-ready"]);
  assert.equal(harness.window.PAAssetsReady, true);
  assert.deepEqual(harness.diagnostics, ["warn", "warn", "error"]);
});

test("transient stylesheet failure retries then starts scripts exactly once", () => {
  const harness = runStylesheetHarness();

  assert.equal(harness.stylesheets.length, 1);
  harness.stylesheets[0].onerror();

  assert.equal(harness.activeStylesheets.size, 0);
  assert.deepEqual(harness.timers.map(({ delay }) => delay), [400]);
  assert.deepEqual(harness.appendedScripts, []);
  assert.deepEqual(harness.events, []);

  harness.timers.shift().callback();
  assert.equal(harness.stylesheets.length, 2);
  assert.equal(harness.activeStylesheets.size, 1);
  assert.equal(harness.maxActiveStylesheets, 1);

  harness.stylesheets[1].onload();

  assert.deepEqual(harness.timers, []);
  assert.equal(harness.stylesheets.length, 2);
  assert.equal(harness.activeStylesheets.size, 1);
  assert.equal(harness.maxActiveStylesheets, 1);
  assert.deepEqual(harness.appendedScripts, ["/status_stream.js"]);
  assert.deepEqual(harness.events, ["pa:assets-ready"]);
  assert.equal(harness.window.PAAssetsReady, true);
  assert.deepEqual(harness.diagnostics, ["warn"]);
});

test("operator pages avoid parallel direct script requests", () => {
  const htmlFiles = fs
    .readdirSync(path.join(root, "data"))
    .filter((name) => name.endsWith(".html"));

  for (const name of htmlFiles) {
    const html = fs.readFileSync(path.join(root, "data", name), "utf8");
    const scriptSources = [...html.matchAll(/<script\s+src="([^"]+)"/g)].map((match) => match[1]);
    if (scriptSources.length === 0) continue;

    if (name === "setup.html") {
      assert.deepEqual(scriptSources, [], name);
      assert.doesNotMatch(html, /<link\s+rel="stylesheet"/, name);
      assert.doesNotMatch(html, /<link\s+rel="icon"/, name);
    } else {
      assert.deepEqual(scriptSources, ["/page_loader.js"], name);
    }

    const dataScripts = html.match(/data-scripts="([^"]+)"/)?.[1].split(",") || [];
    assert.ok(dataScripts.length > 0, `${name} has page dependencies`);
    for (const source of dataScripts) {
      assert.ok(fs.existsSync(path.join(root, "data", source.slice(1))), `${name}: ${source}`);
    }
  }
});
