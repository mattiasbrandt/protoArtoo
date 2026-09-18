#!/usr/bin/env python3
"""Check that include/config.h and docs/pin_map.md name the same GPIO for every pin.

The two files are the project's two homes for hardware truth, and nothing but
this check connects them: the compiler reads one, a builder with a soldering
iron reads the other. A pin moved in only one of them is the defect.

**The Source of Truth inverts per board**, and this check does not assume a
direction. On artoo-esp32 the traced PCB is the truth and `docs/pin_map.md`
records it, so `include/config.h` must follow the document. On FireBeetle 2 the
spec sheet and `include/config.h` are the truth and the document is derived
from them. Which side is authoritative is read from the document's own "Source
of Truth Contract" section - never restated here - and a finding names it, so
the reader knows which file to correct.

**It parses both sides and owns neither.** Every GPIO number it compares is
read from one of the two files; none is written down here. What this file does
own is the join: which document row names which `PIN_*` constant (`ROW_NAMES`).
A row whose key cell carries a `PIN_*` name joins itself and needs no entry.

What it reads, and what it deliberately does not:

- `include/config.h`: every `constexpr uint8_t PIN_* = ...;` inside a
  `#if PA_BOARD == PA_BOARD_*` arm. An alias (`PIN_SBUS1_RX = PIN_RC_CH1`) is
  followed to the pin it names. The `AUX_LED_PIN_*` values are NOT pins - they
  are the AUX slot index behind `robotState.auxLed.pin` (0 = disabled, 1..3 =
  AUX1..AUX3) - and the constant pattern is anchored so they are never read as
  GPIO numbers.
- `docs/pin_map.md`: every table with a `GPIO`, `TX GPIO` or `RX GPIO` column
  inside a section `SECTION_BOARDS` assigns to a board. Prose and tables in
  other sections (the artoo.uk cross-reference, the GPIO budget) are not read.
- `include/board_lanes.inc`: the Board Lanes, so a board whose arm declares no
  lane is reported rather than passed vacuously.

Report, never rewrite - the convention `tools/check_action_registry_drift.py`
set. Run it as `make check-pin-drift`. Its unit tests, which drive it against
synthetic fixtures and prove it can fail, are
`test/test_tools/test_pin_drift.py`. The pin half of the D2 checks (#353)
reuses `check()` rather than implementing a second one.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CONFIG_H = ROOT / "include" / "config.h"
PIN_MAP = ROOT / "docs" / "pin_map.md"
BOARD_LANES = ROOT / "include" / "board_lanes.inc"

ARTOO = "PA_BOARD_ARTOO_ESP32"
FIREBEETLE = "PA_BOARD_FIREBEETLE2"

# The `##` sections of docs/pin_map.md that state one board's wiring. A `###`
# subsection belongs to the `##` above it. Listed rather than guessed from the
# heading text because the LDO section names neither board in its title, and a
# section that disappears is reported rather than silently no longer read.
SECTION_BOARDS = {
    "Serial Ports (Artoo-esp32)": ARTOO,
    "GPIO Assignment Summary (Artoo-esp32)": ARTOO,
    "Serial Ports (FireBeetle 2)": FIREBEETLE,
    "FireBeetle 2: GPIO Assignment Summary": FIREBEETLE,
    "Known Issue: GPIO 48-52 LDO Rails (Unmeasured)": FIREBEETLE,
}

# Which column of a table names the row, in order of preference: the PCB
# silkscreen where the board has one, then a signal or function description.
KEY_COLUMNS = ("PCB Connector", "PCB Header", "Signal", "Function")

# The join: which document row names which config.h pin, per board. Row names,
# never GPIO numbers. A serial-port table's TX/RX columns become "<row> TX" and
# "<row> RX". None marks a row the document carries for a pin the firmware does
# not drive, so it is read and deliberately not compared.
ROW_NAMES: dict[str, dict[str, str | None]] = {
    ARTOO: {
        "S0 TX": None,  # UART0, the USB debug console: no firmware pin constant
        "S0 RX": None,
        "S1 TX": "PIN_DRIVE_TX",
        "S1 RX": "PIN_DRIVE_RX",
        "S2 TX": "PIN_AUDIO_TX",
        "S2 RX": "PIN_AUDIO_RX",
        "S3 TX": "PIN_DOME_TX",
        "S3 RX": "PIN_DOME_RX",
        "CH1": "PIN_RC_CH1",
        "CH2": "PIN_RC_CH2",
        "CH3": "PIN_RC_CH3",
        "CH4": "PIN_RC_CH4",
        "CH5": "PIN_RC_CH5",
        "CH6": "PIN_RC_CH6",
        "ARM1": "PIN_ARM1_SERVO",
        "ARM2": "PIN_ARM2_SERVO",
        "ARM3": "PIN_ARM3_SERVO",
        "ARM4": "PIN_ARM4_SERVO",
        "ARM5": "PIN_ARM5_SERVO",
        "DOME": "PIN_DOME_ESC",
        "I2C (C)": "PIN_I2C_SCL",
        "I2C (D)": "PIN_I2C_SDA",
    },
    FIREBEETLE: {
        "Drive (UART1) TX": "PIN_DRIVE_TX",
        "Drive (UART1) RX": "PIN_DRIVE_RX",
        "Dome link (UART2) TX": "PIN_DOME_TX",
        "Dome link (UART2) RX": "PIN_DOME_RX",
        "Audio module (UART3) TX": "PIN_AUDIO_TX",
        "Audio module (UART3) RX": "PIN_AUDIO_RX",
        "SBUS receiver #1 (drive)": "PIN_SBUS1_RX",
        "SBUS receiver #2 (dome)": "PIN_SBUS2_RX",
        "RC channel #3": "PIN_RC_CH3",
        "RC channel #4": "PIN_RC_CH4",
        "RC channel #5": "PIN_RC_CH5",
        "RC channel #6": "PIN_RC_CH6",
        "Audio module TX (UART3)": "PIN_AUDIO_TX",
        "Audio module RX (UART3)": "PIN_AUDIO_RX",
        "Arm servo #1 (left/top)": "PIN_ARM1_SERVO",
        "Arm servo #2 (right/bottom)": "PIN_ARM2_SERVO",
        "Arm servo #3 (aux strip)": "PIN_ARM3_SERVO",
        "Arm servo #4 (aux strip)": "PIN_ARM4_SERVO",
        "Arm servo #5 (aux strip)": "PIN_ARM5_SERVO",
        "Dome rotation ESC": "PIN_DOME_ESC",
        "Drive TX (UART1)": "PIN_DRIVE_TX",
        "Drive RX (UART1)": "PIN_DRIVE_RX",
        "Dome TX (UART2)": "PIN_DOME_TX",
        "Dome RX (UART2)": "PIN_DOME_RX",
        "I2C SDA": "PIN_I2C_SDA",
        "I2C SCL": "PIN_I2C_SCL",
        # GPIO 52 in the LDO table: held free on purpose.
        "deliberately unassigned \u2014 the board's sole free GPIO": None,
    },
}

UNASSIGNED = "PA_PIN_UNASSIGNED"

# `\s` before PIN_ anchors the name, so AUX_LED_PIN_AUX1 and friends - slot
# indexes, not GPIOs - never match.
PIN_CONSTANT = re.compile(r"^\s*constexpr\s+uint8_t\s+(PIN_[A-Z0-9_]+)\s*=\s*([^;]+);")
BOARD_CONDITION = re.compile(r"^\s*#\s*(if|elif)\s+PA_BOARD\s*==\s*(PA_BOARD_[A-Z0-9_]+)\s*$")
PIN_NAME = re.compile(r"\bPIN_[A-Z0-9_]+\b")


def board_name(board: str) -> str:
    """`PA_BOARD_ARTOO_ESP32` -> `artoo_esp32`, the build's own spelling."""
    return board.removeprefix("PA_BOARD_").lower()


def rel(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path)


@dataclass(frozen=True)
class ConfigPin:
    name: str
    value: str  # the literal right-hand side: a number, another PIN_*, or PA_PIN_UNASSIGNED
    line: int


@dataclass(frozen=True)
class DocClaim:
    row: str
    gpio: int | None  # None is a TBD cell: documented as unassigned
    line: int
    section: str


@dataclass
class Authority:
    """One board's ranking of the two files, as the document's contract states it."""

    ranked: list[tuple[str, str]] = field(default_factory=list)  # (path, parenthetical)
    line: int = 0


# -- include/config.h ---------------------------------------------------------

def load_config_pins(path: Path, errors: list[str]) -> dict[str, dict[str, ConfigPin]]:
    """Every PIN_* constant, grouped by the PA_BOARD arm it is declared in.

    Conditionals are tracked as a stack so a `#if` nested inside a board arm
    does not end the arm, and so the other `#if PA_BOARD ==` ladders in the
    file (chip target, board name) are read the same way - they simply carry
    no pin constants.
    """
    stack: list[str | None] = []
    boards: dict[str, dict[str, ConfigPin]] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        condition = BOARD_CONDITION.match(raw)
        directive = raw.strip()
        if condition:
            if condition.group(1) == "if":
                stack.append(condition.group(2))
            elif stack:
                stack[-1] = condition.group(2)
            continue
        if re.match(r"#\s*if", directive):
            stack.append(None)
            continue
        if re.match(r"#\s*(elif|else)\b", directive):
            if stack:
                stack[-1] = None
            continue
        if re.match(r"#\s*endif\b", directive):
            if stack:
                stack.pop()
            continue

        match = PIN_CONSTANT.match(raw)
        if not match:
            continue
        board = next((frame for frame in reversed(stack) if frame), None)
        name, value = match.group(1), match.group(2).strip()
        if board is None:
            errors.append(
                f"{rel(path)}:{number}: {name} is declared outside any `#if PA_BOARD ==` arm, so "
                "this check cannot tell which board it belongs to"
            )
            continue
        pins = boards.setdefault(board, {})
        if name in pins:
            errors.append(
                f"{rel(path)}:{number}: {name} is declared twice for {board_name(board)} "
                f"(first at line {pins[name].line})"
            )
            continue
        pins[name] = ConfigPin(name, value, number)
    return boards


def resolve(pins: dict[str, ConfigPin], name: str) -> tuple[str, int | None] | str:
    """Follow aliases to a GPIO. Returns (resolved name, gpio) or an error string."""
    seen: list[str] = []
    while True:
        if name in seen:
            return f"alias cycle {' -> '.join(seen + [name])}"
        seen.append(name)
        pin = pins.get(name)
        if pin is None:
            return f"{name} is not declared in this board's arm"
        if pin.value == UNASSIGNED:
            return name, None
        if pin.value.isdigit():
            return name, int(pin.value)
        if PIN_NAME.fullmatch(pin.value):
            name = pin.value
            continue
        return f"{pin.name} = {pin.value} is not a GPIO number, PA_PIN_UNASSIGNED or another PIN_*"


# -- include/board_lanes.inc --------------------------------------------------

def load_lanes(path: Path, errors: list[str]) -> list[tuple[str, str, str]]:
    """(lane, tx pin, rx pin) for every PA_BOARD_LANE row.

    Strict in the same way `load_x_macro_manifest()` in the action-registry
    check is: a line that is neither a comment, a directive nor a row is
    reported, so a malformed row cannot vanish from the count.
    """
    row = re.compile(r"PA_BOARD_LANE\(\s*(\w+)\s*,\s*\w+\s*,\s*(PIN_\w+)\s*,\s*(PIN_\w+)\s*\)")
    lanes: list[tuple[str, str, str]] = []
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        match = row.fullmatch(line)
        if not match:
            errors.append(f"{rel(path)}:{number}: unreadable PA_BOARD_LANE row: {line}")
            continue
        lanes.append(match.groups())
    return lanes


# -- docs/pin_map.md ----------------------------------------------------------

def split_sections(text: str) -> dict[str, list[tuple[int, str]]]:
    """`##` heading -> its lines (with line numbers), `###` subsections included."""
    sections: dict[str, list[tuple[int, str]]] = {}
    current: str | None = None
    for number, line in enumerate(text.splitlines(), start=1):
        heading = re.match(r"^##\s+(.+?)\s*$", line)
        if heading:
            current = heading.group(1)
            sections[current] = []
            continue
        if current is not None:
            sections[current].append((number, line))
    return sections


def split_row(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def clean(cell: str) -> str:
    return cell.replace("`", "").replace("**", "").strip()


def read_tables(lines: list[tuple[int, str]]) -> list[tuple[int, list[str], list[tuple[int, list[str]]]]]:
    """Markdown tables as (header line, header cells, [(line, cells)])."""
    tables = []
    index = 0
    while index < len(lines):
        number, line = lines[index]
        if line.lstrip().startswith("|") and index + 1 < len(lines) and re.match(
            r"^\s*\|[\s:|-]+\|\s*$", lines[index + 1][1]
        ):
            header = [clean(cell) for cell in split_row(line)]
            rows = []
            index += 2
            while index < len(lines) and lines[index][1].lstrip().startswith("|"):
                rows.append((lines[index][0], split_row(lines[index][1])))
                index += 1
            tables.append((number, header, rows))
            continue
        index += 1
    return tables


def parse_gpio(cell: str) -> int | None | str:
    value = clean(cell)
    if value.isdigit():
        return int(value)
    if value.upper() == "TBD":
        return None
    return f"unreadable GPIO cell {cell!r}"


def load_doc_claims(path: Path, errors: list[str],
                    section_boards: dict[str, str] | None = None) -> dict[str, list[DocClaim]]:
    """Every GPIO the document states, per board, keyed by the row that states it."""
    section_boards = SECTION_BOARDS if section_boards is None else section_boards
    sections = split_sections(path.read_text(encoding="utf-8"))
    claims: dict[str, list[DocClaim]] = {}

    for heading, board in section_boards.items():
        if heading not in sections:
            errors.append(
                f"{rel(path)} has no `## {heading}` section - the document was restructured and "
                f"this check has stopped reading {board_name(board)}'s rows there. Update "
                "SECTION_BOARDS in tools/check_pin_drift.py to the new heading"
            )
            continue
        for header_line, header, rows in read_tables(sections[heading]):
            gpio_columns = [
                (index, "" if name == "GPIO" else " " + name.split()[0])
                for index, name in enumerate(header)
                if name in ("GPIO", "TX GPIO", "RX GPIO")
            ]
            if not gpio_columns:
                continue
            key_index = next((header.index(key) for key in KEY_COLUMNS if key in header), None)
            if key_index is None:
                errors.append(
                    f"{rel(path)}:{header_line}: a GPIO table in `{heading}` has none of the "
                    f"columns {', '.join(KEY_COLUMNS)}, so its rows cannot be joined to a pin"
                )
                continue
            for number, cells in rows:
                if len(cells) != len(header):
                    errors.append(
                        f"{rel(path)}:{number}: row has {len(cells)} cells, its table header has "
                        f"{len(header)}"
                    )
                    continue
                key = clean(cells[key_index])
                for column, suffix in gpio_columns:
                    gpio = parse_gpio(cells[column])
                    if isinstance(gpio, str):
                        errors.append(f"{rel(path)}:{number}: {gpio} in row {key!r}")
                        continue
                    claims.setdefault(board, []).append(DocClaim(key + suffix, gpio, number, heading))
    return claims


def load_authority(path: Path, errors: list[str]) -> dict[str, Authority]:
    """The "Source of Truth Contract": each board's ranked list of sources.

    `**For <board> (`PA_BOARD_X`):**` followed by a numbered list whose items
    open with a backticked path and may carry a parenthetical reason.
    """
    sections = split_sections(path.read_text(encoding="utf-8"))
    lines = sections.get("Source of Truth Contract")
    if lines is None:
        errors.append(
            f"{rel(path)} has no `## Source of Truth Contract` section, so this check cannot say "
            "which file is authoritative on which board"
        )
        return {}
    authority: dict[str, Authority] = {}
    current: Authority | None = None
    for number, line in lines:
        board = re.match(r"^\*\*For .*\(`(PA_BOARD_[A-Z0-9_]+)`\):\*\*", line)
        if board:
            current = authority.setdefault(board.group(1), Authority(line=number))
            continue
        item = re.match(r"^\d+\.\s+`([^`]+)`\s*(?:\((.*)\))?", line)
        if item and current is not None:
            current.ranked.append((item.group(1), (item.group(2) or "").strip()))
    return authority


# -- the comparison -----------------------------------------------------------

@dataclass
class Report:
    errors: list[str] = field(default_factory=list)
    compared: dict[str, int] = field(default_factory=dict)  # board -> pins compared
    lanes: dict[str, int] = field(default_factory=dict)  # board -> lanes resolved


def authority_sentence(board: str, authority: dict[str, Authority], doc: Path) -> str:
    """Which of the two files wins on this board, and so what to do next.

    The contract names the two files by their repository paths, so they are
    looked up by those - never by the paths being checked, which a caller may
    point at copies.
    """
    entry = authority.get(board)
    config_name, doc_name = rel(CONFIG_H), rel(PIN_MAP)
    names = {config_name, doc_name}
    ranked = [(source, reason) for source, reason in entry.ranked] if entry else []
    pair = [(source, reason) for source, reason in ranked if source in names]
    if len(pair) != 2:
        return (
            f"The Source of Truth Contract in {rel(doc)} does not rank {config_name} "
            f"against {doc_name} for {board_name(board)}, so this check cannot say which side is "
            "wrong. Next move: add the board to the contract, then reconcile the pin"
        )
    (winner, reason), (loser, _) = pair
    above = [source for source, _ in ranked[: ranked.index(pair[0])]]
    because = f': "{reason}"' if reason else ""
    sentence = (
        f"On {board_name(board)}, {winner} is authoritative and {loser} follows it "
        f"(Source of Truth Contract, {rel(doc)}:{entry.line}{because}). "
        f"Next move: correct {loser} to match {winner}"
    )
    if above:
        sentence += f", after confirming {winner} against {', '.join(above)}, which ranks above both"
    return sentence + f". Change {winner} instead only if it is the one that is wrong"


def check(config: Path = CONFIG_H, doc: Path = PIN_MAP, lanes_path: Path = BOARD_LANES,
          section_boards: dict[str, str] | None = None,
          row_names: dict[str, dict[str, str | None]] | None = None) -> Report:
    """Compare every board's pins across the two files. Reads; never writes."""
    row_names = ROW_NAMES if row_names is None else row_names
    report = Report()
    errors = report.errors

    boards = load_config_pins(config, errors)
    claims = load_doc_claims(doc, errors, section_boards)
    authority = load_authority(doc, errors)
    lanes = load_lanes(lanes_path, errors)

    for board in sorted(set(claims) - set(boards)):
        errors.append(
            f"{rel(doc)} states pins for {board_name(board)}, but {rel(config)} has no "
            f"`#if PA_BOARD == {board}` arm declaring any"
        )

    for board, pins in sorted(boards.items()):
        name = board_name(board)
        why = authority_sentence(board, authority, doc)
        vocabulary = row_names.get(board, {})
        covered: set[str] = set()

        for claim in claims.get(board, []):
            where = f"{rel(doc)}:{claim.line} (row {claim.row!r})"
            named = PIN_NAME.findall(claim.row)
            if len(named) == 1:
                target = named[0]
            elif claim.row in vocabulary:
                target = vocabulary[claim.row]
                if target is None:
                    continue  # documented, and not a pin the firmware drives
            else:
                errors.append(
                    f"{name}: {where} states GPIO {claim.gpio} for a row this check cannot join "
                    f"to a {rel(config)} pin. Next move: map the row in ROW_NAMES in "
                    "tools/check_pin_drift.py, or put the PIN_* name in the row"
                )
                continue

            resolved = resolve(pins, target)
            if isinstance(resolved, str):
                errors.append(f"{name}: {where} names {target}, but {resolved}. {why}")
                continue
            pin_name, gpio = resolved
            covered.add(pin_name)
            if gpio != claim.gpio:
                via = f" (the row names {target}, an alias of it)" if target != pin_name else ""
                errors.append(
                    f"{name} {pin_name}: {rel(config)}:{pins[pin_name].line} says {describe(gpio)}, "
                    f"{where} says {describe(claim.gpio)}{via}. {why}"
                )

        for pin in pins.values():
            if PIN_NAME.fullmatch(pin.value):
                continue  # an alias: its target carries the row
            if pin.name in covered:
                continue
            resolved = resolve(pins, pin.name)
            if isinstance(resolved, str):
                errors.append(f"{name}: {rel(config)}:{pin.line}: {resolved}")
            else:
                errors.append(
                    f"{name} {pin.name}: {rel(config)}:{pin.line} drives {describe(resolved[1])}, "
                    f"and no row of {rel(doc)} states it. A builder wiring from the document "
                    f"cannot find this pin. {why}"
                )

        resolved_lanes = [
            lane for lane, tx, rx in lanes
            if not isinstance(resolve(pins, tx), str) and not isinstance(resolve(pins, rx), str)
        ]
        report.lanes[board] = len(resolved_lanes)
        report.compared[board] = len(covered)
        if not resolved_lanes:
            errors.append(
                f"{name}: no lanes reported for this board - {rel(lanes_path)} declares no lane "
                f"whose pins {rel(config)} defines on {name}, so the wiring this firmware reports "
                "for it cannot be checked. This is not a pass"
            )
        if not covered:
            errors.append(
                f"{name}: no pins compared - {rel(doc)} states no row this check could join to "
                f"{name}'s arm of {rel(config)}. This is not a pass"
            )

    if not boards:
        errors.append(f"{rel(config)} declares no PIN_* constant inside any `#if PA_BOARD ==` arm")
    return report


def describe(gpio: int | None) -> str:
    return "unassigned" if gpio is None else f"GPIO {gpio}"


def main(**sources) -> int:
    """`make check-pin-drift`. `sources` are check()'s arguments, for a caller
    pointing it at other files; the command line passes none."""
    report = check(**sources)
    if report.errors:
        print("Pin drift detected:", file=sys.stderr)
        for error in report.errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    per_board = ", ".join(
        f"{board_name(board)}: {count} pins, {report.lanes[board]} lanes"
        for board, count in sorted(report.compared.items())
    )
    print(f"Pin drift check passed ({per_board}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
