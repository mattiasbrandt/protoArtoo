// =============================================================================
// include/console_cdc_settle.h
//
// Host presence for the serial Console adapter: the USB CDC settle hold-off
// and the debounced host-attach edge, in one poll-driven unit (#275, #260).
//
// Called once per poll from src/tasks/console_task.cpp's main loop, FIRST,
// before that loop makes any call into the serial transport. The answer is one
// of three: make no transport call at all this poll (the settle window is
// open), run the poll as usual, or run it and also treat this poll as a host
// (re)attach -- reset the line editor and reprint the banner.
//
// WHY THE HOLD-OFF EXISTS, read from the vendor driver, not assumed. On the
// FireBeetle 2 `Serial` is HWCDC (~/.platformio-p4/.../cores/esp32/HWCDC.cpp,
// stock arduino-esp32 3.3.11). `Serial` as a bool is isCDC_Connected(), and
// while the SOF watchdog says a host is plugged but the driver's `connected`
// latch is still false -- which is the state right after the host's bus
// reset -- EVERY call to it arms the IN-empty interrupt and calls
// usb_serial_jtag_ll_txfifo_flush(), i.e. sets ep1_conf.wr_done and commits
// whatever is in the IN FIFO, normally nothing, as a packet. The P4's register
// description (soc/usb_serial_jtag_struct.h, hw_ver3) says serial_in_ep_data_free
// then stays 0 "until data in UART Tx FIFO is read by USB Host". The Console
// task polls `Serial` every 10 ms, so after a cable replug that flush landed
// within ~10 ms of SOF resuming, during enumeration, before the host had
// configured the device -- and the register dump on #275 is what showed what
// the peripheral does with a packet committed there. The same driver sets
// `connected` from a RECEIVED packet too (the RX branch of hw_cdc_isr_handler),
// so the first byte the host sends makes `Serial` read true on a transmit path
// that is dead: `connected` is not a TX-liveness signal, which is why nothing
// downstream of this unit can be trusted to notice the wedge and why it is
// prevented here rather than recovered from later.
//
// WHAT THE HOLD-OFF IS. On a genuine plugged false->true edge the Console task
// makes no HWCDC call for CONSOLE_CDC_SETTLE_MS: no `Serial` as bool, no
// write, no read, no drain, no echo. Bytes the host sends meanwhile wait in the
// driver's receive queue (include/console_task.h's CONSOLE_SERIAL_RX_QUEUE_BYTES)
// and are read after the window. When the window closes the ordinary two-poll
// `Serial` debounce runs exactly as it did before #275, and a replug reaches
// isCDC_Connected() for the first time on a configured device -- the state the
// cold-boot attach, proven by every bench row, has always been in.
//
// WHAT COUNTS AS AN EDGE. isPlugged() is ESP-IDF's SOF watchdog
// (usb_serial_jtag_connection_monitor.c): it drops to false 3 ms after the
// last SOF and is documented by HWCDC.cpp itself to flap briefly on a healthy
// link. A flap must not cost a hold-off -- one costs the operator a second of
// deafness for nothing -- so an edge is plugged after at least
// CONSOLE_CDC_UNPLUGGED_POLLS_FOR_EDGE consecutive unplugged polls. A host's
// bus reset holds the bus in SE0 for tens of milliseconds (10 ms is the USB
// minimum; Linux uses 50), several polls at the 10 ms idle cadence, so a
// second reset inside enumeration re-arms the window from the last reset,
// which is the one enumeration completes after. A one-poll gap inside an open
// window neither cancels nor restarts it: the window is a timer from the last
// genuine edge, not a state the cable has to hold.
//
// THE TWO BOOTS NEED NO SPECIAL CASE. A boot with no host polls unplugged
// from the task's first tick until someone attaches, seconds or minutes later,
// so by the same two-poll rule that first attach IS an edge and gets the
// window -- the no-host boot followed by a first attach is one of the two
// places the wedge was measured. A cold boot with the cable in is plugged from
// the first sample and never sees an edge: the ROM enumerated before
// HWCDC::begin(), no bus reset reaches the app, and this unit adds nothing to
// that boot, which is the acceptance criterion "cold boot with the cable in is
// unchanged".
//
// artoo-esp32 IS UNTOUCHED BY CONSTRUCTION. `Serial` there is HardwareSerial on
// UART0, which has no isPlugged(); console_task.cpp passes `plugged = true`
// under the CDC-on-boot gate's #else, so no edge ever exists and every poll
// runs as it did before. The debounce half is the code that used to live
// inline in console_task.cpp (#260), moved here unchanged so that it can be
// driven on the host: HardwareSerial::operator bool() is "driver installed",
// true from setup() on, so it never sees a second false->true there either.
//
// HEADER-ONLY DELIBERATELY, the include/console_host_attach.h pattern and for
// the same reason: platformio.ini's native build_src_filter is fenced, so a new
// src/**.cpp translation unit cannot be pulled into the native test env, and
// src/tasks/console_task.cpp is not in that filter at all. An `inline` unit
// here needs no filter entry -- console_task.cpp and
// test/test_native/test_console_cdc_settle/ both #include it, and the test
// drives it against test/stubs/include/Arduino.h's SerialStub (its pluggedValue
// and connectedValue knobs, its operator-bool call count).
//
// `Serial` is read HERE, by name, rather than handed in as a value: the whole
// property under test is that no read happens while the window is open, and a
// caller that had to read it first in order to pass it in would have already
// made the call this unit exists to withhold.
// =============================================================================

#pragma once

#include <Arduino.h>
#include <stdint.h>

// How long the Console task stays silent toward the CDC after a genuine
// plugged edge. Start-of-work figure, sized from what the #275 probe measured
// on the bench: see that ticket's status comment for the edge-to-first-IN-token
// readings this number sits above, and adjust it here, with the reasoning,
// the way CONSOLE_RECORD_ROOM_WAIT_BOUND_MS carries its own.
static constexpr uint32_t CONSOLE_CDC_SETTLE_MS = 1000;

// Consecutive unplugged polls before the next plugged poll counts as an edge.
// Two, at the 10 ms idle cadence: the SOF watchdog's documented few-ms flap is
// at most one poll wide, a bus reset is several.
static constexpr uint8_t CONSOLE_CDC_UNPLUGGED_POLLS_FOR_EDGE = 2;

enum ConsoleHostPoll : uint8_t {
    // The settle window is open: the caller makes NO call into the serial
    // transport this poll and comes back next poll.
    CONSOLE_HOST_POLL_HOLD,
    // An ordinary poll.
    CONSOLE_HOST_POLL_RUN,
    // An ordinary poll on which the debounced attach edge fired: the caller
    // resets the line editor and reprints the banner (#260), then runs.
    CONSOLE_HOST_POLL_ATTACHED,
};

struct ConsoleHostPresence {
    // Settle half. unpluggedPolls saturates at the edge threshold; it is the
    // "how long has the cable been out" the next plugged sample is judged by.
    uint8_t unpluggedPolls;
    bool holding;
    uint32_t holdStartMs;
    // Debounce half (#260), exactly the two booleans console_task.cpp kept:
    // `Serial` read true on two consecutive polls after being unconfirmed.
    bool hostConfirmedConnected;
    bool hostRawPrevConnected;
};

// Initialise at task start. No window is open and no unplugged poll has been
// seen; `Serial` is read once for the debounce's baseline, as console_task.cpp
// always did before its loop.
inline void consoleHostPresenceInit(ConsoleHostPresence* st) {
    st->unpluggedPolls = 0;
    st->holding = false;
    st->holdStartMs = 0;
    const bool connected = Serial;
    st->hostConfirmedConnected = connected;
    st->hostRawPrevConnected = connected;
}

// One poll. `plugged` is this poll's cable-presence sample and `nowMs` its
// millis(); both are read by the caller BEFORE calling, and neither read
// touches the transport. Returns what the caller does with the rest of the
// poll -- see ConsoleHostPoll. `Serial` is read at most once per call, and
// never while the window is open.
inline ConsoleHostPoll consoleHostPresencePoll(ConsoleHostPresence* st, bool plugged,
                                               uint32_t nowMs) {
    if (!plugged) {
        if (st->unpluggedPolls < CONSOLE_CDC_UNPLUGGED_POLLS_FOR_EDGE) {
            st->unpluggedPolls++;
        }
    } else {
        if (st->unpluggedPolls >= CONSOLE_CDC_UNPLUGGED_POLLS_FOR_EDGE) {
            // A genuine edge: (re)start the window from here. A second bus
            // reset inside enumeration lands here again and moves the window
            // to the last reset, which is the one that matters.
            st->holding = true;
            st->holdStartMs = nowMs;
        }
        st->unpluggedPolls = 0;
    }

    if (st->holding) {
        // Unsigned subtraction so a millis() wrap inside the window is one
        // window, not a permanent one.
        if ((uint32_t)(nowMs - st->holdStartMs) < CONSOLE_CDC_SETTLE_MS) {
            return CONSOLE_HOST_POLL_HOLD;
        }
        st->holding = false;
    }

    // The #260 debounce, unchanged from src/tasks/console_task.cpp: two
    // consecutive "connected" polls after being unconfirmed commit an attach;
    // a single false sample un-confirms eagerly, so a momentary blip costs at
    // most one extra tick of reprint latency and never a missed or duplicated
    // reset.
    const bool hostRawNowConnected = Serial;
    ConsoleHostPoll result = CONSOLE_HOST_POLL_RUN;
    if (hostRawNowConnected) {
        if (st->hostRawPrevConnected && !st->hostConfirmedConnected) {
            st->hostConfirmedConnected = true;
            result = CONSOLE_HOST_POLL_ATTACHED;
        }
    } else {
        st->hostConfirmedConnected = false;
    }
    st->hostRawPrevConnected = hostRawNowConnected;
    return result;
}
