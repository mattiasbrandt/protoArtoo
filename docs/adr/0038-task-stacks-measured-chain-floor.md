# Task stacks: every task has a measured chain and a compile-enforced floor; the sizing rule is applied per chip; the gate re-walks the chains

Status: proposed. Lands on the surface that owns the sizing mechanism -
`include/config.h`, `src/main.cpp` and the slice gate are #182's (#245, #248)
and the gate is coordinator-fenced - so as a sub-issue there or on `main` after
#232. #206 carries only the Console task's own constant (#226).

#245 and #248 established how a task stack is sized in this project: walk the
worst-case static chain from the linked image with `tools/stack_usage_report.py`,
add 25%, round up to the next 512 bytes, and record why that margin. #226 then
found the one stack that had been sized from a high-water mark instead, 4 KB
short of its chain on both chips, and gave it the first compile-enforced floor:
`CONSOLE_TASK_MEASURED_CHAIN_BYTES` plus a `static_assert`. At `0f1412fc`:

- Eight tasks have chains recorded in `config.h` comments and constants that
  nothing checks against them. ServoTask (Core 1), the ArduinoOTA task and
  HostedRecovery are unmeasured literals; SeqDisp is a literal with its chain in
  a comment.
- The rule was applied on the ESP32-P4 and deliberately declined on the
  artoo-esp32 for DomeLinkTask and WebEvents, where it would cost 11,264 B of a
  tight heap for a margin the Xtensa walk cannot confirm (objdump emits ~44% of
  that image's function bodies as data, so every artoo chain is a floor).
- A floor `stack >= chain` passes for every task today at no cost. A rule assert
  would fail on artoo for the two declined tasks.
- A recorded chain is a hand-written number. The Console's assert stops the
  constant being trimmed; nothing notices the chain growing past it.
- The profiler's task list is guarded by a test that scans `src/main.cpp` only;
  WebEvents and HostedRecovery are created elsewhere and are not listed.
- The sizing rule itself lived in a header comment, with no decision record.

**We decided:**

- **Every project-created task has a Measured Chain per chip and a
  compile-enforced floor.** Twelve tasks: the ten in `src/main.cpp`, WebEvents,
  the ArduinoOTA task and HostedRecovery. `loopTask` is sized by
  `ARDUINO_LOOP_STACK_SIZE` in `platformio.ini` and stays outside. The chain is
  a `*_MEASURED_CHAIN_BYTES` constant on both arms with
  `static_assert(*_STACK_BYTES >= *_MEASURED_CHAIN_BYTES)`; the comment-recorded
  chains become constants, the three unmeasured tasks are walked once.
- **The rule is applied per chip where affordable, and declining it is
  recorded beside the constant.** #248's artoo exceptions stand on #248's
  reason. The native test re-derives the constant from the chain by the rule
  wherever the rule is applied, so a constant cannot drift from its derivation;
  where it is declined the test pins the floor only.
- **The gate re-walks every chain.** One table records each task's recipe -
  environment, root symbols, the frames stitched across indirect calls, the
  profiler-image substitution where the product image's body is emitted as data.
  The slice gate's artoo build carries `-fstack-usage` (it changes no code) and a
  check row walks every recipe and fails when a chain exceeds its constant. The
  Xtensa walk is a floor, so the row can miss growth and cannot report false
  growth. The P4 is walked at bench time until the gate builds that target.
- The profiler task-list guard scans every `xTaskCreate` site in the tree, and
  the two missing names join the list.

## Considered options

- **Rule everywhere, pay the artoo heap.** Rejected: ~11 KB against ~36 KB free
  after #226, for margins the Xtensa measurement cannot confirm - #248's reason,
  unchanged.
- **Console only, as merged.** Rejected: the next stack trimmed below its chain
  is found the way the Console was, with a reboot on both boards. ServoTask is a
  Core 1 task with no measurement at all.
- **A dated number with a written re-measure trigger.** Rejected: enforcement
  by review is what let 5120 stand with a comment promising margin.
- **Runtime high-water marks from the soak as the floor.** Rejected as the
  floor, kept as a second instrument: a high-water mark reports the paths that
  ran, which is exactly how the Console's 5120 was justified.
- **Only the ten tasks in `src/main.cpp`.** Rejected: the criterion is the
  task, not the file it happens to be created in; the profiler guard had the
  same blind spot.

## Consequences

- Twelve recipes written once; after that the walk is a gate row and a
  re-measure is a re-run. A slice that grows a chain past its constant fails
  the gate and must re-derive the constant, on both chips, with the reason.
- The chain constants are the second thing `include/config.h` carries per
  task; the block's own claim, "one block for every per-chip task stack",
  becomes true.
- The Console's constants are re-derived after the decision-1 slice removes
  the copies from its chain (ADR 0011, 2026-09-04).
- `uxTaskGetStackHighWaterMark()` returns bytes on ESP-IDF (`portSTACK_TYPE`
  is `uint8_t`); `api_profiler.cpp`'s "returns words" comment multiplies by one
  and is corrected in passing.
- Cross-epic: #182 is told before the surface is touched, per the standing
  contract on `platformio.ini`, `config.h` and the gate.
