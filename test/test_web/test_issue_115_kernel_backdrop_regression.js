// =============================================================================
// test/test_web/test_issue_115_kernel_backdrop_regression.js
//
// Regression test for the reopened #115: recovery overlay broke when the
// kernel-created backdrop pre-existed.
//
// The kernel's _recovery_kernel.html creates #page-recovery-backdrop in the
// DOM before page_bootstrap.js loads. ensureBackdrop() must UPGRADE this
// pre-existing element with dialog semantics, not return it untouched.
//
// This test simulates that real condition: pre-populate the DOM with a
// backdrop that has the kernel's attributes (role="status", aria-live,
// aria-atomic), then call render(). It must not throw, and the element
// must be upgraded to dialog semantics with an announcer.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapPath = join(__dirname, "../../data/page_bootstrap.js");
const bootstrapFile = readFileSync(bootstrapPath, "utf-8");

// Extract the recovery view code (PART 2)
const part2Start = bootstrapFile.indexOf("// =========================== PART 2");
const part2End = bootstrapFile.indexOf("// ============================ PART 3");
const part2Code = bootstrapFile.substring(part2Start, part2End);

// Also need the state model from PART 1
const part1End = bootstrapFile.indexOf("// =========================== PART 2");
const part1Start = bootstrapFile.indexOf("(() => {");
const part1Code = bootstrapFile.substring(part1Start, part1End);

// Minimal DOM mock that supports the operations ensureBackdrop() uses
class MockElement {
  constructor(tag = "div", className = "", ownerDocument = null) {
    this.tag = tag;
    this.className = className;
    this.id = "";
    this.dataset = {};
    this.style = {};
    this.textContent = "";
    this.children = [];
    this.attributes = new Map();
    this.eventListeners = [];
    this.ownerDocument = ownerDocument;
  }

  setAttribute(name, value) {
    this.attributes.set(name, value);
    // If we're setting an id and have access to the document, register ourselves
    if (name === "id" && this.ownerDocument && this.ownerDocument.elements) {
      this.id = value;
      this.ownerDocument.elements.set(value, this);
    }
  }

  getAttribute(name) {
    return this.attributes.get(name) || null;
  }

  appendChild(child) {
    if (!child) return; // Handle null gracefully
    if (!this.children.includes(child)) {
      this.children.push(child);
      // If the child has an id and we have a document, register it
      if (child.id && this.ownerDocument && this.ownerDocument.elements) {
        this.ownerDocument.elements.set(child.id, child);
      }
    }
  }

  querySelector(selector) {
    // Simple implementation: only handles class selectors
    if (selector.startsWith(".")) {
      const className = selector.substring(1);
      return this.children.find((c) => c.className && c.className.includes(className));
    }
    return null;
  }

  querySelectorAll(selector) {
    const results = [];
    const search = (el) => {
      if (selector.startsWith(".")) {
        const className = selector.substring(1);
        if (el.className && el.className.includes(className)) {
          results.push(el);
        }
      }
      if (el.children) {
        el.children.forEach(search);
      }
    };
    this.children.forEach(search);
    return results;
  }

  replaceChildren(...children) {
    this.children = [];
    children.forEach((child) => {
      if (child) this.children.push(child);
    });
  }

  addEventListener(event, handler) {
    this.eventListeners.push({ event, handler });
  }

  focus() {
    // Mock focus
  }

  classList = {
    add: () => {},
    remove: () => {},
    contains: () => false,
    toggle: () => {},
  };
}

// Mock document with the capability to store elements
class MockDocument {
  constructor() {
    this.elements = new Map();
    this.body = new MockElement("body", "", this);
  }

  getElementById(id) {
    return this.elements.get(id) || null;
  }

  createElement(tag) {
    return new MockElement(tag, "", this);
  }

  createTextNode(text) {
    const node = new MockElement("#text", "", this);
    node.textContent = text;
    node.nodeValue = text;
    return node;
  }

  querySelector() {
    return null;
  }

  querySelectorAll() {
    return [];
  }

  get activeElement() {
    return this.body;
  }
}

test("Issue #115 regression: ensureBackdrop() upgrades kernel-created backdrop", (t) => {
  // Set up a mock DOM like what the browser has after the kernel script runs
  const mockDocument = new MockDocument();

  // Pre-create the kernel's backdrop element EXACTLY as _recovery_kernel.html does
  // This is what the real bug depended on.
  const kernelBackdrop = new MockElement("div");
  kernelBackdrop.id = "page-recovery-backdrop";
  kernelBackdrop.setAttribute("role", "status");
  kernelBackdrop.setAttribute("aria-live", "polite");
  kernelBackdrop.setAttribute("aria-atomic", "true");
  mockDocument.elements.set("page-recovery-backdrop", kernelBackdrop);
  mockDocument.body.appendChild(kernelBackdrop);

  // Install global window and document for the bootstrap code
  global.window = { activeElement: mockDocument.body };
  global.document = mockDocument;

  // Execute PART 1 (state model)
  // eslint-disable-next-line no-eval
  eval(part1Code);

  // Execute PART 2 (recovery view) which contains ensureBackdrop and render
  // eslint-disable-next-line no-eval
  eval(part2Code);

  // Get the recovery view exports
  const RecoveryView = window.PARecoveryView;
  const Core = window.PageBootstrap;

  assert.ok(RecoveryView, "PARecoveryView should be defined");
  assert.ok(RecoveryView.render, "render function should exist");
  assert.ok(Core, "PageBootstrap should be defined");

  // Verify the kernel-created backdrop is in the DOM before we call render
  const beforeUpgrade = mockDocument.getElementById("page-recovery-backdrop");
  assert.strictEqual(beforeUpgrade.id, "page-recovery-backdrop");
  assert.strictEqual(
    beforeUpgrade.getAttribute("role"),
    "status",
    "kernel backdrop should have role=status initially"
  );
  assert.strictEqual(
    beforeUpgrade.getAttribute("aria-atomic"),
    "true",
    "kernel backdrop should have aria-atomic=true from kernel"
  );

  // Create bootstrap state with a failed resource to trigger recovery panel
  const state = Core.createBootstrap({
    resources: ["page-styles"],
    sections: ["shell-identity"],
  });

  // Advance to show loading state
  let current = Core.dispatch(state, { type: "TICK", dt: 0 });

  // Call render() with the pre-existing kernel backdrop in the DOM
  // This was throwing "TypeError: Failed to execute 'appendChild' on 'Node': parameter 1 is not of type 'Node'"
  // because ensureBackdrop() returned the kernel element untouched, so the announcer was never created
  assert.doesNotThrow(
    () => RecoveryView.render(current),
    "render() must not throw when backdrop pre-exists from kernel"
  );

  // Verify the backdrop was UPGRADED with dialog semantics
  const backdrop = mockDocument.getElementById("page-recovery-backdrop");
  assert.ok(backdrop, "backdrop should still exist");

  assert.strictEqual(
    backdrop.getAttribute("role"),
    "dialog",
    "ensureBackdrop() must upgrade role from 'status' to 'dialog'"
  );

  assert.strictEqual(
    backdrop.getAttribute("aria-modal"),
    "true",
    "ensureBackdrop() must add aria-modal='true'"
  );

  assert.ok(
    backdrop.getAttribute("aria-label"),
    "ensureBackdrop() must add aria-label"
  );

  // Verify the announcer was created
  const announcer = backdrop.querySelector(".recovery-countdown-announcer");
  assert.ok(announcer, "ensureBackdrop() must create .recovery-countdown-announcer child");

  assert.strictEqual(
    announcer.getAttribute("role"),
    "status",
    "announcer must have role='status'"
  );

  assert.strictEqual(
    announcer.getAttribute("aria-live"),
    "polite",
    "announcer must have aria-live='polite'"
  );

  assert.strictEqual(
    announcer.getAttribute("aria-atomic"),
    "false",
    "announcer must have aria-atomic='false' to avoid over-announcement"
  );

  // Verify the Tab handler is attached (idempotency check)
  assert.strictEqual(
    backdrop.dataset.tabHandlerAttached,
    "true",
    "keydown handler should be marked as attached"
  );

  // Call render again to verify idempotency
  assert.doesNotThrow(
    () => RecoveryView.render(current),
    "render() should be idempotent and not throw on second call"
  );

  // Verify announcer still exists (wasn't lost on second render)
  const announcerAfter = backdrop.querySelector(".recovery-countdown-announcer");
  assert.ok(announcerAfter, "announcer should still exist after second render");

  // Verify the announcer is the same element (not recreated)
  assert.strictEqual(
    announcer,
    announcerAfter,
    "announcer should be the same element, not recreated"
  );
});

test("Issue #115 regression: render() handles missing announcer gracefully", (t) => {
  // Edge case: if announcer is somehow missing, render() should not throw
  const mockDocument = new MockDocument();

  // Pre-create a broken backdrop that's missing the announcer
  const backdropNoAnnouncer = new MockElement("div");
  backdropNoAnnouncer.id = "page-recovery-backdrop";
  backdropNoAnnouncer.setAttribute("role", "status");
  mockDocument.elements.set("page-recovery-backdrop", backdropNoAnnouncer);
  mockDocument.body.appendChild(backdropNoAnnouncer);

  global.window = { activeElement: mockDocument.body };
  global.document = mockDocument;

  // eslint-disable-next-line no-eval
  eval(part1Code);
  // eslint-disable-next-line no-eval
  eval(part2Code);

  const RecoveryView = window.PARecoveryView;
  const Core = window.PageBootstrap;

  const state = Core.createBootstrap({
    resources: ["page-styles"],
    sections: ["shell-identity"],
  });

  let current = Core.dispatch(state, { type: "TICK", dt: 0 });

  // Even if announcer is missing, render() should not throw
  assert.doesNotThrow(
    () => RecoveryView.render(current),
    "render() must handle missing announcer gracefully"
  );

  const backdrop = mockDocument.getElementById("page-recovery-backdrop");
  assert.ok(backdrop, "backdrop should exist");

  // ensureBackdrop() should have created the missing announcer
  const announcer = backdrop.querySelector(".recovery-countdown-announcer");
  assert.ok(announcer, "ensureBackdrop() should create missing announcer");
});

test("Issue #115 regression: ensureBackdrop() is idempotent", (t) => {
  const mockDocument = new MockDocument();

  global.window = { activeElement: mockDocument.body };
  global.document = mockDocument;

  // eslint-disable-next-line no-eval
  eval(part1Code);
  // eslint-disable-next-line no-eval
  eval(part2Code);

  const RecoveryView = window.PARecoveryView;
  const Core = window.PageBootstrap;

  const state = Core.createBootstrap({
    resources: [],
    sections: ["section1"],
  });

  let current = Core.dispatch(state, { type: "TICK", dt: 0 });

  // Call render() three times
  RecoveryView.render(current);
  const backdrop1 = mockDocument.getElementById("page-recovery-backdrop");
  assert.ok(backdrop1, "backdrop should exist after first render");
  const announcer1 = backdrop1.querySelector(".recovery-countdown-announcer");
  assert.ok(announcer1, "announcer should exist after first render");
  const handlersAttached1 = backdrop1.dataset.tabHandlerAttached;

  RecoveryView.render(current);
  const backdrop2 = mockDocument.getElementById("page-recovery-backdrop");
  assert.ok(backdrop2, "backdrop should exist after second render");
  const announcer2 = backdrop2.querySelector(".recovery-countdown-announcer");
  assert.ok(announcer2, "announcer should exist after second render");
  const handlersAttached2 = backdrop2.dataset.tabHandlerAttached;

  RecoveryView.render(current);
  const backdrop3 = mockDocument.getElementById("page-recovery-backdrop");
  assert.ok(backdrop3, "backdrop should exist after third render");
  const announcer3 = backdrop3.querySelector(".recovery-countdown-announcer");
  assert.ok(announcer3, "announcer should exist after third render");
  const handlersAttached3 = backdrop3.dataset.tabHandlerAttached;

  // Verify the same element is returned each time
  assert.strictEqual(
    backdrop1,
    backdrop2,
    "ensureBackdrop should return the same element"
  );
  assert.strictEqual(
    backdrop2,
    backdrop3,
    "ensureBackdrop should return the same element on subsequent calls"
  );

  // Verify the announcer is the same each time (not recreated)
  assert.strictEqual(
    announcer1,
    announcer2,
    "announcer should be the same after second render"
  );
  assert.strictEqual(
    announcer2,
    announcer3,
    "announcer should be the same after third render"
  );

  // Verify handlers are only attached once
  assert.strictEqual(handlersAttached1, "true");
  assert.strictEqual(handlersAttached2, "true");
  assert.strictEqual(handlersAttached3, "true");

  // Verify handler attachment marker is consistent
  assert.strictEqual(
    backdrop3.dataset.tabHandlerAttached,
    "true",
    "handler attachment marker should remain consistent"
  );
});
