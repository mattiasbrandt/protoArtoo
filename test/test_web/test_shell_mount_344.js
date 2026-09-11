// =============================================================================
// test/test_web/test_shell_mount_344.js
//
// The Common Page Bootstrap mounting a surface after the page has loaded --
// the seam the Operator Shell is built on (ADR 0048, #344).
//
// Runs the SHIPPED reducer and browser host together in one vm context, with a
// script-loading DOM stub that resolves each <script src> the host appends, so
// these tests observe the real Resource Step Recovery cursor walking a second
// wave rather than a model of it.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const bootstrapFile = readFileSync(join(__dirname, "../../data/page_bootstrap.js"), "utf-8");

// PART 1 (reducer) and PART 3 (browser host) of the shipped file. PART 2 is the
// recovery view, which the host reaches through optional chaining.
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const part3Src = bootstrapFile.substring(part3Marker);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// A page whose initial chain is `chain`, with every appended <script src>
// answered from `scriptOutcome` (default: loads). Tests drive it through the
// real window.PABootstrap the host publishes.
const makeEnv = ({ chain = [] } = {}) => {
  const env = {
    loaded: [],          // script URLs the host actually appended, in order
    events: [],          // window events the host dispatched
    scriptFails: new Map(), // url -> remaining failures before it loads
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
    dispatchEvent: (event) => {
      env.events.push(event.type);
      return true;
    },
    location: { origin: "http://device" },
  };

  const documentMock = {
    currentScript: { dataset: { scripts: chain.join(",") } },
    readyState: "loading",
    hidden: false,
    visibilityState: "visible",
    addEventListener: () => {},
    querySelectorAll: () => [],
    createElement: () => ({ remove() {} }),
    body: {
      appendChild: (script) => {
        // Answer the load the way a browser would: asynchronously, and with a
        // failure first when the test asked for one, so the retry path is the
        // shipped one rather than a stub.
        const remaining = env.scriptFails.get(script.src) || 0;
        setTimeout(() => {
          if (remaining > 0) {
            env.scriptFails.set(script.src, remaining - 1);
            script.onerror?.();
            return;
          }
          env.loaded.push(script.src);
          script.onload?.();
        }, 5).unref?.();
      },
    },
  };

  class FakeEvent {
    constructor(type) {
      this.type = type;
    }
  }
  class FakeCustomEvent extends FakeEvent {
    constructor(type, opts = {}) {
      super(type);
      this.detail = opts.detail;
    }
  }

  const context = {
    window: windowMock,
    document: documentMock,
    console: { warn: () => {}, log: () => {}, error: () => {} },
    AbortController,
    Date,
    JSON,
    Event: FakeEvent,
    CustomEvent: FakeCustomEvent,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
  };
  context.globalThis = context;

  vm.runInNewContext(part1Src, context, { filename: "page_bootstrap.part1.js" });
  vm.runInNewContext(part3Src, context, { filename: "page_bootstrap.part3.js" });

  env.window = windowMock;
  env.PABootstrap = windowMock.PABootstrap;
  env.fireLoad = () => {
    (windowListeners.get("load") || []).forEach((fn) => fn(new FakeEvent("load")));
  };
  return env;
};

test("a surface mounted after the page loaded gets its scripts loaded, in order", async () => {
  const env = makeEnv({ chain: ["/web_api.js", "/shell.js"] });
  env.fireLoad();
  await sleep(60);
  assert.deepEqual(env.loaded, ["/web_api.js", "/shell.js"], "the shell's own chain loads first");

  env.PABootstrap.mountResources(["/drive.js", "/drive_extra.js"]);
  await sleep(80);
  assert.deepEqual(
    env.loaded,
    ["/web_api.js", "/shell.js", "/drive.js", "/drive_extra.js"],
    "the surface's wave loads after the shell chain, in declared order",
  );
});

test("a script already loaded for the session is not loaded again by a later surface", async () => {
  const env = makeEnv({ chain: ["/web_api.js", "/shell.js"] });
  env.fireLoad();
  await sleep(60);

  // Every surface document declares the whole shared prefix; the shell hands
  // the chain over as-is and the bootstrap is what makes it load once.
  env.PABootstrap.mountResources(["/web_api.js", "/shell.js", "/dome.js"]);
  await sleep(80);
  assert.deepEqual(env.loaded, ["/web_api.js", "/shell.js", "/dome.js"]);
});

test("a section declared by a late-mounted surface actually runs", async () => {
  const env = makeEnv({ chain: ["/shell.js"] });
  let shellSectionRuns = 0;
  env.PABootstrap.registerSection("shell-identity", async () => {
    shellSectionRuns += 1;
  });
  env.fireLoad();
  await sleep(60);
  assert.equal(shellSectionRuns, 1, "the shell's own section runs at boot");

  // The surface's script is what registers the section, so the registration
  // lands while its own wave of resources is still loading.
  let driveSectionRuns = 0;
  env.PABootstrap.mountResources([
    {
      name: "/drive.js",
      load: (done) => {
        env.PABootstrap.registerSection("drive-config", async () => {
          driveSectionRuns += 1;
        });
        done(null);
      },
    },
  ]);
  await sleep(80);

  assert.equal(driveSectionRuns, 1, "the mounted surface's section must run");
  assert.equal(shellSectionRuns, 1, "an already-done section must not be re-run by a mount");
});

test("pa:assets-ready is announced once for the session, not once per mount", async () => {
  const env = makeEnv({ chain: ["/shell.js"] });
  env.fireLoad();
  await sleep(60);
  assert.deepEqual(
    env.events.filter((type) => type === "pa:assets-ready"),
    ["pa:assets-ready"],
    "the first settle announces readiness",
  );

  env.PABootstrap.mountResources(["/dome.js"]);
  await sleep(80);
  env.PABootstrap.mountResources(["/seq.js"]);
  await sleep(80);

  assert.deepEqual(
    env.events.filter((type) => type === "pa:assets-ready"),
    ["pa:assets-ready"],
    "mounting a surface must not re-announce readiness -- /api/events is opened once per session",
  );
});

test("a surface's markup loads through Resource Step Recovery, and a shed connection is retried", async () => {
  const env = makeEnv({ chain: ["/shell.js"] });
  env.fireLoad();
  await sleep(60);

  const attempts = [];
  env.PABootstrap.mountResources([
    {
      name: "/dome.html",
      load: (done) => {
        attempts.push(Date.now());
        // First attempt is refused the way the controller sheds a connection
        // under burst pressure; the second succeeds.
        done(attempts.length === 1 ? { kind: "network" } : null);
      },
    },
    "/dome.js",
  ]);

  // The no-response backoff for a first failure is 2000 ms.
  await sleep(60);
  assert.equal(attempts.length, 1, "the markup is attempted once and then waits");
  assert.deepEqual(env.loaded, ["/shell.js"], "the surface's scripts must wait behind its markup");

  await sleep(2600);
  assert.equal(attempts.length, 2, "the failed markup step is retried, not abandoned");
  assert.deepEqual(env.loaded, ["/shell.js", "/dome.js"], "the scripts load once the markup is in");
});

test("a failed script in a surface's wave is retried without re-running the wave's earlier steps", async () => {
  const env = makeEnv({ chain: ["/shell.js"] });
  env.fireLoad();
  await sleep(60);

  env.scriptFails.set("/sound.js", 1);
  env.PABootstrap.mountResources(["/health_signals.js", "/sound.js"]);
  await sleep(80);
  assert.deepEqual(env.loaded, ["/shell.js", "/health_signals.js"], "the cursor pauses on the failure");

  await sleep(2600);
  assert.deepEqual(
    env.loaded,
    ["/shell.js", "/health_signals.js", "/sound.js"],
    "only the failed step is retried",
  );
});
