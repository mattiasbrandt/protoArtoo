// =============================================================================
// data/status_stream.js
//
// Shared status/event stream wrapper for /api/events.
// - SSE-first status delivery for all pages
// - Visibility-aware lifecycle (disconnect when hidden, reconnect when visible)
// - Fallback-friendly: pages may use polling if EventSource is unavailable
// - A frame the controller could not build is not carried as a status, and a
//   frame handed out again is marked as the replay it is (#346)
// =============================================================================
(() => {
  const RETRY_BASE_MS = 2000;
  const RETRY_MAX_MS  = 30000;
  let retryCount = 0;

  let source = null;
  let reconnectTimer = null;
  let lastStatus = null;
  // When lastStatus ARRIVED, not when it was last handed to somebody. A replay
  // carries this rather than the moment of the replay, so no reader can take
  // one for a measurement that was just taken.
  let lastStatusAt = 0;
  // Set when a live stream is closed, so the next open is known to be a
  // RE-open. A first connect resynchronises nothing: the session's one status
  // read has already run or is about to.
  let resyncOnOpen = false;
  let visible = document.visibilityState !== "hidden";
  let assetsReady = window.PAAssetsReady === true;
  const listeners = new Set();

  // meta travels with a "status" event and says where the frame came from:
  // { cached, receivedAt }. A subscriber written before this reads two
  // arguments and is unaffected -- which is the point, because only the
  // readers that make a safety claim out of a frame need to tell a replay from
  // a measurement.
  const emit = (eventType, payload, meta) => {
    listeners.forEach((listener) => {
      try {
        listener(eventType, payload, meta);
      } catch (_error) {
        // swallow: listener errors must not break stream fan-out to other subscribers
      }
    });
  };

  // What this transport will carry as a status, and deliberately no more than
  // that: it answers "did the controller build this", never "does a particular
  // reader have the fields it needs". The second question belongs to the
  // reader -- denying the footer a frame because it carries no failsafe mirror
  // would be a different rule wearing this one's name.
  //
  // buildStatusJson() replaces the whole payload with {"ok":false,"error":...}
  // when it cannot build one, and sends it over the same "status" event
  // (src/web/web_server.cpp). Every key is then absent, and absent booleans
  // read as false, which is the answer that means "nothing is wrong" on every
  // safety field there is. Refusing it here rather than at each reader is what
  // keeps it out of the session's cache, where it would otherwise be handed to
  // every page that asks later and replayed on every reconnect (#346).
  const isStatusFrame = (payload) =>
    !!payload && typeof payload === "object" && payload.ok !== false;

  // The session's status, replaced. Returns whether the frame was taken, so a
  // caller can tell "the droid reported" from "the droid answered and could
  // not report".
  const adopt = (payload) => {
    if (!isStatusFrame(payload)) {
      emit("status_error", new Error("The controller could not report its status"));
      return false;
    }
    lastStatus = payload;
    lastStatusAt = Date.now();
    emit("status", lastStatus, { cached: false, receivedAt: lastStatusAt });
    return true;
  };

  // The frame the session already holds, handed out again: on reconnect, when
  // a hidden tab comes back, and to a subscriber that arrives after it landed.
  // Nothing has been measured, so nothing claims to have been.
  const replay = () => {
    if (!lastStatus) return;
    emit("status", lastStatus, { cached: true, receivedAt: lastStatusAt });
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
    // Everything on screen has been unwatched since this moment, and the
    // device pushes on a change and on nothing else -- so reopening tells us
    // nothing about what happened while we were away. The next open asks for a
    // snapshot instead of trusting the cache. The ask is an EVENT: whoever
    // owns the session's one status read answers it, and this transport does
    // not grow a reader of its own (#346).
    resyncOnOpen = true;
  };

  const connect = () => {
    if (!assetsReady || source || !visible || typeof EventSource === "undefined") return;

    source = new EventSource("/api/events");

    // Status events are delta-triggered: after a reconnect, an idle device
    // may push nothing for a long time, which left pages showing a stale
    // "connection lost" state despite a live stream. Re-emitting the last
    // known status on open is what keeps values on screen across that gap.
    //
    // It is not, on its own, evidence that the device is answering -- it is
    // this browser's own cache handed back, which is why it goes out marked as
    // a replay and why the re-open also asks for a snapshot (#346).
    source.onopen = () => {
      retryCount = 0;
      replay();
      if (resyncOnOpen) {
        resyncOnOpen = false;
        emit("stream_resync");
      }
    };

    source.addEventListener("status", (event) => {
      let parsed = null;
      try {
        parsed = JSON.parse(event.data);
      } catch (_error) {
        // JSON parse failed — emit malformed payload error to subscribers
        emit("status_error", new Error("Malformed status event payload"));
        return;
      }
      // An event arriving at all is the transport working, whether or not the
      // controller managed to fill it -- so the backoff resets either way.
      retryCount = 0;
      adopt(parsed);
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
    replay();
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
          listener("status", lastStatus, { cached: true, receivedAt: lastStatusAt });
        } catch (_error) {
          // swallow: listener error during initial status delivery — subscription succeeds
        }
      }
      connect();
      return () => listeners.delete(listener);
    },
    // A status that arrived some other way than on the stream -- the one
    // /api/status read the Operator Shell does at boot. The device pushes an
    // event on a change and on nothing else, so a client that connects to a
    // quiet droid is told nothing until something moves; whoever closes that
    // gap hands the answer here rather than keeping it, so the session's last
    // status has one home. Every subscriber is told, including one that
    // subscribes later, so the cold start costs one request for the session
    // instead of one per consumer.
    //
    // It is also the answer to "stream_resync": a fetched snapshot is a
    // measurement the droid just took, so it arrives as a fresh frame and not
    // as a replay. A read that came back as an error envelope is refused here
    // exactly as one that arrived on the stream would be.
    seed(status) {
      adopt(status);
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
