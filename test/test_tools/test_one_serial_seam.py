"""One task owns the serial wire, and one file writes it (#270, ADR 0037).

ADR 0037: *"We decided the serial wire is owned by the Console task. After the
serial adapter binds, no other task writes it."* The behaviour half of that -
a line logged from a non-Console task reaching the wire only through the drain
- is proven natively in `test/test_native/test_console_log_drain/`. This is the
structural half, and it is the one a native test cannot see: that no OTHER
file in `src/` or `include/` touches the transport at all.

WHY BOTH. The native test proves the path that exists behaves. It cannot prove
the absence of a second path, because a new `Serial.print()` in a driver, a
task, or a web handler links and runs and simply writes the wire - no test
fails, nothing is interleaved on a quiet bench, and the property degrades
silently back to the pre-ADR state where "any task may write the wire". Before
this ticket the tree had five such writers outside the sink (two `Serial.print`
banner sites, three `Serial.flush()` sites) and the reason they were invisible
is that a rule written in an ADR is a sentence in a document. This is the
enforcement that sentence implies, in the mould of
`test_queue_send_timeout_zero.py`.

WHY THE WHOLE TREE, NOT A FILE LIST. The rule is about a hardware resource, not
about a directory: `Serial` is a global, so any translation unit can reach it.
A file list would have to be maintained by whoever adds the next file, which is
exactly the person the rule exists to catch.

WHAT IS SCANNED. Calls on the `Serial` object itself - the Console's wire
(UART0 on artoo-esp32, USB CDC on the FireBeetle 2). `Serial1`/`Serial2` and
the `HardwareSerial` instances the audio and dome links own are DIFFERENT
wires with their own owners, and `\\bSerial\\b` does not match them. Reads
(`available()`, `read()`, `operator bool()`) are not writes and are not
scanned: the Console task polls its own input, and nothing about ownership
forbids asking whether a host is attached.

The IDF logger is checked separately below: `ESP_LOGx` never names `Serial`, so
a scan for `Serial.` cannot see it - it reached UART0 through the IDF's own
vprintf. `esp_log_set_vprintf()` is what points it at the Log Ring instead, and
its absence is the way this invariant could be lost without any `Serial.` call
appearing anywhere.
"""

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCAN_DIRS = (REPO_ROOT / "src", REPO_ROOT / "include")
SCAN_SUFFIXES = (".c", ".cpp", ".h", ".hpp", ".inc")

# Members of `Serial` that emit bytes, drain the transport, or configure it.
# Reads are deliberately absent - see the module docstring.
WIRE_MEMBERS = (
    "write",
    "print",
    "println",
    "printf",
    "flush",
    "begin",
    "end",
    "setDebugOutput",
    "setTxTimeoutMs",
    "setRxBufferSize",
    "setTxBufferSize",
)
# \bSerial\b: `Serial1.write` and `audioSerial.write` are other wires with
# other owners, and the word boundary is what keeps them out.
WIRE_CALL_RE = re.compile(
    r"\bSerial\s*\.\s*(" + "|".join(WIRE_MEMBERS) + r")\s*\("
)

# The deliberate exceptions, keyed "<repo-relative path>:<Serial member>" and
# valued (expected occurrence count, why this site may touch the wire).
#
# Keyed by member rather than by line number on purpose: a line number goes
# stale on the next edit above it and turns this test into a chore, while the
# count still catches a SECOND call of the same kind being added to an
# already-allowed file. Every entry carries its reason, so this list can never
# decay into "these were here when I arrived".
ALLOWED_WIRE_WRITERS: dict[str, tuple[int, str]] = {
    "src/console/console_serial_output.cpp:write": (
        1,
        "THE SINK. The one place a Console byte reaches the wire: one "
        "Serial.write() per unit, records and drained log lines alike (ADR "
        "0036's framing, ADR 0037's ownership). Everything else in the "
        "firmware reaches the wire by calling into this file.",
    ),
    "src/main.cpp:begin": (
        1,
        "TRANSPORT BRING-UP. Serial.begin(115200) in setup(), before any task "
        "exists - there is nothing to own the wire yet, and the Console task "
        "cannot bring up the transport it is later handed.",
    ),
    "src/main.cpp:setRxBufferSize": (
        1,
        "TRANSPORT POLICY, same pre-task setup block, and INPUT rather than "
        "output. Sizes the CDC receive queue so a whole over-length command "
        "line - terminator included - is still there when the Console task "
        "reads it; the driver discards input it has no room for and reports "
        "nothing, and the byte it discards first is the CR that triggers the "
        "line's `line-too-long` refusal (#229, include/console_task.h). Must "
        "run BEFORE Serial.begin(), which only sizes the queue if nothing "
        "else has. Configures the transport; writes no bytes.",
    ),
    "src/main.cpp:setTxTimeoutMs": (
        1,
        "TRANSPORT POLICY, same pre-task setup block. Sets the CDC write "
        "timeout to zero so a detached host cannot hold a writer for ~2 s per "
        "call and starve a TWDT-subscribed task (#245 defect 1). Configures "
        "the transport; writes no bytes.",
    ),
    "src/main.cpp:setDebugOutput": (
        1,
        "TRANSPORT POLICY, same pre-task setup block. Detaches the Arduino "
        "core's debug output from this port so the core cannot write it "
        "behind the Console task's back. Configures the transport; writes no "
        "bytes.",
    ),
    "src/tasks/console_task.cpp:flush": (
        1,
        "THE OWNER'S OWN DRAIN, artoo-esp32 only (guarded by the CDC-on-boot "
        "#if). Waits for UART0 to finish sending the ready banner this task "
        "just wrote. It is the wire's owner draining its own write, on the "
        "one transport where flush() actually waits - the site's own comment "
        "carries why it is skipped on the CDC, where flush() DISCARDS the TX "
        "ring instead.",
    ),
}

# Where the IDF logger must be redirected, and the call that does it.
IDF_LOGGER_REDIRECT_FILE = REPO_ROOT / "src" / "main.cpp"
IDF_LOGGER_REDIRECT_RE = re.compile(r"\besp_log_set_vprintf\s*\(")


def strip_comments_and_strings(text: str) -> str:
    """Blank out comments and string/char literals, preserving line structure.

    A comment naming `Serial.write()` (several headers have one) is not a call,
    and a member name inside a string is not one either. Newlines survive so
    reported line numbers stay true to the file.

    Deliberately a copy of test_queue_send_timeout_zero.py's helper rather than
    an import: these two scans are independent rules that happen to need the
    same preprocessing, and a shared helper would make either one's future
    change a change to the other's evidence.
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


def scanned_files():
    files = []
    for root in SCAN_DIRS:
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.suffix in SCAN_SUFFIXES:
                files.append(path)
    return files


def find_wire_calls():
    """Every call on `Serial` that writes, drains or configures the transport.

    Yields (repo-relative path, line number, member name).
    """
    found = []
    for path in scanned_files():
        raw = path.read_text(encoding="utf-8", errors="replace")
        text = strip_comments_and_strings(raw)
        rel = path.relative_to(REPO_ROOT).as_posix()
        for match in WIRE_CALL_RE.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            found.append((rel, line, match.group(1)))
    return found


class OneSerialSeamTest(unittest.TestCase):
    def test_only_the_allowlisted_sites_touch_the_serial_wire(self):
        calls = find_wire_calls()
        seen: dict[str, list[int]] = {}
        for rel, line, member in calls:
            seen.setdefault(f"{rel}:{member}", []).append(line)

        offenders = []
        for key, lines in sorted(seen.items()):
            if key not in ALLOWED_WIRE_WRITERS:
                rel, member = key.rsplit(":", 1)
                offenders.append(
                    f"{rel}:{lines[0]}: Serial.{member}() outside the sink. The "
                    f"Console task owns this wire (ADR 0037): write through "
                    f"src/console/console_serial_output.cpp, or - for a log line - "
                    f"through PA_LOG_*, which the Console task drains. If this site "
                    f"genuinely must touch the transport, add it to "
                    f"ALLOWED_WIRE_WRITERS with the reason."
                )
                continue
            expected, _reason = ALLOWED_WIRE_WRITERS[key]
            if len(lines) != expected:
                offenders.append(
                    f"{key}: found {len(lines)} call(s) at lines "
                    f"{lines}, the allowlist admits {expected}. A second call of "
                    f"the same kind in an allowed file is not covered by the first "
                    f"one's reason - state the new one's, or route it through the "
                    f"sink."
                )

        for key in sorted(ALLOWED_WIRE_WRITERS):
            if key not in seen:
                offenders.append(
                    f"{key}: allowlisted but no longer present. Remove the entry - "
                    f"an allowlist that outlives its sites is how a list stops "
                    f"being read."
                )

        self.assertEqual([], offenders, "\n" + "\n".join(offenders))

    def test_every_allowlist_entry_carries_a_written_reason(self):
        """A path list without reasons is not the deliverable (#270's pin)."""
        for key, (count, reason) in ALLOWED_WIRE_WRITERS.items():
            self.assertGreater(count, 0, f"{key}: an allowlist entry admitting nothing")
            self.assertGreaterEqual(
                len(reason.split()),
                12,
                f"{key}: the reason is too short to be one. Say WHY this site may "
                f"touch the wire when nothing else may.",
            )

    def test_the_scan_actually_finds_the_seam(self):
        """A regex that matches nothing would pass the rule vacuously."""
        calls = find_wire_calls()
        self.assertGreaterEqual(
            len(calls), len(ALLOWED_WIRE_WRITERS), f"only {len(calls)} calls found"
        )
        files = {rel for rel, _, _ in calls}
        self.assertIn(
            "src/console/console_serial_output.cpp",
            files,
            "the sink itself was not scanned - the scan is not working",
        )
        members = {member for _, _, member in calls}
        self.assertIn("write", members, "the one Serial.write() in the tree was not found")

    def test_an_unallowlisted_writer_would_be_reported(self):
        """The rule is shown failing, not assumed to.

        Drives the same regex over a file that writes the wire from a task
        that does not own it, and over the shapes that must NOT be reported:
        a comment, a string, another wire's HardwareSerial instance, and a
        read.
        """
        source = (
            "void driveTask(void *p) {\n"
            "    // Serial.print(\"in a comment\") is not a call\n"
            '    const char *s = "Serial.print(quoted)";\n'
            "    Serial1.write(frame, len);\n"
            "    audioSerial.print(cmd);\n"
            "    if (Serial.available()) { (void)Serial.read(); }\n"
            "    Serial.print(\"I am not the Console task\");\n"
            "    Serial.flush();\n"
            "}\n"
        )
        text = strip_comments_and_strings(source)
        members = [m.group(1) for m in WIRE_CALL_RE.finditer(text)]
        self.assertEqual(
            ["print", "flush"],
            members,
            "the scan did not read exactly the two real Serial writes - the "
            "commented and quoted ones must not be seen, another wire's "
            "instance must not be matched, and reads must not be reported",
        )

    def test_the_idf_logger_is_pointed_at_the_log_ring(self):
        """ESP_LOGx must not reach the wire behind the Console task's back.

        `src/drivers/sbus_decoder.cpp` and `src/drivers/ledc_pwm.cpp` log
        through the IDF logger, which writes stdout - UART0 - with no project
        lock and no knowledge of who owns the wire (ADR 0037's current-state
        audit). Redirecting it with esp_log_set_vprintf() is what makes those
        lines ordinary Log Ring entries. Nothing in a `Serial.` scan can see
        this, which is exactly why it is asserted here.
        """
        text = strip_comments_and_strings(
            IDF_LOGGER_REDIRECT_FILE.read_text(encoding="utf-8")
        )
        self.assertRegex(
            text,
            IDF_LOGGER_REDIRECT_RE,
            "src/main.cpp does not install an esp_log_set_vprintf() hook, so "
            "ESP_LOGx writes UART0 directly and the Console task is not the "
            "only writer of the wire",
        )


if __name__ == "__main__":
    unittest.main()
