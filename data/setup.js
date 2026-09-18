// =============================================================================
// setup.js
//
// Setup page controller — hardware component enable/disable toggles and
// servo/AUX component type selectors. Auto-saves on every change.
// =============================================================================

// What a builder calls the board this image runs on. `identity.board` names the
// firmware build and is not an operator-facing word, and the identity manifest
// carries no name beside it, so this is where the two meet. File scope rather
// than inside one of the modules below, because two of them read it - the board
// picture and guided Setup's board step - and a second copy is a second thing to
// keep in step (include/component_registry.inc holds the product names; this is
// the shorter word the surfaces use).
const BOARD_LABELS = {
  artoo_esp32: "Artoo Controller",
  firebeetle2: "FireBeetle 2",
};

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
        return `Could not check ${featureName}. The controller did not report its features.`;
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
          // Versions match but identity is invalid (invalid feature list)
          diagMessage = "This controller's firmware sent a feature list this page cannot read. Uploading the same release again will not fix it.";
        }
      }

      setIdentityDiagnosis(diagMessage);
    } catch (error) {
      console.warn("[setup] diagnosis failed:", error);
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
    window.PAFeatureAvailability.setIdentityError(reason);
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
    // Show placeholder when identity is unavailable
    showBoardPlaceholder("Could not tell which board this is.");
  });

  // ---- Board picture ----

  // identity.board names the firmware build, but a board's pictures are filed
  // under its Component Registry id (include/component_registry.inc, the Body
  // Controller family), and for the Artoo PCB the two tokens differ.
  const BOARD_PRODUCT_IDS = {
    artoo_esp32: "artoo_pcb",
    firebeetle2: "firebeetle2",
  };

  const boardArt = document.getElementById("board-art");
  const boardArtUse = document.getElementById("board-art-use");
  const boardImage = document.getElementById("board-image");
  const boardPlaceholder = document.getElementById("board-image-placeholder");
  const boardPlaceholderText = document.getElementById("board-placeholder-text");

  const showBoardPlaceholder = (text) => {
    if (boardArt) boardArt.classList.add("hidden");
    if (boardImage) boardImage.classList.add("hidden");
    if (boardPlaceholder) {
      boardPlaceholder.classList.remove("hidden");
      if (boardPlaceholderText) boardPlaceholderText.textContent = text;
    }
  };

  const showBoardImage = () => {
    if (boardArt) boardArt.classList.add("hidden");
    if (boardImage) boardImage.classList.remove("hidden");
    if (boardPlaceholder) boardPlaceholder.classList.add("hidden");
  };

  const showBoardArt = () => {
    if (boardImage) boardImage.classList.add("hidden");
    if (boardPlaceholder) boardPlaceholder.classList.add("hidden");
    boardArt.classList.remove("hidden");
  };

  // The picture comes from this build's asset set (ADR 0065), in the order every
  // product card follows: the line drawing, else the photograph, else the
  // placeholder. Which set was built is read from the document, not declared:
  // setup.html inlines the set's sprite, and only the legacy set's carries
  // symbols.
  const updateBoardImage = (identity) => {
    if (!boardImage || !identity || !identity.board) {
      showBoardPlaceholder("Checking which board this is…");
      return;
    }

    const boardId = identity.board;
    const boardLabel = BOARD_LABELS[boardId] || boardId;
    const productId = BOARD_PRODUCT_IDS[boardId];
    if (!productId) {
      // Not a registry product, so no set has a drawing or a photograph of it,
      // and there is no /<id>.webp route to ask.
      showBoardPlaceholder(`${boardLabel} — No photo of this board yet.`);
      return;
    }

    if (boardArt && boardArtUse && document.getElementById(`art-${productId}`)) {
      // Drawn from the page's own sprite: nothing is fetched, so the
      // deferred-asset gate below has nothing to hold back.
      boardArtUse.setAttribute("href", `#art-${productId}`);
      boardArt.setAttribute("aria-label", `${boardLabel} PCB`);
      showBoardArt();
      return;
    }

    const imageSrc = `/${productId}.webp`;

    // Use onload/onerror properties (cleaner than addEventListener, no duplicate removal needed)
    boardImage.onload = () => {
      showBoardImage();
    };

    boardImage.onerror = () => {
      // Board has no photo yet; show placeholder with board name
      showBoardPlaceholder(`${boardLabel} — No photo of this board yet.`);
    };

    // Set alt and title before setting src
    boardImage.alt = `${boardLabel} PCB`;
    boardImage.title = `${boardLabel} PCB`;

    // Use data-deferred-src so image load is gated by announceAssetsOnce(), which ensures
    // /api/events SSE opens before image fetches compete for the connection.
    // Identity can resolve after the one-shot deferred-asset sweep has already run
    // (a section that is visibly waiting to retry already counts as settled), so a
    // late data-deferred-src would never be swept.
    if (window.PAAssetsReady) boardImage.src = imageSrc;
    else boardImage.dataset.deferredSrc = imageSrc;
  };

  // Listen for identity available event and update board image
  window.addEventListener("pa:identity-available", (event) => {
    updateBoardImage(event.detail);
  });

  // Also check cache at initialization time (if identity came before this script ran)
  if (window.PAIdentity) {
    updateBoardImage(window.PAIdentity);
  }

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
        setSaveSummary(`Saved at ${savedAt} · restart required`, "warn");
      } else {
        setFeatureFeedback(`Saved at ${savedAt}`, "success");
        setSaveSummary(`Saved at ${savedAt}`, "ok");
      }
    } catch (error) {
      console.error("[setup] saveFeatures failed:", error);
      setFeatureFeedback(window.PAApi.messageFor(error), "error");
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
  setSaveSummary("Auto-save ready", "info");
  renderIdentity({ droidName: "protoartoo", mdnsUseName: false });
  setIdentityFeedback("Loading controller identity…");
  if (window.PAIdentity) receiveIdentity(window.PAIdentity);
  loadFeatures();
  // --- Serial connection status ---
  const serialS1 = document.getElementById("serial-s1-state");
  const serialS2 = document.getElementById("serial-s2-state");
  const serialS3 = document.getElementById("serial-s3-state");
  const serialS1Light = document.getElementById("serial-s1-light");
  const serialS2Light = document.getElementById("serial-s2-light");
  const serialS3Light = document.getElementById("serial-s3-light");
  const serialStatusLine = document.getElementById("serial-status-line");
  const diagUptime = document.getElementById("diag-uptime");
  const diagHeapFree = document.getElementById("diag-heap-free");
  const diagHeapMin = document.getElementById("diag-heap-min");
  const diagHeapLargest = document.getElementById("diag-heap-largest");
  const diagHeapFreeLight = document.getElementById("diag-heap-free-light");
  const diagHeapMinLight = document.getElementById("diag-heap-min-light");
  const diagHeapLargestLight = document.getElementById("diag-heap-largest-light");
  const diagMemoryNote = document.getElementById("diag-memory-note");

  // A health signal reads as a droid LED and the COLOUR IS THE READING: the
  // light carries it and the value beside it stays ink (CONTEXT.md "Health
  // Signal", "Status Colour"). Before this the state was painted onto the text
  // with element.style.color and spelled with an emoji beside it, which put a
  // colour on a number and a picture in a readout.
  const setLight = (light, state) => {
    if (light) light.className = `indicator ${state}`;
  };

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
      auxLedPreviewNote.textContent = "";
      return;
    }

    // The words carry both readings. Neither sentence is coloured: the swatch
    // beside them already shows the one thing on this row that is a colour, and
    // it is the strip's own live colour rather than a state.
    if (!available) {
      auxLedPreviewText.textContent = `LED strip on AUX${pin} unavailable`;
      auxLedPreviewNote.textContent = "The strip is recorded here, but the controller has no output driver for it.";
      return;
    }

    auxLedPreviewText.textContent = `AUX${pin} LED - ${effect}`;
    auxLedPreviewNote.textContent = `Live colour ${r},${g},${b} with the ${effect} effect.`;
  };

  const renderSerialStatus = (d) => {
    // The same four readings as before, on the same four states: what changed
    // is that the light carries the colour and the words carry the reading.
    // "off" for a lane that is switched off is the grey CONTEXT.md "Health
    // Signal" asks for - a thing never asked reads grey, never green.
    if (serialS1) {
      serialS1.textContent = !d.drive ? "Disabled"
        : d.drive.state === "commanding" ? "Active" : "Enabled / Idle";
      setLight(serialS1Light, d.drive ? "ok" : "off");
    }
    if (serialS2) {
      serialS2.textContent = !d.audio ? "Disabled"
        : d.audio.state === "playing" ? "Playing" : "Enabled / Idle";
      setLight(serialS2Light, d.audio ? "ok" : "off");
    }
    const s2DriverEl = document.getElementById("s2-driver-label");
    if (s2DriverEl) {
      s2DriverEl.textContent = d.audio?.driver || "";
    }
    if (serialS3) {
      const dl = d.dome_link;
      const transport = typeof dl?.transport === "string" ? dl.transport.toUpperCase() : "N/A";
      if (!dl || dl.state === "disabled") {
        serialS3.textContent = "Disabled";
        setLight(serialS3Light, "off");
      } else if (dl.state === "connected") {
        serialS3.textContent = `Connected (${transport}, hb rx ${dl.hb_rx} / tx ${dl.hb_tx})`;
        setLight(serialS3Light, "ok");
      } else if (dl.state === "lost") {
        serialS3.textContent = `Lost (${transport}) — last seen ${dl.last_rx_ms} ms ago`;
        setLight(serialS3Light, "fail");
      } else {
        serialS3.textContent = `Waiting for dome heartbeat (${transport})`;
        setLight(serialS3Light, "warn");
      }
    }
    // Uptime is telemetry, not a health signal: a number that has never been a
    // state carried a green of its own here until this slice took it off.
    if (diagUptime) {
      diagUptime.textContent = formatUptime(d.uptimeMs);
    }

    const heapFreeKb = Math.round((d.heapFree || 0) / 1024);
    const heapMinKb = Math.round((d.heapMin || 0) / 1024);
    const hasLargest = d.heapLargestBlock !== undefined && d.heapLargestBlock !== null;
    const heapLargestKb = hasLargest ? Math.round(d.heapLargestBlock / 1024) : null;

    const t = window.PA_HEAP || {};
    const heapFreeState = heapFreeKb < Math.round((t.freeCritical || 40000) / 1024) ? "critical" : heapFreeKb < Math.round((t.freeWarn || 65000) / 1024) ? "watch" : "good";
    const heapMinState = heapMinKb < Math.round((t.minCritical || 36864) / 1024) ? "critical" : heapMinKb < Math.round((t.minWarn || 53248) / 1024) ? "watch" : "good";
    const heapLargestState = !hasLargest ? "na" : heapLargestKb < Math.round((t.largestCritical || 20480) / 1024) ? "critical" : heapLargestKb < Math.round((t.largestWarn || 36864) / 1024) ? "watch" : "good";

    // The same four states as before, on the same thresholds. "na" is the
    // firmware that reports no largest block at all - never asked, so grey.
    const lampForState = (state) =>
      state === "critical" ? "fail" : state === "watch" ? "warn" : state === "na" ? "off" : "ok";

    if (diagHeapFree) {
      const word = heapFreeState === "critical" ? "Critical" : heapFreeState === "watch" ? "Watch" : "Good";
      diagHeapFree.textContent = `${heapFreeKb} KB ${word}`;
      setLight(diagHeapFreeLight, lampForState(heapFreeState));
    }
    if (diagHeapMin) {
      const word = heapMinState === "critical" ? "Critical" : heapMinState === "watch" ? "Watch" : "Good";
      diagHeapMin.textContent = `${heapMinKb} KB ${word}`;
      setLight(diagHeapMinLight, lampForState(heapMinState));
    }
    if (diagHeapLargest) {
      if (!hasLargest) {
        diagHeapLargest.textContent = "Not reported by this firmware";
      } else {
        const word = heapLargestState === "critical" ? "Fragmented" : heapLargestState === "watch" ? "Watch" : "Good";
        diagHeapLargest.textContent = `${heapLargestKb} KB ${word}`;
      }
      setLight(diagHeapLargestLight, lampForState(heapLargestState));
    }
    if (diagMemoryNote) {
      diagMemoryNote.textContent = `Memory Min is a historical low-water mark since boot; current low-water mark is ${heapMinKb} KB.`;
    }
    renderAuxLedPreview(d);

    setFeedbackState(serialStatusLine, `Updated ${new Date().toLocaleTimeString()}`, "success");
  };

  // Says so on the status line, then rethrows: the surface poll below has to be
  // able to tell a read that landed from one that did not, and swallowing here
  // would tell it every read landed (#360).
  const refreshSerialStatus = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
      renderSerialStatus(result.data);
    } catch (error) {
      setFeedbackState(serialStatusLine, "Status unavailable", "error");
      throw error;
    }
  };

  // SSE-first serial status updates with visibility-aware fallback polling.
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderSerialStatus(payload);
    });
    // One-shot fetch if SSE hasn't delivered a status frame yet. The status
    // line already carries the failure; the stream is what this page reads
    // from after it.
    if (!window.PAStatusStream.getLastStatus()) {
      refreshSerialStatus().catch(() => {});
    }
  } else {
    // Fallback: poll every 5 s, suspended while the tab is hidden and while the
    // operator is reading another surface -- the shell stops it on the way out
    // and starts it again on the way back (ADR 0048, #360). The failed read is
    // PASurface.poll()'s to report, so that a refresh that never landed does
    // not take the "showing what this screen last read" note down (#360).
    window.PASurface.poll(refreshSerialStatus, {
      cadenceMs: 5000,
      runOnStart: true,
      refreshOnReturn: true,
    }).start();
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
    feedback.className = variant ? `feedback ${variant}` : 'feedback';
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
    const domeEsc = cfg?.domeEsc || {};
    const protoR2link = cfg?.protoR2link || {};
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
      ['domeEsc', 'enableDomeEsc'],
      ['rcCh1', 'enableRcCh1'], ['rcCh2', 'enableRcCh2'], ['rcCh3', 'enableRcCh3'],
      ['rcCh4', 'enableRcCh4'], ['rcCh5', 'enableRcCh5'], ['rcCh6', 'enableRcCh6'],
      ['drive', 'enableDrive'],
      ['audio', 'enableAudio'],
      ['protoR2link', 'enableProtoR2link'],
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

    if (domeEsc.neutralUs !== undefined) p.set('domeEscNeutralUs', domeEsc.neutralUs);
    if (domeEsc.minPulseUs !== undefined) p.set('domeEscMinPulseUs', domeEsc.minPulseUs);
    if (domeEsc.maxPulseUs !== undefined) p.set('domeEscMaxPulseUs', domeEsc.maxPulseUs);
    if (domeEsc.speedLimitPct !== undefined) p.set('domeEscSpeedLimitPct', domeEsc.speedLimitPct);
    if (domeEsc.rndEnable !== undefined) p.set('domeEscRndEnable', domeEsc.rndEnable ? 'true' : 'false');
    if (domeEsc.rndSpeedPct !== undefined) p.set('domeEscRndSpeedPct', domeEsc.rndSpeedPct);
    if (domeEsc.rndPauseMin !== undefined) p.set('domeEscRndPauseMin', domeEsc.rndPauseMin);
    if (domeEsc.rndPauseMax !== undefined) p.set('domeEscRndPauseMax', domeEsc.rndPauseMax);
    if (domeEsc.rndMoveMs !== undefined) p.set('domeEscRndMoveMs', domeEsc.rndMoveMs);
    if (protoR2link.wifiPeerIp !== undefined) p.set('protoR2linkWifiPeerIp', protoR2link.wifiPeerIp);

    if (sys.logLevel !== undefined) p.set('logLevel', sys.logLevel);

    // The Sound Component Member: which module is actually fitted. The saved
    // choice, not `activeMember`, which is the one the droid booted with and is
    // not a setting anybody chose (src/web/api_config.cpp).
    if (components.audio?.member !== undefined) p.set('soundMember', components.audio.member);

    // The Droid Build (ADR 0047). Each half goes as a PAIR, because a variant
    // means nothing against another design and the controller refuses a request
    // that sends one without the other. The Fitted Parts go as the id list the
    // write side takes; an empty one is a real answer - a droid with nothing
    // fitted yet - and is sent as such.
    const build = cfg?.droidBuild || {};
    if (build.domeDesign !== undefined && build.domeVariant !== undefined) {
      p.set('domeDesign', build.domeDesign);
      p.set('domeVariant', build.domeVariant);
    }
    if (build.bodyDesign !== undefined && build.bodyVariant !== undefined) {
      p.set('bodyDesign', build.bodyDesign);
      p.set('bodyVariant', build.bodyVariant);
    }
    if (Array.isArray(build.fitted)) p.set('fittedParts', build.fitted.join(','));

    // Guided Setup's record travels with the backup like any other config key
    // (operator, 2026-09-17 on #371). A configured backup restored without it
    // would read as "never asked" for every category, which is the exact untruth
    // the record exists to prevent - and a pre-Setup backup restoring a droid
    // that guided Setup then offers itself to is honest rather than a surprise.
    // No exclusion list, and one rule decides it: Setup appears when the droid is
    // not set up.
    const guided = cfg?.guidedSetup || {};
    if (guided.run !== undefined) p.set('guidedSetupRun', guided.run);
    if (Array.isArray(guided.visited)) {
      // A run that showed nothing is a real answer and is written as the
      // sentinel; an empty form value would not survive the round trip as one.
      p.set('guidedSetupVisited', guided.visited.length > 0 ? guided.visited.join(',') : '-');
    }

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

  // The same two thresholds as before, answering with a Status Colour state
  // rather than with a hard-coded hex: a colour literal outside :root is a
  // defect (CONTEXT.md "Status Colour"), and these three were Material's own
  // green, amber and red rather than the droid's.
  function hwmState(hwm) {
    if (hwm > 2048) return "ok";
    if (hwm > 1024) return "warn";
    return "fail";
  }

  function fragState(ratio) {
    if (ratio < 0.30) return "ok";
    if (ratio < 0.50) return "warn";
    return "fail";
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
      bar.className = `prof-bar is-${fragState(d.fragRatio)}`;
    }
    if (lbl) {
      const health = d.fragRatio < 0.30 ? "Healthy" : d.fragRatio < 0.50 ? "Watch" : "Critical";
      lbl.textContent = health + " — fragmentation " + pct.toFixed(1) + "% (1 - largest/free)";
    }

    // Task stack HWM table. The state is a signal light in its own cell, so the
    // word beside it stays ink: the table reads down the lights the way the
    // health grid does.
    const hwmTbody = document.getElementById("prof-hwm-tbody");
    if (hwmTbody && Array.isArray(d.taskStacks)) {
      hwmTbody.innerHTML = d.taskStacks.map(t => `<tr>
          <td>${t.name}</td>
          <td class="num">${kb(t.hwmBytes)}</td>
          <td class="num"><span class="indicator ${hwmState(t.hwmBytes)}"></span>${t.status.toUpperCase()}</td>
        </tr>`).join("");
    }

    // Task heap table (Tier 2 — only when taskHeap present)
    const heapSection = document.getElementById("prof-task-heap-section");
    const heapTbody = document.getElementById("prof-heap-tbody");
    if (heapSection && heapTbody && Array.isArray(d.taskHeap) && d.taskHeap.length > 0) {
      heapSection.hidden = false;
      heapTbody.innerHTML = d.taskHeap.map(t => `<tr>
        <td>${t.name}</td>
        <td class="num">${kb(t.current)}</td>
        <td class="num">${kb(t.peak)}</td>
        <td class="num">${t.heapCount}</td>
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
        <td>${s.label}</td>
        <td class="num">${kb(s.heapFree)}</td>
        <td class="num">${kb(s.largestBlock)}</td>
        <td class="num">${s.ts}</td>
      </tr>`).join("");
    }
  }

  // Says so in the feedback line, then rethrows. The rethrow is what the poll
  // below reads: a profiler reading nobody could take must not come back from
  // the surface registry as a fresh one (#360).
  async function refreshProfiler() {
    try {
      const result = await window.PAApi.get("/api/profiler");
      renderProfiler(result.data);
      if (feedback) {
        feedback.textContent = `Memory readings updated at ${new Date().toLocaleTimeString()}`;
        feedback.className = "feedback success";
      }
    } catch (error) {
      if (feedback) {
        feedback.textContent = `Memory readings unavailable: ${window.PAApi.messageFor(error)}`;
        feedback.className = "feedback warning";
      }
      throw error;
    }
  }

  // The memory profiler is the other surface an operator opens when something
  // is already wrong, so it is owned by this surface: the shell stops it the
  // moment they read something else and starts it again on the way back
  // (ADR 0048, #360). Two answers gate it and they are asked in different
  // places -- whether this build HAS a profiler is the manifest's, below;
  // whether asking is wanted at all is the shell's.
  const poll = window.PASurface.poll(refreshProfiler, {
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

// =============================================================================
// Guided Setup - the first-run takeover (#351, #297, CONTEXT.md "Setup")
//
// A controller arrives provisioned and inert: every component toggle defaults
// false, so nothing on the droid is described. This is the one guided pass that
// asks what the builder has, in the order they built it, continuing the flow
// that started when they gave the droid their WiFi.
//
// It is a TAKEOVER, not a destination. While the run is live this surface shows
// the run's chrome and one step at a time; when the run ends - by reaching the
// last step, or by the builder stopping - the chrome goes and this page is its
// ordinary cards again. The run never re-opens. Taking the Setup entry out of
// the nav on the way is data/shell.js's half and is not this module's.
//
// THE VISITED RECORD is the load-bearing part. A run with defaults answers
// itself: every question already has a value, so a tick beside one would claim a
// confirmation the builder never gave - and ours default to "not fitted", which
// is a statement about their droid. So a step is VISITED once it has actually
// been on screen, stored on the controller as an ordinary config key, and until
// then its answer renders hollow. The answer still shows, because the default is
// real; it has just not been looked at
// (r2d2-astromech-simulator v1.79.0, src/js/config/wizard.js:2216).
// =============================================================================
(() => {
  const head = document.getElementById("wizard-head");
  if (!head) return;

  // A step's text may be a string or a reader, because one of them names
  // something this page only learns at runtime. Resolved in one place so a step
  // that grows a reader does not also grow a branch at every site that shows it.
  const textOf = (value) => (typeof value === "function" ? value() : value);

  const checked = (id) => Boolean(document.getElementById(id)?.checked);
  const fittedOrNot = (id) => (checked(id) ? "Fitted" : "Not fitted");
  const countFitted = (ids) => ids.filter(checked).length;

  // ---------------------------------------------------------------------------
  // The steps - ONE ordered array
  //
  // The rail, the header, the footer, back and next, and every count read this
  // and nothing else. Inserting a step is adding a row: #368 puts the Droid
  // Build at position three and nothing below this array has to move.
  //
  // A LEADING UNDERSCORE means "in the run, not in the count" - a step that is
  // shown rather than asked. Today that is the board, which is whichever one this
  // firmware was built for and so was never a question; the convention
  // generalises to any later step that is prose rather than a question, without a
  // second list to keep in step (r2d2-astromech-simulator v1.79.0,
  // src/js/config/wizard.js:50).
  //
  // Four fields, all required, and `why` is CONTENT rather than a doc comment: a
  // step nobody can explain cannot exist, and an explanation on the same object
  // cannot drift from the step it explains. A fifth field - when this answer
  // takes effect - is #370's, and is deliberately not guessed at here.
  //
  // `answer` reads the controls the step itself shows, so the rail says what the
  // builder is looking at rather than what a second copy of the state believes.
  // ---------------------------------------------------------------------------
  const STEPS = [
    {
      key: "wifi",
      title: "WiFi",
      q: "Which network does this droid join?",
      why: "The droid is either on a network of yours or broadcasting its own. This is the link every screen you drive it from comes down, and the one new firmware arrives over.",
      answer: () => wifiAnswer,
    },
    {
      key: "_board",
      title: "Body Controller",
      // Composed, never typed: writing the board's own name into a string here
      // would be one more of the hardcoded artoo-only sentences #348 is counting,
      // and only what is SHOWN may differ between boards (ADR 0065).
      q: () => (boardLabel ? `This is your ${boardLabel}.` : "This is the board doing the work."),
      why: "Nothing to pick here — the board is whichever one this firmware was built for. It is named so the rest of the run reads against the right thing: everything after this describes something plugged into this board.",
      answer: () => boardLabel,
    },
    {
      key: "drive",
      title: "Foot Drive",
      q: "What moves the feet?",
      why: "Leave this off and the droid is a statue — the sticks move, the wheels don't.",
      answer: () => fittedOrNot("enable-drive"),
    },
    {
      key: "domerot",
      title: "Dome Rotation",
      q: "What turns the dome?",
      why: "Off, the dome sits still through every sequence, however the show is written.",
      answer: () => fittedOrNot("enable-dome-esc"),
    },
    {
      key: "domectl",
      title: "Dome Controller",
      q: "What runs the board up in the dome?",
      why: "This is the link that carries light, panel and sound cues up to the dome's own board. Off, the body still drives — the dome just stops listening.",
      answer: () => fittedOrNot("enable-protor2link"),
    },
    {
      key: "servos",
      title: "Body servo controller",
      q: "What moves the arms and the spare outputs?",
      why: "The outputs in the body: two utility arms, and three spare lines for whatever you have on them — a servo, a light, a smoke unit.",
      answer: () => {
        const fitted = countFitted([
          "enable-arm1",
          "enable-arm2",
          "enable-aux1",
          "enable-aux2",
          "enable-aux3",
        ]);
        return fitted === 0 ? "None fitted" : `${fitted} fitted`;
      },
    },
    {
      key: "rc",
      title: "Radio Controller",
      q: "What do you drive it with?",
      why: "The channels you tick are the ones the droid listens to. A channel with nothing wired to it is better left off than left guessing.",
      answer: () => {
        const fitted = countFitted([
          "enable-rc-ch1",
          "enable-rc-ch2",
          "enable-rc-ch3",
          "enable-rc-ch4",
          "enable-rc-ch5",
          "enable-rc-ch6",
        ]);
        return fitted === 0 ? "No channels" : `${fitted} channels`;
      },
    },
    {
      key: "sound",
      title: "Sound",
      q: "What gives the droid its voice?",
      why: "Off, sequences still run start to finish, in silence.",
      answer: () => fittedOrNot("enable-audio"),
    },
    {
      key: "name",
      title: "Name",
      q: "What is this droid called?",
      why: "The name is stored on the droid rather than in this browser, so a second computer or a cleared cache meets the same droid.",
      answer: () => document.getElementById("droid-name-input")?.value || "",
    },
  ];

  // A step is in the COUNT unless its key says otherwise. Every number the run
  // shows is arithmetic on the array above and none is ever typed: the planning
  // for this run said nine steps in one place and ten in another, and a typed
  // total is exactly how that becomes a line on screen that lies.
  const isQuestion = (step) => step.key.charAt(0) !== "_";
  const questionCount = () => STEPS.filter(isQuestion).length;
  // How many questions the run has reached by `index`, counting the step at it.
  const questionsThrough = (index) => STEPS.slice(0, index + 1).filter(isQuestion).length;

  // ---------------------------------------------------------------------------
  // What the controller says about the run
  // ---------------------------------------------------------------------------
  const RUN_NOT_RUN = "not-run";
  const RUN_SKIPPED = "skipped";
  const RUN_COMPLETED = "completed";

  let runState = RUN_NOT_RUN;
  // A droid configured before this record existed carries no record at all
  // (include/guided_setup.h). Its answers are real, once-considered ones, so it
  // is not walked through a first run and its categories are not reported as
  // never asked. The rule lives here rather than in firmware because only this
  // end knows what the steps are.
  let grandfathered = false;
  const visited = new Set();
  let current = 0;
  let wifiAnswer = "";
  let boardLabel = "";
  let saveTimer = null;
  let ending = false;

  const runHasEnded = () => runState !== RUN_NOT_RUN || grandfathered;

  const configuredBeforeTheRecordExisted = (config) => {
    const components = config?.components || {};
    return Object.values(components).some((entry) => entry && entry.enabled === true);
  };

  // ---------------------------------------------------------------------------
  // What is on screen
  //
  // Three states, and the middle one is why the page does not flash: until the
  // controller has answered, this surface says it is reading rather than showing
  // a configuration page that may be about to be replaced by a run.
  //
  // A card is shown during the run when it CONTAINS the current step's body, so
  // the mapping from step to screen is one attribute in the markup and there is
  // no second list of ids here to keep in step with it.
  // ---------------------------------------------------------------------------
  // add/remove rather than toggle(cls, force), which is the idiom the board
  // picture above already uses on this same class.
  const show = (element, visible) => {
    if (!element) return;
    if (visible) element.classList.remove("hidden");
    else element.classList.add("hidden");
  };

  // Both of these are this surface's own vocabulary - the Operator Shell's
  // chrome carries no .card and nothing else in the document carries a
  // data-setup-step - so they are asked of the document rather than of a
  // container this module would otherwise have to find. Under the shell that
  // container is not the body, and reaching for it is how a surface ends up
  // holding a reference to the shell's furniture.
  const isCard = (element) => Boolean(element?.classList?.contains("card"));

  const cards = () => Array.from(document.querySelectorAll(".card"));

  const stepHosts = () => Array.from(document.querySelectorAll("[data-setup-step]"));

  // The card a step's body sits in. setup.html nests no card inside another, so
  // the first one above the body is the one that has to be on screen with it.
  const cardFor = (host) => {
    let node = host;
    while (node && !isCard(node)) {
      node = node.parentElement;
    }
    return node || null;
  };

  const applyLayout = (phase) => {
    const checking = document.getElementById("wizard-checking");
    const foot = document.getElementById("wizard-foot");
    show(checking, phase === "checking");
    show(head, phase === "running");
    show(foot, phase === "running");

    // Only a run that is actually on screen has a current step. While the
    // controller is still being asked, there is no step to show and no card to
    // show it in - and saying that once here is what keeps the two loops below
    // from each having to remember it.
    const hosts = stepHosts();
    const currentHost =
      phase === "running"
        ? hosts.find((host) => host.dataset.setupStep === STEPS[current]?.key)
        : null;
    const currentCard = currentHost ? cardFor(currentHost) : null;

    cards().forEach((card) => {
      if (card === checking || card === head || card === foot) return;
      if (phase === "checking") {
        show(card, false);
        return;
      }
      if (phase === "running") {
        show(card, card === currentCard);
        return;
      }
      // The run has ended: the page is its own cards again, minus the ones that
      // only ever belonged to the run.
      show(card, card.dataset.setupRunOnly === undefined);
    });

    hosts.forEach((host) => {
      show(host, phase === "ended" || host === currentHost);
    });
  };

  // ---------------------------------------------------------------------------
  // The run's chrome
  // ---------------------------------------------------------------------------
  const railHost = document.getElementById("wizard-rail");
  const legend = document.getElementById("wizard-legend");
  const questionText = document.getElementById("wizard-question");
  const stepName = document.getElementById("wizard-step-name");
  const whyText = document.getElementById("wizard-why");
  const position = document.getElementById("wizard-position");
  const backButton = document.getElementById("wizard-back");
  const nextButton = document.getElementById("wizard-next");
  const stopButton = document.getElementById("wizard-stop");
  const feedback = document.getElementById("wizard-feedback");

  const setFeedback = (message, variant = "") => {
    if (!feedback) return;
    feedback.textContent = message;
    feedback.className = variant ? `feedback ${variant}` : "feedback";
  };

  const renderRail = () => {
    if (!railHost) return;
    railHost.innerHTML = "";
    const total = questionCount();
    let anyUnseen = false;

    STEPS.forEach((step, index) => {
      const question = isQuestion(step);
      const seen = visited.has(step.key);
      const answer = String(step.answer?.() || "");
      if (question && !seen) anyUnseen = true;

      const chip = document.createElement("button");
      chip.type = "button";
      chip.className = "wizard-chip";
      if (index === current) chip.classList.add("is-current");
      if (index < current) chip.classList.add("is-behind");
      chip.dataset.step = step.key;

      const label = document.createElement("span");
      label.className = "wizard-chip-label";
      if (question) {
        // Hollow until the question has actually been on screen. The answer
        // below still shows either way, because the default is a real value -
        // it has just not been looked at.
        //
        // One shape in two states, and both out of Geometric Shapes on purpose:
        // a check mark is U+2713, inside the Dingbats block, and an operator
        // surface carries no pictograph (ADR 0066, and
        // tools/check_surface_anatomy.py enforces it). The project's own SVG
        // sprite has no tick to borrow either, and adding one means editing
        // data/shell.js. Filled against hollow says the same thing, restyles
        // with the text around it, and needs nobody's permission.
        const tick = document.createElement("span");
        tick.className = seen ? "wizard-tick" : "wizard-tick is-unseen";
        tick.textContent = seen ? "●" : "○";
        label.appendChild(tick);
      }
      const title = document.createElement("span");
      title.className = "wizard-chip-title";
      title.textContent = step.title;
      label.appendChild(title);
      chip.appendChild(label);

      // Every chip gets the answer slot, even when it has nothing to put in it:
      // the reserved height is in the stylesheet, so a chip does not grow a line
      // the moment it has something to say.
      const answerSlot = document.createElement("span");
      answerSlot.className = "wizard-chip-answer";
      answerSlot.textContent = answer;
      chip.appendChild(answerSlot);

      chip.title = question
        ? `Question ${questionsThrough(index)} of ${total} — ${step.title}${answer ? ` · ${answer}` : ""}${
            seen ? "" : "\nNot asked yet — this is the default, not something you have confirmed."
          }`
        : `${step.title} — shown, not asked${answer ? ` · ${answer}` : ""}`;

      chip.addEventListener("click", () => goTo(index));
      railHost.appendChild(chip);
    });

    // The hollow mark needs a legend in visible text, not only in a tooltip: a
    // bench tablet has no hover at all (ADR 0059). It goes when every question
    // has been on screen, because then it explains nothing.
    show(legend, anyUnseen);
    if (legend && anyUnseen) {
      legend.textContent = "A filled mark is an answer you gave. A hollow one is a question you have not been asked yet, showing the default.";
    }
  };

  const renderStep = () => {
    const step = STEPS[current];
    if (!step) return;
    if (questionText) questionText.textContent = textOf(step.q);
    if (stepName) stepName.textContent = step.title;
    if (whyText) whyText.textContent = textOf(step.why);

    // Position and escape, and nothing else. When an answer takes effect is a
    // different question for every step and belongs beside the step that owns it
    // (#370); a blanket promise here would be false for at least three of them.
    const total = questionCount();
    const escape = "you can stop at any step, and stopping ends the run";
    if (position) {
      position.textContent = isQuestion(step)
        ? `Question ${questionsThrough(current)} of ${total} · ${escape}.`
        : `${step.title} — shown, not asked. Question ${Math.min(questionsThrough(current) + 1, total)} of ${total} is next · ${escape}.`;
    }

    const last = current >= STEPS.length - 1;
    if (backButton) backButton.disabled = current === 0;
    if (nextButton) nextButton.textContent = last ? "Finish" : "Next";
    renderRail();
  };

  // ---------------------------------------------------------------------------
  // The visited record
  //
  // A step becomes visited the moment it is on screen, and the record is written
  // to the controller like any other config key, so Backup and Restore carries it
  // (operator, 2026-09-17 on #371). Debounced rather than written per step: a
  // builder rattling through with Next would otherwise spend one flash write per
  // press, and the record only has to be right by the time they stop.
  // ---------------------------------------------------------------------------
  const visitedParam = () => (visited.size > 0 ? [...visited].join(",") : "-");

  const saveVisited = async () => {
    if (!window.PAApi) return;
    try {
      await window.PAApi.postForm(
        "/api/config",
        { guidedSetupVisited: visitedParam() },
        { timeoutMs: 5000 },
      );
    } catch (error) {
      // Not swallowed: if the controller did not take the record, a later screen
      // would report questions the builder WAS asked as never asked, which is the
      // one thing this record exists to prevent.
      console.error("[setup] guided setup visited save failed:", error);
      setFeedback(
        `The droid did not record which questions it has shown you: ${window.PAApi.messageFor(error)}`,
        "error",
      );
    }
  };

  const markVisited = (step) => {
    if (!step || visited.has(step.key)) return;
    visited.add(step.key);
    clearTimeout(saveTimer);
    saveTimer = setTimeout(saveVisited, 400);
  };

  // ---------------------------------------------------------------------------
  // Moving through the run
  // ---------------------------------------------------------------------------
  const goTo = (index) => {
    if (runHasEnded()) return;
    current = Math.max(0, Math.min(STEPS.length - 1, index));
    markVisited(STEPS[current]);
    applyLayout("running");
    renderStep();
  };

  // Skipping IS finishing (#297), so both of these end the run for good. They
  // stay two different facts about the droid: the surfaces that report on it
  // afterwards are owed the difference between a builder who answered and one who
  // walked away at question two.
  const endRun = async (how) => {
    if (ending || !window.PAApi) return;
    ending = true;
    clearTimeout(saveTimer);
    [backButton, nextButton, stopButton].forEach((button) => {
      if (button) button.disabled = true;
    });
    setFeedback("Saving…");
    try {
      await window.PAApi.postForm(
        "/api/config",
        { guidedSetupRun: how, guidedSetupVisited: visitedParam() },
        { timeoutMs: 5000 },
      );
    } catch (error) {
      console.error("[setup] guided setup run end failed:", error);
      setFeedback(
        `The droid did not record that setup is done, so the run is still open: ${window.PAApi.messageFor(error)}`,
        "error",
      );
      ending = false;
      [backButton, nextButton, stopButton].forEach((button) => {
        if (button) button.disabled = false;
      });
      if (backButton) backButton.disabled = current === 0;
      return;
    }
    runState = how;
    ending = false;
    setFeedback("");
    applyLayout("ended");
  };

  if (backButton) backButton.addEventListener("click", () => goTo(current - 1));
  if (nextButton) {
    nextButton.addEventListener("click", () => {
      if (current >= STEPS.length - 1) {
        endRun(RUN_COMPLETED);
        return;
      }
      goTo(current + 1);
    });
  }
  if (stopButton) stopButton.addEventListener("click", () => endRun(RUN_SKIPPED));

  // An answer changed under the builder's hand, so the rail says what they are
  // looking at rather than what it said when the step opened.
  document.getElementById("feature-form")?.addEventListener("change", () => {
    if (!runHasEnded()) renderRail();
  });
  document.getElementById("droid-name-input")?.addEventListener("input", () => {
    if (!runHasEnded()) renderRail();
  });

  // ---------------------------------------------------------------------------
  // What the run opens on
  // ---------------------------------------------------------------------------
  const renderWifiStep = (config) => {
    const wifi = config?.wifi || {};
    const state = document.getElementById("wizard-wifi-state");
    const prose = document.getElementById("wizard-wifi-prose");
    const ssid = String(wifi.staSsid || "");
    if (!wifi.provisioned) {
      wifiAnswer = "Its own network";
      if (state) state.textContent = "not given a network yet";
      if (prose) {
        prose.textContent =
          "You are on the droid's own network — it has none of yours saved. Give it one and you can reach it from anywhere the network reaches.";
      }
      return;
    }
    if (wifi.mode === "standalone_ap") {
      wifiAnswer = "Standalone AP Mode";
      if (state) state.textContent = "its own network, on purpose";
      if (prose) {
        prose.textContent =
          "This droid is set to broadcast its own network rather than join one of yours. That is a choice, not a gap — you reach it by connecting to the droid.";
      }
      return;
    }
    wifiAnswer = ssid || "WiFi Client Mode";
    if (state) state.textContent = ssid ? `joins ${ssid}` : "set to join a network";
    if (prose) {
      prose.textContent = ssid
        ? `This droid joins ${ssid}. That is the network every screen you drive it from comes down.`
        : "This droid is set to join a network of yours.";
    }
  };

  const applyIdentity = (identity) => {
    const board = identity?.board;
    if (!board) return;
    boardLabel = BOARD_LABELS[board] || String(board);
    if (!runHasEnded()) renderStep();
  };

  window.addEventListener("pa:identity-available", (event) => applyIdentity(event.detail));
  if (window.PAIdentity) applyIdentity(window.PAIdentity);

  // Synchronously, before anything is fetched: this surface does not yet know
  // whether it is a guided run or a configuration page, and showing either one
  // and swapping it a moment later is a page that flickers on every visit.
  // Saying it is reading is what the rest of this page already does while it
  // waits, and it is the honest answer.
  applyLayout("checking");

  const loadRun = async () => {
    const result = await window.PAApi.get("/api/config", { timeoutMs: 5000 });
    const config = result.data;
    const guided = config?.guidedSetup || {};

    runState = typeof guided.run === "string" ? guided.run : RUN_NOT_RUN;
    grandfathered = guided.recorded === false && configuredBeforeTheRecordExisted(config);
    (Array.isArray(guided.visited) ? guided.visited : []).forEach((key) => visited.add(String(key)));

    renderWifiStep(config);

    if (runHasEnded()) {
      applyLayout("ended");
      return true;
    }
    goTo(current);
    return true;
  };

  // Registered as a bootstrap section rather than fetched loose: a read this
  // surface cannot render without belongs to the Page Recovery View, which
  // already says what is missing and retries it, so a failure here is not a
  // page that sits silently on "reading this droid".
  if (window.PABootstrap) {
    window.PABootstrap.registerSection("setup-guided-run", loadRun, {
      label: "whether this droid has been set up",
    });
  } else {
    // No bootstrap to hand the failure to, so this path says so where the
    // builder is looking rather than leaving the card reading "finding out"
    // for ever.
    loadRun().catch((error) => {
      console.error("[setup] guided run unavailable:", error);
      const checking = document.getElementById("wizard-checking");
      const prose = checking?.querySelector(".prose");
      if (prose) {
        prose.textContent = `Could not ask the droid whether it has been set up: ${window.PAApi.messageFor(error)}`;
      }
    });
  }
})();
