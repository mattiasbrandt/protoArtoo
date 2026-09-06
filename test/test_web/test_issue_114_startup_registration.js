// =============================================================================
// test/test_web/test_issue_114_startup_registration.js
//
// Startup work must go through the bootstrap (issue #114). A page module that
// fires its own request at load time bypasses the single request slot and the
// recovery panel, so a failure there leaves the page silently incomplete.
//
// The exception is rc-recent-actions: it reads localStorage synchronously with
// no network request, so it does not compete for the slot and is called
// directly.
//
// Each module is loaded and asked what it registered. This used to shell out to
// a CLI harness and substring-match its stdout, from a hardcoded absolute path
// that only resolved on one machine. Issue #146.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

// Answers every endpoint successfully; these tests are about what a module
// registers at load, not what the controller replies.
const quiet = (path) => {
  if (path === "/api/identity") {
    return {
      data: {
        droidName: "test",
        mdnsUseName: true,
        board: "artoo_esp32",
        board_capabilities: {},
        build_flags: {},
      },
    };
  }
  return { data: {} };
};

test("shell.js registers its identity load as a bootstrap section", (t) => {
  const env = loadPageModule("shell.js", { respond: quiet });

  assert.ok(
    env.sectionNames().includes("shell-identity"),
    `shell.js must register shell-identity, registered: ${env.sectionNames().join(", ") || "(none)"}`
  );
});

test("app.js registers its initial status load as a bootstrap section", (t) => {
  const env = loadPageModule("app.js", { respond: quiet });

  assert.ok(
    env.sectionNames().includes("app-initial-status"),
    `app.js must register app-initial-status, registered: ${env.sectionNames().join(", ") || "(none)"}`
  );
});

test("rc.js keeps its localStorage read out of the bootstrap", (t) => {
  const env = loadPageModule("rc.js", { respond: quiet });

  assert.ok(
    !env.sectionNames().includes("rc-recent-actions"),
    "a synchronous localStorage read does not compete for the request slot and must not take one"
  );
});

test("A registered section is a function the bootstrap can actually run", async (t) => {
  const env = loadPageModule("shell.js", { respond: quiet });

  // Registering a name is only half the contract: the bootstrap has to be able
  // to invoke it and get a promise back.
  await assert.doesNotReject(() => Promise.resolve(env.runSection("shell-identity")));
});
