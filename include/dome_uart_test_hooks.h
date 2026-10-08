// =============================================================================
// include/dome_uart_test_hooks.h
//
// Native-test seam for the shared UART2 arbitration dome_link.cpp owns
// (dome_link.cpp is not in [env:native]'s build_src_filter, so the stub in
// src/native_test_stubs.cpp stands in for it). Tests set the owner in setUp()
// and reset it in tearDown() to drive the audio drivers'
// domeUartOwnedBy(DOME_UART_DOME) guards.
//
// Declared once here, in the one header both the stub's definition and every
// native test that drives the seam include, rather than each test writing its
// own top-level `extern` - the declaration and the definition then stay
// compiler-checked against the same type (matches
// include/log_buffer_test_hooks.h's convention).
// =============================================================================
#pragma once

#include "dome_link.h"  // DomeUartOwner

extern DomeUartOwner g_test_dome_uart_owner;
