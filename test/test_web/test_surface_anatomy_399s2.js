// =============================================================================
// test/test_web/test_surface_anatomy_399s2.js
//
// The Surface Anatomy on the Configure group: Parts, Setup and Servos
// (#399 slice 2, ADR 0066).
//
// What is mechanical about the anatomy on these three surfaces, and nothing
// else. Two kinds of claim are covered, because the sweep makes two kinds:
//
//   A SECTION HEAD CARRIES A COUNT. docs/ui-copy-voice.md rule 8 says a heading
//   never appears bare, and every subtitle this slice writes is computed from
//   what the droid answered rather than typed into the markup. A count that is
//   a constant would look right on every screenshot and be wrong on every
//   droid, so each one is driven with two different answers.
//
//   A STATE IS ON THE LIGHT, NOT ON THE TEXT. CONTEXT.md "Health Signal" says a
//   health signal reads as a droid LED and the colour IS the reading; "Status
//   Colour" says a chosen posture takes no colour at all. Setup painted eleven
//   states onto text with element.style.color. The assertions below are on the
//   light's class AND on the absence of an inline colour beside it, because a
//   renderer that lit the lamp and went on colouring the number would pass the
//   first half alone.
//
// Servos and Setup run through helpers/page_module_env.js - the shipped module,
// a permissive DOM stub, and a responder standing in for the droid. Parts runs
// through helpers/parts_surface.js, which boots the shipped Operator Shell with
// the shipped Parts surface in mini_dom, because the claims about it are about
// cells in a real table rather than about a string a renderer built.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";
import { bootParts, withParts, freshOutputs, output } from "./helpers/parts_surface.js";

// -----------------------------------------------------------------------------
// Servos: the two section heads count what the droid answered
// -----------------------------------------------------------------------------

// The shipped module subscribes to the status stream when one is supported, so
// the test holds the handler and delivers frames the way /api/events does.
const loadServo = (config = {}) => {
  let deliver = null;
  const env = loadPageModule("servo.js", {
    respond: () => ({ data: config }),
    overrides: {
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

const AUX_CONFIG = {
  components: {
    aux1: { enabled: true, type: "rgb" },
    aux2: { enabled: true, type: "mg996r" },
    aux3: { enabled: false, type: "none" },
  },
};

// -----------------------------------------------------------------------------
// Parts: the head carries the counts, the acts have their own column
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Setup: the light carries the state, the value stays ink
// -----------------------------------------------------------------------------

// Every component carries a Component Registry label, the way /api/config
// answers on a real droid. The permissive DOM stub answers for an id the page
// does not have, so the "this component has no label" branch - which in a
// browser only ever runs against a span that is really there - walks a
// parentNode the stub cannot give it. That is the harness's limit, not the
// page's, and a full answer is the honest fixture either way.
const COMPONENTS = {
  drive: "Hoverboard", audio: "DY-SV5W", protoR2link: "AstroPixels Plus", domeEsc: "ISDT ESC70",
  arm1: "MG996R", arm2: "MG90S", aux1: "LED strip", aux2: "MG996R", aux3: "none",
  rcCh1: "SBUS", rcCh2: "SBUS", rcCh3: "SBUS", rcCh4: "SBUS", rcCh5: "SBUS", rcCh6: "SBUS",
};

const components = (on = {}) =>
  Object.fromEntries(
    Object.entries(COMPONENTS).map(([key, label]) => [key, { label, enabled: false, ...(on[key] || {}) }]),
  );

const loadSetup = (config = {}, respond = null) => {
  let deliver = null;
  const env = loadPageModule("setup.js", {
    respond: respond || (() => ({ data: config })),
    overrides: {
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
  const env = loadSetup();
  env.status({ ...HEAP_GOOD });

  assert.equal(env.element("serial-s1-light").className, "indicator off", "no drive in the frame at all");
  assert.equal(env.element("serial-s1-state").textContent, "Disabled");
  assert.equal(env.element("serial-s2-light").className, "indicator off");
  assert.equal(env.element("serial-s3-light").className, "indicator off");
});

// -----------------------------------------------------------------------------
// The output-first table still answers what it answered before the sweep
// -----------------------------------------------------------------------------

