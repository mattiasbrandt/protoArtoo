// =============================================================================
// wifi.js
//
// WiFi page — reflects the active build-time WiFi mode.
// PA_ENABLE_STA_WIFI=1: WiFi Client (STA) mode.
// PA_ENABLE_STA_WIFI=0: Access Point (AP) mode.
// =============================================================================
(() => {
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
    if (feedback) {
      feedback.textContent = "Loading WiFi status...";
      feedback.className = "feedback";
    }
    try {
      const response = await fetch("/api/wifi", { cache: "no-store" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      render(await response.json());
      if (feedback) {
        feedback.textContent = `Updated at ${new Date().toLocaleTimeString()}`;
        feedback.className = "feedback success";
      }
    } catch (error) {
      if (feedback) {
        feedback.textContent = error instanceof Error ? error.message : "Failed to load WiFi status";
        feedback.className = "feedback error";
      }
    }
  };

  if (reloadButton) reloadButton.addEventListener("click", loadWifiStatus);
  loadWifiStatus();
})();
