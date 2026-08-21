# Two-layer board abstraction: chip target + board variant

protoArtoo is adding a second controller family: the ESP32-P4 (first board:
DFRobot FireBeetle 2 ESP32-P4), alongside the fully supported artoo-esp32
(the classic-generation ESP32 D1 Mini clone on the artoo.uk Artoo Controller
PCB). The P4 work must not weld itself to one development board: a P4-based
controller is a category, and the FireBeetle 2 is one member of it.

We decided board support is structured as **two layers**, making explicit the
split that `include/config.h` already practices implicitly:

- **Chip target** (`PA_BOARD_*`, e.g. the ESP32-P4 Target): everything true
  of *any* board built on that chip — which network stack flavor it uses
  (native `esp_wifi` vs Hosted WiFi over the C6 co-processor), UART/RMT/LEDC
  budgets, partition tables, PSRAM policy. Chip-wide code, flags, and docs
  say **ESP32-P4** / `esp32p4`, never a board name.
- **Board Variant**: a thin pin-map block in `include/config.h` plus one
  PlatformIO env per physical board — `artoo_esp32` and `firebeetle2`.
  A variant holds pins and board quirks only.

Naming is part of the decision:

- **artoo-esp32** (env/variant id `artoo_esp32`, macro `PA_BOARD_ARTOO_ESP32`)
  is the canonical name for the artoo.uk build target, replacing the informal
  "classic". The rename of the existing PlatformIO env lands all-at-once —
  Makefile defaults, CI references, and docs in the same change — leaving no
  alias behind.
- **firebeetle2** names only the DFRobot dev board. Anything that would be
  true of another P4 board belongs to the chip target and is named ESP32-P4.

The acceptance test for the abstraction: adding a further board on an
already-supported chip must cost a new pin-map block plus an env, nothing
else.

## Considered options

- **Single-layer per-board port** (FireBeetle pins and quirks woven directly
  into the P4 support) — every later P4 board becomes a second porting
  effort, and chip-wide fixes have to be untangled from board pins after the
  fact. Rejected.
- **Runtime board detection** (one image probes its board) — larger images on
  the flash-constrained artoo-esp32, boot-time probing on safety-relevant
  pins, and no reliable way to probe pin wiring at all. Board identity is a
  compile-time fact. Rejected.
- **Keeping "classic" as the board name** — collides with "classic-generation
  ESP32" as a chip-family description, and names the board by what it is not.
  Rejected in favor of artoo-esp32.

## Consequences

- CI builds every supported env; the per-env size budgets (ADR 0029) ride on
  that.
- The FireBeetle 2 spec sheet (`docs/spec-sheets/`) holds board-variant
  facts; chip-target facts belong to the ESP32-P4 docs and code layer.
- CONTEXT.md gains the artoo-esp32, ESP32-P4 Target, and Board Variant terms;
  the Supported ESP32 Board entry now names artoo-esp32 explicitly.
