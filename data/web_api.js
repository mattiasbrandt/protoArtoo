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
      this.retryAfterMs = retryAfterMs; // From Retry-After header, ADR 0016
    }
  }

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
  // caps in-flight requests so a page load presents as a short paced
  // trickle instead of a burst. Queue wait does not consume the request
  // timeout — the timeout timer starts when the request actually goes out.
  // ADR 0019: narrowed from 2 to 1 for single active request slot
  // (Bounded Page Attempt).
  const MAX_CONCURRENT_REQUESTS = 1;

  // Browser Request Priority (#52 Stories 32-34, mirrors
  // window.PageBootstrap.PRIORITY but defined independently here so
  // web_api.js has no load-order dependency on page_bootstrap.js -- most
  // pages don't load the bootstrap yet). Lower number = higher priority.
  const PRIORITY = { ESTOP: 0, COMMAND: 1, STARTUP: 2, BACKGROUND: 3 };

  let inFlightCount = 0;
  const requestWaiters = []; // { priority, resolve }, kept priority-ordered

  const acquireRequestSlot = (priority) => {
    // Estop bypasses admission and the browser queue entirely -- the
    // safety action must never be delayed by reads, resources, retries, or
    // background work (Story #32). It is allowed to exceed
    // MAX_CONCURRENT_REQUESTS rather than wait for one to free up.
    if (priority === PRIORITY.ESTOP) {
      inFlightCount += 1;
      return Promise.resolve();
    }

    if (inFlightCount < MAX_CONCURRENT_REQUESTS) {
      inFlightCount += 1;
      return Promise.resolve();
    }

    return new Promise((resolve) => {
      // Stable priority insert: ahead of any strictly-lower-priority
      // (higher-number) waiter already queued, behind everything at or
      // above this priority -- so user commands (Story #33) jump ahead of
      // already-queued background work without starving same-tier FIFO order.
      const index = requestWaiters.findIndex((waiter) => waiter.priority > priority);
      const entry = { priority, resolve };
      if (index === -1) {
        requestWaiters.push(entry);
      } else {
        requestWaiters.splice(index, 0, entry);
      }
    });
  };

  const releaseRequestSlot = () => {
    const next = requestWaiters.shift();
    if (next) {
      next.resolve(); // hand the slot to the next queued request
    } else {
      inFlightCount -= 1;
    }
  };

  const request = async (path, opts = {}) => {
    const priority = opts.priority ?? PRIORITY.BACKGROUND;
    await acquireRequestSlot(priority);
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
          const retryAfter = response.headers.get("Retry-After");
          const retryAfterMs = retryAfter ? parseInt(retryAfter, 10) * 1000 : null;
          throw new ApiError(apiMessage || `HTTP ${response.status}`, {
            kind: "http",
            status: response.status,
            retryAfterMs,
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

  // GETs default to background priority (typical automatic loads/polls);
  // POSTs default to command priority (typically an explicit user action --
  // Story #33). Callers pass an explicit `priority` in opts to override
  // either default, e.g. Estop passes PRIORITY.ESTOP. The `??` fallback
  // must be applied AFTER spreading opts -- callers that pass
  // `priority: undefined` (an unset local variable forwarded as an arg,
  // e.g. drive.js's postCommand) must still get the real default, not have
  // it silently clobbered by object-spread key ordering.
  const get = (path, opts = {}) =>
    request(path, { ...opts, method: "GET", priority: opts.priority ?? PRIORITY.BACKGROUND });
  const postForm = (path, form, opts = {}) =>
    request(path, { ...opts, method: "POST", form, priority: opts.priority ?? PRIORITY.COMMAND });
  const postJson = (path, json, opts = {}) =>
    request(path, { ...opts, method: "POST", json, priority: opts.priority ?? PRIORITY.COMMAND });

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
    PRIORITY,
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
