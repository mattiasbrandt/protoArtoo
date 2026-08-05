// =============================================================================
// include/web_param_source.h
//
// Adapts a WebRequest (ADR 0021) to the ConfigParamSource the apply cores
// (ADR 0011) read their fields through. This is the whole of the coupling
// between the web layer and the apply cores: the cores keep taking a borrowed
// const char* per field and never learn which server delivered it.
// =============================================================================
#pragma once

#include "api_param_source.h"
#include "web_request.h"

// Build a param source backed by req. The returned source borrows req, so it
// must not outlive the handler call it was built in.
//
// Fields resolve to request parameters. The one special name is "plain", the
// project's long-standing name for a raw (non-form) request body: backends
// disagree about where such a body lives -- ESPAsyncWebServer surfaces it as a
// parameter, PsychicHttp keeps it as the body -- and reconciling that here is
// what keeps the difference out of the apply cores.
ConfigParamSource webParamSource(WebRequest& req);
