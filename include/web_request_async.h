// =============================================================================
// include/web_request_async.h
//
// Attach point for the ESPAsyncWebServer backend of the WebRequest seam
// (ADR 0021). Only web_server.cpp and web_request_async.cpp include this;
// handler files include web_request.h and never see the vendor type.
// Temporary scaffolding -- deleted by the #91 cutover.
// =============================================================================
#pragma once

class AsyncWebServer;

// Point webRegisterRoute() at the server instance. Must be called before the
// route registration block in startHttpServerOnce().
void webRequestAsyncAttach(AsyncWebServer& server);
