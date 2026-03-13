(() => {
  const form = document.getElementById("config-form");
  const speedLimitMax = document.getElementById("speed-limit-max");
  const webDriveTimeout = document.getElementById("web-drive-timeout");
  const ch8ModeLock = document.getElementById("ch8-mode-lock");
  const reloadButton = document.getElementById("reload-config-button");
  const feedback = document.getElementById("config-feedback");
  if (!form || !speedLimitMax || !webDriveTimeout || !ch8ModeLock || !reloadButton || !feedback) {
    return;
  }

  const renderConfig = (payload) => {
    speedLimitMax.value = payload.speedLimitMax;
    webDriveTimeout.value = payload.webDriveTimeoutMs;
    ch8ModeLock.checked = Boolean(payload.ch8ModeLock);
    feedback.textContent = `Loaded at ${new Date().toLocaleTimeString()}`;
  };

  const loadConfig = async () => {
    feedback.textContent = "Loading config…";

    try {
      const response = await fetch("/api/config", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      renderConfig(await response.json());
    } catch (_error) {
      feedback.textContent = "Failed to load config";
    }
  };

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    feedback.textContent = "Saving config…";

    try {
      const body = new URLSearchParams({
        speedLimitMax: speedLimitMax.value,
        webDriveTimeoutMs: webDriveTimeout.value,
        ch8ModeLock: ch8ModeLock.checked ? "true" : "false",
      });

      const response = await fetch("/api/config", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
        },
        body,
      });

      if (!response.ok) {
        const errorBody = await response.json().catch(() => null);
        throw new Error(errorBody?.error || `HTTP ${response.status}`);
      }

      renderConfig(await response.json());
      feedback.textContent = `Saved at ${new Date().toLocaleTimeString()}`;
    } catch (error) {
      feedback.textContent = error instanceof Error ? error.message : "Failed to save config";
    }
  });

  reloadButton.addEventListener("click", () => {
    loadConfig();
  });

  loadConfig();
})();
