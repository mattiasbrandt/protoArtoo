"""Every queue send in the firmware waits zero ticks (#262, #206).

AGENTS.md "Architecture Guardrails": *"Queue sends in real-time paths should be
non-blocking (timeout 0)"*. #206's acceptance matrix states the consequence the
Controller Console depends on: *"Queue sends from real-time paths remain timeout
zero"*, so a Console command that fills `domeCmdQueue`, `servoCmdQueue` or
`audioCmdQueue` can never make a Core 1 task wait on a consumer. Core 1 carries
drive zero-frame continuity at 50 Hz; a send that blocks there is a missed frame,
not a slow response.

Until now that rule was a sentence in a document and a habit at each call site.
Every call site does obey it -- which is exactly the state a rule is in just
before someone adds the one that does not, and a blocking send is invisible in
review (`xQueueSend(q, &cmd, portMAX_DELAY)` reads like ordinary care) and
invisible at runtime until the queue is full, which on a bench it never is.
This is the enforcement that sentence always implied.

WHY THE WHOLE TREE, NOT JUST THE CORE 1 FILES. The criterion is about Core 1
paths, but a Core 1 task's send may live several files down its call chain --
`rcInputTask` reaches `xQueueSend` through `src/rc_dispatcher_helpers.cpp`, and
`domeLinkTask` through `src/drivers/dome_rx_parser.cpp` -- so a file list would
have to track the call graph to stay honest. Requiring zero everywhere is a
superset that needs no graph, holds today with no exceptions, and costs the
Core 0 paths nothing: they all pass zero already, because a web handler that
blocks on a full queue is its own defect (it holds an HTTP worker). A future
send that genuinely wants to wait adds itself to ALLOWED_BLOCKING_SENDS with a
written reason -- a conscious decision, the same shape as the build-size budget
rule in AGENTS.md, never a silent exemption.

`xQueueReceive` is not checked: a consumer task blocking on its own empty queue
is how a task idles, and several deliberately do (`audio_task.cpp` waits 500 ms,
`sequence_dispatcher.cpp` computes its wait). `xQueueSendFromISR` and
`xQueueOverwrite` take no timeout at all.
"""

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCAN_DIRS = (REPO_ROOT / "src", REPO_ROOT / "include")
SCAN_SUFFIXES = (".c", ".cpp", ".h", ".hpp", ".inc")

# The FreeRTOS queue-send calls whose third argument is xTicksToWait.
# xQueueSendFromISR/xQueueOverwrite/xQueueOverwriteFromISR have no timeout
# parameter and are deliberately absent.
TIMED_SEND_NAMES = ("xQueueSend", "xQueueSendToBack", "xQueueSendToFront")
SEND_CALL_RE = re.compile(r"\b(" + "|".join(TIMED_SEND_NAMES) + r")\s*\(")

# Deliberate exceptions, each keyed "<repo-relative path>:<line>" and valued
# with the reason it may block. Empty on purpose: every send in the tree waits
# zero ticks today. Adding an entry is a decision to let some caller wait on a
# consumer, so it carries its reasoning here where the rule is.
ALLOWED_BLOCKING_SENDS: dict[str, str] = {}

MAIN_CPP = REPO_ROOT / "src" / "main.cpp"
# xTaskCreatePinnedToCore(fn, "Name", stack, param, prio, handle, core) - the
# first argument names the task function and the last pins the core.
TASK_CREATE_RE = re.compile(
    r"xTaskCreatePinnedToCore\s*\(\s*([A-Za-z_]\w*)\s*,"  # entry function
    r"(?:[^;()]|\([^()]*\))*?"                            # the middle arguments
    r",\s*(\d+)\s*\)\s*;",                                # core, closing paren
    re.MULTILINE,
)


def strip_comments_and_strings(text: str) -> str:
    """Blank out comments and string/char literals, preserving line structure.

    A comment mentioning xQueueSend (include/audio_task.h has one) is not a
    call, and a timeout inside a string is not an argument. Newlines survive so
    reported line numbers stay true to the file.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                i += 1
        elif ch == "/" and nxt == "*":
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] == "\n":
                    out.append("\n")
                i += 1
            i += 2
        elif ch in ('"', "'"):
            quote = ch
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    i += 1
                if i < n and text[i] == "\n":
                    out.append("\n")
                i += 1
            i += 1
            out.append('""' if quote == '"' else "''")
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def split_call_arguments(text: str, open_paren_index: int):
    """Return the argument strings of the call whose '(' is at the given index.

    Paren- and bracket-aware, so a nested call or an indexed expression in an
    argument does not split it. Returns None if the call is unterminated.
    """
    depth = 0
    args = []
    current = []
    i = open_paren_index
    n = len(text)
    while i < n:
        ch = text[i]
        if ch in "([":
            depth += 1
            if depth == 1 and ch == "(":
                i += 1
                continue
        elif ch in ")]":
            depth -= 1
            if depth == 0:
                args.append("".join(current).strip())
                return args
        elif ch == "," and depth == 1:
            args.append("".join(current).strip())
            current = []
            i += 1
            continue
        current.append(ch)
        i += 1
    return None


def scanned_files():
    files = []
    for root in SCAN_DIRS:
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.suffix in SCAN_SUFFIXES:
                files.append(path)
    return files


def find_timed_sends():
    """Every timeout-bearing queue send in the scanned tree.

    Yields (repo-relative path, line number, timeout argument text).
    """
    found = []
    for path in scanned_files():
        raw = path.read_text(encoding="utf-8", errors="replace")
        text = strip_comments_and_strings(raw)
        for match in SEND_CALL_RE.finditer(text):
            open_paren = match.end() - 1
            args = split_call_arguments(text, open_paren)
            line = text.count("\n", 0, match.start()) + 1
            rel = path.relative_to(REPO_ROOT).as_posix()
            timeout = args[2] if args is not None and len(args) >= 3 else None
            found.append((rel, line, timeout))
    return found


class QueueSendTimeoutZeroTest(unittest.TestCase):
    def test_every_queue_send_waits_zero_ticks(self):
        sends = find_timed_sends()
        offenders = []
        for rel, line, timeout in sends:
            if timeout is None:
                offenders.append(
                    f"{rel}:{line}: could not read the xTicksToWait argument "
                    f"(unterminated call, or fewer than three arguments)"
                )
                continue
            if timeout == "0":
                continue
            key = f"{rel}:{line}"
            if key in ALLOWED_BLOCKING_SENDS:
                continue
            offenders.append(
                f"{rel}:{line}: waits `{timeout}`, not 0. A queue send that can "
                f"block makes its caller wait on a consumer; on Core 1 that is a "
                f"missed 50 Hz drive frame. Pass 0 and handle the false return, "
                f"or add this site to ALLOWED_BLOCKING_SENDS with the reason."
            )
        self.assertEqual([], offenders, "\n" + "\n".join(offenders))

    def test_the_scan_actually_finds_the_known_sends(self):
        """A regex that matches nothing would pass the rule vacuously."""
        sends = find_timed_sends()
        self.assertGreaterEqual(
            len(sends), 15, f"only {len(sends)} queue sends found - the scan is not working"
        )
        files_with_sends = {rel for rel, _, _ in sends}
        # Two Core 1 call chains and one Core 0 one, so a scan that quietly
        # stopped covering src/, include/ or a subdirectory of either is caught.
        for expected in (
            "src/tasks/rc_input.cpp",
            "src/rc_dispatcher_helpers.cpp",
            "include/console_direct_action_dome.h",
        ):
            self.assertIn(expected, files_with_sends, f"{expected} was not scanned")

    def test_a_blocking_send_would_be_reported(self):
        """The rule is shown failing, not assumed to.

        A guard nobody has seen fail is a claim (the same reasoning
        test_board_uart_allocation.py applies to config.h's static_asserts).
        This drives the same argument parser over a call that waits forever.
        """
        source = (
            "void f(void) {\n"
            "    // xQueueSend(q, &cmd, portMAX_DELAY) in a comment is not a call\n"
            '    const char *s = "xQueueSend(q, &cmd, portMAX_DELAY)";\n'
            "    xQueueSend(okQueue, &cmd, 0);\n"
            "    xQueueSend(badQueue, &cmd, portMAX_DELAY);\n"
            "    xQueueSend(alsoBad, &frames[i], pdMS_TO_TICKS(10));\n"
            "}\n"
        )
        text = strip_comments_and_strings(source)
        timeouts = []
        for match in SEND_CALL_RE.finditer(text):
            args = split_call_arguments(text, match.end() - 1)
            self.assertIsNotNone(args)
            timeouts.append(args[2])

        self.assertEqual(
            ["0", "portMAX_DELAY", "pdMS_TO_TICKS(10)"],
            timeouts,
            "the parser did not read exactly the three real calls' timeouts - "
            "the commented and quoted ones must not be seen, and a nested call "
            "in the timeout argument must not split it",
        )

    def test_every_core_1_task_file_is_inside_the_scan(self):
        """The superset is only honest while it actually contains Core 1.

        Reads which tasks main.cpp pins to core 1, resolves each entry function
        to the file that defines it, and requires that file to be one this test
        scans. A Core 1 task added in a directory the scan does not reach would
        otherwise pass silently.
        """
        main_text = strip_comments_and_strings(MAIN_CPP.read_text(encoding="utf-8"))
        core1_tasks = [
            fn for fn, core in TASK_CREATE_RE.findall(main_text) if core == "1"
        ]
        self.assertGreaterEqual(
            len(core1_tasks), 4, f"only found {core1_tasks} pinned to core 1 - parse failed"
        )

        scanned = {p.relative_to(REPO_ROOT).as_posix() for p in scanned_files()}
        for fn in core1_tasks:
            definition_re = re.compile(
                r"^(?:static\s+)?void\s+" + re.escape(fn) + r"\s*\(", re.MULTILINE
            )
            defining = [
                p.relative_to(REPO_ROOT).as_posix()
                for p in scanned_files()
                if p.suffix in (".c", ".cpp")
                and definition_re.search(p.read_text(encoding="utf-8", errors="replace"))
            ]
            self.assertEqual(
                1, len(defining), f"expected exactly one definition of {fn}, found {defining}"
            )
            self.assertIn(defining[0], scanned, f"{fn}'s file is outside the scan")


if __name__ == "__main__":
    unittest.main()
