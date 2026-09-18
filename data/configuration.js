// =============================================================================
// data/configuration.js
//
// Configuration: what this droid is made of (CONTEXT.md "Configuration", #288).
// The Droid Build, the Component Picker's families and the toggles behind
// them, the component types, the LED strip route and the droid's name.
// Auto-saves on every change.
//
// Guided Setup takes this surface over while the droid is not set up, and its
// questions are these same controls (data/setup.js, which this surface also
// loads). Inspecting and repairing the controller is Maintenance's
// (data/maintenance.js); the two used to be one page (#404).
// =============================================================================

// What a builder calls the board this image runs on. `identity.board` names the
// firmware build and is not an operator-facing word, and the identity manifest
// carries no name beside it, so this is where the two meet. File scope rather
// than inside the module below, because guided Setup's board step reads it -
// data/setup.js draws over this surface and loads after this file - and a
// second copy is a second thing to keep in step (include/component_registry.inc
// holds the product names; this is the shorter word the surfaces use).
const BOARD_LABELS = {
  artoo_esp32: "Artoo Controller",
  firebeetle2: "FireBeetle 2",
};


(() => {
  const featureToggle = (id, name) => ({
    name,
    input: document.getElementById(`enable-${id}`),
    status: document.getElementById(`status-${id}`),
    available: true,
    state: "off",
  });
  const featureToggles = {
    arm1:        featureToggle("arm1", "Utility Arm 1"),
    arm2:        featureToggle("arm2", "Utility Arm 2"),
    aux1:        featureToggle("aux1", "AUX 1"),
    aux2:        featureToggle("aux2", "AUX 2"),
    aux3:        featureToggle("aux3", "AUX 3"),
    domeEsc:     featureToggle("dome-esc", "Dome ESC"),
    rcCh1:       featureToggle("rc-ch1", "RC Channel 1"),
    rcCh2:       featureToggle("rc-ch2", "RC Channel 2"),
    rcCh3:       featureToggle("rc-ch3", "RC Channel 3"),
    rcCh4:       featureToggle("rc-ch4", "RC Channel 4"),
    rcCh5:       featureToggle("rc-ch5", "RC Channel 5"),
    rcCh6:       featureToggle("rc-ch6", "RC Channel 6"),
    drive:       featureToggle("drive", "Foot Drive"),
    audio:       featureToggle("audio", "Audio"),
    protoR2link: featureToggle("protor2link", "protoR2link"),
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
  const auxLedCountInput = document.getElementById("aux-led-count");
  const auxLedRouteStatus = document.getElementById("aux-led-route-status");
  const auxLedRouteBadge = document.getElementById("aux-led-route-badge");
  const auxLedSwatch = document.getElementById("aux-led-swatch");
  const auxLedPreviewText = document.getElementById("aux-led-preview-text");
  const auxLedPreviewNote = document.getElementById("aux-led-preview-note");
  const setupEnabledSummary = document.getElementById("setup-enabled-summary");
  const setupSaveSummary = document.getElementById("setup-save-summary");
  const identityNameInput = document.getElementById("droid-name-input");
  const identityMdnsCheckbox = document.getElementById("mdns-use-name");
  const identitySaveButton = document.getElementById("identity-save-button");
  const identityFeedback = document.getElementById("identity-feedback");
  const identityActions = document.getElementById("identity-actions");
  const identityDiagnosis = document.getElementById("identity-diagnosis");

  // Map from API payload key to featureToggles key
  const TOGGLE_KEY_MAP = {
    enableArm1:        "arm1",
    enableArm2:        "arm2",
    enableAux1:        "aux1",
    enableAux2:        "aux2",
    enableAux3:        "aux3",
    enableDomeEsc:     "domeEsc",
    enableRcCh1:       "rcCh1",
    enableRcCh2:       "rcCh2",
    enableRcCh3:       "rcCh3",
    enableRcCh4:       "rcCh4",
    enableRcCh5:       "rcCh5",
    enableRcCh6:       "rcCh6",
    enableDrive:       "drive",
    enableAudio:       "audio",
    enableProtoR2link: "protoR2link",
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

  // The save state, as a pill beside the feedback line it belongs to. The four
  // outcome classes are the anatomy's own: green it saved, amber there is
  // something left to do about it, red it was refused, and no colour at all
  // while nothing has happened yet (ADR 0066).
  const setSaveSummary = (message, state = "info") => {
    if (!setupSaveSummary) return;
    const classMap = { ok: "pill-ok", saving: "pill-warn", warn: "pill-warn", error: "pill-error", info: "" };
    setupSaveSummary.dataset.state = state;
    setupSaveSummary.className = `status-pill ${classMap[state] ?? ""}`.trim();
    setupSaveSummary.textContent = message;
  };

  const setFeedbackState = (element, message, variant = "") => {
    if (!element) return;
    element.textContent = message;
    element.className = variant ? `feedback ${variant}` : "feedback";
  };

  const setFeatureFeedback = (message, variant = "") => {
    setFeedbackState(featureFeedback, message, variant);
  };

  const normalizeIdentityInput = (value) => String(value || "")
    .toLowerCase()
    .replace(/\s+/g, "")
    .replace(/[^a-z0-9-]/g, "")
    .slice(0, 32);

  const setIdentityFeedback = (message, variant = "") => {
    setFeedbackState(identityFeedback, message, variant);
  };

  // Diagnosis explains WHY identity failed and outlives the feedback line, which
  // every identity action rewrites. Keep it on its own element: setIdentityFeedback
  // assigns textContent, so a save or a reload of identity would erase it.
  const setIdentityDiagnosis = (message) => {
    if (identityDiagnosis) identityDiagnosis.textContent = message || "";
  };

  const renderIdentity = (identity) => {
    if (identityNameInput) {
      identityNameInput.value = normalizeIdentityInput(identity?.droidName || "protoartoo");
    }
    if (identityMdnsCheckbox) {
      identityMdnsCheckbox.checked = Boolean(identity?.mdnsUseName);
    }
  };

  // What the manifest means for each component row is data/feature_availability.js's
  // to hear and publish; this surface only shows the name and says it arrived.
  const receiveIdentity = (identity) => {
    renderIdentity(identity);
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
          // Versions match but identity is invalid (invalid feature list)
          diagMessage = "This controller's firmware sent a feature list this page cannot read. Uploading the same release again will not fix it.";
        }
      }

      setIdentityDiagnosis(diagMessage);
    } catch (error) {
      console.warn("[configuration] diagnosis failed:", error);
      // Show the no-version-evidence sentence when diagnosis cannot fetch versions
      setIdentityDiagnosis("The controller could not report which features are available.");
    }
  };

  window.addEventListener("pa:identity-available", (event) => {
    receiveIdentity(event.detail);
    // Clear the Retry button when identity loads successfully
    if (identityActions) {
      identityActions.innerHTML = "";
    }
    // Clear diagnosis when identity succeeds (it outlives failed state)
    setIdentityDiagnosis("");
  });

  window.addEventListener("pa:identity-unavailable", (event) => {
    const reason = event.detail?.reason || "no-response";
    // Different message based on reason: transport failure promises reconnection;
    // validation failure is terminal and Retry button says what to do
    const message = reason === "incompatible"
      ? "Could not load controller identity."
      : "Could not load controller identity. Reconnecting…";
    setIdentityFeedback(message, "error");
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
      console.error("[configuration] saveIdentity failed:", error);
      setIdentityFeedback(window.PAApi.messageFor(error), "error");
    } finally {
      if (identitySaveButton) {
        identitySaveButton.disabled = false;
      }
    }
  };

  // Whether a component change is still on its way to the controller: from the
  // moment it is made, through the debounce, until the save that carries it has
  // answered. Published because the Restart that would cut it off now lives on
  // Maintenance, and a restart during a save loses the change without a word -
  // Restart used to sit on this page with its button greyed out for exactly
  // this window (#404). Maintenance asks at the press rather than being told,
  // so it gets the right answer however the two surfaces were mounted.
  let savePending = false;
  window.PAConfigurationSave = { isPending: () => savePending };

  // Fields a Component Picker pick carries beside the toggles - today the
  // Sound Component Member. They ride the next save and are cleared once it has
  // been sent, so a pick is never held back for a later one (#369).
  let pendingPickParams = {};

  const setSavePending = (pending) => {
    savePending = pending;
    if (pending) {
      setSaveSummary("Saving...", "saving");
    } else if (setupSaveSummary?.dataset.state === "saving") {
      setSaveSummary("Auto-save ready", "info");
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
    // Where the strip is routed is a CHOSEN POSTURE, not a health signal, so it
    // takes no colour at all: colouring an answer the builder gave would make a
    // setting read as a symptom (CONTEXT.md "Status Colour", amended by the
    // operator 2026-09-16). The two readouts say the same fact at two lengths -
    // the section head says where, the pill beside the count says which line.
    if (auxLedRouteStatus) {
      auxLedRouteStatus.textContent = hasRgb
        ? `Routed via ${AUX_RGB_LABEL_BY_KEY[rgbKey]} LED`
        : "Not routed";
    }
    if (auxLedRouteBadge) {
      auxLedRouteBadge.textContent = hasRgb ? AUX_RGB_LABEL_BY_KEY[rgbKey] : "Not routed";
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

  // The section head's subtitle: how many of the components this image can
  // offer are switched on. It is a count and takes no colour - what a builder
  // ticked is a chosen posture, and a green count would read as a verdict on
  // their droid (CONTEXT.md "Status Colour").
  const updateEnabledSummary = () => {
    if (!setupEnabledSummary) return;
    const toggles = Object.values(featureToggles).filter((toggle) => Boolean(toggle.input) && toggle.available);
    const enabledCount = toggles.filter((toggle) => toggle.input.checked).length;
    const total = toggles.length;
    setupEnabledSummary.textContent = `${enabledCount} of ${total} switched on`;
  };


  const renderFeatures = (payload) => {
    const components = payload?.components || {};

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
      enableDomeEsc: components.domeEsc?.enabled,
      enableRcCh1: components.rcCh1?.enabled,
      enableRcCh2: components.rcCh2?.enabled,
      enableRcCh3: components.rcCh3?.enabled,
      enableRcCh4: components.rcCh4?.enabled,
      enableRcCh5: components.rcCh5?.enabled,
      enableRcCh6: components.rcCh6?.enabled,
      enableDrive: components.drive?.enabled,
      enableAudio: components.audio?.enabled,
      enableProtoR2link: components.protoR2link?.enabled,
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

    // Populate component labels (badges and descriptions) from the config response.
    // Maps API keys to internal component names used for ID lookups.
    const apiKeyToComponentName = {
      drive: "enable_drive",
      audio: "enable_audio",
      protoR2link: "enable_protor2link",
      domeEsc: "enable_dome_esc",
      arm1: "enable_arm1",
      arm2: "enable_arm2",
      aux1: "enable_aux1",
      aux2: "enable_aux2",
      aux3: "enable_aux3",
      rcCh1: "enable_rc_ch1",
      rcCh2: "enable_rc_ch2",
      rcCh3: "enable_rc_ch3",
      rcCh4: "enable_rc_ch4",
      rcCh5: "enable_rc_ch5",
      rcCh6: "enable_rc_ch6",
    };

    const componentLabels = {
      drive: components.drive?.label,
      audio: components.audio?.label,
      protoR2link: components.protoR2link?.label,
      domeEsc: components.domeEsc?.label,
      arm1: components.arm1?.label,
      arm2: components.arm2?.label,
      aux1: components.aux1?.label,
      aux2: components.aux2?.label,
      aux3: components.aux3?.label,
      rcCh1: components.rcCh1?.label,
      rcCh2: components.rcCh2?.label,
      rcCh3: components.rcCh3?.label,
      rcCh4: components.rcCh4?.label,
      rcCh5: components.rcCh5?.label,
      rcCh6: components.rcCh6?.label,
    };

    Object.entries(componentLabels).forEach(([apiKey, label]) => {
      const componentName = apiKeyToComponentName[apiKey];
      if (!componentName) return;

      // Update badge: show the label if it exists, hide if it doesn't
      const badge = document.getElementById(`badge-${componentName}`);
      if (badge) {
        badge.textContent = label || "";
      }

      // Update description label element: populate the <strong> tag with the label.
      // If no label exists, remove the entire label-desc span so the description reads correctly.
      const labelElem = document.getElementById(`label-${componentName}`);
      if (labelElem) {
        if (label) {
          labelElem.textContent = label;
        } else {
          // No label: remove the entire trailing sentence span
          const labelDescSpan = document.getElementById(`label-desc-${componentName}`);
          if (labelDescSpan) {
            // Use parentNode.removeChild for compatibility with test mocks
            if (labelDescSpan.parentNode) {
              labelDescSpan.parentNode.removeChild(labelDescSpan);
            }
          }
        }
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
      window.ComponentPicker?.adopt(result.data);
      // The Droid Build rides on the same payload, so the step below draws the
      // droid's own answer without asking the controller a second time.
      window.DroidBuild?.adopt(result.data);
    } catch (error) {
      console.error("[configuration] loadFeatures failed:", error);
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
    let carriedPick = false;
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
      Object.entries(pendingPickParams).forEach(([field, value]) => body.set(field, value));
      carriedPick = Object.keys(pendingPickParams).length > 0;
      pendingPickParams = {};
      const result = await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
      if (featureEditGeneration === requestEditGeneration) {
        renderFeatures(result.data);
        window.ComponentPicker?.adopt(result.data);
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
        setSaveSummary(`Saved at ${savedAt} · restart required`, "warn");
      } else {
        setFeatureFeedback(`Saved at ${savedAt}`, "success");
        setSaveSummary(`Saved at ${savedAt}`, "ok");
      }
    } catch (error) {
      console.error("[configuration] saveFeatures failed:", error);
      setFeatureFeedback(window.PAApi.messageFor(error), "error");
      // A refused pick is read back rather than left on screen: the cards
      // then show what the droid holds, not the answer it did not take.
      if (carriedPick) loadFeatures();
      // Preserve pending restart status: don't downgrade from warn to error state if restart was already pending
      if (rcRestartPending) {
        setSaveSummary("Save failed, but restart still required", "warn");
      } else {
        setSaveSummary("Save failed", "error");
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

  // A Component Picker pick (data/component_picker.js). Picking is applying:
  // the toggle behind the family is set, and the save goes now, carrying any
  // member field with it, through the same save every toggle on this page uses.
  const applyComponentPick = ({ toggleId = "", enabled = true, params = {} } = {}) => {
    const key = Object.keys(featureToggles).find((name) => featureToggles[name].input?.id === toggleId);
    if (key) {
      featureToggles[key].input.checked = enabled;
      updateToggleStatus(key);
      updateEnabledSummary();
    }
    Object.assign(pendingPickParams, params);
    featureEditGeneration += 1;
    setSavePending(true);
    clearTimeout(saveTimeout);
    saveTimeout = null;
    saveScheduled = false;
    saveFeatures();
  };
  window.PAConfiguration = { applyComponentPick };

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


  initSegmentedTypeControls();
  // The Droid Build step, drawn into its host on this surface; guided Setup
  // shows that same host as a step of its run (data/setup.js).
  window.DroidBuildPicker?.mount({
    body: document.getElementById("droid-build-body"),
    summary: document.getElementById("droid-build-summary"),
    feedback: document.getElementById("droid-build-feedback"),
  });
  // The Component Picker, drawn into every component family's host on this
  // surface; guided Setup shows those same hosts as its steps (data/setup.js).
  window.ComponentPicker?.mount(document);
  window.PAFeatureAvailability.subscribe(() => {
    updateAllToggleStatuses();
    updateEnabledSummary();
  });
  updateEnabledSummary();
  updateAuxLedConfigVisibility();
  setSaveSummary("Auto-save ready", "info");
  renderIdentity({ droidName: "protoartoo", mdnsUseName: false });
  setIdentityFeedback("Loading controller identity…");
  if (window.PAIdentity) receiveIdentity(window.PAIdentity);
  loadFeatures();

  // ---- What the droid is doing right now ----
  //
  // Two things on this surface read the live status rather than the saved
  // configuration: the LED strip preview, which shows the colour the strip is
  // actually showing, and the sound module named beside Audio. The serial
  // lanes and the memory readings that used to share this read are
  // Maintenance's now (data/maintenance.js), so this surface asks for the
  // status on its own account while it is the one on screen (#404).
  const s2DriverLabel = document.getElementById("s2-driver-label");

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
      auxLedPreviewNote.textContent = "";
      return;
    }

    // The words carry both readings. Neither sentence is coloured: the swatch
    // beside them already shows the one thing on this row that is a colour, and
    // it is the strip's own live colour rather than a state.
    if (!available) {
      auxLedPreviewText.textContent = `LED strip on AUX${pin} unavailable`;
      auxLedPreviewNote.textContent = "The strip is recorded, but the controller could not start it.";
      return;
    }

    auxLedPreviewText.textContent = `AUX${pin} LED - ${effect}`;
    auxLedPreviewNote.textContent = `Live colour ${r},${g},${b} with the ${effect} effect.`;
  };

  const renderLiveStatus = (d) => {
    if (s2DriverLabel) {
      s2DriverLabel.textContent = d.audio?.driver || "";
    }
    renderAuxLedPreview(d);
  };

  // Rethrows, so the surface poll below can tell a read that landed from one
  // that did not (#360). This surface has no status line of its own to say it
  // on: the poll's report and the shell's "showing what this screen last read"
  // note say it instead.
  const refreshLiveStatus = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
    renderLiveStatus(result.data);
  };

  // SSE-first, with visibility-aware fallback polling.
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderLiveStatus(payload);
    });
    // One-shot fetch if SSE hasn't delivered a status frame yet. The stream is
    // what this surface reads from after it, so a failure is reported rather
    // than retried here.
    if (!window.PAStatusStream.getLastStatus()) {
      refreshLiveStatus().catch((error) => {
        console.warn("[configuration] status read failed:", error);
      });
    }
  } else {
    // Fallback: poll every 5 s, suspended while the tab is hidden and while the
    // operator is reading another surface -- the shell stops it on the way out
    // and starts it again on the way back (ADR 0048, #360). The failed read is
    // PASurface.poll()'s to report, so that a refresh that never landed does
    // not take the "showing what this screen last read" note down (#360).
    window.PASurface.poll(refreshLiveStatus, {
      cadenceMs: 5000,
      runOnStart: true,
      refreshOnReturn: true,
    }).start();
  }
})();
