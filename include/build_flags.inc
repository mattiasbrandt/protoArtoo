// Build Feature Flag inventory (ADR 0029).
//
// This file is an unguarded X-macro manifest, intentionally included repeatedly.
// Define PA_BUILD_FLAG(name) before including it. Adding #pragma once here
// breaks the mechanism: the manifest is included from two sites with different
// macro definitions (include/config.h:73 for static_assert validation,
// src/web/api_identity_serializers.cpp:85 for JSON emission), and both land
// in the same translation unit for that .cpp file. #pragma once would make the
// second expansion a no-op, so the identity JSON manifest would be silently empty.
//
// Every PlatformIO environment defines each row as 0 or 1 in platformio.ini.
// A derived environment enabling a base-0 flag must unflag (unset) the 0 value
// before setting it to 1; see platformio.ini:32 and the build_unflags at :388.

#ifndef PA_BUILD_FLAG
  #error "Define PA_BUILD_FLAG before including this inventory"
#endif

PA_BUILD_FLAG(PA_HEAP_PROFILE)
PA_BUILD_FLAG(PA_HEAP_TRACING)
PA_BUILD_FLAG(PA_ADMISSION_TRACE)
