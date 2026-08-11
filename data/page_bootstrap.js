// =============================================================================
// data/page_bootstrap.js
//
// The Common Page Bootstrap: page-load recovery for every controller page.
// See docs/page-load-recovery-architecture.md, ADR 0016 and ADR 0019.
//
// THIS IS DELIBERATELY ONE FILE. The three parts below (state model, recovery
// view, browser host) are one cohesive subsystem, and splitting them across
// three <script src> tags made the page open three extra connections in the
// initial burst -- the exact concurrent pattern this bootstrap exists to
// prevent. Measured on the controller: all three were reset by the accept
// guard during an ordinary page load, so the bootstrap never initialized.
// One request keeps the page's opening burst no larger than it was before
// the bootstrap existed.
// =============================================================================

// ============================ PART 1: state model ============================
// =============================================================================
(() => {

  // Recovery Retry Interval: the server's fixed Retry-After for a busy
  // refusal (ADR 0016). Used only when the response carried no usable header.
  const DEFAULT_BUSY_RETRY_MS = 5000;

  // A no-response outcome has no server-given interval to honor, so it uses
  // growing pauses instead of a fixed one.
  const NO_RESPONSE_BASE_BACKOFF_MS = 2000;
  const NO_RESPONSE_MAX_BACKOFF_MS = 30000;

  // Operation Deadline categories: exactly two, per ADR 0019.
  //
  // Ordinary matches web_api.js's DEFAULT_TIMEOUT_MS so the client-side
  // deadline and the request timeout describe the same boundary rather than
  // fighting each other. Catalog is the one known-longer operation; a step
  // carrying it is flagged so the view can say the wait is expected instead of
  // letting it read as a frozen page.
  const OPERATION_DEADLINE_MS = 6000;
  const CATALOG_DEADLINE_MS = 12000;

  const backoffFor = (attempt) => {
    const ms = NO_RESPONSE_BASE_BACKOFF_MS * Math.pow(2, Math.max(0, attempt - 1));
    return Math.min(ms, NO_RESPONSE_MAX_BACKOFF_MS);
  };

  // ---------------------------------------------------------------------------
  // Outcome classification
  //
  // Retry policy has exactly two failure shapes: 'busy' (server said so, honor
  // its interval) and 'no-response' (nothing usable came back, back off). The
  // originating ApiError kind/status is carried through as `reason` purely so
  // the view can say something specific; it never changes a transition.
  // ---------------------------------------------------------------------------
  const classifyOutcome = (error, retryAfterMs = null) => {
    if (!error) return { kind: "success" };

    const kind = error.kind || "unknown";
    const status = error.status || 0;

    if (kind === "http" && status === 503) {
      return {
        kind: "busy",
        reason: "busy",
        retryAfterMs: retryAfterMs ?? error.retryAfterMs ?? DEFAULT_BUSY_RETRY_MS,
      };
    }

    // timeout and network are the honest no-response cases. Other http
    // statuses and malformed JSON are also "no usable result", so they follow
    // the same backoff path -- but keep their own reason so the view can
    // distinguish "device rejected it" from "nothing came back".
    return { kind: "no-response", reason: kind, status };
  };

  // ---------------------------------------------------------------------------
  // State construction
  // ---------------------------------------------------------------------------
  const makeStep = (name) => ({
    name,
    status: "pending",
    attempt: 0,
    nextAt: null,
    reason: null,
  });

  const createBootstrap = ({ resources = [], sections = [], deadlines = {} } = {}) =>
    // Settle the derived flags immediately, so a page declaring no sections
    // (or nothing at all) is already stable rather than waiting for a result
    // that will never arrive.
    recomputeSectionsStable(baseBootstrap(resources, sections, deadlines));

  const baseBootstrap = (resources, sections, deadlines) => ({
    now: 0,
    visible: true,
    nextId: 1,

    // Per-step Operation Deadline overrides, keyed by step name. Anything
    // absent uses the Ordinary category.
    deadlines: { ...deadlines },

    // Resource Step Recovery: resources load one at a time in declared order
    // through a single cursor. A failure pauses the cursor and retries only
    // that step; completed steps are never redone.
    resources: resources.map(makeStep),
    resourceCursor: 0,
    resourcesReady: resources.length === 0,

    // Section Recovery: once resources are ready, sections are independent --
    // one section's failure neither blocks nor resets another.
    sections: sections.map(makeStep),
    sectionsStable: false,

    // Page Startup Order: live updates start only once resources are ready
    // AND sections are stable.
    liveUpdatesStarted: false,

    active: null,
  });

  // ---------------------------------------------------------------------------
  // Structural helpers
  //
  // Every helper returns new objects rather than mutating, so a host can hold
  // a previous state for comparison and rendering diffs stay honest.
  // ---------------------------------------------------------------------------
  const listKey = (kind) => (kind === "resource" ? "resources" : "sections");

  const replaceStep = (state, kind, name, changes) => {
    const key = listKey(kind);
    return {
      ...state,
      [key]: state[key].map((step) => (step.name === name ? { ...step, ...changes } : step)),
    };
  };

  const findStep = (state, kind, name) => state[listKey(kind)].find((step) => step.name === name);

  const takeId = (state) => [state.nextId, { ...state, nextId: state.nextId + 1 }];

  // ---------------------------------------------------------------------------
  // Scheduling
  // ---------------------------------------------------------------------------
  const dispatchableWork = (state) => {
    // Resource cursor while resources are still loading, then the next due section.
    if (!state.resourcesReady) {
      const step = state.resources[state.resourceCursor];
      const due =
        step &&
        (step.status === "pending" ||
          (step.status === "failed-retrying" && step.nextAt <= state.now));
      // If the cursor step is loading, or failed and not yet due, nothing else
      // may start -- that is what makes resource loading strictly ordered.
      return due ? { kind: "resource", name: step.name } : null;
    }

    const dueSection = state.sections.find(
      (s) => s.status === "pending" || (s.status === "failed-retrying" && s.nextAt <= state.now)
    );
    return dueSection
      ? { kind: "section", name: dueSection.name }
      : null;
  };

  const startWork = (state, work) => {
    let next = state;
    let id = work.id;
    if (!id) {
      [id, next] = takeId(next);
    }

    const step = findStep(next, work.kind, work.name);
    next = replaceStep(next, work.kind, work.name, {
      status: "loading",
      attempt: step.attempt + 1,
    });

    const deadlineMs = next.deadlines[work.name] ?? OPERATION_DEADLINE_MS;
    return {
      ...next,
      active: {
        id,
        kind: work.kind,
        name: work.name,
        startedAt: next.now,
        deadlineMs,
        // Flagged so the view can explain an expected-longer wait rather than
        // leaving it looking frozen.
        longRunning: deadlineMs > OPERATION_DEADLINE_MS,
        deadlineAt: next.now + deadlineMs,
      },
    };
  };

  // Start the next piece of work immediately rather than waiting for the next
  // tick, so a fast succession of results does not look stalled.
  const pump = (state) => {
    if (!state.visible || state.active) return state;
    const work = dispatchableWork(state);
    return work ? startWork(state, work) : state;
  };

  const applyOutcome = (state, active, outcome) => {
    if (outcome.kind === "success") {
      let next = replaceStep(state, active.kind, active.name, {
        status: "done",
        nextAt: null,
        reason: null,
      });
      if (active.kind === "resource") {
        const cursor = next.resourceCursor + 1;
        next = {
          ...next,
          resourceCursor: cursor,
          resourcesReady: cursor >= next.resources.length,
        };
      }
      return next;
    }

    const step = findStep(state, active.kind, active.name);
    const retryDelay =
      outcome.kind === "busy"
        ? outcome.retryAfterMs ?? DEFAULT_BUSY_RETRY_MS
        : backoffFor(step.attempt);

    return replaceStep(state, active.kind, active.name, {
      status: "failed-retrying",
      nextAt: state.now + retryDelay,
      reason: outcome.reason || outcome.kind,
    });
  };

  // Sections are "stable" when every one is either done or visibly waiting to
  // retry -- not when all have succeeded. A page with one permanently failing
  // section must still start live updates.
  const recomputeSectionsStable = (state) => {
    const stable = state.sections.every(
      (s) => s.status === "done" || s.status === "failed-retrying"
    );
    return {
      ...state,
      sectionsStable: stable,
      liveUpdatesStarted:
        state.liveUpdatesStarted || (stable && state.resourcesReady),
    };
  };

  const settleActive = (state, outcome) => {
    const active = state.active;
    let next = applyOutcome({ ...state, active: null }, active, outcome);
    next = recomputeSectionsStable(next);
    return pump(next);
  };

  // ---------------------------------------------------------------------------
  // Reducer
  // ---------------------------------------------------------------------------
  const dispatch = (prev, action) => {
    switch (action.type) {
      case "TICK": {
        let state = { ...prev, now: prev.now + action.dt };

        // Operation Deadline: an active request past its deadline with no
        // result is a no-response outcome. This is the same boundary the wire
        // contract draws between busy and no-response.
        if (state.active && state.now >= state.active.deadlineAt) {
          state = settleActive(state, { kind: "no-response", reason: "timeout" });
          return state;
        }

        // Hidden Tab Pause: while hidden no NEW work starts, but the single
        // in-flight request is left to finish within its deadline rather than
        // being aborted just because visibility changed.
        return pump(state);
      }

      case "RESULT": {
        if (!prev.active) return prev; // nothing in flight to resolve
        return settleActive(prev, action.outcome);
      }

      case "DECLARE_SECTIONS": {
        // Page scripts declare their own sections as they execute, which is
        // during resource loading -- before any section work may start. Only
        // additive, and only while no section has run yet, so this can never
        // discard in-progress or completed section state.
        const known = new Set(prev.sections.map((s) => s.name));
        const added = action.names.filter((name) => !known.has(name)).map(makeStep);
        if (added.length === 0) return prev;
        if (prev.sections.some((s) => s.status !== "pending")) return prev;
        // recomputeSectionsStable re-derives the flag from the new list, so a
        // page that was momentarily stable with nothing to do becomes unstable
        // again as soon as it declares real work.
        return recomputeSectionsStable({
          ...prev,
          sections: [...prev.sections, ...added],
          deadlines: { ...prev.deadlines, ...(action.deadlines || {}) },
        });
      }

      case "REFRESH_SECTIONS": {
        // An explicit operator refresh re-runs section work through the same
        // single slot as everything else. Resources are untouched -- they are
        // already loaded, and re-fetching them is exactly the duplicate work
        // Resource Step Recovery exists to avoid.
        //
        // The step `active` points at stays as it is: resetting it to pending
        // while its request is still in flight would leave deriveView with no
        // loading step to report and hide the recovery panel mid-request. Its
        // in-flight response is as fresh as a re-issued one, and it settles
        // through the single slot like everything else.
        const names = action.names ? new Set(action.names) : null;
        const inFlight = prev.active?.kind === "section" ? prev.active.name : null;
        const refreshed = prev.sections.map((step) =>
          (!names || names.has(step.name)) && step.name !== inFlight
            ? { ...step, status: "pending", attempt: 0, nextAt: null, reason: null }
            : step
        );
        return pump(recomputeSectionsStable({ ...prev, sections: refreshed }));
      }

      case "RETRY_NOW": {
        // Operator-facing "Retry now": pull the named waiting step forward
        // regardless of its scheduled time.
        let state = prev;
        for (const kind of ["resource", "section"]) {
          const step = findStep(state, kind, action.name);
          if (step && step.status === "failed-retrying") {
            state = replaceStep(state, kind, action.name, { nextAt: state.now });
          }
        }
        return pump(state);
      }

      case "VISIBILITY": {
        return pump({ ...prev, visible: action.visible });
      }

      default:
        return prev;
    }
  };

  // ---------------------------------------------------------------------------
  // Background Poll: ongoing polling with cadence, backoff, and visibility pause
  // ---------------------------------------------------------------------------
  const createBackgroundPoll = (attempt, {
    cadenceMs = 0,
    skipWhen = () => false,
    runOnStart = false,
    refreshOnReturn = false,
    retry = null,
  } = {}) => {
    let intervalId = null;
    let retryTimeoutId = null;
    let retryAttempt = 0;
    let visibilityListener = null;
    let inFlight = false;

    const runAttempt = async () => {
      if (inFlight) return;
      inFlight = true;
      try {
        const result = await attempt();
        // A falsy result means retry if config exists; truthy means success
        if (!result && retry && retryAttempt < retry.maxAttempts - 1) {
          retryAttempt += 1;
          const delayMs = retry.baseMs * Math.pow(retry.factor, retryAttempt - 1);
          retryTimeoutId = window.setTimeout(() => {
            retryTimeoutId = null;
            runAttempt();
          }, delayMs);
        } else {
          // Success or max attempts reached: stop retrying
          retryAttempt = 0;
        }
      } finally {
        inFlight = false;
      }
    };

    const handleVisibilityChange = () => {
      // Trigger refresh only when becoming visible, if requested
      if (document.visibilityState !== "hidden" && refreshOnReturn) {
        runAttempt();
      }
    };

    const start = () => {
      if (runOnStart) {
        runAttempt();
      }

      if (cadenceMs > 0) {
        intervalId = window.setInterval(() => {
          // Check visibility on each tick (not cached) to handle direct property changes
          if (document.visibilityState !== "hidden" && !skipWhen()) {
            runAttempt();
          }
        }, cadenceMs);
      }

      if (refreshOnReturn || cadenceMs > 0) {
        visibilityListener = handleVisibilityChange;
        document.addEventListener("visibilitychange", visibilityListener);
      }
    };

    const cancelRetry = () => {
      if (retryTimeoutId !== null) {
        window.clearTimeout(retryTimeoutId);
        retryTimeoutId = null;
        retryAttempt = 0;
      }
    };

    const stop = () => {
      if (intervalId !== null) {
        window.clearInterval(intervalId);
        intervalId = null;
      }
      cancelRetry();
      if (visibilityListener) {
        document.removeEventListener("visibilitychange", visibilityListener);
        visibilityListener = null;
      }
    };

    return { start, cancelRetry, stop };
  };

  window.PageBootstrap = {
    DEFAULT_BUSY_RETRY_MS,
    OPERATION_DEADLINE_MS,
    CATALOG_DEADLINE_MS,
    createBootstrap,
    classifyOutcome,
    dispatch,
    createBackgroundPoll,
  };
})();

// =========================== PART 2: recovery view ===========================
// =============================================================================
// data/recovery_view.js
//
// Page Recovery View: renders the Common Page Bootstrap's state as the
// operator-facing panel, so a page that is still loading or waiting to retry
// says so instead of sitting silently. Four states, per the approved design:
//
//   loading      required resources still arriving, nothing has failed
//   busy         the controller refused the request; honor its Retry-After
//   no-response  nothing came back on the first attempt
//   retrying     still nothing back, intervals are growing
//
// Reads bootstrap state and writes DOM. It derives everything it shows from
// that state -- it holds no recovery state of its own, so what the operator
// sees can never drift from what the bootstrap is actually doing.
// See ADR 0016 and docs/page-load-recovery-architecture.md.
// =============================================================================
(() => {
  const BACKDROP_ID = "page-recovery-backdrop";

  // Beyond this attempt count the wait is long enough that the operator needs
  // to be told the intervals are growing, not just that a retry is pending.
  const BACKOFF_VISIBLE_AFTER_ATTEMPT = 1;

  // Plain-language names for what the page is waiting on. Recovery copy must
  // not leak internals, so a raw path or section id is never shown -- an
  // unlabelled step falls back to a generic phrase rather than its filename.
  const labels = new Map();
  const GENERIC_LABEL = { resource: "page files", section: "page data" };

  const labelFor = (name, kind) => labels.get(name) || GENERIC_LABEL[kind] || "page data";

  const REASON_DETAIL = {
    timeout: "Connection timed out. Attempting to reconnect.",
    network: "Connection to the controller was lost. Attempting to reconnect.",
    http: "The controller rejected the request. Retrying.",
    "bad-json": "The controller sent an incomplete reply. Retrying.",
  };

  // ---------------------------------------------------------------------------
  // Deriving what to show
  // ---------------------------------------------------------------------------

  // The step the operator cares about: whatever is blocking progress right
  // now. A waiting resource outranks a waiting section, because nothing else
  // can proceed until required resources land.
  const blockingStep = (state) => {
    if (!state.resourcesReady) {
      const step = state.resources[state.resourceCursor];
      if (step) return { step, kind: "resource" };
    }
    const waiting = state.sections.find((s) => s.status === "failed-retrying");
    if (waiting) return { step: waiting, kind: "section" };
    const loading = state.sections.find((s) => s.status === "loading");
    if (loading) return { step: loading, kind: "section" };
    return null;
  };

  const deriveView = (state) => {
    // Once required resources are in and every section has settled, the page
    // is usable -- get out of the operator's way even if a section is still
    // retrying in the background.
    if (state.resourcesReady && state.sectionsStable) return { visible: false };

    const blocking = blockingStep(state);
    if (!blocking) return { visible: false };

    const { step, kind } = blocking;

    if (step.status !== "failed-retrying") {
      return {
        visible: true,
        mode: "loading",
        stepName: step.name,
        stepLabel: labelFor(step.name, kind),
        kind,
        // A step on the longer Operation Deadline needs to say so, or an
        // expected wait reads as a frozen page.
        longRunning: state.active?.name === step.name && state.active.longRunning === true,
      };
    }

    const waitMs = Math.max(0, (step.nextAt ?? state.now) - state.now);
    const mode =
      step.reason === "busy"
        ? "busy"
        : step.attempt > BACKOFF_VISIBLE_AFTER_ATTEMPT
          ? "retrying"
          : "no-response";

    return {
      visible: true,
      mode,
      stepName: step.name,
      stepLabel: labelFor(step.name, kind),
      kind,
      attempt: step.attempt,
      reason: step.reason,
      waitMs,
      // Round up so a 4.2s wait reads "5 s" and reaches "1 s" before firing,
      // rather than sitting on "0 s" while nothing visibly happens.
      waitSeconds: Math.ceil(waitMs / 1000),
    };
  };

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  const el = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  const countdownPanel = (label, seconds, sublabel) => {
    const panel = el("div", "recovery-countdown-panel");
    if (label) panel.appendChild(el("div", "recovery-countdown-label", label));
    panel.appendChild(el("div", "recovery-countdown-value", `${seconds} s`));
    if (sublabel) panel.appendChild(el("div", "recovery-countdown-label", sublabel));
    return panel;
  };

  const retryButton = (onRetryNow, stepName) => {
    const actions = el("div", "recovery-actions");
    const button = el("button", "btn accent", "Retry now");
    button.type = "button";
    button.addEventListener("click", () => onRetryNow(stepName));
    actions.appendChild(button);
    return actions;
  };

  const buildPanel = (view, onRetryNow) => {
    const panel = el("div", "recovery-panel");

    if (view.mode === "busy") {
      const banner = el("div", "recovery-refused-banner");
      banner.appendChild(el("span", "indicator warn"));
      banner.appendChild(el("span", null, "REQUEST REFUSED"));
      panel.appendChild(banner);
      panel.appendChild(el("div", "recovery-status-reason", "Controller busy"));
      panel.appendChild(
        el(
          "p",
          "recovery-message",
          "Controller is handling other requests. Try again in a moment."
        )
      );
      panel.appendChild(countdownPanel("Retry interval", view.waitSeconds));
      panel.appendChild(retryButton(onRetryNow, view.stepName));
      return panel;
    }

    const header = el("div", "recovery-header");
    const indicatorClass =
      view.mode === "loading" ? "indicator info" : view.mode === "retrying" ? "indicator fail" : "indicator warn";
    header.appendChild(el("span", indicatorClass));

    const headerText = el("div");
    if (view.mode === "loading") {
      headerText.appendChild(el("div", "recovery-status-reason", "Loading page resources"));
      headerText.appendChild(
        el("div", "recovery-status-detail", "Preparing the controller page")
      );
    } else {
      headerText.appendChild(
        el("div", "recovery-status-reason", "No response from controller")
      );
      headerText.appendChild(
        el(
          "div",
          "recovery-status-detail",
          view.mode === "retrying"
            ? "Still waiting. Retrying with increasing intervals."
            : REASON_DETAIL[view.reason] || "Attempting to reconnect."
        )
      );
    }
    header.appendChild(headerText);
    panel.appendChild(header);

    if (view.mode === "loading") {
      const step = el("p", "recovery-step");
      step.appendChild(el("span", "recovery-spinner"));
      step.appendChild(document.createTextNode(`Loading: ${view.stepLabel}`));
      panel.appendChild(step);
      panel.appendChild(
        el(
          "p",
          "recovery-message",
          view.longRunning
            ? "This step normally takes longer than the others. Completed resources stay loaded."
            : "Completed resources stay loaded. Page data loads once required files are in."
        )
      );
      return panel;
    }

    panel.appendChild(
      countdownPanel(
        null,
        view.waitSeconds,
        view.mode === "retrying"
          ? `Next attempt\nAttempt ${view.attempt} (backoff)`
          : `Next attempt\nAttempt ${view.attempt}`
      )
    );

    if (view.mode === "retrying") {
      panel.appendChild(
        el(
          "p",
          "recovery-message",
          "Retry intervals are increasing so the controller is not overwhelmed."
        )
      );
    }

    panel.appendChild(retryButton(onRetryNow, view.stepName));
    return panel;
  };

  // ---------------------------------------------------------------------------
  // Mount / render
  // ---------------------------------------------------------------------------
  let focusedBeforeOverlay = null;
  let overlayIsVisible = false;

  const ensureBackdrop = () => {
    let backdrop = document.getElementById(BACKDROP_ID);
    let isNewElement = false;

    if (!backdrop) {
      // Create the element if it doesn't exist
      backdrop = el("div", "recovery-backdrop");
      backdrop.id = BACKDROP_ID;
      document.body.appendChild(backdrop);
      isNewElement = true;
    }

    // Upgrade (or maintain) dialog semantics. Whether the element came from the
    // kernel or was just created, after this function it must always have:
    // role="dialog", aria-modal="true", aria-label, tabindex, an announcer child,
    // and a Tab handler. This is idempotent -- calling it multiple times is safe.
    backdrop.setAttribute("role", "dialog");
    backdrop.setAttribute("aria-modal", "true");
    backdrop.setAttribute("aria-label", "Page recovery overlay");
    backdrop.setAttribute("tabindex", "-1");

    // Ensure the countdown announcer exists. It is a separate live region so
    // only the countdown update is announced, not the entire panel.
    // aria-atomic=false prevents re-announcing the whole panel on every
    // countdown tick.
    if (!backdrop.querySelector(".recovery-countdown-announcer")) {
      const announcer = el("div", "recovery-countdown-announcer");
      announcer.setAttribute("role", "status");
      announcer.setAttribute("aria-live", "polite");
      announcer.setAttribute("aria-atomic", "false");
      announcer.style.position = "absolute";
      announcer.style.left = "-10000px";
      announcer.style.width = "1px";
      announcer.style.height = "1px";
      announcer.style.overflow = "hidden";
      backdrop.appendChild(announcer);
    }

    // Attach keyboard containment handler if not already present.
    // Identify by a marker on the backdrop so we never attach it twice.
    if (!backdrop.dataset.tabHandlerAttached) {
      backdrop.addEventListener("keydown", (event) => {
        if (event.key !== "Tab") return;

        const focusableElements = backdrop.querySelectorAll(
          "button, [href], input, select, textarea, [tabindex]:not([tabindex=\"-1\"])"
        );

        // If there are no focusable children (e.g., loading state), keep focus
        // on the backdrop itself and prevent Tab from escaping.
        if (focusableElements.length === 0) {
          event.preventDefault();
          backdrop.focus();
          return;
        }

        const firstElement = focusableElements[0];
        const lastElement = focusableElements[focusableElements.length - 1];

        if (event.shiftKey) {
          // Shift+Tab: cycle backwards
          if (document.activeElement === firstElement) {
            event.preventDefault();
            lastElement.focus();
          }
        } else {
          // Tab: cycle forwards
          if (document.activeElement === lastElement) {
            event.preventDefault();
            firstElement.focus();
          }
        }
      });
      backdrop.dataset.tabHandlerAttached = "true";
    }

    return backdrop;
  };

  const setFocus = (backdrop) => {
    // Move focus into the modal; prefer the retry button if available
    const retryButton = backdrop.querySelector(".btn.accent");
    if (retryButton) {
      retryButton.focus();
    } else {
      backdrop.focus();
    }
  };

  const restoreFocus = () => {
    // Return focus to what had it before the overlay appeared.
    // If that element is gone or not focusable, return to document.
    if (focusedBeforeOverlay && focusedBeforeOverlay !== document.body) {
      try {
        focusedBeforeOverlay.focus();
      } catch (e) {
        // Element no longer exists or can't receive focus; use body
      }
    }
    focusedBeforeOverlay = null;
  };

  // Signature kept stable across renders so the countdown can repaint without
  // rebuilding the panel and stealing focus from the Retry now button.
  const signatureOf = (view) =>
    view.visible
      ? `${view.mode}|${view.kind}|${view.stepName}|${view.attempt ?? 0}|${view.longRunning ? 1 : 0}`
      : "hidden";

  let lastSignature = null;

  const render = (state, { onRetryNow = () => {} } = {}) => {
    const view = deriveView(state);
    const backdrop = ensureBackdrop();

    if (!view.visible) {
      backdrop.classList.remove("active");
      document.body.classList.remove("recovery-active");
      // Preserve the announcer but clear the panel content
      const announcer = backdrop.querySelector(".recovery-countdown-announcer");
      if (announcer) {
        backdrop.replaceChildren(announcer);
      } else {
        // Defence in depth: if announcer is missing, just clear everything
        backdrop.replaceChildren();
      }
      lastSignature = "hidden";
      overlayIsVisible = false;
      // Return focus to what had it before the overlay appeared
      restoreFocus();
      return view;
    }

    // Transitioning from hidden to visible: save focus and make visible first.
    // Elements with display:none cannot receive focus, so the backdrop must be
    // visible before any focus move attempts.
    if (!overlayIsVisible) {
      overlayIsVisible = true;
      focusedBeforeOverlay = document.activeElement;
      backdrop.classList.add("active");
      document.body.classList.add("recovery-active");
    }

    const signature = signatureOf(view);
    if (signature !== lastSignature) {
      // New panel content: rebuild it, keeping the announcer if it exists
      const announcer = backdrop.querySelector(".recovery-countdown-announcer");
      backdrop.replaceChildren(buildPanel(view, onRetryNow));
      if (announcer) {
        backdrop.appendChild(announcer);
      }
      lastSignature = signature;
      // Focus moved into the overlay for new content
      setFocus(backdrop);
    } else {
      // Only countdown changed: update both the display and the announcement
      const value = backdrop.querySelector(".recovery-countdown-value");
      if (value) value.textContent = `${view.waitSeconds} s`;
      const announcer = backdrop.querySelector(".recovery-countdown-announcer");
      if (announcer) announcer.textContent = `Next attempt in ${view.waitSeconds} second${view.waitSeconds === 1 ? "" : "s"}`;
    }
    return view;
  };

  window.PARecoveryView = {
    deriveView,
    render,
    // Pages name their own steps in operator language; anything unnamed still
    // renders safely via the generic fallback.
    setLabels(entries) {
      Object.entries(entries).forEach(([name, label]) => labels.set(name, label));
    },
  };
})();

// ============================ PART 3: browser host ===========================
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
    script.onload = () => {
      script.hasLoaded = true;
      done(null);
    };
    script.onerror = () => {
      script.hasLoaded = true;
      script.remove();
      done({ kind: "network" });
    };
    document.body.appendChild(script);
  };

  const runSection = (name, done) => {
    const load = sectionLoaders.get(name);
    if (!load) {
      done({ kind: "http", status: 501 });
      return null;
    }
    // One AbortController per section run, owned here. The loader receives
    // this run's signal and forwards it to the requests it issues, so
    // cancelling the run aborts exactly those requests and can never touch
    // another module's traffic - no other code holds this controller.
    const controller = new AbortController();

    // ========================================================================
    // Section Loader Contract: Handle and Signal
    // ========================================================================
    // Every section loader receives two values for managing its network lifecycle:
    //
    // 1. handle (Section Request Handle):
    //    Mandatory for ALL PAApi traffic. This wrapper ensures every PAApi call
    //    (handle.get, handle.postForm, handle.postJson, handle.estopPostForm)
    //    automatically receives both the section run's abort signal and its
    //    Operation Deadline as non-overridable options. These are spread last
    //    in buildHandleOpts, so caller opts cannot override them. This guarantees
    //    cancelling the section run aborts exactly these requests and injects
    //    the appropriate timeout category.
    //
    // 2. signal (raw AbortSignal):
    //    Passed separately as an escape hatch for non-PAApi work only. Use this
    //    if your loader needs to join raw fetch() calls or EventSource
    //    subscriptions to the section run's cancellation lifecycle. Do NOT pass
    //    this raw signal into PAApi options - the handle already supplies it,
    //    and doing so would bypass the Operation Deadline injection and timeout
    //    logic. Only destructure { handle } unless you have non-PAApi work.
    //
    // Loaders destructure one or both values depending on their work type:
    //   async ({ handle }) => { ... }  // Pure PAApi caller
    //   async ({ handle, signal }) => { ... }  // Needs to join raw fetch/EventSource
    //
    const buildHandleOpts = (callerOpts = {}) => {
      const deadlineMs = state.deadlines[name];
      return {
        ...callerOpts,
        // The section run's signal and deadline category are mandatory overrides,
        // so they are spread last and cannot be overridden by caller opts.
        signal: controller.signal,
        timeoutMs: deadlineMs ?? window.PageBootstrap.OPERATION_DEADLINE_MS,
      };
    };

    const handle = {
      get: (path, opts) => window.PAApi.get(path, buildHandleOpts(opts)),
      postForm: (path, form, opts) => window.PAApi.postForm(path, form, buildHandleOpts(opts)),
      postJson: (path, json, opts) => window.PAApi.postJson(path, json, buildHandleOpts(opts)),
      estopPostForm: (path, form, opts) => window.PAApi.estopPostForm(path, form, buildHandleOpts(opts)),
    };

    Promise.resolve()
      .then(() => load({ signal: controller.signal, handle }))
      .then(() => done(null))
      .catch((error) => done(error));
    return controller;
  };

  // ---------------------------------------------------------------------------
  // Driving the reducer
  //
  // The reducer decides what should run; this only notices when its `active`
  // slot changes and starts the corresponding real work exactly once.
  // ---------------------------------------------------------------------------
  let startedActiveId = null;
  let lastActive = null;
  // The controller for the section run currently in flight, keyed by the
  // reducer attempt id that started it. Cancellation goes through this handle
  // only, so it can only ever hit the run this host itself started.
  let activeSectionRun = null;

  const settle = (id, error, retryAfterMs) => {
    // A result arriving after its deadline already expired belongs to a
    // request the reducer has moved on from; dropping it keeps the reducer's
    // attempt accounting honest. Each attempt has a unique id, so a late result
    // from attempt N is ignored once the reducer moves to attempt N+1.
    if (!state.active || state.active.id !== id) return;
    const outcome = error
      ? Core.classifyOutcome(error, retryAfterMs ?? error?.retryAfterMs ?? null)
      : { kind: "success" };
    apply({ type: "RESULT", outcome });
  };

  const cancelActive = (active) => {
    if (!active) return;
    if (active.kind === "resource") {
      // Remove the pending script tag if it's still loading
      document.querySelectorAll(`script[src="${active.name}"]`).forEach((script) => {
        if (!script.hasLoaded) {
          script.remove();
        }
      });
    } else if (active.kind === "section") {
      // Abort only the run this host started for that attempt; the controller
      // is private to the run, so no other module's request can be affected.
      if (activeSectionRun && activeSectionRun.id === active.id) {
        activeSectionRun.controller.abort();
        activeSectionRun = null;
      }
    }
  };

  const syncActive = () => {
    const active = state.active;
    // If active changed and previous one is gone, cancel the old work
    if (lastActive && lastActive.id !== (active?.id)) {
      cancelActive(lastActive);
    }
    lastActive = active;

    if (!active || active.id === startedActiveId) return;
    startedActiveId = active.id;
    const id = active.id;

    if (active.kind === "resource") {
      loadScript(active.name, (error) => settle(id, error));
    } else if (active.kind === "section") {
      const controller = runSection(active.name, (error) => settle(id, error));
      activeSectionRun = controller ? { id, controller } : null;
    }
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

  // Detect if there is pending work: active request or a step waiting to retry.
  const hasPendingWork = () => {
    if (state.active) return true;
    const allSteps = [...state.resources, ...state.sections];
    return allSteps.some((step) => step.status === "failed-retrying");
  };

  let clockTimer = null;

  const stopClock = () => {
    if (clockTimer !== null) {
      window.clearTimeout(clockTimer);
      clockTimer = null;
    }
  };

  const startClock = () => {
    if (clockTimer !== null || !hasPendingWork()) return;
    clockTimer = window.setTimeout(tick, TICK_MS);
  };

  let lastTickAt = Date.now();
  const tick = () => {
    clockTimer = null;
    const now = Date.now();
    const dt = now - lastTickAt;
    lastTickAt = now;
    apply({ type: "TICK", dt });
    // After dispatch, restart the clock only if there is more work.
    if (hasPendingWork()) {
      startClock();
    }
  };

  const apply = (action) => {
    state = Core.dispatch(state, action);
    syncActive();
    announceAssetsOnce();
    render();
    publishSectionChange();
    // Restart the clock if any action created new work to do.
    startClock();
  };

  // ---------------------------------------------------------------------------
  // Clock and visibility
  // ---------------------------------------------------------------------------
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
    registerSection(name, load, { label = null, deadlineMs = null } = {}) {
      sectionLoaders.set(name, load);
      if (label) window.PARecoveryView?.setLabels({ [name]: label });
      apply({
        type: "DECLARE_SECTIONS",
        names: [name],
        deadlines: deadlineMs ? { [name]: deadlineMs } : undefined,
      });
      // Sections may only be declared before any section work starts, so a
      // late registration is refused. Silently dropping it would leave a page
      // whose data simply never loads and no indication why.
      if (!state.sections.some((s) => s.name === name)) {
        console.warn(
          `[page-bootstrap] section "${name}" registered after section work began; it will not load. ` +
            "Register sections while the page script is executing."
        );
      }
    },
    setResourceLabels(entries) {
      window.PARecoveryView?.setLabels(entries);
    },

    retryNow(name) {
      apply({ type: "RETRY_NOW", name });
    },
    refreshSections(names) {
      apply({ type: "REFRESH_SECTIONS", names });
    },
    getState() {
      return state;
    },
  };

  const start = () => {
    lastTickAt = Date.now();
    apply({ type: "TICK", dt: 0 });
  };

  if (document.readyState === "complete") {
    start();
  } else {
    window.addEventListener("load", start, { once: true });
  }
})();
