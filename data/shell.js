// =============================================================================
// data/shell.js
//
// The Operator Shell (ADR 0048): the persistent frame every surface is shown
// inside. It owns the topbar, the nav, the identity and the status bar, and it
// survives every navigation -- content swaps beneath it in #shell-content while
// the /api/events stream opened at boot is never torn down.
//
// Addresses are hash routes (/#drive). The browser only ever asks the device
// for /, the static handler's default file answers with the shell, and the
// fragment never reaches the ESP32 -- so nothing here needs a firmware change.
//
// The ten .html files stay addressable: each is still the single copy of its
// surface's markup, which this file fetches and mounts, and each carries a thin
// delegate that hands a direct visit over to the shell. Nothing that links to
// one has to be rewritten.
// =============================================================================
(() => {
  // ---------------------------------------------------------------------------
  // The surfaces
  //
  // `page` is the data-page identifier and is the route: it is what the shell,
  // the CSS (body[data-page="..."]) and every surface script key on, and it is
  // deliberately NOT the operator-facing name. Renaming a surface changes
  // `name`, and adds the old spelling to `aliases` so links that were already
  // written keep opening what they name; it never touches `page` (#288).
  //
  // `name` is the one place a surface is named: the nav, the browser title and
  // the Page Recovery View's "Loading: ..." all read it, so they cannot say
  // three different things about the same screen.
  // ---------------------------------------------------------------------------
  const SURFACES = [
    { page: "home", doc: "/dashboard.html", icon: "🏠", name: "Dashboard", aliases: ["dashboard"] },
    { page: "drive", doc: "/drive.html", icon: "🏎️", name: "Drive", aliases: [] },
    { page: "dome", doc: "/dome.html", icon: "🔄", name: "Dome", aliases: [] },
    { page: "sound", doc: "/sound.html", icon: "🔊", name: "Sound", aliases: [] },
    { page: "servo", doc: "/servo.html", icon: "🦾", name: "Servos", aliases: ["servos"] },
    { page: "seq", doc: "/seq.html", icon: "🎬", name: "Sequences", aliases: ["sequences"] },
    { page: "rc", doc: "/rc.html", icon: "🕹️", name: "RC Control", aliases: [] },
    { page: "setup", doc: "/setup.html", icon: "⚙️", name: "Setup", aliases: [] },
    { page: "wifi", doc: "/wifi.html", icon: "📶", name: "WiFi", aliases: [] },
    { page: "firmware", doc: "/firmware.html", icon: "💾", name: "Firmware", aliases: [] },
  ];

  const DEFAULT_PAGE = "home";

  // Setup is the guided first-run takeover, not a place to come back to, so a
  // cold boot must never land there. The reference solves this by never writing
  // the authoring desk to the durable value at all, rather than by filtering it
  // on the way out (r2d2-astromech-simulator src/js/config/workspaces.js:208),
  // and that asymmetry is the point: the runtime answer CAN be Setup -- a
  // reload of /#setup honours its address -- while the remembered answer
  // structurally cannot be.
  const NEVER_REMEMBERED = new Set(["setup"]);

  const surfaceFor = new Map();
  SURFACES.forEach((surface) => {
    surfaceFor.set(surface.page, surface);
    surface.aliases.forEach((alias) => surfaceFor.set(alias, surface));
  });

  // The legacy address of each surface, plus the two spellings of the shell's
  // own document, so a link written before hash routes still opens what it
  // names.
  const surfaceForPath = new Map([
    ["/", surfaceFor.get(DEFAULT_PAGE)],
    ["/index.html", surfaceFor.get(DEFAULT_PAGE)],
  ]);
  SURFACES.forEach((surface) => surfaceForPath.set(surface.doc, surface));

  // ---------------------------------------------------------------------------
  // The remembered surface
  //
  // One serialised object, so a reader can assert against what was actually
  // stored rather than against the in-memory answer -- a regression that only
  // shows on the next cold boot has to be able to fail now.
  // ---------------------------------------------------------------------------
  const STORAGE_KEY = "pa.shell.v1";

  const readStored = () => {
    try {
      const raw = window.localStorage?.getItem(STORAGE_KEY);
      const parsed = raw ? JSON.parse(raw) : null;
      return parsed && typeof parsed === "object" ? parsed : {};
    } catch (_error) {
      // Storage unavailable or holding something this version cannot read:
      // start from nothing rather than failing the boot over a preference.
      return {};
    }
  };

  const writeStored = (patch) => {
    try {
      window.localStorage?.setItem(STORAGE_KEY, JSON.stringify({ ...readStored(), ...patch }));
    } catch (_error) {
      // A browser refusing storage costs the operator the memory of where they
      // were, and nothing else.
    }
  };

  const rememberSurface = (page) => {
    if (NEVER_REMEMBERED.has(page)) return;
    writeStored({ surface: page });
  };

  // The stored value is also corrected on the way in, and the correction is
  // written back: a store that says Setup -- from a hand edit, or from a
  // version that wrote it -- must stop saying Setup rather than be re-filtered
  // on every boot.
  const rememberedSurface = () => {
    const stored = readStored().surface;
    if (surfaceFor.has(stored) && !NEVER_REMEMBERED.has(stored)) return surfaceFor.get(stored);
    if (stored !== undefined) writeStored({ surface: DEFAULT_PAGE });
    return surfaceFor.get(DEFAULT_PAGE);
  };

  // ---------------------------------------------------------------------------
  // Identity
  // ---------------------------------------------------------------------------
  let identityName = "protoartoo";
  let currentSurface = null;

  const applyIdentityName = (name) => {
    identityName = String(name || "protoartoo");
    // "<surface> - <droid>", the same way round on every surface. Dashboard and
    // Sound used to put the droid first, which read as a different page rather
    // than as the same page named differently.
    const surface = currentSurface || surfaceFor.get(DEFAULT_PAGE);
    document.title = `${surface.name} - ${identityName}`;
    document.querySelectorAll("[data-identity-name]").forEach((el) => {
      el.textContent = identityName;
    });
  };

  // Layer 1 validation: ensure the identity manifest conforms to the expected shape
  // before it reaches feature availability resolvers. Protects against null, non-objects,
  // and responses missing required structure. Returns null if validation fails, otherwise
  // returns the validated identity.
  const validateIdentityShape = (identity) => {
    // Identity must be an object
    if (!identity || typeof identity !== "object" || Array.isArray(identity)) {
      return null;
    }
    // board must be a string
    if (typeof identity.board !== "string") {
      return null;
    }
    // board_capabilities and build_flags must be objects if present
    if (!identity.board_capabilities || (typeof identity.board_capabilities !== "object" || Array.isArray(identity.board_capabilities) || identity.board_capabilities === null)) {
      return null;
    }
    if (!identity.build_flags || (typeof identity.build_flags !== "object" || Array.isArray(identity.build_flags) || identity.build_flags === null)) {
      return null;
    }
    // Every present value in board_capabilities and build_flags must be a boolean
    for (const value of Object.values(identity.board_capabilities)) {
      if (typeof value !== "boolean") {
        return null;
      }
    }
    for (const value of Object.values(identity.build_flags)) {
      if (typeof value !== "boolean") {
        return null;
      }
    }
    return identity;
  };

  // Dispatches an identity outcome and keeps what it said, so a surface that
  // mounts after it happened can still be told. See replaySessionFacts().
  let lastIdentityEvent = null;

  const announceIdentity = (event) => {
    lastIdentityEvent = { type: event.type, detail: event.detail };
    if (typeof window.dispatchEvent === "function") {
      window.dispatchEvent(event);
    }
  };

  // Publish the shell's once-per-session identity result for feature consumers.
  // Identity is fetched once at boot and cached in window.PAIdentity.
  // Feature availability resolution reads this cache only and never probes endpoints
  // to discover capabilities — the manifest is authoritative and must not be rediscovered.
  // Setup listens to the event, while the cache closes late-load ordering gaps.
  // Layer 1 validation ensures the manifest conforms to the expected shape before
  // pa:identity-available is published; invalid manifests are treated as unavailable.
  const publishIdentity = (identity) => {
    const validatedIdentity = validateIdentityShape(identity);
    window.PAIdentity = validatedIdentity;
    if (validatedIdentity) {
      announceIdentity(new CustomEvent("pa:identity-available", { detail: validatedIdentity }));
    } else {
      // Invalid manifest shape is treated as unavailable
      announceIdentity(new CustomEvent("pa:identity-unavailable", { detail: { error: "invalid manifest", reason: "incompatible" } }));
    }
    return validatedIdentity;
  };

  const loadIdentity = async ({ handle = null } = {}) => {
    let result;
    try {
      const api = handle || window.PAApi;
      result = api
        ? await api.get("/api/identity")
        : { data: await fetch("/api/identity", { cache: "no-store" }).then((r) => r.json()) };
    } catch (error) {
      // Transport failure: retryable. Unchanged behaviour.
      console.warn("[shell] identity unavailable:", error);
      announceIdentity(new CustomEvent("pa:identity-unavailable", { detail: { error, reason: "no-response" } }));
      throw error;
    }

    applyIdentityName(result.data?.droidName);

    // publishIdentity is the single validation boundary and has already
    // dispatched pa:identity-unavailable if the manifest is unusable.
    if (!publishIdentity(result.data)) {
      const error = new Error("identity manifest failed validation");
      error.kind = "incompatible";
      error.status = 200;   // the response was a valid 2xx; its content was not
      throw error;          // terminal: the bootstrap maps this to failed-terminal
    }
  };

  // Facts the session settled before this surface existed. A surface mounted
  // into the shell runs its scripts long after boot, so an event it would have
  // heard as part of a page load has already fired; the identity outcome is
  // replayed to it here, the same way PAStatusStream hands a new subscriber the
  // last status it saw. Consumers render the outcome rather than counting it,
  // so hearing it again is a repaint.
  const replaySessionFacts = () => {
    if (!lastIdentityEvent) return;
    if (typeof window.dispatchEvent !== "function") return;
    window.dispatchEvent(new CustomEvent(lastIdentityEvent.type, { detail: lastIdentityEvent.detail }));
  };

  window.addEventListener("pa:identity-updated", (event) => {
    applyIdentityName(event.detail?.droidName);
    publishIdentity(event.detail);
  });

  window.PAUi = window.PAUi || {};
  if (typeof window.PAUi.setupActionText !== "function") {
    window.PAUi.setupActionText = (action) => `${action} in Setup`;
  }
  if (typeof window.PAUi.setupActionHtml !== "function") {
    window.PAUi.setupActionHtml = (action) => `${action} in <a class="setup-link" href="/setup.html">Setup</a>`;
  }

  // ---------------------------------------------------------------------------
  // The Latching Estop: what its label says, in every state
  //
  // Every state produces text, including the one before any status has arrived.
  // An estop that renders nothing while it is not engaged cannot be told from
  // one that has stopped updating, which is the whole reason the reference's
  // paint function has no blank branch (r2d2-astromech-simulator v1.79.0,
  // src/js/app/hud.js:188).
  // ---------------------------------------------------------------------------
  const ESTOP_STATE_TEXT = {
    unknown: "Estop: finding out",
    clear: "Estop: clear",
    latched: "Estop: latched",
  };

  // ---------------------------------------------------------------------------
  // Chrome: rendered once, and never again. Everything a navigation changes is
  // an attribute on what is already there, so nothing the shell owns is rebuilt
  // out from under a handler bound to it.
  // ---------------------------------------------------------------------------
  const shellTop = document.getElementById("shell-top");
  if (shellTop) {
    const navHtml = SURFACES.map(
      (surface) =>
        `<a href="#${surface.page}" data-surface-link="${surface.page}">${surface.icon} ${surface.name}</a>`
    ).join("");

    shellTop.innerHTML = `
      <div class="topbar">
        <a href="#${DEFAULT_PAGE}" class="topbar-brand">
          <img src="/r2d2body.svg" alt="R2-D2 body icon" class="topbar-logo">
          <div>
            <h1 data-identity-name>protoartoo</h1>
            <div class="subtitle">R2-D2 Body Controller</div>
          </div>
        </a>
        <div class="topbar-right">
          <!-- The estop is chrome, not a surface's control: it is written here,
               once, so every screen is shown beneath the same one. The action
               line and the line under it are FIXED - they say what a press
               does, which the state line cannot, because the state line says
               what the droid is doing (the reference's fixed-title discipline,
               src/js/maestro/hw-ui.js:227, as visible text rather than a title
               because a title carries no affordance on a bench tablet,
               docs/ui-copy-voice.md rule 12 / ADR 0059). -->
          <div class="shell-estop">
            <!-- The accessible name opens with the word on the face of the
                 button, so someone driving the page by voice can say what
                 they can see (WCAG 2.5.3 Label in Name) - the one control
                 where being unable to say "press STOP" would matter most. -->
            <button id="shell-estop-button" class="btn danger shell-estop-button" type="button"
                    aria-label="STOP - cut drive now. Clear it on Drive or Dashboard.">
              <span class="shell-estop-action">🛑 STOP</span>
              <span class="shell-estop-consequence">Cuts drive - clear it on Drive or Dashboard</span>
            </button>
            <div class="shell-estop-state" id="shell-estop-state" role="status" aria-live="polite">${ESTOP_STATE_TEXT.unknown}</div>
            <div class="shell-estop-feedback feedback compact-feedback" id="shell-estop-feedback" role="status" aria-live="polite" aria-atomic="true"></div>
          </div>
          <div class="topbar-actions" id="shell-top-actions"></div>
        </div>
      </div>
      <nav>
        ${navHtml}
      </nav>
    `;
  }

  const shellStatus = document.getElementById("shell-status");
  if (shellStatus) {
    shellStatus.innerHTML = `
      <div class="status-bar" id="conn-status">
        <div class="status-subline" id="fw-meta">Loading firmware info...</div>
      </div>
    `;
  }

  applyIdentityName(identityName);

  // Register identity load with the bootstrap if available; otherwise run it directly.
  // This ensures the identity request is routed through the bootstrap's single-slot
  // recovery mechanism rather than competing with other startup GETs.
  if (window.PABootstrap) {
    window.PABootstrap.registerSection("shell-identity", loadIdentity, {
      label: "droid identity",
    });
  } else {
    loadIdentity();
  }

  // ---------------------------------------------------------------------------
  // The Latching Estop
  //
  // The shell renders it once and never again (above), so changing screen
  // cannot take it away, cannot re-mount it and cannot clear a latch: the only
  // thing a navigation touches is what is inside #shell-content. What it shows
  // is read from the status the session already holds, never from the surface
  // that happens to be mounted -- the reference's own discipline, where the
  // bench clock follows the board being connected rather than which workspace
  // is open (r2d2-astromech-simulator v1.79.0, src/js/maestro/hw-host.js:341).
  //
  // Latching only. Releasing a latched estop stays on Drive and Dashboard,
  // where an operator went on purpose, because the direction that lets a droid
  // move again must not be one press from every screen (ADR 0048).
  // ---------------------------------------------------------------------------

  // Fallback cadence when the browser has no EventSource. The mounted surface's
  // own poll cannot serve this control: the point of it is that it outlives the
  // surface. Matches the Dashboard's fallback cadence so a no-stream session
  // asks at one rate rather than two.
  const ESTOP_POLL_MS = 3000;

  const estopButton = document.getElementById("shell-estop-button");
  const estopStateLine = document.getElementById("shell-estop-state");
  const estopFeedback = document.getElementById("shell-estop-feedback");

  // null until the droid has said something. Three answers, three texts: see
  // ESTOP_STATE_TEXT above for why there is no blank one.
  let estopLatched = null;

  const renderEstopState = () => {
    if (!estopStateLine) return;
    const key = estopLatched === null ? "unknown" : estopLatched ? "latched" : "clear";
    estopStateLine.textContent = ESTOP_STATE_TEXT[key];
    // Red is "something is stopped or refused" and nothing else colours for
    // state (#327), so the state line takes it only while the latch is set.
    // The button's own face is red at all times: that is the control's
    // identity, not a readout.
    estopStateLine.classList.toggle("is-latched", estopLatched === true);
  };

  const showEstopFeedback = (message, level = "") => {
    if (!estopFeedback) return;
    estopFeedback.textContent = message;
    estopFeedback.className = level
      ? `shell-estop-feedback feedback compact-feedback ${level}`
      : "shell-estop-feedback feedback compact-feedback";
  };

  const applyEstopStatus = (payload) => {
    if (!payload || typeof payload !== "object") return;
    estopLatched = !!payload.estop;
    renderEstopState();
  };

  // The one status read the shell owns. It hands what came back to
  // PAStatusStream rather than keeping it, so the session's last status has one
  // home and a consumer that asks later is answered from it instead of
  // fetching again.
  const readStatusOnce = async ({ handle = null } = {}) => {
    const api = handle || window.PAApi;
    if (!api) return;
    const result = await api.get("/api/status", { cache: "no-store" });
    if (window.PAStatusStream?.seed) window.PAStatusStream.seed(result.data);
    else applyEstopStatus(result.data);
  };

  // The device pushes a status event on a change and on nothing else, so a
  // client that connects to a quiet droid is told nothing at all. This is the
  // read that closes that gap; every later change arrives on the stream.
  const loadInitialStatus = async ({ handle = null } = {}) => {
    if (window.PAStatusStream?.getLastStatus?.()) {
      applyEstopStatus(window.PAStatusStream.getLastStatus());
      return;
    }
    await readStatusOnce({ handle });
  };

  if (estopButton) {
    // Deliberately unguarded against a second press while the first is in
    // flight. POST /api/estop is idempotent in the firmware -- failsafeTrigger()
    // sets a bit it may already hold (src/failsafe_gate.cpp) -- and
    // estopPostForm bypasses the request slot and never retries, so a second
    // press cannot queue behind the first. A pending guard here would swallow
    // exactly the press an operator makes because the first looked like it did
    // nothing.
    estopButton.addEventListener("click", async () => {
      if (!window.PAApi) return;
      showEstopFeedback("Stopping the droid...");
      try {
        await window.PAApi.estopPostForm("/api/estop", {}, { timeoutMs: 3000 });
      } catch (error) {
        // A stop that did not reach the droid has to say so in its own line:
        // the state line still reports what the droid last told us, which is
        // not the same thing and must not be overwritten with a guess.
        showEstopFeedback(`Stop failed: ${window.PAApi.messageFor(error)} - press again`, "error");
        return;
      }
      showEstopFeedback("Stop sent", "success");
      try {
        await readStatusOnce();
      } catch (error) {
        // The stop already succeeded; only the confirmation read failed. The
        // firmware broadcasts the new status itself, so the state line catches
        // up on the stream a moment later.
        console.warn("[shell] status read after stop failed:", error);
      }
    });

    // Subscribed in both modes: the stream is where a change arrives, and it
    // is also what readStatusOnce() hands its answer to, so this is the one
    // path into the control whether the status was pushed or fetched.
    window.PAStatusStream?.subscribe((eventType, payload) => {
      if (eventType === "status") applyEstopStatus(payload);
    });

    if (!window.PAStatusStream?.isSupported()) {
      // Chrome, so deliberately NOT a surface-owned poll (#360): the estop
      // never unmounts and its liveness must not follow whatever screen
      // happens to be open. window.PASurface.poll() is for surfaces only.
      window.PageBootstrap?.createBackgroundPoll(
        () =>
          readStatusOnce().then(
            () => true,
            (error) => {
              console.warn("[shell] estop status poll failed:", error);
              return false;
            }
          ),
        { cadenceMs: ESTOP_POLL_MS, refreshOnReturn: true }
      ).start();
    }

    if (window.PABootstrap) {
      window.PABootstrap.registerSection("shell-status", loadInitialStatus, {
        label: "droid status",
      });
    } else {
      loadInitialStatus();
    }
  }

  // ---------------------------------------------------------------------------
  // Mounting a surface
  // ---------------------------------------------------------------------------
  const shellContent = document.getElementById("shell-content");
  const topActions = document.getElementById("shell-top-actions");

  if (!shellContent) {
    // The shell document is the only one that carries a content region, and a
    // build that drops it leaves a frame with nowhere to put a surface. Say so
    // rather than leaving a page that silently shows nothing; the markup suite
    // is what stops this reaching a device.
    console.error("[shell] #shell-content is missing; no surface can be mounted");
    return;
  }

  // Mounting is the bootstrap's Resource Step Recovery doing the work, so
  // without it there is no router -- the same documented degraded path the
  // identity load above already takes.
  if (!window.PABootstrap?.mountResources) return;

  // A surface whose polling stopped while the operator was elsewhere comes
  // back showing what it last read. One node, shown above whichever surface
  // that is, until that surface has answered again -- so a glance cannot take
  // those values for live ones. Uncoloured on purpose: this is a Note, and
  // colour is reserved for refusals and for what the builder can act on
  // (#327, docs/ui-copy-voice.md rule 11).
  const resumedNote = document.createElement("div");
  resumedNote.className = "surface-resumed";
  resumedNote.setAttribute("role", "status");
  resumedNote.textContent =
    "Showing what this screen last read - it stopped asking while you were on another screen, and is asking again now.";

  // Ids are not unique across surfaces -- Firmware and Setup both carry
  // #reboot-button, Dashboard and Setup both carry #reboot-feedback -- so
  // exactly one surface is in the document at a time. A surface's scripts find
  // their own elements by id and nothing else's, which is the same guarantee
  // they had as separate documents.
  const mounted = new Map();

  // The wave of resources for a surface being mounted for the first time is in
  // flight until its scripts have run. A second route change during that window
  // waits: attaching the next surface would put two in the document at once,
  // and detaching the one still loading would run its scripts against DOM that
  // is no longer there, breaking it for the rest of the session.
  let mountInFlight = null;
  let pendingPage = null;

  const surfaceFromHash = () => {
    const raw = String(window.location.hash || "").replace(/^#/, "").trim().toLowerCase();
    return raw ? surfaceFor.get(raw) || null : null;
  };

  const setAddress = (page) => {
    if (window.history?.replaceState) {
      window.history.replaceState(null, "", `#${page}`);
    } else {
      window.location.hash = page;
    }
  };

  const markActive = (page) => {
    document.querySelectorAll("[data-surface-link]").forEach((link) => {
      link.classList.toggle("active", link.dataset.surfaceLink === page);
    });
  };

  const attach = (surface, entry) => {
    currentSurface = surface;
    document.body.dataset.page = surface.page;
    if (window.PASurface?.isStale(surface.page)) shellContent.appendChild(resumedNote);
    shellContent.appendChild(entry.content);
    if (topActions) entry.actionNodes.forEach((node) => topActions.appendChild(node));
    markActive(surface.page);
    applyIdentityName(identityName);
    rememberSurface(surface.page);
  };

  const detach = (entry) => {
    // The nodes are kept, not discarded: a surface returned to paints what it
    // already had, and the handlers its scripts bound are still on these exact
    // elements. What it was polling has already been stopped by the caller --
    // the surface is left before it is taken off the screen.
    entry.content.remove();
    entry.actionNodes.forEach((node) => node.remove());
    resumedNote.remove();
  };

  // Everything in a surface document's <body> except the frame the shell owns.
  // The frame elements stay in each file so it still reads as a page; they are
  // simply not the surface.
  const SHELL_OWNED_IDS = new Set(["shell-top", "shell-status", "shell-content"]);
  const TOPBAR_ACTIONS_ID = "topbar-actions-template";

  const parseSurfaceDocument = (html) => {
    const parsed = new DOMParser().parseFromString(String(html || ""), "text/html");
    const scripts = (parsed.documentElement?.getAttribute("data-scripts") || "")
      .split(",")
      .map((source) => source.trim())
      .filter(Boolean);

    const nodes = [];
    let actionNodes = [];
    Array.from(parsed.body?.children || []).forEach((child) => {
      if (child.id === TOPBAR_ACTIONS_ID) {
        // A surface's topbar actions belong beside the nav, not in the content
        // region. They are moved rather than copied, so the handlers the
        // surface's scripts bind to them survive an unmount. Moving the estop
        // itself onto the shell is #359's.
        actionNodes = Array.from(document.importNode(child.content, true).children);
        return;
      }
      if (SHELL_OWNED_IDS.has(child.id)) return;
      nodes.push(document.importNode(child, true));
    });

    return { scripts, nodes, actionNodes };
  };

  const terminalError = (message) => {
    const error = new Error(message);
    // A document that parsed but carries no surface is not going to get better
    // by being asked for again, so the bootstrap must stop retrying it.
    error.kind = "incompatible";
    error.status = 200;
    return error;
  };

  // Loads a surface's markup as a bootstrap resource, then declares the rest of
  // its wave: its own scripts, read from the document that just arrived rather
  // than restated here, and a final step that hands the session's settled facts
  // to the scripts that have just run.
  const loadSurfaceDocument = (surface, entry) => (done) => {
    const fetchDocument = window.PAApi
      ? window.PAApi.get(surface.doc).then((result) => result.data)
      : fetch(surface.doc).then((response) => response.text());

    fetchDocument
      .then((html) => {
        const { scripts, nodes, actionNodes } = parseSurfaceDocument(html);
        if (nodes.length === 0 || scripts.length === 0) {
          throw terminalError(`${surface.doc} carries no surface`);
        }
        entry.content.replaceChildren(...nodes);
        entry.actionNodes = actionNodes;
        if (topActions && currentSurface === surface) {
          actionNodes.forEach((node) => topActions.appendChild(node));
        }
        window.PABootstrap.mountResources([
          ...scripts,
          {
            name: `shell:handover:${surface.page}`,
            // Settled off the reducer's own stack, the way every other
            // resource settles, so the mount that follows a deferred route
            // change starts from a state the reducer has finished applying.
            load: (handoverDone) => {
              Promise.resolve().then(() => {
                mountInFlight = null;
                replaySessionFacts();
                handoverDone(null);
                applyPendingPage();
              });
            },
          },
        ]);
        done(null);
      })
      .catch((error) => {
        done(error);
        // A retryable failure keeps the wave in flight, which is what the Page
        // Recovery View is reporting. A terminal one is never retried, so the
        // mount is over: release the route, or the operator is stuck on a
        // surface that can never load -- and apply any route change that
        // arrived while it was loading, because the handover step that would
        // otherwise have applied it was never declared.
        if (error?.kind !== "incompatible" && error?.kind !== "device-error") return;
        mountInFlight = null;
        Promise.resolve().then(applyPendingPage);
      });
  };

  const mount = (surface) => {
    if (currentSurface === surface) return;

    // A surface may hold its own unmount open while it asks the operator
    // something -- an unsaved edit, once #289/#299 has one to protect. Nothing
    // registers a hold today; what exists here is the capability. The address
    // already names where the operator was going, so releasing the hold and
    // re-reading it is the whole resume path (see pa:surface-release below).
    if (currentSurface && window.PASurface?.unmountHeld(currentSurface.page)) return;

    // Stop asking before the screen changes, so the surface being left is not
    // still competing for the controller's three-client budget while the one
    // the operator is reading loads. Only what the browser asks for changes on
    // this path: no sequence stops, no output releases, no drive frame is
    // dropped and no latch clears (ADR 0048, #360).
    window.PASurface?.showing(surface.page);

    const previous = currentSurface ? mounted.get(currentSurface.page) : null;
    if (previous) detach(previous);

    const existing = mounted.get(surface.page);
    if (existing) {
      attach(surface, existing);
      return;
    }

    const entry = { content: document.createElement("div"), actionNodes: [] };
    entry.content.className = "surface";
    entry.content.dataset.surface = surface.page;
    mounted.set(surface.page, entry);

    // Attached before its markup arrives on purpose: the scripts behind the
    // markup bind to elements by id, and an element only has an id the document
    // can find while it is in the document.
    attach(surface, entry);

    mountInFlight = surface.page;
    window.PABootstrap.setResourceLabels?.({ [surface.doc]: surface.name });
    window.PABootstrap.mountResources([
      { name: surface.doc, load: loadSurfaceDocument(surface, entry) },
    ]);
  };

  // A route change that arrived while a mount was in flight is applied by
  // re-reading the address rather than by replaying the page that was asked
  // for: by the time the mount finishes the operator may have moved on again,
  // and the address is the one thing that always says where they are.
  const applyPendingPage = () => {
    if (pendingPage === null) return;
    pendingPage = null;
    applyRoute();
  };

  // The address decides what is shown; the remembered surface only answers when
  // the address says nothing. An address this build does not know pulls over to
  // the default rather than erroring -- a link that names a surface should open
  // something, and the one thing it must never do is leave the operator on a
  // screen that says nothing at all.
  const applyRoute = () => {
    const surface = surfaceFromHash() || rememberedSurface();
    if (mountInFlight) {
      pendingPage = surface.page;
      return;
    }
    if (surface !== currentSurface) mount(surface);
    setAddress(surface.page);
  };

  const navigateTo = (page) => {
    if (window.location.hash === `#${page}`) {
      applyRoute();
      return;
    }
    // Setting the hash is the whole navigation: it lands a history entry, so
    // back and forward work, and the hashchange below does the mounting. One
    // path in, whether the operator clicked the nav, followed a legacy link, or
    // typed the address.
    window.location.hash = page;
  };

  window.addEventListener("hashchange", applyRoute);

  // The surface on screen has answered again, so what it is showing is current
  // and the note above it comes down. Keyed on which surface answered: a poll
  // that lands just after the operator left must not clear the note the next
  // surface is wearing.
  window.addEventListener("pa:surface-fresh", (event) => {
    if (event.detail?.surface !== currentSurface?.page) return;
    resumedNote.remove();
  });

  // A surface that was holding its unmount has finished asking. Re-read the
  // address rather than replaying the page that was refused: by then the
  // operator may have moved on again, and the address is the one thing that
  // always says where they are.
  window.addEventListener("pa:surface-release", () => applyRoute());

  // A click on a link to a surface's own document is a route change, not a page
  // load. Capture phase, so a surface's own delegated handler cannot swallow it
  // first; the click still reaches that handler, it just does not reach the
  // browser's navigation. This is what lets the ten .html addresses, and every
  // caller that writes one (window.PAUi.setupActionHtml, servo.js, the
  // disabled-reason lines in six pages), keep working unchanged.
  document.addEventListener(
    "click",
    (event) => {
      if (event.defaultPrevented || event.button !== 0) return;
      if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
      const anchor = event.target?.closest?.("a[href]");
      if (!anchor || (anchor.target && anchor.target !== "_self")) return;
      const surface = surfaceForPath.get(anchor.getAttribute("href"));
      if (!surface) return;
      event.preventDefault();
      navigateTo(surface.page);
    },
    true
  );

  applyRoute();
})();
