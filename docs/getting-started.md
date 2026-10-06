# Getting started

From a release download to a droid on your WiFi, and building from source when
you want to change the firmware.

- [Update a running droid from a release](#update-a-running-droid-from-a-release)
- [First flash of a blank board](#first-flash-of-a-blank-board)
- [Join the droid to your WiFi](#join-the-droid-to-your-wifi)
- [Build from source](#build-from-source)

## Update a running droid from a release

Already running protoR2? You do not need to build anything to update.

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

A blank board needs more than the two update images: a bootloader, a
partition table and `boot_app0`, each at its own flash address. A feature
release carries all of them for each board, with `<board>-manifest.json`,
which lists every file with its flash address, size and SHA-256, and names the
chip and the firmware and filesystem versions. Download every file that starts
with your board's prefix (`artoo_esp32-` or `firebeetle2-`) and
`SHA256SUMS.txt`, and check them:

```sh
sha256sum -c --ignore-missing SHA256SUMS.txt
```

This is an initial install, and it erases the whole flash, settings included.
To update a droid that already runs protoR2, use
[the update above](#update-a-running-droid-from-a-release), which keeps them.

Connect the board over USB and run the line for your board in the download
folder. It needs [esptool](https://docs.espressif.com/projects/esptool/) v5
(`pip install esptool`), `jq` and a POSIX shell (on Windows, WSL or Git Bash);
the addresses come from the manifest.

```sh
# Artoo PCB
esptool --chip esp32 write-flash --erase-all $(jq -r '.flash[] | "\(.offset) \(.file)"' artoo_esp32-manifest.json)

# FireBeetle 2 (ESP32-P4)
esptool --chip esp32p4 write-flash --erase-all $(jq -r '.flash[] | "\(.offset) \(.file)"' firebeetle2-manifest.json)
```

A patch release carries no files, and releases from before the blank-board
files existed carry only the update pair. The other way onto a blank board is
[Build from source](#build-from-source) and `make flash`. After the first
flash, every update can come from a release.

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

### Repository layout

```
include/        Headers; config.h holds the pins, component_registry.inc the
                products (generated from docs/products.yaml)
src/            Firmware: tasks/ (FreeRTOS tasks), drivers/ (hardware), web/
                (REST API handlers)
data/           The web pages, uploaded to the board's filesystem
test/           test_native/ (logic, no hardware), test_web/ (page scripts),
                test_tools/, playwright/, stubs/, fixtures/
tools/          Setup and deploy wizards, the Console Client, drift checks
docs/           Guides, spec sheets, ADRs (docs/adr/)
```

### Tests, commit format and releases

The test and static-analysis commands, the commit format, the branch rules and
how a release is cut are in [CONTRIBUTING.md](../CONTRIBUTING.md).
