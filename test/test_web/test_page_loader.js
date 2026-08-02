const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const root = path.resolve(__dirname, "../..");
const loaderSource = fs.readFileSync(path.join(root, "data/page_loader.js"), "utf8");

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
