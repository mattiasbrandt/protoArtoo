// =============================================================================
// src/web/web_response_deadline_psychic.cpp
//
// Response-phase deadline send override for the PsychicHttp backend
// (ADRs 0020, 0024). The send override callback installed per socket, and the
// Busy Recovery Page emission on request admission refusal (ADR 0016).
//
// The deadline is enforced through a session-level send override so every
// response byte -- the seam's own sends, PsychicHttp's, and the static file
// handler's -- leaves through the deadline guard. This is the only place on
// this stack that can enforce it without patching a library.
// =============================================================================

#include <Arduino.h>
#include <errno.h>
#include <esp_http_server.h>
#include <lwip/sockets.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../include/logging.h"
#include "../../include/web_backend_psychic.h"
#include "../../include/web_busy_page.h"
#include "../../include/web_response_deadline.h"

static const char* TAG = "WebServer";

// PsychicHttp's per-request worker threads would make responses overlap, and
// the single record below cannot represent two response phases at once: the
// second request to arm would silently take the first one's deadline with it.
// A build failure is the only honest outcome, because the damage is invisible
// at runtime -- the guard would keep publishing counters while guarding the
// wrong request.
#ifdef ENABLE_ASYNC
#error \
    "PsychicHttp per-request worker threads make responses overlap; the response-phase deadline needs a per-socket table before ENABLE_ASYNC can be used (ADR 0022, ADR 0024)"
#endif

static WebResponseDeadline s_responseDeadline;

#ifndef PA_RESPONSE_DEADLINE_MS
#define PA_RESPONSE_DEADLINE_MS 4000
#endif

// How long to wait between write attempts once the client's receive window is
// full. Same value and the same reasoning as the event stream's retry delay:
// the window can only reopen when the client reads, which it cannot do while
// this task spins on the CPU.
constexpr uint32_t kResponseRetryDelayMs = 5;

// Backstop for the one configuration in which nothing else ends the retry loop.
// PA_RESPONSE_DEADLINE_MS=0 is documented in platformio.ini as disabling the
// guard, and with it disabled the loop below has no other exit against a client
// whose window stays full: a non-blocking write never times out on its own, so
// it would be retried forever on the task that serves every connection. 5000 ms
// is what the socket's own send_wait_timeout would have cost for a single
// blocking write, which is the behaviour turning the deadline off asks for.
//
// Dead code on any build that keeps the deadline, because the compiler folds
// the constant comparison away -- the shipped configuration is unchanged.
constexpr uint32_t kResponseRetryUnguardedCapMs = 5000;

// One raw write, carrying esp_http_server's own error mapping.
//
// This reproduces httpd_default_send() rather than calling it. That function is
// declared in the component's private header (esp_httpd_priv.h), so an override
// installed from application code cannot chain to it, and the public
// alternative -- httpd_socket_send() -- dispatches through the session's send
// function, which is the override itself. Five lines of duplication is the
// smaller cost, and the error mapping is the part that has to match exactly:
// the caller distinguishes a full window from a dead socket by it.
int mapSendErrno(int errnoValue) {
    switch (errnoValue) {
        case EAGAIN:
        case EINTR:
            return HTTPD_SOCK_ERR_TIMEOUT;
        case EINVAL:
        case EBADF:
        case EFAULT:
        case ENOTSOCK:
            return HTTPD_SOCK_ERR_INVALID;
        default:
            return HTTPD_SOCK_ERR_FAIL;
    }
}

int rawSocketSend(int sockfd, const char* buf, size_t len, int flags) {
    if (buf == nullptr) {
        return HTTPD_SOCK_ERR_INVALID;
    }
    const int ret = send(sockfd, buf, len, flags);
    if (ret >= 0) {
        return ret;
    }
    return mapSendErrno(errno);
}

// Drops the connection a breach was taken on.
//
// linger{on, 0} for the same reason the event stream eviction sets it: a client
// that stalled a response has a full send queue, and a graceful close leaves
// lwIP holding that queue while it retransmits into a peer that has stopped
// answering -- measured on this controller as a depressed largest free block
// for over a minute. An RST drops it immediately. Only ever applied to a socket
// already being abandoned.
//
// The close itself is queued rather than immediate: this runs on the server
// task, inside the handler whose response is being abandoned, so the session
// must not be torn down underneath the frames still unwinding above.
void closeAfterDeadlineBreach(httpd_handle_t hd, int sockfd) {
    struct linger abandon = {};
    abandon.l_onoff = 1;
    abandon.l_linger = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &abandon, sizeof(abandon)) != 0) {
        PA_LOG_WARN(TAG, "could not set SO_LINGER on socket %d; close will be graceful", sockfd);
    }
    httpd_sess_trigger_close(hd, sockfd);
}

// The session send override. Installed per socket at Connection Admission and
// used by esp_http_server for every byte of every response on that socket:
// httpd_send_all() calls it in a loop for partial writes, so a body larger than
// one socket buffer is checked against the deadline several times over.
//
// Exported for installation from web_admission_psychic.cpp (admissionOpenCallback).
int webResponseDeadlineSendOverride(httpd_handle_t hd, int sockfd, const char* buf, size_t len,
                                    int flags) {
    // Anything this deadline is not guarding keeps the plain write it has
    // always had, bounded by the socket's own send timeout. The callers that
    // reach here asked not to block and own their own bound: the event stream
    // (governed by PA_SSE_SEND_DEADLINE_MS) and the Busy Recovery Page
    // (kBusyRecoverySendDeadlineMs, answered from the refusal path before any
    // request is armed).
    //
    // The distinction is load-bearing rather than tidy. The guarded path below
    // is a retry loop, and the only thing that ends it is the deadline; sending
    // an unguarded write down it would spin forever against a full window.
    if ((flags & MSG_DONTWAIT) != 0 || !webResponseDeadlineGuards(&s_responseDeadline, sockfd)) {
        return rawSocketSend(sockfd, buf, len, flags);
    }

    // How long this one write has spent waiting, so the published maximum
    // describes a single write rather than a whole response phase -- the two
    // answer different questions, and the response phase already has its own.
    uint32_t waitedMs = 0;

    for (;;) {
        // Read before the check so a latched breach can be told from the write
        // that caused it. The counter and the close belong to the transition:
        // every later write in the same response returns the same failure, and
        // counting those would report one stalled client as many.
        const bool alreadyBreached = s_responseDeadline.breached;
        const uint32_t nowMs = millis();
        if (webResponseDeadlineCheck(&s_responseDeadline, sockfd, nowMs,
                                     PA_RESPONSE_DEADLINE_MS) ==
            WebResponseDeadlineVerdict::kBreach) {
            if (!alreadyBreached) {
                g_webResponseDeadlineClosures = g_webResponseDeadlineClosures + 1u;
                g_webResponseDeadlineLastMs = nowMs;
                // Warn, not debug: this is rare by design, so a run that hit it
                // must say so without anyone having raised the log level first.
                PA_LOG_WARN(TAG, "response on socket %d missed the %u ms deadline; dropped", sockfd,
                            (unsigned)PA_RESPONSE_DEADLINE_MS);
                closeAfterDeadlineBreach(hd, sockfd);
            }
            return HTTPD_SOCK_ERR_FAIL;
        }

        // send() is called here rather than through rawSocketSend() so errno is
        // read where it is produced: the classification below needs the errno
        // itself, and the httpd_ error codes rawSocketSend() maps to cannot
        // tell a full window from a memory-starved write.
        errno = 0;
        const int written = (buf != nullptr) ? send(sockfd, buf, len, flags | MSG_DONTWAIT) : -1;
        const int sendErrno = (buf != nullptr) ? errno : EINVAL;

        switch (webSendClassify(written, sendErrno)) {
            case WebSendOutcome::kWritten:
                // Partial writes are the caller's to loop over, and looping
                // there rather than here is what gives the deadline its
                // granularity.
                if (waitedMs > g_webSendRetryMaxMs) {
                    g_webSendRetryMaxMs = waitedMs;
                }
                return written;
            case WebSendOutcome::kFatal:
                // A dead socket. The deadline has nothing to say about it.
                return mapSendErrno(sendErrno);
            case WebSendOutcome::kTransient:
                break;
        }

        // Either the peer's window is full or the stack had no memory to queue
        // the segment. Both clear on their own and both are retried here, under
        // the deadline that bounds how long this task may spend on one write --
        // failing instead would abandon the response mid-body, which for a
        // static file means a well-formed 200 the browser silently accepts but
        // cannot use (missing content prevents script execution).
        if (sendErrno == ENOMEM || sendErrno == ENOBUFS) {
            g_webSendRetriesMemory = g_webSendRetriesMemory + 1u;
        } else {
            g_webSendRetriesWindow = g_webSendRetriesWindow + 1u;
        }
        waitedMs += kResponseRetryDelayMs;
        if (PA_RESPONSE_DEADLINE_MS == 0 && waitedMs >= kResponseRetryUnguardedCapMs) {
            // Reported as a timeout rather than a failure: that is what the
            // blocking write this stands in for would have returned, and
            // esp_http_server already knows how to end a response on it.
            return HTTPD_SOCK_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(kResponseRetryDelayMs));
    }
}

// Emits the Busy Recovery Page straight onto the socket, bypassing the
// response object entirely. The buffer is compile-time constant, so this
// answers a refusal without allocating a single byte -- which is the whole
// reason a refused navigation can be answered at all.
//
// send() is not obliged to take the whole buffer at once, so partial writes
// are looped. A failure mid-response just ends the attempt: the connection is
// being closed either way, and the browser treats a truncated response the
// same as the bare close it would otherwise have received.
//
// MSG_DONTWAIT with an owned deadline, for the same reason the event stream's
// send carries them: this runs on the server task, during exactly the pressure
// window the refusal exists to survive, and a blocking write against a client
// that has stopped reading would hold that task for the socket's own send
// timeout. A client too stalled to take the page within the deadline gets the
// close it was already headed for.
//
// Exported for use from web_admission_psychic.cpp (admissionMiddleware).
constexpr uint32_t kBusyRecoverySendDeadlineMs = 250;

bool webBusyRecoveryPageSend(httpd_req_t* raw) {
    const int sockfd = httpd_req_to_sockfd(raw);
    if (sockfd < 0) {
        return false;
    }

    const uint32_t startMs = millis();
    size_t sent = 0;
    while (sent < kBusyRecoveryResponseLength) {
        const int written = httpd_socket_send(raw->handle, sockfd, kBusyRecoveryResponse + sent,
                                              kBusyRecoveryResponseLength - sent, MSG_DONTWAIT);
        if (written > 0) {
            sent += (size_t)written;
            continue;
        }
        if (written != HTTPD_SOCK_ERR_TIMEOUT) {
            return false;
        }
        if (millis() - startMs >= kBusyRecoverySendDeadlineMs) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(kResponseRetryDelayMs));
    }
    return true;
}

void webDeadlineInitialize() {
    webResponseDeadlineInit(&s_responseDeadline);
}

// Accessors for the backend's singleton response deadline state.
// These wrap the pure core functions in web_response_deadline.h for host testing.

void webResponseDeadlineExemptSocket(int fd) {
    webResponseDeadlineExempt(&s_responseDeadline, fd);
}

void webResponseDeadlineArmSocket(int fd) {
    webResponseDeadlineArm(&s_responseDeadline, fd);
}

int32_t webResponseDeadlineDisarmCurrent(uint32_t nowMs) {
    return webResponseDeadlineDisarm(&s_responseDeadline, nowMs);
}
