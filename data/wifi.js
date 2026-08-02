// =============================================================================
// wifi.js
//
// WiFi page — primary operator surface for Device WiFi Settings.
// Reads:
//   GET /api/config — password-safe saved WiFi settings + pendingApply
//   GET /api/wifi   — active connection diagnostics
// Writes:
//   POST /api/wifi  — stages Device WiFi Settings; no live hardware toggle
// =============================================================================
(() => {
  const POLL_INTERVAL_MS = 10000;
  const WIFI_MODE_CLIENT = "client";
  const WIFI_MODE_STANDALONE_AP = "standalone_ap";
  const DEFAULT_HOSTNAME = "artoo";

  const form = document.getElementById("wifi-settings-form");
  const reloadButton = document.getElementById("reload-wifi-button");
  const saveButton = document.getElementById("wifi-save-settings-button");
  const settingsFeedback = document.getElementById("wifi-settings-feedback");
  const pendingSummary = document.getElementById("wifi-pending-summary");
  const postureDesc = document.getElementById("wifi-posture-desc");
  const applyGuidance = document.getElementById("wifi-apply-guidance");

  const provisioningState = document.getElementById("wifi-provisioning-state");
  const activeMode = document.getElementById("wifi-active-mode");
  const clientState = document.getElementById("wifi-client-state");
  const staIp = document.getElementById("wifi-sta-ip");
  const apIp = document.getElementById("wifi-ap-ip");
  const wifiSignal = document.getElementById("wifi-signal");
  const wifiRssi = document.getElementById("wifi-rssi");
  const activeSummaryMode = document.getElementById("wifi-active-summary-mode");
  const activeSummaryNetwork = document.getElementById("wifi-active-summary-network");
  const activeSummaryAddress = document.getElementById("wifi-active-summary-address");
  const savedSummaryTitle = document.getElementById("wifi-saved-summary-title");
  const savedSummaryMode = document.getElementById("wifi-saved-summary-mode");
  const savedSummarySta = document.getElementById("wifi-saved-summary-sta");
  const savedSummaryAp = document.getElementById("wifi-saved-summary-ap");

  const modeClient = document.getElementById("wifi-mode-client");
  const modeStandaloneAp = document.getElementById("wifi-mode-standalone-ap");
  const staSsid = document.getElementById("wifi-sta-ssid");
  const staPassword = document.getElementById("wifi-sta-password");
  const apSsid = document.getElementById("wifi-ap-ssid");
  const apPassword = document.getElementById("wifi-ap-password");
  const staPasswordHint = document.getElementById("wifi-sta-password-hint");
  const apPasswordHint = document.getElementById("wifi-ap-password-hint");

  const fields = {
    wifiMode: { input: modeClient, error: null },
    staSsid: { input: staSsid, error: document.getElementById("wifi-sta-ssid-error") },
    staPassword: { input: staPassword, error: document.getElementById("wifi-sta-password-error") },
    apSsid: { input: apSsid, error: document.getElementById("wifi-ap-ssid-error") },
    apPassword: { input: apPassword, error: document.getElementById("wifi-ap-password-error") },
  };

  const state = {
    identity: { droidName: DEFAULT_HOSTNAME, mdnsUseName: false },
    wifiConfig: null,
    diagnostics: null,
  };

  const signalLabel = (rssi) => {
    const value = Number(rssi || 0);
    if (!value) return "--";
    if (value >= -67) return `✅ Excellent (${value} dBm)`;
    if (value >= -75) return `✅ Good (${value} dBm)`;
    if (value >= -85) return `⚠️ Fair (${value} dBm)`;
    return `❌ Poor (${value} dBm)`;
  };

  const modeLabel = (mode) =>
    mode === WIFI_MODE_STANDALONE_AP ? "Standalone AP Mode" : "WiFi Client Mode";

  const mdnsHost = () => {
    const name = state.identity?.mdnsUseName
      ? String(state.identity?.droidName || DEFAULT_HOSTNAME)
      : DEFAULT_HOSTNAME;
    const normalized = name.toLowerCase().replace(/[^a-z0-9-]/g, "") || DEFAULT_HOSTNAME;
    return `${normalized}.local`;
  };

  const setFeedback = (message, variant = "") => {
    if (!settingsFeedback) return;
    settingsFeedback.textContent = message;
    settingsFeedback.className = variant ? `feedback mt-12 ${variant}` : "feedback mt-12";
  };

  const setPendingSummary = (text, stateName = "info") => {
    if (!pendingSummary) return;
    const classMap = { ok: "pill-ok", warn: "pill-warn", error: "pill-error", info: "pill-info" };
    pendingSummary.className = `status-pill ${classMap[stateName] || classMap.info} status-pill-compact`;
    pendingSummary.textContent = text;
  };

  const setFieldError = (name, message = "") => {
    const field = fields[name];
    if (!field) return;
    if (field.error) {
      field.error.textContent = message;
    }
    if (field.input) {
      field.input.setAttribute("aria-invalid", message ? "true" : "false");
    }
  };

  const clearFieldErrors = () => {
    Object.keys(fields).forEach((name) => setFieldError(name, ""));
  };

  const fieldForMessage = (message) => {
    const errorFields = ["staSsid", "staPassword", "apSsid", "apPassword", "wifiMode"];
    const match = errorFields.find((name) => new RegExp(name, "i").test(message));
    if (match) return match;
    return "";
  };

  const selectedMode = () =>
    modeStandaloneAp?.checked ? WIFI_MODE_STANDALONE_AP : WIFI_MODE_CLIENT;

  const syncModeOptions = () => {
    document.querySelectorAll(".wifi-mode-option").forEach((option) => {
      const input = option.querySelector("input[type='radio']");
      option.classList.toggle("is-selected", Boolean(input?.checked));
    });
  };

  const renderSettings = (wifi) => {
    if (!wifi) return;
    state.wifiConfig = wifi;

    if (modeClient) modeClient.checked = wifi.mode !== WIFI_MODE_STANDALONE_AP;
    if (modeStandaloneAp) modeStandaloneAp.checked = wifi.mode === WIFI_MODE_STANDALONE_AP;
    syncModeOptions();

    if (staSsid && document.activeElement !== staSsid) {
      staSsid.value = wifi.staSsid || "";
    }
    if (apSsid && document.activeElement !== apSsid) {
      apSsid.value = wifi.apSsid || "";
    }
    if (staPassword) staPassword.value = "";
    if (apPassword) apPassword.value = "";
    if (staPasswordHint) {
      staPasswordHint.textContent = wifi.staPasswordSet
        ? "Saved password present; blank keeps it."
        : "Blank saves no client password.";
    }
    if (apPasswordHint) {
      apPasswordHint.textContent = wifi.apPasswordSet
        ? "Saved AP password present; blank keeps it."
        : "Blank leaves the AP open; 8..63 characters enables WPA2.";
    }
  };

  const renderActiveVsSaved = (activeModeText) => {
    const wifi = state.wifiConfig || {};
    const diag = state.diagnostics || {};
    const staConnected = Boolean(diag.staConnected);
    const staEnabled = Boolean(diag.staEnabled);
    const apAddress = diag.apIp ? `http://${diag.apIp}` : "--";
    const staAddress = staConnected && diag.staIp ? `http://${diag.staIp}` : "--";
    const hostAddress = `http://${mdnsHost()}`;
    const activeNetwork = staEnabled
      ? (staConnected ? "Connected client" : "Client not connected")
      : (diag.apSsid || wifi.apSsid || "--");

    if (activeSummaryMode) {
      activeSummaryMode.textContent = activeModeText;
    }
    if (activeSummaryNetwork) {
      activeSummaryNetwork.textContent = activeNetwork;
    }
    if (activeSummaryAddress) {
      activeSummaryAddress.textContent = staConnected ? `${hostAddress} / ${staAddress}` : apAddress;
    }
    if (savedSummaryTitle) {
      savedSummaryTitle.textContent = wifi.pendingApply ? "Saved after reboot" : "Saved settings";
    }
    if (savedSummaryMode) {
      savedSummaryMode.textContent = wifi.provisioned ? modeLabel(wifi.mode) : "Not provisioned";
    }
    if (savedSummarySta) {
      savedSummarySta.textContent = wifi.staSsid || "--";
    }
    if (savedSummaryAp) {
      savedSummaryAp.textContent = wifi.apSsid || diag.apSsid || "--";
    }
  };

  const renderPosture = () => {
    const wifi = state.wifiConfig;
    const diag = state.diagnostics || {};

    if (!wifi) {
      setPendingSummary("Loading", "info");
      return;
    }

    const provisioned = Boolean(wifi.provisioned);
    const pendingApply = Boolean(wifi.pendingApply);
    const staEnabled = Boolean(diag.staEnabled);
    const staConnected = Boolean(diag.staConnected);
    const activeModeText = !provisioned
      ? "WiFi Provisioning"
      : staEnabled ? "WiFi Client Mode" : "Standalone AP Mode";

    if (provisioningState) {
      provisioningState.textContent = provisioned ? "✅ Provisioned" : "⚠️ WiFi Provisioning";
    }
    if (activeMode) {
      activeMode.textContent = activeModeText;
    }
    if (clientState) {
      clientState.textContent = staEnabled
        ? (staConnected ? "✅ Connected" : "⏸️ Not connected")
        : "Not active";
    }
    if (staIp) {
      staIp.textContent = staConnected && diag.staIp ? diag.staIp : "--";
    }
    if (apIp) {
      apIp.textContent = diag.apIp || "--";
    }
    if (wifiSignal) {
      wifiSignal.textContent = staConnected ? signalLabel(diag.wifiRssi) : "--";
    }
    if (wifiRssi) {
      wifiRssi.textContent = staConnected && diag.wifiRssi ? diag.wifiRssi : "--";
    }
    renderActiveVsSaved(activeModeText);

    if (!provisioned) {
      setPendingSummary("Provisioning", "warn");
      if (postureDesc) {
        postureDesc.textContent = "The controller is waiting for Device WiFi Settings.";
      }
    } else if (pendingApply) {
      setPendingSummary("Pending apply", "warn");
      if (postureDesc) {
        postureDesc.textContent = `Saved ${modeLabel(wifi.mode)} settings are staged but not active yet.`;
      }
    } else {
      setPendingSummary("Active", "ok");
      if (postureDesc) {
        postureDesc.textContent = `${modeLabel(wifi.mode)} settings are applied.`;
      }
    }

    renderGuidance();
  };

  const renderGuidance = () => {
    if (!applyGuidance) return;
    const wifi = state.wifiConfig;
    const diag = state.diagnostics || {};
    if (!wifi) {
      applyGuidance.textContent = "Loading reconnect guidance...";
      return;
    }

    const apAddress = diag.apIp ? `http://${diag.apIp}` : "the AP address";
    const apName = wifi.apSsid || diag.apSsid || "the controller AP";
    const staAddress = diag.staIp ? `http://${diag.staIp}` : "the controller IP from your router";
    const hostAddress = `http://${mdnsHost()}`;

    if (!wifi.provisioned) {
      applyGuidance.textContent =
        `Save Device WiFi Settings, then reboot. Until then, connect to ${apName} and open ${apAddress}.`;
      return;
    }

    if (wifi.pendingApply) {
      if (wifi.mode === WIFI_MODE_STANDALONE_AP) {
        applyGuidance.textContent =
          `Saved Standalone AP Mode is pending. Reboot the controller to apply it, then connect to ${apName} and open ${apAddress}.`;
      } else {
        applyGuidance.textContent =
          `Saved WiFi Client Mode is pending. Reboot the controller to apply it, then reconnect from the WiFi network at ${hostAddress} or ${staAddress}.`;
      }
      return;
    }

    if (wifi.mode === WIFI_MODE_STANDALONE_AP) {
      applyGuidance.textContent =
        `Standalone AP Mode is active. Connect to ${apName} and open ${apAddress}.`;
      return;
    }

    applyGuidance.textContent =
      `WiFi Client Mode is active. Open ${hostAddress} or ${staAddress}.`;
  };

  const loadIdentity = async () => {
    const result = await window.PAApi.get("/api/identity", { timeoutMs: 3000 });
    state.identity = {
      droidName: result.data?.droidName || DEFAULT_HOSTNAME,
      mdnsUseName: Boolean(result.data?.mdnsUseName),
    };
    renderPosture();
  };

  const loadConfig = async () => {
    const result = await window.PAApi.get("/api/config", { timeoutMs: 5000 });
    renderSettings(result.data?.wifi || null);
    renderPosture();
  };

  const loadWifiDiagnostics = async (showLoading = false) => {
    if (showLoading) {
      setFeedback("Loading WiFi settings...");
    }
    const result = await window.PAApi.get("/api/wifi", { timeoutMs: 5000 });
    state.diagnostics = result.data || {};
    renderPosture();
  };

  const loadPageData = async () => {
    if (!window.PAApi) {
      setFeedback("API helper unavailable", "error");
      return;
    }
    clearFieldErrors();
    setFeedback("Loading WiFi settings...");
    try {
      await loadIdentity();
      await loadConfig();
      await loadWifiDiagnostics();
      setFeedback(`WiFi settings loaded at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[wifi] load failed:", error);
      setFeedback(`Failed to load WiFi settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const buildSaveBody = () => {
    const mode = selectedMode();
    const body = new URLSearchParams();
    const nextStaSsid = (staSsid?.value || "").trim();
    const nextApSsid = (apSsid?.value || "").trim();
    const nextStaPassword = staPassword?.value || "";
    const nextApPassword = apPassword?.value || "";

    body.set("wifiMode", mode);
    if (mode === WIFI_MODE_CLIENT || nextStaSsid.length > 0) {
      body.set("staSsid", nextStaSsid);
    }
    if (mode === WIFI_MODE_STANDALONE_AP || nextApSsid.length > 0) {
      body.set("apSsid", nextApSsid);
    }
    if (nextStaPassword.length > 0) {
      body.set("staPassword", nextStaPassword);
    }
    if (nextApPassword.length > 0) {
      body.set("apPassword", nextApPassword);
    }
    return body;
  };

  const saveSettings = async (event) => {
    event.preventDefault();
    if (!window.PAApi) return;
    clearFieldErrors();
    if (saveButton) saveButton.disabled = true;
    setFeedback("Saving WiFi settings...");
    try {
      const result = await window.PAApi.postForm("/api/wifi", buildSaveBody(), { timeoutMs: 5000 });
      renderSettings(result.data?.wifi || null);
      renderPosture();
      setFeedback("WiFi settings saved. Reboot the controller to apply the staged network switch.", "success");
    } catch (error) {
      const message = error?.message || window.PAApi.messageFor(error);
      const fieldName = fieldForMessage(message);
      if (fieldName) {
        setFieldError(fieldName, message);
      }
      setFeedback(window.PAApi.messageFor(error), "error");
    } finally {
      if (saveButton) saveButton.disabled = false;
    }
  };

  let pollTimer = null;
  const refreshDiagnostics = (label) => {
    loadWifiDiagnostics().catch((error) => {
      console.warn(`[wifi] diagnostics ${label} failed:`, error);
    });
  };

  const startPolling = () => {
    if (pollTimer !== null) return;
    pollTimer = window.setInterval(() => {
      if (document.visibilityState !== "hidden") {
        refreshDiagnostics("poll");
      }
    }, POLL_INTERVAL_MS);
  };

  const onVisibilityChange = () => {
    if (document.visibilityState !== "hidden") {
      refreshDiagnostics("refresh");
    }
  };

  if (form) form.addEventListener("submit", saveSettings);
  if (reloadButton) reloadButton.addEventListener("click", loadPageData);
  [modeClient, modeStandaloneAp].forEach((input) => {
    if (input) input.addEventListener("change", syncModeOptions);
  });
  document.addEventListener("visibilitychange", onVisibilityChange);

  loadPageData();
  startPolling();
})();
