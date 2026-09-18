// =============================================================================
// test/test_web/test_unreported_state_rc.js
//
// A receiver nobody switched on is not a healthy receiver. RC Control runs
// through helpers/page_module_env.js - the shipped module, a permissive DOM
// stub, and a responder standing in for the droid.
//
// Thinned from the #399 Surface Anatomy checklist (#406).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

const loadRc = (config, diagnostics) =>
  loadPageModule("rc.js", {
    respond: (path) => {
      if (path === "/api/config") return { data: config };
      if (path === "/api/rc/map") return { data: { mode: config?.rc?.inputMode, map: config?.__map || [] } };
      if (path === "/api/rc") return { data: diagnostics };
      return { data: {} };
    },
    overrides: {
      PAStatusStream: { isSupported: () => false, subscribe: () => () => {}, getLastStatus: () => null },
    },
  });

const RC_COMPONENTS = { rcCh1: { enabled: true }, rcCh2: { enabled: false } };

test("a receiver nobody switched on reads unlit, never green", async () => {
  const env = loadRc(
    { rc: { inputMode: "single_sbus" }, components: RC_COMPONENTS },
    { sources: { sbus1: { enabled: true, linked: true, ageMs: 12 },
                 sbus2: { enabled: true, linked: false, ageMs: 900 },
                 pwm: { enabled: false } } },
  );
  await env.runSection("rc-diagnostics");
  await env.settle();

  const html = env.element("rc-preview-source-health").innerHTML;
  assert.match(html, /<div class="indicator ok"[^>]*><\/div>\s*<span>SBUS1<\/span>/, "a linked receiver is nominal");
  assert.match(html, /<div class="indicator warn"[^>]*><\/div>\s*<span>SBUS2<\/span>/, "a receiver still waiting is degraded");
  assert.match(html, /<div class="indicator off"[^>]*><\/div>\s*<span>PWM<\/span>/, "a source never asked reads grey");
  assert.doesNotMatch(html, /indicator ok"[^>]*><\/div>\s*<span>PWM/, "a source nobody switched on read as nominal");
  assert.match(html, /not switched on/);
});

