# Getting started

From a release download to a droid on your WiFi, and building from source when
you want to change the firmware.

- [Update a running droid from a release](#update-a-running-droid-from-a-release)
- [First flash of a blank board](#first-flash-of-a-blank-board)
- [Join the droid to your WiFi](#join-the-droid-to-your-wifi)
- [Build from source](#build-from-source)

## Update a running droid from a release

Already running protoArtoo? You do not need to build anything to update.

1. Open the [latest release](https://github.com/mattiasbrandt/protoArtoo/releases/latest)
   and download the two images for your Body Controller:

   | Body Controller | Firmware | Filesystem |
   |---|---|---|
   | Artoo PCB | `artoo_esp32-firmware.bin` | `artoo_esp32-filesystem.bin` |
   | FireBeetle 2 (ESP32-P4) | `firebeetle2-firmware.bin` | `firebeetle2-filesystem.bin` |

2. Upload both from the droid's **Firmware** page.

There is one firmware per board, whatever sound module you fitted: every image
carries every supported sound driver. Pick the module on Configuration
(Hardware components -> Sound); it takes effect at the next start.

## First flash of a blank board

A release carries the firmware and the web pages, not the boot loader a blank
board needs. The very first flash goes over USB from a source checkout:
follow [Build from source](#build-from-source) and run `make flash`. After that,
every update can come from a release.

The FireBeetle 2 is a supported Body Controller, but read
[the spec sheet's "Before you buy one"](spec-sheets/firebeetle2-esp32-p4-spec-sheet.md#before-you-buy-one)
before you order one.

## Join the droid to your WiFi

A freshly flashed Body Controller hosts its own WiFi network, `protoArtoo`,
password `protoArtoo1`. Join it, open `http://192.168.4.1`, and on the **WiFi**
page pick how the droid joins WiFi from then on:

- **WiFi Client Mode** joins your home or workshop network; the droid is then
  at `http://artoo.local`.
- **Standalone AP Mode** keeps the droid on its own network, for shows away
  from home.

No source edits and no build-time credentials. The full flow, changing modes
later, and getting back in when you have locked yourself out are in
[WiFi Provisioning](wifi-provisioning.md).

## Build from source

### Prerequisites

- [VS Code](https://code.visualstudio.com/) with the
  [PlatformIO extension](https://platformio.org/install/ide?install=vscode).
  Open the repo and accept the recommended extensions (`.vscode/extensions.json`).
- Python 3.8 or newer. `make check-deps` lists any missing command or package.
- `make`. The Makefile picks the PlatformIO toolchain for each board; a bare
  `pio run` can swap incompatible toolchains in place.
- `clang-format`, for code style (`.clang-format`).

### Build and flash

```bash
git clone https://github.com/mattiasbrandt/protoArtoo.git
cd protoArtoo

# First time only: OTA address and USB port (writes user.mk, gitignored)
make setup

# Interactive build and deploy wizard
make
```

`make setup` asks for the droid's OTA address and its USB port (leave the port
blank to have it found at flash time). Every shortcut below reads `user.mk`.

```bash
make build        # Compile only
make flash        # USB flash (UPLOAD_PORT=/dev/... if two boards are attached)
make ota          # Flash over WiFi (OTA_IP=artoo.local by default)
make uploadfs     # Upload the web pages only (OTA; FireBeetle 2 over USB)
make monitor      # Serial log, no reset on connect
make console      # Type at the Controller Console over USB
make help         # Every named target
```

**`make setup-wifi` is a developer shortcut.** It writes `src/secrets.h`
(gitignored) so a self-built image boots straight onto a known network. A
release image never needs it: it starts WiFi Provisioning instead.

### Building for the FireBeetle 2

The same targets take `BUILD_ENV=firebeetle2`, and `make` selects the ESP32-P4
toolchain for it:

```bash
make build BUILD_ENV=firebeetle2
make flash BUILD_ENV=firebeetle2 UPLOAD_PORT=/dev/ttyACM0
```

Wiring and both boards' pin maps are in [`pin_map.md`](pin_map.md). The chip
revision, allocation tables and known issues are in the
[spec sheet](spec-sheets/firebeetle2-esp32-p4-spec-sheet.md).

### Host firewall and OTA

Every OTA target has the droid connect back to your computer on port
`OTA_HOST_PORT` (default `32320`, overridable in `user.mk`). If your computer
blocks incoming connections by default, allow that port from the droid's
network, or `make ota` fails with `[ERROR]: No response from device`. The rule
and the reason are in
[Troubleshooting](troubleshooting.md#ota-fails-with-error-no-response-from-device-host-firewall).

### Tests, commit format and releases

The test and static-analysis commands, the commit format, the branch rules and
how a release is cut are in [CONTRIBUTING.md](../CONTRIBUTING.md).
