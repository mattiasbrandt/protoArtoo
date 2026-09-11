// =============================================================================
// test/test_web/helpers/mini_dom.js
//
// A small real DOM for the web suites: a node tree, an HTML parser behind
// innerHTML and DOMParser, id lookup that only sees attached nodes, and event
// listeners. Node ships no DOM and this repo has no jsdom, so a module whose
// whole job is moving nodes between documents -- the Operator Shell -- cannot
// be tested against a permissive stub that answers every property with a fresh
// object: "the node is no longer in the document" is exactly the fact such a
// stub cannot express.
//
// It is deliberately small. It implements what the shipped shell and bootstrap
// actually call and nothing else; an unimplemented call is an exception rather
// than a plausible answer, so a test cannot pass by accident.
// =============================================================================

const VOID_ELEMENTS = new Set([
  "area", "base", "br", "col", "embed", "hr", "img", "input",
  "link", "meta", "param", "source", "track", "wbr",
]);

const toCamel = (name) => name.replace(/-([a-z])/g, (_m, c) => c.toUpperCase());
const toDashed = (name) => name.replace(/[A-Z]/g, (c) => `-${c.toLowerCase()}`);

class MiniText {
  constructor(text) {
    this.nodeType = 3;
    this.data = text;
    this.parentNode = null;
  }
  get textContent() {
    return this.data;
  }
}

class MiniElement {
  constructor(tagName, ownerDocument) {
    this.nodeType = 1;
    this.tagName = String(tagName).toUpperCase();
    this.ownerDocument = ownerDocument;
    this.attributes = new Map();
    this.childNodes = [];
    this.parentNode = null;
    this.listeners = [];
    // <template> keeps its children out of the tree, the way the real one does.
    if (this.tagName === "TEMPLATE") this.content = new MiniFragment(ownerDocument);

    const element = this;
    this.dataset = new Proxy(
      {},
      {
        get(_target, key) {
          if (typeof key !== "string") return undefined;
          return element.attributes.get(`data-${toDashed(key)}`);
        },
        set(_target, key, value) {
          element.attributes.set(`data-${toDashed(key)}`, String(value));
          return true;
        },
        has(_target, key) {
          return element.attributes.has(`data-${toDashed(key)}`);
        },
        deleteProperty(_target, key) {
          element.attributes.delete(`data-${toDashed(key)}`);
          return true;
        },
      }
    );

    this.classList = {
      add: (name) => element.#classes(new Set([...element.#classSet(), name])),
      remove: (name) => {
        const set = element.#classSet();
        set.delete(name);
        element.#classes(set);
      },
      contains: (name) => element.#classSet().has(name),
      toggle: (name, force) => {
        const on = force === undefined ? !element.#classSet().has(name) : !!force;
        if (on) element.classList.add(name);
        else element.classList.remove(name);
        return on;
      },
    };
  }

  #classSet() {
    return new Set(String(this.attributes.get("class") || "").split(/\s+/).filter(Boolean));
  }
  #classes(set) {
    this.attributes.set("class", [...set].join(" "));
  }

  get id() {
    return this.attributes.get("id") || "";
  }
  set id(value) {
    this.attributes.set("id", String(value));
  }
  get className() {
    return this.attributes.get("class") || "";
  }
  set className(value) {
    this.attributes.set("class", String(value));
  }
  get src() {
    return this.attributes.get("src");
  }
  set src(value) {
    this.attributes.set("src", String(value));
  }
  get href() {
    return this.attributes.get("href");
  }
  get target() {
    return this.attributes.get("target");
  }

  getAttribute(name) {
    const value = this.attributes.get(name);
    return value === undefined ? null : value;
  }
  setAttribute(name, value) {
    this.attributes.set(name, String(value));
  }

  get children() {
    return this.childNodes.filter((node) => node.nodeType === 1);
  }

  get textContent() {
    return this.childNodes.map((node) => node.textContent).join("");
  }
  set textContent(value) {
    this.childNodes.forEach((node) => {
      node.parentNode = null;
    });
    this.childNodes = [];
    if (value !== "") this.appendChild(new MiniText(String(value)));
  }

  set innerHTML(html) {
    this.childNodes.forEach((node) => {
      node.parentNode = null;
    });
    this.childNodes = [];
    parseInto(this, String(html), this.ownerDocument);
  }

  appendChild(node) {
    if (node instanceof MiniFragment) {
      [...node.childNodes].forEach((child) => this.appendChild(child));
      return node;
    }
    if (node.parentNode) node.parentNode.removeChild(node);
    node.parentNode = this;
    this.childNodes.push(node);
    this.ownerDocument?.onAttach?.(node);
    return node;
  }

  removeChild(node) {
    const at = this.childNodes.indexOf(node);
    if (at >= 0) {
      this.childNodes.splice(at, 1);
      node.parentNode = null;
    }
    return node;
  }

  remove() {
    if (this.parentNode) this.parentNode.removeChild(this);
  }

  replaceChildren(...nodes) {
    this.childNodes.forEach((node) => {
      node.parentNode = null;
    });
    this.childNodes = [];
    nodes.forEach((node) => this.appendChild(node));
  }

  matches(selector) {
    return matchesSelector(this, selector);
  }

  closest(selector) {
    let node = this;
    while (node && node.nodeType === 1) {
      if (node.matches(selector)) return node;
      node = node.parentNode;
    }
    return null;
  }

  querySelectorAll(selector) {
    const found = [];
    const walk = (node) => {
      node.children.forEach((child) => {
        if (child.matches(selector)) found.push(child);
        walk(child);
      });
    };
    walk(this);
    return found;
  }

  querySelector(selector) {
    return this.querySelectorAll(selector)[0] || null;
  }

  addEventListener(type, handler, options) {
    this.listeners.push({ type, handler, capture: options === true || options?.capture === true });
  }
  removeEventListener(type, handler) {
    const at = this.listeners.findIndex((l) => l.type === type && l.handler === handler);
    if (at >= 0) this.listeners.splice(at, 1);
  }

  // Runs the handlers this element registered, the way a real interaction
  // would. Used by tests to drive a control rather than calling internals.
  fire(type, event = {}) {
    this.listeners.filter((l) => l.type === type).forEach(({ handler }) => handler(event));
  }
}

class MiniFragment {
  constructor(ownerDocument) {
    this.nodeType = 11;
    this.ownerDocument = ownerDocument;
    this.childNodes = [];
    this.parentNode = null;
  }
  get children() {
    return this.childNodes.filter((node) => node.nodeType === 1);
  }
  appendChild(node) {
    if (node.parentNode) node.parentNode.removeChild(node);
    node.parentNode = this;
    this.childNodes.push(node);
    return node;
  }
  removeChild(node) {
    const at = this.childNodes.indexOf(node);
    if (at >= 0) {
      this.childNodes.splice(at, 1);
      node.parentNode = null;
    }
    return node;
  }
}

// ---------------------------------------------------------------------------
// Selectors: attribute presence, attribute equality, tag, id and class, and a
// tag with one attribute. That is every selector the shipped shell and
// bootstrap use; anything else throws rather than quietly matching nothing.
// ---------------------------------------------------------------------------
const matchesSelector = (element, selector) => {
  const trimmed = String(selector).trim();
  let match = /^([a-zA-Z]*)\[([a-zA-Z-]+)(?:="([^"]*)")?\]$/.exec(trimmed);
  if (match) {
    const [, tag, attribute, value] = match;
    if (tag && element.tagName !== tag.toUpperCase()) return false;
    if (!element.attributes.has(attribute)) return false;
    return value === undefined || element.attributes.get(attribute) === value;
  }
  match = /^#([\w-]+)$/.exec(trimmed);
  if (match) return element.id === match[1];
  match = /^\.([\w-]+)$/.exec(trimmed);
  if (match) return element.classList.contains(match[1]);
  match = /^([a-zA-Z]+)$/.exec(trimmed);
  if (match) return element.tagName === match[1].toUpperCase();
  throw new Error(`mini_dom: unsupported selector ${selector}`);
};

// ---------------------------------------------------------------------------
// The parser. Tolerant enough for the shipped pages: doctype, comments,
// quoted and valueless attributes, void elements, and <template>.
// ---------------------------------------------------------------------------
const ATTRIBUTE_RE = /([a-zA-Z_:][-a-zA-Z0-9_:.]*)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>]+)))?/g;

const parseInto = (root, html, ownerDocument) => {
  const stack = [root];
  const top = () => stack[stack.length - 1];
  let cursor = 0;

  const appendTo = (parent, node) => {
    if (parent instanceof MiniElement && parent.tagName === "TEMPLATE") {
      parent.content.appendChild(node);
      return;
    }
    parent.appendChild(node);
  };

  while (cursor < html.length) {
    const next = html.indexOf("<", cursor);
    if (next === -1) {
      const text = html.slice(cursor);
      if (text.trim()) appendTo(top(), new MiniText(text));
      break;
    }
    if (next > cursor) {
      const text = html.slice(cursor, next);
      if (text.trim()) appendTo(top(), new MiniText(text));
    }

    if (html.startsWith("<!--", next)) {
      const close = html.indexOf("-->", next);
      cursor = close === -1 ? html.length : close + 3;
      continue;
    }
    if (html.startsWith("<!", next)) {
      const close = html.indexOf(">", next);
      cursor = close === -1 ? html.length : close + 1;
      continue;
    }
    if (html.startsWith("</", next)) {
      const close = html.indexOf(">", next);
      const name = html.slice(next + 2, close).trim().toUpperCase();
      for (let depth = stack.length - 1; depth > 0; depth -= 1) {
        if (stack[depth].tagName === name) {
          stack.length = depth;
          break;
        }
      }
      cursor = close === -1 ? html.length : close + 1;
      continue;
    }

    const close = html.indexOf(">", next);
    if (close === -1) break;
    const raw = html.slice(next + 1, close);
    const selfClosing = raw.endsWith("/");
    const body = selfClosing ? raw.slice(0, -1) : raw;
    const nameEnd = body.search(/[\s/]/);
    const name = (nameEnd === -1 ? body : body.slice(0, nameEnd)).toLowerCase();
    const element = new MiniElement(name, ownerDocument);
    if (nameEnd !== -1) {
      const attributeText = body.slice(nameEnd);
      ATTRIBUTE_RE.lastIndex = 0;
      let attribute;
      while ((attribute = ATTRIBUTE_RE.exec(attributeText)) !== null) {
        const value = attribute[2] ?? attribute[3] ?? attribute[4] ?? "";
        element.attributes.set(attribute[1], value);
      }
    }
    appendTo(top(), element);
    if (!selfClosing && !VOID_ELEMENTS.has(name)) stack.push(element);
    cursor = close + 1;
  }
};

// ---------------------------------------------------------------------------
// The document
// ---------------------------------------------------------------------------
export class MiniDocument {
  constructor() {
    this.documentElement = new MiniElement("html", this);
    this.head = new MiniElement("head", this);
    this.body = new MiniElement("body", this);
    this.documentElement.appendChild(this.head);
    this.documentElement.appendChild(this.body);
    this.listeners = [];
    this.readyState = "complete";
    this.hidden = false;
    this.visibilityState = "visible";
    this.title = "";
  }

  createElement(tagName) {
    return new MiniElement(tagName, this);
  }
  createDocumentFragment() {
    return new MiniFragment(this);
  }

  // A deep copy that belongs to this document, so a node parsed out of a
  // fetched surface document can be attached here.
  importNode(node, deep = false) {
    if (node.nodeType === 3) return new MiniText(node.data);
    if (node.nodeType === 11) {
      const fragment = new MiniFragment(this);
      if (deep) node.childNodes.forEach((child) => fragment.appendChild(this.importNode(child, true)));
      return fragment;
    }
    const copy = new MiniElement(node.tagName, this);
    node.attributes.forEach((value, key) => copy.attributes.set(key, value));
    if (node.tagName === "TEMPLATE" && deep) {
      node.content.childNodes.forEach((child) => copy.content.appendChild(this.importNode(child, true)));
      return copy;
    }
    if (deep) node.childNodes.forEach((child) => copy.appendChild(this.importNode(child, true)));
    return copy;
  }

  // Only attached nodes are findable, which is the whole point: a surface the
  // shell has detached must be invisible to the next surface's scripts.
  getElementById(id) {
    const find = (node) => {
      for (const child of node.children) {
        if (child.id === id) return child;
        const deeper = find(child);
        if (deeper) return deeper;
      }
      return null;
    };
    return find(this.documentElement);
  }

  querySelectorAll(selector) {
    return this.documentElement.querySelectorAll(selector);
  }
  querySelector(selector) {
    return this.documentElement.querySelector(selector);
  }

  addEventListener(type, handler, options) {
    this.listeners.push({ type, handler, capture: options === true || options?.capture === true });
  }
  removeEventListener(type, handler) {
    const at = this.listeners.findIndex((l) => l.type === type && l.handler === handler);
    if (at >= 0) this.listeners.splice(at, 1);
  }

  // Capture listeners first, then the rest -- the ordering the shell relies on
  // to reach a click before a surface's own delegated handler.
  dispatch(type, event) {
    const ordered = [...this.listeners.filter((l) => l.capture), ...this.listeners.filter((l) => !l.capture)];
    ordered.filter((l) => l.type === type).forEach(({ handler }) => handler(event));
  }
}

export class MiniDOMParser {
  constructor(ownerDocument) {
    this.ownerDocument = ownerDocument;
  }
  parseFromString(html) {
    const parsed = new MiniDocument();
    const root = new MiniElement("root", parsed);
    parseInto(root, html, parsed);
    const html_ = root.children.find((child) => child.tagName === "HTML");
    if (html_) {
      parsed.documentElement = html_;
      parsed.head = html_.children.find((child) => child.tagName === "HEAD") || parsed.head;
      parsed.body = html_.children.find((child) => child.tagName === "BODY") || parsed.body;
    }
    return parsed;
  }
}

// A click the way the browser delivers one: a real target, a real
// preventDefault, and the document's capture listeners.
export const clickOn = (document, element, overrides = {}) => {
  const event = {
    type: "click",
    target: element,
    button: 0,
    defaultPrevented: false,
    metaKey: false,
    ctrlKey: false,
    shiftKey: false,
    altKey: false,
    preventDefault() {
      event.defaultPrevented = true;
    },
    ...overrides,
  };
  document.dispatch("click", event);
  return event;
};
