#!/usr/bin/env python3
"""Which Verification jobs a change needs: changed paths in, one line per class out.

Reads the changed paths on stdin, one per line (a rename passes both its old
and its new path), and prints `<class>=true|false` for each class, in the
form `$GITHUB_OUTPUT` takes. `.github/workflows/verification.yml`'s `changes`
job runs it on the pull request's files or the pushed range, and every job
or step that can be skipped is skipped only on its own class saying `false`
(#355).

    git diff --name-only HEAD~1 | python3 tools/ci_changes.py

| Class      | Runs                          |
|------------|-------------------------------|
| `boards`   | the `artoo` and `firebeetle2` jobs |
| `native`   | Native tests                  |
| `analysis` | Static analysis               |
| `web`      | Web logic tests               |
| `tools`    | Tooling tests                 |

The drift checks, "Board jobs cover the budgets" and actionlint belong to no
class: they run on every event.

**It fails closed.** A path no rule names turns every class on, so a new
directory runs everything until someone decides what reads it. An empty list
does too. `--all REASON` prints every class on, for the cases the workflow
cannot list the files at all.

Each rule names the classes whose jobs READ the path, measured from the jobs
rather than guessed from the directory name:

- The board jobs build from `src/`, `include/`, `lib/`, `data/`, `boards/`,
  `partitions/` and `platformio.ini`, and run the Python in `BOARD_TOOLS`:
  `check_build_budgets.py` and what it imports, and the `extra_scripts` the
  budgeted envs name and what those import. That list is held by hand. A new
  import into one of those scripts has to be added to it, or a change to the
  imported file stops rebuilding the boards.
- The native suite (`test_filter = test_native/*`) compiles `src/`,
  `include/`, `lib/`, `test/stubs/`, and opens `data/console_help.txt`,
  `data/asset-sets/` and `test/fixtures/protocol_mirror.json` at run time.
- The web suite reads `data/`, plus `include/component_registry.inc`
  (`test/test_web/helpers/configuration_surface.js`) and
  `tests/fixtures/dome_layout_mk4.json` (`test_guided_setup.js`). Nothing
  else outside `data/`.
- The tooling tests read far more than `tools/`: they scan `src/` and
  `include/`, read `data/`, `docs/*.yaml`, `bench/`, `test/playwright/` and
  `tests/fixtures/`, and `.github/workflows/` (which turns everything on
  anyway).
- A spec sheet is read only by the wiring-card and product drift checks,
  which run on every event, so it needs no class of its own.

Stdlib only: the `changes` job runs it on the runner's own python3, with no
setup step in front of it.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import PurePosixPath

CLASSES = ("boards", "native", "analysis", "web", "tools")
ALL = CLASSES
NONE: tuple[str, ...] = ()

# The Python the board jobs run, and the data it reads, found by following
# imports from tools/check_build_budgets.py and from every extra_scripts entry
# of the budgeted envs in platformio.ini. Held by hand: see the module
# docstring.
BOARD_TOOLS = frozenset({
    "tools/build_budgets.json",
    "tools/check_build_budgets.py",
    "tools/check_framework_envelope.py",
    "tools/extract_version.py",
    "tools/gzip_fsdata.py",
    "tools/littlefs_builder.py",
    "tools/littlefs_image.py",
    "tools/nano_link.py",
    "tools/pio_lock.py",
    "tools/refresh_component_headers.py",
    "tools/slice_verify.py",
    "tools/suite_pause.py",
    "tools/task_stack_recipes.json",
})

# Pictures outside data/ are not shipped in the image and no check reads them.
IMAGE_SUFFIXES = frozenset({".png", ".webp", ".jpg", ".jpeg", ".gif", ".svg"})

FIRMWARE = ("boards", "native", "analysis", "tools")


def _under(path: str, *prefixes: str) -> bool:
    return any(path.startswith(prefix) for prefix in prefixes)


def classify(path: str) -> tuple[tuple[str, ...], str]:
    """The classes `path` turns on, and why. The first rule that matches wins,
    so a narrower rule sits above the broader one it carves out of."""
    p = PurePosixPath(path)
    suffix = p.suffix.lower()

    # What every job's set-up or definition reads, and these rules: a change
    # to which jobs run is checked by all of them.
    if _under(path, ".github/") or path in {
        "Makefile", "package.json", "package-lock.json", "tools/requirements.txt",
        "tools/ci_changes.py",
    }:
        return ALL, "every job is set up or defined by it"

    # Inert: nothing a job builds or runs reads them.
    if _under(path, ".claude/", ".agents/") or path == "LICENSE":
        return NONE, "agent instructions or licence"
    if suffix == ".md" and not _under(path, "docs/spec-sheets/"):
        return NONE, "Markdown"
    if suffix in IMAGE_SUFFIXES and not _under(path, "data/"):
        return NONE, "a picture outside data/"
    if _under(path, "docs/spec-sheets/"):
        return NONE, "a spec sheet: its drift checks run on every event"

    if path == "platformio.ini":
        return FIRMWARE, "the build description"
    if path == "include/component_registry.inc":
        return FIRMWARE + ("web",), "firmware the web suite also reads"
    if _under(path, "src/", "include/"):
        return FIRMWARE, "firmware source"
    if _under(path, "lib/"):
        return ("boards", "native", "analysis"), "a vendored library"

    if path == "data/console_help.txt" or _under(path, "data/asset-sets/"):
        return ("boards", "native", "web", "tools"), "web image a native test opens"
    if _under(path, "data/"):
        return ("boards", "web", "tools"), "the web image"
    if _under(path, "boards/", "partitions/"):
        return ("boards",), "board or partition definition"

    if path in BOARD_TOOLS:
        return ("boards", "tools"), "a script the board build runs"
    if _under(path, "tools/", "test/test_tools/"):
        return ("tools",), "tooling"

    if _under(path, "test/test_native/", "test/stubs/", "test/fixtures/"):
        return ("native",), "native suite"
    if _under(path, "test/test_web/"):
        return ("web",), "web suite"
    if _under(path, "tests/fixtures/"):
        return ("web", "tools"), "a fixture the web suite and a tool read"
    if _under(path, "test/playwright/", "bench/"):
        return ("tools",), "read by the tooling tests"
    if _under(path, "docs/") and suffix in {".yaml", ".yml"}:
        return ("tools",), "a docs registry the tooling tests read"

    return ALL, "no rule names it"


def decide(paths: list[str]) -> tuple[dict[str, bool], dict[str, list[tuple[str, str]]]]:
    """Each class's verdict, and the (path, reason) pairs that turned it on."""
    why: dict[str, list[tuple[str, str]]] = {name: [] for name in CLASSES}
    for path in paths:
        classes, reason = classify(path)
        for name in classes:
            why[name].append((path, reason))
    return {name: bool(why[name]) for name in CLASSES}, why


def explain(verdict: dict[str, bool], why: dict[str, list[tuple[str, str]]],
            shown: int = 3) -> str:
    """Markdown for the run summary: which classes ran, and the first few
    files that turned each one on."""
    lines = []
    for name in CLASSES:
        if not verdict[name]:
            lines.append(f"- `{name}`: skipped")
            continue
        first = ", ".join(f"`{path}` ({reason})" for path, reason in why[name][:shown])
        more = len(why[name]) - shown
        lines.append(f"- `{name}`: ran for {first}" + (f" and {more} more" if more > 0 else ""))
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--all", metavar="REASON",
                        help="turn every class on without reading stdin, for REASON")
    parser.add_argument("--explain", metavar="FILE",
                        help="also write the Markdown reasons to FILE")
    args = parser.parse_args(argv)

    if args.all is not None:
        verdict = {name: True for name in CLASSES}
        reasons = "".join(f"- `{name}`: ran ({args.all})\n" for name in CLASSES)
    else:
        paths = list(dict.fromkeys(line.strip() for line in sys.stdin if line.strip()))
        if paths:
            verdict, why = decide(paths)
            reasons = explain(verdict, why)
        else:
            verdict = {name: True for name in CLASSES}
            reasons = "".join(f"- `{name}`: ran (no changed files listed)\n" for name in CLASSES)

    for name in CLASSES:
        print(f"{name}={'true' if verdict[name] else 'false'}")
    if args.explain:
        with open(args.explain, "w", encoding="utf-8") as out:
            out.write(reasons)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
