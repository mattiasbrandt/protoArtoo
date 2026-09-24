// =============================================================================
// test/test_web/test_footer_recovery.js
//
// The version footer. It once carried its own status fetch, a retry with
// backoff and two polls of its own (#149), so that it did not sit on
// "Firmware info unavailable" after losing a race with the stream. Since #419
// it reads the Live Reading like every surface: the shell's one read and one
// stream (or one fallback poll) are what keep it current, and it asks the droid
// for nothing itself - which is the recovery #149 wanted, with nothing of the
// footer's own left to recover.
//
// Every test drives the shipped data/footer.js in a vm, with the shipped
// status stream and Live Reading, and asserts on what it rendered and what it
// asked for (test/test_web/README.md).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";
import { statusFrame } from "./helpers/fake_droid.js";

const loadFooter = async () => {
  const env = loadPageModule("footer.js", {
    respond: (path) => (path === "/fs-version.json" ? { data: { fsVersion: "bundle-9" } } : { data: {} }),
  });
  await env.settle();
  return { ...env, footer: env.element("fw-meta") };
};

test("A status version containing markup is escaped before it reaches the footer", async () => {
  const env = await loadFooter();

  env.pushStatus(statusFrame({ firmwareVersion: '<img src=x onerror="alert(1)">', fsVersion: "v1.0.0" }));

  assert.ok(
    !env.footer.innerHTML.includes("<img"),
    "a version string from the controller must not be able to inject markup",
  );
  assert.match(env.footer.innerHTML, /&lt;img/);
});

test("the footer shows the versions the droid reported, as each frame arrives", async () => {
  const env = await loadFooter();

  env.pushStatus(statusFrame({ firmwareVersion: "v1.2.3", fsVersion: "v4.5.6" }));
  assert.match(env.footer.innerHTML, /FW:.*v1\.2\.3/s);
  assert.match(env.footer.innerHTML, /FS:.*v4\.5\.6/s);

  env.pushStatus(statusFrame({ firmwareVersion: "v1.2.4", fsVersion: "v4.5.6" }));
  assert.match(env.footer.innerHTML, /FW:.*v1\.2\.4/s, "a later frame repaints it");
});

test("a firmware that sends no web version falls back to the bundle's own file", async () => {
  const env = await loadFooter();

  env.pushStatus(statusFrame({ firmwareVersion: "v1.2.3" }));
  assert.match(env.footer.innerHTML, /FS:.*bundle-9/s);
});

test("the footer asks the droid for no status of its own", async () => {
  const env = await loadFooter();
  const ownReads = env.requests.filter((request) => request.path === "/api/status");

  assert.deepEqual(ownReads, [], "the status is the Live Reading's to read, once, for every reader");
  // Every interval here is the Live Reading's single fallback poll.
  assert.equal(env.intervals.length, 1, "and the footer adds no poll beside the shell's one");
});
