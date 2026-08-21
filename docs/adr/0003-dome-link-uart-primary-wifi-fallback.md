# protoR2link transport priority: UART slip ring primary, WiFi fallback

The protoR2link dome-body link uses two transports: a physical UART serial connection
through the dome's slip ring (GPIO33 TX / GPIO34 RX on the body controller), and a WiFi
UDP connection discovered via mDNS. We designate the UART slip ring as the **primary**
transport and WiFi UDP as the **fallback**.

The firmware probes for the UART slip ring at a regular interval. When the slip ring
is reachable the system promotes to UART and stays there. When the slip ring is absent
or unreachable, the system falls back to WiFi UDP. The active transport is reported as
`dome_link.transport` in the `/api/status` response and must be surfaced to the operator
in the main dashboard UI.

## Considered options

- **WiFi primary, UART fallback** — treat WiFi as the preferred path because it doesn't
  require a physical slip ring connection. Rejected: the slip ring is a dedicated,
  bounded-latency link that is always present on a fully assembled droid; WiFi shares
  bandwidth with other traffic, is subject to interference, and introduces non-deterministic
  latency for dome commands. The slip ring is more appropriate for the primary dome-motion
  control path.
- **UART only, no WiFi path** — simpler ownership model; no mDNS probing; no UART2 sharing
  problem. Rejected: WiFi fallback allows partial dome operation during development and
  bench testing when the slip ring is not wired, or during a dome-assembly phase where
  the ring connector is disconnected. It also allows the operator to recover dome
  connectivity without physically inspecting wiring.
- **UART primary, WiFi fallback (chosen)** — matches the physical assembly model (slip ring
  is the production path), preserves development convenience via WiFi, and gives the
  operator a clear signal ("WiFi (fallback)" in the UI) that the primary link is degraded.
