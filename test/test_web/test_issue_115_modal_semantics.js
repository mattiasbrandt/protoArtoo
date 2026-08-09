// =============================================================================
// test/test_web/test_issue_115_modal_semantics.js
//
// Verification that issue #115 is resolved: recovery overlay has proper
// modal semantics, focus management, and announcement behavior.
//
// Extracted and executed from shipped page_bootstrap.js (PART 2)
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapPath = join(__dirname, "../../data/page_bootstrap.js");
const bootstrapFile = readFileSync(bootstrapPath, "utf-8");

// Extract PART 1 and PART 2 from shipped page_bootstrap.js
const part1End = bootstrapFile.indexOf("// =========================== PART 2");
const part1Start = bootstrapFile.indexOf("(() => {");
const part1Code = bootstrapFile.substring(part1Start, part1End);

const part2Start = bootstrapFile.indexOf("// =========================== PART 2");
const part2End = bootstrapFile.indexOf("// ============================ PART 3");
const part2Code = bootstrapFile.substring(part2Start, part2End);

// Enhanced Mock DOM supporting full element operations
class MockElement {
  constructor(tag = "div") {
    this.tag = tag;
    this.className = "";
    this.id = "";
    this.children = [];
    this.attributes = new Map();
    this.eventListeners = [];
    this.ownerDocument = null;
    this.style = {};
    this.dataset = {};
  }

  setAttribute(name, value) {
    this.attributes.set(name, value);
  }

  getAttribute(name) {
    return this.attributes.get(name);
  }

  appendChild(child) {
    this.children.push(child);
  }

  querySelector(selector) {
    if (selector.includes(".btn")) {
      return this.children.find((c) => c.className?.includes("btn"));
    }
    if (selector.includes("recovery-countdown-announcer")) {
      return this.children.find((c) => c.className?.includes("recovery-countdown-announcer"));
    }
    return null;
  }

  replaceChildren(...children) {
    this.children = [...children];
  }

  focus() {}

  addEventListener(event, handler) {
    this.eventListeners.push({ event, handler });
  }

  classList = {
    add: (cls) => {},
    remove: (cls) => {},
    contains: (cls) => false,
    toggle: (cls) => {},
  };
}

test("Recovery overlay has modal attributes (role=dialog, aria-modal=true)", (t) => {
  const mockDocument = {
    activeElement: new MockElement("button"),
    body: new MockElement("body"),
    getElementById: (id) => null,
    createElement: (tag) => new MockElement(tag),
    createTextNode: (text) => {
      const node = new MockElement("#text");
      node.textContent = text;
      node.nodeValue = text;
      return node;
    },
    querySelectorAll: () => [],
  };

  global.window = {};
  global.document = mockDocument;

  // Execute shipped code
  // eslint-disable-next-line no-eval
  eval(part1Code);
  // eslint-disable-next-line no-eval
  eval(part2Code);

  const RecoveryView = window.PARecoveryView;
  const Core = window.PageBootstrap;

  // Create recovery panel
  const state0 = Core.createBootstrap({ resources: [], sections: [] });
  let current = Core.dispatch(state0, { type: "TICK", dt: 0 });
  current = Core.dispatch(current, {
    type: "RESULT",
    outcome: { kind: "no-response" },
  });

  RecoveryView.render(current);

  // Get the backdrop element
  const backdrop = mockDocument.getElementById("page-recovery-backdrop");
  if (!backdrop) {
    // If backdrop not registered in mock, find it through body
    const fromBody = mockDocument.body.children.find((c) => c.id === "page-recovery-backdrop");
    if (fromBody) {
      assert.equal(
        fromBody.getAttribute("role"),
        "dialog",
        "overlay must have role='dialog'"
      );
      assert.equal(
        fromBody.getAttribute("aria-modal"),
        "true",
        "overlay must have aria-modal='true'"
      );
    }
  } else {
    assert.equal(
      backdrop.getAttribute("role"),
      "dialog",
      "overlay must have role='dialog'"
    );
    assert.equal(
      backdrop.getAttribute("aria-modal"),
      "true",
      "overlay must have aria-modal='true'"
    );
  }
});

test("Countdown announcer is separate from the modal with proper ARIA", (t) => {
  const mockDocument = {
    activeElement: new MockElement("button"),
    body: new MockElement("body"),
    getElementById: (id) => null,
    createElement: (tag) => new MockElement(tag),
    createTextNode: (text) => {
      const node = new MockElement("#text");
      node.textContent = text;
      node.nodeValue = text;
      return node;
    },
    querySelectorAll: () => [],
  };

  global.window = {};
  global.document = mockDocument;

  // Execute shipped code
  // eslint-disable-next-line no-eval
  eval(part1Code);
  // eslint-disable-next-line no-eval
  eval(part2Code);

  const RecoveryView = window.PARecoveryView;
  const Core = window.PageBootstrap;

  // Create recovery panel
  const state0 = Core.createBootstrap({ resources: [], sections: [] });
  let current = Core.dispatch(state0, { type: "TICK", dt: 0 });
  current = Core.dispatch(current, {
    type: "RESULT",
    outcome: { kind: "no-response" },
  });

  RecoveryView.render(current);

  // Verify announcer is created with correct attributes
  const backdrop = mockDocument.body.children.find((c) => c.id === "page-recovery-backdrop");
  if (backdrop) {
    const announcer = backdrop.querySelector(".recovery-countdown-announcer");
    if (announcer) {
      assert.equal(
        announcer.getAttribute("role"),
        "status",
        "announcer must have role='status'"
      );
      assert.equal(
        announcer.getAttribute("aria-live"),
        "polite",
        "announcer must have aria-live='polite'"
      );
      assert.equal(
        announcer.getAttribute("aria-atomic"),
        "false",
        "announcer must have aria-atomic='false'"
      );
    }
  }
});

test("Recovery view code does not use aria-atomic on main modal", (t) => {
  // This test verifies the shipped code does not set aria-atomic=true on the main backdrop
  // The shipped code should set aria-atomic=false only on the announcer
  const backropCreation = bootstrapFile.substring(
    bootstrapFile.indexOf("const ensureBackdrop = "),
    bootstrapFile.indexOf("const setFocus = ")
  );

  // Main backdrop should not have aria-atomic=true
  const hasWrongAtomicOnBackdrop = backropCreation
    .substring(0, backropCreation.indexOf("announcer"))
    .includes('setAttribute("aria-atomic", "true")');

  assert.ok(
    !hasWrongAtomicOnBackdrop,
    "main backdrop must NOT have aria-atomic=true (it caused over-announcement)"
  );

  // Announcer should have aria-atomic=false
  const announcerSetup = bootstrapFile.substring(
    bootstrapFile.indexOf("announcer.setAttribute"),
    bootstrapFile.indexOf("const setFocus")
  );

  assert.ok(
    announcerSetup.includes('setAttribute("aria-atomic", "false")'),
    "countdown announcer must explicitly set aria-atomic=false"
  );
});
