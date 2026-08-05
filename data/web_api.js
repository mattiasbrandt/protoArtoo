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
    constructor(message, { kind = "unknown", status = 0, cause = null, retryAfterMs = null } = {}) {
      super(message);
      this.name = "ApiError";
      this.kind = kind;
      this.status = status;
      this.cause = cause;
      // Populated from the response's Retry-After when the device refuses a
      // request as busy (ADR 0016), so callers honor the server's own interval
      // instead of guessing one.
      this.retryAfterMs = retryAfterMs;
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

  const normalizeError = (error) => {
    if (error instanceof ApiError) return error;
    if (error?.name === "AbortError") {
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

  const request = async (path, opts = {}) => {
    await acquireRequestSlot();
    try {
      return await performRequest(path, opts);
    } finally {
      releaseRequestSlot();
    }
  };

  const performRequest = async (path, {
    method = "GET",
    timeoutMs = DEFAULT_TIMEOUT_MS,
    cache = "no-store",
    headers = {},
    form = null,
    json = null,
  } = {}) => {
    const attempt = async (isRetry) => {
      const controller = new AbortController();
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
          const apiMessage = payload && typeof payload === "object" ? payload.error : "";
          throw new ApiError(apiMessage || `HTTP ${response.status}`, {
            kind: "http",
            status: response.status,
            retryAfterMs: parseRetryAfterMs(response.headers.get("retry-after")),
          });
        }

        return { ok: true, status: response.status, data: payload };
      } catch (error) {
        const apiError = normalizeError(error);
        // Network errors on GETs are usually the device shedding a
        // connection under load (admission control closes the socket);
        // like the truncated-JSON case, one quiet retry beats surfacing a
        // transient. POSTs are never retried — they may not be idempotent.
        if (!isRetry && method === "GET"
            && (apiError.kind === "bad-json" || apiError.kind === "network")) {
          await new Promise((resolve) => window.setTimeout(resolve, BAD_JSON_RETRY_DELAY_MS));
          return attempt(true);
        }
        throw apiError;
      } finally {
        window.clearTimeout(timeoutId);
      }
    };

    return attempt(false);
  };

  const get = (path, opts = {}) => request(path, { ...opts, method: "GET" });
  const postForm = (path, form, opts = {}) => request(path, { ...opts, method: "POST", form });
  const postJson = (path, json, opts = {}) => request(path, { ...opts, method: "POST", json });

  const HTTP_STATUS_MESSAGES = {
    400: "Device rejected the request",
    403: "Access denied by device",
    404: "Not found on device",
    500: "Device error",
    501: "Not supported by device",
    503: "Device unavailable",
  };

  const messageFor = (error) => {
    if (!(error instanceof ApiError)) return "Request failed";
    if (error.kind === "timeout") return "Request timed out";
    if (error.kind === "network") return "Network error";
    if (error.kind === "http") {
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

  const debounce = (fn, ms) => (...args) => {
    const timeoutId = window.PAApi._debounceTimeouts?.get(fn);
    if (timeoutId !== undefined) window.clearTimeout(timeoutId);
    const newId = window.setTimeout(() => fn(...args), ms);
    if (!window.PAApi._debounceTimeouts) window.PAApi._debounceTimeouts = new Map();
    window.PAApi._debounceTimeouts.set(fn, newId);
  };

  window.PAApi = {
    ApiError,
    request,
    get,
    postForm,
    postJson,
    messageFor,
    gateControls,
  };

  window.PAUtils = {
    showFeedback,
    debounce,
  };
})();
