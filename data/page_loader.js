// Load page dependencies one at a time.
// This limits concurrent LittleFS responses on memory-constrained controllers.
(() => {
  const loader = document.currentScript;
  const sources = (loader?.dataset?.scripts || "")
    .split(",")
    .map((source) => source.trim())
    .filter(Boolean);

  const MAX_SCRIPT_ATTEMPTS = 3;
  const RETRY_DELAY_MS = 400;

  const loadNext = (index, attempt = 1) => {
    if (index >= sources.length) {
      loadDeferredAssets();
      window.PAAssetsReady = true;
      window.dispatchEvent(new Event("pa:assets-ready"));
      return;
    }

    const script = document.createElement("script");
    script.src = sources[index];
    script.async = false;
    script.onload = () => loadNext(index + 1);
    script.onerror = () => {
      // The device sheds connections under load, so a failed script load is
      // usually transient. Retry with a pause; only after repeated failures
      // continue the chain so the page still reaches pa:assets-ready and
      // later scripts (and the status stream) are not silently abandoned.
      script.remove();
      if (attempt < MAX_SCRIPT_ATTEMPTS) {
        console.warn(`[page-loader] Retrying ${sources[index]} (attempt ${attempt + 1})`);
        window.setTimeout(() => loadNext(index, attempt + 1), RETRY_DELAY_MS * attempt);
        return;
      }
      console.error(`[page-loader] Failed to load ${sources[index]} after ${attempt} attempts`);
      loadNext(index + 1);
    };
    document.body.appendChild(script);
  };

  const loadDeferredAssets = () => {
    document.querySelectorAll("[data-deferred-src]").forEach((element) => {
      element.src = element.dataset.deferredSrc;
      delete element.dataset.deferredSrc;
    });
  };

  const loadStylesheet = (attempt = 1) => {
    const stylesheet = document.createElement("link");
    stylesheet.rel = "stylesheet";
    stylesheet.href = "/style.css";
    stylesheet.onload = () => loadNext(0);
    stylesheet.onerror = () => {
      stylesheet.remove();
      if (attempt < MAX_SCRIPT_ATTEMPTS) {
        console.warn(`[page-loader] Retrying /style.css (attempt ${attempt + 1})`);
        window.setTimeout(() => loadStylesheet(attempt + 1), RETRY_DELAY_MS * attempt);
        return;
      }
      console.error(`[page-loader] Failed to load /style.css after ${attempt} attempts`);
      loadNext(0);
    };
    document.head.appendChild(stylesheet);
  };

  const start = () => {
    loadStylesheet();
  };
  if (document.readyState === "complete") {
    start();
  } else {
    window.addEventListener("load", start, { once: true });
  }
})();
