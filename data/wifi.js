// =============================================================================
// wifi.js
//
// WiFi page — reflects the active build-time WiFi mode.
// PA_ENABLE_STA_WIFI=1: WiFi Client (STA) mode.
// PA_ENABLE_STA_WIFI=0: Access Point (AP) mode.
//
// Status updates: PAApi.get polling with visibility pause/resume.
// SSE stream does not carry the full wifi-specific payload (apSsid, staEnabled,
// apIp) so polling /api/wifi is required.
// =============================================================================
(() => {
  const POLL_INTERVAL_MS = 10000;

  const reloadButton   = document.getElementById("reload-wifi-button");
  const feedback       = document.getElementById("wifi-feedback");
  const staCard        = document.getElementById("wifi-sta-card");
  const staDesc        = document.getElementById("wifi-sta-desc");
  const apCard         = document.getElementById("wifi-ap-card");
  const apDesc         = document.getElementById("wifi-ap-desc");
  const apSsid         = document.getElementById("wifi-ap-ssid");
  const apIp           = document.getElementById("wifi-ap-ip");
  const staConnected   = document.getElementById("wifi-sta-connected");
  const staIp          = document.getElementById("wifi-sta-ip");
  const wifiSignal     = document.getElementById("wifi-signal");
  const wifiRssi       = document.getElementById("wifi-rssi");

  const signalLabel = (rssi) => {
    if (!rssi || rssi === 0) return "--";
    if (rssi >= -67) return `✅ Excellent (${rssi} dBm)`;
    if (rssi >= -75) return `✅ Good (${rssi} dBm)`;
    if (rssi >= -85) return `⚠️ Fair (${rssi} dBm)`;
    return `❌ Poor (${rssi} dBm)`;
  };

  const render = (data) => {
    const staModeEnabled = !!data.staEnabled;

    // STA (WiFi client) card — shown whenever STA mode is enabled
    if (staCard) {
      if (staModeEnabled) {
        staCard.classList.remove("hidden");
      } else {
        staCard.classList.add("hidden");
      }
    }
    if (staConnected) staConnected.textContent = data.staConnected ? "✅ Connected" : "⏸️ Not connected";
    if (staIp)        staIp.textContent        = data.staIp || "--";
    if (wifiSignal)   wifiSignal.textContent   = signalLabel(data.wifiRssi);
    if (wifiRssi)     wifiRssi.textContent     = data.wifiRssi || "--";
    if (staDesc) {
      staDesc.textContent = data.staConnected
        ? "Connected in WiFi Client (STA) mode. Access Point mode is not available in this build."
        : "WiFi Client (STA) mode is enabled, but the controller is not connected to a network.";
    }

    // AP card — only shown when firmware is built in AP mode
    if (apCard) apCard.classList.toggle("hidden", staModeEnabled);

    if (!staModeEnabled) {
      if (apSsid) apSsid.textContent = data.apSsid || "--";
      if (apIp)   apIp.textContent   = data.apIp   || "--";
      if (apDesc) apDesc.textContent = "Connect to this network to reach the controller.";
    }
  };

  const loadWifiStatus = async () => {
    if (!window.PAApi) {
      if (feedback) {
        feedback.textContent = "API helper unavailable";
        feedback.className = "feedback error";
      }
      return;
    }
    if (feedback) {
      feedback.textContent = "Loading WiFi status...";
      feedback.className = "feedback";
    }
    try {
      const result = await window.PAApi.get("/api/wifi");
      render(result.data);
      if (feedback) {
        feedback.textContent = `Updated at ${new Date().toLocaleTimeString()}`;
        feedback.className = "feedback success";
      }
    } catch (error) {
      if (feedback) {
        feedback.textContent = window.PAApi.messageFor(error);
        feedback.className = "feedback error";
      }
    }
  };

  // Visibility-aware polling: pause when tab is hidden, resume (and refresh
  // immediately) when it becomes visible again. Guard against duplicate
  // listeners by using a module-scoped flag.
  let pollTimer = null;

  const startPolling = () => {
    if (pollTimer !== null) return;
    pollTimer = window.setInterval(() => {
      if (document.visibilityState !== "hidden") {
        loadWifiStatus();
      }
    }, POLL_INTERVAL_MS);
  };

  const onVisibilityChange = () => {
    if (document.visibilityState !== "hidden") {
      // Immediate refresh when tab returns to foreground.
      loadWifiStatus();
    }
  };

  if (reloadButton) reloadButton.addEventListener("click", loadWifiStatus);
  document.addEventListener("visibilitychange", onVisibilityChange);

  loadWifiStatus();
  startPolling();
})();
