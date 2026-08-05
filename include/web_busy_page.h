// =============================================================================
// include/web_busy_page.h
//
// The Busy Recovery Page, as one compile-time byte buffer: status line,
// headers and HTML body already concatenated, ready to be written straight to
// the transport without constructing a response object.
//
// The whole point is that this response costs no heap. It is emitted exactly
// when the controller has decided it cannot safely start new work, so building
// it the ordinary way -- response object, header list, body copy -- would
// allocate at the one moment allocation is the thing being avoided. See
// docs/adr/0016-busy-recovery-page-wire-contract.md.
//
// Content-Length is a literal, and a static_assert ties it to the body it
// describes: editing the body without updating the number fails the build
// rather than shipping a truncated or over-long response.
//
// The markup deliberately mirrors data/_recovery_kernel.html -- same class
// names, same literal colors, same wording -- so an operator who sees this
// page and an operator who sees the in-page recovery view are looking at the
// same thing. Colors are literal rather than custom properties because
// /style.css is exactly what could not be loaded.
// =============================================================================
#pragma once

#include <stddef.h>

// Recovery Retry Interval. One source for the Retry-After header and the
// countdown the page shows, so the operator is never told to wait a different
// length of time than the header asks for. Grounded in this board's measured
// pressure-recovery time, not a generic overload convention (ADR 0016).
#define PA_RECOVERY_RETRY_SECONDS 5

#define PA_BUSY_STRINGIFY_(x) #x
#define PA_BUSY_STRINGIFY(x) PA_BUSY_STRINGIFY_(x)

// Body length as a number, and the same number as a string for the header.
// The static_assert below is what keeps them honest.
#define PA_BUSY_BODY_LENGTH 2329

#define PA_BUSY_RECOVERY_BODY \
    "<!doctype html>" \
    "<html lang=\"en\"><head><meta charset=\"utf-8\">" \
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" \
    "<title>Controller busy</title><style>" \
    "body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;" \
    "background:rgb(11,18,32);color:rgb(212,221,232);" \
    "font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,sans-serif}" \
    ".recovery-panel{background:rgb(21,34,56);border:1px solid rgb(42,74,122);border-radius:8px;" \
    "padding:20px;width:90%;max-width:380px;text-align:center}" \
    ".recovery-header{display:flex;flex-direction:column;align-items:center;gap:8px}" \
    ".indicator{width:14px;height:14px;border-radius:50%;background:rgb(232,168,50);" \
    "box-shadow:0 0 6px rgb(232,168,50)}" \
    ".recovery-status-reason{font-size:0.95rem;font-weight:600}" \
    ".recovery-status-detail{color:rgb(168,188,216);font-size:0.8rem;margin-top:2px}" \
    ".recovery-countdown-panel{background:rgba(0,0,0,0.4);border:1px solid rgb(42,74,122);" \
    "border-radius:6px;padding:10px;margin:12px 0}" \
    ".recovery-countdown-value{font-size:1.4rem;font-weight:700;color:rgb(232,168,50);" \
    "font-variant-numeric:tabular-nums;font-family:Consolas,Menlo,monospace}" \
    ".recovery-countdown-label{color:rgb(168,188,216);font-size:0.75rem}" \
    ".recovery-message{color:rgb(168,188,216);font-size:0.85rem;line-height:1.4}" \
    ".btn{margin-top:12px;padding:10px 16px;background:rgb(74,144,217);color:rgb(240,244,248);" \
    "border:1px solid rgb(106,176,255);border-radius:999px;font-size:0.85rem;font-weight:600;" \
    "cursor:pointer}" \
    "</style></head><body>" \
    "<div class=\"recovery-panel\" role=\"status\" aria-live=\"polite\">" \
    "<div class=\"recovery-header\"><span class=\"indicator\"></span>" \
    "<div><div class=\"recovery-status-reason\">Controller busy</div>" \
    "<div class=\"recovery-status-detail\">It refused this page to protect itself.</div></div></div>" \
    "<div class=\"recovery-countdown-panel\">" \
    "<div class=\"recovery-countdown-value\" id=\"c\">" PA_BUSY_STRINGIFY(PA_RECOVERY_RETRY_SECONDS) \
    "</div>" \
    "<div class=\"recovery-countdown-label\">Retrying automatically</div></div>" \
    "<p class=\"recovery-message\">The controller is still running. Nothing was lost.</p>" \
    "<button type=\"button\" class=\"btn\" id=\"r\">Retry now</button>" \
    "<script>(function(){var n=" PA_BUSY_STRINGIFY(PA_RECOVERY_RETRY_SECONDS) ";" \
    "var c=document.getElementById(\"c\");" \
    "document.getElementById(\"r\").onclick=function(){location.reload()};" \
    "var t=setInterval(function(){n--;c.textContent=n;" \
    "if(n<=0){clearInterval(t);location.reload()}},1000)})();</script>" \
    "</div></body></html>"

static_assert(sizeof(PA_BUSY_RECOVERY_BODY) - 1 == PA_BUSY_BODY_LENGTH,
              "Busy Recovery Page body changed: update PA_BUSY_BODY_LENGTH to match, so the "
              "Content-Length header stays correct by construction");

// The complete response, status line through body. Connection: close because
// the controller is shedding this connection, not keeping it alive to be asked
// again immediately.
inline constexpr char kBusyRecoveryResponse[] =
    "HTTP/1.1 503 Service Unavailable\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Content-Length: " PA_BUSY_STRINGIFY(PA_BUSY_BODY_LENGTH) "\r\n"
    "Retry-After: " PA_BUSY_STRINGIFY(PA_RECOVERY_RETRY_SECONDS) "\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: close\r\n"
    "\r\n" PA_BUSY_RECOVERY_BODY;

inline constexpr size_t kBusyRecoveryResponseLength = sizeof(kBusyRecoveryResponse) - 1;
