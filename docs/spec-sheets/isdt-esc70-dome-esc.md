# ISDT ESC70 Spec Sheet (Dome Rotation, registry token `ledc`)

Working spec for the **ISDT ESC70** brushed electronic speed controller as the
**Dome Rotation** lineup member that ships today
([#391](https://github.com/mattiasbrandt/protoArtoo/issues/391), minted from
[#303](https://github.com/mattiasbrandt/protoArtoo/issues/303) and
[#316](https://github.com/mattiasbrandt/protoArtoo/issues/316)), driven over the
Component Protocol the registry calls `ledc` -- standard RC servo PWM from an
ESP32 LEDC channel.

Research date 2026-09-12. **This is a refresh, not a replacement.** The baseline
settings table, the curve values and the calibration checklist in Section 11 are
the operator's own tuning decisions, locked on the bench on 2026-03-21 and
carried forward here unchanged. What is new is everything around them: the
vendor's own manual, FAQ and app guide read first-hand, the running-mode trap
that makes one line of that baseline a correctness requirement rather than a
preference, the electrical hazards, what protoArtoo actually emits, and what our
own droid has proven.

Every specification below was read this session from ISDT's own manual PDF
(`ESC70说明书_210928B`, 14 pages, Chinese and English, **with** a text layer --
the tables were also rendered as images and read visually to confirm), from
ISDT's own FAQ and app menu guide, from this repository's driver, tests, config,
lessons file and bench record, or from the astromech projects on this disk.
Claims that could not be sourced are marked `UNKNOWN` with the artefact or bench
test that would settle them.

> [!CAUTION]
> **The factory default running mode reverses on the second push, not the first,
> and a droid controller has no second push.** After throttle calibration the ESC
> defaults to **Forward/Reverse with brake**, which uses *double-click reversing*:
> the first command into the reverse zone **brakes**, and only a command that
> returns to neutral and goes negative again reverses -- and only if the motor has
> already stopped. protoArtoo sends a signed speed straight to a pulse width, so
> in that mode a `-30 %` dome command **brakes instead of turning**.
>
> This is why Section 11's baseline says *Running mode: Forward and reverse*. It
> is not a preference. It is the only one of the three modes in which our control
> model is correct (Section 8).

> [!CAUTION]
> **The ESC's throttle lead is a power *output*, and the FireBeetle 2's middle
> column is 3.3 V.** The ESC70's 3-pin lead carries its BEC at **5.0-7.5 V**, and
> ISDT's manual says in so many words *"We Suggest DO NOT supply additional power
> to the receiver, otherwise your ESC may be damaged."* `docs/pin_map.md` says the
> opposing half: *"The middle column is `3V3`, not 5 V."*
>
> Plug the ESC's 3-pin lead onto a FireBeetle 2 row the obvious way and you put
> **5-7.5 V onto the board's 3.3 V plane**. Signal and ground only (Section 5.4).

> [!IMPORTANT]
> **A 70 A ESC on a dome is a deliberate over-spec, and the consequences are real
> rather than academic.** This part is specified for *"1/8 or 1/10 Various
> models"* with *"540/550/775 Brushed motor"* -- a class that draws tens of amps.
> A dome gearmotor draws single digits. Everything awkward about running it on a
> dome (breakaway at low command, the need for maximum Start Force, cogging in
> high-friction sectors) is the low-current end of a very large controller, and
> Section 5.3 is what to do about it.

## Where this sits in the lineup

The **Dome Rotation** category holds two products, and a builder picks one:

| Product | What it is | Status | Registry value | Protocol |
| --- | --- | --- | --- | --- |
| **ISDT ESC70** | brushed RC ESC, RC PWM in | **`supported`** | **11** | `ledc` |
| SyRen 10 | Dimension Engineering single-channel motor driver | `roadmap` | 12 | `de_packet_serial` |

These answer the same question in two different currencies. The ESC70 is driven
by **the pin protoArtoo already has**, needs no UART and no new code; the SyRen is
driven by a serial protocol that needs a UART artoo-esp32 does not have spare.
[`sabertooth-syren-packet-serial.md`](sabertooth-syren-packet-serial.md) Section
12.4 makes the same point from the other side: *"`PIN_DOME_ESC` ... already drives
an ISDT ESC70 over LEDC. A SyRen in R/C mode plugs into that pin with no new code
at all."*

It is a **Dome ESC** in `CONTEXT.md`'s vocabulary -- the thing that turns the dome
-- and not a **Dome Controller**, which is the dome's own board that protoArtoo
talks to over the slip ring.

## 0. Authority Contract

This document is an implementation authority for how protoArtoo drives the ESC70,
and a setup authority for how a builder configures it.

Authority order for agent decisions:

1. **The operator's locked baseline** (Section 11). Those settings were chosen on
   real hardware against a real dome ring on 2026-03-21 and recorded in
   `tasks/lessons.md`. A document does not overrule a bench.
2. **Measured behaviour of our own droid** -- the bench record in Section 13.
3. **ISDT's manual, FAQ and app menu guide**, for what the hardware is and does.
   Where the three disagree, Section 9.4 names the disagreements.
4. This document.
5. Astromech project practice (Section 14) -- evidence of what the hobby does,
   **not** evidence about this ESC. No astromech project on this disk drives one.

If references conflict:

- Prefer the bench over the manual for **tuning**; prefer the manual over the
  bench for **what a setting means**.
- Prefer the FAQ over the manual for **signal compatibility** -- it is the only
  document that states what the ESC rejects.
- Where a vendor document contradicts another vendor document, assert nothing:
  mark `UNKNOWN` and name the test. `Active brake enable` is the live example
  (Section 9.4).

Agent requirements when using this document:

- MUST NOT leave the ESC in the post-calibration default running mode. Set
  **Forward and reverse** (Section 8).
- MUST NOT send anything but a **1000-2000 us pulse at ~50 Hz**. The ESC rejects
  S.BUS, PPM, DSM2, DSMX and 750 us narrow PWM by the vendor's own statement
  (Section 6.1).
- MUST hold neutral for **two seconds** after the ESC powers up. That is not our
  invention -- ISDT's FAQ requires it (Section 7.3).
- MUST emit neutral, never a floating pin, on disable, estop, sleep and command
  timeout (Section 12.3).
- MUST NOT wire the ESC's BEC lead to a controller power rail (Section 5.4).
- MUST NOT assume the dome pin is a servo pin. It is an ESC pin, clamped
  1000-2000 us, and one comment in the codebase still calls the motor brushless
  (Section 16.1).
- MUST treat *"the ESC responds to throttle"* and *"the dome turns"* as two
  different claims. `tasks/lessons.md` has a whole entry about conflating them
  (Section 13.2).

## 1. Scope

Covers the part and its ESC90 sibling, the electrical and mechanical contract,
the battery window and the BEC hazard, the control signal and what the ESC
refuses, arming and throttle calibration, the three running modes and why only
one of them suits a droid, every ISD Go setting with what the vendor says about
it, the protections and how thin their documentation is, the operator's locked
baseline profile, exactly what protoArtoo emits and when, what our own droid has
proven, how the hobby drives a dome generally, how the two Dome Rotation members
differ, and the findings this research turned up against the shipping code.

Does not cover: the mechanical dome drive itself (ring, bearing, gear mesh,
slip ring); motor selection beyond naming the class; the ISD Go app's own UI
beyond its settings; ISDT's brushless ESC range; or the dome *controller* link
(`docs/topology.md` and the dome link spec own that).

## 2. What you are actually buying

**A 38.6 x 31.6 x 17.15 mm brushed car ESC**, about 49 g, with 16 AWG 200 mm
leads and **no connectors fitted**, plus a small two-in-one **electronic switch +
Bluetooth module** (about 4.5 g) on a short lead.

| | |
| --- | --- |
| Model | **ESC70** (the ESC90 is the same product with bigger numbers -- Section 2.2) |
| Manual | `ESC70说明书_210928B`, 14 pages, created 2021-09-28, Chinese pages 2-7 and English pages 8-14 |
| Motor types | *"540/550/775 Brushed motor"* -- **brushed only** |
| Application | *"1/8 or 1/10 Various models"* |
| Waterproofing | **IP65** per the vendor FAQ (*"no damage when rinsed with water"*); an IP67 version is described as future |
| In the box | ESC, switch/Bluetooth module. **No plugs** -- the builder solders everything |

> [!NOTE]
> **The switch module is not optional in practice, and the manual says so
> sideways.** Without it, *"the electronic speed controller will be TURNED ON when
> it detects the battery"* and calibration must be done blind with a transmitter.
> With it, the ESC boots into a low-power state, the button is the power switch,
> the LED is the only error indicator the hardware has, and **Bluetooth -- and
> therefore the entire ISD Go configuration surface -- exists only through it**.
> Every setting in Section 9 is reachable only through that module.

### 2.1 The part's identity is not in doubt

Unlike the DFPlayer Mini ([`dfplayer-mini-sound.md`](dfplayer-mini-sound.md)
Section 2), this is one manufacturer's part with one manual, one firmware line and
one app. No clone families, no chip-marking lottery. What it lacks is a *second*
source of truth: ISDT's three documents are all there is, they are thin in places
(Section 10.2), and nothing independent corroborates them.

### 2.2 ESC70 versus ESC90

One manual covers both, and the product page lists one difference:

| | ESC70 | ESC90 |
| --- | --- | --- |
| Continuous / peak current | **70 / 120 A** | 90 / 180 A |

*"All other specifications listed above apply identically to both models."*
For a dome, where neither number is the constraint, the ESC70 is the right half of
the pair; the ESC90 would be the same overkill with a bigger price.

### 2.3 Availability

Checked 2026-09-12: **readily available**, chiefly through AliExpress sellers and
ISDT's own channel, typically in the **USD 25-40** band. There is no single
authoritative retailer whose stock state matters; ISDT sells direct and lists a
support address and a phone number, and states a **one-year replacement warranty**
(*"quality issues receive replacements (no repairs)"*).

## 3. Project Integration

- **[`src/tasks/dome_task.cpp`](../../src/tasks/dome_task.cpp)** -- 305 lines. The
  only writer of dome speed. Arming, the 500 ms command timeout, estop and sleep
  handling, timed sequence rotation, and the random idle state machine.
- **[`include/dome_math.h`](../../include/dome_math.h)** -- `domeSpeedToPulseUs()`,
  the pure mapping from a normalised `-1.0 .. 1.0` speed to a pulse width, with
  asymmetric neutral trim and the speed-limit scale. Native-testable.
- **[`src/drivers/ledc_pwm.cpp`](../../src/drivers/ledc_pwm.cpp)** and
  **[`include/ledc_pwm.h`](../../include/ledc_pwm.h)** -- the LEDC timer at
  **50 Hz / 16-bit**, the per-channel clamp that holds `LEDC_CH_DOME` to
  **1000-2000 us**, `pulseUsToDuty()`, and `ledcPwmEmergencyStop()`.
- **[`include/config.h`](../../include/config.h)** -- `PIN_DOME_ESC` (**25** on
  artoo-esp32 at `:204`, **48** on firebeetle2 at `:324`, with the `VDD_IO_5` LDO
  caution).
- **[`include/config_store.h`](../../include/config_store.h)** /
  **[`src/config_store.cpp`](../../src/config_store.cpp)** -- `DomeConfig` and its
  defaults: `dome_neutral_us = 1500`, `dome_min_pulse_us = 1000`,
  `dome_max_pulse_us = 2000`, `dome_speed_limit_pct = 100`, plus the random-idle
  fields.
- **[`src/config_serializer.cpp`](../../src/config_serializer.cpp)** `:262-265` --
  every pulse field constrained to `1000..2000` and the speed limit to `0..100`
  on load, so a corrupt NVS value cannot drive the ESC out of range.
- **[`include/component_registry.inc`](../../include/component_registry.inc)**
  `:131` -- row **11**, `isdt_esc70`, *"ISDT ESC70 (RC ESC)"*,
  `COMPONENT_CATEGORY_DOME_ROTATION`, protocol `ledc`,
  `COMPONENT_STATUS_SUPPORTED`, capabilities **0**, no gate, `included = 1`.
- **[`include/servo_helpers.h`](../../include/servo_helpers.h)** `:88-110` --
  `servo_enabled_ledc_mask()`, where the dome bit is set unconditionally when the
  dome toggle is on (the AUX LED reservation never takes the dome channel).
- **[`data/dome.html`](../../data/dome.html)** -- the operator surface: the speed
  slider, the four ESC pulse fields, the random-movement block, and the note that
  *"ESC calibration, PWM frequency, and voltage cutoff are configured via the ISD
  Go mobile app over Bluetooth"*.
- **[`docs/pin_map.md`](../pin_map.md)** -- the dome rows on both boards, the
  artoo.uk *"Dome Servo"* label that is actually an ESC, and the FireBeetle 2
  `3V3`-not-5 V caution this sheet's Section 5.4 depends on.
- **[`docs/failsafe.md`](../failsafe.md)** -- DomeTask's place in the task table.
- **`test/test_native/test_dome_math/`** -- 14 tests over the pulse mapping.
- **`tasks/lessons.md`** -- two dome entries (Sections 13.2 and 16.3).
- **[`sabertooth-syren-packet-serial.md`](sabertooth-syren-packet-serial.md)**
  Section 12.4 -- the SyRen alternative, and why the R/C sub-choice is free.

## 4. Sources Checked

| Source | How it was taken | What it gave |
| --- | --- | --- |
| **ISDT ESC70/ESC90 user manual**, `https://www.isdt.co/down/pdf/ESC70.pdf` | fetched (200, 1.96 MB, 14 pages); **has** a text layer, and pages 12-14 were additionally rendered with `pdftoppm` and read as images to confirm the flowchart and the mode text | The full specification, the wiring rules, the throttle-calibration flowchart, the three running modes and the double-click behaviour, every ISD Go setting's meaning, the protections list, and the switch-module LED states |
| **ISDT ESC70 FAQ**, `https://www.isdt.co/esc70-faq.html` | fetched | **The only statement of what the ESC rejects** (no S.BUS/DSM2/DSMX/PPM/750 us), the 1500 W figure, IP65, the **two-second neutral rule**, the error-message list, Bluetooth range, and the warranty |
| **ISDT ESC70 app menu guide**, `https://www.isdt.co/english-esc70-app-menu-guide.html?lang=en` | fetched | The app's preset modes (On road / Drift / Off road / Rock crawler / Custom), the curve presets (Novice / Standard / Violent / Custom), and a statement about Active Brake that the manual contradicts (Section 9.4) |
| **ISDT ESC70 product page**, `https://www.isdt.co/esc70.html?lang=en` | fetched | The specification table, and the ESC70/ESC90 relationship |
| **This document's previous revision** | read on disk | The operator's locked baseline, the curve values and the calibration checklist -- **carried forward unchanged** as Section 11 |
| **protoArtoo firmware** -- `dome_task.cpp`, `dome_math.h`, `ledc_pwm.{h,cpp}`, `config.h`, `config_store.cpp`, `config_serializer.cpp`, `servo_helpers.h`, `component_registry.inc` | read this session | Sections 12 and 16 |
| **protoArtoo docs** -- `pin_map.md`, `failsafe.md`, `sound_playback.md`'s sibling sheets, `data/dome.html` | read this session | The board wiring, the operator surface, the cross-references |
| **`tasks/lessons.md`**, `tasks/phase3-tasks.md`, `tasks/phase4-tasks.md`, `tasks/phase4_hardware_validation_deferral.md` | read in full | The bench record, the locked baseline's provenance, and two defects this ESC's integration has already caused |
| `~/Documents/GitHub/ShadowMD` | read on disk | SyRen 10 packet serial, the `isDomeMotorStopped` command-throttling idiom, `serialLatency` |
| `~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W` | read on disk | `DOMESPEED = 80`, `DOMEDEADZONERANGE = 20`, `Syren10.autobaud()`, `setTimeout(950)` |
| `~/Documents/Astromech/BetterDuinoFirmwareV4`, `~/Documents/GitHub/AstroPixelsPlus`, `~/Documents/GitHub/CHIRP` | read on disk | Negative results: none of them drive a dome **ESC**; dome motion is SyRen/Sabertooth or nothing |

> [!NOTE]
> **Negative result, recorded so nobody repeats the search.** No astromech project
> on this disk, and no astromech source reachable this session, drives an ISDT
> ESC70. ISDT does not market it for droids, and the builder forums
> (`forums.astromech.net`, `droidwiki.astromech.net`) did not resolve. **The
> astromech evidence for this part is our own bench and nothing else** -- which is
> the opposite of the Sound family, where the hobby had decades of practice to
> draw on. Treat Section 14 as context, not corroboration.

## 5. Electrical

### 5.1 The specification, from the vendor's own table

| Parameter | Value (verbatim where quoted) |
| --- | --- |
| Continuous / peak current | **70 A / 120 A** |
| Maximum power | **1500 W on 3S** (FAQ) |
| Motor type | *"540/550/775 Brushed motor"* |
| Battery | *"2~3S Lipo or 6~8 Cell NiMH"* |
| BEC output | *"5V~7.5V adjustable (step by 0.1V)"*, **3 A continuous**, *"22.5W (7.5V * 3A)"* |
| Wire / connectors | *"16AWG-200mm/ Without Plug"* |
| Dimensions | **38.6 x 31.6 x 17.15 mm** (without wire) |
| Weight | *"about 49g/switch about 4.5g"* |
| Waterproofing | **IP65** (FAQ) |
| Maximum external temperature | **90 C / 194 F** -- *"Doing so may permanently damage your ESC and may also cause damage to your motor"* |
| Bluetooth range | **5 m** (FAQ) |
| Quiescent draw | Non-zero. *"If the battery is not disconnected, the ESC will continue to consume power"* even switched off |

### 5.2 The battery window is the first thing to check, and it is narrow

> [!CAUTION]
> **2-3S LiPo means 6.0-12.6 V. There is no 4S, 5S or 6S option, and no 24 V
> option.** A droid whose main pack is a hoverboard battery (**36 V nominal, 42 V
> charged**) will destroy this ESC instantly if the dome ESC is fed from it. So
> will a 4S pack.
>
> The dome ESC must be fed from a **2-3S / 12 V-class rail of its own**, or from a
> regulated 12 V step-down off the main pack. protoArtoo has nothing to say about
> this in software and cannot detect it: `docs/pin_map.md` records that the Artoo
> Controller PCB has *"no battery monitoring circuitry -- no voltage divider, no
> dedicated ADC trace"*. **Nothing in this project will warn a builder who gets
> this wrong.**

The low-voltage cutoff is set *"to automatic (according to the battery type) or
manually specified from 5.0V to 12.0V"*, which is consistent: the whole protection
range lives below 12.6 V.

### 5.3 70 A of controller for a few amps of dome, and what that costs

This ESC is sized for a 1/10-scale car pulling tens of amps through a 540-class
motor. A dome gearmotor of the class the previous revision named -- a JGB37-520 or
similar -- is a single-digit-amp load.

**Nothing about that is dangerous, and two things about it are awkward:**

1. **Low-command behaviour is the whole game.** All the useful resolution of a
   70 A controller sits above where a dome ever operates. The breakaway problem
   the bench found -- *"movement struggled in localized high-friction sectors"* --
   is a small motor asking a large controller for a small, precise amount of
   current. This is exactly why Section 11's baseline sets **Start Force to
   high/max** and **PWM frequency to 1 kHz**: both push torque into the low end,
   and the manual's own words support both (*"A lower driving frequency. Motor
   output will be stronger, the throttle will feel more punchy due to the higher
   volume of torque"*).
2. **Over-current protection will never fire on a dome.** A 120 A peak limit is
   unreachable with a dome motor, so a jammed dome is **not** protected by the
   ESC. It is protected -- if at all -- by the motor's own stall behaviour and by
   the operator noticing. Do not treat "the ESC has over-current protection" as a
   mechanical safety layer here. Open Item 7.

### 5.4 The BEC is an output, and that is a wiring hazard on both boards

The ESC's 3-pin throttle lead is **not** a passive signal input. ISDT:

> *"Please be reminded the ESC throttle control port has BEC voltage adjustment
> function to the receiver and the servo, We Suggest DO NOT supply additional
> power to the receiver, otherwise your ESC may be damaged."*

So the lead carries **signal, ground, and 5.0-7.5 V out**.

> [!CAUTION]
> **On FireBeetle 2 this is a board-killer in the most natural wiring.** The
> DFR1237 field is a row per GPIO with `IO` / `3V3` / `GND` columns, and
> `docs/pin_map.md` is explicit: *"The middle column is `3V3`, not 5 V."* It also
> notes that this plug-and-go ergonomics is *"one of the reasons the FireBeetle 2
> was chosen"* -- which is exactly what makes the mistake easy. Push the ESC's
> 3-pin lead onto row 48 and the BEC's 5-7.5 V lands on the 3.3 V plane.
>
> **Wire signal and ground only.** Cut, tape back, or pull the pin on the ESC
> lead's centre conductor. This is the same rule the pin map already states for
> servos -- *"Power servos and ESCs from a separate BEC, and bring only the signal
> wire and a common ground to this field"* -- with the direction reversed: here
> the ESC **is** the separate BEC, and it must not be allowed to feed back.

On **artoo-esp32** the answer is `UNKNOWN` and must not be assumed. The PCB's dome
header power pin may be unpowered, may be 5 V, or may be tied to the logic rail;
nothing in this repository records it, and `docs/pin_map.md` describes the board
only as *"a bare PCB with traces, pin headers, and DC terminals only"*. Open
Item 1 is a meter on that pin, and until it is taken, **wire signal and ground
only there too**.

> [!TIP]
> **The BEC is genuinely useful once it is not pointed at the controller.** 3 A at
> 5-7.5 V, adjustable in 0.1 V steps, is a real servo supply -- and servos are
> precisely what `docs/pin_map.md` says the FireBeetle 2's 3.3 V field cannot
> power. An ESC70 already in the droid can be the dome's and the arms' 5 V BEC.
> Set it to **5.0 V** before wiring anything to it; ISDT's own warning is
> *"Wrong BEC voltage setting may lead to damage to the servo or other electrical
> equipment."*

### 5.5 Motor wiring, polarity, and the two ways to reverse direction

Motor leads are **not** polarised: *"The two output wires of the ESC can be
connected to either of two wires of the motor at will."* If the dome turns the
wrong way, there are two fixes and they are equivalent -- *"the two motor wires
can be interchanged or change the direction of motor rotation can be adjusted via
the APP."*

**Battery leads are polarised and unprotected:** *"If the ESC is connected
reversely, your ESC will be damaged."* No reverse-polarity protection is claimed
anywhere in the three vendor documents.

> [!NOTE]
> **There is a third place direction can be flipped, and it is ours.** A builder
> can also swap `dome_min_pulse_us` and `dome_max_pulse_us` on the Dome page.
> Three independent inversion points -- motor leads, the app's Motor Rotation
> setting, and our pulse trim -- is two too many to reason about later. **Pick the
> app setting**, record it, and leave the other two alone; our trim exists for
> endpoint calibration (Section 7.4), not for direction.

## 6. The control signal

### 6.1 One protocol, and the vendor says so by listing what it refuses

ISDT's FAQ is the only document that states the negative, and it is unusually
direct:

> *"only supports 1000us~2000us standard PWM signals"* and *"does not support SUB,
> DSM2, DSMX, PPM signals and 750us narrow PWM signals."*

| | |
| --- | --- |
| Accepted | **Standard RC servo PWM, 1000-2000 us** |
| Rejected, by name | S.BUS (*"SUB"*), DSM2, DSMX, PPM, **750 us narrow PWM** |
| Neutral | 1500 us |
| Frame rate | `UNKNOWN` -- no vendor figure. 50 Hz is the RC standard and is what we emit (Open Item 3) |
| Logic level | `UNKNOWN` from any document -- but **3.3 V works on our unit** (Section 6.2) |

> [!NOTE]
> **"750 us narrow PWM" is worth understanding rather than skipping.** It is the
> half-width signalling some modern receivers and flight controllers emit, where
> the whole 1000-2000 us range is compressed to 500-1000 us. If a builder ever
> feeds this ESC from something other than protoArtoo -- a receiver output, a
> Maestro channel, another controller -- and it behaves as though every command is
> reverse, that is the first thing to check. protoArtoo never emits it:
> `ESC_PULSE_MIN_US`/`ESC_PULSE_MAX_US` are literally `1000`/`2000`
> (`include/ledc_pwm.h`), and `clampPulseWidth()` enforces them per channel.

**This single fact is also why the `ledc` protocol token is the right one.** An
ESC that speaks only servo PWM is reached by a PWM peripheral, and on ESP32 that
peripheral is LEDC. There is no driver to write and no wire protocol to get wrong
-- which is the whole reason this part is `supported` and the SyRen is `roadmap`.

### 6.2 The 3.3 V question, answered by our own bench rather than by ISDT

No ISDT document states the input's logic threshold. The wiring the manual assumes
is a hobby receiver, whose servo outputs are typically 5 V -- and the ESC powers
that receiver from its own BEC, so from ISDT's point of view the question never
arises.

**protoArtoo drives it from an ESP32 GPIO at 3.3 V, and it works.** The bench
record for 2026-03-21/22 is unambiguous:

> *"Dome ESC GPIO25 PWM path: spins at 50/70/90% command from web/API"*

and, from `tasks/lessons.md`:

> *"ISDT app throttle telemetry mirrored protoArtoo command percentages (PWM path
> alive)"*

The ESC's own app read back the throttle percentage our GPIO was commanding.
That is a **measured** answer to a specification the vendor does not publish, on
the artoo-esp32 board, on one unit. It has not been repeated on FireBeetle 2
(Open Item 2), where GPIO 48 additionally sits on the `VDD_IO_5` LDO rail that
`docs/pin_map.md` flags as an unquantified risk to edge quality.

### 6.3 What protoArtoo actually puts on the wire

| | |
| --- | --- |
| Peripheral | ESP32 **LEDC**, low-speed mode, **timer 0**, shared with the servo channels |
| Channel | `LEDC_CH_DOME = 2` |
| Frequency | **50 Hz** (`LEDC_FREQUENCY_HZ`), 20 ms period |
| Resolution | **16-bit** (`LEDC_DUTY_MAX = 65535`) |
| Duty conversion | `duty = pulseUs * 65535 / 20000`, 64-bit intermediate |
| Quantisation | **~0.305 us per count** -- the header calls it *"+/-1 count ... (~0.3us at 50Hz/16-bit)"* |
| Pin | `PIN_DOME_ESC` -- GPIO **25** (artoo-esp32) or **48** (firebeetle2) |
| Hard clamp | **1000-2000 us**, enforced in `clampPulseWidth()` for this channel |

> [!TIP]
> **0.3 us of quantisation is about 0.06 % of the control range**, which is far
> finer than a hobby receiver and far finer than the ESC's own deadband. Pulse
> resolution is not a dome tuning variable and never will be; if the dome moves in
> steps, look at the mechanics, the Start Force and the curve, not at the PWM.

## 7. Arming and calibration

### 7.1 Throttle calibration is mandatory, and it locks everything else

ISDT's own note, from the Chinese text of the manual:

> *"注：未进行油门行程校准时其他所有选项皆不能进行设置！"*
> -- until throttle travel calibration has been done, **no other option can be
> set at all**.

The app shows this as a red `!` on **Remote Calibration** at the top of the
configuration screen, and the FAQ lists `Throttle Not Calibrated` among its error
states. So the order of operations for a new ESC is fixed: calibrate first,
configure second. **Section 11's baseline cannot be applied to an uncalibrated
ESC.**

### 7.2 The calibration sequence, transcribed from the manual's flowchart

Read from the rendered page rather than the text layer, because it is a diagram:

```
Begin Throttle Calibration
  |
  +-- Factory Reset  -or-  Manual Calibration
        |
        v
  Power On:     throttle trigger to END position
                -> power ON
                -> return trigger to NEUTRAL when you hear "Beep"
        |
        v
  Trigger Neutral position:
                remain at NEUTRAL until you hear a single "Beep"
        |
        v
  Throttle Trigger to End Position:
                remain at full THROTTLE until you hear 2 beeps,
                then return to neutral
        |
        v
  Brake trigger to End Position:
                remain at full BRAKE until you hear 3 beeps,
                then return to neutral
        |
        v
  Calibration Complete:
                after you hear 2 beeps, RESTART for final completion
```

with the manual's own footnote:

> *"After the calibration is completed, the default operation mode is forward and
> reverse with brake function."*

and its warning:

> *"The throttle must be calibrated in strict accordance with the following
> sequence, otherwise the car may engage laggy response and may run reversely with
> the remote control directives."*

Before starting, ISDT requires the throttle source to be at its own defaults:
*"please adjust the throttle channel parameters of the remote control to the
default value and the midpoint of the throttle channel to 0."*

### 7.3 The two-second neutral rule is a vendor requirement, and our firmware already obeys it

> *"After each startup, return throttle to center position and maintain for two
> seconds to clear the error."* -- ISDT FAQ

`src/tasks/dome_task.cpp`:

```c
#define ESC_ARMING_DURATION_MS 2000  // Time to hold neutral for arming
...
    ledcPwmSetPulseWidth(LEDC_CH_DOME, neutralUs);
    PA_LOG_INFO(TAG, "Dome ESC arming (neutral=%d us for %d ms)", ...);
    delay(ESC_ARMING_DURATION_MS);
    PA_LOG_INFO(TAG, "Dome ESC armed and ready");
```

**Two seconds of neutral, held before any command, matching the vendor figure
exactly.** The previous revision of this sheet did not mention the rule, and the
firmware constant carried no citation; both now have one. It also explains the
`Receiver Waiting` error in Section 10.1: that error is what the ESC shows when
this hold has not happened.

> [!IMPORTANT]
> **Power-up order matters and protoArtoo cannot control it.** The arming hold
> starts when *DomeTask* starts, not when the *ESC* powers up. If the ESC is
> switched on after the controller has finished booting, it comes up into a
> steady 1500 us stream, which satisfies the rule for as long as nothing else has
> happened -- fine. If the ESC is powered **before** the controller, it spends the
> boot window with **no signal at all**, will report `Receiver Lost`, and needs
> its two seconds of neutral once the controller arrives -- which it then gets.
> Either order works; a builder power-cycling the ESC mid-session gets the hold
> only if DomeTask restarts, which it does not. **Reboot the controller after
> power-cycling the ESC**, or expect to clear the error from the app.

### 7.4 Calibrating against protoArtoo instead of a transmitter

The vendor procedure assumes a transmitter. protoArtoo is a legitimate throttle
source -- it emits exactly the standard 1000/1500/2000 us the procedure wants --
but two steps of the flow are awkward from a web page:

- **Step 1 needs full throttle held while the ESC powers up.** The Dome page's
  slider can command +100 %, but `domeTaskInit()` writes **neutral** at boot and
  the 500 ms command timeout returns to neutral when the slider stops sending.
  In practice: set the slider to +100 %, then switch the ESC on within the
  timeout window, or hold the slider.
- **Every step is "hold until you hear N beeps"**, and the beeps come from the
  motor, which means somebody has to be listening next to the droid.

> [!TIP]
> **The clean way is to calibrate with whatever the ESC will be driven by.** If
> that is protoArtoo, the learned endpoints become exactly our 1000/2000 us and
> `dome_min_pulse_us` / `dome_max_pulse_us` can stay at their defaults. If the ESC
> was calibrated against a transmitter with non-default EPA, its learned endpoints
> will **not** be 1000/2000, and those two config fields are the trim that fixes
> it without recalibrating (Section 12.2). That is what they are for.

### 7.5 The neutral deadband, and why it interacts with our speed limit

The last step of calibration sets the throttle mid-point deadband. The manual's
advice is to leave it alone -- *"for most transmitters keep default; only when the
motor turns with the throttle at centre, and recalibration does not help, set a
larger neutral deadband value"* (Chinese text, paraphrased in the English notes).

For protoArtoo that advice is doubly safe: our neutral is a **fixed 1500 us from a
hardware timer**, not a stick with a trim pot, so the drift the deadband exists to
absorb does not exist here.

> [!WARNING]
> **But the deadband is still there, and `dome_speed_limit_pct` walks into it.**
> The speed limit scales the whole usable range toward neutral
> (`domeSpeedToPulseUs`). At `dome_speed_limit_pct = 100`, full command is
> 2000 us -- 500 us clear of neutral. At **20 %**, full command is **1600 us**, and
> at **10 %** it is **1550 us**, which is plausibly *inside* the ESC's neutral
> deadband.
>
> The symptom is a dome that does nothing at any slider position, with the
> firmware perfectly happy and the log showing a sensible pulse width. Before
> concluding the ESC is dead, **check the speed limit**. Open Item 4 measures
> where the floor actually is.

## 8. Running modes, and the one that matters

The manual gives three, verbatim:

> **a. Forward with brake:** *"In this mode, the vehicle can only move forward and
> brake."*
>
> **b. Forward /Reverse with brake:** *"In this mode, the vehicle can forward,
> reverse and brake. This mode adopts double-click reversing mode, that is, when
> the throttle stick is pushed to the reverse zone for the first time, the motor
> only brakes. When the throttle is returned to the neutral position and pushed to
> the reverse zone for the second time, The car will be reversed if the motor has
> stopped rotating and the brake will still be applied if the motor is rotating."*
>
> **c. Forward and Reverse:** *"In this mode, when the throttle is in the reverse
> zone, the motor will reverse immediately."*

### 8.1 Why mode (c) is the only correct one for this droid

protoArtoo's model is a **signed speed**: `domeSpeedToPulseUs()` maps
`-1.0 .. +1.0` onto a pulse either side of neutral, and every source -- the web
slider, an RC channel, a sequence step, the random idle machine -- produces that
one number. There is no notion of a gesture, a click, or a stick returning to
centre between commands.

| Mode | What a `-0.3` dome command does |
| --- | --- |
| a. Forward with brake | **Brakes. Never reverses.** Half the dome's travel is unreachable |
| b. Forward/Reverse with brake **(factory default after calibration)** | **Brakes.** Reverses only if a later command re-enters the reverse zone from neutral *and* the motor has already stopped |
| **c. Forward and Reverse** | **Reverses immediately.** What the firmware means |

> [!CAUTION]
> **Mode (b) is the default, and it fails in a way that looks like a mechanical
> problem.** A droid in mode (b) will turn one way fine and refuse the other, or
> turn the other way only sometimes -- which reads exactly like a sticky ring, a
> weak motor or a friction sector. The bench session that produced Section 11's
> baseline recorded *"direction flip resistance"*, and mode (b) is the first thing
> that should be ruled out whenever that phrase is used again.

### 8.2 What mode (c) costs

Two of the ESC's features are defined in terms of the other modes, and choosing
(c) gives them up:

- **Active brake enable** is stated by the manual to be *"only effective in
  forward and reverse with brake mode"* -- so in mode (c) it does nothing. (The
  app guide says something different; Section 9.4.)
- **Braking as a distinct command** disappears. In mode (c) the reverse zone is
  reverse, so stopping a moving dome means commanding neutral and letting it coast
  -- or enabling **Active drag brake**, which is the one braking mechanism mode (c)
  *does* have (Section 9.2).

For a dome this is the right trade: a dome has inertia but no forward direction of
travel to arrest, and a coast-to-stop is gentler on a slip ring and a gear train
than a commanded brake.

## 9. Every ISD Go setting

Reachable only over Bluetooth through the switch module (Section 2), pairing by
long-press from powered-down until the blue LED blinks, 5 m range.

### 9.1 The settings the manual defines

| Setting | What ISDT says it does | Our baseline (Section 11) |
| --- | --- | --- |
| **Remote Calibration** | Throttle travel learning. **Blocks every other setting until done** | Prerequisite |
| **Running mode** | Section 8's three options | **Forward and reverse** -- required, not preferred |
| **Battery type / cell count** | Sets the automatic cutoff threshold | Match the actual pack |
| **Low voltage protection** | *"cut off the power output once the voltage is lower than the set data"*; **Auto** (by battery type) or **manual 5.0-12.0 V** | Auto, or an equivalent safe manual value |
| **BEC voltage** | *"manual adjustment from 5.0V to 7.5V (0.1V step)"*. *"Wrong BEC voltage setting may lead to damage to the servo or other electrical equipment"* | **5.0 V** |
| **Motor rotation** | *"moving to the left is the equivalent of the motor turning counter-clockwise, and moving right is clockwise"* | Whichever makes the dome turn the right way (Section 5.5) |
| **PWM frequency** | *"A lower driving frequency. Motor output will be stronger, the throttle will feel more punchy due to the higher volume of torque; ... higher driving frequency, Motor will output smaller torque while being more defined and rotating smoother with lesser noise, but it leads to increasing heating of the ESC"* | **1 kHz** |
| **Starting Force** | *"The larger the value, the higher sensitivity of throttle response and the motor increasing throttle output"* | **High or max** |
| **Braking force** | *"The larger the value, the higher sensitivity of braking response/force"* | **Minimum practical** |
| **Active drag brake level** | Mode (c) only. *"Ensure A non-closed value and the throttle is in the neutral position, the ESC will automatically generate a force that hinders the movement of the motor"* | **Disabled** |
| **Ramp Anti-Skid lock** | Not a separate switch -- it **is** drag brake in mode (c). Section 9.2 | Off, by consequence |
| **Active brake enable** | *"only effective in forward and reverse with brake mode. When this value is set to on, it can produce greater braking force"* | **Disabled** |
| **Throttle curve** | Stepless. *"In the default novice mode, the maximum power output is limited to 70%"* | Section 11.2's shaped curve |
| **Brake curve** | Stepless. *"In the default novice mode, the maximum braking force is limited to 70%"* | Section 11.2's soft curve |
| **Custom startup sound** | Cosmetic | Untouched |

The app additionally offers **preset modes** -- *"On road, Drift, Off road, Rock
crawler, Custom"* -- which set several of the above at once, and **curve presets**
-- *"Novice, Standard, Violent, Custom"*.

> [!WARNING]
> **Out of the box the ESC is throttling itself to 70 %.** The manual's phrase is
> *"the default novice mode"*, for both the throttle and the brake curve. A
> builder who calibrates, sets Running mode, and stops there is commanding a dome
> through a curve that caps output at 70 % -- and will then find the dome weak and
> reach for Start Force, which is not the thing limiting it. **Set the curve
> deliberately.** Section 11.2 is what to set it to, and it is the operator's own
> measured answer rather than a preset.

### 9.2 Active drag brake is mode (c)'s only brake, and it is also a holding torque

The two vendor paragraphs describe one mechanism:

> **Active drag brake level:** *"Ensure A non-closed value and the throttle is in
> the neutral position, the ESC will automatically generate a force that hinders
> the movement of the motor. A bigger set value, the greater the force
> generated."*
>
> **Ramp Anti-Skid lock:** *"In Forward/Reverse mode (climbing mode), the active
> drag brake level is set to a non-zero value to turn on the ramp anti-skid lock
> function. After this function is turned on, when accelerating movement from a
> throttle or braking position to a midpoint position, the motor will generate a
> torque force that is opposite to the current direction of movement to keep the
> vehicle stationary. ... The active drag brake level needs to match the weight of
> the vehicle. If the level is too high, the vehicle will not be stable in place,
> and if the level is too low, the vehicle will not be able to remain firmly on a
> steep slope."*

So in mode (c), a non-zero drag brake gives **a braking force at neutral and a
torque that resists being moved**. For a car on a hill that is hill-hold. For a
dome it is **position hold** -- the dome resists being spun by hand, by momentum,
or by a droid leaning.

> [!NOTE]
> **Section 11's baseline disables it, deliberately, and the trade is worth
> restating rather than assuming.** Disabled means the dome **coasts** to a stop
> and can be turned by hand -- gentler on the drive train, and the behaviour most
> R2 builders expect. Enabled means the dome **stops promptly and holds**, at the
> cost of a standing current at neutral, heat in a motor that is not turning, and
> the *"will not be stable in place"* judder ISDT warns about if the level exceeds
> what the mass wants.
>
> If a dome ever needs to hold a heading on a sloped surface, or stop faster at the
> end of a sequence step, this is the setting -- and it is the **only** braking
> mechanism available in the mode we must run. Open Item 5.

### 9.3 What the settings do *not* include

There is no ramp-rate, acceleration-limit or slew setting. **Start Force and the
throttle curve are the whole of the ESC's response shaping**, and both are
static: they change how output follows the *current* command, not how fast the
command may change. Any real ramping has to come from the host -- and protoArtoo
does not ramp either (Section 16.4).

### 9.4 Where ISDT contradicts ISDT

> [!CAUTION]
> **Active brake enable is described two different ways.**
>
> The manual: *"This setting is only effective in forward and reverse with brake
> mode."*
>
> The app menu guide: *"Takes effect when the negative throttle stroke exceeds
> 50%."*
>
> One is a **mode** precondition, the other a **command-magnitude** precondition,
> and they are not the same claim. If the app guide is right, Active brake can fire
> in mode (c) on any command past half reverse -- which for protoArtoo would mean
> a `-60 %` dome command behaving differently from `-40 %` for reasons nothing in
> the firmware knows about.
>
> `UNKNOWN`. Our baseline disables the setting, which makes the question moot
> today and is one of the reasons to leave it disabled. Open Item 6 is the bench
> test that settles it.

A second, smaller one: the app guide says Active drag brake *"only take effect
when the running mode is [forward and reverse]"*, while the manual's phrasing
(*"Adjust the Forward /Reverse mode under this item"*) is vague enough to read
either way. The Chinese text is unambiguous -- 正反转模式, mode (c) -- so the app
guide is right and the English manual is loose. No conflict in substance.

## 10. Protections and error reporting

### 10.1 What the ESC watches, and what it says

From the manual's feature list: *"battery low-voltage protection, over-temperature
protection, throttle out-of-control protection, BEC over-voltage and
under-voltage protection"*. The FAQ turns those into the messages the app shows:

| Error | ISDT's stated remedy | What it means for a droid |
| --- | --- | --- |
| **Receiver Waiting** | *"Center throttle for 2 seconds; recalibrate if needed"* | The arming hold has not happened (Section 7.3) |
| **Receiver Lost** | *"Check connections; verify PWM signal is 1ms-2ms"* | Signal absent or out of range -- a dead GPIO, a broken lead, or a controller that has not booted |
| **Motor Not Connected** | *"Check motor connections"* | A dome motor lead has come off |
| **Over Current** | *"Check for shorts or excessive load"* | Effectively unreachable on a dome load (Section 5.3) |
| **Battery Over/Under Voltage** | *"Use appropriate battery"* | The 2-3S window (Section 5.2) |
| **Temperature High** | *"Wait for cooling"* | 90 C external is the stated ceiling |
| **Throttle Not Calibrated** | Complete calibration | Blocks every other setting (Section 7.1) |

### 10.2 The reporting is thinner than the protections

> [!IMPORTANT]
> **There is no beep-code table and no LED-code table in any ISDT document read
> this session.** The manual documents beeps only for the *calibration* sequence,
> and the switch module's LED only as:
>
> | LED | Meaning |
> | --- | --- |
> | White, one flash | Powered, awaiting the button |
> | Green, steady | On, **no error** |
> | Red, flashing | On, **an error exists** |
> | Blue, flashing | Bluetooth pairing |
> | Blue, steady | Bluetooth connected |
> | Off | Low-power / off |
>
> **Red means "something", and the only way to learn which something is to open
> the app.** Seven distinct error states collapse to one flashing LED.
>
> For protoArtoo this matters more than it looks. The ESC is a **write-only
> device** to us -- the `ledc` protocol has no return path, the registry row
> declares **zero capabilities**, and `/api/status` has nothing to report about
> the dome ESC beyond what we last commanded. **A droid whose dome ESC is in
> protection cannot tell anybody.** The operator sees a dome that does not move
> and a UI that says it is moving; the same shape of defect `tasks/lessons.md`
> records for the audio module's optimistic "Playing" badge, with no equivalent
> fix available because there is no wire to read.
>
> The mitigations that exist are all mechanical: put the switch module where its
> LED can be seen, and keep a phone paired. Open Item 8 asks whether a `BUSY`-like
> sensed input is worth one GPIO.

## 11. The baseline profile (operator-locked, 2026-03-21)

> [!NOTE]
> **This section is the previous revision of this document, carried forward
> unchanged in substance.** It is the operator's own tuning, chosen on the bench
> against a real dome ring, and recorded in `tasks/lessons.md` as *"a deterministic
> ESC baseline"*. Nothing found in this research contradicts it; what the research
> added is the **reason** behind several of the rows, which now sit beside them.
>
> Final tuning is always mechanical-build dependent -- ring friction, gear mesh,
> mass and inertia, wiring, supply sag, and motor characteristics.

### 11.1 Baseline settings (ISD Go)

| Setting | Baseline | Rationale |
| --- | --- | --- |
| Running mode | **Forward and reverse** | Required for bidirectional dome movement -- **and the only mode in which a signed speed command means what the firmware thinks it means** (Section 8) |
| Battery type | Match actual pack chemistry | Correct cutoff/protection behavior |
| Cell count | Match actual pack | Correct voltage scaling. **2-3S LiPo / 6-8 cell NiMH only** (Section 5.2) |
| Cutoff voltage | Auto (or equivalent safe manual value) | Battery protection |
| BEC voltage | **5.0 V** | Conservative baseline for accessory power -- and the value that makes an accidental back-feed least destructive (Section 5.4) |
| Motor rotation | Forward (swap if mechanically reversed) | Direction alignment. Pick **this** inversion point, not the other two (Section 5.5) |
| PWM frequency | **1 kHz** | Common low-end torque baseline for heavier loads. ISDT: *"A lower driving frequency. Motor output will be stronger"* (Section 9.1) |
| Start force | **High or max** | Improves breakaway torque -- the main lever available on an over-specified controller (Section 5.3) |
| Brake force | Minimum practical value | Reduces abrupt reversal loading |
| Active drag brake | **Disabled** | Avoids neutral drag torque. Also disables ramp anti-skid, which is the same mechanism (Section 9.2) |
| Active brake | **Disabled** | Avoids aggressive braking on direction flips -- and sidesteps the manual-versus-app contradiction (Section 9.4) |

### 11.2 Curve guidance

Curve shaping changes response feel, not absolute maximum output.

Suggested starting curve:

- **Throttle curve:** stronger midrange response while keeping endpoints linear
- **Brake curve:** soft low-mid brake values to avoid shock loading

Concrete baseline values used by many dome builds:

- Throttle curve target: around **+50 input -> +80 to +85 output**, and
  **-50 input -> -80 to -85 output**
- Keep endpoints close to linear saturation: **+/-100 input -> +/-100 output**
- Brake curve target: around **50 input -> 10 to 20 brake**, **100 input -> 25 to
  35 brake**

> [!IMPORTANT]
> **The "+/-100 -> +/-100" line is doing more work than it looks.** The factory
> curve preset is **Novice**, which the manual says *"the maximum power output is
> limited to 70%"* (Section 9.1). Setting the endpoints to saturation is what
> takes that cap off. A dome tuned on a Novice curve is being asked for 100 % and
> given 70 %, and every other setting will be tuned around the wrong number.

### 11.3 Calibration and verification checklist

1. Complete throttle calibration (max, min, neutral) in ISD Go. **Nothing else can
   be set until this is done** (Section 7.1).
2. Verify command polarity and neutral hold behavior.
3. Test unloaded movement at multiple command levels.
4. Test loaded dome movement with sustained one-direction runs.
5. Test direction reversals with neutral dwell to check mechanical stress.
6. Re-tune start force and curve if breakaway or oscillation issues appear.

### 11.4 Operational cautions

- Avoid immediate full-power direction reversals under heavy load.
- If cogging/stall appears at low command, **increase start force first**.
- If harsh reversals occur, reduce brake aggressiveness and add neutral dwell in
  controller logic.
- **If the dome refuses to move at all at low slider values, check
  `dome_speed_limit_pct` before suspecting the ESC** (Section 7.5).
- **If the dome turns one way and not the other, check the running mode before
  suspecting the mechanics** (Section 8.1).

## 12. What protoArtoo actually does

### 12.1 The command path

Every source of dome motion produces one normalised `float` speed in
`-1.0 .. +1.0` and posts a `DomeCommand` to `domeCmdQueue`:

| Source | Where |
| --- | --- |
| Web slider / REST | `src/web/api_drive.cpp:519` |
| RC channel (PWM or SBUS binding) | `src/rc_dispatcher_helpers.cpp:59` |
| Sequence step `SEQ_ACT_DOME_ROTATE` | `src/tasks/sequence_dispatcher.cpp:150` |
| Dome link (`:` rotation cue from the dome) | `src/tasks/dome_link.cpp:381` |
| Serial console direct action | `include/console_direct_action_dome.h:329` |
| RC signal-loss stop | `src/tasks/rc_input.cpp:792` |
| Random idle machine | inside `domeTask()` itself |

**DomeTask is the sole consumer and the sole writer of the dome channel.** It runs
on **Core 1 at priority 4**, ticks every **20 ms** (50 Hz, matching the PWM
frame), and is registered with the task watchdog.

### 12.2 The pulse mapping, and what the four config fields do

```c
uint16_t domeSpeedToPulseUs(float speed, uint16_t neutralUs,
                            uint16_t minPulseUs, uint16_t maxPulseUs,
                            uint8_t speedLimitPct)
```

- clamps `speed` to `-1.0 .. +1.0`
- scales **each half-range independently**, so an asymmetric neutral trim
  (`neutralUs` not at the midpoint of `min..max`) stays correct in both directions
- applies `speedLimitPct` as a symmetric scale on both half-ranges
- clamps the result to `minPulseUs .. maxPulseUs`

| NVS field | Default | What it is for |
| --- | --- | --- |
| `dome_neutral_us` | **1500** | Trims *our* neutral to the ESC's learned neutral |
| `dome_min_pulse_us` | **1000** | Trims full reverse to the ESC's learned reverse endpoint |
| `dome_max_pulse_us` | **2000** | Trims full forward to the ESC's learned forward endpoint |
| `dome_speed_limit_pct` | **100** | Operator speed cap. **Interacts with the ESC deadband** (Section 7.5) |

All four are constrained on load -- pulses to `1000..2000`, the limit to `0..100`
(`src/config_serializer.cpp:262-265`) -- so a corrupted NVS value cannot put the
ESC outside its accepted band. `clampPulseWidth()` enforces the same range a
second time at the LEDC boundary. **Two independent clamps, on the one signal a
builder can mis-trim.**

`test/test_native/test_dome_math/` covers the mapping with 14 tests: neutral, both
full endpoints, both half-scales, the speed limit, asymmetric neutral trim and
boundary clamping. No hardware required.

### 12.3 Neutral is the answer to everything that goes wrong

`setDomeNeutral()` writes `dome_neutral_us` and zeroes `robotState.domeTargetSpeed`.
It is called on:

| Condition | Behaviour |
| --- | --- |
| **Boot** (`domeTaskInit`) | Neutral, then a **2000 ms** arming hold (Section 7.3) |
| **Task start** | Neutral before the first queue read |
| **Estop** | Neutral immediately; commands refused while active |
| **Sleep mode** | Neutral, queue drained, commands discarded |
| **Command timeout** | Neutral after **500 ms** with no fresh command |
| **Sequence step expiry** | Neutral when the step's `durationMs` elapses |
| **Random move expiry** | Neutral at the end of each idle move |
| **Emergency stop** (`ledcPwmEmergencyStop`) | Every configured channel to 1500 us, bypassing clamp and log |
| **Dome disabled at boot** | Task never spawned; the channel holds whatever `ledcPwmInit()` left |

> [!IMPORTANT]
> **The rule is "neutral, never float", and it is a deliberate choice about this
> ESC.** A floating signal line is `Receiver Lost` to an ESC70 -- an error state
> that needs clearing (Section 10.1) -- while a steady 1500 us stream is a happy,
> armed, stopped controller. The 500 ms command timeout exists so that a
> controller that stops talking produces a **stop**, not a held throttle.

### 12.4 The random idle machine

`dome_rnd_*` drives autonomous idle rotation when nothing else is commanding:
enabled flag, speed percent (default **30**), pause bounds (default **6-12 s**),
and move duration (default **2500 ms**). It picks a direction with `esp_random()`,
and it yields to estop, sleep, an active sequence, and any manual command.

It also **keeps `lastCommandMs` fresh while a random move is running**, so the
500 ms timeout does not cut its own move short -- a small detail that is easy to
break and is worth knowing before anyone refactors that loop.

## 13. What our own droid has proven

### 13.1 The record

From `CHANGELOG.md`'s `Hardware Validated` block and
`tasks/phase4_hardware_validation_deferral.md`, 2026-03-21/22, Artoo PCB:

> *"Dome ESC GPIO25 PWM path: spins correctly at 50/70/90% command from web API
> (unloaded and loaded ring tests); ESC baseline parameters confirmed and locked"*

and, in the fuller phase note:

> *"Dome ESC GPIO25 PWM path: spins at 50/70/90% command from web/API; ESC
> baseline locked (1 kHz PWM, Start force MAX, Brake 1, drag/active brake off);
> loaded ring test: movement achieved (friction sectors and direction flip
> resistance noted)"*

**Proven:** the LEDC path reaches the ESC from a 3.3 V GPIO; the ESC's own app
reports back the throttle percentage we command; the motor turns at 50 %, 70 % and
90 % unloaded; the dome ring moves under load.

**Not proven:** anything on **FireBeetle 2 / ESP32-P4** (Open Item 2); the arming
hold against a real ESC power-cycle; sustained thermal behaviour; the deadband
floor; any protection firing.

### 13.2 The lesson that is really about method

`tasks/lessons.md`, 2026-03-21, *"Distinguish PWM signal validity from loaded
torque capability"*:

> **Failure mode:** *"Dome testing repeatedly looked like a firmware command issue
> (no loaded rotation at 50%-100%), but signal-path validation and unloaded testing
> were mixed with drivetrain-loaded observations. This blurred root-cause isolation
> and prolonged tuning loops."*
>
> **Detection signal:** *"ISDT app throttle telemetry mirrored protoArtoo command
> percentages (PWM path alive); Motor spun correctly when decoupled from the dome
> ring at 50/70/90; With ring load coupled, movement struggled in localized
> high-friction sectors and during hard direction flips"*
>
> **Prevention rules:** *"For actuator bring-up, always run paired tests in this
> order: signal telemetry check -> unloaded spin -> loaded mechanism. If app
> telemetry matches command but loaded motion stalls, treat it as torque/mechanical
> domain first, not protocol domain."*

> [!TIP]
> **The ISD Go app is a logic analyser you already own.** Its live throttle
> readout is the one piece of return telemetry this otherwise write-only part
> offers, and it splits "is the signal right" from "is the mechanism moving" in
> one glance. Use it first, every time. It is also, per Section 10.2, the only
> place the ESC's error state is legible.

### 13.3 The defect this part's integration has already caused

`tasks/lessons.md`, 2026-03-21, *"Avoid float-format logging in Core 1 control
tasks"*: `DomeTask` logged its speed with `%.2f`, which on ESP32/newlib routes
through `_dtoa_r` and `malloc` inside a **Core 1 real-time task**, and produced a
*"Stack canary watchpoint triggered (DomeTask)"* panic loop. The fix was integer
percent logging, which is what `setDomeSpeed()` and the command log still use.

Not an ESC fault. It is what happens when a control loop for a part like this ends
up on a real-time core, and it is the reason the dome log lines read `%d%%`.

## 14. How the hobby drives a dome (non-normative)

No astromech project on this disk drives an ESC70 (Section 4's negative result).
What they do instead is worth recording, because it is what a visiting builder will
expect.

### 14.1 The community default is a SyRen 10 on packet serial

**ShadowMD** (`Shadow_MD_DualController_Template.ino`) drives
`SyR->motor(domeRotationSpeed * invertDomeDirection)` with a **-127..+127** signed
speed, and wraps it in two idioms worth stealing:

```cpp
int serialLatency = 25;   // delay factor in ms to prevent queueing of the Serial data
boolean isDomeMotorStopped = true;
...
if ( (!isDomeMotorStopped || domeRotationSpeed != 0) &&
     ((currentMillis - previousDomeMillis) > (2*serialLatency)) )
```

-- *"Eliminate a constant stream of 'don't spin' messages"*, with a note that
*"Constantly sending commands to the SyRen (Dome) is causing foot motor delay"*.

**Padawan360**'s DY-SV5W fork uses the same controller with explicit constants:

```cpp
const byte DOMESPEED = 80;              // of 127
const byte DOMEDEADZONERANGE = 20;
Sabertooth Syren10(128, Serial2);       // address 128
...
Syren10.autobaud();
Syren10.setTimeout(950);
...
domeThrottle = map(Xbox.getAnalogHat(domeAxis,0), -32768, 32767, DOMESPEED, -DOMESPEED);
if (domeThrottle > -DOMEDEADZONERANGE && domeThrottle < DOMEDEADZONERANGE) domeThrottle = 0;
Syren10.motor(1, domeThrottle);
```

Three things transfer directly to an ESC70 build:

1. **A host-side deadzone** (`DOMEDEADZONERANGE = 20` of 127, about 16 %) applied
   to the *input*, not the output. protoArtoo does this for SBUS with
   `dome_input_filter.h`'s neutral band and confirm-frames filter, which is a
   stricter version of the same idea.
2. **A capped maximum** (`DOMESPEED = 80` of 127, about 63 %) baked in as a
   constant. protoArtoo's equivalent is `dome_speed_limit_pct`, configurable
   rather than compiled.
3. **A hardware command timeout** (`setTimeout(950)`) so the controller stops if
   the host stops talking. **An ESC70 has no equivalent** -- it will hold the last
   pulse forever. Our 500 ms `DOME_COMMAND_TIMEOUT_MS` is the firmware standing in
   for a hardware feature the SyRen has and this ESC does not (Section 15).

### 14.2 Why protoArtoo went the other way

`tasks/phase3-tasks.md` records the decision at the time:

> *"the ESC70 uses **standard RC PWM for all runtime control** -- 1000-2000us pulse
> width on DOME (GPIO 25); no proprietary serial protocol at runtime"*
> ... *"Bluetooth (ISD Go APP) handles ESC configuration only ... this is out of
> scope for firmware and must be documented as such in code comments"*

That is the whole trade: **configuration moves out of the firmware and onto a
phone**, and in exchange the runtime driver is a PWM write. A SyRen would move
configuration into the firmware (address, baud, ramping, deadband, timeout arming)
and cost a UART artoo-esp32 does not have.

## 15. How the two Dome Rotation members differ

| | **ISDT ESC70** | SyRen 10 |
| --- | --- | --- |
| Status | **`supported`, shipping** | `roadmap` |
| Registry value | **11** | 12 |
| Protocol token | **`ledc`** | `de_packet_serial` |
| Host cost | **one PWM-capable GPIO** | a UART, or a share of the drive lane |
| New firmware needed | **none** | a driver |
| Configuration lives | **on a phone, over Bluetooth** | in DIP switches and EEPROM |
| Continuous current | **70 A** | 10 A |
| Input voltage | **2-3S LiPo / 6-8 cell NiMH** | up to 24 V |
| Host-side readback | **none** -- write-only | none in R/C mode; serial modes still do not report speed |
| Hardware command timeout | **none** | `setTimeout()`, armed by the host |
| Braking | drag brake only, in the mode we must use | regenerative |
| BEC | **3 A at 5.0-7.5 V, adjustable** | 5 V, small |
| Capability bits declared | **0** | 0 |
| Proven on our hardware | **yes, 2026-03-21** | no |

**The ESC70's real advantage is that it costs nothing to drive.** One GPIO, one
existing LEDC channel, a pure function and a task. Its real cost is that it is
**mute**: no readback, no error reporting to the host, and no hardware watchdog,
so every safety property the dome has is one protoArtoo implements in software.

**The SyRen's real advantage is the timeout**, which is the one thing in this
comparison that protects a droid when the *host* fails rather than when the
*command* stops.
[`sabertooth-syren-packet-serial.md`](sabertooth-syren-packet-serial.md) Section
12.3 makes the same argument for the drive lane; it applies here unchanged.

## 16. Findings against the shipping implementation

Two fixed in the change that carries this sheet; three reported because fixing
them is a decision rather than a correction.

### 16.1 FIXED -- the driver header calls the dome motor brushless

`include/ledc_pwm.h` documented the dome channel as:

```
//   - DOME (GPIO 25)  --  Dome rotation ESC (brushless motor, not a servo)
```

**The ESC70 is a brushed controller and cannot drive a brushless motor at all.**
ISDT's specification is *"540/550/775 Brushed motor"*, and the manual's title is
*"Brushed Electronic Speed Controller"*. The comment is also board-specific
without saying so: `PIN_DOME_ESC` is GPIO 25 on artoo-esp32 and **48** on
FireBeetle 2.

Nothing in the code depends on either error -- a pulse width is a pulse width --
but a builder reading that header would source the wrong ESC.

### 16.2 FIXED -- the sheet was not registered in AGENTS.md

`AGENTS.md` lists eleven spec sheets as authoritative truth sources; this one
existed on disk and was absent from that list, which is the specific gap
[#391](https://github.com/mattiasbrandt/protoArtoo/issues/391) names. Registered.

### 16.3 REPORTED -- `docs/failsafe.md` says these stack sizes are not chip-specific, and they are

The Core 1 task table carries a **`Chip-Specific?`** column reading **`No`** for
`DomeTask`, at **3072 B**. `include/config.h` disagrees:

| | artoo-esp32 (`PA_CHIP_TARGET_ESP32`) | FireBeetle 2 (`PA_CHIP_TARGET_ESP32P4`) |
| --- | --- | --- |
| `DOME_TASK_MEASURED_CHAIN_BYTES` | 2992 | 3280 |
| `DOME_TASK_STACK_BYTES` | **3072** | **4608** |

The same mismatch appears on `DriveTask` (4096 vs 5632), `ServoTask` (4096 vs
4608) and `DomeLinkTask` (6144 vs 9216). The table's numbers are the artoo-esp32
ones and the column is wrong for at least four rows.

**Not changed here.** It is a safety document, the correction spans the drive and
RC lanes as well as the dome, and the non-dome numbers were not verified this
session. It needs its own pass.

> [!WARNING]
> **The dome row carries a second fact the table does not show, and it is the more
> important one.** `include/config.h` records that artoo-esp32's 3072 B is a
> deliberately declined margin: *"the thinnest floor in the block -- 80 B on a
> lower-bound walk, which is under the cost of one interrupt entry -- and it is
> the pre-existing shipping value, recorded here as a known exposure"*.
>
> **80 bytes** is why `DomeTask` panicked on a `%.2f` log line (Section 13.3), and
> it is why nothing may be added casually to that loop. Anyone extending dome
> control on artoo-esp32 should read that comment first.

### 16.4 REPORTED -- nothing ramps, and this ESC will not do it for us

Section 11.4 says *"Avoid immediate full-power direction reversals under heavy
load"* and *"add neutral dwell in controller logic"*. **No firmware enforces
either.** A single `DomeCommand` carrying `-1.0` immediately after one carrying
`+1.0` becomes one PWM frame at 1000 us with the motor still spinning forward, and
in mode (c) the ESC obeys instantly -- which is the whole point of mode (c).

The ESC has no ramp setting to fall back on (Section 9.3), and the SyRen's
equivalent -- its EEPROM ramping -- is exactly what
[`sabertooth-syren-packet-serial.md`](sabertooth-syren-packet-serial.md) warns
*"persist in EEPROM and leak into other modes"*.

**A host-side slew limit in `DomeTask` is the natural home**, and it is a
behaviour change to a Core 1 task with an 80 B stack floor on one board
(Section 16.3), so it is a decision and not a correction. It would also change
what the random idle machine and sequence steps feel like. **Not changed here.**
Open Item 9.

### 16.5 REPORTED -- the dome is write-only and the UI does not say so

`robotState.domeTargetSpeed` is *what we commanded*, not what the dome is doing,
and the Dome page's rotation-state pill is driven from it. There is no readback
path and the registry row honestly declares **zero capabilities**.

So a dome whose ESC is in protection, unpowered, uncalibrated, or in the wrong
running mode shows the same UI as a dome that is turning. This is structurally the
same defect `tasks/lessons.md` records for the audio badge in 2026-03-21
(*"before all this we had this web response 'playing' but obviously we never knew
for sure"*) -- except that the audio module had a UART to interrogate and this ESC
has nothing.

**The honest fixes are small and are not free:** word the pill as *commanded*
rather than *state*, or sense something. Open Items 8 and 10.

### 16.6 CONFIRMED -- the registry row matches the implementation

Row 11, checked field by field this session: value **11** (unique, never reused),
id `isdt_esc70`, name *"ISDT ESC70 (RC ESC)"*, category
`COMPONENT_CATEGORY_DOME_ROTATION`, protocol **`ledc`**, status
`COMPONENT_STATUS_SUPPORTED`, capabilities **0**, gate `nullptr`, `included = 1`.

All correct, and two of them deliberately so:

- **`ledc` is a real Component Protocol under `CONTEXT.md`'s own test** -- *"whether
  it changes the driver"*. An RC-PWM ESC and a packet-serial motor driver are
  different drivers, and the SyRen row's `de_packet_serial` is the proof.
- **Capabilities `0` is the honest word here**, and it means *"nothing to ask"*
  rather than *"not yet investigated"*. There is no return path to ask anything
  over (Section 16.5). Contrast the DFPlayer row, whose `0` means the second thing
  and says so in the registry's own comment.

## 17. Agent Lookup Quick Reference

- Field: Control signal. Required value: **standard RC servo PWM, 1000-2000 us, neutral 1500 us**. Nothing else -- S.BUS, PPM, DSM2, DSMX and 750 us narrow PWM are refused by name.
- Field: Frame rate. Required value: **50 Hz** (what we emit). No vendor figure exists.
- Field: Peripheral. Required value: ESP32 **LEDC**, low-speed, timer 0, channel `LEDC_CH_DOME = 2`, 16-bit.
- Field: Pin. Required value: `PIN_DOME_ESC` -- **GPIO 25** on artoo-esp32, **GPIO 48** on FireBeetle 2 (`VDD_IO_5`, LDO caution).
- Field: Pulse clamp. Required value: **1000-2000 us**, enforced in `clampPulseWidth()` *and* on config load.
- Field: Neutral. Required value: **1500 us**, emitted on boot, disable, estop, sleep, timeout and sequence expiry. **Never float the pin.**
- Field: Arming. Required value: **hold neutral 2000 ms** before the first command. Vendor rule, not ours: *"return throttle to center position and maintain for two seconds to clear the error."*
- Field: Command timeout. Required value: **500 ms** -> neutral (`DOME_COMMAND_TIMEOUT_MS`). The ESC has **no** hardware timeout; this is the only one.
- Field: Running mode. Required value: **Forward and reverse** (mode c). **Not** the post-calibration default, which is Forward/Reverse *with brake* and reverses only on a second push.
- Field: Throttle calibration. Required value: **mandatory, and it blocks every other setting** until done. Sequence: end position at power-on -> neutral (1 beep) -> full throttle (2 beeps) -> full brake (3 beeps) -> 2 beeps -> restart.
- Field: Curve preset out of the box. Required value: **Novice, which caps output at 70 %.** Set the endpoints to saturation or the dome is quietly throttled.
- Field: PWM frequency. Required value: **1 kHz** (operator baseline). Lower = more low-end torque and more noise; higher = smoother and hotter.
- Field: Start force. Required value: **high/max** (operator baseline) -- the main breakaway lever.
- Field: Brake force. Required value: **minimum practical** (operator baseline).
- Field: Active drag brake. Required value: **disabled** (operator baseline). It is mode (c)'s only brake and also a position-hold torque; enabling it is a real option, not an error.
- Field: Active brake. Required value: **disabled** (operator baseline); the manual and the app guide disagree about when it even applies.
- Field: Battery. Required value: **2-3S LiPo or 6-8 cell NiMH only.** Never a hoverboard pack, never 4S+.
- Field: BEC. Required value: **5.0-7.5 V, 3 A, adjustable in 0.1 V steps -- an OUTPUT.** Set 5.0 V. Do not wire it to a controller rail; FireBeetle 2's middle column is 3.3 V.
- Field: Current rating. Required value: 70 A continuous / 120 A peak, 1500 W on 3S. **Over-current protection will never fire on a dome load.**
- Field: Maximum temperature. Required value: **90 C external**.
- Field: Waterproofing. Required value: **IP65** per the vendor FAQ.
- Field: Dimensions / weight. Required value: **38.6 x 31.6 x 17.15 mm**, ~49 g, plus a ~4.5 g switch module.
- Field: Configuration surface. Required value: **ISD Go app over Bluetooth, through the switch module, 5 m range.** Not firmware, not the web UI.
- Field: Error reporting to the host. Required value: **none exists.** Red flashing LED means "an error"; the app is the only place to read which.
- Field: Speed limit interaction. Required value: `dome_speed_limit_pct` scales toward neutral; **a low limit can put full command inside the ESC's deadband.**
- Field: protoArtoo identifiers. Required value: registry value **11**, id `isdt_esc70`, protocol token `ledc`, capabilities **0**, config fields `dome_neutral_us` / `dome_min_pulse_us` / `dome_max_pulse_us` / `dome_speed_limit_pct`.

## 18. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | **What the artoo-esp32 PCB's dome header power pin carries** (Section 5.4) | A meter on the header with the board powered. Until then, wire signal and ground only. This is the highest-value item here: it is the difference between a safe default and a guess |
| 2 | **The whole part on FireBeetle 2 / ESP32-P4** (Sections 6.2, 13.1) | Nothing in Section 12 has been run on that board. GPIO 48's `VDD_IO_5` LDO caution makes edge quality the specific thing to watch; `docs/pin_map.md` says the symptom would be *"erratic dome ESC throttle"* and would never appear in a log |
| 3 | **The ESC's accepted frame rate** | No vendor figure. Try 50 Hz (ours) against 100 Hz and 200 Hz and watch the app's throttle readout. Only matters if we ever want a faster dome update |
| 4 | **Where the neutral deadband floor actually is** (Section 7.5) | Ramp `dome_speed_limit_pct` down until the dome stops responding, and record the pulse width. Gives the Dome page a real minimum instead of `0` |
| 5 | **Active drag brake as dome position-hold** (Section 9.2) | Enable at a low level, in mode (c), and measure: does the dome stop faster, hold heading, and stay cool? It is the only brake mode (c) has |
| 6 | **Active brake: mode-gated or 50 %-stroke-gated?** (Section 9.4) | In mode (c), enable it and command -40 % then -60 %. If they differ, the app guide is right and the manual is wrong |
| 7 | **What actually protects a jammed dome** (Section 5.3) | Stall the ring deliberately at low command and watch current, motor temperature and ESC temperature. The ESC's 120 A limit will not fire; something else has to |
| 8 | **Is a sensed dome input worth a GPIO?** (Sections 10.2, 16.5) | A rotation sensor or even a current shunt would turn a write-only actuator into an observable one. Scope the cheapest useful version before proposing it |
| 9 | **A host-side slew limit in `DomeTask`** (Section 16.4) | Decide whether reversals should be ramped, then implement against the 80 B stack floor on artoo-esp32 |
| 10 | **Word the Dome page's state pill as commanded, not actual** (Section 16.5) | Copy change; needs the operator's call on wording |
| 11 | **Thermal behaviour over a show-length run** | The 90 C ceiling has never been approached in testing, and a dome is a light load -- but nobody has measured it after an hour of random idle rotation |
| 12 | **Does the ESC hold configuration across a firmware OTA of its own?** | ISDT advertises OTA firmware updates. Whether a settings profile survives one is undocumented and would invalidate Section 11's baseline silently |

## 19. Sources

**Primary -- vendor**

- **ISDT ESC70/ESC90 User Manual** -- https://www.isdt.co/down/pdf/ESC70.pdf.
  14 pages, `ESC70说明书_210928B`, created 2021-09-28. Chinese pages 2-7, English
  pages 8-14. **Has a text layer**; pages 12-14 were additionally rendered with
  `pdftoppm -r 150` and read as images to confirm the calibration flowchart and
  the running-mode text.
- **ISDT ESC70 FAQ** -- https://www.isdt.co/esc70-faq.html. The signal-compatibility
  statement, the two-second neutral rule, the error list, IP65, the 1500 W figure,
  Bluetooth range and warranty terms.
- **ISDT ESC70 APP menu guide** -- https://www.isdt.co/english-esc70-app-menu-guide.html?lang=en.
  Preset modes, curve presets, and the Active Brake statement that contradicts the
  manual.
- **ISDT ESC70 product page** -- https://www.isdt.co/esc70.html?lang=en.
  The specification table and the ESC70/ESC90 relationship.

**protoArtoo**

- `src/tasks/dome_task.cpp`, `include/dome_math.h`, `include/dome_input_filter.h`,
  `src/drivers/ledc_pwm.cpp`, `include/ledc_pwm.h`, `include/servo_helpers.h`
- `include/config.h`, `include/config_store.h`, `src/config_store.cpp`,
  `src/config_serializer.cpp`, `include/component_registry.inc`, `src/main.cpp`
- `src/web/api_drive.cpp`, `src/rc_dispatcher_helpers.cpp`,
  `src/tasks/sequence_dispatcher.cpp`, `src/tasks/dome_link.cpp`,
  `src/tasks/rc_input.cpp`, `include/console_direct_action_dome.h`
- `data/dome.html`
- `test/test_native/test_dome_math/` (14 tests)
- `docs/pin_map.md`, `docs/failsafe.md`, `CONTEXT.md`, `CHANGELOG.md`
- `tasks/lessons.md` (two dome entries), `tasks/phase3-tasks.md`,
  `tasks/phase4-tasks.md`, `tasks/phase4_hardware_validation_deferral.md`
- [`sabertooth-syren-packet-serial.md`](sabertooth-syren-packet-serial.md),
  [`dfplayer-mini-sound.md`](dfplayer-mini-sound.md),
  [`firebeetle2-esp32-p4-spec-sheet.md`](firebeetle2-esp32-p4-spec-sheet.md)
- **The previous revision of this file**, whose Section 11 is carried forward

**Read locally, on this disk**

- `~/Documents/GitHub/ShadowMD` -- `Shadow_MD_DualController_Template.ino`, the
  SyRen 10 command path and the `isDomeMotorStopped` / `serialLatency` idioms.
- `~/Documents/GitHub/Padawan360_mega_maestro_DYSV5W` -- `DOMESPEED`,
  `DOMEDEADZONERANGE`, `autobaud()`, `setTimeout(950)`.
- `~/Documents/Astromech/BetterDuinoFirmwareV4`, `~/Documents/GitHub/AstroPixelsPlus`,
  `~/Documents/GitHub/CHIRP` -- negative results for dome ESC control.

> [!NOTE]
> **Negative results, recorded so nobody repeats them.** ISDT publishes **three**
> documents for this part and no more: a manual, a FAQ, and an app menu guide.
> There is no datasheet, no errata, no beep-code table, no LED-code table and no
> stated logic threshold. **No astromech project on this disk drives an ESC70**,
> ISDT does not market it for droids, and the builder forums
> (`forums.astromech.net`, `droidwiki.astromech.net`) did not resolve this
> session -- so the astromech evidence for this part is this project's own bench
> record and nothing else. Numeric ranges and factory defaults for Start Force,
> Braking Force and Active Drag Brake Level are **not published** in any of the
> three documents; the app is the only place they exist, and reading them needs
> the hardware paired.
