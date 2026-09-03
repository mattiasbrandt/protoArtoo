#!/usr/bin/env python3
"""The Makefile's Console Client targets must resolve the port, never assume one.

tools/console_client.py has three modes (docs/console-client.md). Before #230 the
Makefile reached only the read-only capture one, so an interactive session and a
bench-sheet replay were hand-typed command lines with a hand-typed port -- which
is exactly the shape c0eca355 removed from `make flash` after a defaulted
`/dev/ttyUSB0` aimed an ESP32-P4 image at the artoo-esp32 and only esptool's chip
check stopped it.

These read the Makefile as data rather than running it: both targets need a board
on the other end of a cable, so nothing here can execute them, and a port that
silently reverts to a literal is precisely the regression that would not show up
until someone was standing at the bench.
"""

import os
import re
import unittest

REPO_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
MAKEFILE = os.path.join(REPO_ROOT, "Makefile")


def makefile_body() -> str:
    with open(MAKEFILE, encoding="utf-8") as handle:
        return handle.read()


def recipe(target: str) -> str:
    """The recipe lines of `target`, with make's line continuations joined."""
    body = makefile_body()
    match = re.search(rf"^{re.escape(target)}:.*?(?=\n[^\t\n]|\Z)", body, re.S | re.M)
    assert match is not None, f"{target} recipe not found in the Makefile"
    return match.group(0).replace("\\\n", " ")


def console_client_lines() -> list[str]:
    body = makefile_body().replace("\\\n", " ")
    return [line for line in body.splitlines() if "tools/console_client.py" in line
            and (line.startswith("\t") or line.lstrip().startswith("@"))]


class EveryConsoleClientCallResolvesItsPort(unittest.TestCase):
    def setUp(self):
        self.lines = console_client_lines()

    def test_the_makefile_actually_calls_the_console_client(self):
        """Guards the assertions below against silently passing on zero matches
        if the tool is ever renamed or the targets are moved out."""
        self.assertGreaterEqual(len(self.lines), 4)

    def test_no_recipe_hardcodes_a_serial_device(self):
        for line in self.lines:
            with self.subTest(line=line.strip()[:90]):
                self.assertNotRegex(line, r"--port\s+/dev/")

    def test_every_port_comes_from_the_resolver(self):
        """Every one of them resolves in its own recipe: $(RESOLVE_PORT) into
        $$port, then --port $$port. There is no second way to reach a port here,
        and there must not be."""
        for line in self.lines:
            with self.subTest(line=line.strip()[:90]):
                self.assertIn("--port $$port", line)


class InteractiveConsoleTarget(unittest.TestCase):
    def setUp(self):
        self.recipe = recipe("console")

    def test_it_resolves_the_port(self):
        self.assertIn("$(RESOLVE_PORT)", self.recipe)

    def test_it_opens_the_interactive_mode(self):
        self.assertIn("--interactive", self.recipe)

    def test_it_never_uses_the_unsafe_backend(self):
        """--pyserial resets the board 7/7 (docs/troubleshooting.md attach
        matrix) and the client refuses it here anyway; a target that offered it
        would be advertising an unsafe attach."""
        self.assertNotIn("--pyserial", self.recipe)


class BenchRowReplayTarget(unittest.TestCase):
    def setUp(self):
        self.recipe = recipe("bench-rows")

    def test_it_resolves_the_port(self):
        self.assertIn("$(RESOLVE_PORT)", self.recipe)

    def test_it_replays_the_sheet_it_was_given(self):
        self.assertIn("--script $(BENCH_ROWS)", self.recipe)

    def test_it_refuses_an_unset_sheet_instead_of_picking_one(self):
        """A sheet is board-specific; replaying the artoo's rows at a FireBeetle
        is a wrong-board run that reads as a broken firmware."""
        self.assertIn('if [ -z "$(BENCH_ROWS)" ]', self.recipe)
        self.assertIn("exit 1", self.recipe)

    def test_it_forwards_row_selection(self):
        self.assertIn("$(if $(ROWS),--rows $(ROWS))", self.recipe)
        self.assertIn("$(if $(SKIP_MANUAL),--skip-manual)", self.recipe)


class BothTargetsAreDeclaredPhony(unittest.TestCase):
    def setUp(self):
        body = makefile_body().replace("\\\n", " ")
        match = re.search(r"^\.PHONY:.*$", body, re.M)
        self.assertIsNotNone(match, ".PHONY declaration not found in the Makefile")
        self.phony = match.group(0).split()

    def test_console_is_phony(self):
        self.assertIn("console", self.phony)

    def test_bench_rows_is_phony(self):
        self.assertIn("bench-rows", self.phony)


if __name__ == "__main__":
    unittest.main()
