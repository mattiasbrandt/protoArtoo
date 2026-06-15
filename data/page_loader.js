// Load page dependencies one at a time.
// This limits concurrent LittleFS responses on memory-constrained controllers.
(() => {
  const loader = document.currentScript;
  const sources = (loader?.dataset?.scripts || "")
    .split(",")
    .map((source) => source.trim())
    .filter(Boolean);

  const loadNext = (index) => {
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
      console.error(`[page-loader] Failed to load ${sources[index]}`);
    };
    document.body.appendChild(script);
  };

  const loadDeferredAssets = () => {
    document.querySelectorAll("[data-deferred-src]").forEach((element) => {
      element.src = element.dataset.deferredSrc;
      delete element.dataset.deferredSrc;
    });
  };

  const loadStylesheet = () => {
    const stylesheet = document.createElement("link");
    stylesheet.rel = "stylesheet";
    stylesheet.href = "/style.css";
    stylesheet.onload = () => loadNext(0);
    stylesheet.onerror = () => {
      console.error("[page-loader] Failed to load /style.css");
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
