# Network is optional and never load-bearing for droid function

The ESP32-P4 target reaches WiFi through an external backend (on the FireBeetle
2, a fitted ESP32-C6 over SDIO running ESP-Hosted), and the upstream issue
record says that transport will wedge. The first supervision design for #189
proposed "detect a dead link, re-init, escalate to controlled reboot" — an
automatic host reboot on persistent network failure. Epic #182's safety
premise says the opposite: SBUS, 50 Hz drive continuity, latching estop and the
watchdog never touch WiFi, so *the worst hosted-WiFi failure is a web UI
outage*. Both could not stand.

We decided the premise is binding, and stated it as **Network-Optional
Operation** (CONTEXT.md):

- **A network fault never restarts the controller and never degrades a droid
  function.** Network-backend recovery is bounded and stays at the backend
  (co-processor reset / re-init); it reaches a stable degraded state — droid
  fully functional, no web UI — and never a host reboot. `requestSystemRestart`
  keeps its operator-initiated callers only.
- **A Board Variant may declare no network backend at all.** Such a board has
  no web server and every droid function. There is no at-least-one-backend
  compile check. The network seam (#188) must treat zero backends as a legal
  composition; compiling the web stack out of a no-network image is deferred
  until such a board exists.
- **Persistent network failure is announced by the droid itself** — a system
  sound cue, a dome logic-text message, and the serial log — because the
  usual place to report it (`/api/status`) is unreachable exactly when the
  network is the thing that failed.

## Considered options

- **Automatic host reboot, deferred to "safe idle" or a safe-stop handshake**
  — converts a WiFi fault into a drive interruption on a droid that was driving
  fine without WiFi; "idle" cannot be proven from inside the controller; and
  the first FireBeetle unit (#198) carries a C6 fault that survives host reboot,
  so it would loop. Rejected.
- **A `static_assert` that every Board Variant declares at least one WiFi (or
  network) backend** — closes the "0/0 board" hole, but makes a network
  mandatory for a product whose droid functions do not need one. Rejected; the
  hole is closed by defining what a no-network board *is* instead.
- **Reporting co-processor health only in `/api/status`** — honest but
  unreachable when the co-processor is the transport. Rejected as the sole
  signal.

## Consequences

- #189 loses "escalate to controlled reboot"; gains bounded recovery to a
  stable degraded state, co-processor-only reset, a permanent-fault hardware
  proof on the #198 unit, and the no-web-UI failure signalling.
- #188's seam accepts zero backends without redesign.
- "Recovery without a power cycle" means recovery at the network backend. A
  wedge that survives a co-processor reset needs a power cycle to restore the
  web UI — accepted.
- Revision trigger for the deferred stripping: the first Board Variant that
  declares no network backend.
