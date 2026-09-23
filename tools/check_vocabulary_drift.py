#!/usr/bin/env python3
"""Check that no operator copy says "controller" without saying which one.

A droid carries at least four: the **Body Controller** that runs this firmware,
the **Radio Controller** the builder drives it with, the **Dome Controller** in
the dome, and a motor controller turning the feet. `CONTEXT.md` settled it by
qualifying every one and never using the bare word in operator copy **at all -
unconditionally, not only where a second kind can be on screen, so the word
never depends on what else the page happens to show** (`CONTEXT.md` Flagged
Ambiguities, #298, 2026-09-08). #348 then did the sweep. Nothing mechanical
stopped it coming back, which is what this is.

It parses `CONTEXT.md` rather than restating it: the qualified forms are its
own `**... Controller**:` term headings, so adding a term adds a qualifier and
this file needs no edit. "A list written down twice is a list that drifts."

WHAT IT READS. Operator copy as `tools/operator_copy.py` defines it - string
literals, HTML text and the four read attributes, plus `display_name` and
`description` in `docs/action-registry.yaml`, with every comment blanked first.
A comment saying "the controller" is a note to a maintainer and is not a defect;
five registry descriptions saying it are, because they are generated into
`data/console_help.txt` and read in the Console.

WHAT IT DELIBERATELY DOES NOT DO. It does not run the `_Avoid_` lines of
`CONTEXT.md` as a general banned-string list, although `CONTEXT.md:5-19`
describes exactly that and calls backticks the mark of a greppable entry.
Measured on `epic/operator-experience` while this was written: 212 `_Avoid_`
lines carry **8** backticked entries, and not one of them is a banned string -
every one quotes a token the rule is *about* (`amber on `checking``, "editing
`secrets.h``, "`:SM` sequence authoring"). Run as banned strings they fail the
build on 33 correct uses of `Checking`, which is the shipped Availability
state. The convention's own worked example, ``main controller``, is written
plain in the real line. The engine goes in the day `CONTEXT.md` can say which
backtick means "never write this"; until then a rule with no exact source is a
rule that gets muted (#353).

Report, never rewrite - the convention `tools/check_action_registry_drift.py`
set. Run it as `make check-vocabulary-drift`. Its unit tests, which drive it
against fixtures and prove it can fail, are
`test/test_tools/test_vocabulary_drift.py`.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from operator_copy import (  # noqa: E402  (after the path insert above)
    Copy,
    ROOT,
    is_identifier_context,
    registry_copy,
    surface_copy,
)

CONTEXT = ROOT / "CONTEXT.md"

WORD = re.compile(r"\bcontrollers?\b", re.I)

# `**Body Controller**:` and friends - the glossary's own headings.
TERM_HEADING = re.compile(r"^\*\*([^*]+)\*\*:\s*$", re.M)

# Words that stand in front of a noun without naming it. "the controller" is
# the defect; "wheel controller", "servo controller", "Artoo Controller" are
# not, because each says which one. Articles, demonstratives, possessives and
# the prepositions and conjunctions a sentence puts there are the closed set -
# anything else in that slot is a qualifier by definition, which is why this
# list can be finite and the qualifier list cannot.
FUNCTION_WORDS = frozenset(
    """
    a an the this that these those its their his her your our my whose
    no any every each some one another both either neither such
    and or but nor if when while because so than then as
    from to for of in on at by with via into onto over under about
    is are was were be been being it they we you i there here
    """.split()
)

PRECEDING_WORD = re.compile(r"([\w$']+)[\s’]*$")


def qualified_forms(context: Path = CONTEXT) -> list[str]:
    """Every `CONTEXT.md` term that carries the word, longest first.

    Longest first so "Controller Upload Verified" is matched before
    "Controller Console" could claim its first word.
    """
    headings = TERM_HEADING.findall(context.read_text(encoding="utf-8"))
    terms = [heading.strip() for heading in headings if WORD.search(heading)]
    return sorted(set(terms), key=len, reverse=True)


def mask_terms(text: str, terms: list[str]) -> str:
    """Blank every established term, keeping the length so offsets still hold."""
    for term in terms:
        text = re.sub(
            re.escape(term),
            lambda match: " " * len(match.group(0)),
            text,
            flags=re.I,
        )
    return text


def bare_uses(text: str, terms: list[str]) -> list[str]:
    """The bare occurrences in one run of copy, as the words around each."""
    masked = mask_terms(text, terms)
    found = []
    for match in WORD.finditer(masked):
        if is_identifier_context(masked, match.start(), match.end()):
            continue
        word = PRECEDING_WORD.search(masked[: match.start()])
        if word and word.group(1).lower() not in FUNCTION_WORDS:
            continue  # something names which controller this is
        opening = masked[: match.start()].rsplit("\n", 1)[-1][-40:].strip()
        found.append(f"{opening} {match.group(0)}".strip())
    return found


def check(context: Path = CONTEXT, data: Path | None = None,
          registry: Path | None = None) -> tuple[list[str], int, list[str]]:
    """(findings, runs of copy read, the qualified forms). Reads; never writes."""
    terms = qualified_forms(context)
    findings: list[str] = []
    copy: list[Copy] = []
    copy.extend(surface_copy(data) if data is not None else surface_copy())
    copy.extend(registry_copy(registry) if registry is not None else registry_copy())

    if not terms:
        findings.append(
            f"{context.name} declares no `**... Controller**:` term, so this check has no "
            "qualified form to accept. Next move: restore the glossary terms, or correct "
            "TERM_HEADING in tools/check_vocabulary_drift.py to the new heading shape"
        )
        return findings, len(copy), terms

    for piece in copy:
        for phrase in bare_uses(piece.text, terms):
            findings.append(
                f'{piece.where()}: "{phrase}" - which controller? ({piece.kind}). '
                'Next move: name it, as CONTEXT.md "Body Controller" does'
            )
    return findings, len(copy), terms


def main(**sources) -> int:
    """`make check-vocabulary-drift`. `sources` are check()'s arguments."""
    findings, read, terms = check(**sources)
    if findings:
        print("Vocabulary drift detected:", file=sys.stderr)
        for finding in findings:
            print(f"  - {finding}", file=sys.stderr)
        return 1
    print(
        f"Vocabulary check passed ({read} runs of operator copy read, "
        f"{len(terms)} qualified forms accepted)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
