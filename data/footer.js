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

  const escapeHtml = (value) => String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");


  const renderFooter = (status) => {
    if (!status) {
      footer.textContent = "Firmware info unavailable";
      return;
    }

    const fw = String(status.firmwareVersion || "unknown");
    const apiFsVersion = String(status.fsVersion || "unknown");
    const resolvedWeb = apiFsVersion !== "unknown" ? apiFsVersion : fsVersion;

    footer.innerHTML =
      `FW: <span class="mono">${escapeHtml(fw)}</span><br>` +
      `FS: <span class="mono">${escapeHtml(resolvedWeb)}</span>`;
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
    } catch (_error) {
      // fetch timeout/error — show unavailable message
      footer.textContent = "Firmware info unavailable";
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
      if (eventType === "status") renderFooter(payload);
    });

    if (!window.PAStatusStream.getLastStatus()) {
      fetchStatus();
    }
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
