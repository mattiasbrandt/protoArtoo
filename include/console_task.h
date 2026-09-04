// =============================================================================
// include/console_task.h
//
// ConsoleTask - Serial console adapter using embedded-cli
// Core 0, non-real-time, created in setup() regardless of network state.
// =============================================================================

#pragma once

#include <stddef.h>

// How much room the serial transport must have for input the Console task has
// not read yet, applied by src/main.cpp's setup() BEFORE Serial.begin()
// (#229). CDC-on-boot builds only; artoo-esp32 keeps the driver default, for
// the reason at the end of this comment.
//
// WHY THE DEFAULT IS NOT ENOUGH, read from the vendor core rather than
// assumed. On a CDC-on-boot build `Serial` is HWCDC, and HWCDC::begin()
// creates its receive queue with setRxBufferSize(256) - an xQueue of 256
// one-byte items (HWCDC.cpp). Its RX interrupt reads up to 64 bytes out of the
// peripheral FIFO per USB packet and pushes them one at a time with
// xQueueSendFromISR; the FIRST push that fails BREAKS the loop, so the rest of
// that packet and every packet behind it are discarded until the queue drains
// again - with no flag, no counter, no callback and nothing a reader can
// notice afterwards. The Console task empties that queue once per poll of its
// 10 ms cadence, and one 64-byte USB packet per 1 ms frame - the conservative
// floor ADR 0036 already reads off this driver for the other direction - puts
// ~640 bytes into it between two polls, 2.5x what it can hold. So a single
// host write larger than the queue lost its tail, and tools/console_client.py
// sends a scripted line in ONE os.write (SerialTransport._write_marked), which
// is why the bench sheet's `sendlen 260` is one 261-byte burst rather than
// something the poll can keep up with.
//
// WHY THAT TAIL MATTERS MORE THAN ITS BYTES. The tail of a submitted line is
// its CR, and the CR is the only thing that triggers the serial adapter's
// `invalid reason=line-too-long` refusal (lib/embedded-cli/VENDORED.md
// Patch 7 raises a sticky flag as the bytes are lost; onControlInput's CR/LF
// branch is what consumes it). Lose the CR and the refusal is not lost, it is
// DEFERRED: the next line the operator types is appended to the refused one
// and its CR answers for both, so the over-length line's refusal arrives late
// against the wrong request and the next command is never answered at all
// (#229, #215 issuecomment-5544566152).
//
// THE SIZE. The longest line this protocol defines is the browser adapter's
// 255-byte command (docs/console-protocol.md 1.3, src/web/api_console.cpp's
// `char command[256]`); with its terminator that is 256 bytes - the driver's
// default to the byte, with no headroom at all, which is why #229's own
// acceptance row ("a browser line over 255 bytes") lands one byte past what
// the transport can hold. Four times that is the size below: a line sent to
// exceed BOTH adapters' limits still arrives whole and is refused by its own
// CR, in order, and a following command pipelined behind it fits too. The cost
// is the queue's storage, length x item size = 1024 B plus the queue control
// block, taken from internal heap at Serial.begin() - against the ~102 KB the
// P4 has free (include/log_buffer.h's ring-depth ladder) - and nothing at all
// from .bss.
//
// THE RESIDUAL, stated rather than hidden: no fixed size makes an ARBITRARILY
// long line safe. A single host write larger than the queue still loses its
// tail and the driver still says nothing. What a size buys is the length up to
// which the CR is guaranteed to arrive; past it, what keeps the queue drained
// is the Console task reading it often enough - see the poll cadence in
// src/tasks/console_task.cpp, which drops to 1 ms while input is arriving.
//
// NOT APPLIED ON artoo-esp32, and not an oversight: there `Serial` is UART0 at
// 115200 8N1, i.e. 11.52 B/ms, so at most ~115 bytes can accumulate between
// two 10 ms polls - under half the driver's own 256-byte default. The fault is
// native-USB-CDC-specific, the same way #260's host-attach fault is.
static constexpr size_t CONSOLE_SERIAL_RX_QUEUE_BYTES = 1024;

// Create and start the Console task.
//
// Call from setup() after paLogInit() has created the Log Ring: this task
// takes ownership of the serial wire as it starts (ADR 0037), and its first
// act on that wire is to drain the boot lines the ring already holds.
void consoleTask(void* pvParameters);
