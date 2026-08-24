// Build Feature Flag inventory (ADR 0029).
//
// This file is an unguarded X-macro manifest. Define PA_BUILD_FLAG(name)
// before including it. Every PlatformIO environment defines each row as 0 or 1.

#ifndef PA_BUILD_FLAG
  #error "Define PA_BUILD_FLAG before including this inventory"
#endif

PA_BUILD_FLAG(PA_HEAP_PROFILE)
PA_BUILD_FLAG(PA_HEAP_TRACING)
PA_BUILD_FLAG(PA_ADMISSION_TRACE)
