# Issue #69: Does esp_http_server give PsychicHttp a real per-request deadline seam?

**Research Date**: 2026-08-04  
**Issue**: #69 (wayfinder sub-ticket to #53 PsychicHttp evaluation)  
**Background**: Issue #68 closed with finding that ESPAsyncWebServer has no viable seam for a universal response-phase deadline watchdog (AsyncClient claims all 6 callback slots, and cross-task `close()` races the event-dispatch task). This research answers whether esp_http_server (the foundation of PsychicHttp 3.1.2) provides a safer seam.

## Findings Summary

**Conclusion: YES — esp_http_server provides a real, safe deadline seam.**

esp_http_server explicitly exposes per-session/request context attachment, session lifecycle callbacks, and a documented thread-safe external session close mechanism. This directly solves the two specific hazards that killed the ESPAsyncWebServer approach:

1. **No callback-slot exhaustion**: Request handling does not claim fixed callback slots; instead, handlers receive `httpd_req_t*` with arbitrary context pointers (`sess_ctx`, `user_ctx`) the application can populate and query.
2. **Cross-task close is safe and intended**: `httpd_sess_trigger_close()` is explicitly documented as designed for "special circumstances wherein some application requires to close an httpd client session asynchronously" and **queues work** to avoid races.

---

## Per-Request/Session Context Seam

### httpd_req_t Structure — Session and User Context

**Source**: [esp-idf/components/esp_http_server/include/esp_http_server.h](https://raw.githubusercontent.com/espressif/esp-idf/master/components/esp_http_server/include/esp_http_server.h)

The request structure provides two context attachment points:

```c
typedef struct httpd_req {
    // ... other fields ...
    
    void *user_ctx;        /* User context pointer passed during URI registration */
    void *sess_ctx;        /* Session Context Pointer — maintained across all requests on a single TCP connection */
    httpd_free_ctx_fn_t free_ctx;  /* Custom free function for sess_ctx */
} httpd_req_t;
```

**Semantics**:
- `sess_ctx`: Persistent across all requests on a single TCP connection. "The web server will ensure that the context persists across all these request and responses." Application can read/write this in any handler for the same connection.
- `user_ctx`: Set during URI handler registration via `httpd_uri_t.user_ctx`; available to all requests for that URI.
- `free_ctx`: Custom destructor called when session is deleted, before the socket closes.

**Source citation**: esp_http_server official docs, [HTTP Server API Reference — Structures](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html#structures)

**Comparison to ESPAsyncWebServer**: Unlike AsyncClient's six fixed callback slots all claimed by AsyncWebServerRequest, esp_http_server handlers are bare functions receiving a request pointer — no callback-slot conflicts.

---

## Session Cleanup Callback (Destructor Hook)

### Custom Session Closing Callback

**Source**: [esp_http_server.h — httpd_config_t.close_fn](https://raw.githubusercontent.com/espressif/esp-idf/master/components/esp_http_server/include/esp_http_server.h)

```c
typedef struct {
    // ... other config fields ...
    httpd_close_func_t close_fn;  /* Custom session closing callback */
} httpd_config_t;
```

**Documented behavior**:

> Called when a session is deleted, before freeing user and transport contexts and before closing the socket. This is a place for custom de-init code common to all sockets.
> 
> The server will only close the socket if no custom session closing callback is set. If a custom callback is used, `close(sockfd)` should be called in here for most cases.
> 
> Set the user or transport context to NULL if it was freed here, so the server does not try to free it again.
> 
> This function is run for all terminated sessions, including sessions where the socket was closed by the network stack — that is, the file descriptor may not be valid anymore.

This provides a guaranteed cleanup hook for per-session deadline state, running within the server's task context (no cross-task race).

---

## Cross-Task Safe External Session Close

### httpd_sess_trigger_close() — The Key Mechanism

**Source**: [esp_http_server.h — API Reference](https://raw.githubusercontent.com/espressif/esp-idf/master/components/esp_http_server/include/esp_http_server.h)

```c
/**
 * @brief   Trigger an httpd session close externally
 *
 * @note    Calling this API is only required in special circumstances wherein
 *          some application requires to close an httpd client session asynchronously.
 *
 * @param[in] handle    Handle to server returned by httpd_start
 * @param[in] sockfd    The socket descriptor of the session to be closed
 *
 * @return
 *  - ESP_OK    : On successfully initiating closure
 *  - ESP_FAIL  : Failure to queue work
 *  - ESP_ERR_NOT_FOUND   : Socket fd not found
 *  - ESP_ERR_INVALID_ARG : Null arguments
 */
esp_err_t httpd_sess_trigger_close(httpd_handle_t handle, int sockfd);
```

**Key properties**:
- **Explicitly async**: Documented for "special circumstances wherein some application requires to close an httpd client session asynchronously."
- **Queues work**: Does not directly close the socket from the calling task; instead queues the close operation to the HTTP server task. This avoids the cross-task race that plagued ESPAsyncWebServer.
- **Socket descriptor based**: Identifies the session by its socket FD, which can be recorded at request admission time and later looked up from any other task.
- **Error handling**: Returns `ESP_ERR_NOT_FOUND` if the FD is no longer active (connection already closed), suitable for defensive deadline enforcement.

### Companion API: httpd_sess_get_ctx()

For retrieving session context from outside the request handler (e.g., from a deadline monitoring task):

```c
/**
 * @brief   Get session context from socket descriptor
 *
 * Typically if a session context is created, it is available to URI handlers
 * through the httpd_req_t structure. But, there are cases where the web
 * server's send/receive functions may require the context (for example, for
 * accessing keying information etc). Since the send/receive function only have
 * the socket descriptor at their disposal, this API provides them with a way to
 * retrieve the session context.
 *
 * @param[in] handle    Handle to server returned by httpd_start
 * @param[in] sockfd    The socket descriptor for which the context should be extracted.
 *
 * @return
 *  - void* : Pointer to the context associated with this session
 *  - NULL  : Empty context / Invalid handle / Invalid socket fd
 */
void *httpd_sess_get_ctx(httpd_handle_t handle, int sockfd);
```

This allows a deadline monitor task to:
1. Retrieve the deadline state from `sess_ctx` using the socket FD.
2. Check if a deadline has elapsed.
3. Call `httpd_sess_trigger_close()` if expired — queued safely to the server task.

---

## Contrast with ESPAsyncWebServer (Issue #68 Finding)

Issue #68's investigation found two blocking hazards for the current stack:

| Problem | ESPAsyncWebServer | esp_http_server |
|---------|-------------------|-----------------|
| **Callback slot exhaustion** | AsyncClient exposes exactly 6 fixed slots (`onConnect/onError/onAck/onDisconnect/onTimeout/onData/onPoll`). AsyncWebServerRequest claims all 6 in its constructor. No way to add a deadline check without patching the constructor. | Handlers receive a bare request pointer with arbitrary context fields (`sess_ctx`, `user_ctx`). No slot conflict. Deadline state lives in `sess_ctx`, not as a callback. |
| **Cross-task close safety** | `AsyncClient::close()` mutates non-atomic state directly on the calling task's stack (`_pcb`, then `_discard_cb`) before posting `tcp_close()` to the network task. A housekeeping task calling `close()` races the event-dispatch task's own concurrent access to the same client. Confirmed hazard, not speculative. | `httpd_sess_trigger_close()` explicitly queues the close to the server's task. Documented as designed for async/external close. No direct mutation from the calling task. |
| **Vendor patch burden** | Requires patching `AsyncWebServerRequest` constructor or `AsyncTCP::_poll()` — growing the existing patch surface (preference against per #22). | Native seams in public API; no vendor patches needed. |

**Source**: [GitHub issue #68 closing comment](https://github.com/mattiasbrandt/protoArtoo/issues/68#issue-xxxx), [docs/adr/0016-response-phase-watchdog-seam.md](https://github.com/mattiasbrandt/protoArtoo/blob/phase/v1.0.0/docs/adr/0016-response-phase-watchdog-seam.md)

---

## PsychicHttp Wrapping (3.1.2)

**Source**: [PsychicHttp GitHub repository, tag 3.1.2](https://github.com/hoeken/PsychicHttp/tree/3.1.2)

PsychicHttp is a C++ wrapper over esp_http_server. At version 3.1.2:

```cpp
// From PsychicHttpServer.h
class PsychicHttpServer {
    // ...
  private:
    httpd_handle_t server;
    httpd_config_t config;
    // ... URI handlers, middleware chain, etc.
};
```

**Key implication**: PsychicHttp exposes the underlying `httpd_handle_t` (server handle) and relies on the standard esp_http_server APIs. Application code can:
1. Set `sess_ctx` during request handling to store deadline metadata.
2. Implement a deadline monitor task that calls `httpd_sess_trigger_close(server, sockfd)` when a deadline expires.
3. Use `httpd_sess_get_ctx()` to inspect session state from the monitor task.
4. Register a custom `close_fn` callback in the httpd_config to clean up deadline state.

PsychicHttp does **not** itself implement timeouts or deadline enforcement — that responsibility falls to the application (protoArtoo) using the library.

**Note on ENABLE_ASYNC mode**: PsychicHttp includes a commented-out `#define ENABLE_ASYNC` for ESP-IDF 5.1.x per-request async handling. This feature does not change the deadline seam; async handling is orthogonal to context attachment and external close.

---

## Implementation Pattern for protoArtoo

If protoArtoo adopts PsychicHttp + esp_http_server for #53's server-library decision, a deadline watchdog would follow this pattern:

```c
// At request admission (inside a handler or middleware):
struct deadline_ctx {
    uint32_t deadline_ms;  // Absolute time when response must send or close
    int sockfd;            // For later lookup from monitor task
};
r->sess_ctx = new deadline_ctx{ ticks_ms() + RESPONSE_DEADLINE_MS, r->sockfd };

// In a periodic housekeeping/deadline-monitor task:
esp_err_t status = httpd_sess_get_ctx(server_handle, session->sockfd);
if (status && now_ms > deadline_ctx->deadline_ms) {
    httpd_sess_trigger_close(server_handle, session->sockfd);
    // Server task will invoke close_fn, clean up, and close socket.
}

// In the server config's close_fn callback (runs in server task, not housekeeping):
void my_session_close_fn(httpd_handle_t handle, int sockfd) {
    deadline_ctx *ctx = (deadline_ctx *)httpd_sess_get_ctx(handle, sockfd);
    if (ctx) {
        free(ctx);
    }
    close(sockfd);  // Socket close, per documented callback behavior.
}
```

This avoids all of #68's hazards:
- No callback-slot conflict (deadline state lives in `sess_ctx`, not as a callback).
- No cross-task race (close is queued to server task, not called directly).
- No vendor patching (all APIs are public).

---

## Verification & Sources

1. **esp_http_server official API documentation**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html
   - Covers httpd_req_t structure, session context semantics, and lifecycle.
   - Documents httpd_sess_trigger_close() and httpd_sess_get_ctx() explicitly.

2. **esp_http_server.h header (esp-idf master)**: https://raw.githubusercontent.com/espressif/esp-idf/master/components/esp_http_server/include/esp_http_server.h
   - Function signatures and Doxygen comments for all APIs cited.
   - httpd_config_t structure with close_fn callback.

3. **PsychicHttp source (3.1.2)**: https://github.com/hoeken/PsychicHttp/tree/3.1.2
   - PsychicHttpServer wraps httpd_handle_t and httpd_config_t.
   - No custom deadline/timeout implementation; relies on app using esp_http_server APIs directly.

4. **Issue #68 & ADR 0016** (protoArtoo project):
   - [GitHub issue #68 closing comment](https://github.com/mattiasbrandt/protoArtoo/issues/68)
   - [docs/adr/0016-response-phase-watchdog-seam.md](https://github.com/mattiasbrandt/protoArtoo/blob/phase/v1.0.0/docs/adr/0016-response-phase-watchdog-seam.md)
   - Detailed analysis of ESPAsyncWebServer's unfixable callback-slot and cross-task-close hazards.

---

## Recommendation for Issue #53

If protoArtoo's #53 (PsychicHttp evaluation) proceeds:

1. **A universal response-phase deadline watchdog is feasible** using `sess_ctx` + `httpd_sess_trigger_close()` + a monitor task.
2. **No vendor patching required** — all seams are public APIs in esp_http_server.
3. **Cross-task safety is explicit** — `httpd_sess_trigger_close()` is designed for this use case and queues work to avoid races.
4. **Configuration flexibility** — deadlines can be per-category (Ordinary, Catalog) via middleware or handler-level logic.

The deadline requirement from #68 is **no longer a blocker for server-library selection**. esp_http_server solves it cleanly where ESPAsyncWebServer could not.
