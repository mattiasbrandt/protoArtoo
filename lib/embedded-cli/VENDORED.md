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
- Small footprint: ~4.3 KB flash (xtensa), ~1.1-5.2 KB static depending on bindings
- Stack frame <= 80 B

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

Object Size Analysis (Measured at commit 9950006)

Measured with both toolchains under `-Os`:

| Toolchain | State | .text | .rodata | .bss | max stack frame |
|-----------|-------|-------|---------|------|-----------------|
| xtensa | upstream | 3,430 B | 238 B | 20 B | 96 B |
| xtensa | patched | 3,190 B | 118 B | 20 B | 96 B |
| riscv32 | upstream | 4,342 B | 259 B | 20 B | 112 B |
| riscv32 | patched | 3,854 B | 132 B | 20 B | 96 B |

**Deltas (patched - upstream):**
- xtensa: .text -240 B, .rodata -120 B, .bss 0, max frame 0 B
- riscv32: .text -488 B, .rodata -127 B, .bss 0, max frame -16 B

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

**Per-binding and total allocations remain as documented:**
- Per binding: 20 B (`sizeof(CliCommandBinding)`)
- Static BSS without bindings: 20 B
- Stack max frame (re-measured, corrected from prior research): 96 B

**Artoo-esp32 (under ADR 0017 budget with margin):**
- Flash headroom: 26,656 B; library + 55 bindings = 4.3 KB + 2.2 KB = 6.5 KB (24% of headroom)
- Static headroom: 13,688 B; library + 55 bindings = 2.3 KB (17% of headroom)

**FireBeetle 2 (ample margin):**
- Library + 192 bindings = 4.6 KB + 5.2 KB = 9.8 KB flash, 6.3 KB static

## Patches Applied

The vendored source includes four required patches. Each is mechanical, documented, and offered upstream as a PR candidate.

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

## Integration Notes

### Static Buffer Allocation

The caller must provide a static buffer sized by `embeddedCliRequiredSize()`:

```c
#define CLI_BINDING_COUNT 55  // artoo-esp32: 38 RC + verb/status words
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

**FireBeetle 2 (P4, native USB CDC)**:
- Bind all 192 catalog names; no artoo constraints apply.
- USB CDC may have different backpressure; measure `Serial.write` latency.

**Artoo-esp32 (UART0 via debug bridge)**:
- Bind ~55 aliases (38 RC + verb/query prefixes); unbind others for completion RAM budget.
- Unbound aliases still execute (via `onCommand` fallthrough), they just don't Tab-complete or appear in built-in help.

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
