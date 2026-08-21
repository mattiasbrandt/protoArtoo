// =============================================================================
// src/web/web_param_source.cpp
//
// See include/web_param_source.h. One adapter serves every route whose write
// path goes through an apply core, so there is a single place where a backend
// difference in parameter delivery can be reconciled.
// =============================================================================

#include "web_param_source.h"

#include <string.h>

namespace {

const char* paramOrBody(void* ctx, const char* name) {
    WebRequest* req = static_cast<WebRequest*>(ctx);

    const char* value = req->paramRef(name);
    if (value != nullptr) {
        return value;
    }

    // Fall back to the raw body only for "plain", and only when no parameter
    // of that name arrived -- a backend that does surface the body as a
    // parameter has already answered above.
    if (strcmp(name, "plain") == 0) {
        return req->body();
    }
    return nullptr;
}

}  // namespace

ConfigParamSource webParamSource(WebRequest& req) {
    ConfigParamSource params;
    params.ctx = &req;
    params.get = paramOrBody;
    return params;
}
