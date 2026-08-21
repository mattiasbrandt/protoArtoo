// =============================================================================
// include/api_param_source.h
//
// ConfigParamSource - function-pointer parameter lookup seam for API write
// Apply Cores (ADR 0011). Decouples validation/apply logic from the request
// object so it is reachable by native tests.
//
// Production: webParamSource() adapts the project-owned WebRequest (ADR 0021),
// so this seam names no web-server type on either side.
// Tests: a static name->value table (see the ADR 0002 MapReader precedent).
// =============================================================================
#pragma once

struct ConfigParamSource {
    void* ctx;
    // Returns the raw value for `name`, or nullptr if not supplied.
    // Non-owning pointer, valid for the duration of the call.
    const char* (*get)(void* ctx, const char* name);
};

inline const char* configParamGet(const ConfigParamSource& params, const char* name) {
    return params.get(params.ctx, name);
}

inline bool configParamHas(const ConfigParamSource& params, const char* name) {
    return configParamGet(params, name) != nullptr;
}
