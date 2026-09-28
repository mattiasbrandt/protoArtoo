// =============================================================================
// test/test_web/test_build_pair.js
//
// The Dashboard's This droid card says whether the firmware and the web assets
// came from one build.
//
// The filesystem's version is the firmware's with "fs-" in front of it
// (tools/extract_version.py), and the card compared the two strings as they
// stood, so a droid flashed from one commit always read "does not match the
// firmware" (#399, found on the #355 bench round). A builder told that goes
// looking for an upload that never went wrong.
//
// Runs the REAL data/app.js with the real status stream and Live Reading,
// delivers one frame, and reads what the card says.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { loadPageModule } from "./helpers/page_module_env.js";
import { statusFrame } from "./helpers/fake_droid.js";

const FIRMWARE = "v1.3.2-1184-g088c90b2+epic-operator-experience";

const pairReading = (fsVersion) => {
  const env = loadPageModule("app.js");
  env.pushStatus(statusFrame({ firmwareVersion: FIRMWARE, fsVersion }));
  return env.element("build-firmware-detail").textContent;
};

test("firmware and web assets from one build read as a match, and assets from another build do not", () => {
  // The pair exactly as the bench droid reported it: the stamp the build
  // writes into the filesystem, beside the firmware it was built with.
  assert.match(pairReading(`fs-${FIRMWARE}`), / - match$/, "one build is a match");
  assert.match(
    pairReading("fs-v1.3.2-1183-g3b1681b5+epic-operator-experience"),
    /does not match the firmware/,
    "assets from another commit are still a mismatch",
  );
});
