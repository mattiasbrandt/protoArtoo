// =============================================================================
// data/footer.js
//
// Footer metadata controller.
// Shows firmware + web bundle version only, from the Live Reading
// (data/live_reading.js), which owns the stream or the one fallback poll for
// the whole shell. The footer asks the droid for nothing of its own (#419).
// =============================================================================
(() => {
  const footer = document.getElementById("fw-meta");
  if (!footer) return;

  // The web bundle's own version, read from the file that ships with it, for
  // a firmware that does not report one in its status. null until it is read.
  let bundleVersion = null;
  let reading = window.PALiveReading.current();

  // A version the frame carries, or the Live Reading's word for one it does
  // not: Finding out before the droid has sent a frame, Unknown after.
  const versionOf = (field) => {
    const value = reading.status?.[field];
    if (value) return String(value);
    return reading.word(field) || window.PALiveReading.UNKNOWN;
  };

  const renderFooter = () => {
    const fw = versionOf("firmwareVersion");
    const web = reading.status?.fsVersion ? String(reading.status.fsVersion) : bundleVersion || versionOf("fsVersion");
    footer.innerHTML =
      `FW: <span class="mono">${window.PAUtils.escapeHtml(fw)}</span><br>` +
      `FS: <span class="mono">${window.PAUtils.escapeHtml(web)}</span>`;
  };

  const loadBundleVersion = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/fs-version.json", { timeoutMs: 2500, cache: "no-store" });
      if (result.data && typeof result.data === "object" && result.data.fsVersion) {
        bundleVersion = String(result.data.fsVersion);
        renderFooter();
      }
    } catch (error) {
      // The status frame's own fsVersion still answers; this file is only the
      // fallback for a firmware that sends none.
      console.warn("[footer] /fs-version.json unavailable:", error?.message || error);
    }
  };

  window.PALiveReading.subscribe((next) => {
    reading = next;
    renderFooter();
  });
  loadBundleVersion();
})();
