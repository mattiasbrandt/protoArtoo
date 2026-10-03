"""Which Bash command is a firmware or filesystem upload: one answer for every hook.

The five upload hooks (pre_upload_guard, post_upload_hint,
post_upload_failure_hint and the two agent-scoped gates) import this module
instead of each matching words anywhere in the command text. A substring test
fired on a commit message, a heredoc body and a `grep` that merely named an
upload, and missed `make -C sub flash` (#459 item 11).

The rule reads the word that runs: split the command into simple commands,
skip the prefixes that hand the rest to another program unchanged, and ask
whether what is left starts with `make` naming an upload target or `pio` with
an upload target. Text handed to another program as an argument - a commit
message, a `python3 -c` string, a pattern for `grep` - is data, not a command.
"""

from __future__ import annotations

import re
import shlex
from dataclasses import dataclass

# `make flash`, `make ota`, `make uploadfs` and their `-<variant>` targets
# (`flash-monitor`, ...). See the Makefile's upload section.
_MAKE_TARGET = re.compile(r"^(flash|ota|uploadfs)(?:-[\w-]+)?$")
# Options of make that take the next word as their value.
_MAKE_OPTS_WITH_VALUE = {"-C", "-f", "-I", "-o", "-W", "--directory", "--file",
                         "--makefile", "--include-dir", "--old-file", "--what-if"}
_PIO_TARGETS = {"upload", "uploadfs"}
_ASSIGNMENT = re.compile(r"^[A-Za-z_]\w*=")
# The start of a heredoc: `<<WORD`, `<<-WORD`, `<<'WORD'`, `<<"WORD"`. The
# lookbehind keeps a here-string (`<<<`) out.
_HEREDOC = re.compile(r"""(?<!<)<<(?!<)-?\s*(['"]?)([A-Za-z_]\w*)\1""")
# A token made only of these characters separates simple commands. `&` covers
# both `&&` and a background `&`; `(`/`)` cover a subshell.
_SEPARATORS = set(";&|()\n")


@dataclass(frozen=True)
class Upload:
    kind: str          # "upload" (firmware) or "uploadfs" (the filesystem image)
    words: list[str]   # the whole simple command, `VAR=value` prefixes included
    ota: bool          # `make ota`, or a pio `_ota` env


def drop_heredoc_bodies(cmd: str) -> str:
    """The command without the lines a heredoc feeds in.

    A body is data written to a file or a pipe, and it can hold anything,
    including an unbalanced quote, so it goes before the command is split.
    """
    out: list[str] = []
    ends: list[str] = []
    for line in cmd.split("\n"):
        if ends:
            if line.strip() == ends[0]:
                ends.pop(0)
            continue
        out.append(line)
        ends.extend(m.group(2) for m in _HEREDOC.finditer(line))
    return "\n".join(out)


def _tokens(cmd: str) -> list[str]:
    lexer = shlex.shlex(cmd, posix=True, punctuation_chars=";&|()\n")
    # A newline separates commands outside quotes; inside quotes shlex keeps
    # it as part of the word. Taking it out of whitespace (it is in the
    # punctuation set above) makes it a separator token of its own.
    lexer.whitespace = " \t\r"
    lexer.whitespace_split = True
    return list(lexer)


def simple_commands(cmd: str) -> list[list[str]]:
    """The words of each simple command, split on `;`, `&&`, `||`, `|` and newlines."""
    text = drop_heredoc_bodies(cmd)
    try:
        tokens = _tokens(text)
    except ValueError:
        # An unbalanced quote. Bash would refuse it too, so nothing in it runs
        # as written; but this hook runs on every Bash call and must not miss
        # an upload in a command it cannot parse, so it falls back to plain
        # whitespace words - the guard asks rather than stays silent.
        tokens = re.findall(r"[;&|()\n]+|[^\s;&|()]+", text)
    commands: list[list[str]] = [[]]
    for token in tokens:
        if token and set(token) <= _SEPARATORS:
            commands.append([])
        else:
            commands[-1].append(token)
    return [words for words in commands if words]


def _skip_prefixes(words: list[str]) -> list[str]:
    """Drop `VAR=value`, `timeout [opts] N` and `sudo [opts]` in front of the command."""
    index = 0
    while index < len(words):
        word = words[index]
        if _ASSIGNMENT.match(word):
            index += 1
        elif word == "timeout":
            index += 1
            while index < len(words) and words[index].startswith("-"):
                index += 2 if words[index] in ("-s", "-k", "--signal", "--kill-after") else 1
            index += 1  # the duration
        elif word == "sudo":
            index += 1
            while index < len(words) and words[index].startswith("-"):
                index += 2 if words[index] in ("-u", "-g", "-h", "-p", "-C") else 1
        else:
            break
    return words[index:]


def _program(word: str) -> str:
    return word.rsplit("/", 1)[-1]


def _make_target(words: list[str]) -> str:
    index = 1
    while index < len(words):
        word = words[index]
        if word in _MAKE_OPTS_WITH_VALUE:
            index += 2
            continue
        index += 1
        if word.startswith("-") or _ASSIGNMENT.match(word):
            continue
        match = _MAKE_TARGET.match(word)
        if match:
            return match.group(1)
    return ""


def _pio_target(words: list[str]) -> str:
    targets = []
    for index, word in enumerate(words):
        if word in ("-t", "--target") and index + 1 < len(words):
            targets.append(words[index + 1])
        elif word.startswith("--target="):
            targets.append(word.split("=", 1)[1])
        elif word.startswith("-t") and len(word) > 2 and not word.startswith("--"):
            targets.append(word[2:])
    for wanted in ("uploadfs", "upload"):
        if wanted in targets:
            return wanted
    return ""


def _env_is_ota(words: list[str]) -> bool:
    for index, word in enumerate(words):
        if word in ("-e", "--environment") and index + 1 < len(words):
            if words[index + 1].endswith("_ota"):
                return True
        elif word.startswith("--environment=") and word.endswith("_ota"):
            return True
    return False


# Options of flock(1) that take the next word as their value (`flock --help`).
_FLOCK_OPTS_WITH_VALUE = {"-w", "--timeout", "--wait", "-E", "--conflict-exit-code",
                          "--start", "--length"}
_SHELLS = {"sh", "bash", "zsh"}
_PYTHONS = {"python", "python3"}


def _wrapped(words: list[str]) -> list[str] | str | None:
    """The command a known wrapper runs: a word list, a string to re-split, or None.

    This is a list of programs whose argument IS a command, kept explicit and
    short on purpose. Everything else that takes text - a commit message, a
    `python3 -c` string, a `grep` pattern, and `herdr agent prompt`, whose
    argument is text sent to an agent - stays data. The Herdr form matters
    most: `.claude/CLAUDE.md` runs device work as
    `herdr pane run <pane> "tools/gate_in_pane.sh <log> -- <cmd>"` (#459).
    """
    program = _program(words[0])
    # `python3 tools/pio_lock.py ...` and `sh tools/gate_in_pane.sh ...`: the
    # interpreter in front of a known script changes nothing.
    if (program in _PYTHONS | _SHELLS and len(words) > 1
            and _program(words[1]) in ("pio_lock.py", "gate_in_pane.sh")):
        words = words[1:]
        program = _program(words[0])
    if program == "herdr" and words[1:3] == ["pane", "run"] and len(words) > 4:
        return " ".join(words[4:])  # herdr pane run <pane> <cmd>
    if program == "gate_in_pane.sh" and "--" in words:
        return words[words.index("--") + 1:]  # gate_in_pane.sh <log> -- <cmd...>
    if program == "pio_lock.py":
        # main(sys.argv[1:]) execs argv as given: no options, no `--`.
        return words[1:]
    if program in _SHELLS:
        # sh -c "<cmd>", including a cluster ending in c (`bash -lc`).
        for index, word in enumerate(words[1:], start=1):
            if not word.startswith("-"):
                return None
            if not word.startswith("--") and word.endswith("c"):
                return words[index + 1] if index + 1 < len(words) else None
        return None
    if program == "flock":
        index = 1
        while index < len(words) and words[index].startswith("-"):
            if words[index] in ("-c", "--command"):
                return words[index + 1] if index + 1 < len(words) else None
            index += 2 if words[index] in _FLOCK_OPTS_WITH_VALUE else 1
        rest = words[index + 1:]  # past the lock file
        if rest[:1] in (["-c"], ["--command"]):
            return rest[1] if len(rest) > 1 else None
        return rest
    return None


def _upload_in(command: list[str]) -> Upload | None:
    words = _skip_prefixes(command)
    if not words:
        return None
    program = _program(words[0])
    if program == "make":
        target = _make_target(words)
        if target:
            kind = "uploadfs" if target == "uploadfs" else "upload"
            return Upload(kind, command, target == "ota")
        return None
    if program in ("pio", "platformio"):
        target = _pio_target(words)
        if target:
            return Upload(target, command, _env_is_ota(words))
        return None
    inner = _wrapped(words)
    if isinstance(inner, str):
        return find_upload(inner)
    if inner:
        return _upload_in(inner)
    return None


def find_upload(cmd: str) -> Upload | None:
    """The first simple command in `cmd` that uploads, or None."""
    for command in simple_commands(cmd):
        upload = _upload_in(command)
        if upload:
            return upload
    return None


def is_upload_command(cmd: str) -> bool:
    return find_upload(cmd) is not None
