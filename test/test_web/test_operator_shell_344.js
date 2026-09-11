// =============================================================================
// test/test_web/test_operator_shell_344.js
//
// The Operator Shell (#344, ADR 0048), run end to end: the shipped
// page_bootstrap.js drives the shipped shell.js against the shipped
// data/index.html, and every surface it mounts is the shipped .html file,
// fetched through a recording transport. Nothing here restates what the shell
// does -- the assertions are about where nodes ended up and what was asked of
// the controller.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument, MiniDOMParser, clickOn } from "./helpers/mini_dom.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

const bootstrapFile = readData("page_bootstrap.js");
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const part3Src = bootstrapFile.substring(part3Marker);
const shellSrc = readData("shell.js");
const statusStreamSrc = readData("status_stream.js");

const IDENTITY = {
  droidName: "artoo",
  board: "artoo_esp32",
  board_capabilities: { sbus: true },
  build_flags: { audio: true },
};

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Boots a real shell: index.html's frame in the document, index.html's own
// chain driven by the bootstrap host, shell.js executed for real when the host
// reaches it, and every other script answered as a load so the chain advances.
const boot = async ({ hash = "", stored = null } = {}) => {
  const document = new MiniDocument();
  const indexHtml = readData("index.html");
  const parsedIndex = new MiniDOMParser().parseFromString(indexHtml);
  parsedIndex.body.children.forEach((child) => document.body.appendChild(document.importNode(child, true)));
  const chain = /data-scripts="([^"]*)"/.exec(indexHtml)[1];
  document.documentElement.setAttribute("data-scripts", chain);
  document.body.setAttribute("data-page", "home");
  // The host reads its chain from its own script tag, exactly as the inline
  // recovery kernel hands it over.
  document.currentScript = { dataset: { scripts: chain } };

  const env = {
    document,
    requests: [],
    events: [],
    scriptsLoaded: [],
    store: new Map(stored ? [["pa.shell.v1", stored]] : []),
  };

  const windowListeners = new Map();
  const windowMock = {
    setTimeout: (fn, ms) => {
      const timer = setTimeout(fn, ms);
      timer.unref?.();
      return timer;
    },
    clearTimeout: (id) => clearTimeout(id),
    setInterval: (fn, ms) => {
      const timer = setInterval(fn, ms);
      timer.unref?.();
      return timer;
    },
    clearInterval: (id) => clearInterval(id),
    addEventListener: (type, fn) => {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(fn);
    },
    removeEventListener: () => {},
    dispatchEvent: (event) => {
      env.events.push({ type: event.type, detail: event.detail });
      (windowListeners.get(event.type) || []).forEach((fn) => fn(event));
      return true;
    },
    // Two browser behaviours the shell depends on, so the stub has them rather
    // than letting a test pass on something no browser does: an assigned
    // fragment reads back with its "#", and assigning one is a navigation that
    // fires hashchange, while history.replaceState changes the address without
    // one.
    location: {
      origin: "http://device",
      _hash: hash,
      get hash() {
        return this._hash;
      },
      set hash(value) {
        const text = String(value);
        const next = text === "" || text.startsWith("#") ? text : `#${text}`;
        if (next === this._hash) return;
        this._hash = next;
        fireHashChange();
      },
    },
    history: {
      replaceState: (_state, _title, url) => {
        windowMock.location._hash = String(url);
      },
    },
    localStorage: {
      getItem: (key) => (env.store.has(key) ? env.store.get(key) : null),
      setItem: (key, value) => env.store.set(key, String(value)),
      removeItem: (key) => env.store.delete(key),
    },
    PAApi: {
      get: async (path) => {
        env.requests.push(path);
        if (path === "/api/identity") return { data: IDENTITY };
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
    },
    PAUtils: { escapeHtml: (value) => String(value) },
  };

  class FakeEvent {
    constructor(type) {
      this.type = type;
    }
  }

  const fireHashChange = () => {
    (windowListeners.get("hashchange") || []).forEach((fn) => fn(new FakeEvent("hashchange")));
  };
  class FakeCustomEvent extends FakeEvent {
    constructor(type, opts = {}) {
      super(type);
      this.detail = opts.detail;
    }
  }

  const context = {
    window: windowMock,
    document,
    console: { warn: () => {}, log: () => {}, error: (...args) => env.events.push({ type: "console.error", detail: args }) },
    AbortController,
    Date,
    JSON,
    Object,
    Array,
    Set,
    Map,
    String,
    Number,
    Boolean,
    Promise,
    Error,
    Math,
    Event: FakeEvent,
    CustomEvent: FakeCustomEvent,
    DOMParser: class {
      parseFromString(html, type) {
        return new MiniDOMParser().parseFromString(html, type);
      }
    },
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
  };
  context.globalThis = context;

  // Every /api/events connection this session opens, so the budget the whole
  // ticket turns on is counted rather than argued about.
  env.streamsOpened = [];
  context.EventSource = class {
    constructor(url) {
      env.streamsOpened.push(url);
      this.url = url;
    }
    addEventListener() {}
    close() {}
  };

  // The host appends a <script src> per resource; answer each the way a browser
  // would, and execute the files under test for real.
  const REAL_SCRIPTS = { "/shell.js": shellSrc, "/status_stream.js": statusStreamSrc };
  document.onAttach = (node) => {
    if (node.nodeType !== 1 || node.tagName !== "SCRIPT" || !node.src) return;
    const src = node.src;
    setTimeout(() => {
      env.scriptsLoaded.push(src);
      if (REAL_SCRIPTS[src]) vm.runInNewContext(REAL_SCRIPTS[src], context, { filename: src });
      node.onload?.();
    }, 2).unref?.();
  };

  vm.runInNewContext(part1Src, context, { filename: "page_bootstrap.part1.js" });
  vm.runInNewContext(part3Src, context, { filename: "page_bootstrap.part3.js" });

  env.window = windowMock;
  // What an operator does: change the address. Assigning it is the navigation.
  env.navigate = (to) => {
    windowMock.location.hash = to;
  };
  env.storedSurface = () => {
    const raw = env.store.get("pa.shell.v1");
    return raw === undefined ? undefined : JSON.parse(raw).surface;
  };
  // Exactly one surface is in the document at a time, and this is it.
  env.mountedSurface = () => {
    const attached = document.querySelectorAll("[data-surface]");
    assert.ok(attached.length <= 1, `${attached.length} surfaces are attached at once`);
    return attached[0]?.dataset.surface;
  };

  // The host deferred start() to the load event because readyState is
  // "complete" here; it starts immediately, so just let the chain run.
  await sleep(140);
  return env;
};

test("the shell mounts the Dashboard surface into its own frame, from the file that still owns that markup", async () => {
  const env = await boot();

  assert.equal(env.mountedSurface(), "home", "the Dashboard surface is the mounted one");
  assert.ok(
    env.document.getElementById("health-grid"),
    "the Dashboard's markup is in the live document, not merely fetched",
  );
  assert.ok(env.requests.includes("/dashboard.html"), "the surface's markup came from its own file");
  assert.equal(env.document.body.dataset.page, "home", "body carries the surface's data-page identifier");
  assert.equal(env.window.location.hash, "#home", "the address names what is shown");
});

test("changing screen keeps the frame: the same topbar and status nodes, and no second pa:assets-ready", async () => {
  const env = await boot();
  const topBefore = env.document.getElementById("shell-top");
  const statusBefore = env.document.getElementById("shell-status");
  const readyBefore = env.events.filter((e) => e.type === "pa:assets-ready").length;

  env.navigate("#seq");
  await sleep(140);

  assert.equal(env.mountedSurface(), "seq");
  assert.strictEqual(env.document.getElementById("shell-top"), topBefore, "the topbar node survived the navigation");
  assert.strictEqual(env.document.getElementById("shell-status"), statusBefore, "the status bar node survived");
  assert.equal(readyBefore, 1, "the session announced readiness once");
  assert.equal(
    env.events.filter((e) => e.type === "pa:assets-ready").length,
    1,
    "navigating must not re-announce readiness -- /api/events is opened once per session, not once per page view",
  );
});

test("leaving a surface takes its elements out of the document, so ids cannot collide", async () => {
  const env = await boot();
  assert.ok(env.document.getElementById("health-grid"), "Dashboard is mounted to begin with");

  env.navigate("#firmware");
  await sleep(140);

  assert.equal(env.mountedSurface(), "firmware");
  assert.equal(
    env.document.getElementById("health-grid"),
    null,
    "the surface that was left is no longer findable by id",
  );
  assert.ok(env.document.getElementById("reboot-button"), "the mounted surface's own controls are findable");
});

test("returning to a surface repaints it without asking the controller for it again", async () => {
  const env = await boot();
  env.navigate("#wifi");
  await sleep(140);
  const wifiCard = env.document.getElementById("wifi-posture-card");
  assert.ok(wifiCard, "WiFi mounted");

  env.navigate("#home");
  await sleep(60);
  env.navigate("#wifi");
  await sleep(60);

  assert.strictEqual(
    env.document.getElementById("wifi-posture-card"),
    wifiCard,
    "the same nodes come back, so what the surface already had is still there",
  );
  assert.equal(
    env.requests.filter((path) => path === "/wifi.html").length,
    1,
    "the surface's markup is fetched once for the session",
  );
});

test("a cold boot never lands in Setup, and the persisted value is what says so", async () => {
  const env = await boot();
  env.navigate("#setup");
  await sleep(140);

  assert.equal(env.document.body.dataset.page, "setup", "the address is honoured -- the runtime answer CAN be Setup");
  assert.equal(
    JSON.parse(env.store.get("pa.shell.v1")).surface,
    "home",
    "the serialised value still says the last remembered surface, not Setup",
  );

  // The proof that matters is the next cold boot, with nothing in the address.
  const next = await boot({ stored: env.store.get("pa.shell.v1") });
  assert.equal(next.mountedSurface(), "home");
});

test("a stored surface of Setup is corrected, and the correction is written back", async () => {
  const env = await boot({ stored: JSON.stringify({ surface: "setup" }) });

  assert.equal(env.mountedSurface(), "home", "a store that says Setup does not land there");
  assert.equal(
    JSON.parse(env.store.get("pa.shell.v1")).surface,
    "home",
    "and it stops saying Setup rather than being re-filtered on every boot",
  );
});

test("a reload honours the address: the remembered surface only answers when the address says nothing", async () => {
  const env = await boot({ hash: "#rc", stored: JSON.stringify({ surface: "dome" }) });
  assert.equal(env.mountedSurface(), "rc", "the address wins over the memory");

  const noAddress = await boot({ stored: JSON.stringify({ surface: "dome" }) });
  assert.equal(noAddress.mountedSurface(), "dome", "and the memory answers when there is no address");
});

test("a deep link resolves by the surface's canonical name as well as by its identifier", async () => {
  const byName = await boot({ hash: "#sequences" });
  assert.equal(byName.mountedSurface(), "seq");
  assert.equal(
    byName.document.body.dataset.page,
    "seq",
    "the operator-facing name and the data-page identifier are different things, and a rename moves only the first",
  );

  const byIdentifier = await boot({ hash: "#seq" });
  assert.equal(byIdentifier.mountedSurface(), "seq");
});

test("an address this build does not know opens something rather than nothing", async () => {
  const env = await boot({ hash: "#no-such-surface" });
  assert.equal(env.mountedSurface(), "home");
  assert.equal(env.window.location.hash, "#home", "and the address is corrected to what is shown");
});

test("a link to a surface's own .html address routes inside the shell instead of reloading the page", async () => {
  const env = await boot();
  const link = env.document.createElement("a");
  link.setAttribute("href", "/setup.html");
  env.document.querySelector("[data-surface]").appendChild(link);

  const event = clickOn(env.document, link);
  await sleep(140);

  assert.ok(event.defaultPrevented, "the browser is not allowed to fetch the document");
  assert.equal(env.window.location.hash, "#setup");
  assert.equal(env.mountedSurface(), "setup", "and the shell mounted it instead");
});

test("a modified click on a legacy link is left to the browser", async () => {
  const env = await boot();
  const link = env.document.createElement("a");
  link.setAttribute("href", "/setup.html");
  env.document.querySelector("[data-surface]").appendChild(link);

  const event = clickOn(env.document, link, { metaKey: true });
  assert.equal(event.defaultPrevented, false, "opening in a new tab still opens a document");
  assert.equal(env.mountedSurface(), "home");
});

test("a surface's topbar actions ride the topbar, and are the same nodes when it is returned to", async () => {
  const env = await boot();
  const estop = env.document.getElementById("estop-toggle");
  assert.ok(estop, "the Dashboard's topbar actions are mounted beside the nav");
  assert.strictEqual(
    estop.closest("#shell-top-actions"),
    env.document.getElementById("shell-top-actions"),
    "beside the nav, not in the content region",
  );

  env.navigate("#dome");
  await sleep(140);
  assert.equal(env.document.getElementById("estop-toggle"), null, "they leave with the surface that owns them");

  env.navigate("#home");
  await sleep(60);
  assert.strictEqual(
    env.document.getElementById("estop-toggle"),
    estop,
    "and the same node comes back, so the handlers bound to it are still the ones on screen",
  );
});

test("a surface mounted after the session settled is still told the identity it missed", async () => {
  const env = await boot();
  const before = env.events.filter((e) => e.type === "pa:identity-available").length;
  assert.ok(before >= 1, "identity was announced at boot");

  env.navigate("#servo");
  await sleep(140);

  assert.equal(
    env.events.filter((e) => e.type === "pa:identity-available").length,
    before + 1,
    "the surface that mounted late hears the outcome it would have heard as a page load",
  );
});

test("the browser title names the surface and the droid, the same way round on every surface", async () => {
  const env = await boot();
  assert.equal(env.document.title, "Dashboard - artoo");

  env.navigate("#sound");
  await sleep(140);
  assert.equal(env.document.title, "Sound - artoo");

  env.navigate("#seq");
  await sleep(140);
  assert.equal(env.document.title, "Sequences - artoo");
});

test("the nav offers every surface, addressed by hash so the fragment never reaches the controller", async () => {
  const env = await boot();
  const links = env.document.querySelectorAll("[data-surface-link]");
  assert.equal(links.length, 10, "ten surfaces in the nav");
  links.forEach((link) => {
    assert.equal(
      link.getAttribute("href"),
      `#${link.dataset.surfaceLink}`,
      "every nav address is a fragment, so the browser only ever asks the device for /",
    );
  });
  assert.equal(
    env.requests.filter((path) => !path.startsWith("/api/") && !path.endsWith(".html")).length,
    0,
    "and nothing else was asked of the controller",
  );
});

test("the live status stream is opened once for the session, however many screens are visited", async () => {
  const env = await boot();
  // What footer.js does on every page today: hold a subscription for as long
  // as the document lives. Under the shell the document is the session.
  env.window.PAStatusStream.subscribe(() => {});
  await sleep(20);
  assert.deepEqual(env.streamsOpened, ["/api/events"], "one stream at boot");

  for (const route of ["#drive", "#dome", "#rc", "#home", "#drive"]) {
    env.navigate(route);
    await sleep(140);
  }

  assert.deepEqual(
    env.streamsOpened,
    ["/api/events"],
    "five navigations, still one slot of the controller's three -- a navigation used to take and release one",
  );
});

// ---------------------------------------------------------------------------
// Markup invariants. What a file declares decides whether the shell can mount
// it at all, and that is settled before any script runs.
// ---------------------------------------------------------------------------

// The shell's routes, as data/shell.js carries them, read from the shipped file
// so this cannot drift from what actually ships.
const registry = [...shellSrc.matchAll(/\{ page: "([a-z]+)", doc: "\/([a-z]+\.html)"/g)]
  .map(([, page, file]) => ({ page, file }));

test("the shell knows every surface, and every surface is a file that exists", () => {
  assert.equal(registry.length, 10, "ten surfaces");
  registry.forEach(({ file }) => {
    assert.doesNotThrow(() => readData(file), `${file} is served`);
  });
});

test("the shell document carries a content region and no surface of its own", () => {
  const parsed = new MiniDOMParser().parseFromString(readData("index.html"));
  assert.deepEqual(
    parsed.body.children.map((child) => child.id),
    ["shell-top", "shell-content", "shell-status"],
    "index.html is the frame; a surface's markup lives in the file that owns it",
  );
});

test("every surface document hands a direct visit to the shell at its own route", () => {
  registry.forEach(({ page, file }) => {
    const html = readData(file);
    assert.match(
      html,
      new RegExp(`location\\.replace\\("/#${page}"\\)`),
      `${file} must delegate to its own route, not another's`,
    );
    assert.match(html, /<body data-page="([a-z]+)"/, `${file} declares a data-page identifier`);
    assert.equal(
      /<body data-page="([a-z]+)"/.exec(html)[1],
      page,
      `${file}: the route the delegate names is the data-page identifier, not the operator-facing name`,
    );
    // The delegate has to run before the recovery kernel, or the visit spends a
    // request loading a bootstrap for a document that is about to be replaced.
    assert.ok(
      html.indexOf("PAShellDelegate") < html.indexOf("PA:INCLUDE _recovery_kernel.html"),
      `${file}: the delegate must come before the kernel`,
    );
  });
});
