"""Every FreeRTOS task `src/` creates, by the name it registers.

Shared by the two guards that both need the same inventory and must not disagree
about it:

- test_profiler_task_list.py, which requires /api/profiler to list every task
  (a task the profiler never lists reads exactly like a task whose Component
  Toggle is off - that is how SeqDisp went missing for an unknown length of
  time, #250);
- test_task_stack_recipes.py, which requires every task to have a Measured Chain
  and a compile-enforced floor (#271, ADR 0038).

Both scan the whole tree rather than `src/main.cpp`. The criterion is the task,
not the file it happens to be created in: WebEvents and the ArduinoOTA task live
in src/web/web_server.cpp and HostedRecovery in
src/web/web_network_manager_hosted.cpp, and a main.cpp-only scan is blind to all
three - which is the blind spot the profiler guard shipped with.

The registered name is the second argument to xTaskCreate*(), and the first can
be a multi-line lambda whose body contains commas, so this walks the call's
parentheses instead of pattern-matching the argument list.
"""

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SRC_DIR = REPO_ROOT / "src"

# src/native_test_stubs.cpp declares a host stand-in for
# xTaskCreatePinnedToCore() so native tests link; it creates nothing. Excluded
# by path rather than by a cleverer regex, so a real call site can never be
# excluded by accident.
EXCLUDED_FILES = {SRC_DIR / "native_test_stubs.cpp"}

# Tasks the profiler legitimately watches that src/ does not create itself.
# loopTask is spawned by arduino-esp32's core, which calls our setup()/loop();
# it is sized through ARDUINO_LOOP_STACK_SIZE in platformio.ini rather than with
# xTaskCreatePinnedToCore, so it will never appear in the scan below.
EXTERNALLY_CREATED = {"loopTask"}


def task_create_calls(text: str):
    """The argument text of every xTaskCreate...( ... ) call in one file."""
    needle = "xTaskCreate"
    index = text.find(needle)
    while index != -1:
        open_paren = text.find("(", index)
        if open_paren != -1:
            depth = 0
            for pos in range(open_paren, len(text)):
                if text[pos] == "(":
                    depth += 1
                elif text[pos] == ")":
                    depth -= 1
                    if depth == 0:
                        yield text[open_paren + 1 : pos]
                        break
        index = text.find(needle, index + len(needle))


def second_argument_string_literal(args: str):
    """The string literal that is the call's second argument, or None.

    Splits on commas at nesting depth 0, so a lambda body, a template argument
    list or a nested call in the first argument does not end the argument early.
    """
    parts, depth, current = [], 0, []
    for char in args:
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        if char == "," and depth == 0:
            parts.append("".join(current))
            current = []
            continue
        current.append(char)
    parts.append("".join(current))
    if len(parts) < 2:
        return None
    second = parts[1].strip()
    if len(second) >= 2 and second.startswith('"') and second.endswith('"'):
        return second[1:-1]
    return None


def created_task_sites() -> dict:
    """{registered task name: repo-relative file} for every xTaskCreate* in src/."""
    found: dict[str, str] = {}
    for path in sorted(SRC_DIR.rglob("*.cpp")):
        if path in EXCLUDED_FILES:
            continue
        for call in task_create_calls(path.read_text(encoding="utf-8")):
            name = second_argument_string_literal(call)
            if name is not None:
                found[name] = str(path.relative_to(REPO_ROOT))
    return found


def created_task_names() -> set:
    return set(created_task_sites())
