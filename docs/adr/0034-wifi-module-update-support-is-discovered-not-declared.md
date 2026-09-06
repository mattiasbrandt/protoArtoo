# WiFi Module Update Support is discovered, not declared

**Status**: accepted

A **WiFi Module Update** replaces the firmware on a fitted wireless co-processor — on
firebeetle2, the ESP32-C6. Whether that module will accept an update is a property of a
firmware protoArtoo does not build, does not ship and cannot declare at compile time, so
**WiFi Module Update Support** is established by asking the module and is read through
ESP-IDF's `esp_hosted_get_coprocessor_fwversion()` rather than through the Arduino
wrapper. This departs from two documented rules, and both departures are deliberate.

## Why it is discovered rather than declared

`CONTEXT.md`'s **Feature Availability** is explicit that availability is *"declared by the
image and reported to the browser once; it is never discovered by probing endpoints"*, and
its `_Avoid_` list names *"endpoint probing, feature detection"*. That rule exists to stop
us probing **our own** features, which we compile and can therefore simply declare. It has
no purchase on a foreign image: nothing in our build knows what firmware a particular C6
left the factory with, and the **Board Capability Gate** already promises the opposite —
it *"does not attest successful co-processor provisioning, boot, initialization, or
runtime reachability"* (ADR 0029).

Measured, board 2, 2026-08-29: the factory C6 answers the SDIO link and carries WiFi, and
refuses `Req_OTAWrite` at offset 0 and `Req_GetCoprocessorFwVersion` alike. No declaration
we could have written would have predicted that.

## Why the version read bypasses the Arduino wrapper

`hostedGetSlaveVersion()` (`cores/esp32/esp32-hal-hosted.c:71-75`) copies out a file-static
initialised to `{0, 0, 0}` (`:54`) and returns `void`. There is no error channel, so a
refused RPC and a module genuinely reporting `0.0.0` are indistinguishable through it.

`hostedHasUpdate()` (`:109-141`) then compounds it: when the version RPC fails it logs the
error, does **not** return, leaves `slave_version` at `0`, and reaches
`host_version > slave_version` — so a module that cannot report its version is reported as
**having an update available**, with no signal that anything failed. Building the update
offer on that call would invite an update precisely on the boards where it is guaranteed
to fail.

Calling `esp_hosted_get_coprocessor_fwversion()` directly is not vendoring a second hosted
stack: it is the same component's public API, one layer below a wrapper that cannot
express *unknown*. The Arduino wrapper remains the interface for the transfer itself
(`hostedBeginUpdate` / `hostedWriteUpdate` / `hostedEndUpdate` / `hostedActivateUpdate`).

## Considered and rejected

- **Treat `0.0.0` as a sentinel for "unreadable".** Cheapest, and it is the
  absent-read-as-zero pattern this project already forbids in its own soak harness. It is
  also unfalsifiable: a module that genuinely reported `0.0.0` would be indistinguishable
  from a broken read.
- **Patch or fork the Arduino HAL.** Correct upstream, but a framework-package edit is the
  shared mutable surface that has already produced build-corruption incidents on this
  machine. Reporting it upstream is worthwhile; depending on the fix is not.
- **Derive support from a pinned version instead of asking.** Circular: a factory module
  will not report a version either, so there is nothing to look up for exactly the
  population that needs one.
- **Learn support only by attempting a real transfer.** The honest proof, but
  `esp_hosted_slave_ota_begin()`'s erase semantics are undocumented in
  `esp_hosted_ota.h`, and unprovable here — this bench has no module that accepts it. The
  safe probe is the only probe available.

## Consequences

- The three states — **unknown** (never asked, module not on the bus), **not supported**
  (asked and refused), **supported** — are distinct, and unknown is never rendered as not
  supported: one sends the operator to check the module, the other to a wired path this
  project has already ruled out as a builder path (#198 Phase B).
- An unreadable version fails the update closed, before any transfer, with an explicit lab
  override for a hypothetical module that implements OTA but not version reporting.
- Support is per-module and per-image, not per-board: a Board Capability Gate says a
  **WiFi Module** exists, never that it can be updated.
