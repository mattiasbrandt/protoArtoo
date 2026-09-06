# Soak testing a controller — `tools/soak.py`

A **soak** holds a protoArtoo controller's web stack under continuous load for
as long as you ask — minutes, or a whole evening — and then says, in one
sentence and one exit code, whether it held up.

It exists because the failures worth catching in a web stack are not the ones a
smoke test finds. A dashboard that loads once tells you nothing about a board
three hours into an open live-update stream: heap fragmentation creeps, sockets
leak, a stream stalls without closing, a link supervisor gives up quietly. Those
show up in *duration* and in *churn*, so this tool supplies both and watches the
controller's own counters while it does.

`tools/soak.py` is a permanent instrument. It is not tied to any board, any
epic, or any ticket: point it at a controller, declare which firmware image is
on it, and it will read that image's `/api/status` correctly or refuse to run.

**It only reads.** It never flashes, never calls `make ota`, never writes
configuration. The one write it makes at all is `POST /api/c6/reset` on the
bench image, which is the whole point of the driver that makes it.

---

## Contents

- [Before you start](#before-you-start)
- [Running a soak](#running-a-soak)
- [Image Modes](#image-modes)
- [Soak Drivers](#soak-drivers)
- [The Run Verdict and the exit codes](#the-run-verdict-and-the-exit-codes)
- [What to watch while it runs](#what-to-watch-while-it-runs)
- [What a run leaves behind](#what-a-run-leaves-behind)
- [Where the thresholds come from](#where-the-thresholds-come-from)
- [`--self-test`](#--self-test)
- [Adding a new Image Mode](#adding-a-new-image-mode)

---

## Before you start

**Install the dependencies.** The harness needs `rich`, which is in
`tools/requirements.txt`:

```bash
pip install -r tools/requirements.txt      # or, on Arch: pacman -S python-rich
```

Everything else it uses is in the Python standard library.

**Check the board before you commit hours to it.** A soak that starts against
the wrong firmware wastes the whole session, so read `/api/status` first:

```bash
curl -s http://artoo.local/api/status | python3 -m json.tool | head -20
```

Confirm, in this order:

| Check | Why it matters |
| --- | --- |
| The controller answers at all | A soak against an unreachable board is `INVALID`, not a finding |
| `firmwareVersion` is the commit you meant to soak | Evidence from the wrong image answers a question nobody asked |
| No `-dirty` suffix on `firmwareVersion` | A dirty build cannot be reproduced, so its evidence cannot be re-derived |
| For an artoo-esp32 image, `xtensa-esp-elf-nm --defined-only .pio/build/artoo_esp32/firmware.elf \| grep -c btdm_` is above zero (the tool lives in `~/.platformio/packages/toolchain-xtensa-esp-elf/bin`) | Two framework configurations share one toolchain pool on a shared build machine, and an image linked against the other one is about 83 KB smaller with Bluetooth compiled out. The release configuration keeps it in, and the envelope check cannot tell the two apart. Zero means the wrong libs: rebuild before you soak |
| `fsVersion` matches the firmware's expectation | A stale filesystem changes what the dashboard does under the same firmware |
| It is the image you are about to declare with `--image` | See [Image Modes](#image-modes) — a mismatch is refused at preflight |

The default mDNS names are `artoo.local` for an artoo-esp32 controller and
`firebeetle2.local` for a FireBeetle 2; the two differ deliberately so both can
sit on one LAN. If mDNS is flaky, use the IP (`GET /api/wifi` → `staIp`).

**Close your browser tabs.** An open dashboard holds an `/api/events` stream,
and the number of concurrent streams a controller admits is deliberately small
— the run prints the exact cap in its footer, and
[Where the thresholds come from](#where-the-thresholds-come-from) says where it
is read. A soak run at the cap will contest it with your own tab and report a
concurrency it never actually reached. Either close every tab, or run below the
cap and say which you did.

**Nobody else drives the board during the run.** OTA, a second soak, or a
dashboard someone left open on another machine all change what is being
measured.

---

## Running a soak

The minimum:

```bash
python3 tools/soak.py --device artoo.local --image artoo
```

That runs every driver with the defaults: a 30-minute SSE soak at the compiled
client cap, a two-minute reconnect storm, and the reset-recovery driver (which
refuses on that image — see [Soak Drivers](#soak-drivers)). A realistic
overnight-ish invocation:

```bash
python3 tools/soak.py \
  --device artoo.local --image artoo \
  --driver sse_soak --duration 10800 \
  --json ~/soak/2026-09-03-artoo.json
```

`stdout` carries the JSON report and nothing else, so `> report.json` and
`| jq` both work. Everything a human reads goes to `stderr`.

The flags worth knowing about:

| Flag | What it selects |
| --- | --- |
| `--device`, `--port` | Which controller. `--device` is required; there is no default that would be right for every board. |
| `--image` | Which `/api/status` schema to read. Declared, never sniffed. |
| `--build-env` | Which PlatformIO environment the board is running, and therefore which compiled thresholds it is judged against. Defaults to the product environment for `--image`. |
| `--driver` | `all` (the default), or one of `sse_soak`, `reconnect_storm`, `c6_reset_recovery`. |
| `--duration` | How long the SSE soak holds its streams open. Seconds. Hours are what it is for. |
| `--num-clients` | How many concurrent streams the SSE soak holds. Defaults to the cap the firmware compiles in. |
| `--json` | Also write the report to a file — and write it *periodically*, so a kill leaves the measurements taken so far. |
| `--log` | Where the plain transcript goes. Always written; defaults beside `--json`, or to `./soak-<timestamp>.log`. |
| `--progress-interval-s` | Seconds between heartbeat lines (default 30). `0` silences the cadence entirely. |
| `--no-progress` | No stderr output at all. The transcript and the checkpoints still happen. |

`python3 tools/soak.py --help` documents every flag, including the timing
budgets, with the reasoning for each default.

**You can stop a run.** `Ctrl-C` (or `SIGTERM`) stops the drivers, gathers the
post-run readings they were about to take anyway, and writes a report. It is
never reported as a pass, and never as a failure either: see
[the verdicts](#the-run-verdict-and-the-exit-codes). A second `Ctrl-C` kills the
process outright, and the last `--json` checkpoint on disk survives that.

---

## Image Modes

An **Image Mode** is which firmware image's `/api/status` schema the harness
reads. Different images publish the same measurement under different names, in
different shapes, and some do not publish it at all.

| Mode | Image | Notable differences |
| --- | --- | --- |
| `artoo` | the `artoo_esp32` product image | No `bootCount`; `resetReason` is a name, not a number; no recovery-ladder block (the board has no companion radio); no reset route |
| `shipping` | the `firebeetle2` product image | Same as `artoo`, plus the recovery ladder nested under `hostedLink`; still no reset route (#243) |
| `bench` | `bringup/p4_hosted_bench.cpp`, built by `firebeetle2_hosted_bench` | Built to be measured: `bootCount`, the raw reset-reason enum, flat ladder counters, a reset route, and an `/api/events` stream whose payload is a monotonic frame counter |

**The mode is declared and then checked. It is never sniffed.** You pass
`--image`, and at preflight the harness verifies the payload really has that
image's shape — including the fields that must be *absent*. If it does not, the
run stops with `INVALID` and names the mismatch (and, when the payload matches
exactly one other mode, suggests it).

That is deliberate. Sniffing would turn a truncated or half-built response into
a confident claim about which firmware is on the board, and every field read
afterwards would be silently re-labelled. A wrong declaration should fail
loudly at second zero, not produce three hours of mislabelled numbers.

The same principle runs through the whole harness:

> **A field that is absent reads as absent, never as zero.** A `bootCount`
> defaulting to `0` turns "the board did not reboot" into an unfalsifiable
> claim. Where an image genuinely cannot supply a reading, the report says so in
> words instead of showing you a number that means nothing.

---

## Soak Drivers

A **Soak Driver** is one named scenario. Each yields its own verdict, and each
may be **Unavailable** on a given Image Mode. `--driver all` runs them in order.

### `sse_soak` — does a long-held live-update stream stay healthy?

Holds `--num-clients` concurrent `/api/events` streams open for `--duration`,
polling `/api/status` throughout. It fails on:

- a stream that stalls, ends early, or never starts (`--early-stall-check-s`
  gives up quickly on a stream that never delivers a first frame, rather than
  burning the whole duration on a dead one);
- a break in frame continuity — arithmetic on the bench image, whose payload is
  a counter; arrival timing, event names and forward-moving frame ids on a
  product image, whose stream carries no counter;
- the controller rebooting during the run;
- the controller **refusing work**: `refusedHeapFloor` / `refusedHeapFloorDiag`
  are its own count of requests it turned away, and a rise across the run is a
  failure whatever the heap readings look like;
- the largest free 8-bit block dropping below the level the firmware refuses
  ordinary requests at (see
  [Where the thresholds come from](#where-the-thresholds-come-from));
- the recovery ladder reaching `degraded`, on an image that has one — that is
  the bounded transport-failure recovery giving up for the boot.

Run **above** the client cap and the driver still measures, but reports
`OBSERVATION_ONLY` and carries no verdict: past the cap the firmware refuses the
extra stream by design, so what is being measured is admission working, not the
transport holding up.

### `reconnect_storm` — does the server survive churn?

Concurrent workers repeatedly open an `/api/events` stream, hold it briefly, and
**abort it mid-stream** with a TCP reset rather than a clean close. Then a settle
window, and a look at whether the controller came back to where it started.

It fails on connect failures, on transport faults during a deliberate hold, on
leaked sockets (the client count not returning to baseline after settling), on
the heap not recovering within `--heap-recovery-tolerance-pct`, and on a reboot.

Every so many cycles it holds one stream open for longer than the silence budget
on purpose. Without such a cycle, sub-second holds can never say anything about
the stream staying *alive* — and a run in which no cycle could judge liveness
reports exactly that, rather than passing. A driver that can pass by having
stopped looking is not a measurement.

### `c6_reset_recovery` — does the link come back on its own?

Only available on the `bench` Image Mode. It schedules a reset of the companion
WiFi Module through `POST /api/c6/reset`, then watches for the host to
re-establish the link **without rebooting itself**, and for a fresh SSE stream to
start advancing again.

On both product images it is **Unavailable**: there is no reset route to
provoke. It refuses before sending anything, and says which of the two reasons
applies — the FireBeetle 2 has a companion radio and simply has no route yet
(#243); artoo-esp32 has no companion radio at all, so there is nothing to reset.

An unavailable driver is never a pass. It collapses the Run Verdict to
`INVALID`, because a coverage gap is not evidence of health. If you want an exit
code that covers only what this image can actually measure, name the drivers you
want explicitly rather than using `--driver all`.

---

## The Run Verdict and the exit codes

Each driver produces a verdict. The **Run Verdict** is the single verdict for
the whole run, and it speaks the same words its drivers do.

| Verdict | Exit | Means |
| --- | --- | --- |
| `PASS` | `0` | Every driver that ran found nothing wrong |
| `FAIL` | `2` | At least one driver watched the controller and found it wanting |
| `INVALID` | `3` | The run could not be judged: a driver was unavailable, the device was unreachable, the declared image did not match, a response broke its contract, or a threshold could not be resolved |
| `INTERRUPTED / INCOMPLETE` | `3` | You stopped it. It covers only the window actually observed, so it concludes nothing about the window it did not |
| *(self-test only)* | `1` | `--self-test` had a failing assertion |

`FAIL` and `INVALID` are different answers and the distinction is the point.
`FAIL` says the controller misbehaved. `INVALID` says nobody found out.

**The exit codes and the JSON artefact's keys are this tool's stable surface**
and may be relied on; the verdict *wording* is prose and may be reworded
(ADR 0035, `docs/adr/0035-soak-harness-stable-surface.md`). If you are scripting
around this tool, switch on the exit code and read the JSON — not the sentence.

> ⚠️ **One ambiguity to know about if you script it.** A *usage* error — an
> unknown flag, a missing `--device` — also exits `2`, because that is
> `argparse`'s standard status for a bad command line. The two are easy to tell
> apart: a real run always writes a JSON object to `stdout`, and a usage error
> writes nothing there. Check for the report, not just the code.

The artefact carries `schemaVersion` (currently `4`). It is bumped when a key is
removed or changes meaning; adding a key does not bump it. A consumer that
ignores unknown keys is unaffected by an addition — check the version before
relying on anything else.

---

## What to watch while it runs

On `stderr`, a heartbeat line per `--progress-interval-s`, plus (on a terminal)
one status line refreshing every second so a long wait never looks like a hang:

```
=== protoArtoo soak ===
  started       2026-09-03T12:36:31+0200
  device        artoo.local:80
  image mode    artoo (build env artoo_esp32)
  soak drivers  sse_soak 3:00:00
  planned       3:00:00
  log           /home/you/soak/2026-09-03-artoo.json.log
  report        /home/you/soak/2026-09-03-artoo.json
  firmware      2026.09.03-a1b2c3d
  filesystem    2026.09.03-a1b2c3d
[>>] 1/1 sse_soak: starting
[1/1 sse_soak] run 0:02:00 | drv 0:02:00/3:00:00 (1.1%) left 2:58:00 | frames=360 perClientFrames=[120,120,120] polls=24 pollsUnreachable=0 clients=3 clientsHeldOpen=3 heapLargest8bit=41284 floorMargin=32284 refusedHeapFloor=0 refusedHeapFloorDiag=0 refusedSseCap=0 sseEvicted=0
```

What those readings are telling you:

| Reading | Watch for |
| --- | --- |
| `frames`, `perClientFrames` | Should climb steadily and stay roughly even across clients. One client that stops climbing is a stalled stream. |
| `clients` vs `clientsHeldOpen` | The controller's own count of open streams against what the harness is holding. They should match. |
| `heapLargest8bit`, `floorMargin` | The fragmentation reading and its distance from the level the firmware starts refusing ordinary requests at. A margin trending toward zero is the interesting shape; a margin below zero is a failure. |
| `refusedHeapFloor`, `refusedHeapFloorDiag` | The controller's own count of work it turned away. **Any** movement is a finding. |
| `refusedSseCap`, `sseEvicted` | Streams refused at the client cap, and streams the broadcaster dropped for missing their send deadline. Movement means something is contesting the cap, or a reader is not keeping up. |
| `pollsUnreachable` | Failed `/api/status` polls. The transcript has the reason for each; the heartbeat only carries the running count so a bad minute is not a flood on your terminal. |
| `recoveryLadder*` | Present only on an image that has a link supervisor. `degraded` is terminal for the boot. |
| `?` | This image publishes that reading, but *this sample* did not carry it. Never confuse it with a zero — and a reading the image does not publish at all is left off the line entirely. |

The status line disappears at the end; the heartbeats stay in the scrollback and
in the transcript.

---

## What a run leaves behind

**The JSON report** — on `stdout`, and additionally at `--json` if you gave one.
It carries the run header (device, Image Mode, build environment, the exact
`/api/status` paths each reading came from, and the thresholds it was judged
against), the Run Verdict, and each driver's own report: its verdict, its
reasons in plain sentences, and every number behind them. The SSE soak also
carries an uncapped per-poll `heapSeries`, so the *shape* of a run is
recoverable afterwards — "touched 11 764 once" and "sat near 12 000 for twenty
minutes" are different findings.

With `--json`, the file is rewritten periodically during the run through an
atomic replace, so a hard kill or a power cut leaves the last checkpoint intact
rather than a half-written file. A checkpoint is labelled
`IN PROGRESS / INCOMPLETE` — a verdict no finished run can carry, so you can
never mistake one for a conclusion.

**The transcript** — every line `stderr` showed, with no colour and no cursor
control, plus the per-event detail that was too noisy for the terminal. Appended,
never truncated, so pointing two runs at one path keeps both. Its path is in the
report as `logPath`, so the next tool does not have to parse a terminal to find
it.

---

## Where the thresholds come from

Two numbers decide what a run means, and **neither is written into the harness**.
Both are read out of the tree the harness ships in, per build environment, so a
worktree judges its own branch's firmware:

| Threshold | Read from | What it decides |
| --- | --- | --- |
| `PA_ADMISSION_MIN_LARGEST_FREE_BLOCK` (and its `_DIAG` and override siblings) | `platformio.ini`, `[flags_base]` | The heap verdict: the level the firmware itself refuses ordinary requests at |
| `PA_ADMISSION_MAX_SSE_CLIENTS` | `include/web_event_stream.h` (an `#ifndef` default), plus any `-D` the build environment adds | The concurrency verdict: how many streams count as "at the cap", and therefore what `--num-clients` means |

A copy of either number inside the harness would be a second source of truth
with nothing keeping the two in step, and both would rot silently rather than
loudly: a stale floor judges a controller against a level it no longer has, and
a stale cap quietly changes what `--num-clients 3` *means*.

If a threshold cannot be resolved — an unknown `--build-env`, an environment
nobody calibrated a floor for, a header that stopped declaring the cap — the run
is `INVALID` **before the controller is touched**. It is never defaulted. A
verdict taken against a number the harness invented would carry the look of
evidence and none of the substance.

The heap verdict is deliberately *not* "the largest free block fell N% below the
baseline sample". That rule was tried and removed: it failed a healthy board
whose free heap never moved, every refusal counter read zero, and the block
recovered fully the moment the clients left. A percentage of an arbitrary sample
measures how spiky a fragmentation reading is. What an operator needs to know is
whether the controller was still admitting work — so that is what is measured.

`--heap-recovery-tolerance-pct` still governs the *recovery* comparisons, where a
before/after percentage is the right question: did the heap come back after the
storm, and after the reset.

---

## `--self-test`

```bash
python3 tools/soak.py --self-test
```

Runs the whole harness offline against a local `http.server` fixture: no device,
no network beyond loopback. It exits `0` when every scenario passes and `1` on
any failing assertion.

It is worth running before a device session, and worth understanding if you
change the harness, because of one structural rule:

> **The self-test may not contain a parse loop of its own.** It starts a fixture
> serving byte-exact frames and drives the *production* entry points against
> them — the same `BenchClient.stream_sse()`, the same continuity wiring, the
> same schema readers, the same `run()`. There is exactly one SSE parser in the
> file, and the self-test reaches it the same way a device run does.

That rule exists because it was broken: an earlier revision's self-test had its
own inline parse loop, so breaking the real parser outright left it reporting
all green. A test that does not drive the production path proves nothing about
it. If you add a scenario, drive a production entry point and assert on what it
returns.

---

## Adding a new Image Mode

When a new board or a new firmware image arrives, teaching the harness to read
it is one class and one registry entry. Everything about "which field is which"
lives in `StatusSchema`; nothing about it lives in the drivers.

1. **Read the firmware first.** Open the function that builds the payload
   (`buildStatusJson()` for a product image) and the route table that registers
   the endpoints. Do not derive a field map from another image's schema, and do
   not derive it from a captured payload — a capture cannot tell you which
   fields are conditional.

2. **Subclass the closest existing schema** in `tools/soak.py`. Two product
   images that differ only by a board capability should be siblings under
   `ProductImageStatusSchema`, not forks of each other. Set:

   - `name`, and `build_env` — the PlatformIO environment that builds it, which
     is where its thresholds are read from;
   - the field map: `heap_field`, `heap_free_field`, `heap_min_field`,
     `sse_clients_field`, `restart_field` and `restart_verb`;
   - `reset_reason_kind`, plus a `reset_reason()` that classifies what this
     image publishes as crash-shaped, not crash-shaped, or **unknown** — three
     states, because "the mapping collapses this case" and "the device started
     cleanly" are different claims;
   - `restart_detected()` and `restart_report()`, under this image's own key
     names. A `bootCount` and a `uptimeMs` are not the same measurement, and one
     must never be published under the other's name;
   - `link_readiness()` — affirmative evidence only. A missing field means the
     evidence is absent, which is not the same as the link having failed, and
     the diagnostic should say so;
   - `structural_mismatches()` — see step 3;
   - `continuity_tracker_class`, if the stream's shape differs from both
     existing models.

3. **State the absences positively.** For every field this image does *not*
   publish, set the corresponding `publishes_*` / `enforces_*` flag to `False`
   and supply a `*_absence_note` that says why in words. Then have
   `structural_mismatches()` check the absence *is* an absence: a payload that
   started publishing the field is a different image and must be refused, not
   read through a schema that assumes the field cannot exist. Absence checked in
   only one direction is how the wrong board gets accepted.

4. **Register it** in `SCHEMAS`. `--image` takes its choices from that mapping,
   so nothing else needs to know.

5. **Add a fixture and self-test scenarios.** Build the fixture body from the
   real payload's shape — including the neighbouring fields nothing reads, so
   the fixture looks like the payload rather than like the reader's wishlist.
   Where a new image is another one minus a block, derive it by subtraction
   rather than retyping it, so the shared part cannot drift. Then assert in
   *both* directions: the new payload satisfies the new schema, and each of the
   other images' payloads is refused by it.

6. **Say what the new image cannot measure.** If a driver has nothing to provoke
   on it, make that driver return `UNAVAILABLE` with a diagnostic naming the
   reason for *this* board. Do not stub it, do not skip it, and never let it
   report a pass it did not earn. That is the failure mode this whole harness
   was rebuilt to avoid.

`test/test_tools/test_soak_schema.py` holds the drift guards that read the
firmware sources a schema was derived from; add rows there so a renamed payload
field turns this suite red instead of surfacing on a bench day.

---

## See also

- `docs/adr/0035-soak-harness-stable-surface.md` — what this tool promises and
  what it does not.
- `docs/adr/0030-event-stream-evicts-rather-than-queues.md` — why the product
  `/api/events` stream is project-owned and caps its clients.
- `docs/adr/0032-network-optional-operation.md` — why a network fault must never
  restart the controller, which is one of the things the reset-recovery driver
  is checking.
- `docs/api.md` — the REST and SSE contracts the harness reads.
- `docs/troubleshooting.md` — what to do when a soak turns up a crash.
