# Two-layer board abstraction: chip target + board variant

protoArtoo is adding a second controller family: the ESP32-P4 (first board:
DFRobot FireBeetle 2 ESP32-P4), alongside the fully supported artoo-esp32
(the classic-generation ESP32 D1 Mini clone on the artoo.uk Artoo Controller
PCB). The P4 work must not weld itself to one development board: a P4-based
controller is a category, and the FireBeetle 2 is one member of it.

We decided board support is structured as **two layers**, making explicit the
split that `include/config.h` already practices implicitly:

- **Chip target** (`PA_BOARD_*`, e.g. the ESP32-P4 Target): everything true
  of *any* board built on that chip — UART/RMT/LEDC budgets, partition tables,
  PSRAM policy, and whether networking needs an external-backend seam. The
  ESP32-P4 has no native radio, but that does not select a companion chip or
  transport. Chip-wide code, flags, and docs say **ESP32-P4** / `esp32p4`,
  never a board name.
- **Board Variant**: the per-physical-board layer — `artoo_esp32` and
  `firebeetle2`. It owns the pin map, fitted devices, transport and reset
  wiring, provisioning/lifecycle requirements, and board quirks. The
  FireBeetle 2 therefore owns its fitted ESP32-C6 and C6-over-SDIO topology;
  these are not properties of every ESP32-P4 board.

Naming is part of the decision:

- **artoo-esp32** (env/variant id `artoo_esp32`, macro `PA_BOARD_ARTOO_ESP32`)
  is the canonical name for the artoo.uk build target, replacing the informal
  "classic". The rename of the existing PlatformIO env lands all-at-once —
  Makefile defaults, CI references, and docs in the same change — leaving no
  alias behind.
- **firebeetle2** names only the DFRobot dev board. Anything that would be
  true of another P4 board belongs to the chip target and is named ESP32-P4.

The current acceptance test for adding another ESP32-P4 Board Variant is one
pin-map block, one PlatformIO env, and one entry in the Makefile's explicit
`P4_ENVS` toolchain-selection list. The list is a deliberate exception to the
otherwise two-layer board definition; the Makefile does not inspect
`platformio.ini` to discover a target's chip.

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
- A Board Capability Gate declares that the relevant physical topology is
  fitted. It does not attest that a co-processor is correctly provisioned,
  booting, initialized, or reachable at runtime.
- CONTEXT.md gains the artoo-esp32, ESP32-P4 Target, and Board Variant terms;
  the Supported ESP32 Board entry now names artoo-esp32 explicitly.
- A Board Variant may declare **no** network backend (ADR 0032,
  Network-Optional Operation). Such a board compiles without a web server and
  keeps every droid function; there is no at-least-one-backend requirement.
