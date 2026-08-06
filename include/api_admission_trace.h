// =============================================================================
// include/api_admission_trace.h
//
// GET /api/admission/trace -- readback for the admission decision ring
// (include/web_admission_trace.h), on the WebRequest seam (ADR 0021).
//
// Present only on builds with PA_ADMISSION_TRACE, like /api/profiler is present
// only with PA_HEAP_PROFILE. Absent means 404, which is what a harness should
// treat as "this build cannot answer the question" rather than as an empty
// profile.
// =============================================================================
#pragma once

#include "web_admission_trace.h"

#if PA_ADMISSION_TRACE

#include "web_request.h"

// The one ring the admission layers record into. Exposed rather than hidden
// behind a record wrapper because the device hookup already holds every value
// an entry needs, and routing it through a second function would only add a
// call on the path this trace exists to keep cheap.
WebAdmissionTrace* webAdmissionTraceInstance();

//   GET /api/admission/trace           -- the whole ring, oldest row first
//   GET /api/admission/trace?clear=1   -- the same, then arm for the next run
void handleAdmissionTraceGet(WebRequest& req);

#endif  // PA_ADMISSION_TRACE
