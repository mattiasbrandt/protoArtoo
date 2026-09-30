#!/usr/bin/env python3
"""Generate the firmware motion planner and its browser twin from one declaration.

docs/servo-motion.yaml declares how a Servo Output's move is laid out in time
(ADR 0052) - the trapezoid, the triangle, the ramp held to half a throw, the
overshoot aim and the settle back - as typed function bodies in a small subset
of Python syntax. This generator emits them twice:

    docs/servo-motion.yaml
       |-> include/servo_motion_model.h   what ServoTask plans every move with
       '-> data/servo_motion.js           what the Rehearsal times a move with

so the browser runs the device's physics rather than physics of its own (#287
specific 11, #439). The convention is the Droid Parts Catalog's
(tools/generate_droid_parts_catalog.py): one YAML, one generator, committed
outputs stamped "DO NOT EDIT MANUALLY", and tools/check_servo_motion_drift.py
failing when a committed output is not what this generator makes today.

What it refuses, each because the alternative is two outputs that disagree:

  - an assignment or argument whose value is not already the declared type. C
    and JavaScript convert implicitly in different ways, so every conversion is
    a written cast; only value-preserving integer widening is let through.
  - arithmetic mixing f32 with an integer, or an integer literal where an f32
    is wanted. C would convert silently and JavaScript would not round to
    single precision, so the declaration has to say f32() or write 2.0.
  - a construct outside the subset the module docstring of the YAML lists. An
    unknown node is an error, never a best guess.
  - an import the header it names does not declare as a plain literal.

Constants already defined in a firmware header are read out of that header at
generation time. The C++ output includes the header; the browser output gets
the value, and the drift check notices when the header moves.
"""

import argparse
import ast
import hashlib
import io
import re
import struct
import sys
import textwrap
import tokenize
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from registry_yaml import load_registry_yaml

ROOT = Path(__file__).resolve().parent.parent


def rel(path):
    """Repo-relative where the path is in the tree, plain otherwise."""
    try:
        return str(Path(path).resolve().relative_to(ROOT))
    except ValueError:
        return str(path)


DECLARATION_PATH = ROOT / "docs" / "servo-motion.yaml"
FIRMWARE_OUTPUT_PATH = ROOT / "include" / "servo_motion_model.h"
BROWSER_OUTPUT_PATH = ROOT / "data" / "servo_motion.js"

# Where each file BELONGS, as generated text names it, so a run aimed at a
# scratch tree writes byte for byte what a run here writes.
DECLARATION_NAME = "docs/servo-motion.yaml"
GENERATOR_NAME = "tools/generate_servo_motion.py"
FIRMWARE_NAME = "include/servo_motion_model.h"
BROWSER_NAME = "data/servo_motion.js"

STAMP = "DO NOT EDIT MANUALLY"

# The scalar types a declaration may name, and how each language spells it.
CPP_TYPES = {
    "u8": "uint8_t",
    "u16": "uint16_t",
    "u32": "uint32_t",
    "i32": "int32_t",
    "long": "long",
    "f32": "float",
    "bool": "bool",
}
C_TO_DSL = {"uint8_t": "u8", "uint16_t": "u16", "uint32_t": "u32", "int32_t": "i32"}
INT_TYPES = ("u8", "u16", "u32", "i32", "long")
INT_RANGES = {
    "u8": (0, 0xFF),
    "u16": (0, 0xFFFF),
    "u32": (0, 0xFFFFFFFF),
    "i32": (-(2**31), 2**31 - 1),
    "long": (-(2**31), 2**31 - 1),
}
# Value-preserving widenings C and JavaScript agree on without a cast.
WIDENS = {
    "u8": ("u16", "u32", "i32", "long"),
    "u16": ("u32", "i32", "long"),
    "i32": ("long",),
}
LIT = "int-literal"

# Precedence, tighter binds higher. C++ and JavaScript agree for every operator
# the subset admits, which is what lets one table print both.
PREC_ATOM = 20
PREC_UNARY = 15
PREC_MUL = 13
PREC_ADD = 12
PREC_SHIFT = 11
PREC_REL = 10
PREC_EQ = 9
PREC_BITAND = 8
PREC_BITOR = 6
PREC_AND = 5
PREC_OR = 4
PREC_COND = 3

BINOPS = {ast.Add: ("+", PREC_ADD), ast.Sub: ("-", PREC_ADD), ast.Mult: ("*", PREC_MUL),
          ast.Div: ("/", PREC_MUL)}
CMPOPS = {ast.Lt: ("<", "<", PREC_REL), ast.LtE: ("<=", "<=", PREC_REL),
          ast.Gt: (">", ">", PREC_REL), ast.GtE: (">=", ">=", PREC_REL),
          ast.Eq: ("==", "===", PREC_EQ), ast.NotEq: ("!=", "!==", PREC_EQ)}
CASTS = ("f32", "i32", "u16", "u32", "long")


class ModelError(Exception):
    def __init__(self, problems):
        super().__init__("\n".join(problems))
        self.problems = list(problems)


def fail(where, message):
    raise ModelError([f"{where}: {message}"])


# -----------------------------------------------------------------------------
# Imports: constants and enums read out of the firmware headers.
# -----------------------------------------------------------------------------
def read_constant(header_path, name):
    text = Path(header_path).read_text(encoding="utf-8")
    match = re.search(rf"constexpr\s+(\w+)\s+{re.escape(name)}\s*=\s*([^;]+);", text)
    if not match:
        fail(rel(header_path), f"declares no `constexpr ... {name} = ...;`")
    ctype, value = match.group(1), match.group(2).strip()
    if ctype not in C_TO_DSL:
        fail(rel(header_path), f"{name} is a {ctype}, which this generator has no type for")
    literal = re.fullmatch(r"(0x[0-9A-Fa-f]+|\d+)[uU]?", value)
    if not literal:
        fail(rel(header_path), f"{name} = {value} is not a plain literal")
    return {"type": C_TO_DSL[ctype], "value": int(literal.group(1), 0)}


def read_enum(header_path, name):
    text = Path(header_path).read_text(encoding="utf-8")
    match = re.search(rf"enum\s+{re.escape(name)}\s*(?::\s*\w+\s*)?\{{(.*?)\}};", text, re.S)
    if not match:
        fail(rel(header_path), f"declares no `enum {name}`")
    members = {}
    for line in match.group(1).splitlines():
        line = line.split("//", 1)[0].strip().rstrip(",")
        if not line:
            continue
        member = re.fullmatch(r"(\w+)\s*=\s*(\d+)", line)
        if not member:
            fail(rel(header_path), f"enum {name} member `{line}` has no explicit value")
        members[member.group(1)] = int(member.group(2))
    return members


# -----------------------------------------------------------------------------
# The declaration
# -----------------------------------------------------------------------------
def body_comments(source):
    """Every `#` comment in a body, by line, so the outputs keep them."""
    comments = {}
    for token in tokenize.generate_tokens(io.StringIO(source).readline):
        if token.type == tokenize.COMMENT:
            comments[token.start[0]] = token.string[1:].strip()
    return comments


def load_model(path=None, root=None):
    path = Path(path or DECLARATION_PATH)
    root = Path(root or ROOT)
    raw = path.read_bytes()
    doc = load_registry_yaml(io.StringIO(raw.decode("utf-8")))
    if not isinstance(doc, dict) or set(doc) - {"imports", "records", "functions"}:
        fail(rel(path), "top level must be imports, records and functions only")

    model = {"digest": hashlib.sha256(raw).hexdigest(), "constants": {}, "enums": {},
             "headers": {}, "records": {}, "functions": {}}
    for entry in doc.get("imports") or []:
        header = entry.get("header")
        if not header:
            fail(rel(path), f"import {entry} names no header")
        if "constant" in entry:
            model["constants"][entry["constant"]] = dict(
                read_constant(root / header, entry["constant"]), header=header)
            model["headers"][entry["constant"]] = header
        elif "enum" in entry:
            model["enums"][entry["enum"]] = {"members": read_enum(root / header, entry["enum"]),
                                             "header": header}
            model["headers"][entry["enum"]] = header
        else:
            fail(rel(path), f"import {entry} is neither a constant nor an enum")

    for name, record in (doc.get("records") or {}).items():
        fields = []
        for field in record.get("fields") or []:
            if field["type"] not in CPP_TYPES and field["type"] not in model["enums"]:
                fail(f"record {name}", f"field {field['name']} has unknown type {field['type']}")
            fields.append({"name": field["name"], "type": field["type"],
                           "doc": (field.get("doc") or "").strip()})
        model["records"][name] = {"doc": (record.get("doc") or "").strip(), "fields": fields}

    for name, function in (doc.get("functions") or {}).items():
        source = textwrap.dedent(function["body"])
        try:
            tree = ast.parse(source)
        except SyntaxError as error:
            fail(f"function {name}", f"body does not parse: {error}")
        model["functions"][name] = {
            "doc": (function.get("doc") or "").strip(),
            "params": [(p["name"], p["type"]) for p in function.get("params") or []],
            "returns": function["returns"],
            "body": tree.body,
            "source": source,
            "comments": body_comments(source),
        }
    return model


# -----------------------------------------------------------------------------
# Expressions: one walk types a node and prints it in both languages.
# -----------------------------------------------------------------------------
class Expr:
    def __init__(self, type_, cpp, js, cpp_prec=PREC_ATOM, js_prec=PREC_ATOM, value=None):
        self.type = type_
        self.cpp = cpp
        self.js = js
        self.cpp_prec = cpp_prec
        self.js_prec = js_prec
        self.value = value  # an integer literal's value, for range checks


def wrap(text, prec, need):
    return f"({text})" if prec < need else text


def is_int(type_):
    return type_ in INT_TYPES or type_ == LIT


def promote(a, b):
    """C's usual arithmetic conversions, for the integer types admitted."""
    if "long" in (a, b):
        return "long"
    if "u32" in (a, b):
        return "u32"
    return "i32"


def assignable(target, expr):
    if expr.type == target:
        return True
    if expr.type == LIT and target in INT_RANGES:
        low, high = INT_RANGES[target]
        return expr.value is not None and low <= expr.value <= high
    return target in WIDENS.get(expr.type, ())


class Scope:
    def __init__(self, model, function_name):
        self.model = model
        self.where = f"function {function_name}"
        self.locals = {}

    def name(self, node):
        ident = node.id
        if ident in self.locals:
            return Expr(self.locals[ident], ident, ident)
        if ident in self.model["constants"]:
            return Expr(self.model["constants"][ident]["type"], ident, ident)
        for enum, info in self.model["enums"].items():
            if ident in info["members"]:
                return Expr(enum, ident, ident)
        if ident in ("True", "False"):
            return Expr("bool", ident.lower(), ident.lower())
        fail(self.where, f"`{ident}` is not a local, a parameter or an import")

    def expr(self, node):
        if isinstance(node, ast.Constant):
            if isinstance(node.value, bool):
                text = "true" if node.value else "false"
                return Expr("bool", text, text)
            if isinstance(node.value, int):
                text = ast.get_source_segment(self.source, node) or str(node.value)
                return Expr(LIT, text, text, value=node.value)
            if isinstance(node.value, float):
                text = ast.get_source_segment(self.source, node) or repr(node.value)
                single = struct.unpack("f", struct.pack("f", node.value))[0]
                js = text if single == node.value else f"f32({text})"
                return Expr("f32", text + "f", js)
            fail(self.where, f"literal {node.value!r} is not a number")
        if isinstance(node, ast.Name):
            return self.name(node)
        if isinstance(node, ast.Attribute):
            base = self.expr(node.value)
            record = self.model["records"].get(base.type)
            if record is None:
                fail(self.where, f"`{ast.unparse(node)}` reads a field of a {base.type}")
            field = next((f for f in record["fields"] if f["name"] == node.attr), None)
            if field is None:
                fail(self.where, f"{base.type} has no field {node.attr}")
            return Expr(field["type"], f"{base.cpp}.{node.attr}", f"{base.js}.{node.attr}")
        if isinstance(node, ast.UnaryOp):
            operand = self.expr(node.operand)
            if isinstance(node.op, ast.Not):
                if operand.type != "bool":
                    fail(self.where, f"`not` of a {operand.type}")
                return Expr("bool", "!" + wrap(operand.cpp, operand.cpp_prec, PREC_UNARY),
                            "!" + wrap(operand.js, operand.js_prec, PREC_UNARY),
                            PREC_UNARY, PREC_UNARY)
            if isinstance(node.op, ast.USub):
                if operand.type == "f32":
                    type_ = "f32"
                elif operand.type == LIT:
                    type_ = LIT
                elif is_int(operand.type):
                    type_ = promote(operand.type, "i32")
                else:
                    fail(self.where, f"negating a {operand.type}")
                value = -operand.value if operand.value is not None else None
                return Expr(type_, "-" + wrap(operand.cpp, operand.cpp_prec, PREC_UNARY),
                            "-" + wrap(operand.js, operand.js_prec, PREC_UNARY),
                            PREC_UNARY, PREC_UNARY, value)
            fail(self.where, f"unary {type(node.op).__name__} is not in the subset")
        if isinstance(node, ast.BinOp):
            return self.binop(node)
        if isinstance(node, ast.Compare):
            return self.compare(node)
        if isinstance(node, ast.BoolOp):
            values = [self.expr(v) for v in node.values]
            if any(v.type != "bool" for v in values):
                fail(self.where, f"`{ast.unparse(node)}` joins something that is not a bool")
            prec = PREC_AND if isinstance(node.op, ast.And) else PREC_OR
            op = " && " if prec == PREC_AND else " || "
            # An `and` inside an `or` is parenthesised although it binds tighter:
            # it is what a reader expects, and -Wparentheses asks for it.
            need = PREC_AND + 1
            return Expr("bool", op.join(wrap(v.cpp, v.cpp_prec, need) for v in values),
                        op.join(wrap(v.js, v.js_prec, need) for v in values), prec, prec)
        if isinstance(node, ast.IfExp):
            return self.ifexp(node)
        if isinstance(node, ast.Call):
            return self.call(node)
        fail(self.where, f"`{ast.unparse(node)}` is not in the subset")

    def binop(self, node):
        if type(node.op) not in BINOPS:
            fail(self.where, f"operator in `{ast.unparse(node)}` is not in the subset")
        symbol, prec = BINOPS[type(node.op)]
        left, right = self.expr(node.left), self.expr(node.right)
        cpp = (f"{wrap(left.cpp, left.cpp_prec, prec)} {symbol} "
               f"{wrap(right.cpp, right.cpp_prec, prec + 1)}")
        js_inner = (f"{wrap(left.js, left.js_prec, prec)} {symbol} "
                    f"{wrap(right.js, right.js_prec, prec + 1)}")
        if left.type == "f32" and right.type == "f32":
            # Single precision in both: C computes it, JavaScript rounds to it.
            return Expr("f32", cpp, f"f32({js_inner})", prec, PREC_ATOM)
        if not (is_int(left.type) and is_int(right.type)):
            fail(self.where, f"`{ast.unparse(node)}` mixes {left.type} and {right.type}; "
                             "write the cast")
        if left.type == LIT and right.type == LIT:
            fail(self.where, f"`{ast.unparse(node)}` is arithmetic on two literals; fold it")
        type_ = promote(left.type, right.type)
        if symbol == "/":
            return Expr(type_, cpp, f"trunc({js_inner})", prec, PREC_ATOM)
        if type_ == "u32":
            return Expr(type_, cpp, f"({js_inner}) >>> 0", prec, PREC_SHIFT)
        return Expr(type_, cpp, js_inner, prec, prec)

    def compare(self, node):
        if len(node.ops) != 1:
            fail(self.where, f"`{ast.unparse(node)}` chains comparisons")
        left, right = self.expr(node.left), self.expr(node.comparators[0])
        cpp_op, js_op, prec = CMPOPS[type(node.ops[0])]
        numeric = is_int(left.type) and is_int(right.type)
        floats = left.type == "f32" and right.type == "f32"
        same = left.type == right.type and left.type != LIT
        if not (numeric or floats or same):
            fail(self.where, f"`{ast.unparse(node)}` compares {left.type} with {right.type}")
        return Expr("bool",
                    f"{wrap(left.cpp, left.cpp_prec, prec)} {cpp_op} "
                    f"{wrap(right.cpp, right.cpp_prec, prec + 1)}",
                    f"{wrap(left.js, left.js_prec, prec)} {js_op} "
                    f"{wrap(right.js, right.js_prec, prec + 1)}", prec, prec)

    def ifexp(self, node):
        test, body, orelse = self.expr(node.test), self.expr(node.body), self.expr(node.orelse)
        if test.type != "bool":
            fail(self.where, f"`{ast.unparse(node.test)}` is not a bool")
        if body.type == orelse.type:
            type_ = body.type
        elif body.type == LIT and assignable(orelse.type, body):
            type_ = orelse.type
        elif orelse.type == LIT and assignable(body.type, orelse):
            type_ = body.type
        elif is_int(body.type) and is_int(orelse.type):
            type_ = promote(body.type, orelse.type)
        else:
            fail(self.where, f"`{ast.unparse(node)}` has a {body.type} and a {orelse.type} arm")
        need = PREC_COND + 1
        return Expr(type_,
                    f"{wrap(test.cpp, test.cpp_prec, need)} ? {wrap(body.cpp, body.cpp_prec, need)}"
                    f" : {wrap(orelse.cpp, orelse.cpp_prec, need)}",
                    f"{wrap(test.js, test.js_prec, need)} ? {wrap(body.js, body.js_prec, need)}"
                    f" : {wrap(orelse.js, orelse.js_prec, need)}",
                    PREC_COND, PREC_COND)

    def call(self, node):
        if not isinstance(node.func, ast.Name) or node.keywords:
            fail(self.where, f"`{ast.unparse(node)}` is not a call the subset knows")
        callee = node.func.id
        args = [self.expr(a) for a in node.args]
        if callee in CASTS:
            if len(args) != 1:
                fail(self.where, f"{callee}() takes one value")
            return self.cast(callee, args[0], node)
        if callee == "sqrtf":
            if len(args) != 1 or args[0].type != "f32":
                fail(self.where, "sqrtf() takes one f32")
            return Expr("f32", f"sqrtf({args[0].cpp})", f"sqrtf({args[0].js})")
        if callee == "lroundf":
            if len(args) != 1 or args[0].type != "f32":
                fail(self.where, "lroundf() takes one f32")
            return Expr("long", f"lroundf({args[0].cpp})", f"lroundf({args[0].js})")
        function = self.model["functions"].get(callee)
        if function is None:
            fail(self.where, f"`{callee}()` is not declared")
        if len(args) != len(function["params"]):
            fail(self.where, f"{callee}() takes {len(function['params'])} values")
        for arg, (pname, ptype) in zip(args, function["params"]):
            if not assignable(ptype, arg):
                fail(self.where, f"{callee}() wants a {ptype} for {pname}, given a {arg.type}")
        return Expr(function["returns"], f"{callee}({', '.join(a.cpp for a in args)})",
                    f"{callee}({', '.join(a.js for a in args)})")

    def cast(self, callee, arg, node):
        cpp_type = CPP_TYPES[callee]
        cpp = f"({cpp_type}){wrap(arg.cpp, arg.cpp_prec, PREC_UNARY)}"
        if callee == "f32":
            if arg.type == "f32":
                fail(self.where, f"`{ast.unparse(node)}` casts an f32 to itself")
            if not is_int(arg.type):
                fail(self.where, f"`{ast.unparse(node)}` casts a {arg.type} to f32")
            return Expr("f32", cpp, f"f32({arg.js})", PREC_UNARY, PREC_ATOM)
        if not is_int(arg.type):
            fail(self.where, f"`{ast.unparse(node)}` casts a {arg.type} to {callee}; "
                             "an f32 becomes an integer through lroundf()")
        value = arg.value
        if callee == "u16":
            if arg.type == LIT and 0 <= arg.value <= 0xFFFF or arg.type in ("u8", "u16"):
                return Expr("u16", cpp, arg.js, PREC_UNARY, arg.js_prec, value)
            return Expr("u16", cpp, f"{wrap(arg.js, arg.js_prec, PREC_ATOM)} & 0xFFFF",
                        PREC_UNARY, PREC_BITAND)
        if callee == "u32":
            if arg.type in ("u8", "u16", "u32") or arg.type == LIT and arg.value >= 0:
                return Expr("u32", cpp, arg.js, PREC_UNARY, arg.js_prec, value)
            return Expr("u32", cpp, f"{wrap(arg.js, arg.js_prec, PREC_ATOM)} >>> 0",
                        PREC_UNARY, PREC_SHIFT)
        if callee == "i32":
            if arg.type in ("u8", "u16", "i32", LIT):
                return Expr("i32", cpp, arg.js, PREC_UNARY, arg.js_prec, value)
            return Expr("i32", cpp, f"{wrap(arg.js, arg.js_prec, PREC_ATOM)} | 0",
                        PREC_UNARY, PREC_BITOR)
        # long: every integer the subset carries fits.
        return Expr("long", cpp, arg.js, PREC_UNARY, arg.js_prec, value)


# -----------------------------------------------------------------------------
# Statements
# -----------------------------------------------------------------------------
def reassigned_names(body):
    names = set()
    for node in ast.walk(ast.Module(body=body, type_ignores=[])):
        if isinstance(node, ast.Assign):
            for target in node.targets:
                while isinstance(target, ast.Attribute):
                    target = target.value
                if isinstance(target, ast.Name):
                    names.add(target.id)
    return names


def record_literal(model, name):
    fields = ", ".join(
        f"{f['name']}: {'false' if f['type'] == 'bool' else '0'}"
        for f in model["records"][name]["fields"])
    return "{ " + fields + " }"


class FunctionWriter:
    def __init__(self, model, name):
        self.model = model
        self.name = name
        self.function = model["functions"][name]
        self.scope = Scope(model, name)
        self.scope.source = ""
        self.mutable = reassigned_names(self.function["body"])
        self.params = {pname for pname, _ in self.function["params"]}
        self.cpp = []
        self.js = []
        self.last_line = 0

    def comments_before(self, line, depth):
        pending = sorted(n for n in self.function["comments"] if self.last_line < n < line)
        # A blank line in the declaration is a blank line in both outputs.
        lines = self.function["source"].splitlines()
        if self.last_line and any(not lines[n - 1].strip() for n in range(self.last_line + 1, line)):
            self.cpp.append("")
            self.js.append("")
        for number in pending:
            text = self.function["comments"][number]
            self.cpp.append("    " * depth + f"// {text}".rstrip())
            self.js.append("  " * depth + f"// {text}".rstrip())

    def block(self, statements, depth):
        for statement in statements:
            self.statement(statement, depth)

    def statement(self, node, depth):
        self.comments_before(node.lineno, depth)
        self.last_line = node.lineno
        cpp_pad, js_pad = "    " * depth, "  " * depth
        scope = self.scope
        if isinstance(node, ast.AnnAssign):
            if not isinstance(node.target, ast.Name) or not isinstance(node.annotation, ast.Name):
                fail(scope.where, f"`{ast.unparse(node)}` declares something other than a local")
            local, type_ = node.target.id, node.annotation.id
            if local in scope.locals or local in self.params:
                fail(scope.where, f"`{local}` is declared twice")
            constant = local not in self.mutable
            if type_ in self.model["records"]:
                value = node.value
                if isinstance(value, ast.Call) and isinstance(value.func, ast.Name) \
                        and value.func.id == type_ and not value.args:
                    cpp_value, js_value = "{}", record_literal(self.model, type_)
                else:
                    expr = scope.expr(value)
                    if expr.type != type_:
                        fail(scope.where, f"`{local}` is a {type_}, given a {expr.type}")
                    cpp_value = expr.cpp
                    # A record copied from another is a copy in both languages.
                    js_value = f"{{ ...{expr.js} }}" if isinstance(value, ast.Name) else expr.js
                cpp_decl = f"{'const ' if constant else ''}{type_} {local}"
            else:
                if type_ not in CPP_TYPES:
                    fail(scope.where, f"`{local}` has unknown type {type_}")
                expr = scope.expr(node.value)
                if not assignable(type_, expr):
                    fail(scope.where, f"`{local}` is a {type_}, given a {expr.type}; write the cast")
                cpp_value, js_value = expr.cpp, expr.js
                cpp_decl = f"{'const ' if constant else ''}{CPP_TYPES[type_]} {local}"
            scope.locals[local] = type_
            self.cpp.append(f"{cpp_pad}{cpp_decl} = {cpp_value};")
            self.js.append(f"{js_pad}{'const' if constant or type_ in self.model['records'] else 'let'}"
                           f" {local} = {js_value};")
            return
        if isinstance(node, ast.Assign):
            if len(node.targets) != 1:
                fail(scope.where, f"`{ast.unparse(node)}` assigns more than once")
            target = node.targets[0]
            if isinstance(target, ast.Name) and target.id in self.params:
                fail(scope.where, f"parameter `{target.id}` is assigned")
            if isinstance(target, ast.Attribute) and isinstance(target.value, ast.Name) \
                    and target.value.id in self.params:
                fail(scope.where, f"parameter `{target.value.id}` is modified")
            left = scope.expr(target)
            if left.type in self.model["records"]:
                fail(scope.where, f"`{ast.unparse(node)}` replaces a whole record")
            value = scope.expr(node.value)
            if not assignable(left.type, value):
                fail(scope.where, f"`{ast.unparse(target)}` is a {left.type}, given a "
                                  f"{value.type}; write the cast")
            self.cpp.append(f"{cpp_pad}{left.cpp} = {value.cpp};")
            self.js.append(f"{js_pad}{left.js} = {value.js};")
            return
        if isinstance(node, ast.Return):
            value = scope.expr(node.value)
            if not assignable(self.function["returns"], value):
                fail(scope.where, f"returns a {value.type}, declared {self.function['returns']}")
            self.cpp.append(f"{cpp_pad}return {value.cpp};")
            self.js.append(f"{js_pad}return {value.js};")
            return
        if isinstance(node, ast.If):
            self.if_chain(node, depth, "if")
            return
        fail(scope.where, f"`{ast.unparse(node)}` is not a statement the subset knows")

    def if_chain(self, node, depth, keyword):
        cpp_pad, js_pad = "    " * depth, "  " * depth
        test = self.scope.expr(node.test)
        if test.type != "bool":
            fail(self.scope.where, f"`{ast.unparse(node.test)}` is not a bool")
        opener_cpp = f"if ({test.cpp}) {{"
        opener_js = f"if ({test.js}) {{"
        if keyword == "if":
            self.cpp.append(cpp_pad + opener_cpp)
            self.js.append(js_pad + opener_js)
        else:
            self.cpp[-1] += " else " + opener_cpp
            self.js[-1] += " else " + opener_js
        self.block(node.body, depth + 1)
        self.cpp.append(cpp_pad + "}")
        self.js.append(js_pad + "}")
        if len(node.orelse) == 1 and isinstance(node.orelse[0], ast.If):
            self.if_chain(node.orelse[0], depth, "else if")
        elif node.orelse:
            self.cpp[-1] += " else {"
            self.js[-1] += " else {"
            self.block(node.orelse, depth + 1)
            self.cpp.append(cpp_pad + "}")
            self.js.append(js_pad + "}")

    def write(self, source):
        self.scope.source = source
        for pname, ptype in self.function["params"]:
            self.scope.locals[pname] = ptype
        body = self.function["body"]
        if not body or not isinstance(body[-1], ast.Return):
            fail(self.scope.where, "does not end with a return")
        self.block(body, 1)
        return self.cpp, self.js


def cpp_signature(model, name):
    function = model["functions"][name]
    params = []
    for pname, ptype in function["params"]:
        if ptype in model["records"]:
            params.append(f"const {ptype}& {pname}")
        else:
            params.append(f"{CPP_TYPES.get(ptype, ptype)} {pname}")
    returns = function["returns"]
    return f"inline {CPP_TYPES.get(returns, returns)} {name}({', '.join(params)})"


# -----------------------------------------------------------------------------
# Output
# -----------------------------------------------------------------------------
RULE = "-" * 77


def comment_block(prefix, lines):
    return [f"{prefix} {line}".rstrip() if line else prefix for line in lines]


def header_banner(prefix, title, lines):
    out = [f"{prefix} {RULE}", f"{prefix} {title}"]
    out += comment_block(prefix, lines)
    out.append(f"{prefix} {RULE}")
    return out


def render_bodies(model):
    return {name: FunctionWriter(model, name).write(function["source"])
            for name, function in model["functions"].items()}


def provenance(model, prefix, path_name, what):
    return [
        f"{prefix} {'=' * 77}",
        f"{prefix} {path_name}",
        prefix,
        f"{prefix} Auto-generated from {DECLARATION_NAME} by {GENERATOR_NAME}",
        f"{prefix} {STAMP}",
        prefix,
        f"{prefix} Source digest: sha256 {model['digest']}",
        prefix,
    ] + comment_block(prefix, what) + [f"{prefix} {'=' * 77}"]


def render_header(model, bodies):
    lines = provenance(model, "//", FIRMWARE_NAME, [
        "How a Servo Output's move is laid out in time from its Motion Profile",
        "(ADR 0052). ServoTask plans every move with the functions below, and the",
        f"browser runs the same declaration, generated into {BROWSER_NAME}, to",
        "time a move before it is sent (#287 specific 11, #439). To change the",
        f"model, edit {DECLARATION_NAME} and run the generator; the prose that",
        "explains the model as a whole is include/servo_motion_ramp.h's.",
    ])
    lines += ["#pragma once", "", "#include <math.h>", "#include <stdint.h>", ""]
    headers = sorted({h for h in model["headers"].values()
                      if any(h == model["headers"].get(n) for n in used_imports(model))})
    for header in headers:
        names = [n for n in used_imports(model) if model["headers"].get(n) == header]
        include = header.split("include/", 1)[1] if header.startswith("include/") else header
        lines.append(f'#include "{include}"  // {", ".join(names)}')
    for name, record in model["records"].items():
        lines += [""] + header_banner("//", name, record["doc"].splitlines())
        lines.append(f"struct {name} {{")
        members = [f"{CPP_TYPES.get(f['type'], f['type'])} {f['name']};" for f in record["fields"]]
        width = max(len(m) for m in members)
        for field, member in zip(record["fields"], members):
            doc = field["doc"].splitlines()
            if len(doc) > 1:
                lines += comment_block("    //", doc)
                lines.append(f"    {member}")
            elif doc:
                lines.append(f"    {member:<{width}}  // {doc[0]}")
            else:
                lines.append(f"    {member}")
        lines.append("};")
    for name, function in model["functions"].items():
        lines += [""] + header_banner("//", f"{name}()", function["doc"].splitlines())
        lines.append(cpp_signature(model, name) + " {")
        lines += bodies[name][0]
        lines.append("}")
    return "\n".join(lines) + "\n"


def used_imports(model):
    """The imports a record or a body names -- the ones C++ needs a header for."""
    used = []
    names = set()
    for function in model["functions"].values():
        for node in ast.walk(ast.Module(body=function["body"], type_ignores=[])):
            if isinstance(node, ast.Name):
                names.add(node.id)
    for record in model["records"].values():
        names.update(f["type"] for f in record["fields"])
    for enum, info in model["enums"].items():
        if enum in names or names & set(info["members"]):
            used.append(enum)
    used += [c for c in model["constants"] if c in names]
    return used


JS_HELPERS = """\
  // Single precision, as the firmware computes: Math.fround() rounds a double to
  // the nearest float, which is exact for + - * / and for sqrt of a float.
  const f32 = Math.fround;
  const sqrtf = (x) => f32(Math.sqrt(x));
  // The C library's lroundf(): half away from zero. x is already a float, so
  // x + 0.5 is exact in a double and floor() does the rounding.
  const lroundf = (x) => (x < 0 ? -Math.floor(-x + 0.5) : Math.floor(x + 0.5));
  // C integer division truncates towards zero.
  const trunc = Math.trunc;"""


def render_browser(model, bodies):
    lines = provenance(model, "//", BROWSER_NAME, [
        "The Servo Output motion model, as ServoTask runs it: the functions below",
        f"are generated from the same declaration as {FIRMWARE_NAME}, so",
        "a move timed here is the move the droid plans (#287 specific 11, #439).",
        "Constants the firmware headers define are read from those headers when",
        "this is generated, and say where they came from.",
    ])
    lines += ["", "(() => {", "  \"use strict\";", "", JS_HELPERS, ""]
    for name, info in model["constants"].items():
        lines.append(f"  const {name} = {info['value']};  // {info['header']}")
    for enum, info in model["enums"].items():
        for member, value in info["members"].items():
            lines.append(f"  const {member} = {value};  // {enum}, {info['header']}")
    for name, function in model["functions"].items():
        lines += [""] + header_banner("  //", f"{name}()", function["doc"].splitlines())
        params = ", ".join(pname for pname, _ in function["params"])
        lines.append(f"  function {name}({params}) {{")
        lines += ["  " + line if line else "" for line in bodies[name][1]]
        lines.append("  }")
    exports = [f"    {name}," for name in model["constants"]]
    for enum, info in model["enums"].items():
        members = ", ".join(f"{m}" for m in info["members"])
        exports.append(f"    {enum}: Object.freeze({{ {members} }}),")
    exports += [f"    {name}," for name in model["functions"]]
    lines += [
        "",
        "  window.ServoMotion = Object.freeze({",
        f'    source: "{DECLARATION_NAME}",',
        f'    generator: "{GENERATOR_NAME}",',
        f'    sourceSha256: "{model["digest"]}",',
    ] + exports + ["  });", "})();"]
    return "\n".join(lines) + "\n"


def render(declaration_path=None, root=None):
    """Both outputs as text, by path name. Writes nothing."""
    declaration_path = Path(declaration_path or DECLARATION_PATH)
    model = load_model(declaration_path, root)
    bodies = render_bodies(model)
    return {
        FIRMWARE_NAME: render_header(model, bodies),
        BROWSER_NAME: render_browser(model, bodies),
    }


def generate(quiet=False, declaration_path=None, root=None, firmware_path=None,
             browser_path=None):
    outputs = render(declaration_path, root)
    targets = {FIRMWARE_NAME: Path(firmware_path or FIRMWARE_OUTPUT_PATH),
               BROWSER_NAME: Path(browser_path or BROWSER_OUTPUT_PATH)}
    for name, text in outputs.items():
        targets[name].write_text(text, encoding="utf-8")
        if not quiet:
            print(f"Generated {rel(targets[name])}")
    return outputs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--quiet", action="store_true", help="write without the summary")
    args = parser.parse_args(argv)
    try:
        generate(quiet=args.quiet)
    except ModelError as error:
        print(f"{DECLARATION_NAME} cannot be generated:", file=sys.stderr)
        for problem in error.problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
