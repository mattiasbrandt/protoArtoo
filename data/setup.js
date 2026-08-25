// =============================================================================
// setup.js
//
// Setup page controller — hardware component enable/disable toggles and
// servo/AUX component type selectors. Auto-saves on every change.
// =============================================================================
(() => {
  const listeners = new Set();
  let phase = "loading";
  // Identity manifest is fetched once by shell.js at page load and cached in window.PAIdentity.
  // Do not restore per-card endpoint probing; the resolve() function reads this cache only.
  let identity = null;
  let identityErrorReason = null;  // "incompatible" or "no-response" when phase === "error"
  const STATE_LABELS = Object.freeze({
    on: "On",
    off: "Off",
    "not-in-this-build": "Not included",
    "not-on-this-board": "Not on this board",
    checking: "Checking controller",
    "identity-unavailable": "Availability unknown",
    "included": "Included",
  });

  // Resolve the compile-time manifest tiers first (board capability, build flag),
  // then the optional runtime toggle (Component Toggle). Compile tiers resolve
  // before runtime state to preserve the reason: "not in this build" or "not on
  // this board" takes precedence over "off". Component rows and build-conditional
  // panels call this same seam.
  //
  // Layer 2 validation: per-key completeness uses Object.hasOwn — not optional
  // chaining, not the `in` operator — so a MISSING key is distinguished from a
  // false value. The resolver already knows the key it was asked for, so there is
  // no JavaScript mirror of the .inc manifests to drift out of date.
  //
  // A key missing from an already-validated manifest is TERMINALLY unknown: the
  // fetch has completed and no later request will supply it. That is phase="failed"
  // with state="identity-unavailable", which renders as "Availability unknown" —
  // never phase="checking", which would tell the operator the page is still working
  // on an answer that will never arrive.
  const resolve = ({ boardCapability = "", buildFlag = "", enabled = true, hasToggle = true } = {}) => {
    const needsManifest = Boolean(boardCapability || buildFlag);
    if (needsManifest && phase !== "ready") {
      return phase === "error"
        ? { phase: "failed", state: "identity-unavailable" }
        : { phase: "checking", state: "checking" };
    }
    if (boardCapability) {
      if (!identity?.board_capabilities || !Object.hasOwn(identity.board_capabilities, boardCapability)) {
        // Missing key in board_capabilities is terminally unknown (will not arrive in future fetch)
        return { phase: "failed", state: "identity-unavailable" };
      }
      if (identity.board_capabilities[boardCapability] !== true) {
        return { phase: "ready", state: "not-on-this-board" };
      }
    }
    if (buildFlag) {
      if (!identity?.build_flags || !Object.hasOwn(identity.build_flags, buildFlag)) {
        // Missing key in build_flags is terminally unknown (will not arrive in future fetch)
        return { phase: "failed", state: "identity-unavailable" };
      }
      if (identity.build_flags[buildFlag] !== true) {
        return { phase: "ready", state: "not-in-this-build" };
      }
    }
    if (!hasToggle && enabled) {
      return { phase: "ready", state: "included" };
    }
    return enabled
      ? { phase: "ready", state: "on" }
      : { phase: "ready", state: "off" };
  };

  // Helper to derive control availability from resolved state.
  // Control is interactable when the manifest is ready and the feature is not gated.
  const isFeatureAvailable = (result) => {
    return result.phase === "ready" && result.state !== "not-on-this-board" && result.state !== "not-in-this-build";
  };


  const labelFor = (state) => STATE_LABELS[state] || "Availability unknown";

  // Turn a resolved state into the maker-facing explanation shown below a
  // feature. Component and profiler renderers share this copy policy.
  const reasonFor = (state, featureName, { on = "", notInThisBuild = "" } = {}) => {
    if (state === "on" || state === "included") return on;
    if (state === "not-on-this-board") return `This controller board cannot run ${featureName}.`;
    if (state === "not-in-this-build") return notInThisBuild || `This controller was loaded without ${featureName}.`;
    if (state === "checking") return `Checking whether this controller can run ${featureName}…`;
    if (state === "identity-unavailable") {
      // Two different failures read as identity-unavailable; differ by reason:
      // - "no-response": transport failure, retryable, genuinely reconnecting
      // - "incompatible": validation failure, terminal, no reconnection coming
      if (identityErrorReason === "incompatible") {
        return "The controller's manifest is invalid.";
      }
      return `Could not check ${featureName}. Reconnecting to the controller…`;
    }
    return "";
  };

  const notify = () => listeners.forEach((listener) => listener());

  const setIdentity = (nextIdentity) => {
    identity = nextIdentity || null;
    phase = "ready";
    notify();
  };

  const setIdentityError = (reason = "no-response") => {
    identity = null;
    phase = "error";
    identityErrorReason = reason;
    notify();
  };

  const subscribe = (listener) => {
    listeners.add(listener);
    listener();
    return () => listeners.delete(listener);
  };

  window.PAFeatureAvailability = {
    resolve,
    isFeatureAvailable,
    labelFor,
    reasonFor,
    setIdentity,
    setIdentityError,
    subscribe,
  };
})();

(() => {
  const featureToggle = (id, name) => ({
    name,
    input: document.getElementById(`enable-${id}`),
    status: document.getElementById(`status-${id}`),
    available: true,
    state: "off",
  });
  const featureToggles = {
    arm1:         featureToggle("arm1", "ARM1"),
    arm2:         featureToggle("arm2", "ARM2"),
    aux1:         featureToggle("aux1", "AUX1"),
    aux2:         featureToggle("aux2", "AUX2"),
    aux3:         featureToggle("aux3", "AUX3"),
    dome:         featureToggle("dome", "Dome motor"),
    rcCh1:        featureToggle("rc-ch1", "RC channel 1"),
    rcCh2:        featureToggle("rc-ch2", "RC channel 2"),
    rcCh3:        featureToggle("rc-ch3", "RC channel 3"),
    rcCh4:        featureToggle("rc-ch4", "RC channel 4"),
    rcCh5:        featureToggle("rc-ch5", "RC channel 5"),
    rcCh6:        featureToggle("rc-ch6", "RC channel 6"),
    s1Hoverboard: featureToggle("s1-hoverboard", "Hoverboard drive"),
    s2Sound:      featureToggle("s2-sound", "Sound"),
    s3DomeCtrl:   featureToggle("s3-dome-ctrl", "Dome control"),
  };

  // Component type selects — maps API key to select element
  const typeSelects = {
    arm1Type: document.getElementById("type-arm1"),
    arm2Type: document.getElementById("type-arm2"),
    aux1Type: document.getElementById("type-aux1"),
    aux2Type: document.getElementById("type-aux2"),
    aux3Type: document.getElementById("type-aux3"),
  };
  const AUX_RGB_SELECT_KEYS = ["aux1Type", "aux2Type", "aux3Type"];
  const AUX_RGB_PIN_BY_KEY = { aux1Type: 1, aux2Type: 2, aux3Type: 3 };
  const AUX_RGB_LABEL_BY_KEY = { aux1Type: "AUX1", aux2Type: "AUX2", aux3Type: "AUX3" };
  const AUX_RGB_TOGGLE_KEY_BY_TYPE = { aux1Type: "aux1", aux2Type: "aux2", aux3Type: "aux3" };

  const featureFeedback = document.getElementById("feature-feedback");
  const logLevelSelect = document.getElementById("log-level-select");
  const diagFeedback = document.getElementById("diag-feedback");
  const auxLedCountInput = document.getElementById("aux-led-count");
  const auxLedRouteStatus = document.getElementById("aux-led-route-status");
  const auxLedRouteBadge = document.getElementById("aux-led-route-badge");
  const auxLedSwatch = document.getElementById("aux-led-swatch");
  const auxLedPreviewText = document.getElementById("aux-led-preview-text");
  const auxLedPreviewNote = document.getElementById("aux-led-preview-note");
  const setupEnabledSummary = document.getElementById("setup-enabled-summary");
  const setupSaveSummary = document.getElementById("setup-save-summary");
  const rebootButton = document.getElementById("reboot-button");
  const rebootFeedback = document.getElementById("reboot-feedback");
  const identityNameInput = document.getElementById("droid-name-input");
  const identityMdnsCheckbox = document.getElementById("mdns-use-name");
  const identitySaveButton = document.getElementById("identity-save-button");
  const identityFeedback = document.getElementById("identity-feedback");
  const identityActions = document.getElementById("identity-actions");

  // Map from API payload key to featureToggles key
  const TOGGLE_KEY_MAP = {
    enableArm1:        "arm1",
    enableArm2:        "arm2",
    enableAux1:        "aux1",
    enableAux2:        "aux2",
    enableAux3:        "aux3",
    enableDome:        "dome",
    enableRcCh1:       "rcCh1",
    enableRcCh2:       "rcCh2",
    enableRcCh3:       "rcCh3",
    enableRcCh4:       "rcCh4",
    enableRcCh5:       "rcCh5",
    enableRcCh6:       "rcCh6",
    enableS1Hoverboard: "s1Hoverboard",
    enableS2Sound:     "s2Sound",
    enableS3DomeCtrl:  "s3DomeCtrl",
  };

  let saveInFlight = false;
  let saveQueued = false;
  let saveScheduled = false;
  let featureEditGeneration = 0;
  let rcChangeGeneration = 0;
  let savedRcChangeGeneration = 0;
  let rcRestartPending = false;
  let bootActiveRcComponents = {};  // Snapshot of boot-active RC component state from /api/rc
  // Auto-save state
  let saveTimeout = null;
  const RC_TOGGLE_KEYS = new Set(["rcCh1", "rcCh2", "rcCh3", "rcCh4", "rcCh5", "rcCh6"]);

  const setSaveSummary = (message, state = "info") => {
    if (!setupSaveSummary) return;
    const classMap = { ok: "pill-ok", saving: "pill-warn", warn: "pill-warn", error: "pill-error", info: "pill-info" };
    setupSaveSummary.dataset.state = state;
    setupSaveSummary.className = `status-pill ${classMap[state] || classMap.info}`;
    setupSaveSummary.textContent = message;
  };

  const setFeedbackState = (element, message, variant = "") => {
    if (!element) return;
    element.textContent = message;
    element.className = variant ? `feedback mt-12 ${variant}` : "feedback mt-12";
  };

  const setFeatureFeedback = (message, variant = "") => {
    setFeedbackState(featureFeedback, message, variant);
  };

  const setDiagFeedback = (message, variant = "") => {
    setFeedbackState(diagFeedback, message, variant);
  };

  const normalizeIdentityInput = (value) => String(value || "")
    .toLowerCase()
    .replace(/\s+/g, "")
    .replace(/[^a-z0-9-]/g, "")
    .slice(0, 32);

  const setIdentityFeedback = (message, variant = "") => {
    setFeedbackState(identityFeedback, message, variant);
  };

  const renderIdentity = (identity) => {
    if (identityNameInput) {
      identityNameInput.value = normalizeIdentityInput(identity?.droidName || "protoartoo");
    }
    if (identityMdnsCheckbox) {
      identityMdnsCheckbox.checked = Boolean(identity?.mdnsUseName);
    }
  };

  const receiveIdentity = (identity) => {
    renderIdentity(identity);
    window.PAFeatureAvailability.setIdentity(identity);
    setIdentityFeedback(`Identity loaded at ${new Date().toLocaleTimeString()}`, "success");
  };

  // Perform lazy diagnosis of identity failure after assets are ready.
  // Fetches version info to determine why identity is invalid and displays
  // appropriate diagnosis sentence. Never blocks bootstrap state transitions.
  const performIdentityDiagnosis = async () => {
    try {
      // Fetch expected versions (built into this deployment)
      let expectedFwVersion = "unknown";
      if (window.PAApi) {
        try {
          const fwResult = await window.PAApi.get("/fw-version.json", { timeoutMs: 2500, cache: "no-store" });
          if (fwResult.data?.fwVersion) {
            expectedFwVersion = String(fwResult.data.fwVersion);
          }
        } catch (_error) {
          // Continue with unknown if fetch fails
        }
      }

      // Get running version from status stream (live or cached)
      let runningFwVersion = "unknown";
      let runningFsVersion = "unknown";
      const lastStatus = window.PAStatusStream?.getLastStatus?.();
      if (lastStatus?.firmwareVersion) {
        runningFwVersion = String(lastStatus.firmwareVersion);
      }
      if (lastStatus?.fsVersion) {
        runningFsVersion = String(lastStatus.fsVersion);
      }

      // If no cached status, wait briefly for a status event with bounded timeout
      if (runningFwVersion === "unknown" && window.PAStatusStream?.isSupported?.()) {
        try {
          const statusPromise = new Promise((resolve) => {
            const unsubscribe = window.PAStatusStream.subscribe((eventType, payload) => {
              if (eventType === "status" && payload?.firmwareVersion) {
                unsubscribe();
                resolve(payload);
              }
            });
            // Timeout after 3 seconds to avoid indefinite wait
            setTimeout(() => {
              unsubscribe();
              resolve(null);
            }, 3000);
          });
          const status = await statusPromise;
          if (status?.firmwareVersion) {
            runningFwVersion = String(status.firmwareVersion);
          }
          if (status?.fsVersion) {
            runningFsVersion = String(status.fsVersion);
          }
        } catch (_error) {
          // Continue with last known values
        }
      }

      // Determine diagnosis based on version comparison
      let diagMessage = "The controller could not report which features are available.";
      if (expectedFwVersion !== "unknown" && runningFwVersion !== "unknown") {
        if (expectedFwVersion !== runningFwVersion) {
          diagMessage = "The firmware and filesystem do not match. Upload both from the same release.";
        } else {
          // Versions match but identity is invalid (incompatible manifest)
          diagMessage = "The controller reported an invalid manifest. Uploading the same release again will not fix it.";
        }
      }

      setDiagFeedback(diagMessage, "error");
    } catch (error) {
      console.warn("[setup] diagnosis failed:", error);
      // Silent failure: don't show a diagnosis error, leave feedback empty
    }
  };

  window.addEventListener("pa:identity-available", (event) => {
    receiveIdentity(event.detail);
    // Clear the Retry button when identity loads successfully
    if (identityActions) {
      identityActions.innerHTML = "";
    }
  });

  window.addEventListener("pa:identity-unavailable", (event) => {
    const reason = event.detail?.reason || "no-response";
    window.PAFeatureAvailability.setIdentityError(reason);
    setIdentityFeedback("Could not load controller identity. Reconnecting…", "error");
    // Add persistent Retry button outside the live region
    if (window.PABootstrap && identityActions && !identityActions.querySelector("button")) {
      const retryButton = document.createElement("button");
      retryButton.type = "button";
      retryButton.className = "btn accent";
      retryButton.textContent = "Retry now";
      retryButton.addEventListener("click", () => {
        window.PABootstrap.retryNow("shell-identity");
        if (identityActions) identityActions.innerHTML = "";
      });
      identityActions.innerHTML = "";
      identityActions.appendChild(retryButton);
    }
    // Lazy diagnosis: after assets load, fetch version info to provide specific feedback
    if (reason === "incompatible") {
      window.addEventListener("pa:assets-ready", () => {
        performIdentityDiagnosis();
      }, { once: true });
    }
  });

  const saveIdentity = async () => {
    if (!window.PAApi || !identityNameInput) return;
    const droidName = normalizeIdentityInput(identityNameInput.value);
    identityNameInput.value = droidName;
    if (!droidName) {
      setIdentityFeedback("Droid name is required.", "error");
      return;
    }

    if (identitySaveButton) {
      identitySaveButton.disabled = true;
    }
    setIdentityFeedback("Saving identity...");
    try {
      const body = new URLSearchParams();
      body.set("droidName", droidName);
      body.set("mdnsUseName", identityMdnsCheckbox?.checked ? "true" : "false");
      const result = await window.PAApi.postForm("/api/identity", body, { timeoutMs: 5000 });
      renderIdentity(result.data);
      window.dispatchEvent(new CustomEvent("pa:identity-updated", { detail: result.data }));
      setIdentityFeedback(`Identity saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[setup] saveIdentity failed:", error);
      setIdentityFeedback(window.PAApi.messageFor(error), "error");
    } finally {
      if (identitySaveButton) {
        identitySaveButton.disabled = false;
      }
    }
  };

  const setSavePending = (pending) => {
    if (rebootButton) {
      rebootButton.disabled = pending;
      rebootButton.title = pending ? "Waiting for settings to save..." : "";
    }
    if (pending) {
      setSaveSummary("💾 Saving...", "saving");
    } else if (setupSaveSummary?.dataset.state === "saving") {
      setSaveSummary("💾 Auto-save ready", "info");
    }
  };

  const sanitizeAuxLedCount = () => {
    if (!auxLedCountInput) return 1;
    const parsed = Number(auxLedCountInput.value);
    const normalized = Number.isFinite(parsed)
      ? Math.max(1, Math.min(255, Math.round(parsed)))
      : 1;
    auxLedCountInput.value = String(normalized);
    return normalized;
  };

  const segmentedTypeControls = Array.from(document.querySelectorAll(".type-segmented[data-target]"));

  const syncSegmentedControl = (control) => {
    if (!control) return;
    const targetId = control.dataset.target || "";
    const target = document.getElementById(targetId);
    if (!target) return;
    const selected = String(target.value || "");
    control.querySelectorAll(".seg-option[data-value]").forEach((button) => {
      const isActive = button.dataset.value === selected;
      button.classList.toggle("is-active", isActive);
      button.setAttribute("aria-pressed", isActive ? "true" : "false");
    });
  };

  const syncAllSegmentedControls = () => {
    segmentedTypeControls.forEach((control) => syncSegmentedControl(control));
  };

  const initSegmentedTypeControls = () => {
    segmentedTypeControls.forEach((control) => {
      const targetId = control.dataset.target || "";
      const target = document.getElementById(targetId);
      if (!target) return;

      control.addEventListener("click", (event) => {
        const button = event.target.closest(".seg-option[data-value]");
        if (!button) return;
        const nextValue = String(button.dataset.value || "");
        if (target.value !== nextValue) {
          target.value = nextValue;
          target.dispatchEvent(new Event("change", { bubbles: true }));
        } else {
          syncSegmentedControl(control);
        }
      });

      target.addEventListener("change", () => syncSegmentedControl(control));
      syncSegmentedControl(control);
    });
  };

  const getRgbAuxKeys = () => AUX_RGB_SELECT_KEYS.filter((key) => {
    const toggleKey = AUX_RGB_TOGGLE_KEY_BY_TYPE[key];
    const toggle = featureToggles[toggleKey];
    const enabled = Boolean(toggle?.available && toggle.input?.checked);
    return enabled && typeSelects[key]?.value === "rgb";
  });

  const deriveAuxLedPinFromTypes = () => {
    const rgbKey = getRgbAuxKeys()[0];
    return rgbKey ? AUX_RGB_PIN_BY_KEY[rgbKey] : 0;
  };

  const enforceSingleRgbAux = (changedKey = "") => {
    const rgbKeys = getRgbAuxKeys();
    if (rgbKeys.length <= 1) return;
    const keepKey = changedKey && rgbKeys.includes(changedKey) ? changedKey : rgbKeys[0];
    rgbKeys.forEach((key) => {
      if (key !== keepKey && typeSelects[key]) {
        typeSelects[key].value = "none";
      }
    });
    const keptLabel = AUX_RGB_LABEL_BY_KEY[keepKey] || "selected AUX";
    setFeatureFeedback(`Only one AUX line can drive LED strip output. Keeping ${keptLabel}.`, "warning");
  };

  const updateAuxLedConfigVisibility = () => {
    const rgbKeys = getRgbAuxKeys();
    const rgbKey = rgbKeys[0] || "";
    const hasRgb = Boolean(rgbKey);

    if (auxLedCountInput) {
      auxLedCountInput.disabled = !hasRgb;
    }
    if (auxLedRouteStatus) {
      if (!hasRgb) {
        auxLedRouteStatus.textContent = "Not routed";
        auxLedRouteStatus.style.color = "var(--text-dim)";
      } else {
        auxLedRouteStatus.textContent = `Routed via ${AUX_RGB_LABEL_BY_KEY[rgbKey]} LED`;
        auxLedRouteStatus.style.color = "var(--success)";
      }
    }
    if (auxLedRouteBadge) {
      if (!hasRgb) {
        auxLedRouteBadge.textContent = "🔗 Not routed";
        auxLedRouteBadge.className = "status-pill pill-info status-pill-compact";
      } else {
        auxLedRouteBadge.textContent = `🔗 ${AUX_RGB_LABEL_BY_KEY[rgbKey]}`;
        auxLedRouteBadge.className = "status-pill pill-ok status-pill-compact";
      }
    }
  };

  const featureRow = (toggle) =>
    toggle.input?.closest(".component-row") || toggle.input?.closest(".toggle-switch");

  // Give each component row one stable explanation node. The availability
  // renderer calls this before updating its text and aria relationship.
  const ensureFeatureReason = (toggle, row) => {
    if (toggle.reason || !row) return toggle.reason;
    const reason = document.createElement("div");
    reason.id = `${toggle.input.id}-availability-reason`;
    reason.className = "feature-availability-reason";
    reason.hidden = true;
    row.appendChild(reason);
    toggle.input.setAttribute("aria-describedby", reason.id);
    toggle.reason = reason;
    return reason;
  };

  // Make every interactive control in a component row follow the resolved
  // availability. The component renderer calls this after resolving a state.
  const setRowControlsAvailable = (row, available, primaryInput) => {
    if (!row) return;
    row.querySelectorAll("button, select, input").forEach((control) => {
      if (control === primaryInput || control.type !== "hidden") {
        control.disabled = !available;
        control.setAttribute("aria-disabled", available ? "false" : "true");
      }
    });
  };

  // Apply one resolved state to a component row, including its status copy,
  // panel rail, reason, and all controls that must become inert together.
  const updateToggleStatus = (key) => {
    const toggle = featureToggles[key];
    if (!toggle || !toggle.input || !toggle.status) return;
    const row = featureRow(toggle);
    const result = window.PAFeatureAvailability.resolve({
      boardCapability: row?.dataset?.boardCapability || toggle.input.dataset.boardCapability || "",
      buildFlag: row?.dataset?.buildFlag || toggle.input.dataset.buildFlag || "",
      enabled: toggle.input.checked,
    });

    toggle.state = result.state;
    // Derive available from phase and state: control is interactable when
    // the manifest is ready and the feature is not gated
    toggle.available = window.PAFeatureAvailability.isFeatureAvailable(result);
    toggle.status.textContent = window.PAFeatureAvailability.labelFor(result.state);
    toggle.status.className = `toggle-status feature-state feature-state-${result.state}`;
    toggle.input.disabled = !toggle.available;
    toggle.input.setAttribute("aria-disabled", toggle.available ? "false" : "true");

    if (row) {
      row.classList.add("feature-availability-row");
      row.classList.remove(
        "feature-state-on",
        "feature-state-off",
        "feature-state-not-in-this-build",
        "feature-state-not-on-this-board",
        "feature-state-checking",
        "feature-state-identity-unavailable",
      );
      row.classList.add(`feature-state-${result.state}`);
      row.dataset.featureState = result.state;
      setRowControlsAvailable(row, toggle.available, toggle.input);
      const reason = ensureFeatureReason(toggle, row);
      if (reason) {
        reason.textContent = window.PAFeatureAvailability.reasonFor(result.state, toggle.name);
        reason.hidden = toggle.available;
      }
    }
  };

  const updateAllToggleStatuses = () => {
    Object.keys(featureToggles).forEach(updateToggleStatus);
  };

  const updateEnabledSummary = () => {
    if (!setupEnabledSummary) return;
    const toggles = Object.values(featureToggles).filter((toggle) => Boolean(toggle.input) && toggle.available);
    const enabledCount = toggles.filter((toggle) => toggle.input.checked).length;
    const total = toggles.length;
    const state = enabledCount > 0 ? "ok" : "info";
    const prefix = enabledCount > 0 ? "✅" : "⚪";
    setupEnabledSummary.className = `status-pill ${state === "ok" ? "pill-ok" : "pill-info"}`;
    setupEnabledSummary.textContent = `${prefix} ${enabledCount}/${total} on`;
  };


  const renderFeatures = (payload) => {
    const components = payload?.components || {};
    const system = payload?.system || {};

    // Capture boot-active RC state on the first load (when the page initializes)
    const isInitialLoad = Object.keys(bootActiveRcComponents).length === 0;
    if (isInitialLoad) {
      captureBootActiveRcState(payload);
    }

    const togglePayload = {
      enableArm1: components.arm1?.enabled,
      enableArm2: components.arm2?.enabled,
      enableAux1: components.aux1?.enabled,
      enableAux2: components.aux2?.enabled,
      enableAux3: components.aux3?.enabled,
      enableDome: components.dome?.enabled,
      enableRcCh1: components.rcCh1?.enabled,
      enableRcCh2: components.rcCh2?.enabled,
      enableRcCh3: components.rcCh3?.enabled,
      enableRcCh4: components.rcCh4?.enabled,
      enableRcCh5: components.rcCh5?.enabled,
      enableRcCh6: components.rcCh6?.enabled,
      enableS1Hoverboard: components.s1Hoverboard?.enabled,
      enableS2Sound: components.s2Sound?.enabled,
      enableS3DomeCtrl: components.s3DomeCtrl?.enabled,
    };

    Object.entries(TOGGLE_KEY_MAP).forEach(([payloadKey, toggleKey]) => {
      const toggle = featureToggles[toggleKey];
      if (!toggle || !toggle.input || togglePayload[payloadKey] === undefined) return;
      // Do not sync RC toggles after initial load — they are boot-staged and user edits
      // are pending. Syncing them would overwrite pending changes and lose restart tracking.
      if (!isInitialLoad && RC_TOGGLE_KEYS.has(toggleKey)) return;
      toggle.input.checked = Boolean(togglePayload[payloadKey]);
      updateToggleStatus(toggleKey);
    });

    const typePayload = {
      arm1Type: components.arm1?.type,
      arm2Type: components.arm2?.type,
      aux1Type: components.aux1?.type,
      aux2Type: components.aux2?.type,
      aux3Type: components.aux3?.type,
    };
    Object.entries(typeSelects).forEach(([apiKey, select]) => {
      if (select && typePayload[apiKey] !== undefined) {
        select.value = String(typePayload[apiKey] || "none");
      }
    });

    const auxLedPin = Number(payload?.aux_led_pin || 0);
    const routedRgbKey = auxLedPin >= 1 && auxLedPin <= 3 ? AUX_RGB_SELECT_KEYS[auxLedPin - 1] : "";
    if (routedRgbKey && typeSelects[routedRgbKey]) {
      typeSelects[routedRgbKey].value = "rgb";
    }
    enforceSingleRgbAux(routedRgbKey);
    syncAllSegmentedControls();

    if (auxLedCountInput && payload?.aux_led_count !== undefined) {
      auxLedCountInput.value = String(payload.aux_led_count);
    }
    sanitizeAuxLedCount();
    updateAuxLedConfigVisibility();
    updateEnabledSummary();
    setFeatureFeedback(`Components loaded at ${new Date().toLocaleTimeString()}`, "success");
    if (logLevelSelect && system.logLevel !== undefined) {
      logLevelSelect.value = String(system.logLevel);
    }
  };

  const captureBootActiveRcState = (config) => {
    // Snapshot RC component enabled states at page load (boot-active truth).
    // Later, if saved state matches this, no restart is actually needed.
    if (config?.components) {
      for (const key of RC_TOGGLE_KEYS) {
        if (config.components[key] !== undefined) {
          bootActiveRcComponents[key] = Boolean(config.components[key]?.enabled);
        }
      }
    }
  };

  const checkIfRcRestartNeeded = () => {
    // Check if UI values match boot-active truth.
    // If the operator has changed an RC toggle away from boot-active, restart is needed.
    // If they've reverted it back to boot-active, no restart is needed.
    for (const key of RC_TOGGLE_KEYS) {
      const toggle = featureToggles[key];
      if (!toggle || !toggle.input) continue;
      const currentValue = Boolean(toggle.input.checked);
      const bootActiveValue = bootActiveRcComponents[key];
      if (bootActiveValue !== undefined && currentValue !== bootActiveValue) {
        return true;
      }
    }
    return false;
  };

  const loadFeatures = async () => {
    if (!window.PAApi) return;
    setFeatureFeedback("Loading component settings...");
    try {
      const result = await window.PAApi.get("/api/config", { timeoutMs: 5000 });
      renderFeatures(result.data);
    } catch (error) {
      console.error("[setup] loadFeatures failed:", error);
      setFeatureFeedback(`Failed to load component settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  // Auto-save function
  const saveFeatures = async () => {
    if (!window.PAApi) return;
    if (saveInFlight) {
      saveQueued = true;
      return;
    }

    saveInFlight = true;
    const requestEditGeneration = featureEditGeneration;
    const requestRcChangeGeneration = rcChangeGeneration;
    setFeatureFeedback("Saving...");
    try {
      const body = new URLSearchParams();
      Object.entries(featureToggles).forEach(([key, toggle]) => {
        if (toggle.input && toggle.available) {
          const paramKey = "enable" + key.charAt(0).toUpperCase() + key.slice(1);
          body.set(paramKey, toggle.input.checked ? "true" : "false");
        }
      });
      Object.entries(typeSelects).forEach(([apiKey, select]) => {
        const toggleKey = apiKey.replace(/Type$/, "");
        if (select && featureToggles[toggleKey]?.available !== false) {
          body.set(apiKey, select.value);
        }
      });
      body.set("aux_led_pin", String(deriveAuxLedPinFromTypes()));
      if (auxLedCountInput) {
        body.set("aux_led_count", String(sanitizeAuxLedCount()));
      }
      const result = await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
      if (featureEditGeneration === requestEditGeneration) {
        renderFeatures(result.data);
      }
      // Guard RC restart state: only update if this request's RC generation is newer than the last saved one
      if (requestRcChangeGeneration > savedRcChangeGeneration) {
        savedRcChangeGeneration = requestRcChangeGeneration;
        // Check if UI values match boot-active: if so, restart is not needed
        rcRestartPending = checkIfRcRestartNeeded();
      }
      const savedAt = new Date().toLocaleTimeString();
      if (rcRestartPending) {
        setFeatureFeedback(`Saved at ${savedAt}. Restart the controller to apply RC input changes.`, "success");
        setSaveSummary(`🔄 Saved at ${savedAt} · restart required`, "warn");
      } else {
        setFeatureFeedback(`Saved at ${savedAt}`, "success");
        setSaveSummary(`✅ Saved at ${savedAt}`, "ok");
      }
    } catch (error) {
      console.error("[setup] saveFeatures failed:", error);
      setFeatureFeedback(window.PAApi.messageFor(error), "error");
      // Preserve pending restart status: don't downgrade from warn to error state if restart was already pending
      if (rcRestartPending) {
        setSaveSummary(`🔄 Save failed, but restart still required`, "warn");
      } else {
        setSaveSummary("❌ Save failed", "error");
      }
    } finally {
      saveInFlight = false;
      if (saveQueued) {
        saveQueued = false;
        saveFeatures();
        return;
      }
      if (!saveScheduled) {
        setSavePending(false);
      }
    }
  };

  const debouncedSave = (...args) => {
    setSavePending(true);
    clearTimeout(saveTimeout);
    saveScheduled = true;
    saveTimeout = setTimeout(() => {
      saveScheduled = false;
      saveTimeout = null;
      saveFeatures(...args);
    }, 300);
  };

  // Attach listeners to all toggles and selects
  Object.keys(featureToggles).forEach((key) => {
    const toggle = featureToggles[key];
    if (toggle.input) {
      toggle.input.addEventListener("change", () => {
        updateToggleStatus(key);
        if (!toggle.available) return;
        featureEditGeneration += 1;
        if (RC_TOGGLE_KEYS.has(key)) {
          rcChangeGeneration += 1;
        }
        updateEnabledSummary();
        if (["aux1", "aux2", "aux3"].includes(key)) {
          updateAuxLedConfigVisibility();
        }
        debouncedSave();
      });
    }
  });

  Object.entries(typeSelects).forEach(([typeKey, select]) => {
    if (select) {
      select.addEventListener("change", () => {
        featureEditGeneration += 1;
        if (AUX_RGB_SELECT_KEYS.includes(typeKey)) {
          enforceSingleRgbAux(typeKey);
          updateAuxLedConfigVisibility();
        }
        syncAllSegmentedControls();
        debouncedSave();
      });
    }
  });

  if (auxLedCountInput) {
    auxLedCountInput.addEventListener("change", () => {
      featureEditGeneration += 1;
      sanitizeAuxLedCount();
      debouncedSave();
    });
  }

  if (identityNameInput) {
    identityNameInput.addEventListener("input", () => {
      const normalized = normalizeIdentityInput(identityNameInput.value);
      if (identityNameInput.value !== normalized) {
        identityNameInput.value = normalized;
      }
    });
  }

  if (identitySaveButton) {
    identitySaveButton.addEventListener("click", saveIdentity);
  }


  // Reboot functionality

  const handleReboot = async () => {
    if (!confirm("Reboot the controller? The web interface will be unavailable for about 10 seconds.")) {
      return;
    }
    if (!window.PAApi) return;
    setFeedbackState(rebootFeedback, "Sending reboot command...");
    try {
      await window.PAApi.postForm("/api/reboot", {}, { timeoutMs: 5000 });
      setFeedbackState(rebootFeedback, "Reboot command sent. Wait ~10 seconds and refresh...", "success");
      // Start countdown
      let seconds = 12;
      const countdown = setInterval(() => {
        seconds--;
        if (rebootFeedback && seconds > 0) {
          rebootFeedback.textContent = `Rebooting... ${seconds}s until ready`;
        } else {
          clearInterval(countdown);
          if (rebootFeedback) {
            rebootFeedback.textContent = "Controller should be back online. Refresh the page.";
          }
        }
      }, 1000);
    } catch (error) {
      console.error("[setup] handleReboot failed:", error);
      setFeedbackState(rebootFeedback, window.PAApi.messageFor(error), "error");
    }
  };

  if (rebootButton) {
    rebootButton.addEventListener("click", handleReboot);
  }

  // --- Diagnostics: log level selector ---
  const saveLogLevel = async () => {
    if (!logLevelSelect || !window.PAApi) return;
    setDiagFeedback("Saving...");
    try {
      const body = new URLSearchParams();
      body.set("logLevel", logLevelSelect.value);
      const result = await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
      if (result.data?.system?.logLevel !== undefined) {
        logLevelSelect.value = String(result.data.system.logLevel);
      }
      setDiagFeedback(`Log level saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[setup] saveLogLevel failed:", error);
      setDiagFeedback(window.PAApi.messageFor(error), "error");
    }
  };

  if (logLevelSelect) {
    logLevelSelect.addEventListener("change", saveLogLevel);
  }

  initSegmentedTypeControls();
  window.PAFeatureAvailability.subscribe(() => {
    updateAllToggleStatuses();
    updateEnabledSummary();
  });
  updateEnabledSummary();
  updateAuxLedConfigVisibility();
  setSaveSummary("💾 Auto-save ready", "info");
  renderIdentity({ droidName: "protoartoo", mdnsUseName: false });
  setIdentityFeedback("Loading controller identity…");
  if (window.PAIdentity) receiveIdentity(window.PAIdentity);
  loadFeatures();
  // --- Serial connection status ---
  const serialS1 = document.getElementById("serial-s1-state");
  const serialS2 = document.getElementById("serial-s2-state");
  const serialS3 = document.getElementById("serial-s3-state");
  const serialStatusLine = document.getElementById("serial-status-line");
  const diagUptime = document.getElementById("diag-uptime");
  const diagHeapFree = document.getElementById("diag-heap-free");
  const diagHeapMin = document.getElementById("diag-heap-min");
  const diagHeapLargest = document.getElementById("diag-heap-largest");
  const diagMemoryNote = document.getElementById("diag-memory-note");

  const formatUptime = (uptimeMs) => {
    const totalSeconds = Math.floor(Number(uptimeMs || 0) / 1000);
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;
    return `${hours}h ${minutes}m ${seconds}s`;
  };

  const renderAuxLedPreview = (status) => {
    if (!auxLedSwatch || !auxLedPreviewText || !auxLedPreviewNote) return;
    const aux = status?.auxLed;
    const pin = Number(aux?.pin || 0);
    const available = aux?.available !== false;
    const effect = String(aux?.effect || "off");
    const r = Math.max(0, Math.min(255, Number(aux?.r || 0)));
    const g = Math.max(0, Math.min(255, Number(aux?.g || 0)));
    const b = Math.max(0, Math.min(255, Number(aux?.b || 0)));

    auxLedSwatch.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
    auxLedSwatch.style.opacity = pin > 0 && available && effect !== "off" ? "1" : "0.35";

    if (pin === 0) {
      auxLedPreviewText.textContent = "";
      auxLedPreviewText.style.color = "var(--text-dim)";
      auxLedPreviewNote.textContent = "";
      return;
    }

    if (!available) {
      auxLedPreviewText.textContent = `LED strip on AUX${pin} unavailable`;
      auxLedPreviewText.style.color = "var(--warning)";
      auxLedPreviewNote.textContent = "Strip configured, but output driver is unavailable.";
      return;
    }

    auxLedPreviewText.textContent = `AUX${pin} LED - ${effect}`;
    auxLedPreviewText.style.color = "var(--success)";
    auxLedPreviewNote.textContent = `Live color ${r},${g},${b} with ${effect} effect.`;
  };

  const renderSerialStatus = (d) => {
    if (serialS1) {
      serialS1.textContent = !d.s1Hoverboard ? "Disabled"
        : d.s1Hoverboard.state === "commanding" ? "Active" : "Enabled / Idle";
      serialS1.style.color = d.s1Hoverboard ? "var(--success)" : "var(--text-dim)";
    }
    if (serialS2) {
      serialS2.textContent = !d.s2Sound ? "Disabled"
        : d.s2Sound.state === "playing" ? "Playing" : "Enabled / Idle";
      serialS2.style.color = d.s2Sound ? "var(--success)" : "var(--text-dim)";
    }
    const s2DriverEl = document.getElementById("s2-driver-label");
    if (s2DriverEl) {
      s2DriverEl.textContent = d.s2Sound?.driver || "";
    }
    if (serialS3) {
      const dl = d.dome_link;
      const transport = typeof dl?.transport === "string" ? dl.transport.toUpperCase() : "N/A";
      if (!dl || dl.state === "disabled") {
        serialS3.textContent = "Disabled";
        serialS3.style.color = "var(--text-dim)";
      } else if (dl.state === "connected") {
        serialS3.textContent = `Connected (${transport}, hb rx ${dl.hb_rx} / tx ${dl.hb_tx})`;
        serialS3.style.color = "var(--success)";
      } else if (dl.state === "lost") {
        serialS3.textContent = `Lost (${transport}) — last seen ${dl.last_rx_ms} ms ago`;
        serialS3.style.color = "var(--danger)";
      } else {
        serialS3.textContent = `Waiting for dome heartbeat (${transport})`;
        serialS3.style.color = "var(--warning)";
      }
    }
    if (diagUptime) {
      diagUptime.textContent = formatUptime(d.uptimeMs);
      diagUptime.style.color = "var(--success)";
    }

    const heapFreeKb = Math.round((d.heapFree || 0) / 1024);
    const heapMinKb = Math.round((d.heapMin || 0) / 1024);
    const hasLargest = d.heapLargestBlock !== undefined && d.heapLargestBlock !== null;
    const heapLargestKb = hasLargest ? Math.round(d.heapLargestBlock / 1024) : null;

    const t = window.PA_HEAP || {};
    const heapFreeState = heapFreeKb < Math.round((t.freeCritical || 40000) / 1024) ? "critical" : heapFreeKb < Math.round((t.freeWarn || 65000) / 1024) ? "watch" : "good";
    const heapMinState = heapMinKb < Math.round((t.minCritical || 36864) / 1024) ? "critical" : heapMinKb < Math.round((t.minWarn || 53248) / 1024) ? "watch" : "good";
    const heapLargestState = !hasLargest ? "na" : heapLargestKb < Math.round((t.largestCritical || 20480) / 1024) ? "critical" : heapLargestKb < Math.round((t.largestWarn || 36864) / 1024) ? "watch" : "good";

    const colorForState = (state) =>
      state === "critical" ? "var(--danger)"
        : state === "watch" ? "var(--warning)"
          : state === "na" ? "var(--text-dim)" : "var(--success)";

    if (diagHeapFree) {
      const suffix = heapFreeState === "critical" ? "❌ Critical" : heapFreeState === "watch" ? "⚠️ Watch" : "✅ Good";
      diagHeapFree.textContent = `${heapFreeKb} KB ${suffix}`;
      diagHeapFree.style.color = colorForState(heapFreeState);
    }
    if (diagHeapMin) {
      const suffix = heapMinState === "critical" ? "❌ Critical" : heapMinState === "watch" ? "⚠️ Watch" : "✅ Good";
      diagHeapMin.textContent = `${heapMinKb} KB ${suffix}`;
      diagHeapMin.style.color = colorForState(heapMinState);
    }
    if (diagHeapLargest) {
      if (!hasLargest) {
        diagHeapLargest.textContent = "N/A";
      } else {
        const suffix = heapLargestState === "critical" ? "❌ Fragmented" : heapLargestState === "watch" ? "⚠️ Watch" : "✅ Good";
        diagHeapLargest.textContent = `${heapLargestKb} KB ${suffix}`;
      }
      diagHeapLargest.style.color = colorForState(heapLargestState);
    }
    if (diagMemoryNote) {
      diagMemoryNote.textContent = `Memory Min is a historical low-water mark since boot; current low-water mark is ${heapMinKb} KB.`;
    }
    renderAuxLedPreview(d);

    setFeedbackState(serialStatusLine, `Updated ${new Date().toLocaleTimeString()}`, "success");
  };

  const refreshSerialStatus = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
      renderSerialStatus(result.data);
    } catch (error) {
      console.warn("[setup] refreshSerialStatus failed:", error);
      setFeedbackState(serialStatusLine, "Status unavailable", "error");
    }
  };

  // SSE-first serial status updates with visibility-aware fallback polling.
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderSerialStatus(payload);
    });
    // One-shot fetch if SSE hasn't delivered a status frame yet.
    if (!window.PAStatusStream.getLastStatus()) {
      refreshSerialStatus().catch(() => {});
    }
  } else {
    // Fallback: poll every 5 s, suspended while the tab is hidden.
    refreshSerialStatus().catch(() => {});
    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      refreshSerialStatus().catch(() => {});
    }, 5000);
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState !== "hidden") {
        refreshSerialStatus().catch(() => {});
      }
    });
  }
})();

// =============================================================================
// Backup & Restore
// =============================================================================
(() => {
  const downloadBtn = document.getElementById('backup-download-btn');
  const fileInput = document.getElementById('backup-file-input');
  const fileTrigger = document.getElementById('backup-file-trigger');
  const summary = document.getElementById('backup-summary');
  const restoreSections = document.getElementById('restore-sections');
  const restoreBtnRow = document.getElementById('restore-btn-row');
  const restoreBtn = document.getElementById('backup-restore-btn');
  const feedback = document.getElementById('backup-feedback');

  if (!downloadBtn || !fileInput || !feedback) return;

  let parsedBackup = null;

  const setFeedback = (msg, variant = '') => {
    feedback.textContent = msg;
    feedback.className = variant ? `feedback mt-12 ${variant}` : 'feedback mt-12';
  };

  const showRestorePanel = (show) => {
    if (summary) summary.hidden = !show;
    if (restoreSections) restoreSections.hidden = !show;
    if (restoreBtnRow) restoreBtnRow.hidden = !show;
  };

  // ---- DOWNLOAD BACKUP ----
  const downloadBackup = async () => {
    if (!window.PAApi) return;
    downloadBtn.disabled = true;
    setFeedback('Downloading settings...');
    try {
      const [configRes, rcMapRes, tracksRes, moodMapRes, fwRes] = await Promise.allSettled([
        window.PAApi.get('/api/config', { timeoutMs: 10000 }),
        window.PAApi.get('/api/rc/map', { timeoutMs: 10000 }),
        window.PAApi.get('/api/audio/tracks', { timeoutMs: 10000 }),
        window.PAApi.get('/api/audio/mood-map', { timeoutMs: 10000 }),
        fetch('/fw-version.json').then((r) => r.json()).catch(() => ({})),
      ]);

      const failed = [];
      const extract = (res, label) => {
        if (res.status === 'fulfilled') return res.value?.data ?? null;
        failed.push(label);
        return null;
      };

      const config = extract(configRes, 'config');
      const rc_map = extract(rcMapRes, 'rc_map');
      const audio_tracks = extract(tracksRes, 'audio_tracks');
      const audio_mood_map = extract(moodMapRes, 'audio_mood_map');

      if (failed.length > 0) {
        setFeedback(`Failed to fetch: ${failed.join(', ')}. Backup aborted.`, 'error');
        return;
      }

      const fw_version =
        fwRes.status === 'fulfilled' ? (fwRes.value?.firmwareVersion || 'unknown') : 'unknown';

      const backup = {
        schema: 1,
        generated: new Date().toISOString(),
        fw_version,
        config,
        rc_map,
        audio_tracks,
        audio_mood_map,
      };

      const blob = new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `artoo-backup-${new Date().toISOString().slice(0, 10)}.json`;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);

      setFeedback(`Backup downloaded at ${new Date().toLocaleTimeString()}`, 'success');
    } catch (err) {
      setFeedback(`Backup failed: ${window.PAApi?.messageFor(err) || err.message}`, 'error');
    } finally {
      downloadBtn.disabled = false;
    }
  };

  // ---- RESTORE: flatten GET /api/config nested JSON to POST form params ----
  const configToFormParams = (cfg) => {
    const p = new URLSearchParams();
    const d = cfg?.drive || {};
    const rc = cfg?.rc || {};
    const components = cfg?.components || {};
    const dome = cfg?.dome || {};
    const sys = cfg?.system || {};

    if (d.speedLimitMax !== undefined) p.set('speedLimitMax', d.speedLimitMax);
    if (d.speedPresetSlow !== undefined) p.set('speedPresetSlow', d.speedPresetSlow);
    if (d.speedPresetNormal !== undefined) p.set('speedPresetNormal', d.speedPresetNormal);
    if (d.speedPresetTurbo !== undefined) p.set('speedPresetTurbo', d.speedPresetTurbo);
    if (d.stationary !== undefined) p.set('stationary', d.stationary ? 'true' : 'false');
    if (d.webDriveTimeoutMs !== undefined) p.set('webDriveTimeoutMs', d.webDriveTimeoutMs);

    if (rc.sbusTimeoutMs !== undefined) p.set('sbusTimeoutMs', rc.sbusTimeoutMs);
    if (rc.inputMode !== undefined) p.set('rcInputMode', rc.inputMode);
    if (rc?.sbus?.recvCh2 !== undefined) p.set('sbusRecvCh2', rc.sbus.recvCh2 ? 'true' : 'false');

    [
      ['arm1', 'enableArm1'], ['arm2', 'enableArm2'],
      ['aux1', 'enableAux1'], ['aux2', 'enableAux2'], ['aux3', 'enableAux3'],
      ['dome', 'enableDome'],
      ['rcCh1', 'enableRcCh1'], ['rcCh2', 'enableRcCh2'], ['rcCh3', 'enableRcCh3'],
      ['rcCh4', 'enableRcCh4'], ['rcCh5', 'enableRcCh5'], ['rcCh6', 'enableRcCh6'],
      ['s1Hoverboard', 'enableS1Hoverboard'],
      ['s2Sound', 'enableS2Sound'],
      ['s3DomeCtrl', 'enableS3DomeCtrl'],
    ].forEach(([key, param]) => {
      if (components[key]?.enabled !== undefined) {
        p.set(param, components[key].enabled ? 'true' : 'false');
      }
    });

    ['arm1', 'arm2', 'aux1', 'aux2', 'aux3'].forEach((key) => {
      if (components[key]?.type !== undefined) p.set(`${key}Type`, components[key].type);
    });

    [
      'arm1OpenUs', 'arm1CloseUs', 'arm2OpenUs', 'arm2CloseUs',
      'aux1OpenUs', 'aux1CloseUs', 'aux2OpenUs', 'aux2CloseUs',
      'aux3OpenUs', 'aux3CloseUs',
    ].forEach((k) => { if (cfg[k] !== undefined) p.set(k, cfg[k]); });

    if (cfg.aux_led_pin !== undefined) p.set('aux_led_pin', cfg.aux_led_pin);
    if (cfg.aux_led_count !== undefined) p.set('aux_led_count', cfg.aux_led_count);

    if (dome.neutralUs !== undefined) p.set('domeNeutralUs', dome.neutralUs);
    if (dome.minPulseUs !== undefined) p.set('domeMinPulseUs', dome.minPulseUs);
    if (dome.maxPulseUs !== undefined) p.set('domeMaxPulseUs', dome.maxPulseUs);
    if (dome.speedLimitPct !== undefined) p.set('domeSpeedLimitPct', dome.speedLimitPct);
    if (dome.rndEnable !== undefined) p.set('domeRndEnable', dome.rndEnable ? 'true' : 'false');
    if (dome.rndSpeedPct !== undefined) p.set('domeRndSpeedPct', dome.rndSpeedPct);
    if (dome.rndPauseMin !== undefined) p.set('domeRndPauseMin', dome.rndPauseMin);
    if (dome.rndPauseMax !== undefined) p.set('domeRndPauseMax', dome.rndPauseMax);
    if (dome.rndMoveMs !== undefined) p.set('domeRndMoveMs', dome.rndMoveMs);
    if (dome.wifiPeerIp !== undefined) p.set('domeWifiPeerIp', dome.wifiPeerIp);

    if (sys.logLevel !== undefined) p.set('logLevel', sys.logLevel);

    return p;
  };

  // ---- RESTORE: audio tracks (one POST per key) ----
  const TRACKS_SKIP = new Set(['volume', 'chirp_bindings', 'chirp_category_bindings']);

  const restoreAudioTracks = async (tracks) => {
    const failed = [];
    for (const [key, value] of Object.entries(tracks)) {
      if (TRACKS_SKIP.has(key) || typeof value !== 'number') continue;
      const chirp = tracks.chirp_bindings?.[key];
      try {
        if (chirp) {
          let bankedOk = false;
          try {
            await window.PAApi.postForm('/api/audio/tracks',
              new URLSearchParams({ key, track: chirp.index, bank: chirp.bank, page: chirp.page }),
              { timeoutMs: 5000 });
            bankedOk = true;
          } catch { /* fall through to simple track */ }
          if (!bankedOk) {
            await window.PAApi.postForm('/api/audio/tracks',
              new URLSearchParams({ key, track: value }), { timeoutMs: 5000 });
          }
        } else {
          await window.PAApi.postForm('/api/audio/tracks',
            new URLSearchParams({ key, track: value }), { timeoutMs: 5000 });
        }
      } catch {
        failed.push(key);
      }
    }
    if (typeof tracks.volume === 'number') {
      try {
        await window.PAApi.postForm('/api/audio',
          new URLSearchParams({ action: 'volume', level: tracks.volume }), { timeoutMs: 5000 });
      } catch {
        failed.push('volume');
      }
    }
    return failed;
  };

  // ---- RESTORE: apply all selected sections ----
  const performRestore = async () => {
    if (!parsedBackup || !window.PAApi) return;
    restoreBtn.disabled = true;
    setFeedback('Restoring...');

    const lines = [];

    const chkConfig = document.getElementById('restore-chk-config');
    const chkRcMap = document.getElementById('restore-chk-rc-map');
    const chkTracks = document.getElementById('restore-chk-audio-tracks');
    const chkMoodMap = document.getElementById('restore-chk-mood-map');

    if (chkConfig?.checked && parsedBackup.config) {
      try {
        await window.PAApi.postForm('/api/config', configToFormParams(parsedBackup.config),
          { timeoutMs: 10000 });
        lines.push('Core config: restored');
      } catch (err) {
        lines.push(`Core config: FAILED — ${window.PAApi.messageFor(err)}`);
      }
    }

    if (chkRcMap?.checked && parsedBackup.rc_map) {
      try {
        await window.PAApi.postForm('/api/rc/map',
          { plain: JSON.stringify(parsedBackup.rc_map) }, { timeoutMs: 10000 });
        lines.push('RC mappings: restored');
      } catch (err) {
        lines.push(`RC mappings: FAILED — ${window.PAApi.messageFor(err)}`);
      }
    }

    if (chkTracks?.checked && parsedBackup.audio_tracks) {
      const failed = await restoreAudioTracks(parsedBackup.audio_tracks);
      lines.push(
        failed.length === 0
          ? 'Audio tracks: restored'
          : `Audio tracks: partial — ${failed.length} failed (${failed.join(', ')})`,
      );
    }

    if (chkMoodMap?.checked && parsedBackup.audio_mood_map) {
      try {
        await window.PAApi.postForm('/api/audio/mood-map', parsedBackup.audio_mood_map,
          { timeoutMs: 5000 });
        lines.push('Audio mood map: restored');
      } catch (err) {
        lines.push(`Audio mood map: FAILED — ${window.PAApi.messageFor(err)}`);
      }
    }

    const anyRestored = lines.some((l) => l.includes(': restored'));
    const anyIssue = lines.some((l) => l.includes('FAILED') || l.includes('partial'));
    if (anyRestored) lines.push('Reboot recommended to apply all restored settings.');
    setFeedback(lines.join('\n'), anyIssue ? 'error' : 'success');
    restoreBtn.disabled = false;
  };

  // ---- FILE PARSE ----
  const handleFile = (file) => {
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (e) => {
      let backup;
      try {
        backup = JSON.parse(e.target.result);
      } catch {
        setFeedback('Invalid file: not valid JSON.', 'error');
        parsedBackup = null;
        showRestorePanel(false);
        return;
      }

      if (!backup.schema) {
        setFeedback('Invalid backup: missing schema field. Restore blocked.', 'error');
        parsedBackup = null;
        showRestorePanel(false);
        return;
      }

      parsedBackup = backup;

      const date = backup.generated ? backup.generated.slice(0, 10) : 'unknown';
      const fw = backup.fw_version || 'unknown';
      const sections = ['config', 'rc_map', 'audio_tracks', 'audio_mood_map'].filter(
        (k) => backup[k],
      );

      if (summary) {
        summary.textContent =
          `Backup from ${date}, firmware ${fw} — ${sections.length} section${sections.length !== 1 ? 's' : ''} found`;
      }

      [
        ['restore-chk-config', 'config'],
        ['restore-chk-rc-map', 'rc_map'],
        ['restore-chk-audio-tracks', 'audio_tracks'],
        ['restore-chk-mood-map', 'audio_mood_map'],
      ].forEach(([id, key]) => {
        const chk = document.getElementById(id);
        if (chk) { chk.checked = Boolean(backup[key]); chk.disabled = !backup[key]; }
      });

      showRestorePanel(true);

      if (backup.schema > 1) {
        setFeedback(
          `Warning: backup schema ${backup.schema} is newer than schema 1. Restore may be incomplete.`,
          'warning',
        );
      } else {
        setFeedback('');
      }
    };
    reader.readAsText(file);
  };

  downloadBtn.addEventListener('click', downloadBackup);
  if (fileTrigger) fileTrigger.addEventListener('click', () => fileInput.click());
  fileInput.addEventListener('change', () => handleFile(fileInput.files?.[0] ?? null));
  if (restoreBtn) restoreBtn.addEventListener('click', performRestore);
})();

// =============================================================================
// Memory Profiler UI
// Feature Availability comes from the identity manifest. The profiler endpoint
// is polled only after that manifest says this image contains the profiler.
// =============================================================================
(() => {
  const card = document.getElementById("profiler-card");
  if (!card) return;
  const content = document.getElementById("profiler-content");
  const availabilityStatus = document.getElementById("profiler-availability-status");
  const availabilityReason = document.getElementById("profiler-availability-reason");
  const availabilityLamp = document.getElementById("profiler-availability-lamp");
  const feedback = document.getElementById("profiler-feedback");

  function kb(bytes) {
    return (bytes / 1024).toFixed(1) + " KB";
  }

  function hwmColor(hwm) {
    if (hwm > 2048) return "#4caf50";   // green
    if (hwm > 1024) return "#ff9800";   // amber
    return "#f44336";                   // red
  }

  function fragColor(ratio) {
    if (ratio < 0.30) return "#4caf50";
    if (ratio < 0.50) return "#ff9800";
    return "#f44336";
  }

  function setText(id, val) {
    const el = document.getElementById(id);
    if (el) el.textContent = val;
  }

  function renderProfiler(d) {
    setText("prof-heap-free",    kb(d.heapFree));
    setText("prof-heap-min",     kb(d.heapMin));
    setText("prof-heap-largest", kb(d.heapLargest));
    setText("prof-frag-ratio",   (d.fragRatio * 100).toFixed(1) + "%");
    setText("prof-alloc-blocks", d.allocBlocks);
    setText("prof-free-blocks",  d.freeBlocks);
    setText("prof-failed-allocs", d.failedAllocs);

    // Fragmentation bar
    const pct = Math.min(d.fragRatio * 100, 100);
    const bar = document.getElementById("prof-frag-bar");
    const lbl = document.getElementById("prof-frag-label");
    if (bar) {
      bar.style.width = pct.toFixed(1) + "%";
      bar.style.background = fragColor(d.fragRatio);
    }
    if (lbl) {
      const health = d.fragRatio < 0.30 ? "Healthy" : d.fragRatio < 0.50 ? "Watch" : "Critical";
      lbl.textContent = health + " — fragmentation " + pct.toFixed(1) + "% (1 - largest/free)";
    }

    // Task stack HWM table
    const hwmTbody = document.getElementById("prof-hwm-tbody");
    if (hwmTbody && Array.isArray(d.taskStacks)) {
      hwmTbody.innerHTML = d.taskStacks.map(t => {
        const color = hwmColor(t.hwmBytes);
        return `<tr>
          <td style="padding:3px 8px">${t.name}</td>
          <td style="text-align:right;padding:3px 8px">${kb(t.hwmBytes)}</td>
          <td style="text-align:center;padding:3px 8px;color:${color};font-weight:bold">${t.status.toUpperCase()}</td>
        </tr>`;
      }).join("");
    }

    // Task heap table (Tier 2 — only when taskHeap present)
    const heapSection = document.getElementById("prof-task-heap-section");
    const heapTbody = document.getElementById("prof-heap-tbody");
    if (heapSection && heapTbody && Array.isArray(d.taskHeap) && d.taskHeap.length > 0) {
      heapSection.hidden = false;
      heapTbody.innerHTML = d.taskHeap.map(t => `<tr>
        <td style="padding:3px 8px">${t.name}</td>
        <td style="text-align:right;padding:3px 8px">${kb(t.current)}</td>
        <td style="text-align:right;padding:3px 8px">${kb(t.peak)}</td>
        <td style="text-align:right;padding:3px 8px">${t.heapCount}</td>
      </tr>`).join("");
    } else if (heapSection) {
      heapSection.hidden = true;
    }

    // Active window banner
    const currentEl = document.getElementById("prof-current-window");
    if (currentEl) {
      if (d.current) {
        currentEl.textContent = `Active: "${d.current.label}" — running min ${kb(d.current.heapFree)}`;
        currentEl.hidden = false;
      } else {
        currentEl.hidden = true;
      }
    }

    // Snapshot history table
    const snapTbody = document.getElementById("prof-snap-tbody");
    if (snapTbody && Array.isArray(d.snapshots)) {
      snapTbody.innerHTML = d.snapshots.map(s => `<tr>
        <td style="padding:3px 8px">${s.label}</td>
        <td style="text-align:right;padding:3px 8px">${kb(s.heapFree)}</td>
        <td style="text-align:right;padding:3px 8px">${kb(s.largestBlock)}</td>
        <td style="text-align:right;padding:3px 8px">${s.ts}</td>
      </tr>`).join("");
    }
  }

  async function refreshProfiler() {
    try {
      const result = await window.PAApi.get("/api/profiler");
      renderProfiler(result.data);
      if (feedback) {
        feedback.textContent = `Memory readings updated at ${new Date().toLocaleTimeString()}`;
        feedback.className = "feedback mt-12 success";
      }
    } catch (error) {
      if (feedback) {
        feedback.textContent = `Memory readings unavailable: ${window.PAApi.messageFor(error)}`;
        feedback.className = "feedback mt-12 warning";
      }
    }
  }

  const poll = window.PageBootstrap.createBackgroundPoll(refreshProfiler, {
    cadenceMs: 5000,
    runOnStart: true,
    refreshOnReturn: true,
  });
  let polling = false;

  // Render the profiler's declared requirements and own its poll lifecycle;
  // the identity subscriber calls this after every availability transition.
  const renderAvailability = () => {
    const featureName = "Memory Profiler";
    const hasRequirementMetadata = Boolean(card.dataset.boardCapability || card.dataset.buildFlag);
    const result = hasRequirementMetadata
      ? window.PAFeatureAvailability.resolve({
          boardCapability: card.dataset.boardCapability || "",
          buildFlag: card.dataset.buildFlag || "",
          hasToggle: false,  // Profiler is compile-time only, no runtime toggle
        })
      : { phase: "checking", state: "checking" };
    // Derive available from phase and state: control is interactable when
    // the manifest is ready and the feature is not gated
    const available = window.PAFeatureAvailability.isFeatureAvailable(result);
    const stateLabel = window.PAFeatureAvailability.labelFor(result.state);
    const stateReason = window.PAFeatureAvailability.reasonFor(result.state, featureName, {
      on: "Live memory readings refresh while this page is open.",
      notInThisBuild: "Memory Profiler is included only in troubleshooting firmware.", // PROVISIONAL: when a second Build Feature Flag needs a bespoke reason, promote this to a registry field + drift-checker coverage
    });
    card.hidden = false;
    card.classList.remove(
      "feature-state-on",
      "feature-state-not-in-this-build",
      "feature-state-not-on-this-board",
      "feature-state-checking",
      "feature-state-identity-unavailable",
    );
    card.classList.add("feature-availability-panel", `feature-state-${result.state}`);
    card.dataset.featureState = result.state;

    if (availabilityStatus) {
      availabilityStatus.textContent = stateLabel;
      availabilityStatus.className = `feature-availability-status feature-state feature-state-${result.state}`;
    }
    if (availabilityReason) availabilityReason.textContent = stateReason;
    if (availabilityLamp) {
      availabilityLamp.className = `feature-availability-lamp-indicator feature-state-${result.state}`;
    }
    if (content) {
      content.inert = !available;
      content.setAttribute("aria-hidden", available ? "false" : "true");
    }

    if (available && !polling) {
      polling = true;
      poll.start();
    } else if (!available && polling) {
      // Stopping the poll on availability loss is part of inertness: when the
      // profiler is not available (compile-time gate or missing from this image),
      // cease endpoint polling to avoid false "update failed" messages.
      polling = false;
      poll.stop();
    }
  };

  window.PAFeatureAvailability.subscribe(renderAvailability);
})();
