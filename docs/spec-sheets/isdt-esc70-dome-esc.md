# ISDT ESC70 Spec Sheet (brushed RC ESC)

The **ISDT ESC70** is a brushed electronic speed controller for 1/8 and 1/10 RC
cars, configured over Bluetooth from ISDT's ISD Go phone app and controlled by
standard RC servo PWM. In an astromech it turns the dome.

Research date 2026-09-12. Every specification below was read from ISDT's own
manual PDF (`ESC70说明书_210928B`, 14 pages, Chinese and English, **with** a text
layer -- the tables were also rendered as images and read visually to confirm),
from ISDT's own FAQ and app menu guide, from a bench record of one ESC70 turning
a dome, or from the astromech projects that drive domes. Claims that could not be
sourced are marked `UNKNOWN` with the artefact or bench test that would settle
them.

> [!CAUTION]
> **The factory default running mode reverses on the second push, not the first,
> and a droid controller has no second push.** After throttle calibration the ESC
> defaults to **Forward/Reverse with brake**, which uses *double-click reversing*:
> the first command into the reverse zone **brakes**, and only a command that
> returns to neutral and goes negative again reverses -- and only if the motor has
> already stopped. A controller that maps a signed speed straight to a pulse
> width gets a **brake instead of a turn** from a `-30 %` dome command in that
> mode.
>
> **Forward and reverse** is the one of the three modes in which a signed speed
> command means what it says (Section 7).

> [!CAUTION]
> **The ESC's throttle wire is a power *output*.** The ESC70's 3-pin wire carries
> its BEC at **5.0-7.5 V**, and ISDT's manual says in so many words *"We Suggest
> DO NOT supply additional power to the receiver, otherwise your ESC may be
> damaged."* Plug that wire onto a controller header whose centre pin is the
> controller's own 3.3 V supply and you put **5-7.5 V onto the 3.3 V plane**.
> Signal and ground only (Section 4.4).

> [!IMPORTANT]
> **A 70 A ESC on a dome is a deliberate over-spec, and the consequences are real
> rather than academic.** This part is specified for *"1/8 or 1/10 Various
> models"* with *"540/550/775 Brushed motor"* -- a class that draws tens of amps.
> A dome gearmotor draws single digits. Everything awkward about running it on a
> dome (breakaway at low command, the need for maximum Start Force, cogging in
> high-friction sectors) is the low-current end of a very large controller, and
> Section 4.3 is what to do about it.

## 1. Scope

Covers the part and its ESC90 sibling, the electrical and mechanical contract,
the battery window and the BEC hazard, the control signal and what the ESC
refuses, arming and throttle calibration, the three running modes and why only
one of them suits a droid, every ISD Go setting with what the vendor says about
it, the protections and how thin their documentation is, how the hobby drives a
dome generally, and how the ESC70 and the SyRen 10 differ.

Does not cover: the mechanical dome drive itself (ring, bearing, gear mesh,
slip ring); motor selection beyond naming the class; the ISD Go app's own UI
beyond its settings; or ISDT's brushless ESC range.

## 2. What you are actually buying

**A 38.6 x 31.6 x 17.15 mm brushed car ESC**, about 49 g, with 16 AWG 200 mm
wires and **no connectors fitted**, plus a small two-in-one **electronic switch +
Bluetooth module** (about 4.5 g) on a short wire.

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
> Every setting in Section 8 is reachable only through that module.

### 2.1 The part's identity is not in doubt

This is one manufacturer's part with one manual, one firmware line and one app.
No clone families, no chip-marking lottery. What it lacks is a *second* source of
truth: ISDT's three documents are all there is, they are thin in places
(Section 9.2), and nothing independent corroborates them.

### 2.2 ESC70 versus ESC90

One manual covers both, and the product page lists one difference:

| | ESC70 | ESC90 |
| --- | --- | --- |
| Continuous / peak current | **70 / 120 A** | 90 / 180 A |

*"All other specifications listed above apply identically to both models."*
For a dome, where neither number is the constraint, the two behave the same; the
ESC90 costs more.

### 2.3 Availability

Checked 2026-09-12: **readily available**, chiefly through AliExpress sellers and
ISDT's own channel, typically in the **USD 25-40** band. There is no single
authoritative retailer whose stock state matters; ISDT sells direct and lists a
support address and a phone number, and states a **one-year replacement warranty**
(*"quality issues receive replacements (no repairs)"*).

## 3. Sources Checked

| Source | How it was taken | What it gave |
| --- | --- | --- |
| **ISDT ESC70/ESC90 user manual**, `https://www.isdt.co/down/pdf/ESC70.pdf` | fetched (200, 1.96 MB, 14 pages); **has** a text layer, and pages 12-14 were additionally rendered with `pdftoppm` and read as images to confirm the flowchart and the mode text | The full specification, the wiring rules, the throttle-calibration flowchart, the three running modes and the double-click behaviour, every ISD Go setting's meaning, the protections list, and the switch-module LED states |
| **ISDT ESC70 FAQ**, `https://www.isdt.co/esc70-faq.html` | fetched | **The only statement of what the ESC rejects** (no S.BUS/DSM2/DSMX/PPM/750 us), the 1500 W figure, IP65, the **two-second neutral rule**, the error-message list, Bluetooth range, and the warranty |
| **ISDT ESC70 app menu guide**, `https://www.isdt.co/english-esc70-app-menu-guide.html?lang=en` | fetched | The app's preset modes (On road / Drift / Off road / Rock crawler / Custom), the curve presets (Novice / Standard / Violent / Custom), and a statement about Active Brake that the manual contradicts (Section 8.4) |
| **ISDT ESC70 product page**, `https://www.isdt.co/esc70.html?lang=en` | fetched | The specification table, and the ESC70/ESC90 relationship |
| **Bench record, 2026-03-21/22** | one ESC70 on an ESP32 controller and a real dome ring | The 3.3 V logic-level answer the vendor does not publish (Section 5.2), the app's throttle readout as a signal check, and the low-command breakaway behaviour on a loaded ring (Section 4.3) |
| ShadowMD | read from source | SyRen 10 packet serial, the `isDomeMotorStopped` command-throttling idiom, `serialLatency` |
| Padawan360 (DY-SV5W port) | read from source | `DOMESPEED = 80`, `DOMEDEADZONERANGE = 20`, `Syren10.autobaud()`, `setTimeout(950)` |
| BetterDuino firmware V4, AstroPixelsPlus, CHIRP | read from source | Negative results: none of them drive a dome **ESC**; dome motion is SyRen/Sabertooth or nothing |

> [!NOTE]
> **Negative result, recorded so nobody repeats the search.** No astromech project
> read for this sheet, and no astromech source reachable at research time, drives
> an ISDT ESC70. ISDT does not market it for droids, and the builder forums
> (`forums.astromech.net`, `droidwiki.astromech.net`) did not resolve. **The
> astromech evidence for this part is one bench record and nothing else** --
> which is the opposite of the sound modules, where the hobby had decades of
> practice to draw on. Treat Section 10 as context, not corroboration.

## 4. Electrical

### 4.1 The specification, from the vendor's own table

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

### 4.2 The battery window is the first thing to check, and it is narrow

> [!CAUTION]
> **2-3S LiPo means 6.0-12.6 V. There is no 4S, 5S or 6S option, and no 24 V
> option.** A droid whose main pack is a hoverboard battery (**36 V nominal, 42 V
> charged**) will destroy this ESC instantly if the dome ESC is fed from it. So
> will a 4S pack.
>
> The dome ESC must be fed from a **2-3S / 12 V-class rail of its own**, or from a
> regulated 12 V step-down off the main pack. The ESC itself reports a wrong pack
> only as `Battery Over/Under Voltage` in the app (Section 9.1), and only if it
> survives the pack.

The low-voltage cutoff is set *"to automatic (according to the battery type) or
manually specified from 5.0V to 12.0V"*, which is consistent: the whole protection
range lives below 12.6 V.

### 4.3 70 A of controller for a few amps of dome, and what that costs

This ESC is sized for a 1/10-scale car pulling tens of amps through a 540-class
motor. A dome gearmotor of the usual class -- a JGB37-520 or similar -- is a
single-digit-amp load.

**Nothing about that is dangerous, and two things about it are awkward:**

1. **Low-command behaviour is the whole game.** All the useful resolution of a
   70 A controller sits above where a dome ever operates. On a bench, an ESC70
   turning a loaded dome ring *"struggled in localized high-friction sectors"*:
   a small motor asking a large controller for a small, precise amount of
   current. **Start Force high or max** and **PWM frequency 1 kHz** both push
   torque into the low end, and the manual's own words support both (*"A lower
   driving frequency. Motor output will be stronger, the throttle will feel more
   punchy due to the higher volume of torque"*).
2. **Over-current protection will never fire on a dome.** A 120 A peak limit is
   unreachable with a dome motor, so a jammed dome is **not** protected by the
   ESC. It is protected -- if at all -- by the motor's own stall behaviour and by
   the operator noticing. Do not treat "the ESC has over-current protection" as a
   mechanical safety layer here. Open Item 4.

### 4.4 The BEC is an output, and that is a wiring hazard

The ESC's 3-pin throttle wire is **not** a passive signal input. ISDT:

> *"Please be reminded the ESC throttle control port has BEC voltage adjustment
> function to the receiver and the servo, We Suggest DO NOT supply additional
> power to the receiver, otherwise your ESC may be damaged."*

So the wire carries **signal, ground, and 5.0-7.5 V out**: the throttle lead's
centre wire is the BEC output.

> [!CAUTION]
> **On a controller whose servo headers are signal / supply / ground rows, this
> is a board-killer in the most natural wiring.** If the header's centre pin is
> the controller's 3.3 V supply, pushing the ESC's 3-pin wire onto it lands the
> BEC's 5-7.5 V on the 3.3 V plane. If the centre pin's supply is unknown, treat
> it the same way.
>
> **Wire signal and ground only.** Cut, tape back, or pull the pin on the ESC
> wire's centre conductor. It is the usual rule for servos on a logic header --
> power them from a separate BEC and bring only signal and a common ground --
> with the direction reversed: here the ESC **is** the separate BEC, and it must
> not be allowed to feed back.

> [!TIP]
> **The BEC is genuinely useful once it is not pointed at the controller.** 3 A at
> 5-7.5 V, adjustable in 0.1 V steps, is a real servo supply. An ESC70 already in
> the droid can be the dome's and the arms' 5 V BEC. Set it to **5.0 V** before
> wiring anything to it; ISDT's own warning is *"Wrong BEC voltage setting may
> lead to damage to the servo or other electrical equipment."*

### 4.5 Motor wiring, polarity, and the two ways to reverse direction

Motor wires are **not** polarised: *"The two output wires of the ESC can be
connected to either of two wires of the motor at will."* If the dome turns the
wrong way, there are two fixes and they are equivalent -- *"the two motor wires
can be interchanged or change the direction of motor rotation can be adjusted via
the APP."*

**Battery wires are polarised and unprotected:** *"If the ESC is connected
reversely, your ESC will be damaged."* No reverse-polarity protection is claimed
anywhere in the three vendor documents.

## 5. The control signal

### 5.1 One protocol, and the vendor says so by listing what it refuses

ISDT's FAQ is the only document that states the negative, and it is unusually
direct:

> *"only supports 1000us~2000us standard PWM signals"* and *"does not support SUB,
> DSM2, DSMX, PPM signals and 750us narrow PWM signals."*

| | |
| --- | --- |
| Accepted | **Standard RC servo PWM, 1000-2000 us** |
| Rejected, by name | S.BUS (*"SUB"*), DSM2, DSMX, PPM, **750 us narrow PWM** |
| Neutral | 1500 us |
| Frame rate | `UNKNOWN` -- no vendor figure. 50 Hz is the RC standard and is measured working (Section 5.2, Open Item 1) |
| Logic level | `UNKNOWN` from any document -- but **3.3 V is measured working** on one unit (Section 5.2) |

> [!NOTE]
> **"750 us narrow PWM" is worth understanding rather than skipping.** It is the
> half-width signalling some modern receivers and flight controllers emit, where
> the whole 1000-2000 us range is compressed to 500-1000 us. If an ESC70 fed from
> a receiver output, a Maestro channel or another controller behaves as though
> every command is reverse, that is the first thing to check.

An ESC that speaks only servo PWM is reached by a PWM peripheral: no driver to
write and no wire protocol to get wrong.

### 5.2 The 3.3 V question, answered by a bench rather than by ISDT

No ISDT document states the input's logic threshold. The wiring the manual assumes
is a hobby receiver, whose servo outputs are typically 5 V -- and the ESC powers
that receiver from its own BEC, so from ISDT's point of view the question never
arises.

**The ESC70 arms and follows a 3.3 V, 50 Hz pulse, measured on a bench**
(2026-03-21/22, one unit, its signal wire on an ESP32 GPIO through the LEDC
peripheral):

- the controller held 1500 us neutral for two seconds at its boot, and the ESC
  armed (Section 6.3);
- the motor spun at **50 %, 70 % and 90 %** command, unloaded;
- the ISD Go app's live throttle readout **mirrored the commanded percentages**,
  so the ESC was reading the pulse widths it was sent;
- with the dome ring coupled, the ring moved under load, with *"friction sectors
  and direction flip resistance noted"*.

That is a **measured** answer to a specification the vendor does not publish, on
one unit and one controller board. It is not a threshold: a 3.3 V source with
slow edges or a sagging supply has not been tried.

> [!TIP]
> **The ISD Go app is a logic analyser you already own.** Its live throttle
> readout is the one piece of return telemetry this otherwise write-only part
> offers, and it splits "is the signal right" from "is the mechanism moving" in
> one glance. The bench order that settled the 3.3 V question is the one to
> repeat on any bring-up: **app throttle readout, then unloaded spin, then loaded
> mechanism**. If the readout matches the command but the loaded dome stalls, the
> problem is torque or mechanics, not the signal. The app is also, per Section
> 9.2, the only place the ESC's error state is legible.

## 6. Arming and calibration

### 6.1 Throttle calibration is mandatory, and it locks everything else

ISDT's own note, from the Chinese text of the manual:

> *"注：未进行油门行程校准时其他所有选项皆不能进行设置！"*
> -- until throttle travel calibration has been done, **no other option can be
> set at all**.

The app shows this as a red `!` on **Remote Calibration** at the top of the
configuration screen, and the FAQ lists `Throttle Not Calibrated` among its error
states. So the order of operations for a new ESC is fixed: calibrate first,
configure second. **No setting in Section 8 can be applied to an uncalibrated
ESC.**

### 6.2 The calibration sequence, transcribed from the manual's flowchart

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

The ESC learns its endpoints from whatever source calibrates it. A transmitter
with non-default end-point adjustment teaches endpoints other than
1000 / 2000 us, and a controller that later drives the ESC with exactly
1000 / 2000 us then needs its own endpoint trim, or a recalibration against that
controller. Every step is "hold until you hear N beeps", and the beeps come from
the motor, so somebody has to be listening next to the droid.

### 6.3 The two-second neutral rule is a vendor requirement

> *"After each startup, return throttle to center position and maintain for two
> seconds to clear the error."* -- ISDT FAQ

It also explains the `Receiver Waiting` error in Section 9.1: that error is what
the ESC shows when this hold has not happened.

> [!IMPORTANT]
> **The rule applies after each startup of the ESC, not of the controller.** An
> ESC switched on into a steady 1500 us stream gets its two seconds as a matter of
> course. An ESC powered **before** its controller spends the controller's boot
> window with **no signal at all**, reports `Receiver Lost`, and needs its two
> seconds of neutral once the signal arrives. An ESC power-cycled mid-session gets
> the hold only if the controller sends neutral for two seconds after it comes
> back; otherwise the error has to be cleared from the app.

### 6.4 The neutral deadband

The last step of calibration sets the throttle mid-point deadband. The manual's
advice is to leave it alone -- *"for most transmitters keep default; only when the
motor turns with the throttle at centre, and recalibration does not help, set a
larger neutral deadband value"* (Chinese text, paraphrased in the English notes).

A controller whose neutral is a fixed 1500 us from a hardware timer has none of
the stick drift the deadband exists to absorb. The deadband is still there,
though: a command within it of 1500 us does nothing, and its width is not
published (Open Item 7). A controller that scales its full command toward
neutral, for a speed cap, can put full command inside it: at a 10 % cap full
command is 1550 us, and the dome does nothing at any command.

## 7. Running modes, and the one that matters

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

### 7.1 Why mode (c) is the only correct one for a droid

A droid controller's dome command is a **signed speed**, mapped onto a pulse
either side of neutral. Whether it comes from a web slider, an RC channel, a
sequence step or an idle routine, there is no notion of a gesture, a click, or a
stick returning to centre between commands.

| Mode | What a `-0.3` dome command does |
| --- | --- |
| a. Forward with brake | **Brakes. Never reverses.** Half the dome's travel is unreachable |
| b. Forward/Reverse with brake **(factory default after calibration)** | **Brakes.** Reverses only if a later command re-enters the reverse zone from neutral *and* the motor has already stopped |
| **c. Forward and Reverse** | **Reverses immediately.** What a signed speed means |

> [!CAUTION]
> **Mode (b) is the default, and it fails in a way that looks like a mechanical
> problem.** A droid in mode (b) will turn one way fine and refuse the other, or
> turn the other way only sometimes -- which reads exactly like a sticky ring, a
> weak motor or a friction sector. Whenever *"direction flip resistance"* is
> reported on a dome, mode (b) is the first thing to rule out.

### 7.2 What mode (c) costs

Two of the ESC's features are defined in terms of the other modes, and choosing
(c) gives them up:

- **Active brake enable** is stated by the manual to be *"only effective in
  forward and reverse with brake mode"* -- so in mode (c) it does nothing. (The
  app guide says something different; Section 8.4.)
- **Braking as a distinct command** disappears. In mode (c) the reverse zone is
  reverse, so stopping a moving dome means commanding neutral and letting it coast
  -- or enabling **Active drag brake**, which is the one braking mechanism mode (c)
  *does* have (Section 8.2).

For a dome this is the right trade: a dome has inertia but no forward direction of
travel to arrest, and a coast-to-stop is gentler on a slip ring and a gear train
than a commanded brake.

## 8. Every ISD Go setting

Reachable only over Bluetooth through the switch module (Section 2), pairing by
long-press from powered-down until the blue LED blinks, 5 m range.

### 8.1 The settings the manual defines

| Setting | What ISDT says it does |
| --- | --- |
| **Remote Calibration** | Throttle travel learning. **Blocks every other setting until done** |
| **Running mode** | Section 7's three options |
| **Battery type / cell count** | Sets the automatic cutoff threshold |
| **Low voltage protection** | *"cut off the power output once the voltage is lower than the set data"*; **Auto** (by battery type) or **manual 5.0-12.0 V** |
| **BEC voltage** | *"manual adjustment from 5.0V to 7.5V (0.1V step)"*. *"Wrong BEC voltage setting may lead to damage to the servo or other electrical equipment"* |
| **Motor rotation** | *"moving to the left is the equivalent of the motor turning counter-clockwise, and moving right is clockwise"* |
| **PWM frequency** | *"A lower driving frequency. Motor output will be stronger, the throttle will feel more punchy due to the higher volume of torque; ... higher driving frequency, Motor will output smaller torque while being more defined and rotating smoother with lesser noise, but it leads to increasing heating of the ESC"* |
| **Starting Force** | *"The larger the value, the higher sensitivity of throttle response and the motor increasing throttle output"* |
| **Braking force** | *"The larger the value, the higher sensitivity of braking response/force"* |
| **Active drag brake level** | Mode (c) only. *"Ensure A non-closed value and the throttle is in the neutral position, the ESC will automatically generate a force that hinders the movement of the motor"* |
| **Ramp Anti-Skid lock** | Not a separate switch -- it **is** drag brake in mode (c). Section 8.2 |
| **Active brake enable** | *"only effective in forward and reverse with brake mode. When this value is set to on, it can produce greater braking force"* |
| **Throttle curve** | Stepless. *"In the default novice mode, the maximum power output is limited to 70%"* |
| **Brake curve** | Stepless. *"In the default novice mode, the maximum braking force is limited to 70%"* |
| **Custom startup sound** | Cosmetic |

The app additionally offers **preset modes** -- *"On road, Drift, Off road, Rock
crawler, Custom"* -- which set several of the above at once, and **curve presets**
-- *"Novice, Standard, Violent, Custom"*.

> [!WARNING]
> **Out of the box the ESC is throttling itself to 70 %.** The manual's phrase is
> *"the default novice mode"*, for both the throttle and the brake curve. A
> builder who calibrates, sets Running mode, and stops there is commanding a dome
> through a curve that caps output at 70 % -- and will then find the dome weak and
> reach for Start Force, which is not the thing limiting it. **Set the curve
> deliberately**, with its endpoints at saturation (+/-100 input -> +/-100
> output), which is what takes the cap off.

### 8.2 Active drag brake is mode (c)'s only brake, and it is also a holding torque

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
> **Both settings are real options for a dome, and the trade is worth stating.**
> Disabled means the dome **coasts** to a stop and can be turned by hand --
> gentler on the drive train, and the behaviour most R2 builders expect. Enabled
> means the dome **stops promptly and holds**, at the cost of a standing current
> at neutral, heat in a motor that is not turning, and the *"will not be stable in
> place"* judder ISDT warns about if the level exceeds what the mass wants.
>
> If a dome needs to hold a heading on a sloped surface, or stop faster at the end
> of a move, this is the setting -- and it is the **only** braking mechanism
> available in mode (c). Open Item 2.

### 8.3 What the settings do *not* include

There is no ramp-rate, acceleration-limit or slew setting. **Start Force and the
throttle curve are the whole of the ESC's response shaping**, and both are
static: they change how output follows the *current* command, not how fast the
command may change. Any real ramping has to come from the host. In mode (c) a
command that jumps from full forward to full reverse is obeyed at once, with the
motor still spinning forward.

### 8.4 Where ISDT contradicts ISDT

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
> in mode (c) on any command past half reverse -- so a `-60 %` dome command would
> behave differently from `-40 %` for reasons the controller sending it cannot
> see.
>
> `UNKNOWN`. With the setting disabled the question is moot, which is one reason
> to leave it disabled. Open Item 3 is the bench test that settles it.

A second, smaller one: the app guide says Active drag brake *"only take effect
when the running mode is [forward and reverse]"*, while the manual's phrasing
(*"Adjust the Forward /Reverse mode under this item"*) is vague enough to read
either way. The Chinese text is unambiguous -- 正反转模式, mode (c) -- so the app
guide is right and the English manual is loose. No conflict in substance.

## 9. Protections and error reporting

### 9.1 What the ESC watches, and what it says

From the manual's feature list: *"battery low-voltage protection, over-temperature
protection, throttle out-of-control protection, BEC over-voltage and
under-voltage protection"*. The FAQ turns those into the messages the app shows:

| Error | ISDT's stated remedy | What it means for a droid |
| --- | --- | --- |
| **Receiver Waiting** | *"Center throttle for 2 seconds; recalibrate if needed"* | The arming hold has not happened (Section 6.3) |
| **Receiver Lost** | *"Check connections; verify PWM signal is 1ms-2ms"* | Signal absent or out of range -- a dead GPIO, a broken wire, a floating signal line, or a controller that has not booted |
| **Motor Not Connected** | *"Check motor connections"* | A dome motor wire has come off |
| **Over Current** | *"Check for shorts or excessive load"* | Effectively unreachable on a dome load (Section 4.3) |
| **Battery Over/Under Voltage** | *"Use appropriate battery"* | The 2-3S window (Section 4.2) |
| **Temperature High** | *"Wait for cooling"* | 90 C external is the stated ceiling |
| **Throttle Not Calibrated** | Complete calibration | Blocks every other setting (Section 6.1) |

A steady 1500 us stream is an armed, stopped ESC; no pulse at all is
`Receiver Lost`, an error state that needs clearing. So the safe idle output for
this ESC is **neutral, never a floating pin**.

### 9.2 The reporting is thinner than the protections

> [!IMPORTANT]
> **There is no beep-code table and no LED-code table in any ISDT document.** The
> manual documents beeps only for the *calibration* sequence, and the switch
> module's LED only as:
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
> The throttle lead has **no return path**: the ESC is a write-only device to
> whatever drives it. A host cannot learn that the ESC is in protection,
> unpowered, uncalibrated or in the wrong running mode; all it knows is what it
> last commanded. The mitigations that exist are all mechanical: put the switch
> module where its LED can be seen, and keep a phone paired.

## 10. How the hobby drives a dome (non-normative)

No astromech project read for this sheet drives an ESC70 (Section 3's negative
result). What they do instead is worth recording, because it is what a visiting
builder will expect.

### 10.1 The community's usual dome controller is a SyRen 10 on packet serial

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
   to the *input*, not the output.
2. **A capped maximum** (`DOMESPEED = 80` of 127, about 63 %), here baked in as a
   constant.
3. **A hardware command timeout** (`setTimeout(950)`) so the controller stops if
   the host stops talking. **An ESC70 has no equivalent** -- it holds the last
   pulse for as long as the pulse keeps coming. A host driving an ESC70 that
   wants a stop on a silent command source has to supply that timeout itself
   (Section 11).

## 11. How the ESC70 and the SyRen 10 differ

| | **ISDT ESC70** | SyRen 10 |
| --- | --- | --- |
| Control signal | **RC servo PWM, 1000-2000 us** | packet serial, simplified serial, R/C or analog |
| Host cost | **one PWM-capable GPIO** | a UART for the serial modes; one PWM pin in R/C mode |
| Configuration lives | **on a phone, over Bluetooth** | in DIP switches and EEPROM |
| Continuous current | **70 A** | 10 A |
| Input voltage | **2-3S LiPo / 6-8 cell NiMH** | up to 24 V |
| Host-side readback | **none** -- write-only | none in R/C mode; serial modes still do not report speed |
| Hardware command timeout | **none** | `setTimeout()`, armed by the host |
| Braking | drag brake only, in mode (c) | regenerative |
| BEC | **3 A at 5.0-7.5 V, adjustable** | 5 V, small |

The ESC70 is reached by a PWM write and configured on a phone; it is **mute**: no
readback, no error reporting to the host, and no hardware watchdog, so every
safety property a dome on it has is one the host implements. The SyRen's
host-armed timeout protects a droid when the *host* fails, not only when the
*command* stops.

## 12. Quick Reference

- Field: Control signal. Value: **standard RC servo PWM, 1000-2000 us, neutral 1500 us**. Nothing else -- S.BUS, PPM, DSM2, DSMX and 750 us narrow PWM are refused by name.
- Field: Frame rate. Value: no vendor figure. **50 Hz** is measured working.
- Field: Logic level. Value: no vendor figure. **3.3 V** is measured working on one unit.
- Field: Neutral. Value: **1500 us**. A steady neutral is an armed, stopped ESC; no pulse is `Receiver Lost`. **Never float the pin.**
- Field: Arming. Value: **hold neutral 2 s** after each ESC startup. Vendor rule: *"return throttle to center position and maintain for two seconds to clear the error."*
- Field: Command timeout. Value: **none in the ESC.** It follows the last pulse; any timeout is the host's.
- Field: Running mode. Value: **Forward and reverse** (mode c) for a signed speed. **Not** the post-calibration default, which is Forward/Reverse *with brake* and reverses only on a second push.
- Field: Throttle calibration. Value: **mandatory, and it blocks every other setting** until done. Sequence: end position at power-on -> neutral (1 beep) -> full throttle (2 beeps) -> full brake (3 beeps) -> 2 beeps -> restart.
- Field: Curve preset out of the box. Value: **Novice, which caps output at 70 %.** Set the endpoints to saturation or the dome is quietly throttled.
- Field: PWM frequency. Value: adjustable. Lower = more low-end torque and more noise; higher = smoother and hotter. 1 kHz pushes torque into the low end a dome lives in.
- Field: Start force. Value: adjustable. High or max is the main breakaway lever on a dome load.
- Field: Active drag brake. Value: mode (c)'s only brake and also a position-hold torque.
- Field: Active brake. Value: the manual and the app guide disagree about when it even applies.
- Field: Battery. Value: **2-3S LiPo or 6-8 cell NiMH only.** Never a hoverboard pack, never 4S+.
- Field: BEC. Value: **5.0-7.5 V, 3 A, adjustable in 0.1 V steps -- an OUTPUT on the throttle lead's centre wire.** Set 5.0 V. Do not wire it to a controller's supply pin.
- Field: Current rating. Value: 70 A continuous / 120 A peak, 1500 W on 3S. **Over-current protection will never fire on a dome load.**
- Field: Maximum temperature. Value: **90 C external**.
- Field: Waterproofing. Value: **IP65** per the vendor FAQ.
- Field: Dimensions / weight. Value: **38.6 x 31.6 x 17.15 mm**, ~49 g, plus a ~4.5 g switch module.
- Field: Configuration surface. Value: **ISD Go app over Bluetooth, through the switch module, 5 m range.**
- Field: Error reporting to the host. Value: **none exists.** Red flashing LED means "an error"; the app is the only place to read which.

## 13. Open Items

| # | Item | How to settle it |
| --- | --- | --- |
| 1 | **The ESC's accepted frame rate** | No vendor figure. Try 50 Hz against 100 Hz and 200 Hz and watch the app's throttle readout. Only matters for a faster dome update |
| 2 | **Active drag brake as dome position-hold** (Section 8.2) | Enable at a low level, in mode (c), and measure: does the dome stop faster, hold heading, and stay cool? It is the only brake mode (c) has |
| 3 | **Active brake: mode-gated or 50 %-stroke-gated?** (Section 8.4) | In mode (c), enable it and command -40 % then -60 %. If they differ, the app guide is right and the manual is wrong |
| 4 | **What actually protects a jammed dome** (Section 4.3) | Stall the ring deliberately at low command and watch current, motor temperature and ESC temperature. The ESC's 120 A limit will not fire; something else has to |
| 5 | **Thermal behaviour over a show-length run** | The 90 C ceiling has never been approached in testing, and a dome is a light load -- but nobody has measured it after an hour of idle rotation |
| 6 | **Does the ESC hold configuration across a firmware OTA of its own?** | ISDT advertises OTA firmware updates. Whether a settings profile survives one is undocumented and would invalidate a tuned profile silently |
| 7 | **Where the neutral deadband ends** (Section 6.4) | Step the pulse out from 1500 us until the motor turns, in each direction, and record the pulse width. Gives a controller a real minimum command instead of `0` |

## 14. Sources

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

**Bench**

- One ESC70 on an ESP32 LEDC output and a real dome ring, 2026-03-21/22: the
  3.3 V / 50 Hz result, the app throttle readout and the loaded-ring behaviour
  (Sections 4.3 and 5.2).

**Community, read from source**

- ShadowMD -- `Shadow_MD_DualController_Template.ino`, the SyRen 10 command path
  and the `isDomeMotorStopped` / `serialLatency` idioms.
- Padawan360 (DY-SV5W port) -- `DOMESPEED`, `DOMEDEADZONERANGE`, `autobaud()`,
  `setTimeout(950)`.
- BetterDuino firmware V4, AstroPixelsPlus, CHIRP -- negative results for dome ESC
  control.

> [!NOTE]
> **Negative results, recorded so nobody repeats them.** ISDT publishes **three**
> documents for this part and no more: a manual, a FAQ, and an app menu guide.
> There is no datasheet, no errata, no beep-code table, no LED-code table and no
> stated logic threshold. **No astromech project read for this sheet drives an
> ESC70**, ISDT does not market it for droids, and the builder forums
> (`forums.astromech.net`, `droidwiki.astromech.net`) did not resolve at research
> time -- so the astromech evidence for this part is one bench record and nothing
> else. Numeric ranges and factory defaults for Start Force, Braking Force and
> Active Drag Brake Level are **not published** in any of the three documents;
> the app is the only place they exist, and reading them needs the hardware
> paired.
