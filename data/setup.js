// =============================================================================
// setup.js
//
// Setup page controller — hardware component enable/disable toggles and
// servo/AUX component type selectors. Auto-saves on every change.
// =============================================================================
(() => {
  const featureToggles = {
    arm1:         { input: document.getElementById("enable-arm1"),         status: document.getElementById("status-arm1") },
    arm2:         { input: document.getElementById("enable-arm2"),         status: document.getElementById("status-arm2") },
    aux1:         { input: document.getElementById("enable-aux1"),         status: document.getElementById("status-aux1") },
    aux2:         { input: document.getElementById("enable-aux2"),         status: document.getElementById("status-aux2") },
    aux3:         { input: document.getElementById("enable-aux3"),         status: document.getElementById("status-aux3") },
    dome:         { input: document.getElementById("enable-dome"),         status: document.getElementById("status-dome") },
    rcCh1:        { input: document.getElementById("enable-rc-ch1"),       status: document.getElementById("status-rc-ch1") },
    rcCh2:        { input: document.getElementById("enable-rc-ch2"),       status: document.getElementById("status-rc-ch2") },
    rcCh3:        { input: document.getElementById("enable-rc-ch3"),       status: document.getElementById("status-rc-ch3") },
    rcCh4:        { input: document.getElementById("enable-rc-ch4"),       status: document.getElementById("status-rc-ch4") },
    rcCh5:        { input: document.getElementById("enable-rc-ch5"),       status: document.getElementById("status-rc-ch5") },
    rcCh6:        { input: document.getElementById("enable-rc-ch6"),       status: document.getElementById("status-rc-ch6") },
    s1Hoverboard: { input: document.getElementById("enable-s1-hoverboard"), status: document.getElementById("status-s1-hoverboard") },
    s2Sound:      { input: document.getElementById("enable-s2-sound"),     status: document.getElementById("status-s2-sound") },
    s3DomeCtrl:   { input: document.getElementById("enable-s3-dome-ctrl"), status: document.getElementById("status-s3-dome-ctrl") },
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

  const loadIdentity = async () => {
    if (!window.PAApi || !identityNameInput) return;
    setIdentityFeedback("Loading identity...");
    try {
      const result = await window.PAApi.get("/api/identity", { timeoutMs: 5000 });
      renderIdentity(result.data);
      setIdentityFeedback(`Identity loaded at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[setup] loadIdentity failed:", error);
      setIdentityFeedback(`Failed to load identity: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

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
    const enabled = Boolean(featureToggles[toggleKey]?.input?.checked);
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
  const updateToggleStatus = (key) => {
    const toggle = featureToggles[key];
    if (!toggle || !toggle.input || !toggle.status) return;
    toggle.status.textContent = toggle.input.checked ? "Enabled" : "Disabled";
    toggle.status.style.color = toggle.input.checked ? "var(--success)" : "var(--text-dim)";
  };

  const updateEnabledSummary = () => {
    if (!setupEnabledSummary) return;
    const toggles = Object.values(featureToggles).filter((toggle) => Boolean(toggle.input));
    const enabledCount = toggles.filter((toggle) => toggle.input.checked).length;
    const total = toggles.length;
    const state = enabledCount > 0 ? "ok" : "info";
    const prefix = enabledCount > 0 ? "✅" : "⚪";
    setupEnabledSummary.className = `status-pill ${state === "ok" ? "pill-ok" : "pill-info"}`;
    setupEnabledSummary.textContent = `${prefix} ${enabledCount}/${total} enabled`;
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
        if (toggle.input) {
          const paramKey = "enable" + key.charAt(0).toUpperCase() + key.slice(1);
          body.set(paramKey, toggle.input.checked ? "true" : "false");
        }
      });
      Object.entries(typeSelects).forEach(([apiKey, select]) => {
        if (select) body.set(apiKey, select.value);
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
        featureEditGeneration += 1;
        if (RC_TOGGLE_KEYS.has(key)) {
          rcChangeGeneration += 1;
        }
        updateToggleStatus(key);
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
  updateEnabledSummary();
  updateAuxLedConfigVisibility();
  setSaveSummary("💾 Auto-save ready", "info");
  renderIdentity({ droidName: "protoartoo", mdnsUseName: false });
  loadIdentity();
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
// Memory Profiler UI (PA_HEAP_PROFILE=1 builds only)
// Polls /api/profiler through PAApi on load and refresh. If endpoint returns
// 404/501, stops polling for the page session. Retries on transient errors
// (503, network) per ADR 0016. Uses Background Poll for cadence and visibility.
// =============================================================================
(() => {
  const card = document.getElementById("profiler-card");
  if (!card) return;

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

  // Latch for 404/501: the endpoint is permanently absent on this build.
  // Once detected, the attempt function will not fetch again, and poll.stop()
  // ensures the interval and visibility listener are removed in normal operation.
  let latched = false;

  let poll;

  async function refreshProfiler() {
    // Once latched on 404/501, do not make any more requests.
    if (latched) return;

    try {
      const result = await window.PAApi.get("/api/profiler");
      // Success: render the profiler data
      card.hidden = false;
      renderProfiler(result.data);
    } catch (error) {
      // Only latch on 404 (Not Found) or 501 (Not Implemented) — these indicate
      // the feature is permanently absent on this build (PA_HEAP_PROFILE=0).
      // Do NOT latch on 503 (Service Unavailable / admission control) — that is
      // a transient condition and the profiler may succeed on retry per ADR 0016.
      if (error instanceof window.PAApi.ApiError && (error.status === 404 || error.status === 501)) {
        latched = true;
        poll.stop();
      }
      // Transient errors (503, network, timeout, etc.): keep polling on cadence.
    }
  }

  poll = window.PageBootstrap.createBackgroundPoll(refreshProfiler, {
    cadenceMs: 5000,
    runOnStart: true,
    refreshOnReturn: true,
  });
  poll.start();
})();
