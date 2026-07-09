// =============================================================================
// data/status_stream.js
//
// Shared status/event stream wrapper for /api/events.
// - SSE-first status delivery for all pages
// - Visibility-aware lifecycle (disconnect when hidden, reconnect when visible)
// - Fallback-friendly: pages may use polling if EventSource is unavailable
// =============================================================================
(() => {
  const RETRY_BASE_MS = 2000;
  const RETRY_MAX_MS  = 30000;
  let retryCount = 0;

  let source = null;
  let reconnectTimer = null;
  let lastStatus = null;
  let visible = document.visibilityState !== "hidden";
  let assetsReady = window.PAAssetsReady === true;
  const listeners = new Set();

  const emit = (eventType, payload) => {
    listeners.forEach((listener) => {
      try {
        listener(eventType, payload);
      } catch (_error) {
        // swallow: listener errors must not break stream fan-out to other subscribers
      }
    });
  };

  const clearReconnect = () => {
    if (reconnectTimer !== null) {
      window.clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
  };

  const scheduleReconnect = () => {
    if (reconnectTimer !== null || !visible || typeof EventSource === "undefined") return;
    // Half-to-full jitter: multiple open pages/tabs losing the stream at the
    // same moment (device reboot, guard rejection storm) must not reconnect
    // in lockstep and re-burst a recovering device.
    const ceiling = Math.min(RETRY_BASE_MS * Math.pow(2, retryCount), RETRY_MAX_MS);
    const delay = ceiling / 2 + Math.random() * (ceiling / 2);
    retryCount++;
    reconnectTimer = window.setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, delay);
  };

  const close = () => {
    if (!source) return;
    source.close();
    source = null;
  };

  const connect = () => {
    if (!assetsReady || source || !visible || typeof EventSource === "undefined") return;

    source = new EventSource("/api/events");

    source.addEventListener("status", (event) => {
      try {
        lastStatus = JSON.parse(event.data);
        emit("status", lastStatus);
        retryCount = 0;
      } catch (_error) {
        // JSON parse failed — emit malformed payload error to subscribers
        emit("status_error", new Error("Malformed status event payload"));
      }
    });

    source.addEventListener("log", (event) => emit("log", event.data));
    source.addEventListener("rc", (event) => emit("rc", event.data));

    source.onerror = () => {
      emit("stream_error", new Error("Event stream disconnected"));
      close();
      scheduleReconnect();
    };
  };

  const onVisibilityChange = () => {
    visible = document.visibilityState !== "hidden";
    if (!visible) {
      clearReconnect();
      close();
      return;
    }

    connect();
    retryCount = 0;
    if (lastStatus) emit("status", lastStatus);
  };

  document.addEventListener("visibilitychange", onVisibilityChange);
  window.addEventListener("pa:assets-ready", () => {
    assetsReady = true;
    if (listeners.size > 0) connect();
  }, { once: true });

  window.PAStatusStream = {
    subscribe(listener) {
      listeners.add(listener);
      if (lastStatus) {
        try {
          listener("status", lastStatus);
        } catch (_error) {
          // swallow: listener error during initial status delivery — subscription succeeds
        }
      }
      connect();
      return () => listeners.delete(listener);
    },
    isSupported() {
      return typeof EventSource !== "undefined";
    },
    isVisible() {
      return visible;
    },
    getLastStatus() {
      return lastStatus;
    },
  };
})();
