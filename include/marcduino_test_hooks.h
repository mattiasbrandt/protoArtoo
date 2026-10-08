// =============================================================================
// include/marcduino_test_hooks.h
//
// Native-test seam for Command Ownership routing (include/marcduino_router.h).
// dome_link.cpp and dome_rx_parser.cpp are not in [env:native]'s
// build_src_filter, so src/native_test_stubs.cpp stands in for the dome link
// and the body's handler. These let a test see what was forwarded, what the
// body was handed, and make either of them refuse.
//
// Declared once here, in the one header both the stub definitions and every
// test that drives the seam include (include/log_buffer_test_hooks.h's
// convention), so declaration and definition stay compiler-checked.
// =============================================================================
#pragma once

#include "dome_rx_parser.h"  // MarcduinoBodyOutcome

// domeConnected() answers this. Default true.
extern bool g_test_dome_connected;
// domeQueueTx() answers this, and records every line it was handed while true.
extern bool g_test_dome_tx_ok;
extern unsigned g_test_dome_tx_calls;
extern char g_test_dome_last_tx[64];

// executeMarcduinoBodyCommand() answers this. Default Applied.
extern MarcduinoBodyOutcome g_test_marcduino_body_outcome;
// What the body's handler was last handed (executeMarcduinoBodyCommand() or
// parseMarcduinoCommand(), which both count in g_test_marcduino_calls).
extern char g_test_marcduino_last_line[32];

// Back to the defaults above; call from setUp().
void marcduinoTestHooksReset();
