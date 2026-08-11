// =============================================================================
// include/web_backend_psychic.h
//
// Cross-unit interface for the PsychicHttp backend split. Declares
// initialization and callback functions shared between:
// - web_admission_psychic.cpp (connection and request admission)
// - web_response_deadline_psychic.cpp (response deadline send override)
// - web_request_psychic.cpp (seam backend and bootstrap orchestration)
//
// These declarations replace scattered inline extern declarations and ensure
// type safety at compile time rather than deferring mismatches to the linker.
// =============================================================================

#ifndef INCLUDE_WEB_BACKEND_PSYCHIC_H
#define INCLUDE_WEB_BACKEND_PSYCHIC_H

#include <PsychicHttp.h>
#include <esp_err.h>
#include <esp_http_server.h>

#include "web_admission.h"
#include "web_response_deadline.h"

// =============================================================================
// Admission orchestration (web_admission_psychic.cpp)
// =============================================================================

// Close an admitted socket and publish updated census counters.
// Call when a socket is closing so the occupancy reading reflects current state.
void webAdmissionSocketClosed(int sockfd);

// Register connection-admission callback and initialize rate limiter/census.
// Called during server pre-begin() configuration.
void webAdmissionRegisterCallbacks(PsychicHttpServer& server);

// Register request-admission middleware.
// Called after connection callbacks are set up.
void webAdmissionRegisterMiddleware(PsychicHttpServer& server);

// Initialize admission trace configuration (if PA_ADMISSION_TRACE is enabled).
void webAdmissionTraceInit();

// =============================================================================
// Response-phase deadline (web_response_deadline_psychic.cpp)
// =============================================================================

// Initialize deadline state before any connections arrive.
void webDeadlineInitialize();

// Exempt a socket from response-phase deadline enforcement (for event streams).
void webResponseDeadlineExemptSocket(int fd);

// Arm the response-phase deadline for a socket (when a request is admitted).
void webResponseDeadlineArmSocket(int fd);

// Disarm the currently-armed response-phase deadline and report elapsed time.
// Returns elapsed milliseconds, or -1 if the phase cannot be reported.
// Note: Only the currently armed socket can be disarmed; prior armed state is
// implicit. See ADR 0024 for why a per-socket table is needed to support
// ENABLE_ASYNC (currently disabled; see web_response_deadline_psychic.cpp).
int32_t webResponseDeadlineDisarmCurrent(uint32_t nowMs);

// Session send override callback for deadline enforcement.
// Registered per-socket from connection-admission callback.
int webResponseDeadlineSendOverride(httpd_handle_t hd, int sockfd, const char* buf,
                                    size_t len, int flags);

// Emit busy recovery page on socket during request-admission refusal.
// Called from admission middleware on main-frame navigation refusal.
bool webBusyRecoveryPageSend(httpd_req_t* raw);

#endif  // INCLUDE_WEB_BACKEND_PSYCHIC_H
