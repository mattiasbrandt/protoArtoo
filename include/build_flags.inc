// Build Feature Flag inventory (ADR 0029).
//
// This file is an unguarded X-macro manifest, intentionally included repeatedly.
// Define PA_BUILD_FLAG(name) before including it. Adding #pragma once here
// breaks the mechanism — each use in include/config.h includes this file with a
// different PA_BUILD_FLAG macro definition to enumerate flags and to validate
// their values are always defined as 0 or 1.
//
// Every PlatformIO environment defines each row as 0 or 1 in platformio.ini.

#ifndef PA_BUILD_FLAG
  #error "Define PA_BUILD_FLAG before including this inventory"
#endif

PA_BUILD_FLAG(PA_HEAP_PROFILE)
PA_BUILD_FLAG(PA_HEAP_TRACING)
PA_BUILD_FLAG(PA_ADMISSION_TRACE)
