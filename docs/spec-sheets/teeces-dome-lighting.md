# Teeces Dome Lighting Spec Sheet

Working spec for the Teeces logic display and PSI light kit as a **Dome Controller**
lineup member (issue [#313](https://github.com/mattiasbrandt/protoArtoo/issues/313),
minted from [#303](https://github.com/mattiasbrandt/protoArtoo/issues/303)).

Research date 2026-09-10. Every protocol fact below was read from firmware source
or vendor documentation this session; nothing is recalled. Claims that could not be
sourced are marked `UNKNOWN` with the artefact that would settle them.

## Where this sits in the lineup

The **Dome Controller** category holds two products, and a builder picks one:

| Product | Lighting technology | Status |
| --- | --- | --- |
| AstroPixels Plus | WS2812B addressable RGB | `supported` |
| **Teeces** | MAX7219-driven discrete LEDs | `roadmap` |

**AstroPixels Plus is the one protoArtoo drives today because it is the kit the
operator installed** (#303, 2026-09-07). That is a fact about this project's
history, not a verdict on the products. Both are current, both are bought new
today, and neither is the other's fallback. Where they differ this sheet says how
they differ, not which is better.

What Teeces is *not*: it is not an ESP32-class board, it does not speak
**protoR2link**, and it has no WiFi, no web UI and no sequence engine. It is a
light kit with a small serial-commanded microcontroller. That difference in kind
is the whole of the integration problem, and Section 11 states it precisely.

## 0. Authority Contract

This document is an implementation authority for **JawaLite** framing and for the
Teeces board set's electrical and wiring behaviour.

Authority order for agent decisions:

1. Firmware source and vendor documentation listed in Sources.
2. This document.
3. Community build blogs and forum recollection.

If references conflict:

- Prefer firmware source over documentation, including the vendor's own. Three
  documented values in this subject are contradicted by the code that ships with
  them; each is named in place below.
- If still unresolved, mark the value `UNKNOWN` and stop implementation changes
  that depend on it.
- Request new evidence instead of trial-and-error tuning against hardware.

Agent requirements when using this document:

- MUST treat Section 9 as normative and Section 12 as survey.
- MUST NOT invent command letters, address numbers, effect numbers or timing
  constants not present here.
- MUST NOT assume a command that works on an AstroPixels Plus dome works on a
  Teeces dome, or the reverse. Section 10 is the difference list and it is not
  short.
- MUST label anything about current draw as unmeasured; no source states it.

## 1. Scope

Covers the Teeces board set, its controller module, its firmware lineage, and the
JawaLite serial protocol it speaks. Covers what protoArtoo would have to change to
drive one.

Does not cover: holoprojector control (a Teeces board does not drive holos --
Section 9.3), the Magic Panel, the Charge Bay Indicator or the Data Panel. Those
are separate devices on separate channels even in a Teeces build.

## 2. Boards Covered

| Item | Qty in a kit | Role |
| --- | --- | --- |
| RLD -- Rear Logic Display | 1 | Carries the Arduino module; head of the rear chain |
| FLD -- Front Logic Display | 2 | TFLD (top) and BFLD (bottom), stacked |
| PSI -- Process State Indicator | 2 | Front PSI, rear PSI |

Five PCBs plus one Arduino module. There is no separate controller board in v3 --
the Arduino plugs into the back of the RLD. Printed Droid's "Slim" variants add a
sixth board carrying the MCU.

Board revisions found in primary sources: RLD v3.0, RLD v3.1, FLD7, FLD11,
PSI V1, PSI v3.2, PSI v3.4.

> [!IMPORTANT]
> **"Teeces V4" names two unrelated products. Never use it unqualified.**
>
> - **Printed Droid Teeces V4** -- a re-layout of the classic MAX7219 board set:
>   better tracing, 3.5 mm screw terminals, SMD passives, colour-labelled boards.
>   Still the same architecture this sheet describes.
> - **JoyMonkey Teeces V4** -- a completely different RGB and fibre-optic design
>   that was **renamed to the RSeries Logic Engine**. The originator's own words,
>   dated 3/4/19: *"It has been 6 years since I stopped running Teeces lighting
>   kits to concentrate on Teeces V4 droid lighting (now called the Logic
>   Engine)."*
>
> Both usages are primary. Write "Printed Droid Teeces V4" or "RSeries Logic
> Engine" and never the bare string.

Attribution: the hardware was designed by **John Vannoy**, astromech.net username
"Teeces". The firmware almost everyone runs was written by **Marc Verdiell
(CuriousMarc)**. Conflating the two is the most common error in secondary sources
-- CuriousMarc's own MarcDuino page states plainly that the hardware is not his
design.

## 3. Project Integration

GPIO and transport facts for the body side are linked to the project's canonical
references:

- **[`docs/pin_map.md`](../pin_map.md)** -- dome link lane (`S3`), pin budget.
- **[`docs/commands.md`](../commands.md)** -- prefix routing, the `@` family.
- **[`include/config.h`](../../include/config.h)** -- `PIN_DOME_TX` / `PIN_DOME_RX`.
- **[`docs/adr/0045-the-body-models-dome-lights-and-keeps-forwarding-the-raw-families.md`](../adr/0045-the-body-models-dome-lights-and-keeps-forwarding-the-raw-families.md)**
  -- why the raw `@` family keeps forwarding.
- **[`docs/dome-visual-authoring-contract.md`](../dome-visual-authoring-contract.md)**
  -- the `DL:` / `DT:` structured layer.

## 4. Official Sources Checked

| Source | URL | Extraction notes |
| --- | --- | --- |
| V3.2 Kit Sheet, 17 Sept 2012 | http://joymonkey.com/run/files/V3.2%20Kit%20Sheet%20Sept%202012.pdf | **The authoritative product document.** Designer credit, full BOM, LED colour counts, Iset resistors, power options, chain wiring, PVC coupling fit |
| CuriousMarc Teeces sketch v1.2/1.3/1.4 | vendored in Reeltwo `src/dome/TeecesLogics.h` lines 758-3247 | **The authoritative protocol document.** Parser, command table, address map, baud, buffer sizes |
| MarcDuino command reference | https://www.curiousmarc.com/r2-d2/marcduino-system/marcduino-software-reference/marcduino-command-reference | Prefix routing; the `@`-stripping statement |
| Original JEDI JawaLite reference | https://www.curiousmarc.com/r2-d2/marcduino-system/marcduino-software-reference/marcduino-command-reference/original-jedi-jawalite-reference | Upstream grammar; credits Scott Gray |
| Reeltwo `docs/JawaLite.dox` | https://github.com/reeltwo/Reeltwo/blob/master/docs/JawaLite.dox | JawaLite 2.0 syntax and the extended address table |
| `nhutchison/MarcDuinoClient` `main.c` | https://github.com/nhutchison/MarcDuinoClient | The `@`-strip hop; JEDI baud compile flag |
| `nhutchison/MarcDuinoMain` `main.c` | https://github.com/nhutchison/MarcDuinoMain | Prefix constants; the inter-command delays |
| Printed Droid KB: Teeces V4 | https://www.printed-droid.com/kb/teeces-v4-dome-lighting-system/ | **The only source naming the 5-pin signals**; RAW/5V inputs |
| Printed Droid KB: Teeces 32 | https://www.printed-droid.com/kb/teeces-32-dome-lighting-system/ | ESP32-C3 GPIO map, 9600 baud |
| `PrintedDroid/Teeces-ESP32` | https://github.com/PrintedDroid/Teeces-ESP32 | Teeces32 firmware; **no licence file**; last push 2026-06-19 |
| MAX7219/MAX7221 datasheet Rev 4, 7/03 | https://www.analog.com/media/en/technical-documentation/data-sheets/MAX7219-MAX7221.pdf | Chip-layer electrical truth, register map |
| OSH Park shared projects (JoyMonkey) | https://oshpark.com/profiles/JoyMonkey | Board dimensions, layer count, upload dates |
| `barrettandcarly.com/blog/elec` | -- | John Vannoy's original v2/v3 blog, cited by the kit sheet. **Dead, no DNS response** |

## 5. Electrical (board level)

**Supply is 5 V.** Kit sheet, verbatim: *"The system runs on 5V."*

Three documented power paths:

| Path | Range | Notes |
| --- | --- | --- |
| Direct `+5V` / `GND` terminal | 5 V | All RLD revisions. The recommended input |
| RLD v3.1 `RAW` header | 7 V to 14 V | Onboard LM7805 with 1 uF and 10 uF |
| RLD v3.0 `RAW` header | below 9 V | Feeds the Pro Micro's own regulator |

> [!CAUTION]
> RLD v3.0 has no onboard regulator. The kit sheet warns: *"This regulator should
> only be used with power sources below 9V. Going over 9V with a full chain of
> logic displays and PSI's will cause the Pro Micro to overheat and reset."*
> Printed Droid restates the same rule for V4: RAW input up to 12 V is
> *"not recommended but possible"*.

**Current draw is `UNKNOWN`.** No primary source -- not the kit sheet, not either
Printed Droid KB page, not OSH Park -- states current per board or per kit, and
nothing published permits deriving it: brightness is set both in software
(`setIntensity` 0-15) and in hardware (the Iset resistor). Settled by: bench
measurement at a stated brightness, or a datasheet calculation from the Iset
values below plus lit-segment count.

**Iset (brightness) resistors differ per board type:**

| Board | Resistor |
| --- | --- |
| RLD | 28 k |
| FLD (each) | 24 k |
| PSI (each) | 10 k |

**Decoupling:** every PCB takes one 0.1 uF and one 10 uF.

**MAX7219 count: 7 per kit.** The BOM says *"(7) MAX7219 LED Driver chips"*; the
firmware states the distribution unambiguously:

```c
LedControl lcRear =LedControl(DVAL,CVAL,LVAL,4); //rear chain, 3 devices for RLD, one for the rear PSI
LedControl lcFront=LedControl(9,8,7,FDEV);       //front chain, 3 devices: TFLD, BFLD, front PSI
```

RLD = 3, each FLD = 1, each PSI = 1. 3 + 1 + 1 + 1 + 1 = 7, matching the BOM.
Reeltwo's own example instantiates the same shape (`LedControlMAX7221<4>` rear,
`<3>` front), which is a second independent confirmation.

**Package:** MAX7219**CNG**, DIP, socketed on 24-pin sockets.

**LED geometry**, declared in the firmware:

```c
// Grid 0 if used for the top FLD, uses only the first 5x9 LEDs/bits
// Grid 1 is used for the bottom FLD, uses only the first 5x9 LEDs/bits
// Grid 3 is used for the RLD, uses only 5x27 LEDs/bits
```

| Board | Matrix | LEDs |
| --- | --- | --- |
| FLD (each) | 5 x 9 | 45 |
| RLD | 5 x 27 | 135 |
| PSI (each) | 13 + 13, two colours | 26 |

Cross-check: logic LEDs total 45 + 45 + 135 = **225**. The BOM's 3 mm LED counts
minus its stated spares are 60 red + 62 green + 13 yellow + 54 white + 36 blue =
**225** exactly. Two independent primary sources agree.

**LED colour is a build-time choice made by the builder, hole by hole.** The kit
sheet: *"There are no 'correct' color patterns to use."* Logic boards take 3 mm
flangeless LEDs in red, green, yellow, white or blue; PSIs take 5 mm, 14 each of
red/blue/green/yellow supplied. This is the single most consequential fact for
protoArtoo -- see Section 10.

**Logic level:** the classic controller is a 5 V part driving 5 V MAX7219s
directly, no shifter. The ESP32 successors are 3.3 V and do need one -- the
Teeces32 S3 sketch comments *"CLK/CS swapped for SN74AHCT125 wiring"*.

## 6. Electrical (MAX7219 chip level)

From the Analog Devices MAX7219/MAX7221 datasheet, Rev 4, 7/03. Included because
any attempt to drive Teeces boards directly, rather than through their Arduino,
lands here.

| Parameter | Value |
| --- | --- |
| Operating supply voltage `V+` | 4.0 V to 5.5 V |
| Operating supply current, all segments on | 330 mA max (`ISEG_ = -40mA`) |
| Quiescent supply current | 8 mA (`RSET` open circuit) |
| Shutdown supply current | 150 uA |
| Logic high input `VIH` | **3.5 V min** |
| Logic low input `VIL` | 0.8 V max |
| Input hysteresis | 1 V |
| `DOUT` high / low | `V+ - 1` V at -1 mA / 0.4 V at 1.6 mA |
| CLK clock period `tCP` | 100 ns min, i.e. 10 MHz max |
| CLK pulse width high / low | 50 ns min each |
| Display scan rate `fOSC`, 8 digits | 500 / 800 / 1300 Hz (min / typ / max) |
| Segment drive source current | -30 / -40 / -45 mA |
| Max recommended segment current | 40 mA |
| Digit drive sink current | 320 mA |
| Package | 24-pin DIP, wide SO, or CERDIP |

> [!CAUTION]
> **`VIH` min is 3.5 V.** A 3.3 V microcontroller cannot be relied on to drive a
> MAX7219 running at 5 V, which is why the Teeces32 design fits an SN74AHCT125
> level shifter. Any protoArtoo scheme that drives Teeces boards directly from an
> ESP32 needs that shifter too. This does not apply to the normal serial path,
> where protoArtoo talks to the Teeces Arduino, not to the chips.

**Register address map** (low nibble; `D15-D12` are don't-care):

| Register | Hex | Register | Hex |
| --- | --- | --- | --- |
| No-Op | `0xX0` | Digit 6 | `0xX7` |
| Digit 0 | `0xX1` | Digit 7 | `0xX8` |
| Digit 1 | `0xX2` | Decode Mode | `0xX9` |
| Digit 2 | `0xX3` | Intensity | `0xXA` |
| Digit 3 | `0xX4` | Scan Limit | `0xXB` |
| Digit 4 | `0xX5` | Shutdown | `0xXC` |
| Digit 5 | `0xX6` | Display Test | `0xXF` |

**Intensity** is a 16-step register (`0xX0` to `0xXF`). Duty cycle on the MAX7219
runs 1/32 (min) to 31/32 (max) in 2/32 steps; the MAX7221 uses 1/16 to 15/16.

**Cascading:** connect all `LOAD`/`CS` together and `DOUT` to the next device's
`DIN`. Data appears at `DOUT` 16.5 clock cycles after `DIN`, and `DOUT` is never
high-impedance. Writing to one device in a chain means padding the other devices'
slots with No-Op.

**Power-up state:** all control registers reset, display blanked, device in
shutdown, scan limit one digit, no decode, minimum intensity. Program before use.

`RSET` versus segment current, for `VLED = 2.0 V`: 40 mA needs 11.8 k, 30 mA needs
17.1 k, 20 mA needs 28.0 k, 10 mA needs 63.7 k. The Teeces Iset values in Section 5
sit in that range as expected.

## 7. The controller module and firmware

**MCU.** Kit sheet: *"Almost any Arduino can be used, but the V3 was designed with
an Arduino Pro Mini or Pro Micro in mind. A Pro Mini can be mounted directly to the
back of the RLD."* The BOM specifies *"(1) Arduino ProMicro/ProMini (5V 16Mhz)"* --
5 V, 16 MHz, not the 3.3 V/8 MHz variant.

The firmware supports exactly three board types:

| `BOARDtype` | Board | Rear chain D / C / L |
| --- | --- | --- |
| 1 | Pro Mini / Uno / Duemilanove | 12 / 11 / 10 |
| 2 | Sparkfun Pro Micro | 14 / 16 / 10 |
| 3 | Arduino Micro | A2 / A1 / A0 |

The front chain is hardcoded to pins **9 (D), 8 (C), 7 (L)** in all three cases.

**Flashing** is ordinary Arduino flashing through the module's own header; there is
no Teeces-specific programming connector documented on any RLD revision
(`UNKNOWN` whether one exists -- the Eagle board files would settle it). Printed
Droid's Slim V4 fits an Arduino Nano specifically for native-USB flashing.

**Firmware lineage.** The de-facto firmware is not in a repository of its own; it
is a zip:

```
John Vannoy's original (standalone blinker)
  -> CuriousMarc v1.2 / v1.3 / v1.4   full JawaLite emulation, text, PSI states
     -> "Ben" variant (v1.4, 2019-12-27)
     -> Printed Droid v3 (2025-10-23)  EEPROM config, '*' serial menu, watchdog
        -> PrintedDroid/Teeces-ESP32   Teeces32, ESP32-C3 Mini and ESP32-S3 Zero
```

The most convenient authoritative copy of the CuriousMarc sketch is the one
vendored inside Reeltwo at `src/dome/TeecesLogics.h` lines 758-3247.

It also requires the **`TeecesControl`** Arduino library, a renamed fork of
`LedControl` whose header still reads *"LedControl.h - A library for controling
Leds with a MAX7219/MAX7221"*, with `#define MAXDEVICES 4`.

> [!NOTE]
> **Serial control is a property of the CuriousMarc firmware, not of the
> hardware.** CuriousMarc describes his sketch as *"a replacement for the JEDI"*
> and says *"When running my sketch, the Teeces Lights responds a full set of
> JawaLite commands."* Whether the original Vannoy sketch accepts serial input at
> all is `UNKNOWN` -- it is not in any public repository. Settled by: the
> astromech.net Teeces V3 wiki page, or the sketch bundled with an original kit.
> In practice every controller in Section 12 assumes the CuriousMarc lineage.

**Licence.** The classic sketch carries a bare freedom-to-modify grant with a
warranty disclaimer and **no named licence**. OSH Park calls the boards *"open
hardware!"* but **no licence identifier** (CC-BY, CERN-OHL, TAPR) is stated
anywhere. `PrintedDroid/Teeces-ESP32` is public with **no licence file**. Public is
not the same as licensed for reuse; treat all three as unlicensed until a source
says otherwise.

**Teeces32 GPIO maps** (both current, they differ):

| | Rear D/C/CS | Front D/C/CS | Digital PSI |
| --- | --- | --- | --- |
| ESP32-C3 Mini v4.3 | GPIO7 / 6 / 5 | GPIO4 / 3 / 2 | GPIO8, GPIO9 |
| ESP32-S3 Zero v4.7 | GPIO4 / 5 / 6 | GPIO1 / 3 / 2 | GPIO7, GPIO8 |

The S3 build adds three NeoPixel holo outputs (GPIO11/12/13) and reserves I2C on
GPIO9/10 -- reserved only; `Wire.begin()` is never called.

## 8. Wiring, connectors and chains

**Two independent MAX7219 chains, not one.** Quoted from the firmware:

```
// Logic Display and PSI Boards should be wired up in two chains.
// Teeces board V3.1 has two headers to facilitate this.
//   OUT1 should be connected to the Rear PSI (which adds it after the RLD to the "rear" chain)
//   OUT2 should be connected to the Top FLD, bottom FLD, then Front PDI (this is the "front" chain)
```

- **Rear chain:** RLD (3 devices) -> rear PSI (1) = 4 devices
- **Front chain:** TFLD (1) -> BFLD (1) -> front PSI (1) = 3 devices

Padawan360's dome sketch records why: *"some builders ran into [problems] when
using a single chain setup for an extended period of time."*

**Chain connector: 5-pin, wired 1:1, straight through.** Printed Droid:
*"All connections use 5-pin cable, wired pin-to-pin"* with pins
*"Plus, Minus, L, C, and D ... connected 1-to-1."*

| Pin | Function |
| --- | --- |
| Plus | +5 V |
| Minus | GND |
| L | Load / Latch (`CS`) |
| C | Clock |
| D | Data |

Kit sheet orientation rule: *"GND OUT must always go to GND IN on the next board.
Try to use the shortest jumper wires possible to keep the signal from the Arduino
as strong as possible."*

Cable lengths in the official BOM: 2 x 5-pin 8 in (RLD to rear PSI, FLD to front
PSI); 1 x 5-pin **24 in** (RLD to FLD, crossing the dome); 1 x 5-pin 4 in (FLD to
FLD); 2 x 2-pin 12 in and 2 x 2-pin 8 in for holoprojector LEDs.

RLD v3.0 special case: with only one OUT header, the 24 in front-chain cable is
split into a 3-pin and a 2-pin -- the three signal pins go to Arduino 7/8/9, the
two power pins to any +5 V/GND pair.

**Command link is a different wire entirely.** Not the 5-pin connector: a plain
serial line into the Arduino's RX pin. CuriousMarc: *"The serial input connects to
the pin marked RXI or Rx on the Arduino (and optionally TXO or Tx for answer
messages)."* MarcDuino's own page: *"apart from power, you'll need two wires: One
between the two grounds of the Teeces and MarcDuino ... Another wire from the
Lights output on the MarcDuino Slave to the wire marked RXI or RX0 on your board."*

So: **one signal wire (master TX -> Teeces RX) plus a common ground.** TX back is
optional and normally left unconnected -- but see Section 9.6, because the Teeces
does transmit whether you listen or not.

Connector type for the command link is `UNKNOWN` on classic boards -- it is bare
terminals. Printed Droid V4 provides a labelled "RX/TX (Serial Communication)"
3.5 mm screw terminal.

## 9. The command protocol (normative)

### 9.1 The `@` prefix is not part of this protocol

> [!IMPORTANT]
> **`@` is a MarcDuino routing character, stripped before the bytes reach a Teeces
> board.** This is the single fact most often got wrong, and it is the one that
> decides protoArtoo's work.

MarcDuino's own comment, `MarcDuinoClient/main.c`:

```
 *  '@' display command, forwarded to JEDI controller on suart1 after stripping the '@' control character
```

and the code that does it:

```c
void parse_display_command(char* command, uint8_t length)
{
	// forward the command minus the @ character to the JEDI display
	if(feedbackmessageon) serial_puts_p(strDisplayCommandForwarded);
	if(length>=3)	// command must have at least a start, a character and an end
	{
		lightsuart_puts(command+1); // discard the start character, JEDI display doesn't use any
		lightsuart_putc('\r');		// add the termination character
	}
}
```

*"JEDI display doesn't use any"* is the whole answer. There are two hops: the
MarcDuino **master** forwards `@...` to the slave keeping the `@`; the **slave**
strips it and emits the remainder on its lights UART.

The Teeces parser confirms it from the other side -- it rejects anything whose
first character is not a digit:

```c
  if(length<2) goto beep;   // not enough characters
  // get the adress, one or two digits
  char addrStr[3];
  if(!isdigit(inputStr[pos])) goto beep;  // invalid, first char not a digit
```

Note `length>=3` on the MarcDuino side: a bare `@` or `@X` is dropped silently.

### 9.2 Framing

```
command   := address letter [argument] CR
address   := DIGIT | DIGIT DIGIT
letter    := 'T' | 'M' | 'P' | 'R' | 'S' | 'D'
argument  := DIGIT+          ; required for T,P,R,S; absent for D; free ASCII for M
CR        := 0x0D
```

The in-source specification, verbatim:

```
JawaLite command syntax uses plain text format, followed by carriage return. Format is address - command letter - value:
xxYzzz<cr>
where xx= address (one or two chars), Y=command letter, zzz=optional argument, <cr>= carriage return character ('\r'=0x13)
```

> [!WARNING]
> **That comment's `0x13` is wrong; CR is `0x0D`.** The code compares `case '\r':`,
> so the behaviour is correct and only the documentation is wrong. The same
> incorrect constant is copied into downstream documents. MarcDuino corroborates
> the real value: `#define CMD_END_CHAR '\r'`.

Line assembly is `\r`-terminated only. **`\n` is not a terminator.** A sender that
emits bare LF never completes a command, and a stray `\n` is absorbed into the
next command, which then fails the leading-digit test.

Serial parameters: **8N1**. Baud is discussed in 9.7.

### 9.3 Address map

| Addr | Target | On a Teeces board |
| --- | --- | --- |
| 0 | Global, all units | honoured |
| 1 | TFLD -- Top Front Logic Display | honoured |
| 2 | BFLD -- Bottom Front Logic Display | honoured |
| 3 | RLD -- Rear Logic Display | honoured |
| 4 | Front PSI | honoured |
| 5 | Rear PSI | honoured |
| 6 | Front Holoprojector | **accepted and ignored** |
| 7 | Rear Holoprojector | **accepted and ignored** |
| 8 | Top Holoprojector | **accepted and ignored** |

The firmware documents the holo exclusion itself:

```
6T1, 7T1, 8T1 -> holos on  - not implemented, HP lights controlled by MarcDuino HP.
0D, 6D, 7D, 8D -> holos off  - not implemented, HP lights controlled by MarcDuino HP.
```

Addresses 6-8 exist on the wire because the original JEDI board drove the
holoprojector LEDs as well. A MarcDuino slave still emits `6T1\r` and `6D\r` on the
same lights UART in response to `*ON`/`*OF` commands, so **a Teeces sits on a wire
carrying traffic addressed to devices it is not**. It discards that traffic.

JawaLite 2.0 (Reeltwo's `docs/JawaLite.dox`) extends the map well beyond 8 -- 80
Magic Panel, 81 periscope, 82 life form scanner, 83 CBI, 84 data port, 85-89
booster/CPU arm/drink/zapper/buzz-saw, 10-79 reserved, 9 other. **Teeces implements
none of those**; they matter only for judging whether a future protoArtoo JawaLite
driver should range-check addresses.

> [!NOTE]
> **This is a multi-drop addressed bus, populated with one device in the common
> build.** The protocol is explicitly designed for several addressed units on one
> serial line -- `docs/JawaLite.dox`: *"Each and every unit that is connected to a
> single JawaLite 2.0 serial interface bus has to have a unique address."* The
> R-Series firmware anticipates exactly this: *"Mode 0 - Turn Panel off (This will
> also turn stop the Teeces if they share the serial connection and the '0'
> address is used)."* But it is a **write-only TX fan-out or store-and-forward
> chain**, not a bidirectional bus -- MarcDuino's soft UART is *"software serial
> (write only)"*, and the Reeltwo boards implement chaining by forwarding received
> bytes onward.

### 9.4 Commands

**`T` -- display mode**, verbatim from the firmware header:

```
0-5T0	 - test (switches all logics to on)
0-5T1 	 - display to normal random
0T2	 - flah, same as alarm
0T3	 - alarm for 4 seconds (flashes displays on and off)
0T4	 - short circuit (10 seconds sequence)
0T5	 - scream, same as alarm
0T6	 - leia, 34 seconds (moving horizontal bar)
0T10	 - Star Wars. Displays "Star Wars" on RLD, "STARS" on TFLD, "WARS" on BFLD
0T11	 - March (alternating halfs of logics, 47 seconds)
0-3T92   - spectrum, bargraph displays. Runs forever, reset by calling to 0T1
0-3T100  - Text displays. Displays text set by the M command below
```

Confirmed against `doTcommand`'s `switch(argument)`: cases 0, 1, 2, 3, 5, 4, 6, 10,
11, 20, 92, 100, with `case 2: case 3: case 5:` sharing one arm, and
`default: exitEffects();`.

> [!IMPORTANT]
> **Per-address granularity is not uniform.** `T0`, `T1`, `T20`, `T92` and `T100`
> honour the address. `T2`, `T3`, `T4`, `T5`, `T6` and `T11` **ignore the address
> entirely** and set a global effect. So `1T6` does not run Leia on the top front
> display -- it runs Leia on everything.

Effect durations are compile-time, not protocol: `LEIAduration 34000`,
`ALARMduration 4000`, `MARCHduration 47000`, `FAILUREduration 10000`.

**Other letters:**

| Cmd | Syntax | Meaning | Provenance |
| --- | --- | --- | --- |
| `T` | `0-5T<n>` | mode/effect, above | JEDI |
| `M` | `0-3M<text>` | set scroll message | JEDI |
| `P` | `0-3P60` / `0-3P61` | Latin / Aurabesh alphabet | JEDI |
| `D` | `6D`, `7D`, `8D` | holos off, no argument | JEDI; **stub on Teeces** |
| `R` | `0-3R<0-6>` | random blink style | **CuriousMarc extension** |
| `S` | `0,4-5S<0-4>` | PSI state | **CuriousMarc extension** |
| `T20` | `0-5T20` | displays off | **CuriousMarc extension** |
| `W` | `0W<seconds>` | effect duration | **JEDI only -- rejected by Teeces** |
| `P91` | `<a>P91` | select digital output | **JEDI only -- ignored by Teeces** |

`S` values: `S0` all-on test, `S1` normal random, `S2` colour 1, `S3` colour 2,
`S4` off.

> [!CAUTION]
> **`W` is emitted by MarcDuino and rejected by Teeces.** MarcDuino sends `@0W4`
> after an alarm and `@0W10` after a short circuit, as duration modifiers. `W` is
> not in the Teeces dispatch switch, so it falls to `default: goto beep` and
> returns a BEL byte. Teeces gets its durations from the compile-time constants
> above instead. MarcDuino's own source already suspects it -- the comment on the
> second one reads `// for 10 seconds (this one does not seem to respond)`.
>
> `P91` differs: `doPcommand` handles only 60 and 61 with `default: break;`, so
> `P91` is accepted, produces no BEL, and does nothing.

### 9.5 Text

Syntax `<addr>M<text>\r`, addresses 0 (all three logics), 1 TFLD, 2 BFLD, 3 RLD.
The `M` case is special-cased before numeric parsing, so the rest of the line is
taken raw.

**Setting text does not display it.** Two commands are required:

```
3MHELLO WORLD<CR>
3T100<CR>
```

which is exactly what MarcDuino does -- `RLDSetMessage()` then
`RLDDisplayMessage()`.

Limits:

- **Buffer 64** (`#define MAXSTRINGSIZE 64`). Excess is truncated by `strncpy`,
  not rejected.
- **Effective ceiling 62**, because the whole line shares `CMD_MAX_LENGTH 64` and
  the address plus `M` consume 2 or 3 bytes.
- **Uppercase only.** `getLatinLetter` maps `A`-`Z`, `0`-`9`, space, and exactly
  `* # @ - | . < >`. Lowercase is unmapped and falls to the blank glyph, so
  `"Hello"` renders as `H` followed by four blanks. The source header states it:
  *"Only uppercase implemented."*
- Scroll speed is local, not protocol: `#define SCROLLspeed 48` ms per column.

### 9.6 Error and response behaviour

**This link is not write-only, whatever the wiring suggests.** The classic sketch
runs an interactive console on the same port. It echoes every received character,
prints a `> ` prompt after each command, and every executor emits a verbose
acknowledgement:

```c
    ch=serialPort->read();  // get input
    serialPort->print(ch);  // echo back
```
```c
  serialPort->println();
  serialPort->print("Command: T ");
  serialPort->print("Address: ");
  serialPort->print(address);
  serialPort->print(" Argument: ");
  serialPort->print(argument);
```

A single `0T1\r` provokes roughly 40 bytes of reply. **At 2400 baud that is about
170 ms of transmission.**

A rejected command returns a single **BEL, `0x07`**:

```c
  beep:                                 // error exit
    serialPort->write(0x7);             // beep the terminal, if connected
    return;
```

Consequences for a body controller: either leave the Teeces TX unconnected, or
read and discard what comes back. Never parse it as protocol. A lone `0x07` means
the last command was rejected.

**Two real parser defects, both in the classic lineage:**

1. **One-byte buffer overflow.** `cmdString[64]` with the guard
   `if(pos<=CMD_MAX_LENGTH-1)pos++`, and the write happens before the check, so
   `pos` reaches 64 and every character past the 64th writes out of bounds. The
   Teeces32 changelog confirms it independently: *"BUGFIXES (v3.5): Fixed buffer
   overflow in command parsing"*, fixed there as `if (pos < CMD_MAX_LENGTH - 1)`.
2. **Dead length guards.** `if(!length>pos) goto beep;` parses as
   `(!length) > pos`. Since `length>=2` is already guaranteed, this never fires.

Neither is reachable in normal use. Both matter if anything ever fuzzes the port
or sends long text, and both argue for protoArtoo clamping length on its side.

### 9.7 Baud and timing

> [!IMPORTANT]
> **Baud is a compile-time constant of the firmware, not a property of the
> hardware.** "Teeces runs at 2400" is true only of the legacy default.

```c
// Baud Rate sets the baud rate of the serial connection. Current MarcDuino HP Firmware (v1.5)
// uses the JEDI default rate which is very slow at 2400.
// If you control it from something else, you probably want to use 9600.
#define BAUDRATE 2400
```

| Firmware | Baud |
| --- | --- |
| CuriousMarc classic, default | 2400 |
| Printed Droid v3 sketch | 9600 (`Serial.begin(9600)` hardcoded) |
| Teeces32 | 9600, *"JawaLite standard baud rate"* |

The MarcDuino side matches: `_9600BAUDSJEDI_` selects 9600 and **ships commented
out**, so a stock MarcDuino talks to the display at 2400.

**Inter-command delays.** Nothing in the Teeces firmware documents a rate limit,
but MarcDuino's empirically-tuned delays are the de-facto specification:

| Delay | After |
| --- | --- |
| 100 ms | `T5`, `T2`, `T6`, `T100` |
| 50 ms | `W4` |
| **200 ms** | `T92` |
| **250 ms** | `M<message>` |
| 20 ms | `P91` |

The load-bearing comment: `_delay_ms(200); // JEDI needs a large amount of time to
setup, 100 ms not enough`.

**Treat 100 ms as the floor between commands and 250 ms after a text set.**

The reason is structural, not arbitrary. The classic `loop()` reads one byte per
iteration and the same loop body drives every MAX7219 update and effect animation.
The echo chatter of 9.6 is a blocking `print`; while it is transmitting, RX is not
being drained, and anything beyond the 64-byte hardware ring is silently lost --
which desynchronises the line and turns the next command into a BEL.

Buffers: Teeces command line 64, Teeces text 64, MarcDuino serial in 64,
MarcDuino serial out 255.

Overflow behaviour differs by generation: classic silently corrupts (the defect
above); Teeces32 drops and signals (`Serial.write(0x7); // ASCII BEL - audible
alert for buffer overflow`).

## 10. How the two Dome Controller members differ

Both are current products, both are bought new, and a builder picks one. These are
differences in kind, not a ranking.

| | Teeces | AstroPixels Plus |
| --- | --- | --- |
| Light source | Discrete 3 mm / 5 mm LEDs | WS2812B addressable RGB |
| Colour | **Fixed at build time**, chosen per hole by the builder | Per-pixel, changeable at runtime |
| Driver | 7 x MAX7219, two chains | One data line per display |
| MCU | Arduino Pro Mini / Pro Micro / Micro (5 V), or ESP32 on Teeces32 | ESP32 |
| Logic geometry | FLD 5 x 9 each, RLD 5 x 27 | FLD 9 x 10, RLD 27 x 4 (fork-dependent) |
| Command grammar | JawaLite, no prefix | Marcduino `@` family, plus native `LE`/`HP`, plus this fork's `DV:`/`DL:`/`DT:`/`DH:` |
| Accepts a bare `0T1`? | Yes, that is its only form | Yes -- Reeltwo accepts both `@0T1` and `0T1` |
| Network | None | WiFi, web UI, REST, WebSocket |
| Sequence engine | None; effects are fixed compile-time animations | Full |
| Text | Yes, uppercase only, 62 chars | Yes |
| Holoprojectors | **Not driven** -- addresses 6-8 discarded | Driven (3 outputs) |

**The address semantics are not the same, and this is the trap.** Verified by
reading `MarcduinoLogics.h` in the local AstroPixelsPlus fork:

| Command | AstroPixels Plus | Teeces / JawaLite |
| --- | --- | --- |
| `@1T*` | FLD, the whole front | **TFLD, top front only** |
| `@2T*` | **RLD** | **BFLD, bottom front** |
| `@3T*` | **no handler; dropped** | **RLD** |
| `@1M` / `@2M` / `@3M` | TFLD / BFLD / RLD | TFLD / BFLD / RLD -- agree |
| `@1P60` / `@2P60` / `@3P60` | TFLD / BFLD / RLD | TFLD / BFLD / RLD -- agree |

So AstroPixels Plus follows JawaLite addressing for `M` and `P` but not for `T`,
and it has no `@3T` at all. `@2T1` means "rear logic to normal" on one dome and
"bottom front logic to normal" on the other.

**Effect coverage runs in both directions:**

| Effect | Teeces | AstroPixels Plus |
| --- | --- | --- |
| `T1` normal, `T2` flash, `T3` alarm, `T4` failure, `T5` scream, `T6` Leia, `T11` march | yes | yes |
| `T0` test all-on | yes | **no** |
| `T10` Star Wars | yes | **no** |
| `T20` off | yes | **no** |
| `T92` bargraph / spectrum | yes | **no** |
| `T100` text mode | yes | **no** -- `@1M` displays immediately |
| `T12` rainbow, `T15` lights out, `T22` fire, `T24` pulse | **no** | yes |
| `@<a>P<n>` | alphabet select (60/61 only) | **PSI state** (1-11) |

> [!CAUTION]
> **`P` is overloaded across the two.** On a Teeces `1P61` selects the Aurabesh
> font. On AstroPixels Plus `@1P1` through `@1P11` set front PSI mode. Same bytes,
> different subsystem. protoArtoo emits `@0P1` as its PSI reset
> (`src/tasks/sequence_engine.cpp:230-231`), which is a registered action on
> AstroPixels Plus and **a silent no-op on a Teeces** -- `doPcommand` handles only
> 60 and 61 and falls through `default: break;`. The Teeces equivalent is `0S1`.

**Colour is the deepest difference.** protoArtoo's `DL:` grammar carries a colour
argument with eight values and two modes whose entire content is colour
(`RAINBOW`, `FLASHCOLOR`). On a Teeces, colour is soldered in and cannot change.
Note this is already a partial problem on the supported member: our own fork maps
`WHITE` to `kDefault` with the comment *"ReelTwo logics have no white ColorVal."*
Teeces widens that from one value to the whole axis.

## 11. What protoArtoo would have to do

**The gap is far narrower than the ticket assumed, and it is one specific thing.**

Already matching, verified in our own source this session:

| Fact | Evidence |
| --- | --- |
| Dome link is 9600 8N1 | `src/tasks/dome_link.cpp:170` |
| Commands are `\r`-terminated | `src/tasks/dome_link.cpp:920-921` -- `print(txCmd.buf)` then `print('\r')` |
| One command per line, ASCII | same |
| Queue message is 64 bytes | `include/dome_link.h:63-65` -- matches Teeces `CMD_MAX_LENGTH` exactly |
| `@` family already reaches the dome | `src/web/api_drive.cpp:185` |
| `@<digit><T\|P\|M>` already validates | `src/protocol_check.cpp:713-727` |

**What must change:**

1. **Strip the `@`.** protoArtoo forwards `@...` verbatim. That is correct for
   AstroPixels Plus, which emulates a MarcDuino; it is wrong for a Teeces, which
   expects what a MarcDuino *slave* would have emitted. Supporting Teeces means
   protoArtoo performs the hop that `MarcDuinoClient/main.c` performs. This is the
   whole of the framing work.
2. **Widen the accepted letter set.** `src/protocol_check.cpp:723-724` accepts only
   `T`, `P` and `M` after the address digit. Teeces needs `S` (PSI state) and
   optionally `R` (random style), both CuriousMarc extensions. `@0S1` is rejected
   today.
3. **Re-target the reset pair.** `@0T1` is correct on both. `@0P1` is a no-op on
   Teeces; the equivalent is `0S1`. `src/tasks/sequence_engine.cpp:230-231,246-248`
   and `src/tasks/sequence_dispatcher.cpp:323-324,371-372` emit the pair, and
   `src/tasks/sequence_catalog.cpp:226-228` carries it as catalog steps.
4. **Decide what `DL:` means on a monochrome dome.** The colour argument and the
   `RAINBOW` and `FLASHCOLOR` modes have no Teeces meaning. ADR 0045's
   intent-not-truth rule already gives the answer shape -- the model records what
   protoArtoo commanded, never what the device is -- so a colour can be accepted
   and reported while the device ignores it. That is a decision, not a derivation,
   and it belongs on #313.
5. **Map `DL:` targets onto the real address map.** `DL:`'s `FLD` is singular;
   JawaLite has two front displays at addresses 1 and 2. Note
   `docs/droid-parts.yaml:79` already says "Front Logic **Displays**" plural, so
   the catalog is closer to Teeces geometry than `DL:` is.
6. **Rate-limit.** 100 ms floor between commands, 250 ms after a text set
   (Section 9.7). protoArtoo has no such gap today.
7. **Do not parse the return path.** Section 9.6 -- expect about 40 bytes of ASCII
   chatter per command and a bare `0x07` on rejection. `src/drivers/dome_rx_parser.cpp`
   must not treat any of it as a dome line.

**Component Protocol.** The ticket's "TBD" resolves to **JawaLite**. It fits
#303's test -- it changes the driver, so it is a protocol and not configuration.

> [!NOTE]
> **JawaLite may serve more than one product**, which #303 says is the normal case
> and the reason the registry makes the difference visible. JEDI Display (the
> commercial predecessor Teeces emulates) and the R-Series Logic Engine both speak
> it. If either joins the lineup, JawaLite likely deserves its own protocol sheet
> beside this product sheet -- the split `hotrc-sbus-spec.md` and
> `sbus-protocol.md` already model.

**Board Capability Gate.** Almost certainly none is needed. Teeces reaches the body
over the same single UART lane at the same baud as the supported member, so any
board that can be wired for AstroPixels Plus can be wired for a Teeces. That makes
this a **Component Member** question rather than a Gate question -- and under
ADR 0042 a member setting appears exactly because this category would then hold
more than one selectable option.

> [!IMPORTANT]
> **Our dome fork already compiles the MAX7219 chain driver.** `AstroPixelsPlus.ino:171-172`
> includes `dome/TeecesPSI.h` and `dome/TeecesLogics.h` live, and `:323`
> instantiates `LedControlMAX7221<5> ledChain1` -- bound to the Charge Bay
> Indicator and Data Panel, not to any Teeces object. Reeltwo ships
> `TeecesRearLogics`, `TeecesFrontLogics` and `TeecesPSI` classes. A path therefore
> exists where an AstroPixels-class ESP32 drives real Teeces boards directly, and
> it is not hypothetical -- Reeltwo's `examples/teecesLogics/teecesLogics.ino` is
> exactly that program. **This is a different product shape from the one this
> sheet costs**, and it is worth naming on #313 before the framing work is
> scoped, because it would change which side of protoR2link the JawaLite lives on.
>
> Caveat from Section 6: those Teeces logic classes take a `LedControl&`, and
> Reeltwo's Teeces *logics* classes do **not** implement JawaLite -- they use
> Reeltwo's own `RL<n>` / `FL<n>` / `TL<n>` / `BL<n>` `CommandEvent` grammar. Only
> `TeecesPSI` is JawaLite-addressable.

## 12. How other controllers reach Teeces (survey, non-normative)

Six projects, five different integration shapes. Useful because it shows JawaLite
is the convergence point and everything else is local choice.

| Project | Stance | Transport to Teeces |
| --- | --- | --- |
| **MarcDuino** | Opaque two-hop passthrough | Slave lights UART, PC1 (v2) / PC0 (v1), **2400** default |
| **SHADOW** | Replaces the Teeces firmware | `Serial3` @ **57600**, EasyTransfer **binary struct**, not JawaLite |
| **SHADOW_MD** | Assumes a MarcDuino in between | `Serial1` @ 9600 to MarcDuino master, `@`-prefixed |
| **Penumbra Shadow MD** | Same as SHADOW_MD | `Serial1` @ 9600, byte-identical command set |
| **ShadowRC** | Never names the display | Only `:SE00`-`:SE10`; MarcDuino synthesises the `@` internally |
| **Padawan360** | **Is** the Teeces firmware | Runs on the Teeces Arduino, drives both MAX7219 chains itself |
| **r2_control** (Poulson) | Custom firmware, I2C | SMBus byte writes to **0x1c**, grammar `S<n>` |
| **R-Series Logic Engine** | Peer that can also drive Teeces PSIs | Shares the serial wire; `#error CANNOT USE BOTH TEECES AND PSI PRO` |

Two findings worth carrying into #313:

**ShadowMD already speaks more Teeces than it speaks AstroPixels.**
`Shadow_MD_DualController_Template.ino:3560-3599` emits `@0T10` (Star Wars),
`@0T92` (bargraph) and `@0T100` + `@0M<text>` (custom text). All three are real
Teeces effects and **none has a handler in AstroPixels Plus**. A ShadowMD user's
logic-display types 5, 7 and 8 are inert on the supported member today and would
come alive on a Teeces. ADR 0045's reason for keeping the raw families forwarding
-- *"a builder's bindings are worth more than our internal tidiness"* -- points
directly at this.

**Padawan360 shows a third shape entirely**: logic displays over **I2C to address
10**, `triggerI2C(10, n)` with effect codes 0/1/4/5/6/10/11 and 21-25. Neither of
our members uses it. Recorded so nobody rediscovers it as an option.

## 13. Availability (2026-09-10)

Actively produced by one vendor, observed live:

| Product | Price | Page |
| --- | --- | --- |
| Teeces v4 | EUR 30,00 base | https://shop.printed-droid.com/produkt/teeces-v4/ |
| Teeces32 | EUR 30,00 base | https://shop.printed-droid.com/produkt/teeces32/ |

Three tiers each -- PCBs only, KIT (all components plus a pre-programmed Arduino),
or PNP (assembled). Regular or Slim. Ships from Germany.

Bare PCBs remain available as open hardware: OSH Park 5-board set, USD 35.00,
made to order.

The **original kit run ended in 2013** and the originator said so plainly, 3/4/19:
*"I won't be supplying any more Teeces parts. All Teeces components can be sourced
online from various vendors ... a full system should cost under $150 to put
together."* The design survived because the board files are open. No MAX7219
supply problem exists; the historical constraint was the volunteer group-buy
model, not the chip.

## 14. Mechanical

Board dimensions, from the designer's own OSH Park shared projects, all 2-layer:

| Board | Inches | mm | Uploaded |
| --- | --- | --- | --- |
| RLD v3.1 | 4.98 x 1.70 | 126.4 x 43.0 | 2012-05-30 |
| FLD7 | 1.78 x 1.38 | 45.1 x 35.0 | 2012-05-30 |
| PSI v3.2 | 1.89 x 1.89 | 47.9 x 47.9 | 2012-09-19 |
| PSI v3.4 | 1.88 x 1.88 | 47.8 x 47.8 | 2019-06-09 |

The RLD's 5:1 aspect ratio and 5 x 27 grid match the screen-used rear logic
surround being one wide panel; the near-square FLD matches two stacked short ones.

**PSI mounting:** *"PSI diffusers & boards fit standard 1 1/2" PVC DWV Couplings,
found at any US hardware store."*

Included CNC-cut parts: black acrylic logic bezels, non-glare clear acrylic logic
screens, white Lexan PSI diffusers. Printed Droid additionally ships STL bezel
files.

Holoprojector LEDs are not a Teeces board -- they attach via 2-pin headers
soldered to the back of the PSI boards, fed by the 2-pin cables in the BOM.

## 15. Agent Lookup Quick Reference

Use this table first when implementing or reviewing Teeces behaviour.

- Field: Wire framing. Required value: `<address><letter>[<argument>]\r`.
- Field: Leading `@`. Required value: **absent on the wire**; it is a MarcDuino
  routing character stripped by the slave.
- Field: Terminator. Required value: `0x0D` (CR). `\n` is not a terminator.
- Field: Serial parameters. Required value: 8N1.
- Field: Baud. Required value: firmware-dependent -- 2400 classic default, 9600
  Printed Droid v3 and Teeces32.
- Field: Address digits. Required value: one or two ASCII digits.
- Field: Address 0. Required value: global.
- Field: Address 1 / 2 / 3. Required value: TFLD / BFLD / RLD.
- Field: Address 4 / 5. Required value: front PSI / rear PSI.
- Field: Address 6 / 7 / 8. Required value: holoprojectors, accepted and ignored.
- Field: Implemented letters. Required value: `T`, `M`, `P`, `R`, `S`, `D`.
- Field: `W` command. Status: **not implemented; returns BEL.**
- Field: `P91`. Status: accepted, no effect, no BEL.
- Field: Text display. Required value: `<a>M<text>\r` **then** `<a>T100\r`.
- Field: Text case. Required value: uppercase only.
- Field: Text length. Required value: 62 usable, 64 buffer.
- Field: Rejection response. Required value: single `0x07` BEL byte.
- Field: Normal response. Required value: character echo plus a verbose ack, about
  40 bytes per command. Not protocol; do not parse.
- Field: Minimum inter-command delay. Required value: 100 ms; 200 ms after `T92`;
  250 ms after `M`.
- Field: Command buffer. Required value: 64 bytes.
- Field: MAX7219 count. Required value: 7 (RLD 3, each FLD 1, each PSI 1).
- Field: Chains. Required value: two -- rear 4 devices, front 3 devices.
- Field: Chain connector. Required value: 5-pin 1:1, Plus / Minus / L / C / D.
- Field: Command link. Required value: one wire, master TX to Teeces RX, plus
  common ground.
- Field: Supply. Required value: 5 V.
- Field: LED colour. Required value: **fixed at build time**, per hole.
- Field: Component Protocol. Required value: **JawaLite**.

If a required value above cannot be proven for the unit in hand, status is
`UNKNOWN` and implementation work depending on it should stop pending
clarification.

## 16. Open Items

Things this sheet states from documentation or source but that no one has
confirmed on protoArtoo's bench.

1. **Current draw per board and per kit.** No published source exists. Needs bench
   measurement at a stated brightness, or a datasheet calculation from the Iset
   values in Section 5 plus lit-segment count. This is the only genuinely missing
   electrical number.
2. **Which baud the unit in hand runs.** Section 9.7 -- it is a firmware constant
   and all three values are in the wild. A Teeces bought today from Printed Droid
   is 9600, which matches protoArtoo's dome lane; a second-hand board running the
   CuriousMarc default is 2400 and would need reflashing or a lane change.
   **Confirm per unit; do not assume.**
3. **Whether the original Vannoy sketch accepts serial at all.** Section 7. Not in
   any public repository. Settled by the astromech.net Teeces V3 wiki page.
4. **FLD7 versus FLD11.** The OSH Park set lists "FLD11"; the designer's own shared
   project for the same era is named "FLD7". Two revisions or two names for one
   board is `UNKNOWN`. Settled by the Eagle board files.
5. **Hardware licence.** "Open hardware" is asserted; no licence identifier is
   stated anywhere, and `PrintedDroid/Teeces-ESP32` has no licence file. Matters
   only if protoArtoo ever vendors code or board artefacts.
6. **Dome-make compatibility.** No source certifies fit against specific dome
   makes. Boards are dimensioned to the standard blueprint logic surrounds and
   Printed Droid ships bezel STLs, which implies fit is bezel-mediated rather than
   universal. Settled by the login-walled astromech.net dome build threads.
7. **Authoritative JEDI `W`, `K`, `O` semantics.** `W` is emitted by MarcDuino and
   rejected by Teeces; `K` and `O` are wiki-documented and implemented nowhere
   public. Settled by Scott Gray's *JawaLite 2.0 Programmers Reference Manual*
   (`JawaLite2_0_X12.pdf`), which was not retrievable this session.
8. **"Stealth Controller" attribution.** No repository found under the name.
   Excluded from Section 12 rather than guessed.
9. **Whether protoArtoo drives Teeces across protoR2link or a dome-side board does.**
   Section 11's note. This is a design decision for #313, not a fact to look up.

Names for which **no evidence of being real Teeces products was found**, and which
should not appear in project copy: "Tcarrier", "Teeces Deluxe", "Teeces Micro",
"Teeces RGB", "Teeces v1.2 hardware" (v1.2 is a CuriousMarc *firmware* version).

## 17. Sources

**Normative (primary): firmware source**

- CuriousMarc Teeces sketch v1.3, vendored in Reeltwo:
  https://github.com/reeltwo/Reeltwo/blob/master/src/dome/TeecesLogics.h (lines 758-3247)
- Teeces sketch distribution zips (v1.4 "Ben", Printed Droid v3):
  https://www.printed-droid.com/wp-content/uploads/2020/01/Teeces_CuriousMarc_v1.4_Ben.zip
  https://www.printed-droid.com/wp-content/uploads/2021/11/Teeces_PrintedDroid_v3.zip
- `TeecesControl` Arduino library:
  https://www.printed-droid.com/wp-content/uploads/2020/01/TeecesControl_arduinolibrary.zip
- Teeces32 firmware: https://github.com/PrintedDroid/Teeces-ESP32
- MarcDuino master: https://github.com/nhutchison/MarcDuinoMain
- MarcDuino slave (the `@`-strip): https://github.com/nhutchison/MarcDuinoClient
- Reeltwo library: https://github.com/reeltwo/Reeltwo
  (`docs/JawaLite.dox`, `src/core/JawaEvent.h`, `src/core/Marcduino.h`,
  `src/dome/TeecesLogics.h`, `src/dome/TeecesPSI.h`,
  `examples/teecesLogics/teecesLogics.ino`)

**Normative (primary): product and protocol documentation**

- V3.2 Kit Sheet, Sept 2012:
  http://joymonkey.com/run/files/V3.2%20Kit%20Sheet%20Sept%202012.pdf
- MarcDuino command reference:
  https://www.curiousmarc.com/r2-d2/marcduino-system/marcduino-software-reference/marcduino-command-reference
- Original JEDI JawaLite reference:
  https://www.curiousmarc.com/r2-d2/marcduino-system/marcduino-software-reference/marcduino-command-reference/original-jedi-jawalite-reference
- Teeces Dome Lights, CuriousMarc:
  https://www.curiousmarc.com/r2-d2/teeces-dome-lights
- Teeces wiring, marcduino.com: https://marcduino.com/?page_id=119
- Printed Droid KB, Teeces V3 / V4 / 32:
  https://www.printed-droid.com/kb/teeces-v3-dome-lighting-system/
  https://www.printed-droid.com/kb/teeces-v4-dome-lighting-system/
  https://www.printed-droid.com/kb/teeces-32-dome-lighting-system/
- OSH Park board set and shared projects:
  http://store.oshpark.com/collections/droid-parts/products/teecees-r2d2-v3-1-5-board-set
  https://oshpark.com/profiles/JoyMonkey
- End-of-production statement: http://joymonkey.com/run/index.php?pg=tools
- MAX7219/MAX7221 datasheet Rev 4, 7/03:
  https://www.analog.com/media/en/technical-documentation/data-sheets/MAX7219-MAX7221.pdf
- Scott Gray, *JawaLite 2.0 Programmers Reference Manual* (`JawaLite2_0_X12.pdf`,
  jedicontrol.com) -- **not retrieved**; cited as upstream authority by Reeltwo.

**Ecosystem survey (primary source, non-normative for this product)**

- https://github.com/knightshade23/SHADOW
- https://github.com/nhutchison/shadow_md_q85
- https://github.com/reeltwo/PenumbraShadowMD
- https://github.com/ProtocolCosplay/Shadow-RC
- https://github.com/dankraus/padawan360
- https://github.com/nhutchison/LogicEngine
- https://github.com/joymonkey/logicengine
- https://github.com/dpoulson/r2_control (`Hardware/Lights/TeeceesControl.py`)
- https://github.com/reeltwo/AstroPixelsPlus

**Community (unverified, blog or login-walled)**

- astromech.net forum threads and droidwiki pages on Teeces -- **login-walled, not
  read.** Cited by several secondary sources; every such citation in this sheet's
  research is therefore unverified.
- Build blogs: 2geekswebdesign.com, r2d2ireland.com, boda-r2.blogspot.com,
  astromechr2.wordpress.com.
