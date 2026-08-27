# FireBeetle 2 ESP32-P4 Spec Sheet

Working spec for using the DFRobot FireBeetle 2 ESP32-P4 and DFR1237 IO
expansion board as a controller candidate.

Sources re-verified 2026-08-07. The previous revision of this sheet (2026-05-19)
was written against the ESP32-P4 datasheet DFRobot hosts on its wiki, which has
since been superseded twice over. See [Chip Revision: v1.x vs
v3.x](#chip-revision-v1x-vs-v3x) first; it changes the toolchain configuration.

**This project's bench board reports chip revision v1.3**, read from the board
over USB on 2026-08-21 (see [Identifying the
revision](#identifying-the-revision) for the verbatim output). An earlier
revision of this sheet recorded v1.0 for the unit in hand; v1.3 is the
verified reading for the board this project actually builds against.

That correction changes no toolchain setting. v1.0 and v1.3 are both v1.x and
are firmware-interchangeable, and DFRobot ships units across the v1.x family —
so **both are documented throughout this sheet, and you must check your own
board rather than assume it matches ours.** All toolchain guidance below is
written for the v1.x family as a whole; the only split that changes
configuration is v1.x versus v3.x.

## Boards Covered

| Item | SKU | Notes |
| --- | --- | --- |
| FireBeetle 2 ESP32-P4 AI Vision Board | DFR1172 | ESP32-P4 main board with ESP32-C6 wireless co-processor |
| FireBeetle 2 ESP32-P4 Edge AI & Video Processing Kit | DFR1237 | DFR1172 plus passive IO expansion board |

DFRobot markets the main board as "ESP32-P4R32". That is a DFRobot shorthand for
"P4 with 32 MB PSRAM", not an Espressif part number. Espressif's part numbers are
`ESP32-P4NRW16` / `ESP32-P4NRW32` (chip revision v1.x) and
`ESP32-P4NRW16X` / `ESP32-P4NRW32X` (chip revision v3.x).

## Project Integration

This spec sheet is the authoritative hardware reference for the FireBeetle 2 target. GPIO allocations and pin assignments are linked to the project's canonical pin map:

- **[`docs/pin_map.md`](../pin_map.md)** — human-readable cross-reference for all production GPIO assignments, the exposed pin budget, and constraint notes. **Start here for pin questions.**
- **[`include/config.h`](../../include/config.h)** (firebeetle2 block) — compile-time GPIO configuration used by the firmware
- **[`include/firebeetle_required_pins.inc`](../../include/firebeetle_required_pins.inc)** — production pin inventory and static_assert guards

## Official Sources Checked

| Source | URL | Extraction notes |
| --- | --- | --- |
| DFR1172 wiki | https://wiki.dfrobot.com/dfr1172/ | Product specs, onboard pin definitions, Arduino setup |
| DFR1237 wiki | https://wiki.dfrobot.com/dfr1237/ | IO expansion board resources and product specs |
| DFR1237 schematics ZIP | https://dfimg.dfrobot.com/wiki/19348/DFR1237_firebeetle-2-esp32-p4-kit_schematics_V1.0.zip | **Authoritative for header geometry.** Contains a text-extractable KiCad 9.0.0 PDF of the IO expansion board (rev V1.0.0, 2025-06-05). The bundled main-board schematic is raster. |
| DFR1237 datasheet ZIP | https://dfimg.dfrobot.com/wiki/19348/DFR1237_firebeetle-2-esp32-p4-kit_datasheet_V1.0.zip | Contains `esp32-p4-chip-revision-v1.3_datasheet_en.pdf` (Pre-release v0.6, 2025-10-23) plus the mic datasheet. This is the datasheet that applies to the silicon DFRobot ships. |
| DFR1172 datasheet PDF | https://dfimg.dfrobot.com/wiki/21103/DFR1172_firebeetle-esp32-p4r32-development-board_datasheet_V1.0.pdf | ESP32-P4 Series Datasheet Pre-release v0.5, 2025-06-03, 89 pages. **Stale.** Superseded by the v1.3 datasheet in the DFR1237 kit ZIP. |
| ESP32-P4 Datasheet (current) | https://documentation.espressif.com/esp32-p4_datasheet_en.pdf | Pre-release v0.7, 2026-07-14, 101 pages. **Documents chip revision v3.x only.** Do not apply its 400 MHz / v3.x figures to a v1.x board. |
| ESP32-P4 Chip Revision v3.x User Guide | https://documentation.espressif.com/esp32-p4-chip-revision-v3.x_user_guide_en.html | v1.0, 2026.03. Design deltas, ESP-IDF version floor, silkscreen identification |
| ESP32-P4 Series SoC Errata | https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32p4/esp-chip-errata-en-master-esp32p4.pdf | Errata doc v1.3, 2026-07-22. Per-revision affected matrix, chip-marking and eFuse revision identification |
| ESP-IDF `Kconfig.hw_support` (esp32p4) | https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_hw_support/port/esp32p4/Kconfig.hw_support | `ESP32P4_SELECTS_REV_LESS_V3` and `ESP32P4_REV_MIN` semantics |
| ESP-Hosted-MCU | https://github.com/espressif/esp-hosted-mcu | Architecture, transport comparison and throughput table, `docs/features.md`, `docs/troubleshooting.md` |
| `esp_hosted` component | https://components.espressif.com/components/espressif/esp_hosted | v3.0.6; ESP-IDF component form of ESP-Hosted |
| arduino-esp32 `esp32-hal-hosted.c/.h` | https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-hosted.c | Co-processor version query and slave OTA API |
| arduino-esp32 `WiFiGeneric.cpp` | https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/src/WiFiGeneric.cpp | `CONFIG_ESP_HOSTED_ENABLED` behavioural deviations |
| ESP32-P4 product page | https://www.espressif.com/en/products/socs/esp32-p4 | Confirms the P4 has no radio and requires a companion chip |
| arduino-esp32 `boards.txt` / variant | https://github.com/espressif/arduino-esp32 | Dedicated `dfrobot_firebeetle2_esp32p4` board entry and `pins_arduino.h`, present since core 3.3.11 |
| pioarduino platform | https://github.com/pioarduino/platform-espressif32 | Board JSON inventory and bundled framework versions |
| DFR1237 dimension drawing PDF | https://dfimg.dfrobot.com/wiki/19348/DFR1237_firebeetle-2-esp32-p4-kit_dimension_V1.0.pdf | Raster PDF; OCR/image inspection required |

## Chip Revision: v1.x vs v3.x

Espressif released ESP32-P4 chip revision v3.x on 2026-05-08. It is a wafer-level
change, not a firmware change, and it is **not firmware-compatible** with the
earlier v1.x silicon.

Espressif's `vM.X` scheme makes the compatibility rule explicit:

- **Major number change** (v1.x to v3.x): software is incompatible and must be
  rebuilt for the new target.
- **Minor number change** (v1.0 to v1.3): software is compatible; no change
  needed.

So the only split that matters for this board is v1.x versus v3.x. v1.0 and v1.3
are interchangeable from a firmware standpoint.

| | Chip revision v1.x | Chip revision v3.x |
| --- | --- | --- |
| Revisions in the wild | v0.0, v1.0, v1.3 | v3.0, v3.1, v3.2 |
| Max HP CPU clock | 360 MHz | 400 MHz |
| Espressif part numbers | `ESP32-P4NRW16` / `ESP32-P4NRW32` | `ESP32-P4NRW16X` / `ESP32-P4NRW32X` |
| Applicable datasheet | "ESP32-P4 Chip Revision v1.3 Datasheet", Pre-release v0.6 | "ESP32-P4 Series Datasheet", Pre-release v0.7 |
| ESP-IDF | any P4-capable version | **v5.5.3+ or v6.0+ required** |
| `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` | `y` | `n` (the ESP-IDF default) |
| `CONFIG_ESP32P4_REV_MIN` options | v0.0, v0.1, v1.0 (max supported v1.99) | v3.0, v3.1 (max supported v3.99) |
| Arduino `build.chip_variant` | `esp32p4_es` | `esp32p4` |
| PlatformIO generic board | `esp32-p4` | `esp32-p4_r3` |

A vendor survey on 2026-08-21 (DFRobot, Waveshare, M5Stack, Olimex, 4D
Systems) found no development board verifiably shipping v3.x silicon yet;
v1.x remains what the market sells.

**DFR1172/DFR1237 ships v1.x silicon.** DFRobot's kit datasheet bundle contains
the revision-specific `esp32-p4-chip-revision-v1.3_datasheet_en.pdf` (file dated
2026-01-19), and Espressif's `dfrobot_firebeetle2_esp32p4` Arduino board entry
defaults to `build.chip_variant=esp32p4_es` and `build.f_cpu=360000000L`.
Individual units vary within the v1.x family, so **do not infer your board's
revision from this sheet - read it from your own board.** This project's bench
unit is **v1.3**. Because any difference inside v1.x is a minor-number
difference, nothing in the toolchain configuration changes either way.

Datasheet, by the revision you actually have:

- **v1.3** - use `esp32-p4-chip-revision-v1.3_datasheet_en.pdf` from the DFR1237
  kit ZIP. It matches the silicon exactly. This is the case for our bench board.
- **v1.0** - Espressif publishes no v1.0-specific datasheet. Use the same v1.3
  datasheet; minor-revision compatibility makes it applicable.

This also resolves the 360 vs 400 MHz question the previous revision of this
sheet left open: 360 MHz is not a conservative choice, it is the correct one for
this board. And it means the previous PlatformIO recommendation
(`board = esp32-p4_r3`) was wrong.

### Selecting for the wrong revision

- Building v3.x firmware for a v1.x chip, or the reverse, produces an image that
  will not download or will not boot.
- v1.x and v3.x cannot share one firmware image. If a fleet ever mixes both, it
  needs two build targets.
- ESP-IDF enforces this at boot via `CONFIG_ESP32P4_REV_MIN` and refuses to run
  on a chip below the configured minimum. **No pre-v3 build can reject a v1.0
  chip**: the only minimum-revision choices available under
  `ESP32P4_SELECTS_REV_LESS_V3=y` are v0.0, v0.1, and v1.0, all of which accept
  it. There is no v1.3 minimum to be caught out by.

### Identifying the revision

`esptool` prints the revision during connect. Read-only - it resets the chip
into download mode but writes nothing:

```
esptool --port /dev/ttyACM0 chip-id
```

Actual output from this project's bench board, `esptool` v5.3.0, 2026-08-21:

```
Detecting chip type... ESP32-P4
Connected to ESP32-P4 on /dev/ttyACM0:
Chip type:          ESP32-P4 (revision v1.3)
Features:           Dual Core + LP Core, 400MHz
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
MAC:                30:ed:a0:ea:15:cd
```

Look for the `Chip type:` line and read the revision in parentheses. Two
traps in that output:

- **The line to grep for changed.** Older `esptool` printed
  `Chip is ESP32-P4 (revision vX.Y)`; v5.x prints `Chip type:`. Matching on
  "Chip is" silently finds nothing on a current toolchain. v5.x also uses
  hyphenated subcommands (`chip-id`, `flash-id`) where older releases used
  `chip_id` / `flash_id`.
- **`Features: ... 400MHz` is not your chip's ceiling.** That line reports the
  ESP32-P4 *series* capability, not the die's max clock. On any v1.x board the
  maximum is 360 MHz - keep `build.f_cpu = 360000000L` and do not raise the
  clock on the strength of that line.

Without a serial connection, read the manufacturing-code line of the chip
marking:

| Chip revision | Manufacturing code |
| --- | --- |
| v0.0 | `X A XX` |
| v1.0 | `X C XX` |
| **v1.3** (our bench board) | **`X E XX`** |
| v3.0 | `X F XX` |
| v3.1 | `X G XX` |
| v3.2 | `X H XX` |

The revision is also encoded in eFuse: major in
`EFUSE_RD_MAC_SPI_SYS_2_REG[23]`, minor in `EFUSE_RD_MAC_SPI_SYS_2_REG[5:0]`.

### Errata affecting this board

Per ESP32-P4 Series SoC Errata v1.3 (2026-07-22), the same four errata affect
v0.0, v1.0 and v1.3 alike. Nothing is gained or lost by which v1.x revision a
board carries, so this table applies whichever one you have - including our
v1.3 bench board.

| Errata | Affects | Relevance here |
| --- | --- | --- |
| `RMT-176` - RMT continuous TX mode idle level is set by wrapped-back data rather than the end-marker `level` field | v0.0, v1.0, v1.3 | **Check this if RMT drives any line protocol.** Workaround is `RMT_IDLE_OUT_EN_CHn = 1`. ESP-IDF has bypassed it since v5.2, the first version supporting continuous TX mode |
| `I2C-308` - I2C slave fails on multiple-read in non-FIFO mode | v0.0, v1.0, v1.3 | Only if the P4 acts as an I2C *slave* |
| `APM-560` - unauthorized AHB access may block subsequent PSRAM or flash transactions | v0.0, v1.0, v1.3, v3.0 | Access-permission-management designs only |
| `ECDSA_DS-837` - signatures with invalid `s` values are incorrectly accepted | v0.0, v1.0, v1.3 | Only if the ECDSA digital-signature peripheral verifies signatures |

Note on the v3.x announcement: its "bugs fixed in v3.x" list (`MSPI-749/750/751`,
`ROM-764`, `Analog-765`, `DMA-767`) reads as though those were long-standing
defects, but the errata table marks every one of them as affecting **v3.0 only**.
They were regressions introduced in early v3.x silicon and fixed within that
family. They never affected v1.x. Do not treat them as open issues on this board.

What v1.x genuinely lacks relative to v3.x: 400 MHz, Zb bit-manipulation
extensions, 32 PMP entries, IO hold during deep sleep on all IOs, a 160 MHz I2S
clock source, expanded ISP/PPA features, P-384 ECC, and AES DPA countermeasures.

## Mechanical Notes

### Main Board: DFR1172

| Parameter | Value |
| --- | --- |
| PCB outline | 25.4 mm x 60 mm |
| Form factor | FireBeetle 2 footprint |
| Supplied headers | Two 20-pin 2.54 mm male headers, per DFRobot product listing |
| Socket into DFR1237 | 18-pin left row (`U2`), 14-pin right row (`U1`) |

### IO Expansion Board: DFR1237

From the KiCad schematic PDF in the DFR1237 schematics ZIP, plus the raster
dimension drawing.

| Parameter | Value | Source |
| --- | --- | --- |
| PCB outline | 45.00 mm x 60.00 mm | Dimension drawing |
| PCB thickness | 1.6 mm | Dimension drawing |
| Layers | 2 | Dimension drawing |
| Solder mask / silkscreen | Black / white, top and bottom | Dimension drawing |
| Drawing identifier | `DFR1237-V1.0.0-1.6mm-2 Layer-60*45mm`, 2025-06-05 | Dimension drawing |
| Mounting holes | **Four, M3.0** (`H1`-`H4`) | Schematic |
| Fiducials | Two (`FID1`, `FID2`) | Schematic |
| CAD tool | KiCad E.D.A. 9.0.0 | Schematic |

The previous revision of this sheet recorded the hole diameter as "not called
out". The schematic footprints are `MountingHole_M3.0`, so M3 is confirmed.

Mechanical clearances still to respect:

- Keep the two USB-C connectors, MIPI CSI/DSI connectors, TF card slot, reset
  button, BOOT button, and microphone area accessible if the main board is
  socketed into an enclosure.
- Treat the DFR1237 45 mm width as the envelope when the expansion board is
  installed; the DFR1172 module alone is only 25.4 mm wide.

## Electrical Summary

### Board level (DFR1172 wiki)

| Parameter | Value |
| --- | --- |
| Logic voltage | 3.3 V only |
| USB-C input | 5 V DC |
| VIN/VCC input | 5 V DC |
| Deep sleep current, 5 V VIN | 31.5 mA |
| Idle current, 5 V VIN | 80 mA |
| Wi-Fi AP current, 5 V VIN | 130 mA average, 1330 mA peak |
| Wi-Fi STA current, 5 V VIN | 80 mA average, 1050 mA peak |
| Operating temperature | -10 C to 60 C |

Do not put 5 V signals on GPIO headers. The 5 V rail is power only.

The 31.5 mA "deep sleep" figure is board level and is dominated by the
regulators and the ESP32-C6, not the P4. The P4 die itself draws 0.012 mA in
deep sleep with only the LP timer and LP memory powered. Budget from the board
figure, not the chip figure.

### Chip level (ESP32-P4 datasheet)

| Parameter | Value |
| --- | --- |
| GPIO input high | min 0.75 x VDD |
| GPIO input low | max 0.25 x VDD |
| GPIO output high | min 0.8 x VDD (high-impedance load) |
| GPIO output low | max 0.1 x VDD (high-impedance load) |
| GPIO source current | typ 40 mA at VDD = 3.3 V, `PAD_DRIVER = 3` |
| GPIO sink current | typ 28 mA at VDD = 3.3 V, `PAD_DRIVER = 3` |
| Internal pull-up/down | typ 45 kOhm |
| Pin capacitance | typ 2 pF |
| Input leakage | max 50 nA |
| Ambient temperature (silicon) | -40 C to 85 C |

The silicon is rated -40 to +85 C; DFRobot rates the assembled board -10 to
+60 C. Use the board figure.

Active-mode current at 360 MHz is not tabulated separately; the v0.7 datasheet
tabulates 400 MHz. As an order-of-magnitude reference at 400 MHz with all
peripheral clocks enabled: 56 mA dual-core WAITI, 112 mA dual-core spin loop,
150 mA dual-core 32-bit data access. Expect roughly 90 percent of those at
360 MHz.

ESP32-P4 supports a per-pin input hysteresis filter (`gpio_config_t::hys_ctrl_mode`,
disabled by default). Worth enabling on slow-edged or long-run digital inputs.

## ESP32-P4 / Board Resources

| Resource | Value |
| --- | --- |
| Main SoC | ESP32-P4, chip revision **v1.3** on our bench board (verified 2026-08-21 by `esptool`, and again 2026-08-22 by the running firmware reporting `Revision: 103`); DFRobot ships across the v1.x family, so check your own |
| HP CPU | RISC-V 32-bit dual-core, **360 MHz max on this board** (the `400MHz` in `esptool` output is a series figure, not this die's ceiling) |
| LP CPU | RISC-V 32-bit single-core, 40 MHz |
| PSRAM | 32 MB in package |
| Flash on DFR1172 | 16 MB external QSPI flash. `esptool flash-id` on our board reports manufacturer `ef`, device `4018` - a Winbond W25Q128-class part - and detects 16MB |
| HP L2 memory | 768 KB |
| HP TCM | 8 KB zero-wait |
| LP SRAM | 32 KB |
| HP ROM | 128 KB |
| LP ROM | 16 KB |
| User eFuse | 1792 bits user accessible from 4096-bit OTP |
| Total GPIOs on package | 55 |
| Package | QFN104, 10 x 10 mm |

Wireless is not on this die. See the next section.

## Wireless: ESP-Hosted over SDIO

### The P4 die has no radio

No Wi-Fi, no Bluetooth, no antenna, no PHY. This is a property of the SoC, not of
this board, and it does not change between chip revisions. Espressif's own
product page states the arrangement plainly: *"If the application requires
wireless connectivity, any product from the ESP32-C/S series can serve as a
wireless companion chip for ESP32-P4, connecting via SPI/SDIO/UART interfaces
using ESP-Hosted or ESP-AT solutions."*

DFRobot's marketing ("integrates WiFi/Bluetooth", "Wi-Fi 6") is true at the
**board** level and false at the **SoC** level. What the board adds is a
physically separate **ESP32-C6** wired to the P4 over a 4-bit SDIO bus, running
**ESP-Hosted-MCU** as a communication co-processor.

Every wireless capability in this section belongs to the C6. The seven GPIOs the
link consumes are the tell: an on-die radio does not need a bus.

### Transport pins

Declared by Espressif's Arduino variant for this board
(`variants/dfrobot_firebeetle2_esp32p4/pins_arduino.h`) and matched by the DFR1172
wiki `WiFi` pin table:

| Variant define | GPIO | Role |
| --- | --- | --- |
| `BOARD_SDIO_ESP_HOSTED_CLK` | GPIO18 | SDIO clock |
| `BOARD_SDIO_ESP_HOSTED_CMD` | GPIO19 | SDIO command |
| `BOARD_SDIO_ESP_HOSTED_D0` | GPIO14 | SDIO data 0 |
| `BOARD_SDIO_ESP_HOSTED_D1` | GPIO15 | SDIO data 1 |
| `BOARD_SDIO_ESP_HOSTED_D2` | GPIO16 | SDIO data 2 |
| `BOARD_SDIO_ESP_HOSTED_D3` | GPIO17 | SDIO data 3 |
| `BOARD_SDIO_ESP_HOSTED_RESET` | GPIO54 | C6 reset (wiki calls it `EN`) |

`BOARD_HAS_SDIO_ESP_HOSTED` selects these automatically in
`cores/esp32/esp32-hal-hosted.c`. They can be overridden at runtime before
`WiFi.begin()` via `WiFiGenericClass::setPins(clk, cmd, d0, d1, d2, d3, rst)`,
which exists only when `CONFIG_ESP_HOSTED_ENABLED` is set.

The DFR1172 wiki additionally lists a `WAKEUP` line on GPIO6. The Arduino variant
does not define it, and it is **not required for boot or link bring-up**: in the
esp-hosted-mcu Kconfig, GPIO6 is the host-wakeup input, used only when
`ESP_HOSTED_HOST_DEEP_SLEEP_ALLOWED` is enabled so the slave can wake a sleeping
host. Whether it is physically routed to the C6 on this board is still unread
from the main-board schematic; nothing in the normal path depends on it.

> [!NOTE]
> **GPIO54 polarity: a contradiction on paper, tested, and not the cause.** The
> DFR1172 wiki calls this pin `EN`. On an ESP32-C6-MINI-1, `EN` is chip-enable:
> HIGH runs the module, LOW holds it in reset - an **active-LOW** reset. The
> bundled `esp_hosted` component builds with
> `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=1`, under which the host would
> release reset by driving the pin **LOW**. The two readings disagree.
>
> Tested 2026-08-22: both `RESET_ACTIVE_HIGH` symbols set to `n` and **verified
> applied in the generated config** before flashing. The failure was identical.
> Inverting the polarity changes nothing on this board, so this is not the cause
> of the link failure. Left recorded because the paper contradiction is real and
> will confuse the next reader otherwise.

GPIO14/GPIO15 doubling as the `LP_UART` IO MUX pads is why LP UART is impractical
on this board while Wi-Fi is in use, and GPIO16-GPIO19 being `ADC1_CHANNEL0-3` is
why only GPIO20-GPIO23 remain as reachable ADC1 inputs.

### How it works

Two distinct paths share the SDIO bus:

- **Control plane.** Application Wi-Fi calls (`esp_wifi_init()`,
  `esp_wifi_connect()`, ...) go to `esp_wifi_remote`, which forwards them to
  ESP-Hosted. The host encodes them as protobuf RPC requests, ships them over
  SDIO, and the C6 deserialises, executes, and replies. To the application it
  looks like an ordinary ESP-IDF Wi-Fi call.
- **Data plane.** Network packets are **not** serialised - they move as raw
  frames. Only RPC control traffic pays the protobuf cost.

Asynchronous Wi-Fi events raised by the C6 are delivered to the host and
terminate in the standard ESP-IDF event loop, so `WiFi.onEvent()` and the
`ARDUINO_EVENT_WIFI_*` handlers behave normally.

In Arduino terms the entire `WiFi` class compiles under
`#if SOC_WIFI_SUPPORTED || CONFIG_ESP_HOSTED_ENABLED`. `WiFi.begin()`,
`WiFi.softAP()`, scanning, events, and the whole `Network`/`NetworkInterface`
stack above it are unchanged. ESP-Hosted is an API-transparent layer, not an
AT-command modem.

### The C6 as hardware: module, factory firmware, reflashing

Confirmed 2026-08-22 against the DFR1172 wiki and by inspecting the vendor
binary. Previously this sheet described the C6 only as "a physically separate
ESP32-C6"; these are the specifics.

| Fact | Detail | Source |
| --- | --- | --- |
| Module | **ESP32-C6-MINI-1** | DFR1172 wiki, "On-board Function Diagram" table |
| Physical location | **Underside** of the FireBeetle 2, with its own PCB antenna | wiki table; confirmed visually on the bench board |
| Factory firmware | `C6_v14_eco2_0022.bin`, 1178352 bytes, dated 2025-02-12 | DFRobot-published ZIP linked from wiki doc 21646 |
| Factory firmware type | **ESP-Hosted-MCU slave firmware** - not ESP-AT, not blank | strings in the binary: `ESP-Hosted-MCU Slave FW version :: %d.%d.%d`, `esp_hosted_rpc.pb-c.c`, `fg_mcu_slave`, `./main/sdio_slave_api.c` |
| Factory firmware age | Built from `esp_as_mcu_host`, the project's **former** name - an early build | build path string in the binary |

**The C6 is user-reflashable, and DFRobot documents it**:
`https://wiki.dfrobot.com/dfr1172/docs/21646`, "ESP32-C6 Firmware Update Guide".
A USB-TTL adapter is jumpered to the C6 side and the image written with Flash
Download Tool or any esptool-compatible writer; if the download fails, hold `RST`
on the board while starting it. The wiring diagram on that page is a raster
image, so the jumper pin mapping has to be read off it visually - it is not
transcribed here rather than guessed.

> [!NOTE]
> The DFR1237 `UART` header is **not** a route to the C6. It exposes the P4's
> `UART0` pair plus power only (see "IO Expansion Header Map"). Reflashing the C6
> requires the connection in wiki doc 21646.

### Measured: first boot does not bring the link up

Recorded 2026-08-22 on the bench board, `CONFIG_ESP_HOSTED_ENABLED=y`,
arduino-esp32 3.3.11 / ESP-Hosted 2.12.11, factory C6 firmware untouched:

```
E sdmmc_io: sdmmc_init_io: sdmmc_io_send_op_cond (1) returned 0x107
E sdio_wrapper: sdmmc_card_init failed
E H_SDIO_DRV: card init failed
E transport: ensure_slave_bus_ready failed
E H_API: ESP-Hosted link not yet up
[E][WiFiGeneric.cpp:291] wifiLowLevelInit(): esp_wifi_init 0xffffffff: ESP_FAIL
```

`0x107` is `ESP_ERR_TIMEOUT`. The failure is at **SDIO card enumeration**, below
the ESP-Hosted protocol - the slave is not answering electrically at all, which
is a different class of fault from a host/slave version mismatch (that would fail
later, at handshake or RPC). The P4 side is otherwise healthy: USB CDC console,
timers, heap and application code all run normally.

Ruled out by measurement, so nobody repeats them:

- **Host pin configuration** - the variant defines match the table above, and
  `hostedInit()` demonstrably copies them into `esp_hosted_sdio_config`.
- **The TF card slot** - it is SDMMC **slot 0** (GPIO39-45); ESP-Hosted uses
  **slot 1** (`CONFIG_ESP_HOSTED_SDIO_SLOT 1`). No pin or slot overlap. A seated
  card is irrelevant.
- **`CONFIG_ESP_HOSTED_P4_DEV_BOARD_NONE=1`** - benign. It only defers deep-sleep
  wakeup GPIO configuration; it does not disable or misconfigure SDIO.
- **Application code driving GPIO54** - removed entirely and reflashed; identical
  failure. The hosted driver owns the pin.

### Configuration gotcha: three Kconfig namespaces, one effective

Hit twice while eliminating the hypotheses above, and it silently invalidates
experiments. The generated `sdkconfig.defaults` carries the same ESP-Hosted SDIO
settings under three prefixes:

| Namespace | Example | Role |
| --- | --- | --- |
| `CONFIG_ESP_HOSTED_SDIO_*` | `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` | the user-facing Kconfig option |
| `CONFIG_SDIO_*` | `CONFIG_SDIO_CLOCK_FREQ_KHZ` | second copy |
| `CONFIG_ESP_SDIO_*` | `CONFIG_ESP_SDIO_CLOCK_FREQ_KHZ` | **the effective transport config** |

The third holds the values the Arduino HAL's fallback branch reads
(`CONFIG_ESP_SDIO_PIN_CLK=18`, `..._PIN_CMD=19`, `..._GPIO_RESET_SLAVE=54`),
plus `..._BUS_WIDTH=4` and the clock.

> [!CAUTION]
> Setting `CONFIG_ESP_HOSTED_SDIO_<X>` in `custom_sdkconfig` does **not**
> propagate to `CONFIG_ESP_SDIO_<X>`. The build accepts the line, reports
> success, and the effective value is unchanged. Always grep the generated
> `sdkconfig.defaults` for **every** variant of a symbol before believing an
> override applied - and before treating a negative result as evidence.
>
> Exception worth knowing: there is no `CONFIG_ESP_SDIO_RESET_ACTIVE_HIGH`. Only
> two `RESET_ACTIVE_HIGH` symbols exist in the whole generated config.

### ⚠️ Programming the C6: what is verified, and one hazard

Established on the bench 2026-08-22. Full working notes in issue #198.

> [!CAUTION]
> ⛔ **Do NOT connect the programmer's `5V` to the pad silkscreened `NC`.**
> DFRobot wiki doc 21646's wiring diagram shows exactly that, and following it
> made the board **heat up on the opposite face, near the DSI/CSI FPC connectors**,
> where the ESP32-P4 and its power circuitry sit. Power was removed immediately
> and the board was undamaged, but the connection is wrong. Espressif's generic
> ESP-Hosted guidance - *do not connect VDD* - is correct here and the
> board-specific diagram is not. Power the board from its own USB-C.

**For passive UART console capture** (receive-only observation of the C6's boot output,
non-destructive; chip remains running from flash), verified by capturing 46.5 KB in 20 s
with no ground wire to the pads, and by #198 Phase A's 118-cycle boot log:

| USB-TTL (3V3 logic) | FireBeetle 2 |
| --- | --- |
| `TXD` | (not connected — receive only) |
| `RXD` | pad **3** of the four gold pads below the beetle logo (underside) |
| `GND` | (not needed — FT232 and board share ground via host USB) |
| ⛔ `5V` | **not connected** |

> [!WARNING]
> **DFRobot doc 21646 has three documentation defects.** (1) Its wiring diagram
> routes `5V` to the `NC` pad, which heats the board (CAUTION above). (2) The
> **Test Points** figure (labelling `ESP32C6_IO9/BOOT / RX / TX / GND / 3V3 /
> RST`) is **correct**; the **Hardware Connection** figure is **wrong**. (3) The
> Hardware Connection figure places the GND signal in download-mode boot
> position, which accidentally straps `ESP32C6_IO9/BOOT` to ground. The pad
> order (confirmed by continuity on 2026-08-23, all jumpers removed) is
> **left-to-right: `ESP32C6_IO9/BOOT` — `ESP32C6_RX` — `ESP32C6_TX` — `GND`
> (corner).** The corner pad (`GND`) is nearest the `48/49/50` header column and
> is the only one of the four with `3V3` and `ESP32C6_RST` stacked directly
> beneath it. Count from the header, not from a board edge.
> 
> **For passive UART console capture** (observe the C6's boot output, non-destructive):
> use **one contact only** — `RXD` → pad 3 (`ESP32C6_TX`). Pad 1 must be **free** (not
> grounded); the FT232 and board already share ground through the host USB, so no ground
> wire to the pad array is needed. Grounding pad 1 forces download mode and prevents boot
> from flash. Confirm contact on pad 3 by traffic, not by voltage: a floating FT232
> `RXD` idles at ~3.3 V via its own internal pull-up, so a meter reading proves nothing.
> Only received bytes prove contact.
>
> **For flash download mode (erase/write/read):** **ground pad 1** (`ESP32C6_IO9/BOOT`)
> to force download mode, plus `TXD` → pad 2 (`ESP32C6_RX`) and `RXD` → pad 3
> (`ESP32C6_TX`). This configuration is verified by `esptool chip-id` and by full 4 MB
> read-back. Without pad 1 grounded, the chip boots from flash instead.

The host P4 must be prevented from driving GPIO54 (the C6 reset line); parking
it in ROM bootloader does this without holding buttons:

```bash
esptool --chip esp32p4 --port <P4> --before default-reset --after no-reset flash-id
#   -> "Staying in bootloader."
```

### C6 flash layout

Read from the chip, and corrected 2026-08-22 (see #198): the board is **factory
misprovisioned**. A complete merged image (bootloader + partition table + app) was
written at global `0x10000` instead of `0x0`, so the chip carries two boot chains,
one nested inside the other:

| Global offset | What is there |
| --- | --- |
| `0x0` | outer C6 bootloader (entry `0x4086c410`) |
| `0x8000` | outer partition table (`0xAA50`): `factory` app @ `0x10000`, 1024 KiB |
| `0x10000` | **inner** bootloader (entry `0x4086c11c`) - the merged image's own loader |
| `0x18000` | **inner** partition table (`0xAA50`, valid MD5) - the coherent dual-OTA layout |
| `0x20000` | the **real** app: `network_adapter`, `83efce6-dirty`, ESP-IDF v5.4-dev, Oct 24 2024 |

Inner table (the coherent one, matching esp-hosted-mcu commit `83efce6`; offsets are
relative to a base of `0x0`, i.e. this image expects to live at `0x0`):

| Label | Type | Offset | Size |
| --- | --- | --- | --- |
| `nvs` | data | `0x9000` | 16 KiB |
| `otadata` | data | `0xd000` | 8 KiB |
| `phy_init` | data | `0xf000` | 4 KiB |
| `ota_0` | app | `0x10000` | 1536 KiB |
| `ota_1` | app | `0x190000` | 1536 KiB |

> [!WARNING]
> **The chip is misprovisioned, not merely oddly partitioned - do not "fix" the
> outer table.** The on-chip bytes from `0x10000` onward are DFRobot's published
> `C6_v14_eco2_0022.bin` **in full, byte-for-byte** (SHA-256 of the shifted slice
> `d8625fd1...35ce3a`), a complete merged image that DFRobot's own Flash Download
> Tool screenshot flashes at `0x0` (this is the exact file in DFRobot's current "factory
> default firmware" ZIP from wiki doc 21646 - downloaded 2026-08-22 and hash-verified
> byte-identical to the on-chip bytes). Here it was written at `0x10000`. The outer
> bootloader therefore **selects `factory @ 0x10000` and attempts to validate it** -
> but that image is another **bootloader**, not an app, and its first RAM segment
> (`0x40875720-0x40876d5c`) overlaps the running outer bootloader's own DRAM, so the
> ESP-IDF image loader rejects it (`overlaps bootloader data` / `not bootable`) and
> resets. It does **not** recurse into the nested loader. This malformed chain
> plausibly explains why the C6 never runs its app and never brings up its SDIO slave
> (the CMD5/`0x107` timeout). The C6 silicon itself is healthy - it answers esptool -
> so this is a misprovisioned flash **state**, not dead silicon, and a correct reflash
> should fix it. The placement error is byte-proven; the exact runtime failure mode is
> inference until a C6 UART boot log is captured.
>
> The earlier "app-only image overflowing a 1024 KiB `factory` partition" reading in
> this sheet was **wrong and is retracted** (credit: independent analysis by Codex,
> reproduced here with `esptool image-info` and `gen_esp32part.py`). The
> 129,776-byte "overhang" is the tail of a whole merged image written at the wrong
> base, not an app spilling its slot.

> [!NOTE]
> **Reflash (only if we choose to reflash rather than RMA - see #198):** write a
> coherent layout, do not patch the nested one. Verify the 4 MB backup hash is
> stored safely and run read-only `esptool get-security-info` first (stop on
> secure-boot / flash-encryption / secure-download). Then: erase the C6; flash
> DFRobot's complete merged `C6_v14_eco2_0022.bin` at `0x0` exactly as DFRobot
> documents; read back and hash-compare; boot once and capture the C6 log to prove
> the nested/shifted layout is gone. Only then apply the 2.12.11 update at
> Espressif's published offsets (`ota_data_initial.bin` @ `0xd000`, then
> `esp32c6-v2.12.11.bin` @ `0x10000`) - the 1,226,800-byte app fits the coherent
> 1536 KiB `ota_0` slot with **346,064 bytes spare**, so there is no need to invent
> a larger `factory` table. **Do not** flash the new app into the current nested
> layout, and **do not** hand-build a larger outer `factory` entry to legitimise the
> overhang - that fixes the wrong layer and discards the coherent dual-OTA structure.

### Current status

The link has never been observed to come up on this board. Every host-side lever
reachable from `custom_sdkconfig`, the Arduino variant, or the application has been
tried and measured; the failure sits at SDIO CMD5 enumeration
(`sdmmc_io_send_op_cond`, `0x107` timeout), below the ESP-Hosted protocol. The
leading explanation is no longer "old slave firmware" but a **byte-proven factory
misprovisioning** (see the layout above): the merged C6 image sits at `0x10000`
instead of `0x0`, so the C6 most likely never reaches its real app. This is a reflash
of a misprovisioned unit (misprovisioned flash state, not dead silicon), not routine provisioning - escalate the evidence
to DFRobot in parallel (#198). The single most decisive non-destructive test is a
**C6 UART boot log** on a cold start (TX/RX/GND only, never adapter 5 V), which shows
directly which bootloader / table / app the C6 selects; measuring the GPIO54 (`EN`)
level is still worthwhile. Note esp-hosted-mcu #127 (M5Stack Tab5, P4+C6) hits the
identical `send_op_cond 0x107` with a HEALTHY, booting slave, so 0x107 is ambiguous
(dead slave vs live slave + host-link failure) - the UART boot log tells them apart, and
a reflash may be necessary but not sufficient. #198 carries the reasoning, the reflash procedure, and
the risks.

### Wireless capability (from the ESP32-C6)

| Parameter | Value |
| --- | --- |
| Wi-Fi standards | 802.11 b/g/n/ax |
| Wi-Fi 6 (802.11ax) caveat | 20 MHz only, non-AP mode |
| Band | 2.4 GHz only - **no 5 GHz** |
| Bandwidth | 20 MHz and 40 MHz |
| Modes | Station, SoftAP, SoftAP+Station, promiscuous |
| Aggregation | TX/RX A-MPDU, TX/RX A-MSDU |
| Bluetooth | BLE via ESP-Hosted / UART HCI, for NimBLE or Bluedroid |

### Throughput

Espressif's published figures for the SDIO 4-bit transport with an ESP32-C6
co-processor, measured in a shield box at 40 MHz bandwidth:

| Direction | UDP | TCP |
| --- | --- | --- |
| Host TX | 79.5 Mbit/s | 53.4 Mbit/s |
| Host RX | 68.1 Mbit/s | 44 Mbit/s |

For context, SDIO 4-bit is the highest-performing ESP-Hosted transport; standard
SPI manages roughly 22-25 Mbit/s and UART about 0.7 Mbit/s. These are Espressif's
shield-box numbers on a reference design, not measurements on this board, and
real over-the-air results will be lower. But the headline is that the hosted
architecture is not the bottleneck - SDIO 4-bit comfortably exceeds what native
ESP32 Wi-Fi typically delivers in practice. Latency and jitter under concurrent
load are the properties worth measuring, not raw bandwidth.

### Feature support

Implemented by ESP-Hosted-MCU:

- Wi-Fi Station, SoftAP, SoftAP+Station
- Wi-Fi Enterprise security (optional)
- Wi-Fi Easy Connect / DPP QR-code onboarding (optional)
- iTWT (802.11ax individual Target Wake Time) - supported by the C6
- Network Split, to divide traffic handling between host and co-processor
- Bluetooth over ESP-Hosted or UART HCI (NimBLE, Bluedroid)
- OpenThread / Zigbee over a dedicated UART
- Host Power Save, letting the P4 sleep and be woken by the C6
- GPIO Expander, letting the host drive the C6's GPIOs

**ESP-NOW is not in the implemented-features list.** If a design depends on
ESP-NOW - a common choice for body-to-dome links in droid builds - confirm it
before committing, because ESP-Hosted does not currently advertise it.

### Arduino behavioural differences

Verified in `libraries/WiFi/src/WiFiGeneric.cpp`. These are real deviations from
native-Wi-Fi ESP32 behaviour and are easy to trip over:

| Behaviour | Under ESP-Hosted |
| --- | --- |
| Wi-Fi credential persistence | **Disabled.** `wifiLowLevelInit()` forces `cfg.nvs_enable = false` and `persistent = false`. The Wi-Fi driver will not store or restore credentials, so a bare `WiFi.begin()` cannot reconnect from driver-held state. The application must own its credentials. |
| SmartConfig / ESP-Touch | Unavailable. All `SC_EVENT` handling is compiled out. |
| Network provisioning | Unavailable. `NETWORK_PROV_EVENT` registration is compiled out. |
| `setDualAntennaConfig()` | Unavailable. |

The credential point is the one most likely to cause a surprise, and it is benign
for any design that already keeps SSID and password in its own NVS namespace and
passes them explicitly to `WiFi.begin(ssid, pass)`.

### Co-processor firmware lifecycle

This is the genuinely new operational surface: **two firmware artefacts, on two
chips, that must stay compatible.** The Arduino core exposes an API for managing
it (`cores/esp32/esp32-hal-hosted.h`), so it is tractable rather than scary:

| Call | Purpose |
| --- | --- |
| `hostedGetHostVersion(&maj, &min, &patch)` | ESP-Hosted version bundled in the host image |
| `hostedGetSlaveVersion(&maj, &min, &patch)` | Version actually running on the C6 |
| `hostedGetSlaveTargetName()` | Co-processor target, e.g. `esp32c6`. Queried live from the slave when it is running ESP-Hosted 2.12.2 or newer |
| `hostedHasUpdate()` | Compares the two, logs both, returns true when the host is newer |
| `hostedGetUpdateURL()` | Builds `https://espressif.github.io/arduino-esp32/hosted/<target>-v<major>.<minor>.<patch>.bin`. See the version-scheme warning below - the version is **not** the Arduino core version |
| `hostedBeginUpdate()` / `hostedWriteUpdate()` / `hostedEndUpdate()` / `hostedActivateUpdate()` | Streams a new slave image to the C6 over the existing SDIO link |
| `hostedIsInitialized()` / `hostedIsWiFiActive()` / `hostedIsBLEActive()` | Link and radio state |

> [!WARNING]
> **The slave image version tracks the ESP-Hosted component, not arduino-esp32.**
> `hostedGetUpdateURL()` formats `host_version_struct`, which is populated from
> `ESP_HOSTED_VERSION_{MAJOR,MINOR,PATCH}_1` in
> `esp_hosted/host/esp_hosted_host_fw_ver.h`. For arduino-esp32 **3.3.11** those
> macros are **2 / 12 / 11**, so the image this toolchain expects is
> `esp32c6-v2.12.11.bin`. Verified 2026-08-22: that URL returns HTTP 200, while
> `esp32c6-v3.3.11.bin` returns **404**. Assuming the Arduino core version sends
> you to a file that does not exist.

Points that matter for a fielded device:

- **The C6 is updated over SDIO, not over wires.** No ESP-Prog, no jumpers, no
  disassembly. That removes the worst version of this problem.
- **Version skew is detected, not silently tolerated.** `hostedHasUpdate()` warns
  in both directions: host-newer prints an update URL, host-older prints
  "Version on Host is OLDER than version on co-processor".
- **Host-side OTA does not carry the slave.** Flashing a new P4 application
  leaves the C6 on whatever it was running. If a core upgrade bumps the bundled
  ESP-Hosted version, the slave OTA has to be driven separately - worth wiring
  into the device's own update flow rather than discovering it in the field.
- A dead or mismatched link surfaces as
  `E (…) transport: Not able to connect with ESP-Hosted slave device`.
- ESP-Hosted also ships as a standalone ESP-IDF component
  (`espressif/esp_hosted`, v3.0.6 at time of writing) for framework `espidf`
  builds, configured via `CONFIG_SLAVE_IDF_TARGET_ESP32C6` and
  `CONFIG_ESP_HOSTED_CP_TARGET_ESP32C6`.

### What to prove before relying on it

> [!IMPORTANT]
> Item 0, ahead of all of these: **the link has not yet been observed to come up
> on this board at all.** See "Measured: first boot does not bring the link up".
> Everything below assumes a working transport and remains unmeasured here.

Bandwidth is not the risk; the transport is fast. The things worth measuring on
this board specifically:

1. Behaviour of a long-lived SSE or WebSocket fan-out across the RPC boundary,
   including reconnect storms.
2. Socket and PCB accounting under concurrent connections, since host-side lwIP
   tuning was characterised against native Wi-Fi.
3. Recovery after a C6 reset or SDIO link fault - does the host stack come back,
   or does it need a reboot?
4. Whether host application OTA and slave OTA can coexist in one update flow.

### Issue-tracker signals (espressif/esp-hosted-mcu, checked 2026-08-21)

Open issues on the P4 + C6 SDIO transport that bear directly on the list
above. All states verified against the GitHub tracker on the check date.

| Issue | State | Title (abridged) | Bears on |
| --- | --- | --- | --- |
| [#197](https://github.com/espressif/esp-hosted-mcu/issues/197) (EHM-220) | open | SDIO data path wedges under sustained outbound TCP load; no recovery without RST, no liveness API | Items 1 and 3 |
| [#220](https://github.com/espressif/esp-hosted-mcu/issues/220) (EHM-252) | open | `sdio_read_regs()` reports success on an all-ones bus read, so a dead link is never declared failed and the host never restarts | Item 3 |
| [#121](https://github.com/espressif/esp-hosted-mcu/issues/121) (EHM-124) | open | Unrecoverable host SDIO state (P4 + C6) | Item 3 |
| [#167](https://github.com/espressif/esp-hosted-mcu/issues/167) (EHM-188) | open | Unrecoverable host SDIO state, second report | Item 3 |
| [#221](https://github.com/espressif/esp-hosted-mcu/issues/221) (EHM-253) | open | SDIO uplink overload: slave Wi-Fi task wedge, silent host RX stall | Items 1 and 2 |
| [#144](https://github.com/espressif/esp-hosted-mcu/issues/144) (EHM-156) | open | `sdio_rx_get_buffer` / `transport_drv_sta_tx` assert failures (P4 + C6) | Item 2 |
| [#219](https://github.com/espressif/esp-hosted-mcu/issues/219) (EHM-250) | open | SDIO TX DMA alignment rejection when TX buffers come from SPIRAM (`MEMPOOL_PREFER_SPIRAM`) | Item 2; relevant to any PSRAM-heavy design |
| [#230](https://github.com/espressif/esp-hosted-mcu/issues/230) (EHM-261) | open | Unguarded `_h_destroy_semaphore` in `wait_for_sync_response()` panics the calling task on RPC timeout | Item 3; an RPC timeout can kill the host task outright |
| [#226](https://github.com/espressif/esp-hosted-mcu/issues/226) (EHM-257) | open | `rpc_wifi_scan_get_ap_records()` memcpys from a NULL payload when the scan RPC response is lost | Host stability; avoid unguarded scan calls |
| [#212](https://github.com/espressif/esp-hosted-mcu/issues/212) (EHM-238) | open | P4 + C6: NimBLE `HCI_Reset` times out after advertising starts, Wi-Fi scan returns 0, persists across slave re-flash | Wi-Fi + BLE coexistence; avoid concurrent BLE use until resolved |

Takeaway for firmware design: several reports describe the link dying without
the host being told (no liveness API; a dead bus that still reads as success),
so host code should supervise the C6 itself - watchdog the transport, treat
prolonged silence as a fault, and own an escalation path (C6 reset via
GPIO54, then controlled host reboot). Issue states change; re-check before
relying on this table.

## Onboard Fixed Pin Use

Sourced from the DFR1172 wiki pin tables and cross-checked against Espressif's
`variants/dfrobot_firebeetle2_esp32p4/pins_arduino.h`.

| Function | ESP32-P4 pins | Notes |
| --- | --- | --- |
| User LED | GPIO3 | `LED_BUILTIN`. Also JTAG `MTDI` and `TOUCH_CHANNEL2` at silicon level |
| BOOT button | GPIO35 | Strapping pin, weak pull-up at reset |
| Default I2C labels | GPIO7 SDA, GPIO8 SCL | Arduino `SDA`/`SCL`. Also `T2`/`T3` touch aliases |
| UART0 | GPIO37 TX, GPIO38 RX | Arduino `TX`/`RX`; ROM/debug UART path |
| Microphone (PDM/I2S) | GPIO12 CLK, GPIO9 DATA | `MIC_I2S_CLK` / `MIC_I2S_DATA` |
| ESP32-C6 SDIO | GPIO14-GPIO19, GPIO54 reset, GPIO6 wakeup | See ESP-Hosted table above |
| TF card SDIO | GPIO39 D0, GPIO40 D1, GPIO41 D2, GPIO42 D3, GPIO43 CLK, GPIO44 CMD | Arduino `BOARD_SDMMC_SLOT 0` |
| TF card power enable | GPIO45 | `BOARD_SDMMC_POWER_PIN`, **active LOW**, power channel 4. The wiki calls this "EN"; it is a power switch, not an SDIO signal |

## IO Expansion Header Map

Taken from the DFR1237 KiCad schematic, which supersedes the OCR-derived list in
the previous revision of this sheet. All labels below are the actual net names on
the board.

### Module sockets

`U2` (`ESP32-P4_L`), 18 pins:

| Pin | Net | Pin | Net |
| --- | --- | --- | --- |
| 1 | `RST` | 10 | `7/SDA` |
| 2 | `3V3` | 11 | `48` |
| 3 | `NC` | 12 | `49` |
| 4 | `GND` | 13 | `50` |
| 5 | `NC` | 14 | `52` |
| 6 | `28/SCK` | 15 | `4` |
| 7 | `29/MO` | 16 | `5` |
| 8 | `30/MI` | 17 | `D1/TX` (GPIO37) |
| 9 | `8/SCL` | 18 | `D0/RX` (GPIO38) |

`U1` (`ESP32-P4_R`), 14 pins:

| Pin | Net | Pin | Net |
| --- | --- | --- | --- |
| 1 | `VIN` | 8 | `22/A2` |
| 2 | `3V3` | 9 | `21/A1` |
| 3 | `GND` | 10 | `20/A0` |
| 4 | `32/I3C/SCL` | 11 | `36` |
| 5 | `33/I3C/SDA` | 12 | `35` |
| 6 | `51/A4` | 13 | `34` |
| 7 | `23/A3` | 14 | `31` |

### Connectors

| Ref | Type | Contents |
| --- | --- | --- |
| `J3` / `J5` / `J6` | 3 x 17-pin main GPIO field | Signal row `J3`, `+3V3` row `J5`, `GND` row `J6` |
| `J2` | 5-pin SPI | `30/MI`, `29/MO`, `28/SCK`, `GND`, `+3V3` |
| `J9` | 4-pin UART | `TX`, `RX`, `GND`, `+3V3` |
| `J1` | 3-pin I2C | `7/SDA` + power/ground |
| `J7` | 3-pin I2C | `8/SCL` + power/ground |
| `J4` | 2x2 | `VIN`, `RST` |
| `J14` / `J15` | 3-pin | `+3V3` / `GND` rails |

`J3` signal order, pin 1 to pin 17:

`4`, `5`, `20/A0`, `21/A1`, `22/A2`, `23/A3`, `31`, `32/I3C/SCL`,
`33/I3C/SDA`, `34`, `35`, `36`, `48`, `49`, `50`, `51/A4`, `52`

Two things the previous revision of this sheet missed, both visible in the
schematic and both consequential:

- **GPIO20-GPIO23 are silkscreened A0-A3, and GPIO51 is A4.** They are the
  board's analog pins.
- **GPIO32/GPIO33 are silkscreened I3C SCL/SDA.** They are the P4's IO MUX I3C
  master pins, not generic GPIOs.

The `SPI`, `UART`, and `I2C` groupings are still convenience labels; ESP32-P4
routes most digital peripherals through the GPIO matrix. But the analog and I3C
labels above reflect real silicon capability, not convenience.

## GPIO Suitability

Datasheet v0.7 Section 2.3.5 introduced a formal priority taxonomy that this
sheet now adopts in place of its own ad-hoc ratings:

- **P1** - fixed IO MUX pins, or GPIO-matrix pins with peripheral-specific
  hardware (for example, I3C pins with configurable pull-ups).
- **P2** - any GPIO via the GPIO matrix, usable without restriction.
- **P3** - usable via the GPIO matrix, but conflicts with an important function:
  strapping (GPIO34-GPIO38), USB Serial/JTAG (GPIO24, GPIO25),
  **JTAG (GPIO2, GPIO3, GPIO4, GPIO5)**, UART0 (GPIO37, GPIO38).

UART2 through UART4 have no P1 pins at all, so they must come from the P2/P3
pool.

> [!IMPORTANT]
> **Observed 2026-08-23: protoArtoo's full peripheral set fits this board with zero
> spare header GPIOs.** Counting the pins this board actually routes, and honouring
> the "Pairs to avoid unless proven" table below in full, the supply is 15 usable
> GPIOs against a demand of 14 - six RC/SBUS inputs, a two-pin audio UART, and six
> servo/ESC outputs. One spare, and it is an avoid-list pin.
>
> **The constraint is this board, not the ESP32-P4.** The chip carries five HP UARTs
> and ample GPIO; the FireBeetle 2 routes a subset, further reduced by the ESP32-C6
> SDIO link (GPIO14-19), the TF card slot (GPIO39-45), USB and MIPI, and pins not
> brought out at all (GPIO0-2, 10-11, 13, 26-27, 46-47, 53). This is the chip-layer
> versus board-variant distinction ADR 0028 draws: a different ESP32-P4 board has the
> same peripherals and more headers.
>
> Recorded because "works, with no room to grow" is a materially different
> recommendation from "works", and a builder choosing this board deserves to know
> which one they are getting. Figures are as of this date and change with the
> peripheral set; the current allocation and its revision triggers live on issue #190.

### Two board-level constraints that override the taxonomy

**1. Prefer GPIO36 and lower.** Espressif's own variant header carries this
comment:

> Use GPIOs 36 or lower on the P4 DevKit to avoid LDO power issues with high
> numbered GPIOs.

GPIO48 sits on `VDD_IO_5` and GPIO49-GPIO54 on `VDD_IO_6`, both fed by internal
regulators. This directly contradicts the previous revision of this sheet, which
put `UART4` on GPIO48/GPIO49 specifically to stay away from strapping pins. Treat
GPIO48-GPIO52 as usable but requiring measurement, not as a safe default.

**2. ADC1 is nearly exhausted before you start.** ADC1 channels 0-7 are
GPIO16-GPIO23. GPIO16-GPIO19 are consumed by the ESP32-C6 SDIO link, so
**GPIO20-GPIO23 are the only ADC1 channels this board can reach.** Assigning
UART1 and UART2 there, as the previous revision recommended, costs every ADC1
channel on the board. ADC2 (GPIO49-GPIO54) remains, but lands in the
LDO-cautioned range.

### Exposed GPIO table

| GPIO | Board label | Priority | Silicon alternates | Notes |
| --- | --- | --- | --- | --- |
| GPIO4 | `4` | P3 | JTAG `MTMS`, `TOUCH_CHANNEL3`, LP GPIO | Arduino `T0`. Input-enabled at reset. Downgraded from "Good" |
| GPIO5 | `5` | P3 | JTAG `MTDO`, `TOUCH_CHANNEL4`, LP GPIO | Arduino `T1`. `MTDO` is an output-capable pad. Downgraded from "Good" |
| GPIO7 | `7/SDA` | P2 | `SPI2_CS_PAD`, `TOUCH_CHANNEL6`, LP GPIO | Board default SDA; Arduino `T2` |
| GPIO8 | `8/SCL` | P2 | `SPI2_D_PAD`, `TOUCH_CHANNEL7`, LP GPIO | Board default SCL; Arduino `T3` |
| GPIO20 | `20/A0` | P2 | `ADC1_CHANNEL4` | Clean digital, but spends an ADC1 channel |
| GPIO21 | `21/A1` | P2 | `ADC1_CHANNEL5` | Clean digital, but spends an ADC1 channel |
| GPIO22 | `22/A2` | P2 | `ADC1_CHANNEL6` | Clean digital, but spends an ADC1 channel |
| GPIO23 | `23/A3` | P2 | `ADC1_CHANNEL7`, `REF_50M_CLK_PAD` | Clean digital, but spends an ADC1 channel |
| GPIO28 | `28/SCK` | P2 | `SPI2_CS_PAD`, `GMAC_PHY_RXDV_PAD` | Arduino `SCK`. See silkscreen note below |
| GPIO29 | `29/MO` | P2 | `SPI2_D_PAD`, `GMAC_PHY_RXD0_PAD` | Arduino `MOSI` |
| GPIO30 | `30/MI` | P2 | `SPI2_CK_PAD`, `GMAC_PHY_RXD1_PAD` | Arduino `MISO` |
| GPIO31 | `31` | P2 | `SPI2_Q_PAD`, `GMAC_PHY_RXER_PAD` | Arduino `SS`. Best clean pin in the <=36 range |
| GPIO32 | `32/I3C/SCL` | P1 for I3C | `SPI2_HOLD_PAD`, `GMAC_RMII_CLK_PAD` | Reassignable, but costs the only I3C clock pin |
| GPIO33 | `33/I3C/SDA` | P1 for I3C | `SPI2_WP_PAD`, `GMAC_PHY_TXEN_PAD` | Reassignable, but costs the only I3C data pin |
| GPIO34 | `34` | P3 | Strapping (JTAG source), `SPI2_IO4_PAD` | See strapping notes |
| GPIO35 | `35` | P3 | Strapping (boot mode), BOOT button, `SPI2_IO5_PAD` | Avoid |
| GPIO36 | `36` | P3 | Strapping (ROM print), `SPI2_IO6_PAD` | See strapping notes |
| GPIO37 | `37/T` | P3 | UART0 TX (IO MUX), `SPI2` eight-line | Keep as console/download UART |
| GPIO38 | `38/R` | P3 | UART0 RX (IO MUX), `SPI2_DQS_PAD` | Keep as console/download UART |
| GPIO48 | `48` | P2 | `SD1_CDATA7_PAD`, `GMAC_PHY_RXER_PAD` | `VDD_IO_5`. LDO caution |
| GPIO49 | `49` | P2 | `ADC2_CHANNEL0`, `GMAC_PHY_TXEN_PAD` | Arduino `A5`. `VDD_IO_6`. LDO caution |
| GPIO50 | `50` | P2 | `ADC2_CHANNEL1`, `GMAC_RMII_CLK_PAD` | Arduino `A6`. `VDD_IO_6`. LDO caution |
| GPIO51 | `51/A4` | P2 | `ADC2_CHANNEL2`, `ANA_COMP0`, `GMAC_PHY_RXDV_PAD` | Arduino `A4`. `VDD_IO_6`. LDO caution |
| GPIO52 | `52` | P2 | `ADC2_CHANNEL3`, `ANA_COMP0`, `GMAC_PHY_RXD0_PAD` | Arduino `A7`. `VDD_IO_6`. LDO caution |

Silkscreen note: DFRobot labels GPIO28 `SCK`, but the P4 IO MUX assigns
`SPI2_CS_PAD` to GPIO28 and `SPI2_CK_PAD` to GPIO30. Espressif's Arduino variant
follows the silkscreen (`SCK = 28`, `MISO = 30`), which means Arduino SPI on this
board runs through the GPIO matrix rather than the IO MUX fast path. Functionally
fine; relevant if maximum SPI clock is ever needed.

### Not available on the IO headers

| GPIOs | Why |
| --- | --- |
| GPIO3 | Onboard LED |
| GPIO6, GPIO14-GPIO19, GPIO54 | ESP32-C6 ESP-Hosted SDIO link |
| GPIO9, GPIO12 | Onboard microphone |
| GPIO39-GPIO45 | Onboard TF card SDIO plus power enable |
| GPIO24, GPIO25 | USB Serial/JTAG (`USB1P1_N0`/`P0`) |
| GPIO0-GPIO2, GPIO10, GPIO11, GPIO13, GPIO26, GPIO27, GPIO46, GPIO47, GPIO53 | Not brought out on DFR1237 |

## Strapping and Boot Behaviour

Five strapping pins, sampled into latches at reset and free as normal IO
afterwards. Default levels are set by internal weak pulls when the pin is
floating or sees a high-impedance load.

| Pin | Default at reset | Role | Practical risk with defaults |
| --- | --- | --- | --- |
| GPIO34 | Floating | JTAG signal source | **None by default.** Only read when `EFUSE_JTAG_SEL_ENABLE` is burnt; the eFuse default is 0, so GPIO34 is ignored |
| GPIO35 | Weak pull-up (bit = 1) | Boot mode | **Real.** Held low at reset forces joint download boot. BOOT button pin |
| GPIO36 | Floating | ROM message printing | **None by default.** Only read when `EFUSE_UART_PRINT_CONTROL` is non-zero; the eFuse default is 0, so GPIO36 is ignored |
| GPIO37 | Floating | Boot control, UART0 TX | Boot mode table lists "any value"; the pin does not select SPI vs download boot |
| GPIO38 | Floating | Boot control, UART0 RX | Boot mode table lists "any value" |

Boot mode selection reduces to: GPIO35 high (default) is SPI boot; GPIO35 low
with GPIO36 high is joint download boot.

This is a meaningful relaxation of the previous revision's blanket "Caution" on
GPIO34 and GPIO36. On a board with unburnt eFuses, only GPIO35 can actually
disturb boot. The datasheet does warn that GPIO34 "does not have any internal
pull resistors and the strapping value must be controlled by the external circuit
that cannot be in a high impedance state" - which matters only once
`EFUSE_JTAG_SEL_ENABLE` is burnt.

Strapping timing to respect if you drive these pins:

| Parameter | Min |
| --- | --- |
| `tSU` - power rails stable before `CHIP_PU` goes high | 0 ms |
| `tH` - hold after `CHIP_PU` high, before pins become normal IO | 3 ms |

## UART Lane Plan

ESP32-P4 has six UART controllers: five HP (`UART0`-`UART4`) and one `LP_UART`.
`UART0`-`UART4` TX/RX/RTS/CTS route to any GPIO through the GPIO matrix.
`UART0` defaults to GPIO37/GPIO38 via IO MUX; `UART0` RTS/CTS IO MUX pads are
GPIO8/GPIO9.

The DFR1237 UART-labeled header (`J9`) only exposes the `UART0` pair plus power.
Extra lanes are created in firmware.

Feature ceiling: 5 MBaud, 5-8 data bits, 1/1.5/2 stop bits, parity, RS485, IrDA,
GDMA, hardware and software flow control, receive timeout, wake-on-UART. HP UARTs
share 260 x 8-bit of FIFO RAM across all five controllers, so deep per-port FIFOs
come at the expense of the others.

### Recommended allocation

There is no allocation on this board that costs nothing. Pick by what the design
actually needs.

| Lane | TX | RX | What it costs | When to pick it |
| --- | --- | --- | --- | --- |
| `UART0` | GPIO37 | GPIO38 | Nothing extra | Always keep. ROM logs, fallback console, UART download |
| `UART1` | GPIO20 | GPIO21 | `ADC1_CHANNEL4/5` | Default first lane if no analog input is needed |
| `UART2` | GPIO22 | GPIO23 | `ADC1_CHANNEL6/7` | Default second lane if no analog input is needed |
| `UART3` | GPIO32 | GPIO33 | The I3C master interface | Third lane; safe if I3C is unused |
| `UART4` | GPIO31 | GPIO28/29/30 | SPI2 quad group | Fourth lane; only if the SPI header is not used as SPI |

If analog input **is** needed, invert the order: keep GPIO20-GPIO23 as A0-A3,
take `UART1` from GPIO32/GPIO33 and `UART2` from the SPI-labeled group, and
accept GPIO48-GPIO52 for `UART3`/`UART4` only after measuring them under load.

Pairs to avoid unless proven:

| Pair | Why |
| --- | --- |
| GPIO4/GPIO5 | JTAG `MTMS`/`MTDO`, P3. The previous revision listed this as the preferred fallback; it is not |
| Anything on GPIO48-GPIO52 | LDO caution from Espressif's own variant header |
| GPIO34, GPIO36 | Strapping. Safe with unburnt eFuses, but the margin disappears the moment JTAG or ROM-print eFuses are programmed |
| GPIO35 | BOOT button and boot-mode strapping |

### Low-power UART

`LP_UART` routes only to LP GPIOs, and its IO MUX pads `LP_UART_TXD_PAD` /
`LP_UART_RXD_PAD` are GPIO14/GPIO15 - both consumed by the ESP32-C6 SDIO link.
Other LP GPIOs (GPIO0-GPIO15) can carry it through the LP GPIO matrix, but on
this board the only LP GPIOs reaching a header are GPIO4, GPIO5, GPIO7, and
GPIO8. Treat `LP_UART` as impractical here unless Wi-Fi is abandoned.

### Arduino example

```cpp
#include <Arduino.h>

HardwareSerial ServoBus(1);
HardwareSerial AuxBus(2);

constexpr int UART1_TX = 20;
constexpr int UART1_RX = 21;
constexpr int UART2_TX = 22;
constexpr int UART2_RX = 23;

void setup() {
  Serial.begin(115200); // USB CDC; cdc_on_boot defaults to 1 on this board.

  ServoBus.begin(115200, SERIAL_8N1, UART1_RX, UART1_TX);
  AuxBus.begin(115200, SERIAL_8N1, UART2_RX, UART2_TX);
}

void loop() {
  if (ServoBus.available()) {
    AuxBus.write(ServoBus.read());
  }
}
```

Always pass pins explicitly so the code documents the header assignment. Do not
rely on default `Serial1`/`Serial2` pins.

### ESP-IDF example

```cpp
#include "driver/uart.h"

constexpr uart_port_t SERVO_UART = UART_NUM_1;
constexpr int SERVO_TX = 20;
constexpr int SERVO_RX = 21;

void init_servo_uart() {
  uart_config_t cfg = {};
  cfg.baud_rate = 115200;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;

  uart_driver_install(SERVO_UART, 2048, 0, 0, nullptr, 0);
  uart_param_config(SERVO_UART, &cfg);
  uart_set_pin(SERVO_UART, SERVO_TX, SERVO_RX,
               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}
```

Flow control is routable if the peer needs it:

```cpp
uart_set_pin(UART_NUM_2, 22, 23, 31, 32); // TX, RX, RTS, CTS
```

Only assign RTS/CTS if the external device uses flow control.

## Arduino-ESP32 Notes

**A dedicated board entry now exists.** Since arduino-esp32 core 3.3.11
(2026-07-22) there is `DFRobot FireBeetle 2 ESP32-P4` with its own variant. Use
it instead of `ESP32P4 Dev Module`; DFRobot's getting-started page still says
otherwise and is out of date.

Key values baked into that board entry:

| Setting | Value |
| --- | --- |
| `build.variant` | `dfrobot_firebeetle2_esp32p4` |
| `build.chip_variant` | `esp32p4_es` (menu default: "Before v3.00") |
| `build.f_cpu` | `360000000L` |
| `build.flash_size` | `16MB` |
| `build.flash_mode` | `dio` (bootloader `build.boot=qio`) — **but see the note below; the PlatformIO board JSON in this sheet uses `qio`, and `qio` is what has actually been flashed and booted** |
| `build.flash_freq` | `80m` |
| `build.bootloader_addr` | `0x2000` |
| `build.usb_mode` | `1` (Hardware CDC and JTAG) |
| `build.cdc_on_boot` | `1` |
| `build.defines` | `-DBOARD_HAS_PSRAM` |
| `build.partitions` | `default` (a 4 MB table on a 16 MB part) |

Tool settings that matter:

- **Chip Variant** menu: leave at "Before v3.00" for this board. "v3.00 or newer"
  switches to `chip_variant=esp32p4` at 400 MHz and will not boot on v1.x
  silicon.
- The default partition scheme is the 4 MB `default` table. Pick a 16 MB scheme
  (`16M Flash (3MB APP/9.9MB FATFS)` or similar) to use the flash on the board.
- `USB CDC On Boot` defaults to enabled here, so `Serial` is USB CDC. `Serial0`
  is the UART0 path on GPIO37/GPIO38. **This default belongs to the Arduino IDE,
  not to PlatformIO** — under PlatformIO you must pass
  `-DARDUINO_USB_CDC_ON_BOOT=1` and `-DARDUINO_USB_MODE=1` in `build_flags`,
  or `Serial` silently becomes UART0 and nothing reaches `/dev/ttyACM*`. Board
  JSON keys do not work for this; see the note on the board JSON below.

> [!NOTE]
> **Flash mode, unresolved contradiction.** The table above records `dio` for
> `build.flash_mode` (bootloader `qio`), while this sheet's ready-made
> PlatformIO board JSON specifies `qio`. Measured on 2026-08-22: a `qio` image
> flashed with `Hash of data verified` and boots and runs on the v1.3 board, so
> `qio` is empirically viable for the app image. The two figures have not been
> reconciled against a primary source — do not cite "the spec sheet default"
> as if it were unambiguous.

Convenience aliases the variant defines, beyond what the silkscreen shows:

| Alias | GPIO | | Alias | GPIO |
| --- | --- | --- | --- | --- |
| `LED_BUILTIN` | 3 | | `A0` | 20 |
| `TX` / `RX` | 37 / 38 | | `A1` | 21 |
| `SDA` / `SCL` | 7 / 8 | | `A2` | 22 |
| `SCK` | 28 | | `A3` | 23 |
| `MOSI` | 29 | | `A4` | 51 |
| `MISO` | 30 | | `A5` | 49 |
| `SS` | 31 | | `A6` | 50 |
| `T0`-`T3` | 4, 5, 7, 8 | | `A7` | 52 |

Note `A4` maps to GPIO51 in both the silkscreen and the variant, but the variant
adds `A5`-`A7` on GPIO49/GPIO50/GPIO52, which the silkscreen does not label.

## PlatformIO Notes

There is still **no DFRobot FireBeetle 2 ESP32-P4 board JSON** in
pioarduino/platform-espressif32 as of release `55.03.311` (2026-07-24;
re-checked 2026-08-21, still the latest release and still no DFRobot board
JSON). Only
generic and Espressif EV-board targets exist:

| Board ID | `chip_variant` | `f_cpu` | Applies to this board? |
| --- | --- | --- | --- |
| `esp32-p4` | `esp32p4_es` | 360 MHz | Closest generic match |
| `esp32-p4_r3` | `esp32p4` | 400 MHz | **No.** v3.x silicon only |
| `esp32-p4-evboard` | `esp32p4_es` | 360 MHz | Espressif EV board |
| `esp32-p4_r3-evboard` | `esp32p4` | 400 MHz | Espressif EV board, v3.x |

`55.03.311` bundles arduino-esp32 3.3.11 and ESP-IDF v5.5.5, so the
`dfrobot_firebeetle2_esp32p4` **variant** is present in the framework package
even though no board JSON references it. Supply a project-local board definition
to reach it.

Save as `boards/dfrobot_firebeetle2_esp32p4.json` in the project root:

> [!IMPORTANT]
> **This board JSON alone does not give you a USB serial console.** `Serial`
> binds to UART0 (GPIO37/GPIO38), so a board flashed over USB Serial/JTAG runs
> but prints **nothing** on `/dev/ttyACM*` — which reads as dead firmware when
> it is only a misrouted console. Add the build flags shown in the PlatformIO
> env below.
>
> Putting `cdc_on_boot` / `usb_mode` *in this JSON does not work* — the keys
> are accepted but this platform does not translate them into defines.
> Verified on 2026-08-22: with the keys in the board manifest the linked ELF
> carried **0 `HWCDC` symbols** and `Serial` resolved to `Serial0`; moving the
> same settings to `build_flags` took it to **21 `HWCDC` symbols** and the
> console came up. The Arduino-settings table above lists `build.cdc_on_boot`
> and `build.usb_mode` because those are **Arduino IDE** menu settings, and
> PlatformIO does not inherit them.

```json
{
  "build": {
    "core": "esp32",
    "extra_flags": [
      "-DBOARD_HAS_PSRAM",
      "-DARDUINO_DFROBOT_FIREBEETLE2_ESP32P4"
    ],
    "f_cpu": "360000000L",
    "f_flash": "80000000L",
    "f_psram": "200000000L",
    "flash_mode": "qio",
    "mcu": "esp32p4",
    "chip_variant": "esp32p4_es",
    "variant": "dfrobot_firebeetle2_esp32p4"
  },
  "connectivity": ["wifi", "bluetooth"],
  "debug": { "openocd_target": "esp32p4.cfg" },
  "frameworks": ["arduino", "espidf"],
  "name": "DFRobot FireBeetle 2 ESP32-P4",
  "upload": {
    "flash_size": "16MB",
    "maximum_ram_size": 327680,
    "maximum_size": 16777216,
    "require_upload_port": true,
    "speed": 460800
  },
  "url": "https://wiki.dfrobot.com/dfr1172/",
  "vendor": "DFRobot"
}
```

```ini
[env:firebeetle2_esp32p4]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
board = dfrobot_firebeetle2_esp32p4
framework = arduino
monitor_speed = 115200
board_build.partitions = default_16MB.csv
build_flags =
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_USB_MODE=1
```

Fall back to `board = esp32-p4` (not `esp32-p4_r3`) if a project-local board JSON
is not wanted. That target defaults to 4 MB flash, so override
`board_upload.flash_size`, `board_upload.maximum_size`, and the partition table.

ESP-IDF framework variant:

```ini
[env:firebeetle2_esp32p4_idf]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
board = dfrobot_firebeetle2_esp32p4
framework = espidf
monitor_speed = 115200
```

Two items to verify on first build rather than assume:

- **Flash mode.** Espressif's Arduino board entry uses `flash_mode=dio` with a
  `qio` bootloader. The JSON above carries `qio`, matching the generic
  `esp32-p4` targets. If the image fails to boot, try `"flash_mode": "dio"`.
- **`connectivity`.** Wi-Fi here is ESP-Hosted over SDIO, not native. The field
  is cosmetic in PlatformIO but the distinction is not.

## ESP-IDF Notes

- Current stable is **v6.0.2**; the v5.5 LTS line is at **v5.5.5**. pioarduino
  `55.03.311` bundles v5.5.5.
- For **v3.x** silicon, ESP-IDF v5.5.3+ or v6.0+ is mandatory.
- For the **v1.x** silicon on this board, set
  `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` in `sdkconfig`. The ESP-IDF default is
  `n`, which targets v3.x, so this must be set explicitly. An image built with
  the wrong setting will fail to download or fail to boot.
- With that set, `CONFIG_ESP32P4_REV_MIN` offers v0.0, v0.1, and v1.0; the
  default falls to the lowest, and the maximum supported becomes v1.99. Any of
  those accepts the v1.0 chip on this board, so no further action is needed.
- If RMT is used in continuous TX mode, `RMT-176` applies. ESP-IDF has bypassed
  it since v5.2, but confirm rather than assume if the RMT idle level matters.
- Wi-Fi requires the `esp_hosted` component and matching ESP32-C6 slave firmware.
  It is not `esp_wifi` against local silicon.

## Peripheral Remapping Cheat Sheet

| Peripheral | Remappable to header GPIOs? | Notes |
| --- | --- | --- |
| `UART0`-`UART4` | Yes, any GPIO via GPIO matrix | `UART0` IO MUX pads GPIO37/38, RTS/CTS GPIO8/9 |
| `LP_UART` | LP GPIOs only | IO MUX pads GPIO14/15 are taken by the C6 link; impractical here |
| I2C | Yes | GPIO7/GPIO8 are board defaults, not fixed |
| I3C | GPIO32/GPIO33 only | One I3C master; those are its pins |
| SPI2/SPI3 | Yes, but IO MUX defaults live on GPIO28-GPIO38 | Board silkscreen does not match IO MUX roles |
| RMT | Yes | Useful for single-wire protocols |
| LEDC PWM | Yes | Good for simple PWM output |
| MCPWM | Yes | Motor control |
| TWAI/CAN | Yes, with external transceiver | Needs an RX/TX GPIO pair |
| ADC1 | Fixed: GPIO16-GPIO23 | Only GPIO20-GPIO23 reach a header |
| ADC2 | Fixed: GPIO49-GPIO54 | GPIO49-GPIO52 reach a header; LDO caution |
| Analog comparator | Fixed: `ANA_COMP0` GPIO51/52, `ANA_COMP1` GPIO53/54 | Only `ANA_COMP0` is reachable |
| Touch | Fixed: `TOUCH_CHANNEL1`-`14` on GPIO2-GPIO15 | Only GPIO4, 5, 7, 8 reach a header |
| USB, MIPI CSI/DSI, flash | No general remap | Dedicated pins |
| TF card SDIO | Board-wired GPIO39-GPIO45 | Fixed if using the onboard slot |
| ESP32-C6 SDIO | Board-wired GPIO14-GPIO19 plus GPIO54/GPIO6 | Reserved |

## Open Items

Things this sheet states from documentation but has not confirmed on hardware:

1. ~~**Chip revision.**~~ Resolved: the board reports **v1.0**. Confirm again for
   any additional unit, since DFRobot ships across the v1.x family.
2. **GPIO48-GPIO52 under load.** Espressif's "LDO power issues with high numbered
   GPIOs" warning is unquantified. Measure before committing a UART or any
   timing-critical signal there.
3. **PlatformIO flash mode.** `qio` vs `dio` per the note above.
4. **GPIO6.** The wiki lists it as a C6 wakeup line; the Arduino variant does
   not. Read the main-board schematic or probe it.
5. **Main-board schematic.** DFRobot ships it only as a raster PDF. If precise
   net-level truth is needed for GPIO6, the mic, or the TF power switch, ask
   DFRobot for source CAD.
6. **Mounting-hole coordinates.** M3 diameter is confirmed; positions are not.
   Use the physical board or DFRobot CAD for enclosure work.
