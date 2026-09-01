// Board Capability Gate inventory (ADR 0029).
//
// This file is an unguarded X-macro manifest, intentionally included repeatedly.
// Define PA_BOARD_CAPABILITY(name) before including it. Adding #pragma once
// here breaks the mechanism: the manifest is included from two sites with
// different macro definitions (include/config.h:81 for static_assert validation,
// src/web/api_identity_serializers.cpp:77 for JSON emission), and both land
// in the same translation unit for that .cpp file. #pragma once would make the
// second expansion a no-op, so the identity JSON manifest would be silently empty.

#ifndef PA_BOARD_CAPABILITY
  #error "Define PA_BOARD_CAPABILITY before including this inventory"
#endif

PA_BOARD_CAPABILITY(PA_CAP_NATIVE_WIFI)
PA_BOARD_CAPABILITY(PA_CAP_HOSTED_WIFI)
PA_BOARD_CAPABILITY(PA_CAP_DRIVE_BACKEND_HOVERBOARD)
PA_BOARD_CAPABILITY(PA_CAP_DEDICATED_AUDIO_UART)
