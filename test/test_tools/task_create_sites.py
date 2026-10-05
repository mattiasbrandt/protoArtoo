"""Every FreeRTOS task `src/` creates, by the name it registers.

Shared by the two guards that both need the same inventory and must not disagree
about it:

- test_profiler_task_list.py, which requires /api/profiler to list every task
  (a task the profiler never lists reads exactly like a task whose Component
  Toggle is off - that is how SeqDisp went missing for an unknown length of
  time, #250);
- test_task_stack_recipes.py, which requires every task to have a Measured Chain
  and a compile-enforced floor (#271, ADR 0040).

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


def task_create_call_spans(text: str):
    """(offset, argument text) of every xTaskCreate...( ... ) call in one file."""
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
                        yield index, text[open_paren + 1 : pos]
                        break
        index = text.find(needle, index + len(needle))


def task_create_calls(text: str):
    """The argument text of every xTaskCreate...( ... ) call in one file."""
    for _, args in task_create_call_spans(text):
        yield args


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


# -- where each call sits ------------------------------------------------------
#
# The boot heap figure (tools/slice_verify.py, #468) counts the tasks every boot
# creates, so each recipe arm says whether its task is one of them. These read
# the call site that claim rests on: the function the call is in, and the
# condition of the innermost `if` around it. They do not follow callers; whether
# that function runs on every boot is read by hand and written in the recipe.

CONTROL_WORDS = {"if", "for", "while", "switch", "do", "else"}


def blank_comments_and_strings(text: str) -> str:
    """The text with every comment and string or character literal blanked.

    Same length, newlines kept, so an offset into the original is an offset
    into this; a brace in a comment or a string can no longer open a block.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "//":
            end = text.find("\n", i)
            end = n if end == -1 else end
        elif two == "/*":
            end = text.find("*/", i + 2)
            end = n if end == -1 else end + 2
        elif text[i] in "\"'":
            quote, end = text[i], i + 1
            while end < n and text[end] != quote:
                end += 2 if text[end] == "\\" else 1
            end += 1
        else:
            i += 1
            continue
        for k in range(i, min(end, n)):
            if out[k] != "\n":
                out[k] = " "
        i = end
    return "".join(out)


def _block_header(code: str, open_brace: int) -> tuple[str, str]:
    """(keyword or name, parenthesised text) of the header before a `{`.

    For `if (x) {` that is ("if", "x"); for `void f(int a) {` it is ("f",
    "int a"); for `else {` it is ("else", "").
    """
    end = open_brace - 1
    while end >= 0 and code[end].isspace():
        end -= 1
    if end < 0:
        return "", ""
    if code[end] != ")":
        start = end
        while start >= 0 and (code[start].isalnum() or code[start] == "_"):
            start -= 1
        return code[start + 1:end + 1], ""
    depth = 0
    for pos in range(end, -1, -1):
        if code[pos] == ")":
            depth += 1
        elif code[pos] == "(":
            depth -= 1
            if depth == 0:
                inner = " ".join(code[pos + 1:end].split())
                word_end = pos - 1
                while word_end >= 0 and code[word_end].isspace():
                    word_end -= 1
                word_start = word_end
                while word_start >= 0 and (code[word_start].isalnum() or code[word_start] == "_"):
                    word_start -= 1
                return code[word_start + 1:word_end + 1], inner
    return "", ""


def call_context(code: str, offset: int) -> tuple[str | None, str | None]:
    """(enclosing function, innermost enclosing `if` condition or None).

    `code` is blank_comments_and_strings() of the file. A guard is reported only
    when the call's own block is an `if`; a loop or an `else` between the call
    and its function is reported by keyword, so it is never mistaken for none.
    """
    guard: str | None = None
    first = True
    depth, pos = 0, offset
    while pos > 0:
        pos -= 1
        if code[pos] == "}":
            depth += 1
        elif code[pos] == "{":
            if depth:
                depth -= 1
                continue
            word, inner = _block_header(code, pos)
            if word in CONTROL_WORDS:
                if first:
                    guard = inner if word == "if" else word
                first = False
                continue
            return word or None, guard
    return None, guard


def created_task_contexts() -> dict:
    """{registered task name: {"function": ..., "guard": ...}} for every task."""
    found: dict[str, dict] = {}
    for path in sorted(SRC_DIR.rglob("*.cpp")):
        if path in EXCLUDED_FILES:
            continue
        text = path.read_text(encoding="utf-8")
        code = blank_comments_and_strings(text)
        for offset, call in task_create_call_spans(text):
            name = second_argument_string_literal(call)
            if name is None or code[offset:offset + 11] != "xTaskCreate":
                continue
            function, guard = call_context(code, offset)
            found[name] = {"function": function, "guard": guard}
    return found


def is_once_guard(guard: str | None, path: Path) -> bool:
    """True when `guard` is `!flag` and the file sets `flag = true;`.

    Such a guard only stops a second creation; it does not make the first one
    conditional.
    """
    if not guard or not guard.startswith("!"):
        return False
    flag = guard[1:].strip()
    return flag.isidentifier() and f"{flag} = true;" in path.read_text(encoding="utf-8")
