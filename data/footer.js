// =============================================================================
// data/footer.js
//
// Footer metadata controller.
// Shows firmware + web bundle version only.
// Prefers SSE status stream; falls back to periodic API fetch when needed.
// =============================================================================
(() => {
  const footer = document.getElementById("fw-meta");
  if (!footer) return;

  let fsVersion = "unknown";
  let unsubscribe = null;
  let hasRealData = false;
  let versionPoll = null;
  let retryPoll = null;

  const renderFooter = (status) => {
    if (!status) {
      footer.textContent = "Firmware info unavailable";
      hasRealData = false;
      return;
    }

    const fw = String(status.firmwareVersion || "unknown");
    const apiFsVersion = String(status.fsVersion || "unknown");
    const resolvedWeb = apiFsVersion !== "unknown" ? apiFsVersion : fsVersion;

    footer.innerHTML =
      `FW: <span class="mono">${window.PAUtils.escapeHtml(fw)}</span><br>` +
      `FS: <span class="mono">${window.PAUtils.escapeHtml(resolvedWeb)}</span>`;
    hasRealData = true;
  };

  const loadFsVersion = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/fs-version.json", { timeoutMs: 2500, cache: "no-store" });
      if (result.data && typeof result.data === "object" && result.data.fsVersion) {
        fsVersion = String(result.data.fsVersion);
      }
    } catch (_error) {
      // ignore: fetch error - keep fallback fsVersion value
    }
  };

  const fetchStatus = async () => {
    if (!window.PAApi) {
      footer.textContent = "Firmware info unavailable";
      hasRealData = false;
      return false;
    }
    try {
      const result = await window.PAApi.get("/api/status", { timeoutMs: 3000, cache: "no-store" });
      renderFooter(result.data);
      return true;
    } catch (error) {
      // fetch timeout/error - log and show unavailable message
      console.warn("[footer] failed to fetch status:", error?.message || error);
      footer.textContent = "Firmware info unavailable";
      hasRealData = false;
      return false;
    }
  };

  const init = async () => {
    await loadFsVersion();

    if (window.PAStatusStream?.isSupported()) {
      // SSE mode: subscribe and set up conditional polling
      unsubscribe = window.PAStatusStream.subscribe((eventType, payload) => {
        if (eventType === "status") {
          renderFooter(payload);
          // Status arrived: cancel any pending retry
          retryPoll?.cancelRetry();
        }
      });

      // Start retry poll only if no cached status
      if (!window.PAStatusStream.getLastStatus()) {
        retryPoll = window.PageBootstrap.createBackgroundPoll(fetchStatus, {
          retry: { baseMs: 500, factor: 2, maxAttempts: 3 },
          runOnStart: true,
        });
        retryPoll.start();
      }

      // Version poll: conditional, only while no real data
      versionPoll = window.PageBootstrap.createBackgroundPoll(fetchStatus, {
        cadenceMs: 5000,
        skipWhen: () => hasRealData,
        refreshOnReturn: true,
      });
      versionPoll.start();
    } else {
      // Fallback mode: no SSE, poll for everything
      versionPoll = window.PageBootstrap.createBackgroundPoll(fetchStatus, {
        cadenceMs: 5000,
        runOnStart: true,
        refreshOnReturn: true,
      });
      versionPoll.start();
    }

    window.addEventListener("beforeunload", () => {
      if (unsubscribe) unsubscribe();
      if (versionPoll) versionPoll.stop();
      if (retryPoll) retryPoll.stop();
    });
  };

  init();
})();
