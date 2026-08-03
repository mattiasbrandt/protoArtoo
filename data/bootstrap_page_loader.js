// =============================================================================
// data/bootstrap_page_loader.js
//
// Bootstrap-aware page loader: uses Page Bootstrap to manage script loading
// with recovery UI (ADRs 0016-0019, issue #52).
//
// Loads scripts in declared order with recovery modal for failures, timeouts,
// and server-side refusals (503 Busy Recovery Page). Integrates with wifi.js
// (and other pages) to render bootstrap state transitions as UI.
// =============================================================================

(() => {
  if (!window.PageBootstrap) {
    console.error("[bootstrap-loader] PageBootstrap not available");
    return;
  }

  const loader = document.currentScript;
  const sources = (loader?.dataset?.scripts || "")
    .split(",")
    .map((source) => source.trim())
    .filter(Boolean);
  const sections = (loader?.dataset?.sections || "")
    .split(",")
    .map((sec) => sec.trim())
    .filter(Boolean);

  const POLL_INTERVAL_MS = 100; // Tick interval for countdown timers
  const recoveryBackdrop = document.getElementById("recovery-backdrop");
  const recoveryModals = {
    loading: document.getElementById("recovery-loading"),
    busy: document.getElementById("recovery-busy"),
    noResponse: document.getElementById("recovery-no-response"),
    retrying: document.getElementById("recovery-retrying"),
  };

  let state = window.PageBootstrap.createBootstrap({
    resources: sources,
    sections: sections,
  });

  let pollInterval = null;
  let countdownTimers = {}; // Per-modal countdown state

  // Which resource/section is currently shown as busy/no-response/retrying,
  // and which outcome put it there. The bootstrap reducer marks a failed
  // step "failed-retrying" regardless of whether the cause was an explicit
  // busy refusal or a timeout -- the loader (not the reducer) is the only
  // place that knows which, since it's the caller of loadScript(). render()
  // is polled every POLL_INTERVAL_MS by the ticker; without this it would
  // unconditionally reset back to the generic loading modal on every tick,
  // undoing showBusyState()/showNoResponseState() a fraction of a second
  // after they ran.
  let pendingRecovery = null; // { name, kind: "busy" | "no-response" | "retrying" }

  // Render bootstrap state to UI
  function render(newState) {
    state = newState;

    if (pendingRecovery) {
      const step = [...state.resources, ...state.sections].find(
        (s) => s.name === pendingRecovery.name
      );
      if (step && step.status === "failed-retrying") {
        // Still waiting on this step's retry -- keep the modal that was
        // already shown for it, don't let the poller stomp on it.
        return;
      }
      // The step moved on (retry attempt started, or it succeeded) --
      // resume normal rendering below.
      pendingRecovery = null;
    }

    // Hide all modals, show backdrop only if recovering
    Object.values(recoveryModals).forEach((m) => (m.style.display = "none"));

    // Show appropriate modal based on state
    if (!state.resourcesReady) {
      // Still loading resources
      recoveryModals.loading.style.display = "block";
      recoveryBackdrop.style.display = "flex";

      // Update current step name
      if (state.active && state.active.kind === "resource") {
        const stepEl = document.getElementById("recovery-current-step");
        if (stepEl) {
          stepEl.textContent = `Loading: ${state.active.name}`;
        }
      }
      return;
    }

    if (!state.sectionsStable) {
      // Loading sections
      recoveryModals.loading.style.display = "block";
      recoveryBackdrop.style.display = "flex";

      if (state.active && state.active.kind === "section") {
        const stepEl = document.getElementById("recovery-current-step");
        if (stepEl) {
          stepEl.textContent = `Loading: ${state.active.name} data`;
        }
      }
      return;
    }

    // All resources and sections loaded
    recoveryBackdrop.style.display = "none";
    if (window.PABootstrapWifi && window.PABootstrapWifi.onRecoveryDone) {
      window.PABootstrapWifi.onRecoveryDone();
    }
  }

  // Show busy (503) recovery page state
  function showBusyState(retryAfterMs) {
    recoveryModals.busy.style.display = "block";
    recoveryBackdrop.style.display = "flex";

    const el = document.getElementById("recovery-busy-countdown");
    if (el) {
      let remaining = Math.ceil(retryAfterMs / 1000);
      el.textContent = remaining + " s";
      if (countdownTimers.busy) clearInterval(countdownTimers.busy);
      countdownTimers.busy = setInterval(() => {
        remaining--;
        if (remaining < 0) remaining = Math.ceil(retryAfterMs / 1000);
        el.textContent = remaining + " s";
      }, 1000);
    }
  }

  // Show no-response (timeout) recovery page state
  function showNoResponseState(attempt, nextRetryMs) {
    recoveryModals.noResponse.style.display = "block";
    recoveryBackdrop.style.display = "flex";

    const el = document.getElementById("recovery-no-response-countdown");
    const attemptEl = document.getElementById("recovery-no-response-attempt");
    if (el) {
      let remaining = Math.ceil(nextRetryMs / 1000);
      el.textContent = remaining + " s";
      if (attemptEl) attemptEl.textContent = `Attempt ${attempt}`;
      if (countdownTimers.noResponse) clearInterval(countdownTimers.noResponse);
      countdownTimers.noResponse = setInterval(() => {
        remaining--;
        if (remaining < 0) remaining = Math.ceil(nextRetryMs / 1000);
        el.textContent = remaining + " s";
      }, 1000);
    }
  }

  // Show retrying-with-backoff state
  function showRetryingState(attempt, nextRetryMs) {
    recoveryModals.retrying.style.display = "block";
    recoveryBackdrop.style.display = "flex";

    const el = document.getElementById("recovery-retrying-countdown");
    const attemptEl = document.getElementById("recovery-retrying-attempt");
    if (el) {
      let remaining = Math.ceil(nextRetryMs / 1000);
      el.textContent = remaining + " s";
      if (attemptEl) attemptEl.textContent = `Attempt ${attempt} (backoff)`;
      if (countdownTimers.retrying) clearInterval(countdownTimers.retrying);
      countdownTimers.retrying = setInterval(() => {
        remaining--;
        if (remaining < 0) remaining = Math.ceil(nextRetryMs / 1000);
        el.textContent = remaining + " s";
      }, 1000);
    }
  }

  // Show the "no-response" outcome for a resource/section step: state 3
  // (no-response, first attempt) for its first failure, state 4 (retrying
  // with growing backoff) once step.attempt shows this isn't the first try.
  function dispatchNoResponse(name, listKey) {
    const step = state[listKey].find((s) => s.name === name);
    if (!step) return;
    const nextRetryMs = step.nextAt - state.now;
    if (step.attempt > 1) {
      pendingRecovery = { name, kind: "retrying" };
      showRetryingState(step.attempt, nextRetryMs);
    } else {
      pendingRecovery = { name, kind: "no-response" };
      showNoResponseState(step.attempt, nextRetryMs);
    }
  }

  // Load a single script with recovery handling.
  //
  // A plain <script src> tag cannot distinguish an ADR 0016 Busy Recovery
  // Page (503 + Retry-After) from a generic network failure -- both just
  // fire onerror. Fetch the resource first so the response status and
  // Retry-After header are visible, then execute it as a script only once
  // the response is known-good.
  function loadScript(scriptName) {
    return fetch(scriptName, { cache: "no-store" })
      .then((response) => {
        if (response.status === 503) {
          const retryAfterHeader = response.headers.get("Retry-After");
          const retryAfterMs = retryAfterHeader
            ? Number(retryAfterHeader) * 1000
            : null;
          return { ok: false, busy: true, retryAfterMs, status: 503 };
        }

        if (!response.ok) {
          return { ok: false, busy: false, status: response.status };
        }

        return response.text().then((source) => {
          const script = document.createElement("script");
          script.text = source;
          script.async = false;
          document.body.appendChild(script);
          return { ok: true, status: response.status };
        });
      })
      .catch(() => ({ ok: false, busy: false, status: 0, error: "Script load failed" }));
  }

  // Main load loop using bootstrap
  async function loadResources() {
    // Start polling for ticks
    pollInterval = setInterval(() => {
      state = window.PageBootstrap.dispatch(state, { type: "TICK", dt: POLL_INTERVAL_MS });
      render(state);
    }, POLL_INTERVAL_MS);

    // Load each script
    for (const scriptName of sources) {
      // Wait for work to be dispatchable
      while (!state.active || state.active.kind !== "resource" || state.active.name !== scriptName) {
        await new Promise((resolve) => setTimeout(resolve, 10));
      }

      // Try loading with timeout
      const start = Date.now();
      const deadline = start + window.PageBootstrap.OPERATION_DEADLINE_MS;

      let result = null;
      while (!result && Date.now() < deadline) {
        result = await Promise.race([
          loadScript(scriptName),
          new Promise((resolve) => setTimeout(() => resolve(null), 100)),
        ]);
      }

      // Dispatch result
      if (result && result.ok) {
        state = window.PageBootstrap.dispatch(state, {
          type: "RESULT",
          outcome: { kind: "success" },
        });
      } else if (result && result.busy) {
        // Explicit ADR 0016 Busy Recovery Page (503 + Retry-After) --
        // Story #8: "Controller busy" only after an explicit refusal.
        const retryAfterMs = result.retryAfterMs ?? window.PageBootstrap.DEFAULT_BUSY_RETRY_MS;
        state = window.PageBootstrap.dispatch(state, {
          type: "RESULT",
          outcome: { kind: "busy", retryAfterMs },
        });
        pendingRecovery = { name: scriptName, kind: "busy" };
        showBusyState(retryAfterMs);
      } else if (Date.now() >= deadline) {
        // Timeout -- Story #9: "No response from controller".
        state = window.PageBootstrap.dispatch(state, {
          type: "RESULT",
          outcome: { kind: "no-response" },
        });
        dispatchNoResponse(scriptName, "resources");
      } else {
        // Network error -- also reported as "No response from controller"
        // per Story #9 (the UI does not distinguish network failure from
        // timeout; both are honest "no response" outcomes).
        state = window.PageBootstrap.dispatch(state, {
          type: "RESULT",
          outcome: { kind: "no-response" },
        });
        dispatchNoResponse(scriptName, "resources");
      }

      render(state);
    }

    // Load deferred assets
    document.querySelectorAll("[data-deferred-src]").forEach((el) => {
      el.src = el.dataset.deferredSrc;
      delete el.dataset.deferredSrc;
    });

    // Sections load in parallel (simulate with immediate success)
    for (const section of sections) {
      state = window.PageBootstrap.dispatch(state, { type: "TICK", dt: 0 });
      if (state.active && state.active.kind === "section" && state.active.name === section) {
        state = window.PageBootstrap.dispatch(state, {
          type: "RESULT",
          outcome: { kind: "success" },
        });
      }
      render(state);
    }

    // Cleanup
    if (pollInterval) clearInterval(pollInterval);

    // Signal ready
    window.PAAssetsReady = true;
    window.dispatchEvent(new Event("pa:assets-ready"));
  }

  // Public API for retry actions
  window.PABootstrapWifi = {
    retryNow: () => {
      if (state.active && (state.active.kind === "resource" || state.active.kind === "section")) {
        state = window.PageBootstrap.dispatch(state, {
          type: "RETRY_NOW",
          name: state.active.name,
        });
        render(state);
      }
    },

    onRecoveryDone: null, // Callback when resources+sections done
  };

  // Visibility handling for Hidden Tab Pause
  document.addEventListener("visibilitychange", () => {
    state = window.PageBootstrap.dispatch(state, {
      type: "VISIBILITY",
      visible: !document.hidden,
    });
    render(state);
  });

  // Start loading
  const start = () => {
    loadResources().catch((err) => {
      console.error("[bootstrap-loader] Error:", err);
    });
  };

  if (document.readyState === "complete") {
    start();
  } else {
    window.addEventListener("load", start, { once: true });
  }
})();
