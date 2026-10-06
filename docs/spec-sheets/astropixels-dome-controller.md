# AstroPixels Spec Sheet (Darren Poulson, we-make-things.co.uk)

**AstroPixels** is an addressable-LED dome lighting set for a full-size R2-D2 by
Darren Poulson: WS2812B logic, PSI and holoprojector PCBs on an ESP32 carrier,
sold as one kit through we-make-things.co.uk. It fits the same surrounds as
Teeces, and with the right firmware and two PCA9685 expanders the same ESP32
also moves the dome panels and holoprojectors.

Research date 2026-09-12. Every fact below was read from one of five places: the
Reeltwo library at the version AstroPixelsPlus builds against, the source of the
AstroPixelsPlus fork at
[`github.com/mattiasbrandt/AstroPixelsPlus`](https://github.com/mattiasbrandt/AstroPixelsPlus),
the vendor's own firmware and pages, upstream AstroPixelsPlus, or the astromech
projects that drive a dome. Claims that could not be sourced are marked
`UNKNOWN` with the artefact or bench test that would settle them.

> [!IMPORTANT]
> **AstroPixels is the product. AstroPixelsPlus is firmware that runs on it.**
> They are not two boards and not two vendors. AstroPixels is a set of
> WS2812B-based logic, PSI and holo PCBs plus an ESP32 carrier, sold by
> we-make-things.co.uk. AstroPixelsPlus is one of the firmware images you can
> put on that carrier -- written by Mimir "rimim" Reynisson, built on the
> Reeltwo library. Upstream lives at `reeltwo/AstroPixelsPlus`; the
> AstroPixelsPlus fork at `mattiasbrandt/AstroPixelsPlus` is a public fork of it
> with a body-link mode, panel calibration and more commands.
>
> This distinction is load-bearing: the hardware facts (Sections 2 and 4) are
> true of any firmware; the protocol facts (Sections 6-9) are true only of
> AstroPixelsPlus, and several are true only of the fork.

> [!CAUTION]
> **An AstroPixels board out of the box does not speak Marcduino.** The firmware
> Darren Poulson ships on every set is the stock `standard` build, whose serial
> grammar is `LE...` and `HP...` -- *"With the latest standard firmware, serial2
> is listening at 9600 baud for standard astropixel commands. If you want to use
> marcduinos, there is a custom firmware"* (vendor docs, Interfacing). There are
> three firmware families for this hardware and only two of them parse
> Marcduino. Section 5 is the chooser, and getting it wrong looks like a dome
> that lights up beautifully and ignores every Marcduino command it is sent.

## 1. Scope

Covers the AstroPixels board set, its electrical and mounting behaviour, the
three firmware families available for it, the Serial2 header and the baud it
runs at, the Marcduino/JawaLite grammar the AstroPixelsPlus firmware accepts,
the fork's HTTP surface and panel map, and how the astromech hobby drives a dome.

Does not cover: the PCA9685 expander itself (that is
[`pca9685-servo-expander.md`](pca9685-servo-expander.md)), the dome
rotation motor or its ESC (that is
[`isdt-esc70-dome-esc.md`](isdt-esc70-dome-esc.md)), body audio (the Sound
family sheets), or the Teeces board set.

## 2. What you are actually buying

### 2.1 The kit, from the vendor's own list

Nine PCBs plus cabling, sold as one indivisible set:

| Item | Qty | Role |
| --- | --: | --- |
| Main Board | 1 | Breakout board carrying the ESP32 "brain"; everything plugs into it |
| RLD -- Rear Logic Display | 1 | Single wide display |
| FLD -- Front Logic Display | 2 | TFLD (top) and BFLD (bottom), stacked and daisy-chained |
| PSI -- Process State Indicator | 2 | Front PSI, rear PSI |
| HP -- HoloProjector light board | 3 | Front, top, rear |
| 30 cm servo extensions | 4 | |
| 30 cm servo cables | 2 | |
| 20 cm servo cables | 2 | |
| 10 cm servo cables | 4 | |

Verbatim from the vendor's Kit Contents page: *"1 x Main Board - This is a
breakout board that everything connects to and contains the ESP32 'brain'"*.

The store listing states what is **not** included: *"Power supply (5v, 1A
recommended)"* and *"Bezels and mounting systems"*.

### 2.2 The ESP32 is an off-the-shelf part, and its pin count matters

The Main Board is a carrier, not a microcontroller board. Verbatim, from the
vendor's Overview page:

> *"I use a standard, off the shelf ESP32 dev board (30 pin version). These can
> be bought from anywhere (ebay, amazon, aliexpress) if you do break yours. Then
> you can simply flash the correct firmware back on it."*

And from the Construction FAQ: *"The development boards I use are very easy to
find (**must be 30pin versions** for the Astropixels)"*.

This is a genuinely good property and worth stating plainly: **the expensive
part of this kit is the LED PCBs, and the part that breaks is a GBP 5 commodity
module you can replace yourself.** The vendor names the failure mode too --
*"The USB ports can be fragile and easily knocked off whilst working in the
dome."*

`platformio.ini` in both the vendor repo and the AstroPixelsPlus fork declares
`board = esp32dev` -- a plain dual-core ESP32-WROOM class part, not an S3, C3 or
P4.

> [!WARNING]
> **The Arduino ESP32 core must be 2.0.x.** The vendor's setup page: *"There is
> currently a bug in the ReelTwo library which makes it incompatible with the
> latest v3 ESP32 board libraries."* Select *"the latest 2.0.\* version"*. The
> AstroPixelsPlus fork obeys this by pinning `platform-espressif32` to `v5.2.0`,
> which carries a 2.0.x core. A v3 core is not a faster path; it is a build that
> does not run.

### 2.3 The LED geometry, read out of the renderer

The boards are plain WS2812B strings, and the counts are declared in the Reeltwo
library rather than by the vendor. From
`Reeltwo@23.5.3 src/dome/LogicEngine.h:392`, credited in its own comment:

```cpp
template <uint8_t DATA_PIN = FRONT_LOGIC_PIN>
class AstroPixelFLDPCB0 : public FastLEDPCB<WS2812B, DATA_PIN, 90, 0, 90, 9, 10>
{
        // 2022 AstroPixel boards by Darren Poulson
        // Neopixel FLD boards, First Release. 9x5 LEDs per board
```

and `src/dome/LogicEngine.h:519`:

```cpp
class AstroPixelRLDPCB0 : public FastLEDPCB<WS2812B, DATA_PIN, 108, 0, 108, 27, 4>
        // 2022 AstroPixel boards by Darren Poulson
```

| Board | LED type | Count | Matrix | Source |
| --- | --- | --: | --- | --- |
| FLD pair (TFLD + BFLD as one chain) | WS2812B | 90 | 9 x 10 | `LogicEngine.h:392` |
| FLD, each board alone | WS2812B | 45 | 9 x 5 | same, from the comment |
| RLD | WS2812B | 108 | 27 x 4 | `LogicEngine.h:519` |
| PSI, each | WS2812B | 25 | 5 x 5 | `NeoPSI.h:63` (`AstroPixelPSIPCB`) |
| PSI, 8x8 variant | WS2812B | 64 | 8 x 8 | `NeoPSI.h:82` (`AstroPixelPSI8PCB`) |
| HP, each | WS2812B ring | 7 | ring | `HoloLights.h:56,142` -- `NUM_LEDS = 7`, `numPixels = 7` default |

**A standard set is 269 WS2812B pixels**: 90 + 108 + 25 + 25 + (3 x 7). That
arithmetic is this sheet's, from the six sourced rows above; no source states
the total.

> [!NOTE]
> `AstroPixelPSI8PCB` (64 LEDs, 8 x 8) exists in the library and is **not** what
> a current kit ships -- the firmware the vendor and the AstroPixelsPlus fork both
> instantiate is `AstroPixelFrontPSI` / `AstroPixelRearPSI`, which alias the
> 25-LED `AstroPixelPSIPCB`. Treat the 8x8 as a variant that exists, not as one
> you can assume. Which kits shipped with it is `UNKNOWN`; settled by asking the
> vendor or counting the LEDs on the board in hand.

### 2.4 It replaces Teeces mechanically, on purpose

Vendor Installation page, verbatim: *"The Astropixel kit is designed to fit into
the same surrounds as the old teecees lights with the same mounting holes."* The
Main Board is designed to mount on the back of the RLD, and the supplied cable
lengths assume exactly that:

| Run | Length |
| --- | --- |
| Main Board -> RLD | 1 x 10 cm |
| Main Board -> FLD | 1 x 30 cm extension + 1 x 10 cm |
| FLD -> FLD | 1 x 10 cm |
| Main Board -> FPSI | 1 x 30 cm extension + 1 x 20 cm |
| Main Board -> RPSI | 1 x 30 cm |
| Main Board -> THP | 1 x 30 cm extension + 1 x 10 cm |
| Main Board -> RHP | 1 x 20 cm |
| Main Board -> FHP | 1 x 30 cm extension + 1 x 30 cm |

Bezels and spacers are a separate, community-supplied problem. The vendor links
a laser-cut bezel PDF, a set of printable spacers, Joel Joannisse's printable
bezel-with-diffuser (single-filament-swap PETG, 0.4 mm nozzle, 0.2 mm layers),
and Dana Jan's MK4 HP board mount on Printables.

### 2.5 Availability, price and terms

| Fact | Value | Source |
| --- | --- | --- |
| Price | **GBP 80.00** | we-make-things.co.uk product page |
| Stock, 2026-09-12 | **"Out of stock"** | same |
| Quantity limit | *"limited to one per customer"* | same |
| Resale | *"not for resale outside of the club"* | same |
| UK dispatch | *"usually within a week"* | same |
| International | *"please allow up to four weeks for delivery"* | same |
| Charity | GBP 10 per set to the Droidbuilders UK charity | vendor FAQ, Purchasing |

> [!IMPORTANT]
> **This is a club-economics product, not a commercial one, and that shapes what
> you can rely on.** The vendor states outright: *"I'm doing this for the love of
> the club ... I don't aim to make any money from this."* Consequences a spec
> sheet has to record rather than admire: it goes out of stock (it was, on the
> research date); you cannot buy individual boards (*"I just sell the kits as
> is"*); there is no scale variant; and there is no second supplier, because the
> design files are not published -- *"Are the files available for me to make my
> own? At the moment, no."* A build that depends on AstroPixels depends on one
> person's spare time.

### 2.6 It does not fit a Home Depot R2

Asked and answered twice on the vendor's FAQ: *"the Home Depot R2 is not full
size. These lights are designed to fit into a full sized replica."* And, on
returns: *"If you've bought them after all these warnings, then it is on you."*

### 2.7 What the kit does **not** give you

Panel and holoprojector **motion** is not part of AstroPixels. The kit lights
the dome; it does not move it. Moving servos needs two PCA9685 expanders the
builder buys separately, and firmware that knows what to do with them. The
AstroPixelsPlus fork addresses them like this:

| Board | I2C address | Moves | Channels used |
| --- | --- | --- | --: |
| Panel controller | `0x40` | ring panels P1-P4, P7, P11, P13 and pie panels PP1, PP2, PP4, PP6 | 11 of 16 |
| Holo controller | `0x41` (A0 bridged) | FHP, RHP, THP -- two axes each | 6 of 16 |

Source: the fork's `docs/HARDWARE_WIRING.md` section 1, and the
`servoSettings[]` table at `AstroPixelsPlus.ino:419-453`. Deeper detail on the
expander itself is [`pca9685-servo-expander.md`](pca9685-servo-expander.md).

## 3. Sources Checked

| Source | URL or path | What it settled |
| --- | --- | --- |
| **AstroPixels documentation site** | https://r2djp.gitbook.io/astropixels | **The authoritative product document.** Kit contents, power, pin map, ESP32 choice, mounting, the native `LE`/`HP` grammar, firmware families, FAQ |
| Full-text export of the same | `https://r2djp.gitbook.io/astropixels/llms-full.txt` | Read in full (33 KB, 20 pages) rather than page by page |
| Vendor store listing | https://we-make-things.co.uk/product/astropixels/ | Price, stock state, kit contents, per-customer limit, shipping |
| **Vendor firmware** | https://github.com/dpoulson/Astropixels | **The stock image.** Five build environments; `src/standard/main.cpp` is what ships on every board; `src/standard-md/` is the JawaLite build. **No LICENSE file** |
| Vendor's Reeltwo fork | https://github.com/dpoulson/Reeltwo | The library the vendor's own instructions tell you to install, *"a fork of the original repository due to a long standing bug that meant it would not compile"* |
| **Reeltwo library @ 23.5.3** | https://github.com/reeltwo/Reeltwo, at the tag the AstroPixelsPlus fork pins | LED geometry, the `LogicEngineRenderer` effect and color enums, the sequence packing formula, `HoloLights` defaults. LGPL-2.1 |
| **The AstroPixelsPlus fork** | https://github.com/mattiasbrandt/AstroPixelsPlus | 314 `MARCDUINO_ACTION`s, the Serial2 body-link handler, the panel map, the HTTP surface. LGPL-2.1 |
| Upstream AstroPixelsPlus | https://github.com/reeltwo/AstroPixelsPlus | The base the fork diverges from; its README carries the `#AP*` config commands and the *"prefix @ is optional and is ignored"* rule |
| AstroPixels web installer | https://dpoulson.github.io/Astropixels/ | How a builder actually changes firmware family; Chrome or Edge only |
| `docs/HARDWARE_WIRING.md` in the fork | `mattiasbrandt/AstroPixelsPlus/docs/` | The `G V R T` header, the slip-ring practice, the PCA9685 addressing. **Stale on baud -- see 6.2** |

## 4. Electrical

### 4.1 Supply

**5 V, clean, 1 A minimum.** Vendor Power page, verbatim:

> *"The Astropixels require a clean 5V supply that can provide at least 1A of
> current. If you change the brightness or trigger certain effects then the
> current usage can be quite a bit higher. Most users shouldn't go over about
> 700mA with normal operation."*

| Path | Vendor's verdict |
| --- | --- |
| USB from a computer | Fine for testing. *"Whilst it is fine to use the USB connector for initial testing, they are fragile so shouldn't be used in a final install."* |
| USB power bank | *"Something like a 10000mAh one should run the Astropixels with standard code for 10 hours"* |
| Cut USB cable into the screw terminal | *"more robust and doesn't run the risk of pulling the fragile usb port off the ESP32"* |
| **Buck converter in the dome** | *"The best way (IMHO) ... Pass the raw main battery voltage through the slip ring, and then drop it down to 5v once it is in the dome."* Example named: [Pololu D30V30F5](https://www.pololu.com/product/4892) |

> [!IMPORTANT]
> **The vendor gives the reason for the buck converter.** *"Doing it this way
> reduces voltage losses and current through the slip ring."* A slip ring's
> channels are the scarce resource in a droid dome, and 700 mA at 5 V through
> ring contacts is both a bigger current and a worse voltage drop than the same
> power at pack voltage. It also keeps the dome's 5 V rail independent of the
> body's, which matters for the serial link -- see 6.1.

**Measured current for a set is `UNKNOWN`.** The vendor's ~700 mA is the only
figure in evidence and it is a typical-use estimate, not a measurement at a given
brightness. Settled by: an inline meter on the dome 5 V feed at the configured
`BRI` values, with the logics idling and again during a busy effect.

### 4.2 Brightness is the current knob, and it is a firmware constant

The `BRI` field of a `LogicEngineSettings` is 0-255 and the vendor's defaults are
front **160**, rear **140** (vendor Changing Colors page, Defaults table). The
vendor's own warning: *"Beware of both heat and power consumption if you raise it
too high."*

**No serial command reaches it.** Brightness is not a field in `LE`, in `@nT`, or
in the sequence packing formula -- the four packed fields are effect, color,
speed scale and duration (4.3). A dimmer dome means reflashing the dome.

**Logic display settings do not survive a reboot.** `Reeltwo LogicEngine.h:958-961`
is literally `void restoreSettings() { defaultSettings(); }`, and neither the
vendor firmware nor AstroPixelsPlus instantiates an NVS-backed settings
controller. Color, brightness and palette are compiled-in defaults on every boot;
anything a sender set before the reboot is gone.

### 4.3 The sequence packing formula

Every LogicEngine selection is one `long`, and the packing is declared at
`Reeltwo src/dome/LogicEngine.h:160`:

```cpp
static long sequence(byte seq, ColorVal colorVal = kDefault,
                     uint8_t speedScale = 0, uint8_t numSeconds = 0)
{
    return(
        (long int)seq * 10000L +
        (long int)colorVal * 1000L +
        (long int)speedScale * 100 +
        numSeconds);
}
```

That is the whole expressive budget of a logic command: **effect, color, speed
scale, duration in seconds.** Every logic grammar in this document -- `LE`,
`@nT` -- is a different spelling of those four numbers.

### 4.4 The data pins, from the vendor's own table

| Board | Default ESP32 GPIO |
| --- | --: |
| RLD | 33 |
| FLD | 15 |
| FPSI | 32 |
| RPSI | 23 |
| THP | 27 |
| RHP | 26 |
| FHP | 25 |
| AUX1 | 2 |
| AUX2 | 4 |
| AUX3 | 5 |
| AUX4 | 18 |
| AUX5 | 19 |
| Serial2 RX | 16 |
| Serial2 TX | 17 |

Vendor Overview page. The AstroPixelsPlus fork's `AstroPixelsPlus.ino:232-260`
declares exactly these values, so the vendor's table and the firmware agree --
which is worth saying, because they do not agree about baud (6.2).

The five AUX pins are the expansion budget, and they are contended. In the
AstroPixelsPlus fork they are claimed by, in build-flag order: the DFPlayer sound
serial (AUX4/AUX5), the CBI MAX7221 chain (AUX1/AUX2/AUX3), FireStrip (AUX4) and
BadMotivator (AUX5). The fork's shipped `platformio.ini` compiles all four
out (`AP_ENABLE_CBI=0`, `AP_ENABLE_DATAPANEL=0`, `AP_ENABLE_FIRESTRIP=0`,
`AP_ENABLE_BADMOTIVATOR=0`), so in that image the AUX pins are free.

### 4.5 The I2C header

*"The i2c header has +ve, Gnd, Data, and clock connections. Only the data, clock,
and gnd are needed. The default address is 0x0A."* (vendor, Interfacing). SDA is
GPIO 21 and SCL GPIO 22 (`AstroPixelsPlus.ino:232-233`).

Two different things use this bus and they must not be confused:

- **`0x0A`** -- the AstroPixels board acting as an I2C **slave**, so another
  controller can command it. This is how the stock firmware was originally
  driven.
- **`0x40` / `0x41`** -- the two PCA9685 expanders, on the same two pins, with
  the AstroPixels board as **master**. This is what AstroPixelsPlus uses.

`AstroPixelsPlus.ino` makes them mutually exclusive by construction:
`#define USE_I2C_ADDRESS 0x0a` is commented out with the note *"Define
USE_I2C_ADDRESS to enable slave mode. This will disable servo support"*, and the
`servoSettings[]` table is `#ifndef USE_I2C_ADDRESS`. **A dome cannot be an I2C
slave and move panels.**

### 4.6 Failure modes the vendor names

From the Troubleshooting page, worth carrying because each maps to a distinct
fault:

| Symptom | Vendor's diagnosis |
| --- | --- |
| Wrong colors, or not all boards lit | *"you have the wrong board connected to the wrong header"* |
| One board dark | Cable reversed -- S to S. Try the board on the RLD header to prove the board rather than the pin: *"it may be a dead pin on the ESP32"* |
| Nothing lit at all | Wiring and power. *"There should at least be a red light on the ESP32"* |
| Lit up to a point, then dark | A dead pixel. *"If a single pixel goes, then 99% of the time nothing after it will work either"* -- WS2812B chains are serial, so the first failure truncates everything downstream. Vendor replaces the board |

## 5. The three firmware families, and why the choice is load-bearing

### 5.1 What ships on the board

> *"If you have been messing with the firmware and want to go back to the basics,
> the example Astropixels sketch in the ReelTwo library is what is installed by
> default when they are shipped."* -- vendor, Original Firmware

That sketch is `dpoulson/Astropixels src/standard/main.cpp`, and it is 42 lines.
Reproduced in the relevant part:

```cpp
#define COMMAND_SERIAL Serial2
#define MD_BAUD 9600

I2CReceiver i2cReceiver(0x0a);

AstroPixelRLD<>       RLD(LogicEngineRLDDefault, 3);
AstroPixelFLD<>       FLD(LogicEngineFLDDefault, 1);
AstroPixelFrontPSI<>  frontPSI(LogicEngineFrontPSIDefault, 4);
AstroPixelRearPSI<>   rearPSI(LogicEngineRearPSIDefault, 5);

CommandEventSerial<> commandSerial(COMMAND_SERIAL);
```

`CommandEventSerial` -- **not** `MarcduinoSerial`. The stock board listens for
raw ReelTwo `CommandEvent` strings: `LE...` and `HP...`. It has no Marcduino
parser at all, and no panel servos.

### 5.2 The chooser

| Family | Where from | Serial grammar | Baud | Panels |
| --- | --- | --- | --: | --- |
| **`standard`** (shipped) | vendor repo / [web installer](https://dpoulson.github.io/Astropixels/) | `LE` / `HP` ReelTwo commands, plus I2C slave `0x0A` | 9600 | none |
| `imperial`, `r2kt`, `special` | same | as `standard`, different color defaults | 9600 | none |
| **`standard-md`** | same | JawaLite / Marcduino, 117 actions, plus `*RT` and `@AP` raw escapes | 9600 | 5, direct ESP32 pins |
| **AstroPixelsPlus (upstream)** | `reeltwo/AstroPixelsPlus` | Marcduino, plus `#AP*` config, `~RT` / `@AP` escapes, WiFi and a web UI | **2400** default | 13 + 6 over two PCA9685s |
| **AstroPixelsPlus fork** | `mattiasbrandt/AstroPixelsPlus` | as upstream, plus a Serial2 body-link mode (`mbodylink`), more logic effects, per-panel calibration in NVS, `/api/dome/layout` | 2400 default; its Setup page profile sets 9600 | 13 + 6 |

The vendor's own framing of the third-party option, verbatim: *"The AstroPixels
Plus firmware is a third party one created by Mimir who also did the main ReelTwo
library that the Astropixels are based on. ... This is with a thirdparty firmware
that I haven't tested or know much about."*

### 5.3 The stock grammar, for completeness

Worth recording, because a builder debugging a silent dome will want to know what
the board answers to *before* they reflash it, and because `@AP` / `~RT` / `*RT`
let you reach this grammar from inside a Marcduino build.

`LE<designation><effect><color><speed><time>`, leading zeros dropped:

| Designation | Device |
| --: | --- |
| 0 | all logics and PSI |
| 1 | front logics |
| 3 | rear logics |
| 4 | front PSI |
| 5 | rear PSI |

The effect numbers here **are** the `LogicEngineRenderer` enum values -- `00`
Normal, `01` Alarm, `02` Failure, `03` Leia, `04` March, `05` Single Color, `06`
Flashing Color, `07`/`08` Flip Flop, `09` Color Swap, `10` Rainbow, `14` Lights
Out, `15`-`18` text, `19` Roaming Pixel, `20`/`21` Scanline, `22` Fire, `23` PSI
Swipe, `24` Pulse, `99` Random (vendor, Interfacing). Color is `ColorVal`
(Section 7.7). Speed is 0-9 with 0 fastest, and the vendor documents its units
per effect: *"Flip Flop and Rainbow - 200ms x speed; Flash - 250ms x speed; March
- 150ms x speed; Color Swap - 350ms x speed"*. Time is two digits of seconds,
`00` continuous.

The vendor's own worked examples: *"you could send via i2c the command 'LE30000'
to set all logics to a pale green for the duration of the Leia message, or
'LE1201010' to make the front logics run a cylon style effect."*

### 5.4 The vendor's Marcduino build against the fork

`standard-md` exists and is the vendor's answer to *"act more like a drop in
replacement for existing teecees lights"*. Its wiring note is the tersest good
instruction in this whole subject: *"connect from the slave port on the Marcduino
to the Serial2 port on the Astropixels. You just need to connect - to G, and S to
R. No need for the +ve/power."*

It is a far smaller firmware than the AstroPixelsPlus fork:

| | `standard-md` | AstroPixelsPlus fork |
| --- | --: | --: |
| `MARCDUINO_ACTION` registrations | 117 | **314** |
| Panel servo channels | 5, on direct ESP32 pins | 13 panel + 6 holo, over two PCA9685s |
| Panel calibration persisted | no | yes, NVS, per panel |
| Serial2 body-link mode | no | yes |
| Web UI / REST API | no | yes |

`standard-md` also carries `%`-routing advice for a MarcDuino master: *"You may
need to edit the commands sent out to add % to the beginning of them. This sends
the command out of the slave port on the marcduino (dropping the %) and into the
Astropixels."*

### 5.5 Provenance and licence

| Artefact | Author | Licence |
| --- | --- | --- |
| AstroPixels PCB designs | Darren Poulson | **Not published.** *"Are the files available for me to make my own? At the moment, no."* Manufactured at JLCPCB, designed in EasyEDA |
| `dpoulson/Astropixels` firmware | Darren Poulson | **No LICENSE file** -- default all-rights-reserved |
| `dpoulson/Reeltwo` | fork of reeltwo/Reeltwo | LGPL-2.1 |
| `reeltwo/Reeltwo` | Mimir Reynisson | LGPL-2.1 |
| `reeltwo/AstroPixelsPlus` | Mimir Reynisson | LGPL-2.1 |
| `mattiasbrandt/AstroPixelsPlus` | fork of the above | LGPL-2.1 |

> [!NOTE]
> The vendor tells you to install **`dpoulson/Reeltwo`**, not the upstream
> library: *"This is a fork of the original repository due to a long standing bug
> that meant it would not compile. I've since reverted the change that caused it,
> along with adding a few extra functions."* The AstroPixelsPlus fork instead pins
> **`reeltwo/Reeltwo#23.5.3`** (`platformio.ini`). Both build. Which extra
> functions the vendor's fork adds, and whether any of them matter to
> AstroPixelsPlus, is `UNKNOWN`; settled by diffing the two libraries.

## 6. The Serial2 header

### 6.1 Physical

| Property | Value |
| --- | --- |
| Port | `Serial2`, RX GPIO 16 / TX GPIO 17 |
| Header | the Main Board's `G V R T` header |
| Framing | `SERIAL_8N1` |
| Baud | AstroPixelsPlus: NVS `mserial2`, 2400 or 9600 (6.2); vendor builds: 9600 |
| Conductors for a two-way link through a slip ring | 3: TX, RX, GND |

The header is silkscreened `G V R T`. **`V` is left unconnected for a link to a
body controller** -- the AstroPixelsPlus fork's wiring guide is explicit that no
power crosses the slip ring, *"slip-ring channels are a finite resource"*, and
separate supplies back-feeding each other is the hazard. TX and RX cross: dome
`T` to the controller's RX, dome `R` to the controller's TX. Common ground is
mandatory. The vendor's `standard-md` note says the same for a one-way link from
a MarcDuino: *"connect - to G, and S to R. No need for the +ve/power"* (5.4).

### 6.2 The baud is a persisted dome-side setting, and the fork's docs are wrong about it

This is the single most likely reason a correctly wired link stays silent.

| Source | Says | Verdict |
| --- | --- | --- |
| `AstroPixelsPlus.ino:197` | `#define MARC_SERIAL2_BAUD_RATE 2400` | **The firmware default.** True |
| `AstroPixelsPlus.ino:1202` | `COMMAND_SERIAL.begin(preferences.getInt(PREFERENCE_MARCSERIAL2, MARC_SERIAL2_BAUD_RATE), ...)` | **The actual value is NVS key `mserial2`**, defaulting to 2400 |
| Fork `data/setup.html:284-288` | the Setup page's link profile defaults `mserial2: '9600'` | **The Setup page profile.** True |
| Fork `docs/HARDWARE_WIRING.md:470-479` | *"Serial2 runs at **2400 baud** ... Both dome and body must use 2400 baud on this link"* | **Wrong**, and it quotes a constant `COMMAND_BAUD_RATE` that does not exist in the source |
| Fork `docs/SETUP.md:926,1040` | *"Serial2 (2400 baud...)"*, *"Serial2 baud rate is **2400**"* | **Wrong** |
| Fork `docs/COMMANDS.md:51` | *"Serial2 -- TTL header at 2400 baud"* | **Wrong** |
| upstream README | *"running at 2400 baud"* on the Serial2 header | True of upstream's default |
| **Vendor, both stock builds** | `#define MD_BAUD 9600` | **9600 is the vendor's own baud** |

> [!CAUTION]
> **The baud is stored in the dome's NVS rather than compiled in.** A dome
> flashed from scratch with no `mserial2` key comes up at **2400** and will not
> hear a controller talking 9600. Set `mserial2` to 9600 on the dome's Setup page,
> and set it again after any `#APZERO` factory reset (7.8). Three documents in the
> fork's repository make four statements that Serial2 runs at 2400; they are
> stale and are named above so nobody re-derives the wrong answer from them.
>
> The 2400 default is not an error upstream: AstroPixelsPlus is designed to sit
> where a JEDI display sits, and the MarcDuino slave drives its JEDI port at 2400
> by default. A body controller wired to the same header is a different
> position, and the hobby's body controllers talk 9600 (10.1).

### 6.3 Framing hazards

`Reeltwo src/core/Marcduino.h:165` terminates on **`0x0D` only**:

```cpp
if (ch == 0x0D)
{
    fBuffer[fPos] = '\0';
    fPos = 0;
    if (*fBuffer != '\0')
        Marcduino::processCommand(fPlayer, fBuffer);
}
else if (fPos < SizeOfArray(fBuffer)-1)
    fBuffer[fPos++] = ch;
```

A line feed is therefore **data, not a terminator**: `\r\n` framing leaves a
stray `\n` as the first byte of the next command, and that command then matches
nothing. Send a bare `\r`.

The AstroPixelsPlus fork does not use `MarcduinoSerial` in its body-link mode.
When `mbodylink` is enabled it clears `marcduinoSerial.setStream()` outright and
reads Serial2 itself in `handleBodySerial()`, which accepts **either** `\r` or
`\n` (`AstroPixelsPlus.ino:926`). It is also why hardware Marcduino over Serial2
is unavailable while the body link is on.

| Limit | Value | Source |
| --- | --- | --- |
| Body-link RX buffer (fork) | 65 bytes -- **64 data + NUL** | `AstroPixelsPlus.ino:921` |
| `MarcduinoSerial` buffer (upstream path) | 64 -- **63 usable** | `Marcduino.h:121` |
| Overflow behaviour, both paths | **silent discard** | `AstroPixelsPlus.ino:952-958`; `Marcduino.h:174` |

**63 characters is the budget that fits both paths.**

### 6.4 What the firmware does *not* send

**There is no ACK, no NAK, no error line and no status query on Serial2.** The
only dome-originated Serial2 traffic in the AstroPixelsPlus fork comes from its
body-link mode, for its own reasons; upstream sends nothing (10.2). A command the
firmware could not parse produces nothing at all, so a sender has to validate
before it sends.

The fork's richer health picture -- `body_link.enabled`, `connected`,
`transport`, `uart_hb_age_ms`, `wifi_hb_age_ms`, `hb_rx`, `peer_ip`,
`peer_source` -- exists only on its own `/api/health`, over HTTP (Section 8).

## 7. The command grammar

### 7.1 There is no parser. There is a prefix-match list.

Everything in this section follows from one function, and an integrator who does
not know it will misread every table below.
`Reeltwo src/core/Marcduino.h:37-64`, verbatim:

```cpp
static void processCommand(AnimationPlayer& player, const char* cmd)
{
    bool found = false;
    for (Marcduino* marc = *head(); marc != NULL; marc = marc->fNext)
    {
        int len = strlen_P(marc->fMarc);
        if (strncmp_P(cmd, marc->fMarc, len) == 0 ||
            (marc->fMarc[0] == '@' && isdigit(cmd[0]) && strncmp_P(cmd, marc->fMarc+1, len-1) == 0 && len--))
        {
            AnimationStep animation = marc->fAnimation;
            if (animation != NULL)
            {
                *command() = cmd + len;
                player.animateOnce(animation);
                found = true;
            }
        }
    }
    if (!found && *cmd == '@') { /* JawaCommander fallback */ }
}
```

Five consequences, each verified against the AstroPixelsPlus fork's source:

**(a) Matching is unanchored prefix matching.** Each `MARCDUINO_ACTION(name,
string, body)` appends one entry to a flat list. A command matches if the
registered string is a *prefix* of it; the remainder becomes the argument,
retrieved with `Marcduino::getCommand()`.

**(b) There is no validation and no error path.** An unrecognised command is
dropped in silence -- and a *longer* unknown code runs a *shorter* registration's
action. `@0T25` matches the registration `@0T2` and runs FLASHCOLOR. `@0T100`
matches `@0T1` and runs NORMAL. The dome will never tell you.

**(c) The loop does not break, and the last match wins.** `animateOnce()`
replaces the single animation slot, so when several registrations match, the one
registered **last** is the one that runs. Registration order is declaration
order, which is `#include` order in `AstroPixelsPlus.ino`:

```
line 550  MarcduinoSound.h      $
line 741  MarcduinoHolo.h       *, @6/@7/@8, @HP
line 743  MarcduinoLogics.h     @0T/@1T/@2T, @nM, @nP60/61
line 744  MarcduinoSequence.h   :SE
line 745  MarcduinoPanel.h      :
line 746  MarcduinoPSI.h        @0P/@1P/@2P
```

**(d) The `@` is optional only in front of a digit.** The second clause needs
both `fMarc[0] == '@'` and `isdigit(cmd[0])`. So `@0T1` and `0T1` are the same
command; `@HP...` and `@AP...` require the `@`; and `*`, `:`, `$`, `#` always
require their own prefix. This clause exists because a MarcDuino *slave* strips
the `@` before forwarding to its JEDI port -- a board wired in the JEDI's place
sees `0T1\r`, and one wired to a body controller sees `@0T1\r`. Both work.

**(e) The generic JawaLite fallback is dead code here.** It fires only if a
`JawaCommander<>` has been instantiated, and `grep -rn JawaCommander` over the
fork returns nothing. Only explicitly registered strings do anything. This is
visible in the fork's own code: `MarcduinoPanel.h:4` issues `@4S3`, a JawaLite
"set PSI random speed", which matches no registration and is therefore a no-op.

> [!CAUTION]
> **`@1P60` does not set the Latin font. It sets the front PSI to Leia.**
> `MarcduinoLogics.h` registers `@1P60` (font) and `MarcduinoPSI.h` registers
> `@1P6` (PSI Leia). `@1P6` is a prefix of `@1P60`, MarcduinoPSI.h is included
> **after** MarcduinoLogics.h, and last match wins. The same holds for `@1P61`,
> `@2P60` and `@2P61`. Only `@3P60` / `@3P61` behave as documented, because no
> `@3P6` registration exists. Verified in the AstroPixelsPlus fork at
> `AstroPixelsPlus.ino:743,746`. Do not send `@1P60` or `@2P60` expecting a font
> change.

### 7.2 `@` -- logic displays

| Command | Renderer effect | Enum |
| --- | --- | --: |
| `@0T1` / `@1T1` / `@2T1` | NORMAL | 0 |
| `@nT2` | FLASHCOLOR | 6 |
| `@nT3` | ALARM | 1 |
| `@nT4` | FAILURE | 2 |
| `@nT5` | REDALERT | 11 |
| `@nT6` | LEIA | 3 |
| `@nT11` | MARCH | 4 |
| `@nT12` | RAINBOW | 10 |
| `@nT15` | LIGHTSOUT | 14 |
| `@nT22` | FIRE | 22 |
| `@nT24` | PULSE | 24 |

Address `0` is both displays, `1` is the FLD pair, `2` is the RLD
(`MarcduinoLogics.h`).

> [!IMPORTANT]
> **The last four rows are fork-only.** Upstream `reeltwo/AstroPixelsPlus` has no
> `@nT12`, `@nT15`, `@nT22` or `@nT24`. On upstream those degrade silently by
> rule (b): `@0T12` and `@0T15` run NORMAL, `@0T22` and `@0T24` run FLASHCOLOR.
> A sequence that uses them looks right on the fork and wrong on upstream.

> [!NOTE]
> **The wire number is not the renderer number**, and both appear in this
> subject. `@0T5` is REDALERT (11); `@0T11` is MARCH (4). The vendor's native
> `LE` grammar (5.3) uses the renderer numbers directly, so `LE011` and `@0T3`
> are the same effect spelled two ways. The fork's own `docs/COMMANDS.md:321-337`
> lists only seven of the eleven rows above and is out of date against its source.

Text, from `MarcduinoLogics.h:243-265`:

| Command | Target |
| --- | --- |
| `@1M<text>` | top half of the FLD |
| `@2M<text>` | bottom half of the FLD |
| `@3M<text>` | RLD |

`@1M` and `@2M` are **not independent**: both write into static buffers and then
re-render the whole front display as `"<top>\n<bottom>"`. Color is
`FLD.randomColor()` -- which, because `randomColor()` is `ColorVal(random(10))`,
can return `kDefault`. There is no Marcduino command that sets the color of
`@nM` text.

### 7.3 `@` -- PSI

`@0P<n>` both, `@1P<n>` front, `@2P<n>` rear, with the same seven effect numbers
as the `T` family: 1 NORMAL, 2 FLASHCOLOR, 3 ALARM, 4 FAILURE, 5 REDALERT,
6 LEIA, 11 MARCH (`MarcduinoPSI.h`).

**Address numbers mean different devices depending on the verb.** `@1T` is the
FLD and `@1P` is the front PSI; `@2T` is the RLD and `@2P` is the rear PSI. That
is not a transcription error here -- it is `MarcduinoLogics.h:3` against
`MarcduinoPSI.h:3`.

### 7.4 `*` -- holoprojectors

The `*` family is a set of thin wrappers that emit ReelTwo `HP...`
`CommandEvent` strings. Example, `MarcduinoHolo.h`:

```cpp
MARCDUINO_ACTION(FrontHoloPosDown, *HP001, ({
    CommandEvent::process(F("HPF1010"));
}))
```

The underlying grammar is `HP<designator><type><function><color><...>` with an
optional `|<seconds>` runtime suffix:

| Field | Values |
| --- | --- |
| designator | `F` front, `R` rear, `T` top, `D` radar eye, `O` other, `A` all three, `X` front+rear, `Y` front+top, `Z` rear+top, `S` sequences |
| type | `0` LED functions, `1` servo functions |
| LED function | `01` Leia (forced blue), `02` color projector, `03` dim pulse, `04` cycle, `05` solid color, `06` rainbow, `07` short circuit, `96`-`99` twitch/override control, `00` off |
| servo function | `01` go to preset position, `04` random position, `05` wag L/R, `06` wag U/D, `98`/`99` disable/enable auto twitch |
| position | `0` down, `1` centre, `2` up, `3` left, `4` upper left, `5` lower left, `6` right, `7` upper right, `8` lower right |
| runtime | `|<seconds>`, after which the holo returns to its last auto-twitch mode |

So `HPF0040` is front / LED / cycle / random color, and `HPF1010` is front /
servo / preset position / down. The vendor documents the same scheme on its
HoloProjectors and Interfacing pages, and confirms `HPA0025|20` *"will turn all
HPs twinkling blue for 20 seconds"*.

> [!CAUTION]
> **`@7` is the top holo and `@8` is the rear holo in this firmware, which is the
> reverse of every specification including ReelTwo's own.**
>
> | | 6 | 7 | 8 |
> | --- | --- | --- | --- |
> | `Reeltwo/docs/JawaLite.dox` | front | **rear** | **top** |
> | `Reeltwo/src/core/JawaEvent.h` | `kJawaFrontHolo = 6` | `kJawaRearHolo = 7` | `kJawaTopHolo = 8` |
> | **fork `MarcduinoHolo.h:664-706`** | front | **top** | **rear** |
>
> Verified in the AstroPixelsPlus fork's source: `@7T1` emits `HPT0040` and
> `@8T1` emits `HPR0040`. A body controller following the JawaLite table moves
> the wrong holoprojector. The `*HP` forms, which name the holo by letter, are
> not affected.

Holo motion needs servos on the second PCA9685, six channels; the LED
functions work on a bare kit.

### 7.5 `$` -- sound

The AstroPixelsPlus fork's shipped image compiles sound out: `platformio.ini`
sets `-DMARC_SOUND_LOCAL_ENABLED=false`, and `AstroPixelsPlus.ino:196` defaults
`MARC_SOUND_PLAYER` to `MarcSound::kDisabled`. **That image makes no sound.** A
build with local sound enabled plays through the DFPlayer serial on AUX4/AUX5
(4.4).

### 7.6 `:` -- panels

`:OPnn` opens, `:CLnn` closes, `:OFnn` flutters, and there are pie-panel spellings
(`:OPP1`-`:OPP6`), group shortcuts and thirteen dynamic group choreographies
(`:OW$`, `:OMA$`, `:OD$`, ...). The full panel map is Section 9.

The MarcDuino dialect assumes two boards, and a body MarcDuino uses the same
`:OPnn` numbers for the body's own panels. `:OP01` sent to this dome opens ring
panel P1 (`AstroPixelsPlus.ino:430`); the same string means something else to a
body board.

### 7.7 Two color number spaces on one board

This is the most reliable way to get a wrong-colored dome, and both are
primary-sourced.

| Code | `ColorVal` (logics and PSI) | `HP` (holoprojectors) |
| --: | --- | --- |
| 0 | kDefault | Random |
| 1 | kRed | Red |
| 2 | kOrange | **Yellow** |
| 3 | kYellow | **Green** |
| 4 | kGreen | **Cyan** |
| 5 | kCyan | **Blue** |
| 6 | kBlue | **Magenta** |
| 7 | kPurple | **Orange** |
| 8 | kMagenta | Purple |
| 9 | kPink | **White** |

Sources: `Reeltwo src/dome/LogicEngine.h:143-155`; the vendor's HoloProjectors
page and `Reeltwo src/dome/HoloLights.h`.

> [!WARNING]
> **`ColorVal` has no white.** Holos do (index 9); the logics and PSI cannot show
> white at all. The AstroPixelsPlus fork maps a requested logic `WHITE` to
> `kDefault` and says so in its own source comment:
>
> ```cpp
> if (strcmp(token, "WHITE") == 0)  { out = LogicEngineRenderer::kDefault; return true; } // ReelTwo logics have no white ColorVal.
> ```

`randomColor()` is `ColorVal(random(10))`, so it can return `kDefault`.
**Brightness and palette are not reachable over serial at all** (Section 4.2).

### 7.8 `#` -- configuration

Handled by the dome. From the upstream README and the fork:

| Command | Effect |
| --- | --- |
| `#APWIFI` / `#APWIFI0` / `#APWIFI1` | toggle / off / on |
| `#APREMOTE` / `#APREMOTE0` / `#APREMOTE1` | droid remote support (compiled out in the fork's shipped image) |
| `#APZERO` | clear all preferences **including WiFi and `mserial2`** -- see the caution in 6.2 |
| `#APRESTART` | restart |
| `#SO`, `#SC`, `#SW` | fork-only panel calibration: save open, save closed, swap |

The fork's body-link mode also uses the `#` prefix for its heartbeat and sleep
lines; those are not configuration.

### 7.9 Escapes to the native layer

Three registrations let a Marcduino-era sender reach the raw ReelTwo
`CommandEvent` grammar of Section 5.3 without reflashing:

| Escape | Where | Effect |
| --- | --- | --- |
| `@AP<cmd>` | fork and upstream | `CommandEvent::process(<cmd>)` |
| `~RT<cmd>` | upstream | same |
| `*RT<cmd>` | vendor `standard-md` | same |

These are the only way to set a logic color and duration together from a
Marcduino-shaped link. They are unvalidated, and `@APLE1060015` spends 13 of the
63 characters (6.3) on one logic selection.

### 7.10 Concurrency: one animation slot

`AnimationPlayer player(servoSequencer)` is a singleton and `animateOnce()` calls
`end()` before installing the new step. **Any matching command aborts whatever
multi-step sequence is running.** Single-step actions complete within one
`AnimatedEvent::process()` pass and chain safely; multi-step ones
(`MARCDUINO_ANIMATION` -- the `:SE05`-`:SE09` family and the fork's long `DM:`
dances) run for tens of seconds and will be cut short by the next command that
matches anything.

This is the mechanism behind the fork's queue and its drain discipline: the body
link pumps `drainMarcduinoCommandQueue()` after **each** complete frame rather
than once per main loop, so *"a dense body choreography cannot fill the shared
ingress queue"* (`AstroPixelsPlus.ino:940-943`). The queue is eight entries
(`MarcduinoIngress.h`), and a full queue logs `[CMD][<source>][queue-full]` and
drops.

**There is no flow control and no inter-command pacing in the protocol.** The
classic MarcDuino ecosystem assumes the host paces itself -- CuriousMarc's `\p`
100 ms pause is an app-side convention that never reaches the wire, and
MarcDuino's own `init_jedi()` uses 100 ms after a holo command and 20 ms between
display commands. **Pacing is the sender's job.** What rate the fork's ingress
actually sustains is `UNKNOWN`; settled by streaming alternating `@0T1`/`@0T2` at
9600 and counting applied effects against sent commands.

## 8. The fork's HTTP surface

The AstroPixelsPlus fork runs an async web server. Two endpoints matter to
anything that talks to the dome from outside:

- **`GET /api/dome/layout`** -- the dome's own report of the
  panel/holo/PSI/logic elements it can move or light, composed with live runtime
  state. About 24 KB.
- **`POST /api/cmd`** -- takes a command over HTTP, body `cmd=<command>`,
  form-encoded.

The rest is the dome's own operator surface: `/api/state`, `/api/health`
(including the `body_link` block of 6.4 and per-family apply and reject counts),
`/api/sleep`, `/api/wake`, `/api/diag/i2c`, `/api/panels/config`,
`/api/holos/config`, `/api/servo/test`, `/api/servo/stop`,
`/api/dome/layout-template` and `/api/dome/element-status`.

> [!NOTE]
> **Two networks are in play and they are easy to conflate.** The dome's own AP
> defaults to SSID `AstroPixels` / passphrase `Astromech` at
> `http://192.168.4.1`. The fork prefers client mode on a LAN, finds its body
> peer by mDNS, and takes a manual `bodypeerip` override. A body peer reaches the
> dome over WiFi only when both are on the **same** network, which an AP-mode
> dome is not.

## 9. The panel map

What `:OPnn` actually moves, on a Mr Baddeley MK4 complex dome with the
AstroPixelsPlus fork's default wiring. Source: `AstroPixelsPlus.ino:398-453` and
`WiringConfig.h`.

| Slot | Part | PCA9685 `0x40` channel | Firmware pin | Open command(s) |
| --: | --- | --: | --: | --- |
| 0 | P1 (ring) | CH0 | 1 | `:OP01` |
| 1 | P2 (ring) | CH1 | 2 | `:OP02` |
| 2 | P3 (ring) | CH2 | 3 | `:OP03` |
| 3 | P4 (ring) | CH3 | 4 | `:OP04` |
| 4 | P7 (ring upper) | CH4 | 5 | `:OP07` |
| 5 | P11 (ring lower) | CH5 | 6 | `:OP11` |
| 6 | P13 (ring front) | CH6 | 7 | `:OP13` |
| 7 | PP5 (pie) | CH7 | 8 | `:OPP5` -- **inactive by default** |
| 8 | PP1 (pie) | CH8 | 9 | `:OP08` / `:OPP1` |
| 9 | PP2 (pie) | CH9 | 10 | `:OP09` / `:OPP2` |
| 10 | PP4 (pie) | CH10 | 11 | `:OP10` / `:OPP4` |
| 11 | PP6 (pie) | CH11 | 12 | `:OP12` / `:OPP6` |
| 12 | PP3 (pie) | CH12 | 13 | `:OPP3` -- **inactive by default** |

Holoprojector axes, PCA9685 `0x41`:

| Slot | Axis | Channel | Firmware pin |
| --: | --- | --: | --: |
| 13 | FHP horizontal | CH0 | 17 |
| 14 | FHP vertical | CH1 | 18 |
| 15 | THP horizontal | CH2 | 19 |
| 16 | THP vertical | CH3 | 20 |
| 17 | RHP vertical | CH4 | 21 |
| 18 | RHP horizontal | CH5 | 22 |

Group forms: `:OP00` all panels, `:OP14` top panels, `:OP15` bottom panels.
`:OP05` and `:OP06` are **recognised no-ops** -- P5 carries the Magic Panel and
P6 is fixed, so neither has a servo. The fixed panels with no slot at all are
P5, P6, P8 (rear PSI), P9 (rear logic), P10, P12 (front logic) and P14 (front
PSI).

Three facts that matter more than the table:

1. **Default pulse range is 800-2200 us**, and per-panel calibration lives in the
   dome's NVS, set from its web UI or with `#SO` / `#SC` / `#SW` (7.8).
2. **Which panels exist is a design fact; which channel each is on is not.** The
   source says so: *"The Mr Baddeley MK4 Complex Dome IS a standard for WHICH
   panels exist ... The CHANNEL-TO-SLOT mapping below is NOT a standard --
   builders wire each servo to whichever PCA9685 silkscreen channel is physically
   convenient."* Overrides live in NVS namespaces `panels` and `holos`, applied
   by `panelConfigLoad()` / `holoConfigLoad()` **before** `SetupEvent::ready()`,
   because that call triggers the first I2C write.
3. **PP3 is deliberately excluded from dance choreographies**, because PP3 mounts
   HP3: *"dance sequences must not disturb the holo projector"*
   (`AstroPixelsPlus.ino:352-354`).

## 10. How the hobby drives a dome (non-normative)

Survey of the projects that drive a dome over serial. Every claim here is about
those projects.

### 10.1 The universal conventions

**9600 8N1, bare `\r`, no flow control, no acknowledgement.** That combination
holds across MarcDuino, ShadowMD, ShadowRC and PenumbraShadowMD. Two traps worth
recording because they have cost other people time:

- CuriousMarc's own command reference says commands end with *"carriage return
  (0x13)"*. **0x13 is DC3.** CR is 0x0D, as MarcDuino's own `CMD_END_CHAR '\r'`
  and ReelTwo's `if (ch == 0x0D)` both confirm. Do not propagate 0x13.
- MarcDuino panel commands are **length-rigid**: `if (length!=5)` rejects them.
  `:OP1` and `:OP001` are both silently dropped. Always zero-pad.

**The dome owns the sequence.** In every MarcDuino-lineage project the body sends
a *name* (`:SE01`) and never a step list.

**Port allocation.** ShadowMD on an Arduino Mega: `Serial1` dome MarcDuino,
`Serial2` motor controllers, `Serial3` body MarcDuino, all 9600. PenumbraShadowMD
on ESP32 keeps the same shape.

### 10.2 Nobody has a working return path

- **No heartbeat, no watchdog, no retry, no CRC** on any serial dome link in the
  surveyed corpus.
- MarcDuino *does* echo every received character and emits `OK\n\r` after
  accepted `:` and `#` commands -- and **no body controller in the corpus reads
  it.** PenumbraShadowMD's entire use of the dome RX line is to drain it and
  print it to USB.
- Error messages are compiled out: MarcDuino ships with `_ERROR_MSG_ 0` and
  `_FEEDBACK_MSG_ 0` in the master.
- Upstream AstroPixelsPlus emits nothing at all on Serial2. The fork's body-link
  mode is the exception in this corpus.

### 10.3 Pacing, as the ecosystem assumes it

- PenumbraShadowMD rate-limits to **one command per button per second**, and
  inserts `delay(50)` after a group-close before the next routine and `delay(30)`
  between panel commands.
- ShadowRC uses 300-550 ms where a servo must finish first, and fires two or
  three commands back-to-back otherwise.
- MarcDuino's own `init_jedi()` uses 100 ms after a holo command and 20 ms
  between display commands.
- CuriousMarc's `\p` 100 ms pause is an **app-side** convention that never
  reaches the wire: *"If you are commanding the MarcDuino from your own
  controller ... you'll have to implement the delays between commands yourself."*

A body controller that bursts without pacing is outside what any dome firmware in
this family was built for.

### 10.4 Slip ring practice

No project's source documents a dome-serial slip-ring conductor budget; what
exists is community documentation.

- Printed Droid: the 24-wire ring is *"the standard, with eight of these wires
  dedicated to signal transmission"*; the 12-wire allocates four to signal; *"each
  wire capable of handling up to 2 amperes"* with a recommendation not to exceed
  7 A total. And: *"It is most prudent to transmit the maximum voltage into the
  dome and then step it down locally within the dome"* -- the same advice the
  AstroPixels vendor gives (4.1).
- Reeltwo's own dome BOM cites a 12-wire ring.
- **A serial dome link needs three conductors for two-way traffic: TX, RX,
  GND.** Most of the hobby uses two, because its links are one-way.
- **Noise mitigation is `UNKNOWN`.** No project documents a slip-ring-specific
  retry, CRC or baud reduction. The AstroPixelsPlus fork's troubleshooting
  advice -- rotate the dome by hand while watching for heartbeat gaps, add 100 nF
  across the UART lines at the ring connector, keep signal wires away from motor
  runs -- is the most concrete guidance found anywhere, and it is untested.

### 10.5 Level shifting

The AstroPixels Main Board's ESP32 is a 3.3 V part, so a 3.3 V controller such
as another ESP32 needs no level shifting on the Serial2 lines. A 5 V controller
such as an Arduino Mega does: whether the Serial2 RX net is 5 V tolerant is
**`UNKNOWN`** -- no repo, vendor page or schematic in evidence addresses it, and
the design files are not public. Settled by a continuity check on the RX net for
a series resistor or divider, or by asking the vendor.

## 11. How the two Dome Controller members differ

| | **AstroPixels** | **Teeces** |
| --- | --- | --- |
| Lighting | WS2812B addressable RGB | MAX7219-driven discrete LEDs |
| Color | any, per pixel | fixed by the LED fitted |
| Controller | ESP32, socketed 30-pin devkit | Arduino Pro Micro on the RLD |
| WiFi / web UI / OTA | yes, with AstroPixelsPlus | none |
| LED count | 269 | 7 MAX7219s driving 296 |
| Text and fonts | yes, Latin and Aurabesh | yes, via JawaLite |
| Panel servos | yes, with two PCA9685s | no |
| Holoprojectors | yes, light and servo | no -- separate device |
| Supply | 5 V, ~700 mA typical | 5 V, current `UNKNOWN` |
| Serial | 9600 stock, 2400/9600 on AstroPixelsPlus | 9600 |
| Protocol | `LE`/`HP` native, or Marcduino/JawaLite with the right firmware | JawaLite |
| Price | GBP 80, one supplier, out of stock on the research date | varies, several suppliers |
| Design files | not published | community, multiple vendors |

The two are mechanically interchangeable -- AstroPixels was designed to drop into
Teeces surrounds with the same mounting holes -- and protocol-incompatible in
every respect that matters. [`teeces-dome-lighting.md`](teeces-dome-lighting.md),
"How Teeces and AstroPixels Plus differ", is the difference list from the
other side.

## 12. Quick Reference

| Question | Value | Section |
| --- | --- | --- |
| What is the product? | AstroPixels: 9 PCBs, WS2812B, GBP 80, Darren Poulson | 2 |
| What is AstroPixelsPlus? | third-party firmware for it, by Mimir Reynisson; the AstroPixelsPlus fork extends it | 5 |
| Which firmware families parse Marcduino? | `standard-md`, AstroPixelsPlus upstream and the fork | 5.2 |
| Dome MCU | 30-pin ESP32 devkit, `board = esp32dev`, Arduino core 2.0.x only | 2.2 |
| LED total | 269 WS2812B | 2.3 |
| Supply | 5 V, >= 1 A, ~700 mA typical | 4.1 |
| Dome serial pins | GPIO 16 RX / GPIO 17 TX, header `G V R T`, `V` unconnected to a body controller | 4.4, 6.1 |
| **Baud** | AstroPixelsPlus: NVS key `mserial2`, firmware default **2400**; vendor builds 9600 | **6.2** |
| Terminator | `\r` (0x0D) only on the Marcduino path | 6.3 |
| Max command length | **63 characters** | 6.3 |
| Does the dome ACK? | **No. Never. Nothing.** | 6.4 |
| Prefix families | `@` logics/PSI, `*` holos, `:` panels, `$` sound, `#` config | 7 |
| Unknown command behaviour | **silently runs a shorter prefix's action** | 7.1b |
| `@0T5` is | REDALERT (enum 11), not effect 5 | 7.2 |
| `@1P60` is | **front PSI Leia, not the Latin font** | 7.1 |
| `@7` / `@8` are | **top / rear** -- reversed vs JawaLite | 7.4 |
| White on the logics? | no; `ColorVal` has no white | 7.7 |
| Color spaces | two, mutually incompatible | 7.7 |
| Panel map | 13 panel slots + 6 holo axes, two PCA9685s | 9 |
| Does the dome persist logic settings? | **No. Every reboot is compiled-in defaults** | 4.2 |

## 13. Open Items

Each with the artefact or bench test that settles it.

1. **Measured current for a set.** Vendor says ~700 mA typical; no measurement is
   in evidence. *Inline meter on the dome 5 V feed, idle and during a busy
   effect.*
2. **Sustained command rate.** Unmeasured. *Stream alternating `@0T1`/`@0T2` at
   9600 and count applied effects against sent commands; watch for
   `[CMD][...][queue-full]` in the dome log.*
3. **Is the AstroPixels Serial2 RX net 5 V tolerant?** Relevant to anyone
   driving it from a Mega. *Continuity check for a series resistor, or ask the
   vendor.*
4. **Which PSI variant is fitted.** 25-LED `AstroPixelPSIPCB` is what the
   firmware instantiates; a 64-LED 8x8 variant exists in the library. *Count the
   LEDs on the board.*
5. **What `dpoulson/Reeltwo` adds over `reeltwo/Reeltwo`.** The vendor's install
   instructions name their own fork; the AstroPixelsPlus fork pins upstream
   23.5.3. *Diff the two.*
6. **Whether `*HPS303` still crashes.** Upstream issue #2 reported a
   `Guru Meditation ... IntegerDivideByZero` on that command. In the pinned
   23.5.3 the dim-pulse speed has a default assignment
   (`HoloLights.h:382`) and the divisor is non-zero on the default path, so it
   appears fixed -- **not reproduced**. *Send `*HPS303` to a dome and watch the
   reset reason.*
7. **Four stale baud statements in the AstroPixelsPlus fork's docs** (6.2).
   *Correct them in the fork.*
8. **The fork's `docs/COMMANDS.md` logic-effect table is incomplete** against
   its own source -- seven of eleven rows (7.2). *Correct in the fork.*
9. **Availability.** Out of stock on the research date, one supplier, no spares
   sold, design files closed (2.5). Nothing to test; a risk to carry.

## 14. Sources

**Vendor (product authority)**
- AstroPixels documentation, https://r2djp.gitbook.io/astropixels -- read in
  full via its `llms-full.txt` export (20 pages, 33 KB)
- Product listing, https://we-make-things.co.uk/product/astropixels/
- `dpoulson/Astropixels` -- stock firmware, five build environments, no LICENSE
- `dpoulson/Reeltwo` -- the library the vendor's instructions install
- AstroPixels web installer, https://dpoulson.github.io/Astropixels/

**Firmware**
- `reeltwo/Reeltwo` @ **23.5.3** -- at the tag the AstroPixelsPlus fork pins.
  LGPL-2.1. `src/core/Marcduino.h`, `src/dome/LogicEngine.h`, `src/dome/NeoPSI.h`,
  `src/dome/HoloLights.h`, `docs/JawaLite.dox`
- `reeltwo/AstroPixelsPlus` -- upstream. LGPL-2.1
- `mattiasbrandt/AstroPixelsPlus` -- the AstroPixelsPlus fork. LGPL-2.1.
  `AstroPixelsPlus.ino`, `MarcduinoIngress.h`, `DomeSequences.h`,
  `WiringConfig.h`, `Marcduino*.h`, `data/setup.html`, `docs/HARDWARE_WIRING.md`,
  `FORK_IMPROVEMENTS.md`
- `reeltwo/PenumbraShadowMD` -- the reeltwo body-side sketch, for convention
- `nhutchison/MarcDuinoMain` and `MarcDuinoClient` -- classic MarcDuino
- `dankraus/padawan360`, ShadowRC v1.3, `rimim/SHADOW` -- survey only

**Community**
- CuriousMarc MarcDuino command reference and the original JEDI JawaLite page
  (**note the 0x13 terminator error**, 10.1)
- Printed Droid knowledge base -- slip ring, ShadowMD, MarcDuino V3
