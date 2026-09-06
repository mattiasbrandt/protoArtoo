# WiFi Provisioning (Runtime, ADR 0015)

This is the operator-facing guide to getting a protoArtoo controller onto a
network from a browser — no firmware source, no PlatformIO, no editing
`secrets.h`. It covers first-boot setup, choosing an ongoing WiFi mode,
switching modes later, and recovering a controller you've locked yourself out
of. See [ADR 0015](adr/0015-runtime-wifi-provisioning.md) for the design
rationale and [api.md](api.md) for the underlying `/api/wifi` and
`/api/config` contract.

## Release artifacts stay WiFi-mode-free

Public release binaries are published one per **sound backend**
(`artoo_esp32_chirp`, `artoo_esp32_mp3trigger`, `artoo_esp32_dysv5w` —
`<env>-firmware.bin` + `<env>-filesystem.bin`), matching whichever audio
module you have wired. There is no separate "AP build" or "client build" —
every release binary boots the same way and lets you choose WiFi Client Mode
or Standalone AP Mode after flashing, from the browser.

## First boot: WiFi Provisioning

A freshly flashed controller has no saved network settings (an
**Unprovisioned Controller**) and starts **WiFi Provisioning**: it hosts its
own WiFi network using the **Default AP Credential**.

| | |
|---|---|
| Network name (SSID) | `protoArtoo` |
| Password | `protoArtoo1` |
| Browser address | `http://192.168.4.1` |

1. On your phone or laptop, join the `protoArtoo` WiFi network with the
   password above.
2. Open `http://192.168.4.1` in a browser.
3. Go to the **WiFi** page and choose your ongoing mode (below).

The Default AP Credential is public and shared by design — it's a bootstrap
credential, not a security boundary. Change the AP password once you're set
up (see "Changing AP settings" below).

## Choosing an ongoing WiFi mode

The WiFi page (`/wifi.html`) is the one place to view and change network
settings — the Setup page only links to it.

### WiFi Client Mode (recommended)

The controller joins your existing WiFi network. From the **WiFi Client
Settings** section, enter your network name and password, then save.

Once applied, reach the controller at `http://artoo.local` (or your droid's
custom mDNS name, if you've set one on the Setup page) or its IP address from
your router.

**WPA3-only WiFi networks are not supported; use WPA2 or WPA2/WPA3 mixed
mode.** Mixed mode is the common home-router default and accepts the
controller. On a WPA3-only network the join simply never completes.

### Standalone AP Mode

The controller hosts its own network instead of joining yours — useful in
the field, away from any home network. From the **Standalone AP Settings**
section, set a network name and (optionally) a password, then save.

Once applied, join that network and open `http://192.168.4.1`.

## Staged Network Switch: save, then apply

Saving WiFi settings never changes your active connection immediately — the
controller would drop the browser session mid-edit. Instead:

1. **Save** — the WiFi page validates and persists the new settings to the
   controller (`POST /api/wifi`). The page shows them as "pending" alongside
   your currently active settings.
2. **Apply** — use the **Reboot to Apply** button (or power-cycle the
   controller). The new mode takes effect on that reboot.
3. **Reconnect** — the WiFi page tells you exactly where to go next: the AP
   address and network name for Standalone AP Mode, or `artoo.local` / the
   observed IP for WiFi Client Mode.

### Changing AP settings

The AP SSID and password are editable the same way — save new values in the
**Standalone AP Settings** section, then apply. This is how you replace the
shared Default AP Credential with your own password. Leaving the AP password
field blank keeps the network open (no password); 8–63 characters sets a
WPA2 password (ESP32 SoftAP requirement).

### Password fields are write-only

`GET /api/config` and `GET /api/wifi` never return plaintext passwords —
only `staPasswordSet` / `apPasswordSet` flags. When saving settings, leaving
a password field blank keeps the existing saved password; you only need to
type a password when you're actually changing it.

## Ordinary connection trouble is not a WiFi mode change

If WiFi Client Mode is active but the controller can't reach your network
(wrong password, network down, router out of range), the controller stays
in WiFi Client Mode and reports the disconnected state — it does **not**
automatically fall back to hosting its own AP. That would be surprising and
would hide the actual problem. If you need to reach the controller while
it's stuck like this, use Network Recovery Mode.

## Network Recovery Mode

Network Recovery Mode is an explicit local action for when saved WiFi
settings leave the controller unreachable (wrong password, unreachable
network, etc.) and you have no other way in.

**To enter it:** power-cycle the controller **3 times in a row**, each cycle
landing before the previous boot has been running long enough to count as
stable. (This only counts true power-on resets — a crash, watchdog reset, or
an ordinary dropped WiFi connection never triggers it.)

Once latched, the controller temporarily starts WiFi Provisioning — same
Default AP Credential and address as first boot — **without erasing your
saved Device WiFi Settings**. From there:

1. Join the `protoArtoo` AP and open `http://192.168.4.1`.
2. On the WiFi page, correct your Client or AP settings and save.
3. Reboot to apply — the controller returns to its normal saved posture.

If you don't change anything and just reboot, the controller comes back up
with its previous (still-broken) saved settings and Network Recovery Mode
ends.

## Developer WiFi Shortcut (self-build only)

`make setup-wifi` (writes `src/secrets.h`, gitignored) is a convenience for
developers building from source: it lets a self-built firmware image boot
straight into a known WiFi posture without going through provisioning first.
It has no effect on public release binaries and is never required to use a
downloaded release — `secrets.h` is not part of the release networking
contract.
