// Board Lane inventory (CONTEXT.md "Board Lane"; #339, from #304).
//
// A Board Lane is a per-Board-Variant declared value naming WHERE that board
// routes a signal. A Board Capability Gate says what a board can be wired for;
// a Lane says where it is routed. The rows below name the signals; the values
// come from the board's own pin-map arm above in config.h, so a GPIO number
// still has exactly one home and this manifest never becomes a second copy of
// it -- which is the defect the Lane exists to close (data/setup.html states
// artoo-esp32's GPIO numbers as fact on every board).
//
// This file is an unguarded X-macro manifest, intentionally included
// repeatedly. Define PA_BOARD_LANE(name, uart_port, tx_pin, rx_pin) before
// including it. Adding #pragma once here breaks the mechanism: the manifest is
// included from two sites with different macro definitions (include/config.h
// for the routed-pin static_asserts, src/web/api_identity_serializers.cpp for
// JSON emission), and both land in the same translation unit for that .cpp
// file. #pragma once would make the second expansion a no-op, so the identity
// JSON manifest would be silently empty.
//
// `name` is the JSON key the identity manifest reports the lane under, so it
// is project vocabulary rather than a PCB silkscreen legend -- the silkscreen
// text is a Board Component Label (ADR 0033, include/component_labels.inc).

#ifndef PA_BOARD_LANE
  #error "Define PA_BOARD_LANE before including this inventory"
#endif

PA_BOARD_LANE(drive, UART_PORT_DRIVE, PIN_DRIVE_TX, PIN_DRIVE_RX)

// The audio lane's controller is shared with protor2link wherever
// PA_CAP_DEDICATED_AUDIO_UART is 0, and on such a board only RX rides that
// controller -- TX is the software bit-bang in src/drivers/audio_soft_uart_tx.h,
// because the one shared controller's TX is committed to the dome link. The
// Lane still reports where the signal is routed; the Gate is what says whether
// the controller is the audio module's own, and a reader needs both.
PA_BOARD_LANE(audio, UART_PORT_AUDIO, PIN_AUDIO_TX, PIN_AUDIO_RX)

PA_BOARD_LANE(protor2link, UART_PORT_DOME, PIN_DOME_TX, PIN_DOME_RX)
