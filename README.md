# protoR2

**Open-source ESP32 body-controller firmware for R2D2 (astromech) droids: build
sequences on a timeline, wire and set up your droid from the browser, and drive
it by RC or web. Pluggable drive, sound, radio, dome and servo hardware.**

protoR2 runs on the Body Controller in your droid's body: the board that
owns the Foot Drive, the body servos and lights, the sound module, Dome
Rotation and the link to the Dome Controller. You set it up, wire it and run
it from any web browser, with no app and no rebuild. Every source line is open.

[Get started](#get-started) | [Features](#features) | [What it supports](#what-it-supports) | [Safety](#safety)

## Features

### Set up and wire your droid

- **A guided Setup.** A freshly flashed droid walks you through what it is made
  of, one question at a time, and ends with a summary of where it stands.
- **Say which droid you built.** Pick your Dome Design and Body Design, each at
  its variant, and the parts it carries are fitted for you.
- **Wiring, one table.** Every part, the output it is on, the serial links, the
  Dome ESC and the radio, named by what your board prints. Print it as a
  wiring sheet with a box to tick for each wire.
- **Find by Moving.** Not sure which output a part is on? The droid twitches
  each free output in turn; press That one when your part moves.
- **Calibrate by eye.** Drive a servo with a dial, then press Set MIN, Set
  CENTER or Set MAX. Set its speed, its ease and what it does at power-up.
- **Backup and restore**, Sequences included.

### Build Sequences on a timeline

- **Drag and drop.** Body parts, dome panels, lights, sounds and Dome Rotation
  each land on a lane of their own. Every edit is one Undo.
- **Gestures.** One move across many parts, like a wave round the ring, written
  once and paced by the droid.
- **A Sequence Tempo.** Type the BPM or tap it, then put steps on the beat.
- **Sequences inside Sequences**, linked, so improving one improves them all.
- **The Rehearsal** reads what you wrote when you save it and says what will
  not happen as you meant it. It never stops a save.
  [Authoring sequences](docs/sequence-authoring.md)

### Control it from a browser or RC

- **The Dashboard** draws your droid's body and dome. Click a door to open it,
  press a chip to run a show, pin the ones you use.
- **RC Radio or none.** PWM and SBUS receivers, with every channel
  mapped and calibrated in the browser. No radio fitted? Drive from the web.
- **Moods** set how alive the droid is when nobody touches it: Quiet,
  Mid-Awake, Full-Awake and Awake+.
- **Marcduino commands you already know**, including your ShadowMD and
  Padawan360 bindings. [Command reference](docs/commands.md)

### Dome and lights

- **Dome Rotation** by RC or a slider, with a speed cap and random idle turns.
  Time one turn and the droid tracks where it believes the dome points.
- **Lights, on one page.** Logic displays, PSIs and Magic Panel in the dome,
  and an LED strip on as many body wires as your board has.
- **Two-way protoR2link** to the Dome Controller, so the dome can fire body
  sounds and moves too. [Dome companion](#dome-companion)

### Sound

- **Pick your sound module on Configuration.** One firmware carries every
  supported driver; the choice takes effect at the next start.
- **Named Tracks**, random chatter that follows the Mood, and ShadowMD's
  bank-and-sound numbering. [Sound reference](docs/sound_playback.md)

### Controller Console

- **Type to the droid** from the Dashboard or over a USB cable: the same
  commands, the same answers, the same safety rules.
- **It keeps answering when the web stops**, which is when you need it most.
  [Console guide](docs/console.md)

### Updates

- **One firmware and one filesystem image per board** on every release.
  Upload both from the Firmware page; your settings and calibration stay.

## What it supports

The products in each family are peers: fit the one you own.

<table>
<tr>
<td width="33%" valign="top">
<img src="docs/images/families/body-controller.svg" width="28" height="28" alt=""><br>
<b>Body Controller</b><br>
<a href="https://www.artoo.uk/">Artoo PCB</a><br><a href="docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md">FireBeetle 2 (ESP32-P4)</a>
</td>
<td width="33%" valign="top">
<img src="docs/images/families/radio-controller.svg" width="28" height="28" alt=""><br>
<b>Radio Controller</b><br>
<a href="docs/spec-sheets/hotrc-ds650-radio.md">HotRC DS-650</a><br>RC Radio<br><a href="docs/spec-sheets/rc-receiver-spec.md">RC Receiver - PWM</a><br><a href="docs/spec-sheets/sbus-protocol.md">RC Receiver - SBUS</a><br><a href="docs/spec-sheets/elrs-crsf-radio.md">RC Receiver - ELRS</a>
<br><sub>Roadmap: <a href="docs/spec-sheets/xbox-controller-input.md">Xbox Controller</a></sub>
</td>
<td width="33%" valign="top">
<img src="docs/images/families/body-servo-controller.svg" width="28" height="28" alt=""><br>
<b>Body servo controller</b><br>
<a href="docs/spec-sheets/servo-communication.md">Body controller board GPIO</a><br><a href="docs/spec-sheets/pca9685-servo-expander.md">PCA9685</a>
<br><sub>Roadmap: <a href="docs/spec-sheets/pololu-maestro-servo-controller.md">Pololu Maestro</a></sub>
</td>
</tr>
<tr>
<td width="33%" valign="top">
<img src="docs/images/families/dome-rotation.svg" width="28" height="28" alt=""><br>
<b>Dome Rotation</b><br>
<a href="docs/spec-sheets/isdt-esc70-dome-esc.md">ISDT ESC70 (RC ESC)</a>
<br><sub>Roadmap: <a href="docs/spec-sheets/sabertooth-syren-packet-serial.md">SyRen 10</a></sub>
</td>
<td width="33%" valign="top">
<img src="docs/images/families/dome-controller.svg" width="28" height="28" alt=""><br>
<b>Dome Controller</b><br>
<a href="docs/spec-sheets/astropixels-dome-controller.md">AstroPixels Plus</a>
<br><sub>Roadmap: <a href="docs/spec-sheets/teeces-dome-lighting.md">Teeces</a></sub>
</td>
<td width="33%" valign="top">
<img src="docs/images/families/foot-drive.svg" width="28" height="28" alt=""><br>
<b>Foot Drive</b><br>
<a href="docs/spec-sheets/hoverboard-hacked-firmware-foot-drive.md">Hoverboard, hacked firmware</a>
<br><sub>Roadmap: <a href="docs/spec-sheets/sabertooth-syren-packet-serial.md">Sabertooth 2x25</a>, <a href="docs/spec-sheets/flipsky-vesc-foot-drive.md">Flipsky Mini V6 VESC</a></sub>
</td>
</tr>
<tr>
<td width="33%" valign="top">
<img src="docs/images/families/sound.svg" width="28" height="28" alt=""><br>
<b>Sound</b><br>
<a href="docs/spec-sheets/dy-sv5w-sound.md">DY-SV5W</a><br><a href="docs/spec-sheets/mp3-trigger-sound.md">MP3 Trigger</a><br><a href="docs/spec-sheets/chirp-audio-trigger-sound.md">CHIRP Audio Trigger</a>
<br><sub>Roadmap: <a href="docs/spec-sheets/dfplayer-mini-sound.md">DFPlayer Mini</a></sub>
</td>
</tr>
</table>

[Full support detail](docs/goal.md#target-hardware-profile)

The Artoo PCB carries a generic ESP32 clone. The FireBeetle 2 builds the full
feature set and ships with every release, but read
[the spec sheet's "Before you buy one"](./docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md#before-you-buy-one)
first. Pins and wiring for both boards: [pin map](docs/pin_map.md).

## Get started

- **[Update from a release](docs/getting-started.md#update-a-running-droid-from-a-release)**:
  download two images, upload them from the Firmware page.
- **[First flash of a blank board](docs/getting-started.md#first-flash-of-a-blank-board)**
  and **[building from source](docs/getting-started.md#build-from-source)**.
- **[Join the droid to your WiFi](docs/wifi-provisioning.md)** from a browser, no
  credentials in the build.
- **[Project status](docs/status.md)** and the [changelog](CHANGELOG.md).

## Dome companion

The dome runs **[mattiasbrandt/AstroPixelsPlus](https://github.com/mattiasbrandt/AstroPixelsPlus)**,
a fork of [reeltwo/AstroPixelsPlus](https://github.com/reeltwo/AstroPixelsPlus)
that speaks **protoR2link**: a two-way link to the body over the slip ring,
with a heartbeat each way and WiFi as the fallback. The body is the droid's
only sound source, and the dome can ask it for sounds and moves, so one
Sequence coordinates both halves. How it compares to a classic MarcDuino build:
[topology](docs/topology.md).

## Safety

- **The emergency stop latches.** It never clears itself; you release it from
  the red STOP on every screen. Estop and sleep take the pulse off every servo.
- **Five independent failsafe layers.** Any one of them holds the droid out
  of drive until it clears. [How they work](docs/failsafe.md)
- **Real-time control runs on its own CPU core**, apart from WiFi and the web
  pages, and no network fault reaches the drive path.

## Contributing and licence

Ideas and bug reports are welcome, and read honestly. Commit format, branches
and the pull request checklist: [CONTRIBUTING.md](CONTRIBUTING.md).

MIT, see [LICENSE](LICENSE) for what it covers: this repository's firmware, web
pages, docs and tooling. Third-party libraries keep their own licences, the
Artoo Controller PCB is [Steve's](https://www.artoo.uk/) hardware design, and
the MK4 droid is [MrBaddeley's](https://www.patreon.com/mrbaddeley) paid design;
no print files or geometry live here.

Star Wars, R2-D2 and related names and marks belong to Lucasfilm Ltd. This is
a non-commercial fan project with no affiliation with, connection to, or
endorsement from Lucasfilm or The Walt Disney Company.

## Acknowledgements

- **[Steve](https://www.artoo.uk/)** designed the Artoo Controller PCB, which
  let this project start on firmware rather than hardware.
- **[Mike Eddington](https://github.com/mikeeddington-lgtm)** wrote
  [r2d2-astromech-simulator](https://github.com/mikeeddington-lgtm/r2d2-astromech-simulator),
  the most carefully made operator interface in the droid-builder space. We
  adapted its ideas: capturing a servo end where the servo already is;
  checking a sequence when it is committed and never refusing the save; a
  timeline of authored moves; picking a part on a drawing of the droid;
  saying on the surface what it cannot show; explanation as a required field;
  one Escape per layer, questions answered in two verbs, quiet receipts. His
  is a simulator; protoR2 configures and moves a real droid.
- **[Mr Baddeley](https://www.patreon.com/c/mrbaddeley)** created the
  3D-printable MK4 astromech, and his
  [Facebook community](https://www.facebook.com/groups/MrBaddeley/) is where
  builders meet.
- **[Printed Droid](https://www.printed-droid.com/)**'s
  [R2-D2 terminology](https://www.printed-droid.com/kb/r2-d2-terminology) is
  how protoR2 names dome parts and takes their panel bearings; their
  drawing is their own work and is not reproduced here.
- **[astromech.net](https://astromech.net/)** and the MarcDuino and SHADOW
  communities. This firmware is meant as a contribution back.
- **Firmware and libraries:** hoverboard firmware by
  [EFeru](https://github.com/EFeru/hoverboard-firmware-hack-FOC) and
  [RoboDurden](https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32),
  dome firmware by [reeltwo](https://github.com/reeltwo/AstroPixelsPlus),
  [PsychicHttp](https://github.com/hoeken/PsychicHttp) and
  [ArduinoJson](https://github.com/bblanchon/ArduinoJson).
- **Product photographs** stay with their owners; provenance per image is in
  [product image provenance](docs/product-image-provenance.md).

*protoR2 was called protoArtoo and began as open firmware for the Artoo
Controller PCB. Today that board is one Body Controller in the table above.*
