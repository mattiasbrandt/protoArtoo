// =============================================================================
// include/web_body_ceiling.h
//
// The one server-wide request-body ceiling the PsychicHttp backend enforces
// (PsychicHttpServer::maxRequestBodySize), built from each route's own bound
// as the route table registers (#427).
//
// PsychicHttp checks that ceiling before any handler runs, then buffers every
// body under it for every route: PsychicRequest::loadBody() mallocs
// content_len + 1 and copies it into a std::string while the malloc is still
// live. The library starts the ceiling at MAX_REQUEST_BODY_SIZE, 16 KB
// (PsychicCore.h), so a ceiling that only rose from there buffered 16 KB on
// artoo_esp32, where no route declares more than 12 KB. Starting it at
// kDefaultMaxBodyBytes instead makes it end at the largest bound a registered
// route declares on this board: 12 KB on ESP32 (POST /api/config and
// POST /api/seq), 24 KB on ESP32-P4 (POST /api/seq, SEQ_FILE_MAX_BYTES).
//
// A body over the ceiling is refused by the library with its own text/html 400
// before the handler runs; one between a route's bound and the ceiling is
// still buffered and reaches the handler, and a handler that checks its bound
// answers 413 from contentLength().
//
// Header-only and free of vendor types so the native suite can drive it.
// `unsigned long` is the type PsychicHttpServer::maxRequestBodySize has.
// =============================================================================
#pragma once

#include <stddef.h>

#include "web_request.h"  // kDefaultMaxBodyBytes

// Start the ceiling at the smallest bound any route has, replacing whatever
// the library initialised it to. Call once, before the first route registers.
inline void webBodyCeilingReset(unsigned long& ceiling) {
    ceiling = kDefaultMaxBodyBytes;
}

// Raise the ceiling to fit one route's bound. It never lowers: every route
// must still receive the bodies it declares it takes.
inline void webBodyCeilingAdmitRoute(unsigned long& ceiling, size_t routeMaxBodyBytes) {
    if (routeMaxBodyBytes > ceiling) {
        ceiling = routeMaxBodyBytes;
    }
}
