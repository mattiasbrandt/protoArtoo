# protoR2link Arbiter: functional-core transport selection, no virtual transport interface

The protoR2link transport selection logic — UART (slip ring) promotion, WiFi fallback,
the 30 s / 150 ms slip-ring probe cadence, the 1 Hz heartbeat gate, and the sleep-sync
state machine — moves out of the `domeLinkTask()` loop into a pure decision module, the
**protoR2link Arbiter** (named after the existing drive arbiter convention: a module that
picks one winner among competing sources). The arbiter is a single step function over
explicit state:

```cpp
// Arduino-free header; compiles in the native test environment.
struct DomeLinkArbiterState  { /* hb timestamps, probe timers, sleepSynced,
                                  lastSyncedSleepMode; default-init == boot state */ };
struct DomeLinkArbiterInputs { uint32_t nowMs; bool uartHeartbeatSeen;
                               bool staConnected; bool peerKnown;
                               bool bodySleeping; bool domeConnected; };
enum class SleepSyncAction : uint8_t { None, SendSleep, SendWake };
struct DomeLinkArbiterActions {
    DomeLinkTransport txRoute;   // routes queue drain + heartbeat + sleep sync
    bool acquireUart;            // arbiter decides WHEN; the shell does HOW
    bool releaseUartToAudio;     // triggers the UART2 -> audio RX (GPIO35) handoff
    bool sendUartProbe;          // 30 s #PAHB probe on the inactive transport
    bool sendHeartbeat;          // 1 Hz gate
    SleepSyncAction sleepSync;
};
DomeLinkArbiterActions domeLinkArbiterStep(DomeLinkArbiterState& s,
                                           const DomeLinkArbiterInputs& in);
```

The concrete transports (UART2 serial I/O, WiFi UDP heartbeat, HTTP command forwarding,
mDNS peer resolution, the UART2 ownership handoff with AudioTask) stay as concrete
functions in `dome_link.cpp`, reduced to an imperative shell that gathers inputs, calls
the step function, and executes the returned actions. The actions struct is plain data,
so native tests assert on it directly — no fakes, no mocks. Tests are timelines: step the
clock through boot grace, UART steady state, heartbeat loss, WiFi fallback at 5 s, the
30 s probe acquire/send, the 150 ms window release-to-audio, `#APHB` recovery, and
sleep-sync transitions.

Delivery is two slices. Slice 1 is behavior-preserving: the extraction plus timeline
tests that pin current promotion behavior, **including the known deviation** (the 5 s
UART heartbeat timeout makes WiFi the steady-state transport whenever both peers share a
network — see the comment in `dome_link.cpp` and ADR 0003). Slice 2 changes only the
arbiter step plus the deliberately flipped pinned tests: gate WiFi fallback on UART never
having established contact on the current boot, resolving the deviation accepted for
v1.0.0.

Public interface that survives unchanged: `domeQueueTx()`, `domeConnected()`,
`domeUartAcquire()/Release()/OwnedBy()`, and `robotState.domeActiveTransport`
(`/api/status` `dome_link.transport`, per protoR2link Transport Visibility). The dome
sequence timeout watchdog and `dome=seqon`/`dome=seqoff` handling stay in the task loop
untouched — ADR 0004's Suppression Window replaces them. This work lands before the
ADR 0004 Sequence Coordinator MVP slice so the coordinator's dome fallback builds on a
tested transport layer.

## Status

accepted (2026-06-10 design session; design-it-twice comparison of three candidate
interfaces)

## Considered options

- **Virtual `DomeTransport` interface with UART and WiFi adapters (ports & adapters)** —
  an abstract transport with `processRx` / `sendCommand` / `sendHeartbeat` /
  `onActivate` / `onDeactivate`, two production adapters, and a mock adapter for tests.
  Rejected for three reasons. (1) It misrepresents the ground truth: RX is *merged* —
  both transports listen every tick regardless of which is active — and only TX routes,
  so "activate one transport" is the wrong lifecycle; the probe deliberately drives the
  *inactive* transport. (2) What varies across the seam is test-vs-production of the
  *selection policy*, not transport implementations; with actions as plain data the
  policy tests need no transport substitutes at all, so the virtual seam would be
  single-purpose indirection. (3) Virtual dispatch adds per-tick vtable cost on the
  Core 1 real-time path for nothing. **Future architecture reviews should not re-suggest
  a transport adapter interface here** unless a genuinely new transport appears — the
  slip ring and WiFi are the physical reality of the droid and all UARTs are committed.

- **Pure decision functions, but probe cadence left in the task loop** — extract only
  the promote/fall-back decision and sleep sync as pure functions; keep the 30 s probe
  acquire/send/release sequencing inline. Rejected: the probe *is* promotion policy —
  it is the mechanism by which the UART slip ring gets re-promoted, and it is entangled
  with the slice 2 gating fix. A policy module that does not decide "acquire UART now,
  send probe, release to audio at T+150 ms" leaves the most defect-prone behavior
  untestable, which defeats the purpose of the seam.

- **Behavior change bundled with the extraction** — fix the WiFi-steady-state gating in
  the same change that introduces the seam. Rejected: if hardware testing then shows a
  regression, it is impossible to tell whether the extraction or the policy change caused
  it. Pinning current behavior first (deviation included) makes the slice 2 diff small,
  reviewable, and attributable.

- **Inline fix only, no seam** — change the gating logic directly in `domeLinkTask()`.
  Rejected: smallest diff, but the policy stays untestable and this class of deviation
  can recur silently; the known deviation was only discovered through live observation.
