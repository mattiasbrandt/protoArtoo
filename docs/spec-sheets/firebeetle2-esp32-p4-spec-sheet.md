# FireBeetle 2 ESP32-P4 Spec Sheet

Working spec for using the DFRobot FireBeetle 2 ESP32-P4 and DFR1237 IO
expansion board as a controller candidate.

## Boards Covered

| Item | SKU | Notes |
| --- | --- | --- |
| FireBeetle 2 ESP32-P4 AI Vision Board | DFR1172 | ESP32-P4R32 main board with ESP32-C6 wireless co-processor |
| FireBeetle 2 ESP32-P4 Edge AI & Video Processing Kit | DFR1237 | DFR1172 plus passive IO expansion board |

## Official Sources Checked

| Source | URL | Extraction notes |
| --- | --- | --- |
| DFR1172 wiki | https://wiki.dfrobot.com/dfr1172/ | Product specs, onboard pin definitions, Arduino setup |
| DFR1237 wiki | https://wiki.dfrobot.com/dfr1237/ | IO expansion board resources and product specs |
| DFR1172 schematic PDF | https://dfimg.dfrobot.com/wiki/21103/DFR1172_firebeetle-esp32-p4r32-development-board_schematics_V1.0.pdf | Raster PDF; text extraction empty, OCR/image inspection required |
| DFR1172 ESP32-P4 datasheet PDF | https://dfimg.dfrobot.com/wiki/21103/DFR1172_firebeetle-esp32-p4r32-development-board_datasheet_V1.0.pdf | Espressif ESP32-P4 Series Datasheet pre-release v0.5, 89 pages; normal `pdftotext` extraction worked |
| DFR1237 dimension drawing PDF | https://dfimg.dfrobot.com/wiki/19348/DFR1237_firebeetle-2-esp32-p4-kit_dimension_V1.0.pdf | Raster PDF; OCR/image inspection required |
| Arduino-ESP32 docs | https://espressif.github.io/arduino-esp32/ | ESP32-P4 listed as stable and development supported |
| ESP-IDF serial connection docs | https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/get-started/establish-serial-connection.html | ESP32-P4 USB flashing/serial behavior |

## Mechanical Notes

### Main Board: DFR1172

| Parameter | Value |
| --- | --- |
| PCB outline | 25.4 mm x 60 mm |
| Form factor | FireBeetle 2 footprint |
| Supplied headers | Two 20-pin 2.54 mm male headers, per DFRobot product listing |
| Mounting-hole dimensions | Not prioritized here; use physical measurement or source CAD for enclosure work |

The DFR1172 board is the narrow module that plugs into the DFR1237 expansion
board.

### IO Expansion Board: DFR1237

The DFR1237 dimension PDF is a raster drawing. `pdftotext` extracted no text, so
the following dimensions come from 600 DPI rendering plus OCR and direct image
inspection.

| Parameter | Value |
| --- | --- |
| PCB outline | 45.00 mm x 60.00 mm |
| PCB thickness | 1.6 mm |
| Layers | 2 |
| Solder mask | Black, top and bottom |
| Silkscreen | White, top and bottom |
| Drawing date | 2025-06-05 |
| Drawing identifier | `DFR1237-V1.0.0-1.6mm-2 Layer-60*45mm` |
| Mounting detail | Four mounting holes are shown; exact diameter is not called out in the drawing |

Hole dimensions are not the priority for this sheet. Use the physical board or
DFRobot CAD/Gerber data for final enclosure work.

### Expansion Header Geometry

The IO expansion board is passive. It does not add level shifting, buffering, or
new peripheral hardware. It exposes the DFR1172 module pins on labeled 2.54 mm
headers and provides repeated 3V3/GND rails.

Important mechanical clearances:

- Keep the two USB-C connectors, MIPI CSI/DSI connectors, TF card slot, reset
  button, BOOT button, and microphone area accessible if the main board is
  socketed into an enclosure.
- The large black circles in the DFR1237 drawing are mounting holes, not GPIO
  pads. Do not place standoffs through header/pin fields.
- Treat the DFR1237 45 mm width as the envelope when the expansion board is
  installed; the DFR1172 module alone is only 25.4 mm wide.

## Electrical Summary

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
| ESP32-P4 GPIO input high | min 0.75 x VDD |
| ESP32-P4 GPIO input low | max 0.25 x VDD |
| ESP32-P4 GPIO source current | typ 40 mA at VDD = 3.3 V, `PAD_DRIVER = 3` |
| ESP32-P4 GPIO sink current | typ 28 mA at VDD = 3.3 V, `PAD_DRIVER = 3` |
| Internal pull-up/down | typ 45 kOhm |

Do not put 5 V signals on GPIO headers. The 5 V rail is power only.

## ESP32-P4 / Board Resources

| Resource | Value |
| --- | --- |
| Main SoC | ESP32-P4R32 |
| HP CPU | RISC-V 32-bit dual-core, 360 MHz default |
| LP CPU | RISC-V 32-bit single-core, 40 MHz |
| PSRAM | 32 MB in package |
| Flash on DFR1172 | 16 MB external QSPI flash |
| HP L2 memory | 768 KB |
| LP SRAM | 32 KB |
| HP ROM | 128 KB |
| LP ROM | 16 KB |
| User eFuse | 1792 bits user accessible from 4096-bit OTP |

The Espressif datasheet notes 360 MHz as the default HP clock; 400 MHz exists in
some documentation and PlatformIO manifests for rev.300/rev.301 generic targets,
but DFRobot's public DFR1172 wiki states 360 MHz. Use 360 MHz as the conservative
board spec unless the actual board revision and toolchain configuration are
verified.

## Onboard Fixed Pin Use

These pins are already tied to board functions and should not be casually reused
on IO headers.

| Function | ESP32-P4 pins | Notes |
| --- | --- | --- |
| User LED | GPIO3 | Arduino blink examples use `3` |
| BOOT button | GPIO35 | Also a strapping pin; sampled during reset |
| Default I2C labels | GPIO7 SDA, GPIO8 SCL | Labels/defaults only; I2C controllers can be routed elsewhere |
| UART0 labels | GPIO37 TX, GPIO38 RX | Default ROM/debug UART path |
| PDM microphone | GPIO12 CLK, GPIO9 DATA | Avoid if using onboard mic |
| ESP32-C6 Wi-Fi/BT SDIO | GPIO14 D0, GPIO15 D1, GPIO16 D2, GPIO17 D3, GPIO18 CLK, GPIO19 CMD, GPIO54 EN, GPIO6 WAKEUP | Reserved for wireless co-processor |
| TF card SDIO | GPIO39 D0, GPIO40 D1, GPIO41 D2, GPIO42 D3, GPIO43 CLK, GPIO44 CMD, GPIO45 EN | Reserved if using onboard microSD |

## IO Expansion Header Labels

The DFR1237 drawing exposes the following GPIO labels in the main repeated
header area:

`GPIO4`, `GPIO5`, `GPIO20`, `GPIO21`, `GPIO22`, `GPIO23`, `GPIO31`, `GPIO32`,
`GPIO33`, `GPIO34`, `GPIO35`, `GPIO36`, `GPIO48`, `GPIO49`, `GPIO50`, `GPIO51`,
`GPIO52`, plus visible dedicated labels for `GPIO7`, `GPIO8`, `GPIO28`, `GPIO29`,
`GPIO30`, `GPIO37`, and `GPIO38`.

The labels `SPI`, `UART`, and `I2C` on the expansion board are convenience
groupings. They do not lock the silicon peripheral to those pins. ESP32-P4 uses
GPIO matrix routing for most digital peripherals, so UART, I2C, SPI, TWAI, RMT,
LEDC, MCPWM, and similar functions can usually be assigned in software to other
GPIO header pins.

## UART-Capable GPIO Matrix

At the silicon level, `UART0` through `UART4` can route TX/RX/RTS/CTS through
the ESP32-P4 GPIO matrix to ordinary GPIOs. The practical question is not
"can this pin become UART?" but "is this exposed pin safe to use as UART on this
board?"

### DFR1237 Exposed GPIO Suitability

| GPIO | Expansion label/context | UART suitability | Notes |
| --- | --- | --- | --- |
| GPIO4 | Main GPIO header | Good | Exposed, not listed as an onboard fixed function. Also has LP/touch/JTAG-related alternate functions, but usable as normal GPIO after configuration. |
| GPIO5 | Main GPIO header | Good | Exposed, not listed as an onboard fixed function. Good spare UART candidate. |
| GPIO7 | `7/D`, I2C data label | Good if I2C not needed | DFRobot default SDA. Can be reassigned if the I2C header/default bus is not used. |
| GPIO8 | `8/C`, I2C clock label | Good if I2C not needed | DFRobot default SCL. Can be reassigned if the I2C header/default bus is not used. |
| GPIO20 | Main GPIO header | Best | Clean exposed GPIO. Recommended UART TX/RX pool. |
| GPIO21 | Main GPIO header | Best | Clean exposed GPIO. Recommended UART TX/RX pool. |
| GPIO22 | Main GPIO header | Best | Clean exposed GPIO. Recommended UART TX/RX pool. |
| GPIO23 | Main GPIO header | Best | Clean exposed GPIO. Recommended UART TX/RX pool. |
| GPIO28 | `28/SCK`, SPI label | Good if SPI label not needed | Exposed on SPI header. Can be UART if not using this header as SPI clock. |
| GPIO29 | `29/MO`, SPI label | Good if SPI label not needed | Exposed on SPI header. Can be UART if not using this header as SPI MOSI/MO. |
| GPIO30 | `30/MI`, SPI label | Good if SPI label not needed | Exposed on SPI header. Can be UART if not using this header as SPI MISO/MI. |
| GPIO31 | Main GPIO header | Best | Exposed. Also has SPI2 IO MUX role, but clean for GPIO-matrix UART if SPI2 is not assigned here. |
| GPIO32 | Main GPIO header | Best | Exposed. Good UART candidate. |
| GPIO33 | Main GPIO header | Good | Exposed. Good UART candidate unless planned for SPI2/Ethernet alternate use. |
| GPIO34 | Main GPIO header | Caution | JTAG signal source strapping pin. Usable after reset, but attached UART device must not force a bad reset level. |
| GPIO35 | Main GPIO header / BOOT | Avoid | BOOT button and boot-mode strapping. Do not use for routine UART lanes unless the reset behavior is proven safe. |
| GPIO36 | Main GPIO header | Caution | Boot/ROM-print related strapping pin. Usable after reset, but risky with externally driven UART devices during reset. |
| GPIO37 | `37/T`, UART label | Reserved for UART0 by default | Default UART0 TX and ROM/debug path. Leave as console/download UART unless intentionally moving debug to USB CDC. |
| GPIO38 | `38/R`, UART label | Reserved for UART0 by default | Default UART0 RX and ROM/debug path. Leave as console/download UART unless intentionally moving debug to USB CDC. |
| GPIO48 | Main GPIO header | Good | Exposed. Avoid only if using extended SD/MMC or Ethernet alternate mappings that need it. |
| GPIO49 | Main GPIO header | Good / analog-capable | Exposed. Also ADC2 channel 0 / RMII alternate capability. Good for digital UART if analog/Ethernet not needed. |
| GPIO50 | Main GPIO header | Good / analog-capable | Exposed. Also ADC2 channel 1 / RMII alternate capability. Good for digital UART if analog/Ethernet not needed. |
| GPIO51 | Main GPIO header | Good / analog-capable | Exposed. Also ADC2 channel 2 / analog comparator capability. Good for digital UART if analog/comparator not needed. |
| GPIO52 | Main GPIO header | Good / analog-capable | Exposed. Also ADC2 channel 3 / analog comparator capability. Good for digital UART if analog/comparator not needed. |

### Not Recommended for IO-Board UART Use

These are ESP32-P4 GPIOs, but they are either not the useful DFR1237 exposed
pool or are already committed to onboard devices.

| GPIOs | Why not use for extra UART lanes |
| --- | --- |
| GPIO3 | Onboard LED |
| GPIO6, GPIO14-GPIO19, GPIO54 | ESP32-C6 Wi-Fi/Bluetooth SDIO/control connection |
| GPIO9, GPIO12 | Onboard PDM microphone |
| GPIO39-GPIO45 | Onboard TF card SDIO |
| GPIO0-GPIO2, GPIO10-GPIO13, GPIO24-GPIO27, GPIO46-GPIO47, GPIO53 | Not identified as the normal useful DFR1237 exposed header pool in the inspected drawing/wiki material |

## Recommended UART Lane Plan

Use USB CDC for programming/logging when possible, leave GPIO37/GPIO38 as the
default UART0 escape hatch, and allocate additional hardware UARTs from clean
DFR1237 header GPIOs.

| Lane | TX | RX | Status | Suggested use |
| --- | --- | --- | --- | --- |
| `UART0` | GPIO37 | GPIO38 | Keep default | ROM logs, fallback console, UART download/debug |
| `UART1` | GPIO20 | GPIO21 | Best | Primary external serial device |
| `UART2` | GPIO22 | GPIO23 | Best | Secondary external serial device |
| `UART3` | GPIO31 | GPIO32 | Best | Third external serial device |
| `UART4` | GPIO48 | GPIO49 | Good | Fourth external serial device; keeps away from boot strapping pins |

Alternate pairs:

| Pair | When to use |
| --- | --- |
| GPIO4/GPIO5 | Good fallback if GPIO20-GPIO23 are needed for another bus |
| GPIO28/GPIO29 or GPIO28/GPIO30 | Good if the SPI-labeled header is not used as SPI |
| GPIO50/GPIO51 or GPIO51/GPIO52 | Good if ADC/comparator functions are not needed |
| GPIO33/GPIO34 | Usable, but GPIO34 reset/JTAG strapping makes it less clean |
| GPIO36 with another GPIO | Only after proving the attached device does not disturb boot/reset behavior |

## UART Lane Reassignment

### What "More UART Lanes" Means

ESP32-P4 has six UART controllers:

- Five high-performance UART controllers: `UART0` through `UART4`.
- One low-power UART controller: `LP_UART`.

The DFR1237 UART-labeled header only exposes the default `UART0` signals:

| Expansion label | GPIO | Default role |
| --- | --- | --- |
| `37/T` | GPIO37 | `UART0_TXD_PAD` |
| `38/R` | GPIO38 | `UART0_RXD_PAD` |
| `GND` | GND | Reference |
| `3V3` | 3.3 V | Power |

Additional UART lanes are created in firmware by assigning `UART1` through
`UART4` TX/RX pins to other free GPIO headers.

### UART Routing Rules

From the ESP32-P4 datasheet:

- `UART0` through `UART4` pins can be chosen from any GPIO through the GPIO
  matrix.
- `UART0` defaults to GPIO37/GPIO38 through IO MUX.
- `LP_UART` pins can be chosen from LP GPIOs through the LP GPIO matrix.
- `LP_UART` defaults to LP_GPIO14/LP_GPIO15, which correspond to GPIO14/GPIO15.

Board-level constraints matter more than the SoC's theoretical freedom:

- Avoid GPIO14-GPIO19, GPIO54, and GPIO6 if using Wi-Fi/Bluetooth, because those
  are tied to the ESP32-C6 co-processor.
- Avoid GPIO39-GPIO45 if using the onboard TF card.
- Avoid GPIO9/GPIO12 if using the onboard microphone.
- Avoid GPIO3 if using the onboard LED as a status indicator.
- Be cautious with GPIO34-GPIO38. GPIO34 controls JTAG signal source at reset,
  and GPIO35-GPIO38 control boot/download behavior. GPIO35 is also the BOOT
  button. These pins can be regular IO after reset, but attached devices must
  not force bad levels during reset.
- GPIO37/GPIO38 are useful for ROM logs and UART download. Reassigning `UART0`
  is possible after boot, but it removes the board's most predictable hardware
  serial console unless USB CDC is used intentionally.

### Arduino Examples

Use `HardwareSerial` with explicit pins. The IO expansion labels are just the
physical GPIO numbers.

```cpp
#include <Arduino.h>

HardwareSerial ServoBus(1);
HardwareSerial AuxBus(2);

constexpr int UART1_TX = 20;
constexpr int UART1_RX = 21;
constexpr int UART2_TX = 22;
constexpr int UART2_RX = 23;

void setup() {
  Serial.begin(115200); // USB CDC when USB CDC On Boot is enabled.

  ServoBus.begin(115200, SERIAL_8N1, UART1_RX, UART1_TX);
  AuxBus.begin(115200, SERIAL_8N1, UART2_RX, UART2_TX);
}

void loop() {
  if (ServoBus.available()) {
    int b = ServoBus.read();
    AuxBus.write(b);
  }
}
```

If the Arduino core exposes `Serial1`, `Serial2`, etc. for the selected ESP32-P4
variant, the same pin-explicit pattern applies:

```cpp
Serial1.begin(115200, SERIAL_8N1, 21, 20); // RX, TX
Serial2.begin(115200, SERIAL_8N1, 23, 22); // RX, TX
```

Do not rely on default `Serial1`/`Serial2` pins on this board. Pass pins
explicitly so the code documents the IO expansion header assignment.

### ESP-IDF Examples

ESP-IDF uses `uart_set_pin()` after `uart_driver_install()`.

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

Hardware flow control is also routable if needed:

```cpp
uart_set_pin(UART_NUM_2, 22, 23, 31, 32); // TX, RX, RTS, CTS
```

Only assign RTS/CTS if the external device actually uses flow control. Otherwise
leave them as `UART_PIN_NO_CHANGE`.

### Low-Power UART

The LP UART is not the first choice for normal external modules. Its default
pins are GPIO14/GPIO15, which are already used by the ESP32-C6 wireless SDIO
link on DFR1172. Treat LP UART as unavailable unless Wi-Fi/Bluetooth is not used
and the board-level ESP32-C6 connection is deliberately disabled or proven safe.

## PlatformIO Notes

Local PlatformIO board discovery found generic ESP32-P4 targets but no dedicated
DFRobot FireBeetle 2 ESP32-P4 board ID in the installed platform:

| Board ID | Notes |
| --- | --- |
| `esp32-p4` | Generic ESP32-P4 ES / pre-rev.300 target, 360 MHz, 4 MB default flash |
| `esp32-p4_r3` | Generic ESP32-P4 rev.300 target, 400 MHz, 16 MB default flash |
| `esp32-p4-evboard` | Espressif function EV board, not DFR1172 |
| `esp32-p4_r3-evboard` | Espressif function EV board rev.301, not DFR1172 |

For DFR1172, start from the generic rev.300 target only if the board's actual
chip revision matches. Override flash and runtime assumptions explicitly.

```ini
[env:firebeetle2_esp32p4]
platform = https://github.com/pioarduino/platform-espressif32.git
board = esp32-p4_r3
framework = arduino
monitor_speed = 115200

; DFR1172 has 16 MB flash and 32 MB PSRAM.
board_upload.flash_size = 16MB
board_build.flash_mode = qio
board_build.f_flash = 80000000L
board_build.psram_type = qspi
build_flags =
  -DBOARD_HAS_PSRAM
```

For conservative clocking, consider overriding CPU frequency to 360 MHz until the
actual board revision is verified:

```ini
board_build.f_cpu = 360000000L
```

If using ESP-IDF:

```ini
[env:firebeetle2_esp32p4_idf]
platform = https://github.com/pioarduino/platform-espressif32.git
board = esp32-p4_r3
framework = espidf
monitor_speed = 115200
board_upload.flash_size = 16MB
```

PlatformIO telemetry reported a local `.platformio/.cache` permission warning
during `pio boards`; that did not prevent board list output, but it should be
fixed outside this spec if PlatformIO behaves inconsistently.

## Arduino-ESP32 Notes

DFRobot's getting-started guide says to select `ESP32P4 Dev Module`, not a
DFRobot-specific board entry. Important tool settings:

- Enable `USB CDC On Boot` if using the programming USB-C port as `Serial`.
- If `USB CDC On Boot` is disabled, serial output goes through UART TX/RX
  instead; use `Serial0` deliberately for UART0 output.
- Select a partition scheme compatible with 16 MB flash.
- Board LED example uses GPIO3.

Arduino-ESP32 currently lists ESP32-P4 as supported in stable and development
channels. Keep ESP32-P4 projects on Arduino core 3.3.x or newer unless a tested
older core is deliberately pinned.

## Boot and Debug Pin Cautions

ESP32-P4 boot configuration uses strapping pins:

| Pin | Role |
| --- | --- |
| GPIO34 | JTAG signal source control |
| GPIO35 | Boot mode control; default weak pull-up; DFR1172 BOOT button |
| GPIO36 | Boot mode / ROM print control |
| GPIO37 | Boot mode control; default UART0 TX on DFR1172 |
| GPIO38 | Boot mode control; default UART0 RX on DFR1172 |

External circuits on these pins must not pull them to unintended levels during
reset. If using them as UART lanes, ensure connected devices are high-impedance
or benign through reset.

## Peripheral Remapping Cheat Sheet

| Peripheral | Can remap to header GPIOs? | Notes |
| --- | --- | --- |
| UART0-UART4 | Yes, any GPIO via GPIO matrix | Best candidate for extra serial buses |
| LP_UART | LP GPIOs only | Defaults overlap ESP32-C6 SDIO pins; avoid on DFR1172 |
| I2C | Yes | SDA/SCL labels are defaults, not fixed |
| SPI2/SPI3 user buses | Yes, but IO MUX defaults exist on some pins | Avoid flash and onboard SDIO pins |
| RMT | Yes | Useful for single-wire protocols |
| LEDC PWM | Yes | Good for simple PWM output |
| TWAI/CAN | Yes, with external transceiver | Needs RX/TX GPIO pair |
| USB, MIPI CSI/DSI, flash | No general remap | Dedicated pins |
| TF card SDIO | Board-wired to GPIO39-GPIO45 | Treat as fixed if using onboard slot |
| ESP32-C6 wireless SDIO | Board-wired to GPIO14-GPIO19 plus GPIO54/GPIO6 | Treat as reserved |

## Mechanical Follow-Up

Hole diameter and exact mounting coordinates are intentionally not expanded here.
For enclosure or daughterboard work, use the physical board or DFRobot source
CAD/Gerber rather than OCR from the published drawing.
