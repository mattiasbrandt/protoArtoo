#!/usr/bin/env python3
"""Check that the committed parts catalog outputs still match the YAML they came from.

docs/droid-parts.yaml generates two committed artefacts - include/droid_parts.h
and data/droid_parts.js - and the one failure the generator cannot catch is
nobody running it. An edited catalog with stale outputs beside it is a Part a
builder can see and not wire, or wire and not see, and every Part name on screen
in A1c and C4a is only as true as the last regeneration (#301, #356, #358).

    python3 tools/check_droid_parts_drift.py      # or: make check-parts-drift

REPORT, NEVER REWRITE. This tool follows the convention
tools/check_action_registry_drift.py set: it prints what disagrees and exits 1,
and it writes nothing at all. A check that rebuilds what it tests in place can
never fail twice - the first run silently repairs the drift it was meant to
report, and the second passes on evidence it manufactured itself.

The staleness half runs THE REAL GENERATOR with its writes intercepted in
memory, then byte-compares the result against the committed files. The pattern
is r2d2-astromech-simulator v1.79.0 (175ad1b), tools/check-build.js:35, which
swaps fs.writeFileSync for a capture before importing its builder - added in
that release specifically to stop re-implementing the generator inside the
checker. A checker holding its own copy of the generation rules tests the copy.

Around that sit the checks a byte comparison cannot make, because they are about
the generator rather than about staleness:

  - both orphan directions, the way tools/build.js:31 and :48 check their
    manifest. Every catalog row must reach the committed outputs, and every id
    in them must trace back to a row. build.js records that its second direction
    was a warning until 2026-08-08 and that four modules went missing from the
    manifest during that window, which is why both directions here exit 1.
  - the stamp and the provenance header on each output, including that the
    recorded source digest is the digest of the catalog as committed.
  - every Part Kind the catalog emits has a consumer in the browser. A Kind
    nothing reads is a flag declared where nothing consults it - the defect
    docs/droid-parts.yaml warns about at `kind:` - and `light` costs six Parts
    their treatment the moment its reader goes (#357).

This checker guards STALENESS, never TRUTH. A green run says the committed
outputs are what today's generator makes of today's catalog; it says nothing
about whether the catalog is right. The bearing convention recorded in
docs/droid-parts.yaml's header is the standing example: the position words and
the documented bearing convention contradict each other, this check pins that
contradiction into the committed output quite happily, and settling it needs a
drawing rather than a checker.

Deliberately NOT merged with the three checks D2 owns. They follow the same
report-never-rewrite convention and should read the same way, but they are
separate tools over separate sources.
"""

from __future__ import annotations

import contextlib
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_droid_parts_catalog as gen

# No paths of its own, deliberately. Everything this checks is addressed through
# the generator's own constants, so a catalog, an output or a generator that
# moves cannot leave this tool checking a file nobody writes any more - the
# principle tools/check-packs.js:36 states as "a list written down twice is a
# list that drifts". It also means the generator's own tests can aim this whole
# check at a scratch tree by patching those constants, which is how the
# double-failure case below is exercised without touching the repository.

STAMP = "DO NOT EDIT MANUALLY"
DIGEST_RE = re.compile(r"Source digest: sha256 ([0-9a-f]{64})")
FIRMWARE_TABLE_RE = re.compile(
    r"DROID_PART_IDS\[DROID_PART_COUNT\]\s*=\s*\{(.*?)\n\};", re.S
)
FIRMWARE_ID_RE = re.compile(r'"([A-Za-z0-9_]+)"')
FIRMWARE_COUNT_RE = re.compile(r"constexpr size_t DROID_PART_COUNT = (\d+);")
FIRMWARE_ID_MAX_RE = re.compile(r"constexpr size_t DROID_PART_ID_MAX_LEN = (\d+);")
BROWSER_ASSIGNMENT = "window.DroidParts = "


class Wrote(Exception):
    """The generator wrote somewhere this check was not expecting.

    Loud rather than passed through to the real filesystem: this tool's whole
    contract is that it writes nothing, and a generator that grew a third output
    has to be taught to this checker rather than quietly writing it from here.
    """


@contextlib.contextmanager
def writes_captured(targets):
    """Run the generator with every write it makes redirected into memory.

    `targets` is the set of resolved paths the generator is expected to write;
    what it writes to each arrives in the yielded dict instead of on disk. The
    swap is on Path.write_text, which is what the generator uses, and it is
    restored whatever happens - the same shape as check-build.js:35 swapping
    fs.writeFileSync around its builder import.
    """
    captured = {}
    real_write_text = Path.write_text

    def intercept(self, data, *args, **kwargs):
        resolved = self.resolve()
        if resolved not in targets:
            raise Wrote(f"{resolved}")
        captured[resolved] = data
        return len(data)

    Path.write_text = intercept
    try:
        yield captured
    finally:
        Path.write_text = real_write_text


def build_in_memory(errors):
    """The two outputs the current generator makes of the current catalog.

    Returns (catalog, {path: text}), or (None, {}) when the catalog itself is
    broken - which is a drift report of its own rather than a traceback, since
    an operator running this has a catalog to fix either way.
    """
    targets = {
        gen.FIRMWARE_OUTPUT_PATH.resolve(),
        gen.BROWSER_OUTPUT_PATH.resolve(),
    }
    try:
        with writes_captured(targets) as captured:
            catalog = gen.generate(quiet=True)
    except gen.CatalogError as error:
        for problem in error.problems:
            errors.append(f"{gen.rel(gen.CATALOG_PATH)}: {problem}")
        return None, {}
    except Wrote as error:
        errors.append(
            f"the generator wrote to {error}, which this check does not know "
            "about; teach it that output before trusting a green run"
        )
        return None, {}
    return catalog, captured


def check_committed_bytes(captured, errors):
    """Byte-compare each committed output against the fresh run.

    Bytes rather than text: a re-encoding, a stray BOM or a line ending that
    changed under an editor is drift by any definition a consumer cares about,
    and reading both sides as characters would normalise exactly those away.
    """
    for path in (gen.FIRMWARE_OUTPUT_PATH, gen.BROWSER_OUTPUT_PATH):
        name = gen.rel(path)
        if not path.exists():
            errors.append(f"{name} is missing; run the generator")
            continue
        committed = path.read_bytes()
        built = captured[path.resolve()].encode("utf-8")
        if committed != built:
            errors.append(
                f"{name} is not what {gen.GENERATOR_NAME} produces from "
                f"{gen.CATALOG_NAME} today "
                f"({len(committed)} bytes committed, {len(built)} generated); "
                "regenerate it"
            )


def check_stamp_and_provenance(errors):
    """Each committed output says where it came from, and says it truthfully.

    The digest is the one line here that can be wrong rather than merely absent:
    it names the catalog bytes the file was generated from, so comparing it with
    the catalog as committed catches a hand-repaired output that was never
    regenerated - the case where somebody fixes one name by hand and the rest of
    the file stays a commit behind.
    """
    digest = hashlib.sha256(gen.CATALOG_PATH.read_bytes()).hexdigest()
    for path in (gen.FIRMWARE_OUTPUT_PATH, gen.BROWSER_OUTPUT_PATH):
        name = gen.rel(path)
        if not path.exists():
            continue  # already reported by the byte comparison
        text = path.read_text(encoding="utf-8")
        if STAMP not in text:
            errors.append(f"{name} carries no {STAMP!r} stamp")
        for cited in (gen.CATALOG_NAME, gen.GENERATOR_NAME):
            if cited not in text:
                errors.append(f"{name} provenance header does not name {cited}")
        found = DIGEST_RE.search(text)
        if found is None:
            errors.append(f"{name} provenance header carries no source digest")
        elif found.group(1) != digest:
            errors.append(
                f"{name} records source digest {found.group(1)}, but "
                f"{gen.CATALOG_NAME} is {digest}; the output is a catalog behind"
            )


def committed_browser_payload(errors):
    """The committed browser module's payload, read as the page would get it.

    The module is one assignment of one JSON object, so the object is decoded
    from the assignment rather than pattern-matched: raw_decode stops at the end
    of the value and never has to guess where the JSON ends.
    """
    path = gen.BROWSER_OUTPUT_PATH
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8")
    start = text.find(BROWSER_ASSIGNMENT)
    if start < 0:
        errors.append(f"{gen.rel(path)} assigns no {BROWSER_ASSIGNMENT.strip(' =')}")
        return None
    try:
        payload, _ = json.JSONDecoder().raw_decode(text, start + len(BROWSER_ASSIGNMENT))
    except ValueError as error:
        errors.append(f"{gen.rel(path)}: its payload is not readable JSON: {error}")
        return None

    # Shape before content. A committed output is a file somebody can damage by
    # hand, and every reader below indexes into these records - so a part that
    # lost its id is reported here rather than raised three functions later,
    # where the operator gets a traceback instead of a drift report.
    parts = payload.get("parts") if isinstance(payload, dict) else None
    if not isinstance(parts, list):
        errors.append(f"{gen.rel(path)}: its payload carries no list of parts")
        return None
    for position, part in enumerate(parts):
        if not isinstance(part, dict) or not isinstance(part.get("id"), str):
            errors.append(f"{gen.rel(path)}: parts[{position}] carries no id")
            return None
    return payload


def committed_firmware_ids(errors):
    """The id table as committed, read out of the header rather than rebuilt."""
    path = gen.FIRMWARE_OUTPUT_PATH
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8")
    table = FIRMWARE_TABLE_RE.search(text)
    if table is None:
        errors.append(f"{gen.rel(path)} carries no DROID_PART_IDS table")
        return None
    ids = FIRMWARE_ID_RE.findall(table.group(1))

    # The two constants beside the table describe it, and a consumer sizes a
    # buffer against them. A table edited by hand past the count is the one
    # shape of damage that compiles.
    count = FIRMWARE_COUNT_RE.search(text)
    if count is None:
        errors.append(f"{gen.rel(path)} declares no DROID_PART_COUNT")
    elif int(count.group(1)) != len(ids):
        errors.append(
            f"{gen.rel(path)}: DROID_PART_COUNT is {count.group(1)} and the "
            f"table holds {len(ids)} ids"
        )
    longest = FIRMWARE_ID_MAX_RE.search(text)
    if longest is None:
        errors.append(f"{gen.rel(path)} declares no DROID_PART_ID_MAX_LEN")
    elif ids and int(longest.group(1)) != max(len(i) for i in ids):
        errors.append(
            f"{gen.rel(path)}: DROID_PART_ID_MAX_LEN is {longest.group(1)} and "
            f"the longest id in the table is {max(len(i) for i in ids)}"
        )
    return ids


def check_orphans(catalog, payload, firmware_ids, errors):
    """Both directions, for both outputs: nothing declared is lost, nothing
    generated is invented.

    A byte comparison already fails when either side moves, so what this adds is
    a NAMED failure: "doorFL is declared and reaches no output" sends a reader
    to the row, where "the file differs" sends them to a diff. It is also the
    direction that survives a generator defect, since a wrong generator agrees
    with itself perfectly.

    Both outputs carry every declared Part since #358 - `control:` says what is
    drivable, not what is nameable - so the two lists are compared against the
    catalog on exactly the same terms.
    """
    declared = [part["id"] for part in catalog["parts"]]
    for name, emitted in (
        (gen.BROWSER_NAME, None if payload is None else [p["id"] for p in payload["parts"]]),
        (gen.FIRMWARE_NAME, firmware_ids),
    ):
        if emitted is None:
            continue
        for part_id in declared:
            if part_id not in emitted:
                errors.append(
                    f"{gen.CATALOG_NAME} declares {part_id}, which reaches {name} "
                    "not at all"
                )
        for part_id in emitted:
            if part_id not in declared:
                errors.append(
                    f"{name} carries {part_id}, which traces back to no row in "
                    f"{gen.CATALOG_NAME}"
                )


def browser_modules():
    """The hand-written browser modules - every consumer candidate there is.

    The generated catalog is excluded because a Kind consumed only by the file
    that emitted it is exactly the case being looked for.
    """
    generated = gen.BROWSER_OUTPUT_PATH.resolve()
    return [
        path
        for path in sorted(gen.BROWSER_OUTPUT_PATH.parent.glob("*.js"))
        if path.resolve() != generated
    ]


def check_kinds_have_a_consumer(payload, errors):
    """Every Part Kind the catalog emits is read by something in the browser.

    A Kind is advisory and firmware has no use for one, so the browser is the
    only place a consumer can be. `light` is the whole reason a PSI, a logic
    display and the Magic Panel are Parts rather than footnotes on a panel row
    (#357, ADR 0045): with nothing reading it those six Parts silently get the
    treatment of something that moves - travel, throw and position offered for a
    device that has none.

    A consumer is a module that spells the token as a string literal. That is a
    presence check rather than a proof that the module branches on `kind`, so
    the summary names the file that answered for each Kind: a match in a module
    that has nothing to do with Part Kinds is meant to be visible to whoever
    reads the output.
    """
    if payload is None:
        return {}
    kinds = sorted({part["kind"] for part in payload["parts"] if "kind" in part})
    consumers = {}
    modules = [(path, path.read_text(encoding="utf-8")) for path in browser_modules()]
    for kind in kinds:
        reading = [
            gen.rel(path)
            for path, text in modules
            if f'"{kind}"' in text or f"'{kind}'" in text
        ]
        if not reading:
            errors.append(
                f"Part Kind {kind!r} reaches {gen.BROWSER_NAME} and no browser "
                f"module reads it; a Kind nothing consults gives every Part "
                f"carrying it the treatment of something it is not"
            )
        else:
            consumers[kind] = reading
    return consumers


def main() -> int:
    errors: list[str] = []
    catalog, captured = build_in_memory(errors)
    if catalog is None:
        print("Droid parts catalog drift detected:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    check_committed_bytes(captured, errors)
    check_stamp_and_provenance(errors)
    payload = committed_browser_payload(errors)
    firmware_ids = committed_firmware_ids(errors)
    check_orphans(catalog, payload, firmware_ids, errors)
    consumers = check_kinds_have_a_consumer(payload, errors)

    if errors:
        print("Droid parts catalog drift detected:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    kinds = ", ".join(
        f"{kind} -> {', '.join(reading)}" for kind, reading in sorted(consumers.items())
    )
    print(
        f"Droid parts catalog drift check passed "
        f"({len(catalog['parts'])} parts, "
        f"{len(catalog['designs'])} designs, "
        f"2 committed outputs byte-identical to a fresh run"
        f"{'; kinds ' + kinds if kinds else ''})."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
