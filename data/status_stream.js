// =============================================================================
// data/status_stream.js
//
// Shared status/event stream wrapper for /api/events.
// - SSE-first status delivery for all pages
// - Visibility-aware lifecycle (disconnect when hidden, reconnect when visible)
// - Fallback-friendly: pages may use polling if EventSource is unavailable
// =============================================================================
(() => {
  const RETRY_MS = 2000;

  let source = null;
  let reconnectTimer = null;
  let lastStatus = null;
  let visible = document.visibilityState !== "hidden";
  const listeners = new Set();

  const emit = (eventType, payload) => {
    listeners.forEach((listener) => {
      try {
        listener(eventType, payload);
      } catch (_error) {
        // Listener errors must not break stream fan-out.
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
    reconnectTimer = window.setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, RETRY_MS);
  };

  const close = () => {
    if (!source) return;
    source.close();
    source = null;
  };

  const connect = () => {
    if (source || !visible || typeof EventSource === "undefined") return;

    source = new EventSource("/api/events");

    source.addEventListener("status", (event) => {
      try {
        lastStatus = JSON.parse(event.data);
        emit("status", lastStatus);
      } catch (_error) {
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
    if (lastStatus) emit("status", lastStatus);
  };

  document.addEventListener("visibilitychange", onVisibilityChange);

  window.PAStatusStream = {
    subscribe(listener) {
      listeners.add(listener);
      if (lastStatus) {
        try {
          listener("status", lastStatus);
        } catch (_error) {
          // Keep subscribe path resilient.
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
