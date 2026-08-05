// =============================================================================
// data/page_bootstrap_host.js
//
// Drives the Common Page Bootstrap reducer against the real browser: owns the
// clock, loads the stylesheet and the page's script chain, runs page-declared
// section loads, renders the Page Recovery View, and gates Live Page Updates.
//
// Replaces page_loader.js on pages that have adopted the bootstrap. It keeps
// that file's contract intact -- one resource at a time, retry a failed load
// rather than abandoning the chain, swap [data-deferred-src] once assets are
// in, and announce readiness on window -- but the retry policy now comes from
// the reducer instead of a second mechanism, and readiness is announced when
// live updates may actually start rather than merely when scripts finished.
//
// See docs/page-load-recovery-architecture.md and ADR 0019.
// =============================================================================
(() => {
  const loader = document.currentScript;
  const scripts = (loader?.dataset?.scripts || "")
    .split(",")
    .map((source) => source.trim())
    .filter(Boolean);

  const TICK_MS = 250;

  const Core = window.PageBootstrap;
  if (!Core) {
    // page_bootstrap.js is a hard prerequisite and is loaded ahead of this
    // file by the page itself. Failing loudly beats a page that silently
    // never loads anything.
    throw new Error("[page-bootstrap] page_bootstrap.js must load before page_bootstrap_host.js");
  }

  // The stylesheet is deliberately NOT part of this chain. Pages carry a
  // render-blocking <link> in <head>, so the browser fetches it once and
  // natively; page_loader.js fetched it a second time and, because the chain
  // waited on it, a stylesheet failure could stall everything behind it. Here
  // a failed stylesheet costs styling only -- scripts and recovery still run.
  let state = Core.createBootstrap({ resources: scripts, sections: [] });
  const sectionLoaders = new Map();
  let assetsAnnounced = false;

  // ---------------------------------------------------------------------------
  // Resource loading
  // ---------------------------------------------------------------------------
  const loadScript = (src, done) => {
    const script = document.createElement("script");
    script.src = src;
    // Preserve execution order: the chain is sequential by design, and async
    // would let a later script run against a not-yet-defined earlier global.
    script.async = false;
    script.onload = () => done(null);
    script.onerror = () => {
      script.remove();
      done({ kind: "network" });
    };
    document.body.appendChild(script);
  };

  const runSection = (name, done) => {
    const load = sectionLoaders.get(name);
    if (!load) {
      done({ kind: "http", status: 501 });
      return;
    }
    Promise.resolve()
      .then(() => load())
      .then(() => done(null))
      .catch((error) => done(error));
  };

  // ---------------------------------------------------------------------------
  // Driving the reducer
  //
  // The reducer decides what should run; this only notices when its `active`
  // slot changes and starts the corresponding real work exactly once.
  // ---------------------------------------------------------------------------
  let startedActiveId = null;

  const settle = (id, error, retryAfterMs) => {
    // A result arriving after its deadline already expired belongs to a
    // request the reducer has moved on from; dropping it keeps the reducer's
    // attempt accounting honest.
    if (!state.active || state.active.id !== id) return;
    const outcome = error
      ? Core.classifyOutcome(error, retryAfterMs ?? error?.retryAfterMs ?? null)
      : { kind: "success" };
    apply(state.active.kind === "command" ? { type: "COMMAND_RESULT", outcome } : { type: "RESULT", outcome });
  };

  const syncActive = () => {
    const active = state.active;
    if (!active || active.id === startedActiveId) return;
    startedActiveId = active.id;
    const id = active.id;

    if (active.kind === "resource") {
      loadScript(active.name, (error) => settle(id, error));
    } else if (active.kind === "section") {
      runSection(active.name, (error) => settle(id, error));
    }
    // Commands are dispatched by page code, which settles them itself.
  };

  const announceAssetsOnce = () => {
    if (assetsAnnounced || !state.liveUpdatesStarted) return;
    assetsAnnounced = true;

    document.querySelectorAll("[data-deferred-src]").forEach((element) => {
      element.src = element.dataset.deferredSrc;
      delete element.dataset.deferredSrc;
    });

    // Page Startup Order: this is the signal status_stream.js waits on before
    // opening /api/events, so it fires once the page is genuinely ready for
    // live updates -- not merely once its scripts finished downloading.
    window.PAAssetsReady = true;
    window.dispatchEvent(new Event("pa:assets-ready"));
  };

  const render = () => {
    window.PARecoveryView?.render(state, {
      onRetryNow: (name) => apply({ type: "RETRY_NOW", name }),
    });
  };

  // Pages gate controls on whether the data behind them actually loaded, so
  // they need to know when a section's status changes -- but not on every
  // clock tick, which would fire several times a second for no new fact.
  let lastSectionSignature = null;
  const publishSectionChange = () => {
    const signature = state.sections.map((s) => `${s.name}:${s.status}`).join(",");
    if (signature === lastSectionSignature) return;
    lastSectionSignature = signature;
    window.dispatchEvent(
      new CustomEvent("pa:bootstrap-change", {
        detail: {
          sections: state.sections.map((s) => ({ name: s.name, status: s.status })),
          resourcesReady: state.resourcesReady,
          sectionsStable: state.sectionsStable,
        },
      })
    );
  };

  const apply = (action) => {
    state = Core.dispatch(state, action);
    syncActive();
    announceAssetsOnce();
    render();
    publishSectionChange();
  };

  // ---------------------------------------------------------------------------
  // Clock and visibility
  // ---------------------------------------------------------------------------
  let lastTickAt = Date.now();
  const tick = () => {
    const now = Date.now();
    const dt = now - lastTickAt;
    lastTickAt = now;
    apply({ type: "TICK", dt });
  };

  document.addEventListener("visibilitychange", () => {
    // Resync the clock on return so a long hidden stretch does not land as one
    // enormous dt that instantly expires every pending deadline.
    lastTickAt = Date.now();
    apply({ type: "VISIBILITY", visible: !document.hidden });
  });

  // ---------------------------------------------------------------------------
  // Page-facing API
  // ---------------------------------------------------------------------------
  window.PABootstrap = {
    // Page scripts call this as they execute, which is during resource
    // loading -- before any section work is allowed to start.
    registerSection(name, load, { label = null } = {}) {
      sectionLoaders.set(name, load);
      if (label) window.PARecoveryView?.setLabels({ [name]: label });
      apply({ type: "DECLARE_SECTIONS", names: [name] });
    },
    setResourceLabels(entries) {
      window.PARecoveryView?.setLabels(entries);
    },
    submitCommand(name, run) {
      apply({ type: "SUBMIT_COMMAND", name });
      const id = state.active && state.active.name === name ? state.active.id : null;
      Promise.resolve()
        .then(() => run())
        .then(() => settle(id ?? state.active?.id, null))
        .catch((error) => settle(id ?? state.active?.id, error));
    },
    retryNow(name) {
      apply({ type: "RETRY_NOW", name });
    },
    getState() {
      return state;
    },
  };

  const start = () => {
    lastTickAt = Date.now();
    window.setInterval(tick, TICK_MS);
    apply({ type: "TICK", dt: 0 });
  };

  if (document.readyState === "complete") {
    start();
  } else {
    window.addEventListener("load", start, { once: true });
  }
})();
