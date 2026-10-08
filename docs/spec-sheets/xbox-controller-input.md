# Xbox Controller Spec Sheet (XInput, GIP and Bluetooth HID)

The **Xbox controller** family spans four input protocols: Xbox 360 wired USB
(XInput), Xbox 360 wireless through Microsoft's USB receiver, Xbox One and Series
over USB (GIP), and Xbox One S and Series over Bluetooth. On a droid that gives
three arrangements: **wireless with a USB dongle**, **wired**, and **Bluetooth**
direct to the controller. The astromech hobby's Xbox droids are built on
Padawan360's Xbox 360 receiver arrangement.

Research date 2026-09-10. Every report byte, descriptor field, VID/PID and
default in this document was read from Microsoft's own published specification,
from library source, from Espressif's per-target documentation, or from the
astromech projects that use these controllers. Claims that could not be sourced
are marked `UNKNOWN` with the artefact or bench test that would settle them.

> [!CAUTION]
> **Two findings shape any use of these controllers.**
>
> 1. **No single ESP32-family chip in common droid use carries every path.** The
>    classic ESP32 has **Bluetooth Classic** and **no USB host at all**. The
>    ESP32-P4 has a **USB host** and **no radio of its own**, and the ESP32-C6
>    companion it is commonly paired with is BLE-only. The wired and dongle
>    paths need a USB host; the Bluetooth Classic path needs a BR/EDR radio.
>    Section 4.
> 2. **Which Bluetooth a controller speaks depends on its firmware version, not
>    its model.** The same physical model 1708 pad is Bluetooth Classic on
>    firmware 4.8 and BLE on firmware 5.15. A builder can move it either way, and
>    Bluepad32 documents both the update and the *reversion* procedure. Section
>    5.4.

## 1. Scope

Covers the four Xbox input protocols, the controller generations and what can
still be bought, the USB descriptors and report layouts, pairing and
loss-of-signal behaviour, which ESP32-family chip can carry which path, the host
libraries, and what the astromech hobby actually does.

Does not cover: rumble and LED output beyond what identifies a controller,
headset and audio (GIP carries an audio data class a droid has no use for), the
Xbox Adaptive Controller's accessory ports, controller firmware update
mechanics beyond the fact that they change the transport, or third-party
"Xbox-compatible" pads except where their VID/PIDs are already in library
source.

## 2. Products Covered

| Product | Model | Transports it offers | Status 2026-09-10 |
| --- | --- | --- | --- |
| Xbox 360 wired | -- | USB (XInput) | **discontinued**, second-hand only |
| Xbox 360 wireless + Wireless Gaming Receiver | -- | proprietary 2.4 GHz to a USB dongle | **discontinued**, second-hand only |
| Xbox One (original) | 1537 | USB (GIP), Xbox Wireless | discontinued; **no Bluetooth** |
| Xbox One S | 1708 | USB (GIP), **Bluetooth**, Xbox Wireless | second-hand; the pivotal model |
| Xbox Series X\|S | 1914 | USB-C (GIP), **Bluetooth LE**, Xbox Wireless | **current, in production** |
| Xbox Elite Series 2 | -- | USB-C (GIP), Bluetooth, Xbox Wireless | current, premium |
| Xbox Adaptive Controller | -- | USB (GIP), Bluetooth | current |

> [!IMPORTANT]
> **The hobby's standard part is the one you cannot buy.** Padawan360 is built
> around the Xbox 360 wireless controller and the Microsoft Wireless Gaming
> Receiver, and both were discontinued with the 360 line. Its README already
> warns builders off the clones -- *"I highly recommend buying 1st party
> official Microsoft receiver. Your mileage may vary with off-brand components
> here. If you are having problems pairing... and you are not using an official
> receiver, it's likely the culprit."*
>
> **The current-production pad is the Series X|S (1914), over Bluetooth LE**,
> which is not the arrangement any droid project uses today.

> [!NOTE]
> **A wireless 360 controller plugged in by USB does not work as a wired one.**
> `USB_Host_Shield_2.0` records the PID with a comment: `#define
> XBOX_WIRELESS_PID 0x028F // Wireless controller only support charging`. The
> wired controller is a different part, PID `0x028E`.

## 3. Sources Checked

| Source | URL or path | Extraction notes |
| --- | --- | --- |
| **[MS-GIPUSB]**, v20240916 | https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-gipusb/e7c90904-5e21-426e-b9ad-d82adeee0dbc | **Microsoft's own GIP specification**, published 2024-09-16, revision 1.0. PDF fetched and extracted: the interface descriptor triple, the message header, the data classes, the 4 ms polling rate |
| `felis/USB_Host_Shield_2.0`, vendored copy | `padawan360/libraries/USB_Host_Shield_20.zip`, extracted | **The version Padawan360 actually ships.** `XBOXRECV.cpp` and `XBOXUSB.cpp` read in full: VID/PIDs, both report layouts, the connect/disconnect packet, the 3-second status poll |
| `felis/USB_Host_Shield_2.0`, upstream | https://github.com/felis/USB_Host_Shield_2.0 | Adds `XBOXONE` (wired GIP) and `XBOXONESBT` (Bluetooth Classic via `BTHID`) that the vendored copy predates; Xbox One VID/PID table |
| Bluepad32 `supported_gamepads.md` | https://github.com/ricardoquesada/bluepad32/blob/main/docs/supported_gamepads.md | **The firmware-version finding.** Model 1708 at v3.1/v4.8 is BR/EDR and at v5.15+ is BLE; model 1914 is v5.15+ BLE only |
| Bluepad32 `README.md` | https://github.com/ricardoquesada/bluepad32 | Supported chips (ESP32-P4 absent), the "subset only" caveat, and the BTstack licensing position |
| Espressif ESP-USB host docs | https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html | *"ESP32-P4 has two USB 2.0 OTG controllers: one High-Speed and one Full-Speed. Each controller can operate as a USB host independently"*; 16 host channels; interrupt transfers supported |
| Espressif per-target docs probe | (method in Section 4.1) | USB host page exists for esp32s3 and esp32p4, **404 for esp32 and esp32c6**. Classic-BT GAP page exists for esp32, **404 for esp32c6 and esp32p4** |
| ESP Component Registry | first-party MCP search | `espressif/usb_host_hid` exists; **no Xbox, XInput or GIP component exists** |
| **Padawan360**, canonical | `dankraus/padawan360`, cloned | The design rationale, the components list, the pairing procedure, and the disconnect handler quoted in Section 8 |
| Padawan360 controller guide | `padawan360/xbox360-controller-guide.jpg`, **read visually** | The complete 40-action control grammar, and its own two notes about signal loss and the LED ring |
| Padawan360 Flipsky fork | `Imperiallandm/Padawan360_mega_maestro_DYSV5W`, read from source | The same Xbox stack on a Mega, plus the `isLeftStickDrive` v2.0 change |
| **ShadowMD** | `RealNobser/ShadowMD`, read from source | The contrast case: PS3 Move Navigation over a Bluetooth dongle, and **the best failsafe ladder in the hobby** (Section 8.4) |
| R2-D2 Astromech Simulator | read from source | Models wired-vs-wireless as one catalogue row with a sub-variant: *"one #include and one object name in the sketch"* |

Two web research passes ran alongside this. Their protocol claims were checked
against source before use, and two did not survive:

- *"The 360 wireless host does not receive an explicit disconnect packet; the
  connection simply stops receiving reports"* -- **contradicted by
  `XBOXRECV.cpp`**, whose `readReport()` opens with *"This report is send when a
  controller is connected and disconnected"* and handles it as report type
  `0x08` (Section 5.2).
- *"Padawan360 has no explicit safety mechanism; it relies on motor stall
  detection and user re-engagement"* -- **contradicted by the sketch**, which
  zeroes both motor controllers on disconnect under a comment reading *"set all
  movement to 0 so if we lose connection we don't have a runaway droid!"*
  (Section 8.3).

A third was self-contradictory and is resolved here from Espressif's own docs:
one pass reported the P4 as needing *"an external USB dongle + SPI MAX chip"*
while also correctly reporting its native USB host. **The P4 needs no MAX3421E**
(Section 4.2).

## 4. Which chip can do what

The method is stated before the result.

### 4.1 The evidence method, and its limits

Espressif builds the ESP-IDF documentation per target from the SoC capability
headers, so a peripheral's API page exists for exactly the chips that have the
peripheral. Probing for the page is therefore a cheap, checkable capability
test. Results, 2026-09-10:

| Chip | `esp-usb .../usb_host.html` | `esp_gap_bt.html` (Bluetooth Classic) |
| --- | --- | --- |
| ESP32 (classic) | **404** | **200** |
| ESP32-S3 | 200 | -- |
| ESP32-C6 | **404** | **404** |
| ESP32-P4 | **200** | **404** |

This is strong evidence but it is not a datasheet: a 404 proves the API is not
documented for that target, and the SoC caps are the reason, but a reader who
needs the primary form should take it from each chip's datasheet feature list.
The P4 row is independently confirmed by the quoted ESP-USB text, and the P4's
lack of any radio is already established in
[the FireBeetle 2 sheet](firebeetle2-esp32-p4-spec-sheet.md).

### 4.2 The result, which is the opposite way round from expectation

| | **Classic ESP32** | **ESP32-P4** (with an ESP32-C6 companion) |
| --- | --- | --- |
| Bluetooth Classic (BR/EDR) | **yes** | no -- no radio on die |
| Bluetooth LE | yes | only via the C6 over ESP-Hosted |
| USB host | **no** | **yes -- two OTG controllers, HS and FS** |
| Xbox 360 wired | no | **yes** |
| Xbox 360 wireless dongle | no | **yes** |
| Xbox One/Series wired (GIP) | no | **yes** |
| Xbox 1708 on firmware < 5 (BR/EDR) | **yes** | no |
| Xbox 1914 / 1708 on firmware 5.15+ (BLE) | yes | `UNKNOWN` -- see below |

> [!IMPORTANT]
> **Neither chip carries every path, and the newer chip is the one that cannot
> do Bluetooth.** The P4 brings a USB host -- which is exactly what Padawan360
> needs an add-on shield for. What it does not bring is a radio.

**The P4's USB host needs no add-on hardware.** Espressif's own words: *"ESP32-P4
has two USB 2.0 OTG controllers: one High-Speed and one Full-Speed. Each
controller can operate as a USB host independently, so either one alone or both
together can function as USB hosts simultaneously."* The stack supports
interrupt transfers, which is what every Xbox report uses, and 16 host channels.
Padawan360's MAX3421E shield exists because an Arduino Mega has no USB host; a
P4 does.

**Whether Bluetooth HID host works over ESP-Hosted is the biggest `UNKNOWN`
here.** A P4 reaches BLE through the C6 over SDIO with
`CONFIG_ESP_HOSTED_ENABLED=y`. Whether that link exposes a full **HID host**
role -- GAP scanning, bonding, GATT client, HID report parsing -- rather than
Wi-Fi plus a BLE peripheral role, is not documented either way in what was read.
Open Item 1, with the test that would settle it.

> [!WARNING]
> **The C6 is BLE-only, so a Bluetooth Classic Xbox controller can never reach a
> P4 through it**, regardless of how good the ESP-Hosted bridge turns out to be.
> That is a silicon fact about the companion chip, not a software gap.

## 5. The protocols

There are four, and the single label *"Bluetooth HID"* names only one of them.

### 5.1 Xbox 360 wired USB (XInput)

Not a HID device. The interface is **vendor-specific (`bInterfaceClass 0xFF`)**,
which is what stops a HID driver claiming it and hands it to Microsoft's own
driver instead.

| | |
| --- | --- |
| VID | `0x045E` Microsoft |
| PID | `0x028E` wired controller |
| Other VIDs in library source | `0x1BAD` Mad Catz, `0x162E` Joytech, `0x0E6F` Gamestop/Afterglow |
| Report | 20 bytes, interrupt IN |

Report layout, read from `XBOXUSB.cpp`:

- Bytes `0,1` are the header and must be `0x00, 0x14` -- *"the controller also
  sends different status reports"*, which the parser drops.
- Buttons: bytes `2..5` assembled as a big-endian `uint32`.
- Sticks: little-endian `int16` pairs at `6/7`, `8/9`, `10/11`, `12/13` --
  LeftHatX, LeftHatY, RightHatX, RightHatY.
- The analog triggers live in the low two bytes of that button word, which is
  why the library treats L2 and R2 as *"special as they are analog buttons"* and
  computes their click state separately.

### 5.2 Xbox 360 wireless through the Wireless Gaming Receiver

The dongle is the USB device; the controllers are behind it.

| | |
| --- | --- |
| VID / PID | `0x045E` / `0x0719` official; `0x0291` third-party |
| Controllers per dongle | **4** -- four IN and four OUT pipes |
| Endpoint packet size | 32 bytes |
| Over-the-air | proprietary Microsoft 2.4 GHz, **not Bluetooth**, undocumented |

**The report is the wired report with a four-byte prefix.** Buttons move from
bytes `2..5` to `6..9` and the sticks from `6..13` to `10..17`. That is the whole
difference, and it is why the simulator can describe the swap as *"one #include
and one object name in the sketch"*.

> [!IMPORTANT]
> **The dongle signals connect and disconnect explicitly.** From
> `XBOXRECV.cpp`, above the handler: *"This report is send when a controller is
> connected and disconnected."* Report type `0x08`, with the second byte
> carrying `0x80` controller, `0x40` headset, `0xC0` both, and `0x00` for gone.
>
> That is the single strongest argument for the dongle arrangement on a droid.
> It is also, per Section 6.3, not a complete answer.

The library additionally polls a status and battery request to all four slots
**every 3 seconds** (`checkStatus()`), which is a keepalive-shaped behaviour
rather than a documented protocol keepalive.

### 5.3 Xbox One and Series over USB (GIP)

This is the best-documented Xbox protocol, because Microsoft published it.
**[MS-GIPUSB], revision 1.0, 2024-09-16**, with an explicit grant: *"you can make
copies of it in order to develop implementations of the technologies that are
described in this documentation."*

The interface descriptor, quoted from the specification:

| Field | Value | Meaning |
| --- | --- | --- |
| `bInterfaceNumber` | `0x00` | Interface 0 |
| `bNumEndpoints` | `0x02` | one interrupt IN, one interrupt OUT |
| `bInterfaceClass` | **`0xFF`** | Vendor specific class |
| `bInterfaceSubClass` | **`0x47`** | GIP Subclass |
| `bInterfaceProtocol` | **`0xD0`** | GIP Protocol |

Both endpoints are 64-byte capable and *"Polling interval / rate is up to 4 ms /
250 Hz."*

The GIP message header, single-packet form:

| Offset | Field | Notes |
| --- | --- | --- |
| 0 | MessageType | bits 7:5 data class, bits 4:0 message number |
| 1 | Flags | includes the System bit |
| 2 | Sequence ID | wrapping counter; `0x00` reserved |
| 3 | Payload Length | bit 7 = extend into the next byte; bits 6:0 = length |

Data classes: `000` Command, `001` Low Latency Data, `010` Standard Latency
Data, `011` Audio Data. A droid wants Low Latency Data and nothing else.

> [!NOTE]
> **`FF/47/D0` is the triple a host driver matches on**, and it is not
> `03/00/00`. `espressif/usb_host_hid` claims HID-class interfaces, so it will
> not bind a GIP controller. A GIP host driver on ESP-IDF is a custom class
> driver over the USB Host Library -- which is precisely the case Espressif names
> for using that library directly.

### 5.4 Xbox One S and Series over Bluetooth

The transport is decided by **controller firmware version**, and Bluepad32
documents it per model:

| Model | Firmware | Transport |
| --- | --- | --- |
| 1708 (and 1797) | v3.1, v4.8 | **Bluetooth Classic (BR/EDR)** |
| 1708 | v5.15 or newer | **Bluetooth LE** |
| 1914 | v5.15 or newer | **Bluetooth LE** |
| Adaptive Controller | v5.15 or newer | Bluetooth LE |
| 1537 (Xbox One original) | any | **none -- no Bluetooth radio** |

Bluepad32 links Microsoft's instructions for both **updating** and **reverting**
controller firmware, which means a builder can move a 1708 between the two
transports deliberately -- and can arrive at a droid having moved it by
accident, because a controller updates itself when plugged into a console or the
Xbox Accessories app.

> [!CAUTION]
> **This is the support problem in the Bluetooth path.** Two builders with
> identically-labelled controllers can need two different radios. On a classic
> ESP32, which has both BR/EDR and BLE, that is survivable. On any BLE-only path
> it is a hard incompatibility that presents to the operator as *"my controller
> will not pair"*, with nothing on the droid able to explain why.

The Bluetooth HID report descriptor is **not published by Microsoft**. The
working references are `xpadneo` (Linux) and `XBOXONESBT` in
`USB_Host_Shield_2.0`, which reaches it through `BTHID` -- that is, **Bluetooth
Classic HID over L2CAP**, not BLE.

### 5.5 Xbox Wireless (the proprietary adapter)

The 2.4 GHz protocol used between a controller and the Xbox Wireless Adapter is
**not documented by Microsoft** and is not Bluetooth. GIP rides over it, but the
over-the-air layer is closed. `UNKNOWN` and out of reach: nothing should plan
around it.

## 6. Pairing, reconnection and loss of signal

How a gamepad disconnect reaches a droid's failsafe is the question to design
against.

### 6.1 Pairing

| Arrangement | Procedure | Survives power cycle |
| --- | --- | --- |
| 360 + dongle | press the dongle button, then the controller's sync button | yes |
| Bluetooth | hold the pair button 3-4 s until the Xbox button flashes fast | yes, standard bonding |
| Wired | none | n/a |

Padawan360's README documents the dongle dance verbatim, ending *"they should
sync up and the blinking pattern on the controller will change and swirl."*

### 6.2 Auto power-off

Xbox controllers power themselves off after an idle period -- widely reported as
**15 minutes**, and adjustable only from a console's own settings, which a droid
builder may not have. For a droid this is a feature, not a fault: a controller
left on a table disarms itself. But it means **an idle droid will see a
disconnect it did not cause**, and a failsafe has to treat that as normal rather
than as an error state.

### 6.3 What actually happens on signal loss

| Arrangement | Explicit event? | How fast |
| --- | --- | --- |
| 360 + dongle | **yes** -- report `0x08`, second byte `0x00` | as soon as the dongle notices; `UNKNOWN` in ms |
| Dongle unplugged | **yes** -- USB device gone; the library's `Release()` clears `XboxReceiverConnected` and all four slots | USB enumeration timescale |
| Bluetooth | **yes** -- a link-supervision timeout raises a disconnect | seconds, not milliseconds |
| Wired | **yes** -- USB disconnect | immediate |
| **Batteries pulled mid-drive** | `UNKNOWN` | `UNKNOWN` |

> [!CAUTION]
> **The last row is the one that matters, and no source read answers it.** Every
> documented disconnect path is graceful. What a droid must survive is the
> ungraceful one -- a controller dropped in a crowd, a battery door knocked open,
> a pack going flat under load. Whether the 360 dongle emits a `0x08` in that
> case or simply stops producing reports is not stated in the library, and
> Bluetooth's answer is a supervision timeout measured in seconds.
>
> **So a host cannot wait to be told.** It needs its own
> last-report-received watchdog, as ShadowMD has (Section 8.4), and the
> explicit disconnect event is an optimisation on top of it that makes the
> common case fast.

### 6.4 The timescales

| Signal | Timeout |
| --- | --- |
| ShadowMD's PS3 stop threshold | 300 ms |
| ShadowMD's PS3 force-disconnect threshold | 10 s |
| Bluetooth link supervision | typically seconds |
| Controller idle power-off | ~15 minutes |

Only a host's own watchdog works on a droid's stopping timescale; the link's
own timeouts are an order of magnitude slower.

## 7. Libraries

### 7.1 Bluepad32 -- the only credible Bluetooth answer

| | |
| --- | --- |
| Repository | https://github.com/ricardoquesada/bluepad32 |
| Licence | Apache 2.0 -- **but see below** |
| Chips | ESP32, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-H2, Pico W, Pico 2 W |
| **ESP32-P4** | **not listed** |
| Xbox support | models 1708, 1914, Adaptive, per Section 5.4 |

Its README carries the caveat that decides which chips it suits:
*"Original **ESP32**, **Pico W** and **Pico 2 W** support all listed
controllers. **ESP32-S3**, **ESP32-C3**, **ESP32-C6** and **ESP32-H2** only
support a subset."* The mechanism is Section 5.4's: the later chips are BLE-only,
so BR/EDR controllers fall out of the set. **The classic ESP32 is in the
full-support tier.**

> [!IMPORTANT]
> **Licensing is a real question, not a formality.** Bluepad32 is Apache 2.0,
> but it depends on BTstack, which is *"free to use for open source projects.
> But commercial for closed-source projects"*, and the README's guidance for
> ESP32 is *"You should contact BTstack people."* Whether a project can adopt
> it depends on how that project is distributed.

### 7.2 `felis/USB_Host_Shield_2.0`

The hobby's incumbent, and the source of every 360 protocol fact in this sheet.
It provides `XBOXUSB` (wired 360), `XBOXRECV` (dongle, 4 controllers), `XBOXONE`
(wired GIP), `XBOXONESBT` (Bluetooth Classic via `BTHID`) and `XBOXOLD`.

It is built around a **MAX3421E USB host controller on SPI**, because the AVR
boards it targets have no USB host. On a chip with a native USB host, such as
the P4, that chip is redundant hardware solving a problem the silicon already
solved. Its value there is as a **specification of the 360 protocols**, which is
genuine and which nothing else provides.

Note also that `XBOXRECV::Poll()` walks four IN pipes every call and runs
`checkStatus()` every 3 seconds -- an architecture that assumes it is driven
from a single-threaded `loop()`, not a periodic RTOS task.

### 7.3 The others

| Library | Verdict |
| --- | --- |
| `asukiaaa/XboxSeriesXControllerESP32_asukiaaa` | BLE-only, Series X only; reported to need BLE5 features the classic ESP32 lacks. `UNKNOWN` whether that is still true; not read |
| `espressif/usb_host_hid` | Real and first-party, but **cannot claim a vendor-specific interface**. Not applicable |
| `badgeteam/hid-host` | A HID report-descriptor gamepad decoder in the component registry. Same objection |
| Linux `xpad`, `xone`, `xpadneo` | Not usable as code, excellent as corroboration |

> [!NOTE]
> **The ESP Component Registry has no Xbox, XInput or GIP component.** Searched
> through the first-party registry tool. On ESP-IDF, the USB half is written
> rather than adopted.

## 8. How the hobby actually uses these (non-normative)

### 8.1 Why Padawan360 chose it, in its author's own words

From the sketch header:

> "Heavily influenced by DanF's Padawan code which was built for Arduino+Wireless
> PS2 controller... I was running into frequent disconnect issues with 4
> different controllers... I decided that PS2 Controllers were going to be more
> difficult to come by every day, so I explored some existing libraries out there
> to leverage and came across the USB Host Shield and it's support for PS3 and
> Xbox 360 controllers. **Bluetooth dongles were inconsistent as well** so I
> wanted to be able to have something with parts that other builder's could
> easily track down and buy parts even at your local big box store."

Three of those four reasons have since inverted. PS2 controllers were scarce;
the 360 receiver is now scarcer. Bluetooth dongles were inconsistent; Bluetooth
host stacks on an ESP32 are a supported, maintained library. And the local big
box store no longer stocks the part. **The reasoning was sound and the
conclusion has expired**, which is worth saying plainly rather than inheriting
the choice.

### 8.2 The control grammar, read from the project's own guide

Padawan360 ships `xbox360-controller-guide.jpg`. Read visually, it maps
**roughly forty actions onto one pad** using a modifier grammar:

| Control | Action |
| --- | --- |
| Right stick | drive -- forward, back, turn; **press = change drive speed** |
| Left stick | dome -- turn left, turn right; **press = enable/disable holoprojectors** |
| **Start** | **enable/disable movement** |
| Back | enable/disable auto mode |
| Home | turn controller on; **Home + L1 + R1 = turn controller off** |
| D-pad up/down + R1 | volume up/down |
| D-pad up/down + L1 | logic-light brightness |
| A / B / X / Y alone | a random track from a per-button range |
| A / B / X / Y + L1, L2 or R1 | a specific track, plus a logic-light or holoprojector effect |

Its own two notes on the sheet are the important ones:

> "Everything (including auto mode) is disabled if controller loses power or goes
> out of range."
>
> "LED ring around the Home button indicates speed setting."

The second means **the controller itself displays droid state**. Padawan360 sets
the LED ring to a rotating pattern when drive is disarmed and to LED1/2/3 for the
three speed presets.

### 8.3 The failsafe, which is real and better than reported

The whole of it, from `padawan360_body_uno.ino`:

```cpp
  // if we're not connected, return so we don't bother doing anything else.
  // set all movement to 0 so if we lose connection we don't have a runaway droid!
  // a restraining bolt and jawa droid caller won't save us here!
  if(!Xbox.XboxReceiverConnected || !Xbox.Xbox360Connected[0]){
    ST.drive(0);
    ST.turn(0);
    SyR.motor(1,0);
    firstLoadOnConnect = false;
    return;
  }
```

Two properties worth copying and one worth not:

- It checks **both** the dongle and the controller slot.
- Drive is **disarmed at boot** and armed with Start; `firstLoadOnConnect`
  re-arms the disarmed state on every reconnect, so a controller coming back
  never resumes driving. That is an arming sequence, in the hobby's own code.
- But it is **cooperative**: it only runs if `loop()` runs, and it depends on
  `Usb.Task()` having been called. A wedged USB stack produces no zeroing.

### 8.4 ShadowMD's ladder, which is the best failsafe in the hobby

ShadowMD drives PS3 Move Navigation controllers over a Bluetooth dongle -- a
third topology, doing the Bluetooth stack in AVR software. Its input supervision
is the most developed thing of its kind in the surveyed projects:

| Condition | Action |
| --- | --- |
| `msgLagTime > 300 ms` | stop motors, keep the controller |
| `msgLagTime > 10000 ms` | stop motors **and force-disconnect**, wait for reconnect |
| `badPS3Data > 10` | stop motors **and force-disconnect** |
| MAC not in the configured list | refuse the controller |

That last row deserves emphasis: **ShadowMD pins the controller by MAC address**
(`PS3ControllerFootMac`, plus a configured backup), so pairing alone is not
enough to drive the droid. For a machine driven in public, that is a meaningful
protection against another builder's controller in the same room, and no Xbox
project surveyed does anything equivalent.

### 8.5 Nobody has done this on an ESP32

No astromech project surveyed drives an Xbox controller from an ESP32. The
community's Xbox droids are Arduino Mega plus USB Host Shield, exactly as
Padawan360 documents; its Bluetooth droids are the SHADOW family on PS3
Navigation controllers. There is no ESP32 build to copy and no builder guide to
inherit.

## 9. Quick Reference

- Field: Xbox 360 wired VID/PID. Value: `0x045E` / `0x028E`.
- Field: Xbox 360 wireless controller over USB. Value: PID `0x028F`, **charging only, no input**.
- Field: Xbox 360 Wireless Gaming Receiver VID/PID. Value: `0x045E` / `0x0719`; third-party `0x0291`.
- Field: Controllers per 360 receiver. Value: **4**.
- Field: 360 wired report. Value: 20 bytes; header `0x00 0x14`; buttons bytes 2-5 big-endian u32; sticks int16 LE at 6/7, 8/9, 10/11, 12/13.
- Field: 360 wireless report. Value: same layout **offset by 4** -- buttons 6-9, sticks 10/11, 12/13, 14/15, 16/17; input reports carry `readBuf[1] == 0x01`.
- Field: 360 connect/disconnect notification. Value: `readBuf[0] == 0x08`; second byte `0x80` controller, `0x40` headset, `0xC0` both, `0x00` disconnected.
- Field: GIP interface descriptor. Value: **`bInterfaceClass 0xFF`, `bInterfaceSubClass 0x47`, `bInterfaceProtocol 0xD0`**.
- Field: GIP endpoints. Value: one interrupt IN and one interrupt OUT, 64-byte capable, polling up to **4 ms / 250 Hz**.
- Field: GIP message header. Value: MessageType, Flags, Sequence ID, Payload Length (bit 7 = extend).
- Field: GIP data classes. Value: `000` Command, `001` Low Latency Data, `010` Standard Latency Data, `011` Audio Data.
- Field: GIP specification. Value: **[MS-GIPUSB], revision 1.0, 2024-09-16** -- an official Microsoft open specification.
- Field: Xbox HID class. Value: **none of them are HID**. `espressif/usb_host_hid` cannot claim any Xbox controller.
- Field: Bluetooth transport, model 1708. Value: **BR/EDR** on firmware v3.1 / v4.8; **BLE** on v5.15 or newer.
- Field: Bluetooth transport, model 1914. Value: **BLE**, firmware v5.15 or newer.
- Field: Bluetooth, model 1537. Value: **none** -- that controller has no Bluetooth.
- Field: Bluetooth HID report descriptor. Status: **not published by Microsoft; UNKNOWN.** `xpadneo` and `XBOXONESBT` are the working references.
- Field: Xbox Wireless (proprietary adapter). Status: **undocumented; UNKNOWN.** Do not plan around it.
- Field: Classic ESP32. Value: **Bluetooth Classic and BLE; no USB host**.
- Field: ESP32-P4. Value: **USB host -- two USB 2.0 OTG controllers, one HS and one FS, 16 channels; no radio on die**.
- Field: ESP32-C6. Value: **BLE only; no Bluetooth Classic, no USB host**.
- Field: Bluetooth HID host over ESP-Hosted. Status: **UNKNOWN** -- Open Item 1.
- Field: Bluepad32 on ESP32-P4. Value: **not listed**.
- Field: Controller idle power-off. Value: about **15 minutes**, adjustable only from a console.
- Field: Disconnect on ungraceful loss. Status: **UNKNOWN** -- a host-side last-report watchdog is the only sure stop (Section 6.3).

## 10. Open Items

1. **Can a Bluetooth HID *host* role work over ESP-Hosted to the C6?** This
   decides whether an ESP32-P4 with a C6 companion can ever take a Bluetooth
   controller, and nothing read answers it. Settled by reading the ESP-Hosted HCI
   bridge's supported roles, or by attempting a BLE HID host scan on such a board
   and seeing whether it enumerates a controller.
2. **Does an ungraceful disconnect produce an event?** Section 6.3. Settled on a
   bench: pull the batteries from a paired controller mid-report and watch
   whether the dongle emits a `0x08`, and how long a Bluetooth link takes to
   drop.
3. **What is the 360 dongle's disconnect latency in milliseconds?** Not stated
   anywhere read. Same bench test, with a timestamp.
4. **Is `asukiaaa/XboxSeriesXControllerESP32_asukiaaa` still classic-ESP32
   incompatible?** Reported as needing BLE5 features the original ESP32 lacks;
   not read.

## 11. Sources

**Normative (primary): vendor specification**

- [MS-GIPUSB]: Gaming Input Protocol (GIP) Universal Serial Bus (USB) Extension,
  revision 1.0, 2024-09-16:
  https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-gipusb/e7c90904-5e21-426e-b9ad-d82adeee0dbc
- PDF read for this sheet:
  https://winprotocoldocs-bhdugrdyduf5h2e4.b02.azurefd.net/MS-GIPUSB/%5bMS-GIPUSB%5d.pdf

**Normative (primary): silicon documentation**

- ESP-USB Host Library, ESP32-P4:
  https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html
- Per-target documentation probe, method and results in Section 4.1
- [`docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md`](firebeetle2-esp32-p4-spec-sheet.md)
  for the P4's lack of a radio and the C6/SDIO topology

**Normative (primary): library source, where no specification exists**

- `felis/USB_Host_Shield_2.0`: https://github.com/felis/USB_Host_Shield_2.0
  (`XBOXRECV`, `XBOXUSB`, `XBOXONE`, `XBOXONESBT`, `xboxEnums.h`)
- The vendored copy Padawan360 ships, `padawan360/libraries/USB_Host_Shield_20.zip`
- Bluepad32: https://github.com/ricardoquesada/bluepad32 and its
  `docs/supported_gamepads.md`

**Ecosystem survey (primary source, non-normative)**

- https://github.com/dankraus/padawan360, including
  `xbox360-controller-guide.jpg` and `padawan360_body_uno.ino`
- https://github.com/Imperiallandm/Padawan360_mega_maestro_DYSV5W
- https://github.com/RealNobser/ShadowMD
- https://github.com/reeltwo/PenumbraShadowMD,
  https://github.com/ProtocolCosplay/Shadow-RC, https://github.com/reeltwo/Reeltwo
- R2-D2 Astromech Simulator

**Corroboration (reverse engineering, non-normative)**

- Linux `xpad`:
  https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c
- `medusalix/xone`: https://github.com/medusalix/xone
- `atar-axis/xpadneo`: https://atar-axis.github.io/xpadneo/
- `quantus/xbox-one-controller-protocol`, now superseded by [MS-GIPUSB]:
  https://github.com/quantus/xbox-one-controller-protocol

**Builder documentation**

- Padawan360's own README, components and pairing sections
- Xbox 360 Wireless Gaming Receiver support page:
  https://support.xbox.com/en-US/xbox-on-other-devices/connections/xbox-360-wireless-gaming-receiver-windows
- Astromech droid wiki, "Cheap XBox Receivers":
  https://astromech.net/droidwiki/Cheap_XBox_Receivers -- **referenced by
  Padawan360 but login-walled**, so its content is not relied on here
