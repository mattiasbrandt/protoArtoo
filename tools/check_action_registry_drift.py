#!/usr/bin/env python3
"""Check that action metadata stays aligned across YAML, C++, and JS."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import sys

import yaml


ROOT = Path(__file__).resolve().parents[1]
REGISTRY_PATH = ROOT / "docs" / "action-registry.yaml"
RC_MAPPING_PATH = ROOT / "include" / "rc_mapping.h"
RC_ACTION_TYPES_PATH = ROOT / "include" / "rc_action_types.h"
ACTION_REGISTRY_PATH = ROOT / "src" / "web" / "action_registry.cpp"
RC_JS_PATH = ROOT / "data" / "rc.js"
WEB_DIR = ROOT / "src" / "web"
DOME_CUE_HANDLER_PATH = ROOT / "src" / "drivers" / "dome_cue_handler.cpp"
AUDIO_DOLLAR_PARSER_PATH = ROOT / "src" / "tasks" / "audio_dollar_parser.cpp"
BINDABLE_CPP_FILE = "include/rc_mapping.h"

DOMAIN_GROUP = {
    "drive": "Movement",
    "servo": "Arms",
    "dome": "Sequences",
    "sound": "Sound",
    "system": "System",
    "aux": "Aux",
}

ACTION_GROUP_OVERRIDE = {
    "system.action.set-mode": "Mode",
    "system.action.estop": "Safety",
    "dome.action.marcduino-command": "Command",
    "dome.action.set-speed": "Movement",
}

NON_TESTABLE_TOKENS = {"drive_speed", "drive_steer", "dome_speed", "estop"}
PAYLOAD_REQUIRED_TOKENS = {"seq", "cmd", "dome_seq"}


@dataclass(frozen=True)
class ExpectedAction:
    enum: str
    token: str
    name: str
    display_name: str
    domain: str
    description: str
    safety_critical: bool
    group: str
    testable: bool


def normalize(text: object) -> str:
    return " ".join(str(text if text is not None else "").split())


def runtime_field(entry: dict, field: str) -> str:
    return normalize(entry.get(f"runtime_{field}", entry.get(field)))


def action_group(entry: dict) -> str:
    name = entry.get("name")
    if name in ACTION_GROUP_OVERRIDE:
        return ACTION_GROUP_OVERRIDE[name]
    return DOMAIN_GROUP.get(entry.get("domain"), "Other")


def action_testable(token: str) -> bool:
    return token not in NON_TESTABLE_TOKENS and token not in PAYLOAD_REQUIRED_TOKENS


def robot_action_enum_order() -> dict[str, int]:
    # RobotActionId enum is defined in rc_action_types.h (split from rc_mapping.h)
    # Try to read from rc_action_types.h first, fall back to rc_mapping.h for compatibility
    text = None
    if RC_ACTION_TYPES_PATH.exists():
        text = RC_ACTION_TYPES_PATH.read_text(encoding="utf-8")
    else:
        text = RC_MAPPING_PATH.read_text(encoding="utf-8")

    match = re.search(r"enum\s+RobotActionId\s*:[^{]+{(?P<body>.*?)\n};", text, re.S)
    if not match:
        raise ValueError("could not find RobotActionId enum")

    order: dict[str, int] = {}
    for raw_line in match.group("body").splitlines():
        line = raw_line.split("//", 1)[0].strip()
        if not line:
            continue
        enum_name = line.split("=", 1)[0].split(",", 1)[0].strip()
        if enum_name:
            order[enum_name] = len(order)
    return order


def load_registry_doc() -> dict:
    with REGISTRY_PATH.open("r", encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def load_expected_actions(doc: dict) -> list[ExpectedAction]:
    enum_order = robot_action_enum_order()
    expected: list[ExpectedAction] = []
    seen_enums: set[str] = set()
    seen_tokens: set[str] = set()

    for entry in doc.get("entries", []):
        if entry.get("cpp_file") != BINDABLE_CPP_FILE or not entry.get("cpp_enum"):
            continue

        enum = entry["cpp_enum"]
        token = entry.get("rc_token")
        if not token:
            raise ValueError(f"{entry['name']} is bindable but has no rc_token")
        if enum not in enum_order:
            raise ValueError(f"{entry['name']} references unknown RobotActionId {enum}")
        if enum in seen_enums:
            raise ValueError(f"duplicate cpp_enum in YAML: {enum}")
        if token in seen_tokens:
            raise ValueError(f"duplicate rc_token in YAML: {token}")
        seen_enums.add(enum)
        seen_tokens.add(token)

        expected.append(
            ExpectedAction(
                enum=enum,
                token=token,
                name=normalize(entry["name"]),
                display_name=runtime_field(entry, "display_name"),
                domain=normalize(entry["domain"]),
                description=runtime_field(entry, "description"),
                safety_critical=bool(entry.get("safety_critical")),
                group=action_group(entry),
                testable=action_testable(token),
            )
        )

    expected.sort(key=lambda action: enum_order[action.enum])
    return expected


def parse_to_string_tokens() -> dict[str, str]:
    # robotActionIdToString is now in rc_action_types.h (split from rc_mapping.h)
    text = None
    if RC_ACTION_TYPES_PATH.exists():
        text = RC_ACTION_TYPES_PATH.read_text(encoding="utf-8")
    else:
        text = RC_MAPPING_PATH.read_text(encoding="utf-8")

    body = re.search(
        r"robotActionIdToString\(RobotActionId target\).*?switch \(target\) {(?P<body>.*?)\n    }",
        text,
        re.S,
    )
    if not body:
        raise ValueError("could not find robotActionIdToString switch")
    return {
        enum: token
        for enum, token in re.findall(
            r"case\s+([A-Z0-9_]+):\s*\n\s*return\s+\"([^\"]+)\";",
            body.group("body"),
        )
    }


def parse_from_string_tokens() -> dict[str, str]:
    # parseRobotActionId is now in rc_action_types.h (split from rc_mapping.h)
    text = None
    if RC_ACTION_TYPES_PATH.exists():
        text = RC_ACTION_TYPES_PATH.read_text(encoding="utf-8")
    else:
        text = RC_MAPPING_PATH.read_text(encoding="utf-8")

    body = re.search(
        r"parseRobotActionId\(const char\* raw, RobotActionId\* out\).*?(?P<body>if \(strcmp.*?)\n    return false;",
        text,
        re.S,
    )
    if not body:
        raise ValueError("could not find parseRobotActionId body")
    return {
        token: enum
        for token, enum in re.findall(
            r"strcmp\(raw,\s*\"([^\"]+)\"\).*?\*out\s*=\s*([A-Z0-9_]+);",
            body.group("body"),
            re.S,
        )
    }


def parse_action_registry() -> dict[str, tuple[str, str, str, str, bool]]:
    text = ACTION_REGISTRY_PATH.read_text(encoding="utf-8")
    rows = re.findall(
        r"{\s*([A-Z0-9_]+),\s*\"([^\"]+)\",\s*\"([^\"]+)\",\s*\"([^\"]+)\",\s*\"([^\"]+)\",\s*(true|false)\s*}",
        text,
    )
    return {
        enum: (normalize(name), normalize(display), normalize(domain), normalize(desc), safety == "true")
        for enum, name, display, domain, desc, safety in rows
    }


def parse_js_fallback() -> dict[str, tuple[str, str, str, bool, bool]]:
    text = RC_JS_PATH.read_text(encoding="utf-8")
    match = re.search(r"const HARDCODED_ACTION_TARGETS = \[(?P<body>.*?)\n  \];", text, re.S)
    if not match:
        raise ValueError("could not find HARDCODED_ACTION_TARGETS")

    rows: dict[str, tuple[str, str, str, bool, bool]] = {}
    for row in re.findall(r"{(?P<row>[^{}]+)}", match.group("body")):
        fields = {}
        for key, single_quoted, double_quoted, boolean in re.findall(
            r"(\w+):\s*(?:'([^']*)'|\"([^\"]*)\"|(true|false))",
            row,
        ):
            fields[key] = single_quoted or double_quoted or boolean
        token = fields.get("token")
        if not token:
            continue
        rows[token] = (
            normalize(fields.get("label", "")),
            normalize(fields.get("group", "")),
            normalize(fields.get("description", "")),
            fields.get("testable") == "true",
            fields.get("safetyCritical") == "true",
        )
    return rows


def find_registered_routes() -> set[str]:
    """All literal paths passed to server.on(...) across src/web/*.cpp."""
    routes: set[str] = set()
    for path in sorted(WEB_DIR.glob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        for match in re.finditer(r'server\.on\(\s*"([^"]+)"', text):
            routes.add(match.group(1))
    return routes


def find_documented_api_paths(doc: dict) -> set[str]:
    return {
        entry["api_path"]
        for entry in doc.get("entries", [])
        if entry.get("api_path")
    }


def check_api_endpoints(doc: dict, errors: list[str]) -> None:
    """Bidirectional check: every server.on(...) route <-> every registry api_path.

    Covers the full API surface (not just RC-bindable actions), so a new
    endpoint added in src/web/*.cpp without a matching registry entry — or a
    registry entry left behind after a route is removed/renamed — is caught.
    """
    routes = find_registered_routes()
    documented = find_documented_api_paths(doc)
    for route in sorted(routes - documented):
        errors.append(f"{route} registered via server.on(...) but missing from the registry (api_path)")
    for path in sorted(documented - routes):
        errors.append(f"{path} documented as api_path but no matching server.on(...) route exists")


def find_dome_cues() -> set[str]:
    """BD:<CUE> names handled by dome_cue_handler.cpp's strcmp(cue, "...") chain."""
    text = DOME_CUE_HANDLER_PATH.read_text(encoding="utf-8")
    return set(re.findall(r'strcmp\(cue,\s*"([A-Z_]+)"\)', text))


def find_documented_cues(doc: dict) -> set[str]:
    cues: set[str] = set()
    for entry in doc.get("entries", []):
        cmd = entry.get("marcduino_cmd")
        if isinstance(cmd, str) and cmd.startswith("BD:"):
            cues.add(cmd[3:])
    return cues


def check_dome_cues(doc: dict, errors: list[str]) -> None:
    """Bidirectional check: dome_cue_handler.cpp cues <-> registry BD:<CUE> entries."""
    implemented = find_dome_cues()
    documented = find_documented_cues(doc)
    for cue in sorted(implemented - documented):
        errors.append(f"BD:{cue} handled in dome_cue_handler.cpp but missing from the registry (marcduino_cmd)")
    for cue in sorted(documented - implemented):
        errors.append(f"BD:{cue} documented in the registry but no longer handled in dome_cue_handler.cpp")


def find_dollar_tokens() -> set[str]:
    """Single-char $ tokens handled by audio_dollar_parser.cpp's switch, plus $nnn."""
    text = AUDIO_DOLLAR_PARSER_PATH.read_text(encoding="utf-8")
    match = re.search(r"switch \(\*arg\) \{(?P<body>.*?)\n    \}", text, re.S)
    if not match:
        raise ValueError("could not find dollar-command switch in audio_dollar_parser.cpp")
    tokens = set(re.findall(r"case '(.)':", match.group("body")))
    tokens.add("nnn")  # numeric $nnn branch, handled before the switch
    return tokens


def find_documented_dollar_tokens(doc: dict) -> set[str]:
    tokens: set[str] = set()
    for entry in doc.get("entries", []):
        cmd = entry.get("marcduino_cmd")
        # `$...` is the generic raw-passthrough entry (any dollar string), not a
        # specific token — exclude it from the per-token diff.
        if isinstance(cmd, str) and cmd.startswith("$") and cmd != "$...":
            tokens.add(cmd[1:])
    return tokens


def check_dollar_commands(doc: dict, errors: list[str]) -> None:
    """Bidirectional check: audio_dollar_parser.cpp tokens <-> registry $<token> entries."""
    implemented = find_dollar_tokens()
    documented = find_documented_dollar_tokens(doc)
    for token in sorted(implemented - documented):
        errors.append(f"${token} handled in audio_dollar_parser.cpp but missing from the registry (marcduino_cmd)")
    for token in sorted(documented - implemented):
        errors.append(f"${token} documented in the registry but no longer handled in audio_dollar_parser.cpp")


def add_mismatch(errors: list[str], label: str, expected: object, actual: object) -> None:
    if expected != actual:
        errors.append(f"{label}: expected {expected!r}, got {actual!r}")


def main() -> int:
    errors: list[str] = []
    doc = load_registry_doc()
    expected = load_expected_actions(doc)
    expected_by_enum = {action.enum: action for action in expected}
    expected_by_token = {action.token: action for action in expected}

    to_string = parse_to_string_tokens()
    from_string = parse_from_string_tokens()
    registry = parse_action_registry()
    js_fallback = parse_js_fallback()

    for action in expected:
        add_mismatch(errors, f"{action.enum} robotActionIdToString", action.token, to_string.get(action.enum))
        add_mismatch(errors, f"{action.token} parseRobotActionId", action.enum, from_string.get(action.token))

        row = registry.get(action.enum)
        if row is None:
            errors.append(f"{action.enum} missing from ACTION_REGISTRY")
        else:
            add_mismatch(errors, f"{action.enum} registry name", action.name, row[0])
            add_mismatch(errors, f"{action.enum} registry display_name", action.display_name, row[1])
            add_mismatch(errors, f"{action.enum} registry domain", action.domain, row[2])
            add_mismatch(errors, f"{action.enum} registry description", action.description, row[3])
            add_mismatch(errors, f"{action.enum} registry safety_critical", action.safety_critical, row[4])

        js = js_fallback.get(action.token)
        if js is None:
            errors.append(f"{action.token} missing from HARDCODED_ACTION_TARGETS")
        else:
            add_mismatch(errors, f"{action.token} JS label", action.display_name, js[0])
            add_mismatch(errors, f"{action.token} JS group", action.group, js[1])
            add_mismatch(errors, f"{action.token} JS description", action.description, js[2])
            add_mismatch(errors, f"{action.token} JS testable", action.testable, js[3])
            add_mismatch(errors, f"{action.token} JS safetyCritical", action.safety_critical, js[4])

    for enum in sorted(set(registry) - set(expected_by_enum)):
        errors.append(f"{enum} appears in ACTION_REGISTRY but not bindable YAML")
    for enum in sorted(set(to_string) - set(expected_by_enum) - {"ROBOT_ACTION_NONE"}):
        errors.append(f"{enum} appears in robotActionIdToString but not bindable YAML")
    for token in sorted(set(js_fallback) - set(expected_by_token)):
        errors.append(f"{token} appears in HARDCODED_ACTION_TARGETS but not bindable YAML")

    # Full-registry checks (not limited to RC-bindable actions): every API
    # endpoint, BD:<CUE> dome event, and $<token> dollar command implemented
    # in firmware must have a registry entry, and vice versa.
    check_api_endpoints(doc, errors)
    check_dome_cues(doc, errors)
    check_dollar_commands(doc, errors)

    if errors:
        print("Action registry drift detected:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(f"Action registry drift check passed "
          f"({len(expected)} bindable actions, "
          f"{len(find_registered_routes())} API endpoints, "
          f"{len(find_dome_cues())} dome cues, "
          f"{len(find_dollar_tokens())} dollar tokens).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
