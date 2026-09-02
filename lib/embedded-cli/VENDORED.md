# embedded-cli Vendored Library

## Source

- **Repository**: https://github.com/funbiscuit/embedded-cli
- **Vendored commit**: `8e796cbf2263f055d43b6ad99b9ddc8feece4cfd` (2026-03-11, post-PR #57 home/end/delete keys)
- **License**: MIT (see `LICENSE` file)
- **Status**: Maintenance mode; bug and feature PRs still merged; features go to the Rust rewrite

## Why embedded-cli

Rationale documented in `tasks/serial-interface-embedded-cli-deep-dive.md`:

- Fixed static buffers, zero heap in the byte/line path
- Complete line editing with history, tab completion
- Overflow-safe command buffer with built-in line ending support
- Structured dispatch via `onCommand` callback with catch-all
- Small footprint: 3.5 KB flash object (xtensa, all six patches), 20 B static regardless of catalog size (neither Patch 5's catalog completion callback nor Patch 6's history filter pays a per-entry RAM cost)
- Stack frame 96 B max (see measured table below)

## Configuration

Intended configuration (compile-time):

```c
EmbeddedCliConfig config = {
    .rxBufferSize = 256,           // UART/USB RX ring
    .cmdBufferSize = 256,          // Command line buffer
    .historyBufferSize = 512,      // In-RAM command history
    .enableAutoComplete = false,   // Live suggestions off; Tab still completes
    .invitation = "> "             // Prompt
};
config.cliBuffer = staticBuffer;   // Caller provides fixed allocation
```

Object Size Analysis (Measured at commit 9950006; Patch 5 row re-measured 2026-08-29;
Patch 6 rows measured 2026-09-02)

Measured with both toolchains under `-Os`:

| Toolchain | State | .text | .rodata | .bss | max stack frame |
|-----------|-------|-------|---------|------|-----------------|
| xtensa | upstream | 3,430 B | 238 B | 20 B | 96 B |
| xtensa | patched (1-4) | 3,190 B | 118 B | 20 B | 96 B |
| xtensa | patched (1-5) | 3,606 B | 118 B | 20 B | 96 B |
| xtensa | patched (1-5), re-measured 2026-09-02 | 3,506 B | 118 B | 20 B | 80 B |
| xtensa | patched (1-6) | 3,526 B | 118 B | 20 B | 80 B |
| riscv32 | upstream | 4,342 B | 259 B | 20 B | 112 B |
| riscv32 | patched (1-4) | 3,854 B | 132 B | 20 B | 96 B |
| riscv32 | patched (1-5) | 4,284 B | 132 B | 20 B | 96 B |
| riscv32 | patched (1-6) | 4,308 B | 132 B | 20 B | 96 B |

**Deltas (patched 1-4 - upstream):**
- xtensa: .text -240 B, .rodata -120 B, .bss 0, max frame 0 B
- riscv32: .text -488 B, .rodata -127 B, .bss 0, max frame -16 B

**Deltas (patched 1-5 - patched 1-4):**
- xtensa: .text +416 B, .rodata 0, .bss 0, max frame 0 B (`embedded_cli.c`'s own file-wide max frame is unchanged at 96 B - `getExternalAutocompletedCommand` inlines into `onAutocompleteRequest` at `-Os`, whose own frame grows 48 B -> 80 B, still under the file's existing 96 B max)
- riscv32: .text +430 B, .rodata 0, .bss 0, max frame 0 B (same shape as xtensa)

**Deltas (patched 1-6 - patched 1-5), both measured in one session with the
compilers named below:**
- xtensa: .text +20 B, .rodata 0, .bss 0, max frame 0 B
- riscv32: .text +24 B, .rodata 0, .bss 0, max frame 0 B

That is the cost of one NULL test, one indirect call and the branch around
`historyPut` - a callback pointer's worth of code and nothing else. `.bss` is
untouched because the pointer itself lives in `struct EmbeddedCli`, which the
caller allocates: the whole cost in RAM is **+4 B of the caller's `cliBuffer`**
(`embeddedCliRequiredSize()` grows by one pointer), not of this object's static
data. `src/tasks/console_task.cpp`'s 2 KB static buffer already checks the
required size at init and has room for it.

> **The xtensa `patched (1-5)` row does not reproduce on today's toolchain**,
> and the 2026-09-02 re-measurement row above records what it actually reads:
> 3,506 B and an 80 B max frame, against the 3,606 B / 96 B recorded on
> 2026-08-29. `git log lib/embedded-cli/src/embedded_cli.c` shows no source
> change between the two measurements (`65e87d1` is still the last commit to
> touch it), and riscv32 reproduces its 4,284 B exactly, so this is the xtensa
> compiler version moving, not the patch set. It is recorded rather than
> corrected in place because the older row is a true record of what that
> toolchain produced. Compilers used for the 2026-09-02 rows:
> `xtensa-esp32-elf-gcc (crosstool-NG esp-14.2.0_20251107) 14.2.0` and
> `riscv32-esp-elf-gcc (crosstool-NG esp-14.2.0_20260121) 14.2.0`. Deltas are
> only meaningful within one row-pair measured by one compiler - which is why
> the Patch 6 delta above is stated against the re-measured baseline, not
> against the 2026-08-29 number.

`.bss` is unchanged by Patch 5 in `embedded_cli.c` itself - the new candidate pool
(`pa_console_completion::g_argCandidatePool`, 256 B) lives in
`include/console_completion.h`, compiled into the *caller's* translation unit
(`src/tasks/console_task.cpp`), not into `embedded_cli.c`. See "Patch 5" below
for that file's own object-size and stack-frame numbers, and #238's closing
comment for the measured whole-firmware delta on both boards.

**Measurement commands:**
```bash
xtensa-esp32-elf-gcc -Os -mlongcalls -I include -fstack-usage -c embedded_cli.c -o out.o
xtensa-esp32-elf-size -A out.o          # sum .text* / .rodata* / .bss*
sort -t$'\t' -k2 -rn out.su | head -1   # largest stack frame

riscv32-esp-elf-gcc -Os -march=rv32imafc_zicsr_zifencei -I include -fstack-usage -c embedded_cli.c -o out.o
riscv32-esp-elf-size -A out.o
sort -t$'\t' -k2 -rn out.su | head -1
```

**Critical findings:**
1. **Patches reduce code size, not increase it.** The project-help ownership patch sets `cliInternalBindingCount = 0` and guards `initInternalBindings()`, allowing the compiler to eliminate dead code (internal help binding and its strings). This accounts for the .rodata reductions (xtensa -120 B, riscv32 -127 B).

2. **Max stack frame is 96 B, not 80 B.** Prior research in `tasks/serial-interface-embedded-cli-deep-dive.md` reported 80 B, but re-measurement shows 96 B on both toolchains. This difference is significant for stack allocation (e.g., #217 Console task sizing). The larger frame comes from `embeddedCliProcess` context and local buffers.

3. **Prior research figures NOT reproduced.** The earlier document claimed Flash measurements of 4,299 B (xtensa) and 4,591 B (riscv32) for object code. Measured here: 3,190 B and 3,854 B (patched). The prior figures may have included linking overhead, runtime stubs, or different compilation flags not documented at the time. These measurements use `-Os` with no additional optimizations.

**Per-binding and total allocations remain as documented (for the still-live
binding path other callers may use):**
- Per binding: 20 B (`sizeof(CliCommandBinding)`)
- Static BSS without bindings: 20 B
- Stack max frame (re-measured, corrected from prior research): 96 B

**As shipped, neither board uses the binding path for completion at all**
(zero bindings registered - Patch 5's catalog completion callback,
"Board Differences" below). Both boards' actual cost is the library object
itself (3.5 KB flash, all six patches, xtensa; 4.3 KB riscv32) plus the
~256 B RAM candidate pool in `include/console_completion.h` - a flat cost
independent of catalog size, replacing the per-board binding-subset sizing
this subsection originally worked through. #238's closing comment on the
tracking issue carries the measured whole-firmware flash/RAM delta on both
boards against the #233 baseline.

## Patches Applied

The vendored source includes six required patches. Each is mechanical, documented, and offered upstream as a PR candidate.

### Patch 1: Safe Enter - No Auto-completion on Enter

**Problem**: `onControlInput` (line 818-820) calls `onAutocompleteRequest` unconditionally on every Enter/Return, even when live autocomplete is disabled. A typed unique prefix silently expands to the bound alias name and executes.

Example:
```
> so    (types unique prefix)
< [expanded to sound_rand_general]
Command executes: sound_rand_general
```

**Solution**: Gate the autocompletion call on the `CLI_FLAG_AUTOCOMPLETE_ENABLED` flag.

**File**: `src/embedded_cli.c`
**Lines**: 818-820 (in onControlInput)
**Change**: Wrap the `onAutocompleteRequest` call with `if (IS_FLAG_SET(impl->flags, CLI_FLAG_AUTOCOMPLETE_ENABLED))`

**Upstream candidacy**: Yes, this is a safety improvement and upstream merges PRs.

**Test**: `test/test_native/test_cli_safe_enter/test_cli_safe_enter.cpp` - verifies that typing a unique prefix and pressing Enter executes the typed text, not the expansion.

### Patch 2: Safe Overflow - Explicit Line-Too-Long Rejection

**Problem**: When a command line exceeds `cmdBufferSize - 2`, the library silently drops the excess bytes (lines 793-794). On Enter, the truncated line executes.

Example:
```
> [very long command exceeding 256 bytes]
[bytes beyond 256 are silently discarded]
[truncated line still executes on Enter]
```

**Solution**: Explicit rejection of overlong input. The listener counts bytes since the last line ending; when overflow would occur, it stops feeding and returns `line-too-long` error to the operator.

**Listener responsibility**: Detect overflow, discard to CR/LF, and clear the library's partial buffer (see below).

**Library patch (optional)**: Add a public `embeddedCliResetInput()` function (~5 lines) to clear the partial command buffer without modifying other state. Allows explicit reset when the listener detects overflow.

**File**: `src/embedded_cli.c`
**Lines**: Add new function after `embeddedCliProcess` (around line 570)

```c
void embeddedCliResetInput(EmbeddedCli *cli) {
    PREPARE_IMPL(cli);
    impl->cmdSize = 0;
    UNSET_U8FLAG(impl->flags, CLI_FLAG_OVERFLOW);
}
```

**Alternative (workaround)**: Listener feeds NUL bytes (count = bytes that exceeded buffer) to reset the buffer without a patch.

**Upstream candidacy**: Yes, defensive programming.

**Test**: `test/test_native/test_cli_safe_overflow/test_cli_safe_overflow.cpp` - verifies that input beyond 256 bytes is rejected with an explicit error and does not execute.

### Patch 3: UTF-8 Ingestion - Accept High-Bit Bytes

**Problem**: `isDisplayableChar` (line 1176-1178) rejects bytes >= 0x80.

```c
static bool isDisplayableChar(char c) {
    return (c >= 32 && c <= 126);  // Rejects UTF-8
}
```

Non-ASCII SSIDs (and other UTF-8 configuration values) are silently corrupted or discarded when typed over serial.

**Solution**: Accept all bytes >= 0x20 (space) into the line buffer. Validation of UTF-8 integrity and meaning is the Console parser's job, not the input layer's.

**File**: `src/embedded_cli.c`
**Lines**: 1190-1196 (isDisplayableChar)
**Change**: Remove the upper bound check and cast to unsigned: `return ((unsigned char)c >= 0x20);`

**Critical note on the unsigned cast**: On x86 and most embedded systems, `char` is signed by default. A direct comparison `c >= 0x20` treats high-bit bytes (0x80-0xFF) as negative numbers and rejects them. The `(unsigned char)` cast is load-bearing: without it, the patch would silently accept the UTF-8 bytes into the buffer but discard them on ingestion, defeating the entire purpose.

**Rationale**: The serial protocol and Console dispatcher will validate UTF-8 encoding and meaning. The input layer should pass bytes unchanged.

**Upstream candidacy**: Yes, UTF-8 support is expected in modern embedded systems.

**Test**: `test/test_native/test_cli_utf8_ingestion/test_cli_utf8_ingestion.cpp` - verifies that high-bit bytes enter the line buffer unchanged and are not silently discarded.

### Patch 4: Project-Help Ownership - No Internal Help Binding

**Problem**: The library binds the name `"help"` internally (line 940-942, `initInternalBindings`). A Console command literally named `help` is shadowed by the internal binding.

**Solution**: Skip `initInternalBindings` so the project's `help` command owns the name.

**File**: `src/embedded_cli.c`
**Lines**: 
- Line 210: Set `cliInternalBindingCount = 0` (was 1)
- Line 512 (in `embeddedCliNew`): Comment out or conditionally skip the call to `initInternalBindings(impl)`

**Alternative (if structured help is not wanted)**: Keep the internal binding and name the structured Console discovery command `operations` or `actions`.

**Upstream candidacy**: Conditional. If upstream plans to keep the internal help, this becomes project-specific. If they plan to make it optional, this is a candidate patch.

**Test**: `test/test_native/test_cli_project_help/test_cli_project_help_ownership.cpp` - verifies that a project-owned `help` command is not shadowed by the library binding.

### Patch 5: Catalog Completion Callback

**Problem**: `getAutocompletedCommand` (line 1020) and Tab's candidate listing in
`onAutocompleteRequest` (line 1105, before this patch) only ever compare a typed
prefix against `impl->bindings` - the array `embeddedCliAddBinding()` fills. The
project's runtime catalog (##219, ##238) carries 175+ operation names plus their
argument keys; registering all of them as `CliCommandBinding`s would cost 20 B
each (`sizeof(CliCommandBinding)`) - roughly 3.5 KB, most of the artoo-esp32
board's remaining static RAM headroom after #233 (9,100 B free) - just to make
completion see them. The board's Console task registers **zero** bindings today
(`src/tasks/console_task.cpp` sets `onCommand` and never calls
`embeddedCliAddBinding`), so before this patch Tab completed nothing on either
board.

A second, narrower gap: the library's whole-buffer matching model has no
concept of "the operation name is already typed, now complete the *next*
token" - `getAutocompletedCommand` compares a candidate's full text against
the *entire* command buffer as one prefix, so it cannot complete an argument
key (`speed=`) that comes after an already-typed operation name and a space.

**Solution**: Add an optional external completion source,
`EmbeddedCli::getCompletionCandidate(cli, index)`, enumerated by index
(0, 1, 2, ... until NULL) instead of stored in an array - a project catalog
already resident in flash (`const` tables) can be walked in place, at zero
extra RAM cost for the candidate names themselves. When set, it **replaces**
binding-based completion for that `cli` instance (never merged - bindings and
an external source are two different candidate universes, and merging them
produces ambiguous listings neither the operator nor the code asked for) and
completes the **current token** - the substring of the command buffer after
the last space, or the whole buffer if there is none - instead of the whole
line. That is what lets one callback complete both an operation name (the
first token) and a later argument key (a later token) with the same
mechanism. A companion getter, `embeddedCliGetCmdBuffer(cli)`, exposes the
current buffer read-only so the external source can see what has been typed
and decide what it is completing.

Two smaller, deliberate behavior differences from the binding path, both
scoped to the external-source branch only (the binding path's behavior is
untouched, verified byte-for-byte by the unchanged `getAutocompletedCommand`
function body):
- An **empty current token** is allowed to match (Tab right after an
  operation name and its trailing space lists that operation's argument
  keys - discovery, not a no-op). The binding path's `prefixLen == 0` early
  return is intentionally not touched.
- A completed candidate **ending in `=`** (an argument key) does not get the
  unconditional trailing space every other single-candidate completion adds -
  `key=value` has no space around `=` (the Console protocol's own syntax), so
  the operator would otherwise have to backspace it before typing the value.

**File**: `include/embedded_cli.h`, `src/embedded_cli.c`
**Lines**:
- `include/embedded_cli.h`: new `getCompletionCandidate` field on `struct
  EmbeddedCli`; new `embeddedCliGetCmdBuffer()` declaration.
- `src/embedded_cli.c`: `AutocompletedCommand` gains a `tokenStart` field
  (0 for the binding path, unchanged); new `getExternalAutocompletedCommand()`
  static function; `onAutocompleteRequest()` branches on whether
  `cli->getCompletionCandidate` is set; new `embeddedCliGetCmdBuffer()`
  definition next to `embeddedCliResetInput()` (Patch 2).

**Defensive bound**: the completed-token write path added by this patch
checks `tokenStart + autocompletedLen + 2 <= cmdMaxSize` before writing,
matching Patch 2's Safe Overflow philosophy - a candidate token that would
not fit is refused rather than corrupting the fixed command buffer.

**RAM ownership**: this patch adds no static RAM to `embedded_cli.c` itself
(`.bss` unchanged, see the Object Size Analysis table above). The 256 B
candidate pool this project's completion source uses lives in
`include/console_completion.h`, in the *caller's* translation unit - the
library itself remains a pure zero-copy enumerator over whatever the caller's
callback returns.

**Upstream candidacy**: Yes - a project-supplied candidate source is a
natural extension of the existing bindings model for any embedded-cli user
with a candidate set too large to bind (a generated catalog, a filesystem
listing), and the change is additive (a new optional field, defaulting to
NULL/unused, with zero behavior change to the existing bindings path).

**Test**: `test/test_native/test_cli_catalog_completion/test_cli_catalog_completion.cpp` -
verifies unique-match completion (with and without the trailing-space
suppression for `=`-ending candidates), ambiguous-prefix extend-then-list,
current-token-only splicing (an earlier token is preserved), that an
external source replaces rather than merges with bindings, the fixed-buffer
overflow bound, and - the regression #238's coordinator brief asked for
explicitly - that Enter still never autocompletes (Patch 1) with an external
completion source wired in, including immediately after a Tab press that
only *extended* the line without fully completing it.
`test/test_native/test_console_completion/test_console_completion.cpp`
(alongside `include/console_completion.h`, not in `lib/`) covers this
project's own candidate source against the real catalog: operation-name vs.
argument-key mode selection, resolving the operation from the first token
regardless of later tokens, no-params and unknown-operation degradation, and
that availability fields never gate completion (##238's "known-but-
unavailable operations remain completable" acceptance criterion).

### Patch 6: History Filter Callback

**Problem**: `parseCommand` (line 902, before this patch) pushes the submitted
line into the Up/Down history ring unconditionally, and it does so **before
dispatch**:

```c
// push command to history before buffer is modified
historyPut(&impl->history, impl->cmdBuffer);
```

The project's Console refuses a password argument with
`invalid reason=secret-not-settable` (##227, docs/console-protocol.md s.4.1) -
the value never reaches an Apply Core, a log, a record or the recent-log ring.
But by the time the dispatcher has refused it, the line the operator typed is
already in the history ring, recallable with Up-arrow for the rest of the
session. The refusal is therefore only as good as the editor underneath it,
which is the one place the project could not reach without a patch here.

**Solution**: Add an optional predicate, `EmbeddedCli::shouldStoreHistory(cli,
line)`, consulted in `parseCommand()` immediately before `historyPut()`. When
it is NULL - the default, and the state of every other consumer - behaviour is
byte-for-byte upstream's. When it returns false, the history write is skipped
and **nothing else changes**: the line still dispatches, because refusing to
remember a command is not refusing to run it, and the project's own use is
precisely a line the dispatcher must reach in order to answer that it is
refused.

Deciding *before* the write is the substance of the patch, not a detail of it.
The alternative shape - store the line, then remove it once the dispatcher
reports a refusal - would put the secret in a buffer that outlives the
decision, and `historyRemove()` does not scrub: it shifts the ring, and when
the entry being removed is the oldest one it returns without moving anything
at all, leaving those bytes verbatim in the history buffer behind
`itemsCount`. There is no removal path in this patch on purpose.

The callback receives the line as typed, before `parseCommand` splits the
buffer into name and args (the split overwrites separators with NUL), so a
predicate can see the whole `operation key=value ...` line. It must not retain
the pointer: the buffer is reused on the next line.

**File**: `include/embedded_cli.h`, `src/embedded_cli.c`
**Lines**:
- `include/embedded_cli.h`: new `shouldStoreHistory` field on `struct
  EmbeddedCli`, next to `getCompletionCandidate` (Patch 5).
- `src/embedded_cli.c`: the `historyPut()` call in `parseCommand()` becomes
  conditional on the callback.

**RAM ownership**: no static RAM is added to `embedded_cli.c` (`.bss`
unchanged, see the Object Size Analysis table). The one pointer lives in
`struct EmbeddedCli`, which the caller allocates, so the cost is +4 B of the
caller's `cliBuffer` - accounted for by `embeddedCliRequiredSize()`, which the
caller already checks against its static buffer at init.

**Upstream candidacy**: Yes - additive, defaults to unchanged behaviour, and
"do not remember this line" is a general need (a password prompt, a line
carrying a token) rather than a project-specific one. Same shape as Patch 5: a
new optional field with no effect until it is set.

**Test**: `test/test_native/test_cli_history_filter/test_cli_history_filter.cpp` -
verifies that an unset filter stores every line (upstream behaviour), that a
refused line is unreachable by Up-arrow while the last accepted line still is,
that a refused line still executes, that the predicate receives the whole line
as typed, and that the ring keeps working after a refusal. History is read the
way the operator reads it - press Up, look at the command buffer - because
that is the exposure being closed; the ring internals are static to
`embedded_cli.c`. The project's own rule for WHICH lines are refused lives in
`include/console_write_exclusion.h` and is covered by
`test/test_native/test_console_write_exclusion/`.

## Integration Notes

### Static Buffer Allocation

The caller must provide a static buffer sized by `embeddedCliRequiredSize()`.
As shipped (`src/tasks/console_task.cpp`), `maxBindingCount` stays at the
library default (8) and `embeddedCliAddBinding()` is never called: Tab
completion is Patch 5's catalog completion callback, not bindings (see
"Board Differences" below) - the 175+-entry catalog costs 20 B per name as a
`CliCommandBinding`, which the binding-count sizing this section originally
illustrated was written for and which the project deliberately does not pay:

```c
#define CLI_BUFFER_SIZE embeddedCliRequiredSize(&config)

static CLI_UINT cliBuffer[BYTES_TO_CLI_UINTS(CLI_BUFFER_SIZE)];

config.cliBuffer = cliBuffer;  // No malloc needed
```

Verify `BYTES_TO_CLI_UINTS` is defined and that the static allocation compiles.

### Dispatcher

The `onCommand` callback receives `CliCommand{name, args}` where `args` is a tokenized, NUL-separated list if `binding->tokenizeArgs` is true.

For Console commands with key=value arguments:

```c
// Bind with tokenizeArgs = false; tokenize in dispatcher to allow body/secret bypass
binding->tokenizeArgs = false;
```

The dispatcher calls `embeddedCliTokenizeArgs` after routing, so the Console can intercept special modes (body mode, secret-input mode).

### Output

Call `embeddedCliPrint` under the log mutex for asynchronous output (logs, events) that arrives while the operator is typing:

```c
portMUX_TYPE *logMutex = /* the existing log output lock */;
taskENTER_CRITICAL(logMutex);
embeddedCliPrint(cli, logLine);
taskEXIT_CRITICAL(logMutex);
```

`embeddedCliPrint` handles the redraw atomically; the typed line remains visible after the output.

### Board Differences

**Superseded (kept for history): a per-board binding subset.** The original
plan here was "bind all 192 catalog names on FireBeetle 2, bind ~55 aliases
on artoo-esp32 for completion RAM budget, unbound aliases still execute but
don't Tab-complete." #238's coordinator brief rejected this explicitly: any
board-specific completion subset is a reduced catalog on one board, which the
epic's own acceptance matrix ("No board receives a reduced command or
completion catalog", ##206) forbids. Patch 5's catalog completion callback
(above) is the shipped mechanism instead: **both boards bind zero commands**
and complete identically from the same in-image catalog via
`include/console_completion.h`'s `consoleCompletionCandidate()`, at a flat
~256 B RAM cost (the argument-key candidate pool) regardless of catalog size
or board.

**FireBeetle 2 (P4, native USB CDC)**:
- USB CDC may have different backpressure; measure `Serial.write` latency.

**Artoo-esp32 (UART0 via debug bridge)**:
- No RAM trade-off to make: the catalog completion callback's cost does not
  scale with catalog size, so there is nothing to trim for this board's
  tighter static RAM headroom (#233).

### Test Environment

Host tests run under `env:native` in PlatformIO. The library compiles for native x86-64 and tests are unit-level (input/output buffers, tokenization, overflow).

## Maintenance

- Upstream repo: https://github.com/funbiscuit/embedded-cli
- Last vendored commit: `8e796cbf2263f055d43b6ad99b9ddc8feece4cfd`
- Last upstream activity: 2026-03-11 (PR #57 merge, CI maintenance 2026-02-11)
- Issue resolution: Authors respond within weeks (issues #51, #53 closed 2024-07, 2025-08)

To update the vendored source:

1. Fetch the upstream: `git fetch upstream` (requires remote setup)
2. Verify the commit: `git log upstream/master --oneline | head`
3. Copy new source: `cp upstream/lib/src/embedded_cli.c lib/embedded-cli/src/`
4. Re-apply patches manually or with `git apply lib/embedded-cli/*.patch`
5. Re-measure and update this document
6. Commit: `chore(lib): update embedded-cli to <new-sha>`

## License Compliance

The MIT license in `LICENSE` permits use, modification, and distribution provided the license and copyright notice are retained. This file and the patches retain upstream attribution.
