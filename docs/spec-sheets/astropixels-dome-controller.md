# AstroPixels Spec Sheet (Dome Controller, registry token `protor2link`)

Working spec for **AstroPixels** -- the addressable-LED dome board set by Darren
Poulson -- as the `supported` **Dome Controller** lineup member
([#386](https://github.com/mattiasbrandt/protoArtoo/issues/386), minted from
[#303](https://github.com/mattiasbrandt/protoArtoo/issues/303) and
[#316](https://github.com/mattiasbrandt/protoArtoo/issues/316)), reached over the
Component Protocol the registry calls `protor2link`, and registered at
`include/component_registry.inc:139` as part id **13**.

Research date 2026-09-12. Every fact below was read this session from one of
five places: the Reeltwo library at the exact version the dome builds against,
the operator's own AstroPixelsPlus fork on this disk, this repository's own
source, tests and ADRs, the vendor's pages, or the astromech projects on this
machine. Claims that could not be sourced are marked `UNKNOWN` with the artefact
or bench test that would settle them.

> [!IMPORTANT]
> **AstroPixels is the product. AstroPixelsPlus is firmware that runs on it.**
> They are not two boards and not two vendors. AstroPixels is a set of
> WS2812B-based logic, PSI and holo PCBs plus an ESP32 carrier, sold by
> we-make-things.co.uk. AstroPixelsPlus is one of the firmware images you can
> put on that carrier -- written by Mimir "rimim" Reynisson, built on the
> Reeltwo library. protoArtoo talks to **the AstroPixelsPlus firmware running on
> the AstroPixels product**, and specifically to the operator's fork of it
> ([`mattiasbrandt/AstroPixelsPlus`](https://github.com/mattiasbrandt/AstroPixelsPlus),
> `AGENTS.md:16`), which is where **protoR2link** lives.
>
> This distinction is load-bearing for four sections below: the hardware facts
> (Section 2, 5) are true of any firmware; the protocol facts (Sections 7-10)
> are true only of AstroPixelsPlus, and several are true only of the fork.

> [!CAUTION]
> **An AstroPixels board out of the box does not speak the language protoArtoo
> sends.** The firmware Darren Poulson ships on every set is the stock
> `standard` build, whose serial grammar is `LE...` and `HP...`, not Marcduino
> -- *"With the latest standard firmware, serial2 is listening at 9600 baud for
> standard astropixel commands. If you want to use marcduinos, there is a custom
> firmware"* (vendor docs, Interfacing). There are three firmware families for
> this hardware and protoArtoo works with exactly one of them. Section 6 is the
> chooser, and getting it wrong looks like a dome that lights up beautifully and
> ignores the body completely.

## Where this sits in the lineup

The **Dome Controller** category holds two products, and a builder picks one:

| Product | Lighting technology | Status | Registry id |
| --- | --- | --- | --- |
| **AstroPixels** | WS2812B addressable RGB | `supported` | 13 |
| Teeces | MAX7219-driven discrete LEDs | `roadmap` | 14 |

**AstroPixels is the one protoArtoo drives today because it is the kit the
operator installed** (#303, 2026-09-07). That is a fact about this project's
history, not a verdict on the products. Both are current and both are bought new
today. Where they differ, Section 14 says how -- and
[`teeces-dome-lighting.md`](teeces-dome-lighting.md) says it from the other side.

> [!NOTE]
> **The registry row names the firmware, not the product.**
> `include/component_registry.inc:139` calls part 13 **"AstroPixels Plus"**, and
> that is the string a builder reads on the Component Picker card. What they
> bought is an **AstroPixels** set; AstroPixelsPlus is firmware they then chose
> to flash onto it. Recorded here as drift rather than corrected: the row's
> `id` token `astropixels_plus` is persisted in NVS and must never be
> renumbered or renamed, and changing the operator-visible `name` is an operator
> decision, not a research one. The same substitution runs through
> `CONTEXT.md:706`, `docs/topology.md:37,64`, `docs/goal.md:41` and
> `docs/terminology.md:139`, all of which say "AstroPixelsPlus-class board"
> where they mean an AstroPixels board running AstroPixelsPlus.

## 0. Authority Contract

This document is an implementation authority for the **AstroPixels hardware**,
for the **protoR2link** body/dome contract, and for the command grammar the
**AstroPixelsPlus** firmware accepts.

Authority order for agent decisions:

1. Firmware source -- the operator's fork, the vendor's own sketches, and the
   Reeltwo library at the version the dome builds against -- and the vendor's
   documentation site, all listed in Sources.
2. This document.
3. Community build blogs and forum recollection.

If references conflict:

- Prefer source over documentation, **including the dome repository's own
  documentation.** Three documented values in this subject are contradicted by
  the code that ships beside them; each is named in place below (Sections 7.2,
  8.2, 8.4).
- Prefer the **vendor's** documentation for hardware and for the stock firmware;
  prefer the **fork's source** for anything protoR2link touches.
- If still unresolved, mark the value `UNKNOWN` and stop implementation changes
  that depend on it.

Agent requirements when using this document:

- MUST treat Sections 7 and 8 as normative and Sections 13 and 14 as survey.
- MUST NOT invent command letters, address numbers, effect numbers or timing
  constants not present here.
- MUST NOT assume a command that works on an AstroPixels dome running
  AstroPixelsPlus works on the same hardware running the vendor's stock
  firmware. Section 6.3 is the difference list and it is not short.
- MUST NOT confuse the three colour number spaces this one board carries
  (Section 8.7). They genuinely disagree.
- MUST label anything about measured current as unmeasured on our own droid; the
  vendor states a figure, we have never checked it.

## 1. Scope

Covers the AstroPixels board set, its electrical and mounting behaviour, the
three firmware families available for it, the Marcduino/JawaLite grammar the
AstroPixelsPlus firmware accepts, the structured visual layer this project added
on top, and the **protoR2link** link contract in both directions.

Does not cover: the PCA9685 expander itself (that is
[`pca9685-servo-expander.md`](pca9685-servo-expander.md)), the dome
rotation motor or its ESC (that is
[`isdt-esc70-dome-esc.md`](isdt-esc70-dome-esc.md) -- a different category
entirely), body audio (the Sound family sheets), or the Teeces board set.

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

`platformio.ini` in both the vendor repo and the fork declares `board =
esp32dev` -- a plain dual-core ESP32-WROOM class part, not an S3, C3 or P4.

> [!WARNING]
> **The Arduino ESP32 core must be 2.0.x.** The vendor's setup page: *"There is
> currently a bug in the ReelTwo library which makes it incompatible with the
> latest v3 ESP32 board libraries."* Select *"the latest 2.0.\* version"*. The
> fork obeys this by pinning `platform-espressif32` to `v5.2.0`, which carries a
> 2.0.x core. A v3 core is not a faster path; it is a build that does not run.

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
> a current kit ships -- the firmware the vendor and the fork both instantiate is
> `AstroPixelFrontPSI` / `AstroPixelRearPSI`, which alias the 25-LED
> `AstroPixelPSIPCB`. Treat the 8x8 as a variant that exists, not as an
> alternative you can assume. Which kits shipped with it is `UNKNOWN`; settled
> by asking the vendor or counting the LEDs on the board in hand.

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
| Stock, 2026-09-12 | **"Out of stock"** | same, read this session |
| Quantity limit | *"limited to one per customer"* | same |
| Resale | *"not for resale outside of the club"* | same |
| UK dispatch | *"usually within a week"* | same |
| International | *"please allow up to four weeks for delivery"* | same |
| Charity | GBP 10 per set to the Droidbuilders UK charity | vendor FAQ, Purchasing |

> [!IMPORTANT]
> **This is a club-economics product, not a commercial one, and that shapes what
> you can rely on.** The vendor states outright: *"I'm doing this for the love of
> the club ... I don't aim to make any money from this."* Consequences a spec
> sheet has to record rather than admire: it goes out of stock (it is, today);
> you cannot buy individual boards (*"I just sell the kits as is"*); there is no
> scale variant; and there is no second supplier, because the design files are
> not published -- *"Are the files available for me to make my own? At the
> moment, no."* A build that depends on AstroPixels depends on one person's
> spare time. Section 14 is the mitigation.

### 2.6 It does not fit a Home Depot R2

Asked and answered twice on the vendor's FAQ: *"the Home Depot R2 is not full
size. These lights are designed to fit into a full sized replica."* And, on
returns: *"If you've bought them after all these warnings, then it is on you."*

### 2.7 What the kit does **not** give you

Panel and holoprojector **motion** is not part of AstroPixels. The kit lights
the dome; it does not move it. Driving servos needs two PCA9685 expanders the
builder buys separately, and the firmware that knows what to do with them:

| Board | I2C address | Drives | Channels used |
| --- | --- | --- | --: |
| Panel controller | `0x40` | ring panels P1-P4, P7, P11, P13 and pie panels PP1, PP2, PP4, PP6 | 11 of 16 |
| Holo controller | `0x41` (A0 bridged) | FHP, RHP, THP -- two axes each | 6 of 16 |

Source: `AstroPixelsPlus/docs/HARDWARE_WIRING.md` section 1, and the
`servoSettings[]` table at `AstroPixelsPlus.ino:419-453`. Deeper detail on the
expander itself is [`pca9685-servo-expander.md`](pca9685-servo-expander.md).

## 3. Project Integration

Body-side facts are linked to the project's canonical references rather than
restated:

- **[`include/config.h`](../../include/config.h)** -- `UART_PORT_DOME`,
  `PIN_DOME_TX` / `PIN_DOME_RX` per Board Variant, and `DOME_LINK_TASK_STACK_BYTES`.
- **[`docs/pin_map.md`](../pin_map.md)** -- the dome link lane (`S3` on artoo-esp32).
- **[`docs/commands.md`](../commands.md)** -- the command surface inventory and
  the dome RX families.
- **[`docs/topology.md`](../topology.md)** -- the body/dome ownership split.
- **[`docs/dome-visual-authoring-contract.md`](../dome-visual-authoring-contract.md)**
  -- the `DL:` / `DH:` / `DT:` structured layer.
- **[`docs/dome-visual-presets.md`](../dome-visual-presets.md)** -- the `DV:`
  named-preset contract and the parity table.
- **[ADR 0003](../adr/0003-dome-link-uart-primary-wifi-fallback.md)** -- UART
  primary, WiFi fallback, and why.
- **[ADR 0005](../adr/0005-protor2link-arbiter-functional-core.md)** -- the
  transport arbiter as a functional core.
- **[ADR 0045](../adr/0045-the-body-models-dome-lights-and-keeps-forwarding-the-raw-families.md)**
  -- why the raw `@` / `*` / `%` families keep forwarding.
- **[ADR 0055](../adr/0055-the-body-routes-a-command-by-what-it-owns-not-by-its-prefix.md)**
  -- routing by ownership, and the 101-token measurement in Section 12.3.

Implementation, as it actually is on disk today:

| Concern | File |
| --- | --- |
| Transport, heartbeat, layout cache, UART/WiFi arbitration | `src/tasks/dome_link.cpp` (948 lines) |
| Transport decision as a pure function | `src/dome_link_arbiter.cpp`, `include/dome_link_arbiter.h` |
| Marcduino parse and dispatch of dome-originated lines | `src/drivers/dome_rx_parser.cpp` |
| `BD:<CUE>` named body cues | `src/drivers/dome_cue_handler.cpp` |
| `DV:` / `DL:` / `DT:` / `DH:` validation before send | `src/protocol_check.cpp`, mirrored in `data/seq_protocol_check.js` |
| Command string constants | `include/marcduino.h` |
| Native coverage | `test_dome_link_arbiter`, `test_dome_link_encoding`, `test_marcduino_helpers`, `test_protocol_check{,_dl,_dt,_dh}` |

> [!NOTE]
> #386's own table names only `src/drivers/dome_rx_parser.cpp` and
> `src/drivers/dome_cue_handler.cpp` as the implementation. That is the
> *ingress parser*, about 300 lines of a subsystem whose transport, arbiter and
> validator are three other files and 1,300 further lines. Recorded as drift in
> the ticket description, not a defect in the code.

## 4. Sources Checked

| Source | URL or path | What it settled |
| --- | --- | --- |
| **AstroPixels documentation site** | https://r2djp.gitbook.io/astropixels | **The authoritative product document.** Kit contents, power, pin map, ESP32 choice, mounting, the native `LE`/`HP` grammar, firmware families, FAQ |
| Full-text export of the same | `https://r2djp.gitbook.io/astropixels/llms-full.txt` | Read in full this session (33 KB, 20 pages) rather than page by page |
| Vendor store listing | https://we-make-things.co.uk/product/astropixels/ | Price, stock state, kit contents, per-customer limit, shipping |
| **Vendor firmware** | https://github.com/dpoulson/Astropixels | **The stock image.** Five build environments; `src/standard/main.cpp` is what ships on every board; `src/standard-md/` is the JawaLite build. **No LICENSE file** |
| Vendor's Reeltwo fork | https://github.com/dpoulson/Reeltwo | The library the vendor's own instructions tell you to install, *"a fork of the original repository due to a long standing bug that meant it would not compile"* |
| **Reeltwo library @ 23.5.3** | https://github.com/reeltwo/Reeltwo, cloned at the tag the dome pins | LED geometry, the `LogicEngineRenderer` effect and colour enums, the sequence packing formula, `HoloLights` defaults. LGPL-2.1 |
| **The operator's dome firmware** | `~/Documents/GitHub/AstroPixelsPlus` (`mattiasbrandt/AstroPixelsPlus`) | **The authority for protoR2link.** 314 `MARCDUINO_ACTION`s, the body-link handler, the panel map, the `DV:`/`DL:`/`DT:`/`DH:` implementation. LGPL-2.1 |
| Upstream AstroPixelsPlus | https://github.com/reeltwo/AstroPixelsPlus | The base the fork diverges from; its README carries the `#AP*` config commands and the *"prefix @ is optional and is ignored"* rule |
| AstroPixels web installer | https://dpoulson.github.io/Astropixels/ | How a builder actually changes firmware family; Chrome or Edge only |
| This repository | `src/`, `include/`, `docs/`, `test/` | Everything on the body side |
| `docs/HARDWARE_WIRING.md` in the fork | `~/Documents/GitHub/AstroPixelsPlus/docs/` | The `G V R T` header, the slip-ring practice, the PCA9685 addressing. **Stale on baud -- see 7.2** |

## 5. Electrical

### 5.1 Supply

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
> **Take the buck-converter advice, and note why.** *"Doing it this way reduces
> voltage losses and current through the slip ring."* A slip ring's channels are
> the scarce resource in a droid dome, and 700 mA at 5 V through ring contacts
> is both a bigger current and a worse voltage drop than the same power at pack
> voltage. This also keeps the dome's 5 V rail independent of the body's, which
> matters for the UART -- see 5.4.

**Measured current on our own droid is `UNKNOWN`.** The vendor's ~700 mA is the
only figure in evidence and it is a typical-use estimate, not a measurement of
this set at this brightness. Settled by: an inline meter on the dome 5 V feed at
the configured `BRI` values, with the logics idling and again during `DV:SCREAM`.

### 5.2 Brightness is the current knob, and it is a firmware constant

The `BRI` field of a `LogicEngineSettings` is 0-255 and the vendor's defaults are
front **160**, rear **140** (vendor Changing Colors page, Defaults table). The
vendor's own warning: *"Beware of both heat and power consumption if you raise it
too high."*

This matters for protoArtoo in one specific way: **nothing protoArtoo sends can
change it.** Brightness is not a field in `DL:`, in `@nT`, or in the sequence
packing formula -- the four packed fields are effect, colour, speed scale and
duration (5.3). A builder who wants a dimmer dome reflashes the dome; the body
cannot ask.

### 5.3 The sequence packing formula

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

That is the whole expressive budget of a logic command: **effect, colour, speed
scale, duration in seconds.** Every higher-level grammar in this document --
`LE`, `@nT`, `DL:` -- is a different spelling of those four numbers.

### 5.4 The data pins, from the vendor's own table

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

Vendor Overview page. The fork's `AstroPixelsPlus.ino:232-260` declares exactly
these values, so the vendor's table and the firmware agree -- which is worth
saying, because they do not agree about baud (7.2).

The five AUX pins are the expansion budget, and they are contended. In the fork
they are claimed by, in build-flag order: the DFPlayer sound serial (AUX4/AUX5),
the CBI MAX7221 chain (AUX1/AUX2/AUX3), FireStrip (AUX4) and BadMotivator
(AUX5). **In the image protoArtoo talks to, all four of those are compiled out**
(`platformio.ini`: `AP_ENABLE_CBI=0`, `AP_ENABLE_DATAPANEL=0`,
`AP_ENABLE_FIRESTRIP=0`, `AP_ENABLE_BADMOTIVATOR=0`), so the AUX pins are free.
That is a deliberate consequence of the body owning sound (Section 12.2).

### 5.5 The I2C header

*"The i2c header has +ve, Gnd, Data, and clock connections. Only the data, clock,
and gnd are needed. The default address is 0x0A."* (vendor, Interfacing). SDA is
GPIO 21 and SCL GPIO 22 (`AstroPixelsPlus.ino:232-233`).

Two different things use this bus and they must not be confused:

- **`0x0A`** -- the AstroPixels board acting as an I2C **slave**, so another
  controller can command it. This is how the stock firmware was originally
  driven. protoArtoo does not use it.
- **`0x40` / `0x41`** -- the two PCA9685 expanders, on the same two pins, with
  the AstroPixels board as **master**. This is what the fork uses.

`AstroPixelsPlus.ino` makes them mutually exclusive by construction:
`#define USE_I2C_ADDRESS 0x0a` is commented out with the note *"Define
USE_I2C_ADDRESS to enable slave mode. This will disable servo support"*, and the
`servoSettings[]` table is `#ifndef USE_I2C_ADDRESS`. **A dome cannot be an I2C
slave and drive panels.**

### 5.6 Failure modes the vendor names

From the Troubleshooting page, worth carrying because each maps to a distinct
fault:

| Symptom | Vendor's diagnosis |
| --- | --- |
| Wrong colours, or not all boards lit | *"you have the wrong board connected to the wrong header"* |
| One board dark | Cable reversed -- S to S. Try the board on the RLD header to prove the board rather than the pin: *"it may be a dead pin on the ESP32"* |
| Nothing lit at all | Wiring and power. *"There should at least be a red light on the ESP32"* |
| Lit up to a point, then dark | A dead pixel. *"If a single pixel goes, then 99% of the time nothing after it will work either"* -- WS2812B chains are serial, so the first failure truncates everything downstream. Vendor replaces the board |

## 6. The three firmware families, and why the choice is load-bearing

### 6.1 What ships on the board

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

### 6.2 The chooser

| Family | Where from | Serial grammar | Baud | Panels | protoArtoo works? |
| --- | --- | --- | --: | --- | --- |
| **`standard`** (shipped) | vendor repo / [web installer](https://dpoulson.github.io/Astropixels/) | `LE` / `HP` ReelTwo commands, plus I2C slave `0x0A` | 9600 | none | **No** |
| `imperial`, `r2kt`, `special` | same | as `standard`, different colour defaults | 9600 | none | **No** |
| **`standard-md`** | same | JawaLite / Marcduino, 117 actions, plus `*RT` and `@AP` raw escapes | 9600 | 5, direct ESP32 pins | Partly -- see 6.4 |
| **AstroPixelsPlus (upstream)** | `reeltwo/AstroPixelsPlus` | Marcduino, plus `#AP*` config, `~RT` / `@AP` escapes, WiFi and a web UI | **2400** default | 13 + 6 over two PCA9685s | Partly -- no protoR2link |
| **AstroPixelsPlus (operator's fork)** | `mattiasbrandt/AstroPixelsPlus` | as upstream, **plus protoR2link, `DV:`/`DL:`/`DT:`/`DH:`, panel calibration, `/api/dome/layout`** | 9600 by its own setup profile | 13 + 6 | **Yes. This is the one.** |

The vendor's own framing of the third-party option, verbatim: *"The AstroPixels
Plus firmware is a third party one created by Mimir who also did the main ReelTwo
library that the Astropixels are based on. ... This is with a thirdparty firmware
that I haven't tested or know much about."*

### 6.3 The stock grammar, for completeness

Worth recording, because a builder debugging a silent dome will want to know what
the board answers to *before* they reflash it, and because `@AP` / `~RT` / `*RT`
let you reach this grammar from inside a Marcduino build.

`LE<designation><effect><colour><speed><time>`, leading zeros dropped:

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
Swipe, `24` Pulse, `99` Random (vendor, Interfacing). Colour is `ColorVal`
(Section 8.7). Speed is 0-9 with 0 fastest, and the vendor documents its units
per effect: *"Flip Flop and Rainbow - 200ms x speed; Flash - 250ms x speed; March
- 150ms x speed; Color Swap - 350ms x speed"*. Time is two digits of seconds,
`00` continuous.

The vendor's own worked examples: *"you could send via i2c the command 'LE30000'
to set all logics to a pale green for the duration of the Leia message, or
'LE1201010' to make the front logics run a cylon style effect."*

### 6.4 The vendor's Marcduino build is not a drop-in for us

`standard-md` exists and is the vendor's answer to *"act more like a drop in
replacement for existing teecees lights"*. Its wiring note is the tersest good
instruction in this whole subject: *"connect from the slave port on the Marcduino
to the Serial2 port on the Astropixels. You just need to connect - to G, and S to
R. No need for the +ve/power."*

But it is a far smaller firmware than the fork and cannot serve protoArtoo:

| | `standard-md` | operator's fork |
| --- | --: | --: |
| `MARCDUINO_ACTION` registrations | 117 | **314** |
| Panel servo channels | 5, on direct ESP32 pins | 13 panel + 6 holo, over two PCA9685s |
| Panel calibration persisted | no | yes, NVS, per panel |
| protoR2link | no | yes |
| Web UI / REST API | no | yes |
| `DV:` / `DL:` / `DT:` / `DH:` | no | yes |

`standard-md` also carries `%`-routing advice that explains a prefix protoArtoo
still forwards: *"You may need to edit the commands sent out to add % to the
beginning of them. This sends the command out of the slave port on the marcduino
(dropping the %) and into the Astropixels."*

### 6.5 Provenance and licence

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
> along with adding a few extra functions."* The operator's AstroPixelsPlus fork
> instead pins **`reeltwo/Reeltwo#23.5.3`** (`platformio.ini`). Both build. Which
> extra functions the vendor's fork adds, and whether any of them matter to us,
> is `UNKNOWN`; settled by diffing the two libraries.

## 7. protoR2link -- the link contract

### 7.1 Physical

| Property | Dome side | Body side |
| --- | --- | --- |
| Port | `Serial2`, RX GPIO 16 / TX GPIO 17 | `UART_PORT_DOME` (Serial2) |
| Pins | fixed by the Main Board's `G V R T` header | per Board Variant: artoo-esp32 GPIO 33 TX / 34 RX (`config.h:164-165`), firebeetle2 GPIO 22 TX / 23 RX (`config.h:286-287`) |
| Framing | `SERIAL_8N1` | `SERIAL_8N1` (`dome_link.cpp:170`) |
| Baud | NVS `mserial2`, 2400 or 9600 | **9600, hardcoded** (`dome_link.cpp:170`) |
| Conductors | 3 through the slip ring: TX, RX, GND | same |

The dome header is silkscreened `G V R T`. **`V` is left unconnected for a body
link** -- the fork's wiring guide is explicit that no power crosses the slip
ring, *"slip-ring channels are a finite resource"*, and separate supplies
back-feeding each other is the hazard. TX and RX cross: dome `T` to body RX, dome
`R` to body TX. Common ground is mandatory.

### 7.2 The baud is a persisted dome-side setting, and two documents are wrong about it

This is the single most likely reason a correctly wired link stays silent.

| Source | Says | Verdict |
| --- | --- | --- |
| `AstroPixelsPlus.ino:197` | `#define MARC_SERIAL2_BAUD_RATE 2400` | **The firmware default.** True |
| `AstroPixelsPlus.ino:1202` | `COMMAND_SERIAL.begin(preferences.getInt(PREFERENCE_MARCSERIAL2, MARC_SERIAL2_BAUD_RATE), ...)` | **The actual value is NVS key `mserial2`**, defaulting to 2400 |
| Fork `data/setup.html:284-288` | *"Defaults match the protoArtoo body/dome link profile"*, `mserial2: '9600'` | **The shipping profile.** True |
| Fork `docs/HARDWARE_WIRING.md:470-479` | *"Serial2 runs at **2400 baud** ... Both dome and body must use 2400 baud on this link"* | **Wrong**, and it quotes a constant `COMMAND_BAUD_RATE` that does not exist in the source |
| Fork `docs/SETUP.md:926,1040` | *"Serial2 (2400 baud...)"*, *"Serial2 baud rate is **2400**"* | **Wrong** |
| Fork `docs/COMMANDS.md:51` | *"Serial2 -- TTL header at 2400 baud"* | **Wrong** |
| upstream README | *"running at 2400 baud"* on the Serial2 header | True of upstream's default |
| protoArtoo `src/tasks/dome_link.cpp:170` | `s_domeSerial.begin(9600, SERIAL_8N1, ...)` | **9600, not configurable** |
| **Vendor, both stock builds** | `#define MD_BAUD 9600` | **9600 is the vendor's own baud** |

> [!CAUTION]
> **9600 is correct, and it is stored in the dome's NVS rather than compiled in.**
> A dome flashed from scratch with no `mserial2` key comes up at **2400** and
> will not hear protoArtoo, which cannot be reconfigured. Set `mserial2` to 9600
> on the dome's Setup page before or after any `#APZERO` factory reset. Four
> documents in the dome repository still say 2400; they are stale and are named
> above so nobody re-derives the wrong answer from them.
>
> The 2400 default is not an error upstream: AstroPixelsPlus is designed to sit
> where a JEDI display sits, and the MarcDuino slave drives its JEDI port at 2400
> by default. It is simply the wrong side of the droid for us.

### 7.3 Transport arbitration

Two transports, UART primary (ADR 0003):

| Constant | Value | File |
| --- | --: | --- |
| Heartbeat interval | 1000 ms | `include/dome_link_arbiter.h:28` |
| Heartbeat timeout | 5000 ms | `:29` |
| UART re-probe interval | 30000 ms | `:30` |
| UART probe window | 150 ms | `:31` |

The body sends `#PAHB\r` at 1 Hz; the dome answers `#APHB\r` at 1 Hz. When the
UART heartbeat goes stale the body falls back to WiFi -- UDP on port 4901 for
heartbeats, HTTP `POST /api/cmd` for commands, with a 250 ms connect and 250 ms
read timeout (`dome_link.cpp:294-304`). Every 30 s it re-probes UART for 150 ms
and promotes back on success.

ADR 0003's reasoning, verbatim: the slip ring *"is a dedicated, bounded-latency
link that is always present on a fully assembled droid"*, while WiFi *"shares
bandwidth with other traffic, is subject to interference, and introduces
non-deterministic latency"*. WiFi exists so a dome on the bench, or a droid
mid-assembly with the ring connector off, still works.

### 7.4 What crosses, in each direction

**Body to dome** (`domeQueueTx()`, the sole writer; `include/dome_link.h:78-84`):

| Line | Meaning | Emitted by |
| --- | --- | --- |
| `#PAHB` | body heartbeat, 1 Hz | `dome_link.cpp` |
| `#PASL` / `#PAWU` | body entered / left Sleep Mode | `dome_link_arbiter.cpp:78-80`, polled on every fresh dome connection so a reconnect cannot inherit a stale state |
| `@...`, `*...`, `%...` | raw logics / holo / slave-out families, uninterpreted | sequence steps, RC actions, `POST /api/manual-command` |
| `:SE##`, `:OP##`, `:CL##`, `:OF##` | sequences and panels the dome owns | sequence dispatcher, RC actions |
| `DV:<NAME>` | named visual preset | sequence steps |
| `DL:` / `DH:` / `DT:` | structured visual authoring | sequence steps, validated first |
| `@0T1` + `@0P1` | the standard visual cleanup pair at sequence end | `sequence_dispatcher.cpp:323-324,371-372` |

**Dome to body** (`dome_link.cpp:340-399`, then `dome_rx_parser.cpp`):

| Line | Body's response |
| --- | --- |
| `#APHB` | intercepted as heartbeat, never parsed |
| `#APSL` / `#APWU` | `commandedSetSleep()` + immediate status broadcast |
| `dome=seqon,<seconds>` | set `domeSeqActive` with a timeout, and `$O` to suppress random audio |
| `dome=seqoff` | clear it, and `$R` to resume random audio |
| `dome=rot,<pct>,<ms>` | clamp to +/-100 and queue a timed dome rotation on `domeCmdQueue` |
| `BD:<CUE>` | play a named body cue -- 11 recognised, unknown ones logged and dropped |
| `:SE10/11/13/14` | mood alias |
| `:SE01-09, :SE15, :SE16` | decomposed into body audio and body sequence |
| `:SE30-:SE36` | direct body sequence |
| `:OPnn` / `:CLnn` / `:MVnnxxxx` | body arm or utility-arm motion -- **see the collision in 8.6** |
| `$...` | body audio |
| `@`, `*`, `%`, `&`, `!` | explicitly not body-handled; logged and dropped |

The 11 `BD:` cues, from `dome_cue_handler.cpp`: `SCREAM`, `HAPPY`, `OVERLOAD`,
`ALARM`, `VADER`, `ROCKMARCH`, `LEIA`, `CANTINA`, `HEART`, `HELLO`, `RESET`.

> [!NOTE]
> **`dome=rot` is a dome asking the body to turn the dome**, and it is the one
> line in this table that reaches a motor. It is gated on the Dome ESC being
> staged active (ADR 0027), clamped to +/-100 %, and dropped if the queue is
> full. It is implemented and tested, and it was **missing from
> `docs/commands.md`'s Dome RX list** until this sheet was written; that list is
> corrected in the same commit.

### 7.5 Framing hazards, and how each side avoids them

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
nothing. Two things keep this from biting us:

- protoArtoo appends `'\r'` and nothing else (`dome_link.cpp:921`).
- The fork does not use `MarcduinoSerial` for the body link at all. When
  `mbodylink` is enabled it clears `marcduinoSerial.setStream()` outright and
  reads Serial2 itself in `handleBodySerial()`, which accepts **either** `\r` or
  `\n` (`AstroPixelsPlus.ino:926`). That is a fork improvement, and it is also
  why hardware Marcduino over Serial2 is unavailable while the body link is on.

| Limit | Value | Source |
| --- | --- | --- |
| Body-link RX buffer (fork) | 65 bytes -- **64 data + NUL** | `AstroPixelsPlus.ino:921` |
| `MarcduinoSerial` buffer (upstream path) | 64 -- **63 usable** | `Marcduino.h:121` |
| Body TX queue entry | 64 bytes, `\r` added after | `include/dome_link.h:68-71` |
| Validator cap on generated commands | **63 chars** | `docs/dome-visual-authoring-contract.md`, `src/protocol_check.cpp` |
| Overflow behaviour, both sides | **silent discard** | `AstroPixelsPlus.ino:952-958`; `Marcduino.h:174` |

**63 characters is the real budget.** It is why `DT:` has a deliberately small
grammar and percent-encoding rather than being a general text transport.

### 7.6 What the dome does *not* send

**There is no ACK, no NAK, no error line and no status query on this link.** The
only unsolicited dome-to-body traffic is the heartbeat, the sleep-sync pair, the
`dome=` control lines and `BD:` cues -- all of which the dome originates for its
own reasons. A command the dome could not parse produces nothing at all.

Two consequences protoArtoo already lives with:

1. **Validation must happen on the body.** That is exactly why
   `src/protocol_check.cpp` and its browser mirror exist, and why the `DH:`
   effect/colour matrix is enforced before send rather than relying on the dome
   to refuse.
2. **Health is inferred, not reported.** `domeConnected()` is "a heartbeat
   arrived inside 5 s", nothing more. The richer picture -- `body_link.enabled`,
   `connected`, `transport`, `uart_hb_age_ms`, `wifi_hb_age_ms`, `hb_rx`,
   `peer_ip`, `peer_source` -- exists only on the dome's own `/api/health`, over
   HTTP, and the body does not poll it.

## 8. The command grammar

### 8.1 There is no parser. There is a prefix-match list.

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

Five consequences, each verified against the operator's fork this session:

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
> `@3P6` registration exists. Verified in the operator's fork at
> `AstroPixelsPlus.ino:743,746` this session. Do not emit `@1P60` or `@2P60`
> expecting a font change.

### 8.2 `@` -- logic displays

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
> A sequence that uses them will look right on the operator's droid and wrong on
> anyone else's.

> [!NOTE]
> **The wire number is not the renderer number**, and both appear in this
> subject. `@0T5` is REDALERT (11); `@0T11` is MARCH (4). The vendor's native
> `LE` grammar (6.3) uses the renderer numbers directly, so `LE011` and `@0T3`
> are the same effect spelled two ways. The fork's own `docs/COMMANDS.md:321-337`
> lists only seven of the eleven rows above and is out of date against its source.

Text, from `MarcduinoLogics.h:243-265`:

| Command | Target |
| --- | --- |
| `@1M<text>` | top half of the FLD |
| `@2M<text>` | bottom half of the FLD |
| `@3M<text>` | RLD |

`@1M` and `@2M` are **not independent**: both write into static buffers and then
re-render the whole front display as `"<top>\n<bottom>"`. Colour is
`FLD.randomColor()` -- which, because `randomColor()` is `ColorVal(random(10))`,
can return `kDefault`. There is no way to set the colour of `@nM` text; `DT:`
exists for exactly that reason (Section 9).

### 8.3 `@` -- PSI

`@0P<n>` both, `@1P<n>` front, `@2P<n>` rear, with the same seven effect numbers
as the `T` family: 1 NORMAL, 2 FLASHCOLOR, 3 ALARM, 4 FAILURE, 5 REDALERT,
6 LEIA, 11 MARCH (`MarcduinoPSI.h`).

**Address numbers mean different devices depending on the verb.** `@1T` is the
FLD and `@1P` is the front PSI; `@2T` is the RLD and `@2P` is the rear PSI. That
is not a transcription error here -- it is `MarcduinoLogics.h:3` against
`MarcduinoPSI.h:3`.

### 8.4 `*` -- holoprojectors

The `*` family is a set of thin wrappers that emit ReelTwo `HP...`
`CommandEvent` strings. Example, `MarcduinoHolo.h`:

```cpp
MARCDUINO_ACTION(FrontHoloPosDown, *HP001, ({
    CommandEvent::process(F("HPF1010"));
}))
```

The underlying grammar is `HP<designator><type><function><colour><...>` with an
optional `|<seconds>` runtime suffix:

| Field | Values |
| --- | --- |
| designator | `F` front, `R` rear, `T` top, `D` radar eye, `O` other, `A` all three, `X` front+rear, `Y` front+top, `Z` rear+top, `S` sequences |
| type | `0` LED functions, `1` servo functions |
| LED function | `01` Leia (forced blue), `02` colour projector, `03` dim pulse, `04` cycle, `05` solid colour, `06` rainbow, `07` short circuit, `96`-`99` twitch/override control, `00` off |
| servo function | `01` go to preset position, `04` random position, `05` wag L/R, `06` wag U/D, `98`/`99` disable/enable auto twitch |
| position | `0` down, `1` centre, `2` up, `3` left, `4` upper left, `5` lower left, `6` right, `7` upper right, `8` lower right |
| runtime | `|<seconds>`, after which the holo returns to its last auto-twitch mode |

So `HPF0040` is front / LED / cycle / random colour, and `HPF1010` is front /
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
> Verified in the operator's fork this session: `@7T1` emits `HPT0040` and `@8T1`
> emits `HPR0040`. A body controller following the JawaLite table drives the
> wrong holoprojector. protoArtoo's own `DH:` layer sidesteps this by using
> letter targets (`F`/`R`/`T`/`A`) rather than numbers -- which is a good reason
> to prefer `DH:` over raw `*` and `@n` holo commands.

Servo-driven holo motion needs the second PCA9685 and six channels; the LED
functions work on a bare kit.

### 8.5 `$` -- sound, which the body owns

The fork's shipped image compiles sound out: `platformio.ini` sets
`-DMARC_SOUND_LOCAL_ENABLED=false`, and `AstroPixelsPlus.ino:196` defaults
`MARC_SOUND_PLAYER` to `MarcSound::kDisabled`. **The dome does not make noise.**

This is deliberate and is the clearest divergence from the rest of the hobby,
where the sound module is usually a dome-side MarcDuino responsibility
(`docs/topology.md:57`). Here the dome *requests* audio and the body plays it --
either by forwarding a `$...` line the body's `AudioTask` consumes, or by the
higher-level `BD:<CUE>` vocabulary the fork invented for the purpose.

### 8.6 `:` -- panels, and a number-space collision

`:OPnn` opens, `:CLnn` closes, `:OFnn` flutters, and there are pie-panel spellings
(`:OPP1`-`:OPP6`), group shortcuts and thirteen dynamic group choreographies
(`:OW$`, `:OMA$`, `:OD$`, ...). The full panel map is Section 11.

> [!CAUTION]
> **`:OP01` means two different things depending on which way it is travelling.**
>
> | Direction | `:OP01` means |
> | --- | --- |
> | body to dome | open dome ring panel **P1** (`AstroPixelsPlus.ino:430`) |
> | dome to body | open body utility arm **ARM1** (`marcduino_helpers.h:31-49`) |
>
> The body maps panel numbers 1-5 to `armId` 0-4 and treats `:OP00` / `:OP99` as
> a broadcast to both arms, while the dome treats `:OP00` as "all dome panels".
> This is ADR 0055's number-space collapse in its sharpest form: *"The dialect
> assumes two boards. protoArtoo is one. Both boards use the same numbers for
> different hardware, so when one controller answers for both, the two number
> spaces collapse into each other."*
>
> It is not a latent bug today, because direction disambiguates and the two
> parsers are separate. It **is** a trap for anyone adding a loopback, an echo,
> or a shared command log, and it is why ADR 0055 routes by ownership rather
> than by prefix.

### 8.7 Three colour number spaces on one board

This is the most reliable way to get a wrong-coloured dome, and all three are
primary-sourced.

| Code | `ColorVal` (logics and PSI) | `HP` (holoprojectors) | `DL:` token | `DH:` token |
| --: | --- | --- | --- | --- |
| 0 | kDefault | Random | `DEFAULT` | `DEFAULT` |
| 1 | kRed | Red | `RED` | `RED` |
| 2 | kOrange | **Yellow** | `ORANGE` | `ORANGE` |
| 3 | kYellow | **Green** | `YELLOW` | `GREEN` |
| 4 | kGreen | **Cyan** | `GREEN` | -- |
| 5 | kCyan | **Blue** | -- | `BLUE` |
| 6 | kBlue | **Magenta** | `BLUE` | `PURPLE` |
| 7 | kPurple | **Orange** | `PURPLE` | `YELLOW` |
| 8 | kMagenta | Purple | -- | -- |
| 9 | kPink | **White** | -- | `WHITE` |

Sources: `Reeltwo src/dome/LogicEngine.h:143-155`; the vendor's HoloProjectors
page and `Reeltwo src/dome/HoloLights.h`; the fork's `parseVisualLogicColor` and
`parseVisualHoloColor` in `DomeSequences.h:392-401,585-597`.

> [!WARNING]
> **`ColorVal` has no white, so `DL:...:WHITE` is not white.** The fork maps it to
> `kDefault` and says so in its own source comment:
>
> ```cpp
> if (strcmp(token, "WHITE") == 0)  { out = LogicEngineRenderer::kDefault; return true; } // ReelTwo logics have no white ColorVal.
> ```
>
> Holos *do* have white (index 9), so `DH:A:FLASH:WHITE` is white while
> `DL:LOGIC:FLASHCOLOR:WHITE` is the palette default. Worse for a debugging
> operator: the authoring contract specifies that telemetry reports the
> **requested** colour, so `/api/health` will say `"color": "WHITE"` for a
> display that is not showing white. There is no defect to fix in our code --
> the hardware palette does not contain the colour -- but the asymmetry must not
> be discovered on a bench at midnight.

`randomColor()` is `ColorVal(random(10))`, so it can return `kDefault`.
**Brightness and palette are not reachable over serial at all** (Section 5.2).

### 8.8 `#` -- configuration

Handled by the dome, never by the body. From the upstream README and the fork:

| Command | Effect |
| --- | --- |
| `#APWIFI` / `#APWIFI0` / `#APWIFI1` | toggle / off / on |
| `#APREMOTE` / `#APREMOTE0` / `#APREMOTE1` | droid remote support (compiled out in the shipped image) |
| `#APZERO` | clear all preferences **including WiFi and `mserial2`** -- see the caution in 7.2 |
| `#APRESTART` | restart |
| `#SO`, `#SC`, `#SW` | fork-only panel calibration: save open, save closed, swap |

`#PAHB`, `#PASL`, `#PAWU`, `#APHB`, `#APSL` and `#APWU` also live on this prefix
and are protoR2link's, not configuration.

### 8.9 Escapes to the native layer

Three registrations let a Marcduino-era sender reach the raw ReelTwo
`CommandEvent` grammar of Section 6.3 without reflashing:

| Escape | Where | Effect |
| --- | --- | --- |
| `@AP<cmd>` | fork and upstream | `CommandEvent::process(<cmd>)` |
| `~RT<cmd>` | upstream | same |
| `*RT<cmd>` | vendor `standard-md` | same |

These are the only way to set a logic colour and duration together from a
Marcduino-shaped link -- and they are also why `DL:` was worth building instead:
`@APLE1060015` is unreadable, unvalidated and 13 of your 63 characters.

### 8.10 Concurrency: one animation slot

`AnimationPlayer player(servoSequencer)` is a singleton and `animateOnce()` calls
`end()` before installing the new step. **Any matching command aborts whatever
multi-step sequence is running.** Single-step actions complete within one
`AnimatedEvent::process()` pass and chain safely; multi-step ones
(`MARCDUINO_ANIMATION` -- the `:SE05`-`:SE09` family and the long `DM:` dances)
run for tens of seconds and will be cut short by the next command that matches
anything.

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
display commands. **Pacing is the body's job.** What rate the fork's ingress
actually sustains is `UNKNOWN`; settled by streaming alternating `@0T1`/`@0T2` at
9600 and counting applied effects against sent commands.

## 9. The structured visual layer

### 9.1 Why it exists

The raw families of Section 8 cannot express what a sequence editor needs. A
logic command carries effect, colour, speed and duration (5.3), but `@0T5` only
carries the effect -- colour and duration are not in the wire form at all, and
`@nM` text takes a random colour. The escape hatches (`@AP`, `~RT`) can reach the
full grammar but are unvalidated and spend a third of the 63-character budget on
ceremony.

`docs/dome-visual-presets.md` records the moment this became concrete: a
body-owned `DM:ROCKMARCH` played music and drove panels, *"but the FLDs stayed
default blue, while dome-native ROCKMARCH renders a richer typed preset: red
MARCH logic/PSI/holo with duration/color semantics."* The conclusion was that
**the body cannot reproduce dome-native visual identity by approximating with raw
public commands**, and the fix is a typed intent command rather than a better
approximation.

### 9.2 The four families

| Family | Grammar | Owner |
| --- | --- | --- |
| `DV:<NAME>` | named visual preset, closed uppercase set | dome renders; body only names it |
| `DL:<target>:<mode>[:<color>[:<durationSec>]]` | logics and PSI | dome renders, body validates |
| `DH:<target>:<effect>[:<color>[:<durationOrCount>]]` | holoprojectors | dome renders, body validates |
| `DT:<target>:<color>:<durationSec>:<speed>:<encodedText>` | scrolling text | dome renders, body validates |

`DL:` targets: `FLD`, `RLD`, `LOGIC` (both), `FPSI`, `RPSI`, `PSI` (both), `ALL`.
`DH:` targets: `F`, `R`, `T`, `A` -- **letters, not the numbers of 8.4**, which
is how this layer avoids the `@7`/`@8` swap. `DT:` targets: `FLD`, `RLD`,
`LOGIC`.

Validation is mirrored byte-for-byte between `src/protocol_check.cpp` and
`data/seq_protocol_check.js`: uppercase only, full-string match, total length
`<= 63`, unknown enum rejected, unsupported target/effect/colour combination
rejected. The `DH:` effect/colour/duration matrix is strict -- `DH:A:RAINBOW:RED`
and `DH:A:WAG:RED:5` are refused **before send** rather than being left to the
dome, which would refuse them silently or not at all.

`DT:` uses percent-encoding rather than base64 (`%0A` newline, `%25` percent,
`%3A` colon), caps encoded text at 40 characters and decoded at 32, allows one
newline, and rejects carriage return and non-printable ASCII.

The dome side reports back on `/api/health` under `visual_authoring`, with
`last_cmd`, the decoded fields, `apply_count` and `reject_count` per family.
**That telemetry is the only feedback channel this protocol has, and it is over
HTTP, not the serial link** (7.6).

Hardware-verified 2026-06-22, one case per family, recorded in the contract:
`DL:LOGIC:MARCH:RED:5`, `DT:FLD:DEFAULT:5:0:TEST%0ATEXT`, `DH:A:FLASH:RED:5`,
`DV:RESET_VISUALS` -- all applied with `reject_count` 0.

### 9.3 What `DV:` actually renders

`DV:<NAME>` selects a dome-resident preset. Each is a small script of typed
calls, and reading one shows why the body could not have approximated it --
`DomeSequences.h:205-212`:

```cpp
static void domeApplyMarchVisuals()
{
    CommandEvent::process(F("HPA0021|47")); // all holos red flashes
    FLD.selectSequence(LogicEngineRenderer::MARCH, FLD.kRed, 0, 47);
    RLD.selectSequence(LogicEngineRenderer::MARCH, RLD.kRed, 0, 47);
    frontPSI.selectSequence(LogicEngineRenderer::MARCH, frontPSI.kDefault, 0, 47);
    rearPSI.selectSequence(LogicEngineRenderer::MARCH, rearPSI.kDefault, 0, 47);
}
```

Five devices, two colour spaces, one duration, in one body-side token. `DV:VADER`
and `DV:ROCKMARCH` share this renderer; `ALARM`, `LEIA`, `HEART`, `CANTINA`,
`SCREAM`, `OVERLOAD`, `HELLO` and `RESET` have their own.

The dome's own named sequence vocabulary is larger -- 40 `DM:` names, ported from
Jessica Janiuk's `thePunderWoman/AstroPixelsPlus` fork and adapted
(`DomeSequences.h:1-8`): `ALARM BEEPCANTINA BLOOM BYEBYE CANTINA DISCO FAINT
FLUTTER GIRLONFIRE HARLEMSHAKE HEART HELLO LEIA LOW MARCHINGANTS OCWAVE OPENALL
OPENWAVE OVERLOAD PIES RANDOM RESET ROCKMARCH RYTHMIC SCREAM SCREAMNOPANEL
SCREAMPANEL SECANTINA SELEIA SESCREAM SHORT SMIRKWAVE SMIRKWAVEPANEL STOP
TOPPANELS VADER WAVE WAVEPANEL WIGGLE YODA`.

### 9.4 The layer is not body-only, and an old dome makes it inert

`DL:`, `DH:` and `DT:` are **not** wrappers the body can satisfy alone -- the
body validates, serialises and forwards; the dome renders. On any firmware
without this build they match nothing and are dropped in silence (8.1b). Only
`DV:` is partly body-side, and only in the sense that the body picks the name
from a closed list; the preset itself lives in the dome.

**This is the sharpest form of the Section 6 warning.** A builder with the same
AstroPixels hardware and stock firmware gets nothing from Sections 9 and 10.

## 10. The dome's HTTP surface

The fork runs an async web server, and protoArtoo uses exactly two things from
it.

**`GET /api/dome/layout`** -- the dome's own report of the panel/holo/PSI/logic
elements it can drive, composed with live runtime state. protoArtoo caches it:
roughly 24 KB, allocated on first fetch, 500 ms connect and 2000 ms read timeout,
refresh throttled to once per 30 s, served out to browsers in small chunks by
`domeLayoutCacheReadChunk()` so no per-request buffer is needed
(`dome_link.cpp:655-770`, `include/dome_link.h`). This is the **Dome Layout View
Model**: the connected dome's statement of what is drivable now, as distinct from
the Droid Parts Catalog's statement of what exists (ADR 0045).

**`POST /api/cmd`** -- the WiFi-fallback command path, body
`cmd=<command>`, form-encoded (`dome_link.cpp:294-304`).

The rest of the dome's surface is operator-facing and protoArtoo does not consume
it: `/api/state`, `/api/health` (including `body_link` and `visual_authoring`),
`/api/sleep`, `/api/wake`, `/api/diag/i2c`, `/api/panels/config`,
`/api/holos/config`, `/api/servo/test`, `/api/servo/stop`,
`/api/dome/layout-template` and `/api/dome/element-status`.

> [!NOTE]
> **Two independent networks are in play and they are easy to conflate.** The
> dome's own AP defaults to SSID `AstroPixels` / passphrase `Astromech` at
> `http://192.168.4.1`; the fork prefers client mode on the operator's LAN, and
> discovers the body by mDNS at `protoartoo.local` with a manual `bodypeerip`
> override. The WiFi fallback path needs dome and body on the **same** network,
> which an AP-mode dome is not.

## 11. The panel map

What `:OPnn` actually moves, on a Mr Baddeley MK4 complex dome with the fork's
default wiring. Source: `AstroPixelsPlus.ino:398-453` and `WiringConfig.h`.

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
   dome's NVS, set from its web UI with `#SO` / `#SC` / `#SW`. The body never
   sends calibration.
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

> [!NOTE]
> **This table is what `docs/droid-parts.yaml`'s `dome_link_panel:` field is
> waiting for.** That file currently says *"TBD until verified against the fork's
> panel table on hardware -- do not guess."* The table above is read from the
> fork's source rather than guessed, which satisfies half the instruction; the
> hardware half is a bench check that each `:OPnn` moves the named panel on the
> operator's own droid. #386 carries the proposed values.

## 12. What protoArtoo already does, and what is left

### 12.1 Done, and hardware-verified

- **Bidirectional link on two transports**, UART primary with WiFi/UDP fallback,
  1 Hz heartbeats both ways, automatic promotion and demotion (ADR 0003/0005).
- **Bilateral sleep/wake sync**, with the body's state as ground truth and
  `#PASL`/`#PAWU` re-sent on every fresh connection so a reconnect cannot
  inherit a stale state.
- **Dome-originated body control**: named audio cues (`BD:`), random-audio
  suppression around dome sequences (`dome=seqon`/`seqoff`), and timed dome
  rotation requests (`dome=rot`).
- **The structured visual layer**, validated identically in firmware and browser,
  with one case per family hardware-verified on 2026-06-22 (Section 9.2).
- **The dome layout cache**, so the browser can draw the dome's real element set
  without the body holding a 24 KB buffer per request.

### 12.2 The body owns sound, and that is the deliberate divergence

`docs/topology.md:57` states it as a difference from the reference designs: the
hobby's usual arrangement is *"dome-side module ownership"*, protoArtoo's is
*"body-side audio authority"*. The dome image is built with
`MARC_SOUND_LOCAL_ENABLED=false`, so the AUX4/AUX5 sound serial is unused and the
dome asks rather than plays.

The cost is one extra hop and one extra vocabulary (`BD:`); the benefit is that
the audio catalogue, the named-track model and the volume control live on the
board with the web UI, the filesystem and the operator's sequence editor.

### 12.3 Not done, and honest about it

**The 101-token measurement.** ADR 0055 measured protoArtoo against the ShadowMD
command template, which emits 101 distinct strings across a two-board dialect:

| | count |
| --- | --: |
| interprets itself | 57 |
| forwards, and the dome fork handles | 8 |
| forwards, and the dome fork has no handler | 7 |
| **rejects or silently swallows** | **29** |

**36 of 101 do nothing**, and the largest single cause is structural rather than
missing features: `src/web/api_drive.cpp` consumes every `:` and `#` command and
returns success regardless. **23 of the 29 rejected tokens have a working handler
in the dome fork** and are unreachable only because the body ate them. ADR 0055
is the decision that fixes this; it is not yet fully implemented.

**No health polling.** The dome publishes `body_link` and `visual_authoring`
telemetry on its own `/api/health`, and protoArtoo never reads it. The body's
view of dome health is one bit: heartbeat inside 5 s.

**No baud negotiation, and no diagnosis of the most likely failure.** The body is
fixed at 9600 and cannot tell "dome absent" from "dome at 2400" (7.2). A one-line
improvement would be to probe at 2400 after N failed 9600 probes and report
*"dome found at 2400 -- set mserial2 to 9600"* rather than "no dome".

**`*RD00` has no handler in the dome fork.** The fork implements `*RD01`,
`*RD02` and `*RD03` but not the all-holos form, which is in the standard ShadowMD
button table. `*ON00`, `*OF00` and `*ST00` are all present, so this is a
one-registration gap rather than a pattern. Verified this session.

### 12.4 Costs to state plainly

- **The dome is a second firmware with its own release cycle**, its own web UI,
  its own NVS and its own update path. Two OTA stories, not one.
- **The fork is 173 commits ahead of upstream and two behind**, which is a
  rewrite of the control layer rather than a patch set. It is not going to be
  merged upstream, and the divergence is a maintenance obligation.
- **A sequence that uses fork-only commands is not portable** to any other
  droid (8.2, 9.4).
- **Logic display settings do not survive a dome reboot.**
  `Reeltwo LogicEngine.h:958-961` is literally `void restoreSettings() {
  defaultSettings(); }`, and no NVS-backed controller is instantiated. Colour,
  brightness and palette are compiled-in defaults on every boot. Anything the
  body set is gone. This is why `DV:`/`DL:` re-assert visual state at the start
  of a sequence rather than assuming it.

## 13. How the hobby drives a dome (non-normative)

Survey, for context on why protoR2link is shaped as it is. Every claim here is
about other projects and none of it is authority for ours.

### 13.1 The universal conventions

**9600 8N1, bare `\r`, no flow control, no acknowledgement.** That combination
holds across MarcDuino, ShadowMD, ShadowRC and PenumbraShadowMD. Two traps worth
recording because they have cost other people time:

- CuriousMarc's own command reference says commands end with *"carriage return
  (0x13)"*. **0x13 is DC3.** CR is 0x0D, as MarcDuino's own `CMD_END_CHAR '\r'`
  and ReelTwo's `if (ch == 0x0D)` both confirm. Do not propagate 0x13.
- MarcDuino panel commands are **length-rigid**: `if (length!=5)` rejects them.
  `:OP1` and `:OP001` are both silently dropped. Always zero-pad.

**The dome owns the sequence.** In every MarcDuino-lineage project the body sends
a *name* (`:SE01`) and never a step list. protoArtoo is the deliberate exception:
ADR 0004 and ADR 0008 put the timeline, audio and panel intent in the body so a
community sequence is tunable per droid without reflashing the dome, and `DV:`
exists precisely to recover the dome-native *look* that giving up dome-side
sequencing would otherwise cost.

**Port allocation.** ShadowMD on an Arduino Mega: `Serial1` dome MarcDuino,
`Serial2` motor controllers, `Serial3` body MarcDuino, all 9600. PenumbraShadowMD
on ESP32 keeps the same shape. protoArtoo's `S1`/`S2`/`S3` lane naming follows
this convention.

### 13.2 Nobody has a working return path

This is the finding most worth carrying, because it explains why protoR2link
looks unusual rather than incomplete.

- **No heartbeat, no watchdog, no retry, no CRC** on any serial dome link in the
  surveyed corpus.
- MarcDuino *does* echo every received character and emits `OK\n\r` after
  accepted `:` and `#` commands -- and **no body controller in the corpus reads
  it.** PenumbraShadowMD's entire use of the dome RX line is to drain it and
  print it to USB.
- Error messages are compiled out: MarcDuino ships with `_ERROR_MSG_ 0` and
  `_FEEDBACK_MSG_ 0` in the master.
- AstroPixelsPlus emits nothing at all on Serial2.

**protoR2link's heartbeat, transport arbitration and dome-to-body command flow
have no equivalent in the reference designs.** That is a real differentiator, and
it is also why the dome fork had to be written: no stock firmware would answer.

### 13.3 Pacing, as the ecosystem assumes it

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

### 13.4 Slip ring practice

No project's source documents a dome-serial slip-ring conductor budget; what
exists is community documentation.

- Printed Droid: the 24-wire ring is *"the standard, with eight of these wires
  dedicated to signal transmission"*; the 12-wire allocates four to signal; *"each
  wire capable of handling up to 2 amperes"* with a recommendation not to exceed
  7 A total. And: *"It is most prudent to transmit the maximum voltage into the
  dome and then step it down locally within the dome"* -- the same advice the
  AstroPixels vendor gives (5.1).
- Reeltwo's own dome BOM cites a 12-wire ring.
- **A serial dome link needs three conductors: TX, RX, GND.** protoArtoo uses all
  three because the link is bidirectional; most of the hobby uses two, because
  theirs is not.
- **Noise mitigation is `UNKNOWN`.** No project documents a slip-ring-specific
  retry, CRC or baud reduction. The fork's own troubleshooting advice -- rotate
  the dome by hand while watching for heartbeat gaps, add 100 nF across the UART
  lines at the ring connector, keep signal wires away from motor runs -- is the
  most concrete guidance found anywhere, and it is untested here.

### 13.5 Level shifting

Both ends of protoArtoo's link are ESP32s at 3.3 V, so the question does not
arise for us. It does for anyone driving an AstroPixels board from a 5 V Arduino
Mega: whether the Serial2 RX net is 5 V tolerant is **`UNKNOWN`** -- no repo,
vendor page or schematic in evidence addresses it, and the design files are not
public. Settled by a continuity check on the RX net for a series resistor or
divider, or by asking the vendor.

## 14. How the two Dome Controller members differ

| | **AstroPixels** | **Teeces** |
| --- | --- | --- |
| Lighting | WS2812B addressable RGB | MAX7219-driven discrete LEDs |
| Colour | any, per pixel | fixed by the LED fitted |
| Controller | ESP32, socketed 30-pin devkit | Arduino Pro Micro on the RLD |
| WiFi / web UI / OTA | yes, with AstroPixelsPlus | none |
| LED count | 269 | 7 MAX7219s driving 296 |
| Text and fonts | yes, Latin and Aurabesh | yes, via JawaLite |
| Panel servos | yes, with two PCA9685s | no |
| Holoprojectors | yes, light and servo | no -- separate device |
| Supply | 5 V, ~700 mA typical | 5 V, current `UNKNOWN` |
| Serial | 9600 stock, 2400/9600 on AstroPixelsPlus | 9600 |
| Protocol | `LE`/`HP` native, or Marcduino/JawaLite with the right firmware | JawaLite |
| Price | GBP 80, one supplier, out of stock today | varies, several suppliers |
| Design files | not published | community, multiple vendors |
| protoArtoo status | `supported` | `roadmap` |

The two are mechanically interchangeable -- AstroPixels was designed to drop into
Teeces surrounds with the same mounting holes -- and protocol-incompatible in
every respect that matters. [`teeces-dome-lighting.md`](teeces-dome-lighting.md)
Section 10 is the difference list from the other side and it is not short.

> [!NOTE]
> **Supply risk cuts the other way here.** Teeces is a roadmap item with several
> vendors and published community designs; AstroPixels is supported today with
> one part-time supplier, no spares, and closed design files (2.5). If the
> Dome Controller category is ever reduced to one member, the argument for
> keeping Teeces on the roadmap is availability, not capability.

## 15. Agent Lookup Quick Reference

| Question | Answer | Section |
| --- | --- | --- |
| What is the product? | AstroPixels: 9 PCBs, WS2812B, GBP 80, Darren Poulson | 2 |
| What is AstroPixelsPlus? | third-party firmware for it, by Mimir Reynisson | 6 |
| Which firmware does protoArtoo need? | `mattiasbrandt/AstroPixelsPlus` | 6.2 |
| Registry row | `include/component_registry.inc:139`, part id 13, `protor2link`, `supported`, caps 0, no gate, always included | Lineup |
| Dome MCU | 30-pin ESP32 devkit, `board = esp32dev`, Arduino core 2.0.x only | 2.2 |
| LED total | 269 WS2812B | 2.3 |
| Supply | 5 V, >= 1 A, ~700 mA typical | 5.1 |
| Dome serial pins | GPIO 16 RX / GPIO 17 TX | 5.4 |
| Body serial pins | artoo-esp32 GPIO 33 TX / 34 RX; firebeetle2 22 / 23 | 7.1 |
| **Baud** | **9600** -- dome-side NVS key `mserial2`, firmware default 2400 | **7.2** |
| Terminator | `\r` (0x0D) only on the Marcduino path | 7.5 |
| Max command length | **63 characters** | 7.5 |
| Heartbeats | `#PAHB` body, `#APHB` dome, 1 Hz, 5 s timeout | 7.3 |
| Does the dome ACK? | **No. Never. Nothing.** | 7.6 |
| Prefix families | `@` logics/PSI, `*` holos, `:` panels, `$` sound, `#` config, `DV:`/`DL:`/`DT:`/`DH:` structured | 8 |
| Unknown command behaviour | **silently runs a shorter prefix's action** | 8.1b |
| `@0T5` is | REDALERT (enum 11), not effect 5 | 8.2 |
| `@1P60` is | **front PSI Leia, not the Latin font** | 8.1 |
| `@7` / `@8` are | **top / rear** -- reversed vs JawaLite | 8.4 |
| `DL:...:WHITE` is | not white; logics have no white ColorVal | 8.7 |
| Colour spaces | three, mutually incompatible | 8.7 |
| Panel `:OP01` is | dome ring panel P1 outbound, body ARM1 inbound | 8.6 |
| Panel map | 13 panel slots + 6 holo axes, two PCA9685s | 11 |
| Does the dome persist logic settings? | **No. Every reboot is compiled-in defaults** | 12.4 |
| Where does sound live? | **the body** | 12.2 |
| Who owns the sequence timeline? | **the body** (ADR 0004/0008), unlike the rest of the hobby | 13.1 |

## 16. Open Items

Each with the artefact or bench test that settles it.

1. **Measured current for our own set.** Vendor says ~700 mA typical; we have
   never measured. *Inline meter on the dome 5 V feed, idle and during
   `DV:SCREAM`.*
2. **The `dome_link_panel` values for `docs/droid-parts.yaml`.** Section 11 is
   read from the fork's source; the catalog asks for verification on hardware.
   *Send each `:OPnn` and watch which panel moves.*
3. **Sustained command rate.** Unmeasured on either side. *Stream alternating
   `@0T1`/`@0T2` at 9600 and count applied effects against sent commands; watch
   for `[CMD][...][queue-full]` in the dome log.*
4. **Does the body ever emit a command longer than 63 characters?** `DomeTxCmd`
   truncates at 64 silently, and the `SEQ_FALLBACK` path forwards unvalidated
   `DM:*` names. *Add a length assertion in `domeQueueTx()`, or a native test
   over every emitted string.*
5. **Is the AstroPixels Serial2 RX net 5 V tolerant?** Irrelevant to us, relevant
   to anyone arriving from a Mega. *Continuity check for a series resistor, or
   ask the vendor.*
6. **Which PSI variant is fitted.** 25-LED `AstroPixelPSIPCB` is what the
   firmware instantiates; a 64-LED 8x8 variant exists in the library. *Count the
   LEDs on the board.*
7. **What `dpoulson/Reeltwo` adds over `reeltwo/Reeltwo`.** The vendor's install
   instructions name their own fork; ours pins upstream 23.5.3. *Diff the two.*
8. **Whether `*HPS303` still crashes.** Upstream issue #2 reported a
   `Guru Meditation ... IntegerDivideByZero` on that command. In the pinned
   23.5.3 the dim-pulse speed has a default assignment
   (`HoloLights.h:382`) and the divisor is non-zero on the default path, so it
   appears fixed -- **not reproduced here**. It matters because protoArtoo
   forwards raw `*` commands uninterpreted, so an operator can reach it.
   *Send `*HPS303` to the bench dome and watch the reset reason.*
9. **`*RD00` registration in the dome fork.** One-line gap (12.3).
10. **Baud misdiagnosis.** The body cannot distinguish "no dome" from "dome at
    2400". *Probe 2400 after N failed 9600 probes and say so in the status.*
11. **Four stale baud statements in the dome repository's docs** (7.2). *Correct
    them in the dome repo; out of scope for this one.*
12. **The vendor's own `docs/COMMANDS.md` logic-effect table is incomplete**
    against its own source -- seven of eleven rows (8.2). *Correct in the dome
    repo.*
13. **Availability.** Out of stock at the time of writing, one supplier, no
    spares sold, design files closed (2.5). Nothing to test; a risk to carry.

## 17. Sources

**Vendor (product authority)**
- AstroPixels documentation, https://r2djp.gitbook.io/astropixels -- read in
  full this session via its `llms-full.txt` export (20 pages, 33 KB)
- Product listing, https://we-make-things.co.uk/product/astropixels/
- `dpoulson/Astropixels` -- stock firmware, five build environments, no LICENSE
- `dpoulson/Reeltwo` -- the library the vendor's instructions install
- AstroPixels web installer, https://dpoulson.github.io/Astropixels/

**Firmware**
- `reeltwo/Reeltwo` @ **23.5.3** -- cloned at the tag the dome pins. LGPL-2.1.
  `src/core/Marcduino.h`, `src/dome/LogicEngine.h`, `src/dome/NeoPSI.h`,
  `src/dome/HoloLights.h`, `docs/JawaLite.dox`
- `reeltwo/AstroPixelsPlus` -- upstream. LGPL-2.1
- **`mattiasbrandt/AstroPixelsPlus`** -- the firmware protoArtoo talks to;
  read on disk this session. LGPL-2.1. `AstroPixelsPlus.ino`,
  `MarcduinoIngress.h`, `DomeSequences.h`, `WiringConfig.h`, `Marcduino*.h`,
  `docs/HARDWARE_WIRING.md`, `FORK_IMPROVEMENTS.md`
- `reeltwo/PenumbraShadowMD` -- the reeltwo body-side sketch, for convention
- `nhutchison/MarcDuinoMain` and `MarcDuinoClient` -- classic MarcDuino
- `dankraus/padawan360`, ShadowRC v1.3, `rimim/SHADOW` -- survey only

**Community**
- CuriousMarc MarcDuino command reference and the original JEDI JawaLite page
  (**note the 0x13 terminator error**, 13.1)
- Printed Droid knowledge base -- slip ring, ShadowMD, MarcDuino V3

**This repository** -- `src/tasks/dome_link.cpp`, `src/dome_link_arbiter.cpp`,
`src/drivers/dome_rx_parser.cpp`, `src/drivers/dome_cue_handler.cpp`,
`src/protocol_check.cpp`, `include/dome_link.h`, `include/marcduino.h`,
`include/config.h`, `docs/dome-visual-authoring-contract.md`,
`docs/dome-visual-presets.md`, `docs/topology.md`, `docs/commands.md`,
ADRs 0003, 0004, 0005, 0008, 0027, 0042, 0045, 0055, and the `test_dome_*` /
`test_protocol_check*` native suites.
