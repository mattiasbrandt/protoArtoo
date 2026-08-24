// Board Capability Gate inventory (ADR 0029).
//
// This file is an unguarded X-macro manifest. Define
// PA_BOARD_CAPABILITY(name) before including it.

#ifndef PA_BOARD_CAPABILITY
  #error "Define PA_BOARD_CAPABILITY before including this inventory"
#endif

PA_BOARD_CAPABILITY(PA_CAP_NATIVE_WIFI)
PA_BOARD_CAPABILITY(PA_CAP_HOSTED_WIFI)
