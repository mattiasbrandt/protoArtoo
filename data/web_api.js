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
    constructor(message, { kind = "unknown", status = 0, cause = null } = {}) {
      super(message);
      this.name = "ApiError";
      this.kind = kind;
      this.status = status;
      this.cause = cause;
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

  const request = async (path, {
    method = "GET",
    timeoutMs = DEFAULT_TIMEOUT_MS,
    cache = "no-store",
    headers = {},
    form = null,
    json = null,
  } = {}) => {
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
        });
      }

      return { ok: true, status: response.status, data: payload };
    } catch (error) {
      throw normalizeError(error);
    } finally {
      window.clearTimeout(timeoutId);
    }
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
