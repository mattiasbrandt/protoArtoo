// Board Capability Gate inventory (ADR 0029).
//
// This file is an unguarded X-macro manifest, intentionally included repeatedly.
// Define PA_BOARD_CAPABILITY(name) before including it. Adding #pragma once
// here breaks the mechanism — each board block in include/config.h includes this
// file with a different PA_BOARD_CAPABILITY macro definition to enumerate its
// capabilities and to validate its completeness.

#ifndef PA_BOARD_CAPABILITY
  #error "Define PA_BOARD_CAPABILITY before including this inventory"
#endif

PA_BOARD_CAPABILITY(PA_CAP_NATIVE_WIFI)
PA_BOARD_CAPABILITY(PA_CAP_HOSTED_WIFI)
