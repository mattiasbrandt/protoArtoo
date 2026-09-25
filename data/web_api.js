// =============================================================================
// data/web_api.js
//
// Shared HTTP helper for protoArtoo web pages.
// - Normalizes timeout/network/http/json errors
// - Provides GET / form POST / JSON POST helpers
// - Keeps API error handling consistent across pages
// =============================================================================
(() => {
  const DEFAULT_TIMEOUT_MS = 6000;

  class ApiError extends Error {
    constructor(message, {
      kind = "unknown", status = 0, cause = null, retryAfterMs = null,
      field = null, reason = null, accepts = null,
    } = {}) {
      super(message);
      this.name = "ApiError";
      this.kind = kind;
      this.status = status;
      this.cause = cause;
      // Populated from the response's Retry-After when the device refuses a
      // request as busy (ADR 0016), so callers honor the server's own interval
      // instead of guessing one.
      this.retryAfterMs = retryAfterMs;
      // A settings write the droid refused says which field, why and what it
      // would have taken as keys beside its sentence (docs/api.md "Refusals
      // from a settings write"). A caller words the refusal from these and
      // never reads them out of the message. null when the answer had none.
      this.field = field;
      this.reason = reason;
      this.accepts = accepts;
    }
  }

  // Retry-After is seconds-or-HTTP-date per RFC 9110. The device sends a small
  // integer, but parse defensively and reject anything non-positive so a bad
  // header degrades to the caller's default rather than a zero-delay hot loop.
  const parseRetryAfterMs = (headerValue) => {
    if (!headerValue) return null;
    const seconds = Number(headerValue);
    if (Number.isFinite(seconds)) return seconds > 0 ? seconds * 1000 : null;
    const dateMs = Date.parse(headerValue);
    if (Number.isNaN(dateMs)) return null;
    const delta = dateMs - Date.now();
    return delta > 0 ? delta : null;
  };

  // An AbortError has two distinct causes that callers must be able to tell
  // apart: the request's own timeout fired (kind "timeout", surfaced to the
  // operator), or the caller cancelled it through the signal it owns (kind
  // "cancelled", normally swallowed because the caller asked for it).
  const normalizeError = (error, callerSignal = null) => {
    if (error instanceof ApiError) return error;
    if (error?.name === "AbortError") {
      if (callerSignal?.aborted) {
        return new ApiError("Request cancelled", { kind: "cancelled", cause: error });
      }
      return new ApiError("Request timeout", { kind: "timeout" });
    }
    return new ApiError("Network request failed", { kind: "network", cause: error });
  };

  const parseResponse = async (response) => {
    const contentType = response.headers.get("content-type") || "";
    const isJson = contentType.includes("application/json");

    if (isJson) {
      return response.json().catch(() => {
        throw new ApiError("Malformed JSON response", { kind: "bad-json", status: response.status });
      });
    }

    return response.text();
  };

  // ESP32 has few concurrent AsyncTCP socket slots; a GET racing the page's
  // persistent SSE connection can occasionally receive a truncated response
  // body. That's a transient transport fault, not a real client/server
  // error, so it's worth one quiet retry before surfacing it.
  const BAD_JSON_RETRY_DELAY_MS = 150;

  // The device serves without HTTP keep-alive, so every request is its own
  // TCP connection, and its accept-time admission control rejects connection
  // bursts outright. Page code that fires many calls at once
  // (Promise.all/allSettled) would open them all as parallel sockets; this
  // FIFO caps in-flight requests so a page load presents as a short paced
  // trickle instead of a burst. Queue wait does not consume the request
  // timeout — the timeout timer starts when the request actually goes out.
  //
  // Narrowed from 2 to 1 per ADR 0019: page recovery assumes a single active
  // request slot, so the transport must not run two requests behind the
  // bootstrap's back and defeat its ordering.
  const MAX_CONCURRENT_REQUESTS = 1;
  let inFlightCount = 0;
  const requestWaiters = [];

  const acquireRequestSlot = () => {
    if (inFlightCount < MAX_CONCURRENT_REQUESTS) {
      inFlightCount += 1;
      return Promise.resolve();
    }
    return new Promise((resolve) => requestWaiters.push(resolve));
  };

  const releaseRequestSlot = () => {
    const next = requestWaiters.shift();
    if (next) {
      next(); // hand the slot to the next queued request
    } else {
      inFlightCount -= 1;
    }
  };

  // Cancellation is caller-owned: a caller that needs to cancel its request
  // creates its own AbortController and passes controller.signal as
  // opts.signal (standard platform pattern). Ownership travels with the
  // request, so cancelling one caller's request can never touch another
  // caller's request - there is no shared abort state in this module at all.
  // The bootstrap owns one controller per section run (see page_bootstrap.js);
  // requests issued without a signal (footer, status widgets) are not
  // cancellable by anyone.
  const throwIfCancelled = (signal) => {
    if (signal?.aborted) {
      throw new ApiError("Request cancelled", { kind: "cancelled" });
    }
  };

  const request = async (path, opts = {}) => {
    const { signal = null } = opts;
    throwIfCancelled(signal);
    await acquireRequestSlot();
    try {
      // Cancelled while queued for the slot: skip the dispatch entirely so a
      // dead request does not spend slot time, and release via the same
      // finally as every other outcome.
      throwIfCancelled(signal);
      return await performRequest(path, opts);
    } finally {
      // Sole release point for the slot this request acquired, on every
      // outcome (success, error, timeout, cancellation). Nothing else may
      // release a slot, so accounting cannot drift (ADR 0019).
      releaseRequestSlot();
    }
  };

  // Estop bypass: skips slot acquisition entirely so estop is never queued,
  // must never be auto-retried, and strips any caller signal so the E-Stop
  // POST can never be cancelled once dispatched.
  const estopRequest = async (path, opts = {}) => {
    return performRequest(path, { ...opts, noRetry: true, signal: null });
  };

  const performRequest = async (path, {
    method = "GET",
    timeoutMs = DEFAULT_TIMEOUT_MS,
    cache = "no-store",
    headers = {},
    form = null,
    json = null,
    noRetry = false,
    signal = null,
  } = {}) => {
    const attempt = async (isRetry) => {
      // The internal controller drives the timeout; the caller's signal (if
      // any) is bridged onto it so either source can abort the fetch. The
      // caller's controller is never touched from here - ownership stays with
      // the caller.
      const controller = new AbortController();
      const onCallerAbort = () => controller.abort();
      if (signal) {
        signal.addEventListener("abort", onCallerAbort, { once: true });
      }
      const timeoutId = window.setTimeout(() => controller.abort(), timeoutMs);

      try {
        const requestHeaders = { ...headers };
        let body;
        if (form) {
          body = form instanceof URLSearchParams ? form : new URLSearchParams(form);
          requestHeaders["Content-Type"] = "application/x-www-form-urlencoded;charset=UTF-8";
        } else if (json !== null) {
          body = JSON.stringify(json);
          requestHeaders["Content-Type"] = "application/json";
        }

        const response = await fetch(path, {
          method,
          cache,
          headers: requestHeaders,
          body,
          signal: controller.signal,
        });

        const payload = await parseResponse(response);

        if (!response.ok) {
          const body = payload && typeof payload === "object" ? payload : {};
          const key = (name) => (typeof body[name] === "string" ? body[name] : null);
          throw new ApiError(key("error") || `HTTP ${response.status}`, {
            kind: "http",
            status: response.status,
            retryAfterMs: parseRetryAfterMs(response.headers.get("retry-after")),
            field: key("field"),
            reason: key("reason"),
            accepts: key("accepts"),
          });
        }

        return {
          ok: true,
          status: response.status,
          // The response's declared media type, surfaced so a caller can check
          // that what came back is what its endpoint contracts. parseResponse()
          // already reads this header to choose JSON-vs-text parsing but kept
          // it to itself, so the Live Logs panel had no way to tell log text
          // from an intercepted HTML page and rendered the page (#261).
          // Strictly additive: every existing caller destructures ok/status/data.
          contentType: response.headers.get("content-type") || "",
          data: payload,
        };
      } catch (error) {
        const apiError = normalizeError(error, signal);
        // Network errors on GETs are usually the device shedding a
        // connection under load (admission control closes the socket);
        // like the truncated-JSON case, one quiet retry beats surfacing a
        // transient. POSTs are never retried — they may not be idempotent.
        // Also skip retries if noRetry is set (estop cannot be retried).
        if (!noRetry && !isRetry && method === "GET"
            && (apiError.kind === "bad-json" || apiError.kind === "network")) {
          await new Promise((resolve) => window.setTimeout(resolve, BAD_JSON_RETRY_DELAY_MS));
          // The caller may have cancelled during the retry delay; a cancelled
          // request must not go back out on the wire.
          throwIfCancelled(signal);
          return attempt(true);
        }
        throw apiError;
      } finally {
        window.clearTimeout(timeoutId);
        if (signal) {
          signal.removeEventListener("abort", onCallerAbort);
        }
      }
    };

    return attempt(false);
  };

  const get = (path, opts = {}) => request(path, { ...opts, method: "GET" });
  const postForm = (path, form, opts = {}) => request(path, { ...opts, method: "POST", form });
  const postJson = (path, json, opts = {}) => request(path, { ...opts, method: "POST", json });
  const estopPostForm = (path, form, opts = {}) => estopRequest(path, { ...opts, method: "POST", form });

  const HTTP_STATUS_MESSAGES = {
    400: "Device rejected the request",
    403: "Access denied by device",
    404: "Not found on device",
    500: "Device error",
    501: "Not supported by device",
    503: "Device unavailable",
  };

  // ---------------------------------------------------------------------------
  // The words a refusal is shown in (#348)
  //
  // The droid names a refusal in its own vocabulary - `web_control_disabled`,
  // and three more it emits from src/web/api_actions.cpp. Those tokens are the
  // wire format and stay exactly as they are; what may never happen is one of
  // them reaching a screen, which is what every caller of messageFor() did
  // until this map existed, because an HTTP error carries the droid's `error`
  // field as its message.
  //
  // So this is the door: one map from token to sentence, read rather than
  // typed, so a surface CANNOT say the internal word. That is the shape
  // r2d2-astromech-simulator v1.79.0 uses for the same problem
  // (src/js/maestro/setup-hw.js:999, `PW_END_WORD`): storage keeps `min`/`max`,
  // every message reads the word out of the map.
  //
  // `route` is the builder's next move where there is one, and null where there
  // is not. A refusal by a safety rule is a settled no: nothing to do about it,
  // so no destination, no link, and no suggestion to buy or fit anything
  // (CONTEXT.md "Availability Family").
  const DEVICE_REFUSALS = Object.freeze({
    invalid_action_token: Object.freeze({
      text: "Unknown action. Reload the page.",
      route: null,
    }),
    // Only the estop reaches this guard (evaluateActionTestGuard,
    // include/api_actions.h). Stopping the droid is not something a test press
    // does, and there is nothing for the builder to change about that.
    safety_critical_blocked: Object.freeze({
      text: "The estop is never sent as a test.",
      route: null,
    }),
    web_control_disabled: Object.freeze({
      text: "Web control is off.",
      route: Object.freeze({ href: "#drive", label: "Turn it on in Foot Drive" }),
    }),
    // An analog action, or one that needs a payload: the test button sends
    // neither, so this control cannot drive it however the droid is set up.
    action_not_testable: Object.freeze({
      text: "Needs a value the test button cannot send.",
      route: null,
    }),
  });

  /**
   * The refusal behind an error, where the droid named one this map knows.
   *
   * @returns {{text: string, route: ({href: string, label: string}|null)}|null}
   *   null when the error is not one of the droid's own refusal tokens, which
   *   is every transport failure and every message already written for people.
   */
  const refusalFor = (error) => {
    if (!(error instanceof ApiError) || error.kind !== "http") return null;
    return DEVICE_REFUSALS[error.message] || null;
  };

  const messageFor = (error) => {
    if (!(error instanceof ApiError)) return "Request failed";
    if (error.kind === "timeout") return "Request timed out";
    if (error.kind === "cancelled") return "Request cancelled";
    if (error.kind === "network") return "Network error";
    if (error.kind === "http") {
      // Before the raw message: a token the droid refused in its own
      // vocabulary is translated here or it is shown verbatim.
      const refusal = refusalFor(error);
      if (refusal) return refusal.route ? `${refusal.text} ${refusal.route.label}.` : refusal.text;
      if (error.message && !error.message.startsWith("HTTP ")) return error.message;
      return HTTP_STATUS_MESSAGES[error.status]
        || (error.status >= 500 ? "Device error" : "Device rejected the request");
    }
    return error.message || "Request failed";
  };

  // Disable or enable a list of controls, keeping aria-disabled in sync.
  const gateControls = (elements, enabled) => {
    elements.forEach((el) => {
      if (!el) return;
      el.disabled = !enabled;
      el.setAttribute("aria-disabled", enabled ? "false" : "true");
    });
  };

  // Shared UI utilities
  const showFeedback = (el, text, level = "") => {
    if (!el) return;
    el.textContent = text;
    el.className = level ? `feedback ${level}` : "feedback";
  };

  // Escape HTML special characters for text context (null/undefined → empty string).
  const escapeHtml = (value) => {
    return String(value ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  };

  // Escape HTML special characters for attribute context (null/undefined → empty string).
  const escapeAttr = (value) => {
    return String(value ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  };

  // Debounce with per-call-site state management.
  const debounce = (fn, ms) => {
    let timeoutId = null;
    return (...args) => {
      if (timeoutId !== null) window.clearTimeout(timeoutId);
      timeoutId = window.setTimeout(() => {
        fn(...args);
        timeoutId = null;
      }, ms);
    };
  };

  window.PAApi = {
    ApiError,
    request,
    get,
    postForm,
    postJson,
    estopPostForm,
    messageFor,
    refusalFor,
    gateControls,
  };

  window.PAUtils = {
    showFeedback,
    escapeHtml,
    escapeAttr,
    debounce,
  };
})();
