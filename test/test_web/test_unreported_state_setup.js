// =============================================================================
// test/test_web/test_unreported_state_setup.js
//
// A serial lane nobody asked about reads unlit, never green. The lanes were
// Setup's and are Maintenance's since #404 split that page; the file keeps its
// name because a renamed test file reads to the gate as a deleted one.
// Maintenance runs through helpers/page_module_env.js - the shipped module, a
// permissive DOM stub, and a responder standing in for the droid - with the
// shipped Feature Availability module its page loads first handed in for real.
//
// Thinned from the #399 Surface Anatomy checklist (#406).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { createRequire } from "node:module";

import { loadPageModule } from "./helpers/page_module_env.js";

const require = createRequire(import.meta.url);
const { createFeatureAvailability } = require("../../data/feature_availability.js");

const loadMaintenance = (config = {}, respond = null) => {
  let deliver = null;
  const env = loadPageModule("maintenance.js", {
    respond: respond || (() => ({ data: config })),
    overrides: {
      PAFeatureAvailability: createFeatureAvailability(),
      PAStatusStream: {
        isSupported: () => true,
        subscribe: (handler) => { deliver = handler; return () => {}; },
        getLastStatus: () => null,
      },
    },
  });
  env.status = (payload) => deliver("status", payload);
  return env;
};

const HEAP_GOOD = { heapFree: 177152, heapMin: 150000, heapLargestBlock: 61440 };

test("a lane nobody asked about reads grey, never green", () => {
  const env = loadMaintenance();
  env.status({ ...HEAP_GOOD });

  assert.equal(env.element("serial-s1-light").className, "indicator off", "no drive in the frame at all");
  assert.equal(env.element("serial-s1-state").textContent, "Disabled");
  assert.equal(env.element("serial-s2-light").className, "indicator off");
  assert.equal(env.element("serial-s3-light").className, "indicator off");
});

