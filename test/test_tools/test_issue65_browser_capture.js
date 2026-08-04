"use strict";

const assert = require("assert");
const path = require("path");
const { performance } = require("perf_hooks");

const {
  captureTerminalScreenshots,
} = require(path.resolve(__dirname, "../../tools/issue65_browser_capture.js"));

async function main() {
  const events = [];
  let stopped = false;
  const page = {
    async evaluate(action) {
      global.window = {
        stop() {
          stopped = true;
          events.push("stop");
        },
      };
      try {
        return action();
      } finally {
        delete global.window;
      }
    },
    async screenshot(options) {
      assert.strictEqual(stopped, true, "unfinished loads must stop before capture");
      events.push(options.fullPage ? "full" : "viewport");
    },
  };
  const artifactErrors = [];

  await captureTerminalScreenshots(
    page,
    { viewport: "/tmp/viewport.png", full: "/tmp/full.png" },
    artifactErrors,
    performance.now() + 3_000,
  );

  assert.deepStrictEqual(events, ["stop", "viewport", "full"]);
  assert.deepStrictEqual(artifactErrors, []);
}

main().catch((error) => {
  process.stderr.write(`${error.stack || error}\n`);
  process.exitCode = 1;
});
