"""The upload guard asks on an upload and on nothing that only names one (#459).

Runs the hook as Claude Code does: JSON on stdin, an ask decision on stdout or
nothing. The four sibling upload hooks read the same helper.
"""

import json
import subprocess
import sys
import unittest
from pathlib import Path

HOOK = Path(__file__).resolve().parents[2] / ".claude" / "hooks" / "pre_upload_guard.py"

ASKS = [
    "make flash",
    "timeout 600 make flash",
    "make -C . flash",
    "make -C sub flash",
    "cd x && make ota HOST=a",
    "echo hi; make uploadfs",
    "pio run -e artoo_esp32 -t upload",
    'herdr pane run w8:p1 "tools/gate_in_pane.sh /tmp/x.log -- make flash"',
    'herdr pane run w8:p1 "tools/gate_in_pane.sh /tmp/x.log -- pio run -e artoo_esp32 -t upload"',
    'bash -c "make ota"',
]

DOES_NOT_ASK = [
    'git commit -m "docs: make flash needs a port"',
    "cat >> Makefile <<'EOF'\n# run make flash first\nEOF",
    'grep -- "-t upload" tools/pio_lock.py',
    "python3 -c \"print('make flash; pio run -t upload')\"",
    'herdr agent prompt w8:p1 "run make flash later"',
]


def decide(command):
    payload = {"tool_name": "Bash", "tool_input": {"command": command}}
    r = subprocess.run([sys.executable, str(HOOK)], input=json.dumps(payload),
                       capture_output=True, text=True, check=True)
    if not r.stdout.strip():
        return "allow"
    return json.loads(r.stdout)["hookSpecificOutput"]["permissionDecision"]


class UploadGuard(unittest.TestCase):
    def test_an_upload_asks(self):
        for command in ASKS:
            with self.subTest(command=command):
                self.assertEqual(decide(command), "ask")

    def test_text_naming_an_upload_does_not_ask(self):
        for command in DOES_NOT_ASK:
            with self.subTest(command=command):
                self.assertEqual(decide(command), "allow")


if __name__ == "__main__":
    unittest.main()
