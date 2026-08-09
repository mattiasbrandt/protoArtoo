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
  let pollTimer = null;
  let unsubscribe = null;
  let retryTimer = null;

  const renderFooter = (status) => {
    if (!status) {
      footer.textContent = "Firmware info unavailable";
      return;
    }

    const fw = String(status.firmwareVersion || "unknown");
    const apiFsVersion = String(status.fsVersion || "unknown");
    const resolvedWeb = apiFsVersion !== "unknown" ? apiFsVersion : fsVersion;

    footer.innerHTML =
      `FW: <span class="mono">${window.PAUtils.escapeHtml(fw)}</span><br>` +
      `FS: <span class="mono">${window.PAUtils.escapeHtml(resolvedWeb)}</span>`;
  };

  const loadFsVersion = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/fs-version.json", { timeoutMs: 2500, cache: "no-store" });
      if (result.data && typeof result.data === "object" && result.data.fsVersion) {
        fsVersion = String(result.data.fsVersion);
      }
    } catch (_error) {
      // ignore: fetch error — keep fallback fsVersion value
    }
  };

  const fetchStatus = async () => {
    if (!window.PAApi) {
      footer.textContent = "Firmware info unavailable";
      return;
    }
    try {
      const result = await window.PAApi.get("/api/status", { timeoutMs: 3000, cache: "no-store" });
      renderFooter(result.data);
    } catch (error) {
      // fetch timeout/error — log and show unavailable message
      console.warn("[footer] failed to fetch status:", error?.message || error);
      footer.textContent = "Firmware info unavailable";
    }
  };

  const retryFetchWithBackoff = async (attempt = 0) => {
    const maxAttempts = 3;
    const baseDelayMs = 500;
    const delayMs = baseDelayMs * Math.pow(2, attempt);

    try {
      await fetchStatus();
      if (retryTimer !== null) {
        window.clearTimeout(retryTimer);
        retryTimer = null;
      }
      return;
    } catch (_error) {
      // fetchStatus already logged and updated footer
    }

    if (attempt < maxAttempts - 1) {
      retryTimer = window.setTimeout(() => {
        retryFetchWithBackoff(attempt + 1);
      }, delayMs);
    }
  };

  const startFallbackPolling = () => {
    fetchStatus();
    pollTimer = window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      fetchStatus();
    }, 5000);
  };

  const startSseMode = () => {
    unsubscribe = window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") {
        renderFooter(payload);
        if (retryTimer !== null) {
          window.clearTimeout(retryTimer);
          retryTimer = null;
        }
      }
    });

    if (!window.PAStatusStream.getLastStatus()) {
      retryFetchWithBackoff(0);
    }

    pollTimer = window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      fetchStatus();
    }, 5000);
  };

  const onVisibilityChange = () => {
    if (document.visibilityState !== "hidden") {
      fetchStatus();
    }
  };

  const init = async () => {
    await loadFsVersion();

    if (window.PAStatusStream?.isSupported()) {
      startSseMode();
    } else {
      startFallbackPolling();
    }

    document.addEventListener("visibilitychange", onVisibilityChange);
    window.addEventListener("beforeunload", () => {
      document.removeEventListener("visibilitychange", onVisibilityChange);
      if (unsubscribe) unsubscribe();
      if (pollTimer !== null) {
        window.clearInterval(pollTimer);
        pollTimer = null;
      }
    });
  };

  init();
})();
