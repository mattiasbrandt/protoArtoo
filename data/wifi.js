(() => {
  const wifiApSsid = document.getElementById("wifi-ap-ssid");
  const wifiApIp = document.getElementById("wifi-ap-ip");
  const wifiStaEnabled = document.getElementById("wifi-sta-enabled");
  const wifiStaConnected = document.getElementById("wifi-sta-connected");
  const wifiStaIp = document.getElementById("wifi-sta-ip");
  const reloadWifiButton = document.getElementById("reload-wifi-button");
  const wifiFeedback = document.getElementById("wifi-feedback");

  if (!wifiApSsid || !wifiApIp || !wifiStaEnabled || !wifiStaConnected || !wifiStaIp || !reloadWifiButton || !wifiFeedback) {
    return;
  }

  const renderWifi = (payload) => {
    wifiApSsid.textContent = payload.apSsid || "--";
    wifiApIp.textContent = payload.apIp || "--";
    wifiStaEnabled.textContent = payload.staEnabled ? "Yes" : "No";
    wifiStaConnected.textContent = payload.staConnected ? "Connected" : "Disconnected";
    wifiStaIp.textContent = payload.staIp || "--";
    wifiFeedback.textContent = `Loaded at ${new Date().toLocaleTimeString()}`;
  };

  const loadWifi = async () => {
    wifiFeedback.textContent = "Loading WiFi status…";

    try {
      const response = await fetch("/api/wifi", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      renderWifi(await response.json());
    } catch (_error) {
      wifiFeedback.textContent = "Failed to load WiFi status";
    }
  };

  reloadWifiButton.addEventListener("click", () => {
    loadWifi();
  });

  loadWifi();
})();
