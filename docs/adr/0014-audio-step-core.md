# Audio Step Core: functional-core audio task loop

The audio task's remaining decision logic — the enable/disable/sleep/init-retry
lifecycle transitions, the per-command translation to playback requests (sleep gating,
relative volume, dollar slot-vs-track selection), the playback-policy invocation, and
the gating of status/catalog work — moves out of the `audioTask()` loop into a pure
decision module, the **Audio Step Core** (`audio_task_step`). This is the second
instance of the **Step Core** pattern established by the protoR2link Arbiter (ADR 0005):
a pure step over explicit state; the task loop gathers inputs, calls the step, and
executes the returned plain-data actions.

Unlike the Arbiter's single step function, the Audio Step Core exposes four phase
functions that mirror the loop's genuine sequential structure, because command
processing mutates state (randomMode, volume, cadence timestamps) that the same
iteration's random tick and auto-query gating must observe:

```cpp
// Arduino-free; compiles in the native test environment.
struct AudioStepState { /* driverInitialized, beginRetryCount, lastAudioEnabled,
                           randomMode, wasSleeping, currentVol, lastRandMs,
                           lastPlayMs, lastAutoQueryMs; default-init == boot state */ };

AudioStepTickActions       audioStepTick(AudioStepState&, const AudioStepTickInputs&);
AudioStepInitResultActions audioStepInitResult(AudioStepState&, bool beginOk,
                                               bool catalogCapable);
AudioStepCommandActions    audioStepCommand(AudioStepState&,
                                            const AudioStepCommandInputs&,
                                            const AudioCommand& cmd);
AudioStepIdleActions       audioStepIdle(AudioStepState&, const AudioStepIdleInputs&);
```

The core calls the playback policy (`audioPlaybackResolveRequest` /
`audioPlaybackResolveRandomTick`, ADR 0010/0013) and the dollar parser internally; both
stay independently tested pure modules behind the step seam. The core also owns the
intent's state effects (randomMode transitions, currentVol, lastPlayMs/lastRandMs
cadence updates), which previously lived in `executePlaybackIntent()`.

The task loop stays as the imperative adapter and owns every side effect: driver calls,
dome-UART acquire/release arbitration, NVS binding-cache refresh, config cache reads,
logging, and all RobotState audio-zone writes (ADR 0012 zone ownership). The
triplicated module-state RobotState write block collapses into one adapter helper. The
CHIRP catalog-refresh execution path is deliberately untouched (TWDT history,
issue #15). Public interfaces survive unchanged: the `audioQueue*` helpers,
`AudioCommand`, and the driver seam.

Accepted behavior corners (analyzed, no observable state difference beyond one 500 ms
iteration of latency on an idle driver):

- A sleep-mode entry that lands on a driver-init iteration is handled one iteration
  later (previously evaluated between init and queue processing).
- The playback config, named tracks, mood, and dome-sequence flag are snapshotted once
  per loop iteration instead of re-read per command (previously up to six extra config
  cache reads per iteration), aligning with the one-generation-per-response snapshot
  semantics of ADR 0012.

## Status

accepted (2026-07-08 architecture-review grilling session)

## Considered options

1. **Two-phase translator (core translates, adapter keeps policy + intent execution).**
   Smallest diff, but the loop's state machine stays split: the core would own
   lifecycle state while `executePlaybackIntent` keeps mutating randomMode, volume, and
   cadence timestamps in the adapter. Two places to understand one state machine;
   rejected for weak locality.
2. **Single step function with cross-iteration init feedback.** The Arbiter's exact
   single-step shape, feeding `driver->begin()` outcomes back as next-iteration inputs.
   Rejected: it distorts the same-iteration init semantics (post-init binding refresh,
   module-state seed, and queue processing all happen in the init iteration today) and
   adds a hidden ordering contract between iterations.
3. **Four phase functions mirroring the loop order (chosen).** tick (disable/init/sleep
   transitions, queue-drain decision), initResult (retry ceiling policy), command
   (translation + policy + intent state effects), idle (random tick + auto-query
   cadence gating). Each phase is small, the calling contract is the loop's natural
   order, and native tests can drive full timelines: disable/enable cycles, init retry
   to the give-up ceiling, sleep gates per command kind, Track Stop vs Quiet
   (ADR 0010), and cadence windows.
