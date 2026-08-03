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

  // Render bootstrap state to UI
  function render(newState) {
    state = newState;

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

  // Load a single script with recovery handling
  function loadScript(scriptName) {
    return new Promise((resolve) => {
      const script = document.createElement("script");
      script.src = scriptName;
      script.async = false;

      script.onload = () => {
        resolve({ ok: true, status: 200 });
      };

      script.onerror = () => {
        script.remove();
        resolve({ ok: false, status: 0, error: "Script load failed" });
      };

      document.body.appendChild(script);
    });
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
      } else if (Date.now() >= deadline) {
        // Timeout
        state = window.PageBootstrap.dispatch(state, {
          type: "RESULT",
          outcome: { kind: "no-response" },
        });
        const step = state.resources.find((r) => r.name === scriptName);
        if (step) {
          showNoResponseState(step.attempt, step.nextAt - state.now);
        }
      } else {
        // Network error
        state = window.PageBootstrap.dispatch(state, {
          type: "RESULT",
          outcome: { kind: "no-response" },
        });
        const step = state.resources.find((r) => r.name === scriptName);
        if (step) {
          showNoResponseState(step.attempt, step.nextAt - state.now);
        }
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
