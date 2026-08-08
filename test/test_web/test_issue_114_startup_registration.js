// =============================================================================
// test/test_web/test_issue_114_startup_registration.js
//
// Verification that issue #114 is resolved: two of three startup requests
// (shell identity, app initial status) are now registered as bootstrap
// sections instead of bypassing it.
//
// rc-recent-actions is NOT registered because it reads localStorage
// synchronously with no network request, so it's called directly during
// startup instead and silently handles errors.
//
// Uses the page_module_harness to load real page modules and observe
// what sections they register.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { execSync } from "child_process";

test("shell.js registers identity load as a section", (t) => {
  // The harness output shows registered sections with their names.
  // A properly registered section appears as:
  //   filename   section-name         RESOLVED  <-- SWALLOWS
  const output = execSync(
    "node test/test_web/helpers/page_module_harness.js shell.js 2>&1",
    {
      cwd: "/home/mattias/Documents/GitHub/protoArtoo",
      encoding: "utf-8",
    }
  );

  assert(
    output.includes("shell-identity"),
    "shell.js must register 'shell-identity' as a bootstrap section"
  );
});

test("app.js registers initial status load as a section", (t) => {
  const output = execSync(
    "node test/test_web/helpers/page_module_harness.js app.js 2>&1",
    {
      cwd: "/home/mattias/Documents/GitHub/protoArtoo",
      encoding: "utf-8",
    }
  );

  assert(
    output.includes("app-initial-status"),
    "app.js must register 'app-initial-status' as a bootstrap section"
  );
});

test("rc.js does NOT register recent action tokens as a section", (t) => {
  // rc-recent-actions is not a network request (just localStorage), so it
  // does not compete for the bootstrap slot. It's called directly during
  // startup instead and silently handles errors (corrupt data → empty list).
  const output = execSync(
    "node test/test_web/helpers/page_module_harness.js rc.js 2>&1",
    {
      cwd: "/home/mattias/Documents/GitHub/protoArtoo",
      encoding: "utf-8",
    }
  );

  assert(
    !output.includes("rc-recent-actions"),
    "rc.js must NOT register 'rc-recent-actions' as a bootstrap section"
  );
});
