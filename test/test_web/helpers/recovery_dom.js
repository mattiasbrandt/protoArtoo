// =============================================================================
// test/test_web/helpers/recovery_dom.js
//
// A DOM mock good enough to run the shipped recovery view (PART 2 of
// data/page_bootstrap.js) for real: it resolves the selectors that code
// actually uses, tracks document.activeElement through focus(), and can
// deliver a keyboard event to the listeners the code installed.
//
// It exists so recovery tests can assert on what the shipped code DID -
// which element ended up focused, whether the event was consumed - instead
// of matching substrings of its source. A mock that cannot answer those
// questions is what pushed those tests into source-text assertions in the
// first place (issues #138, #146).
// =============================================================================

import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapPath = join(__dirname, "../../../data/page_bootstrap.js");

export const bootstrapSource = readFileSync(bootstrapPath, "utf-8");

const PART2_MARKER = "// =========================== PART 2";
const PART3_MARKER = "// ============================ PART 3";

// PART 1 is the state model (window.PageBootstrap), PART 2 the recovery view
// (window.PARecoveryView). PART 2 needs PART 1's state to render anything, so
// tests evaluate both.
export const part1Source = bootstrapSource.substring(
  bootstrapSource.indexOf("(() => {"),
  bootstrapSource.indexOf(PART2_MARKER)
);
export const part2Source = bootstrapSource.substring(
  bootstrapSource.indexOf(PART2_MARKER),
  bootstrapSource.indexOf(PART3_MARKER)
);

// -----------------------------------------------------------------------------
// Selector matching
//
// Covers the shapes the recovery view uses: grouped selectors, tag names,
// compound classes (".btn.accent"), attribute presence ("[href]"), attribute
// equality, and :not(). Anything outside that raises rather than silently
// matching nothing - a selector the mock does not understand must not read as
// "no elements found".
// -----------------------------------------------------------------------------
const TOKEN = /^(?:([a-zA-Z][\w-]*)|\.([\w-]+)|\[([\w-]+)(?:=(?:"([^"]*)"|'([^']*)'|([^\]]*)))?\]|:not\(([^)]*)\))/;

const hasClass = (node, className) =>
  typeof node.className === "string" && node.className.split(/\s+/).includes(className);

const matchesCompound = (node, compound) => {
  let rest = compound.trim();
  if (!rest) return false;

  while (rest.length > 0) {
    const token = TOKEN.exec(rest);
    if (!token) {
      throw new Error(`recovery_dom: unsupported selector fragment "${rest}"`);
    }
    const [matched, tag, className, attrName, dq, sq, bare, notInner] = token;

    if (tag !== undefined) {
      if (String(node.tag).toLowerCase() !== tag.toLowerCase()) return false;
    } else if (className !== undefined) {
      if (!hasClass(node, className)) return false;
    } else if (attrName !== undefined) {
      const value = node.getAttribute(attrName);
      if (value === null || value === undefined) return false;
      const expected = dq ?? sq ?? bare;
      if (expected !== undefined && String(value) !== expected) return false;
    } else if (notInner !== undefined) {
      if (matchesCompound(node, notInner)) return false;
    }

    rest = rest.slice(matched.length);
  }
  return true;
};

const matchesSelector = (node, selector) =>
  selector.split(",").some((group) => {
    const compound = group.trim();
    if (compound.includes(" ") || compound.includes(">")) {
      throw new Error(`recovery_dom: combinators are not supported ("${compound}")`);
    }
    return matchesCompound(node, compound);
  });

// -----------------------------------------------------------------------------
// Nodes
// -----------------------------------------------------------------------------
export class MockElement {
  constructor(tag = "div", ownerDocument = null) {
    this.tag = tag;
    this.className = "";
    this.id = "";
    this.type = "";
    this.textContent = "";
    this.dataset = {};
    this.style = {};
    this.children = [];
    this.attributes = new Map();
    this.eventListeners = [];
    this.ownerDocument = ownerDocument;
    // Counted so a test can tell "focus was moved here" apart from "focus was
    // moved here again".
    this.focusCount = 0;
    this.classes = new Set();
    this.classList = {
      add: (cls) => this.classes.add(cls),
      remove: (cls) => this.classes.delete(cls),
      contains: (cls) => this.classes.has(cls),
      toggle: (cls) => (this.classes.has(cls) ? this.classes.delete(cls) : this.classes.add(cls)),
    };
  }

  setAttribute(name, value) {
    this.attributes.set(name, String(value));
    if (name === "id") this.id = String(value);
  }

  getAttribute(name) {
    if (this.attributes.has(name)) return this.attributes.get(name);
    if (name === "id" && this.id) return this.id;
    return null;
  }

  appendChild(child) {
    if (!child) return child;
    if (!this.children.includes(child)) this.children.push(child);
    return child;
  }

  replaceChildren(...children) {
    this.children = children.filter(Boolean);
  }

  // Depth-first, document order - the order the browser would return.
  descendants() {
    const out = [];
    const walk = (node) => {
      for (const child of node.children) {
        out.push(child);
        walk(child);
      }
    };
    walk(this);
    return out;
  }

  querySelector(selector) {
    return this.descendants().find((node) => matchesSelector(node, selector)) || null;
  }

  querySelectorAll(selector) {
    return this.descendants().filter((node) => matchesSelector(node, selector));
  }

  addEventListener(event, handler) {
    this.eventListeners.push({ event, handler });
  }

  removeEventListener(event, handler) {
    this.eventListeners = this.eventListeners.filter(
      (l) => !(l.event === event && l.handler === handler)
    );
  }

  focus() {
    this.focusCount += 1;
    if (this.ownerDocument) this.ownerDocument.activeElement = this;
  }
}

export class MockDocument {
  constructor() {
    this.body = new MockElement("body", this);
    this.activeElement = this.body;
    this.visibilityState = "visible";
  }

  createElement(tag) {
    return new MockElement(tag, this);
  }

  createTextNode(text) {
    const node = new MockElement("#text", this);
    node.textContent = text;
    return node;
  }

  getElementById(id) {
    if (!id) return null;
    return this.body.descendants().find((node) => node.id === id) || null;
  }

  querySelector(selector) {
    return this.body.querySelector(selector);
  }

  querySelectorAll(selector) {
    return this.body.querySelectorAll(selector);
  }

  addEventListener() {}
}

// -----------------------------------------------------------------------------
// Loading the shipped recovery view
// -----------------------------------------------------------------------------

// Evaluates the shipped PART 1 + PART 2 against a fresh mock document and
// returns the globals they installed. Each call gets its own document and its
// own module closure, so the view's module-level focus state does not leak
// between tests.
export const loadRecoveryView = () => {
  const document = new MockDocument();
  const window = {};

  global.window = window;
  global.document = document;

  // eslint-disable-next-line no-eval
  (0, eval)(part1Source);
  // eslint-disable-next-line no-eval
  (0, eval)(part2Source);

  return {
    document,
    window,
    Core: window.PageBootstrap,
    RecoveryView: window.PARecoveryView,
    backdrop: () => document.getElementById("page-recovery-backdrop"),
  };
};

// The step the recovery fixtures block on. A required resource is used rather
// than a section because a section that is merely retrying leaves the page
// usable, and the view deliberately stays hidden for that (deriveView's
// "get out of the operator's way" rule).
export const BLOCKING_RESOURCE = "app.js";

// Backoff after the first failed attempt, from PART 1's
// NO_RESPONSE_BASE_BACKOFF_MS. Fixtures that assert on the countdown depend on
// this being the wait they are counting down.
export const FIRST_BACKOFF_MS = 2000;

// Builds the bootstrap state that puts the recovery panel on screen, by
// driving the shipped reducer rather than hand-building a state object.
// attempts=1 lands in "no-response"; 2 or more lands in "retrying".
export const stateShowingRecovery = (Core, { attempts = 1 } = {}) => {
  let state = Core.createBootstrap({ resources: [BLOCKING_RESOURCE], sections: [] });
  // First tick starts the resource; each RESULT fails it and schedules a retry.
  state = Core.dispatch(state, { type: "TICK", dt: 0 });
  for (let i = 0; i < attempts; i += 1) {
    state = Core.dispatch(state, {
      type: "RESULT",
      outcome: { kind: "no-response", reason: "timeout" },
    });
    if (i < attempts - 1) {
      // Pull the retry forward so the next failure is a fresh attempt rather
      // than a no-op on a step that is not due yet.
      state = Core.dispatch(state, { type: "RETRY_NOW", name: BLOCKING_RESOURCE });
    }
  }
  return state;
};

// Settles the blocking resource, which is what hides the overlay again.
export const stateHidingRecovery = (Core, state) =>
  Core.dispatch(Core.dispatch(state, { type: "RETRY_NOW", name: BLOCKING_RESOURCE }), {
    type: "RESULT",
    outcome: { kind: "success" },
  });

// -----------------------------------------------------------------------------
// Events
// -----------------------------------------------------------------------------
export const keyEvent = (key, { shiftKey = false } = {}) => {
  const event = {
    key,
    shiftKey,
    defaultPrevented: false,
    preventDefault() {
      this.defaultPrevented = true;
    },
  };
  return event;
};

// Delivers an event to every listener the shipped code registered for that
// type. Returns the event so the caller can inspect defaultPrevented.
export const dispatchEvent = (node, type, event) => {
  const listeners = node.eventListeners.filter((l) => l.event === type);
  if (listeners.length === 0) {
    throw new Error(`recovery_dom: no "${type}" listener installed on <${node.tag}>`);
  }
  listeners.forEach(({ handler }) => handler(event));
  return event;
};
