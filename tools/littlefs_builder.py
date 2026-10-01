"""The platform's LittleFS image builder, read once for every tool that writes
or prices a filesystem image (#461, #462).

tools/littlefs_image.py writes the image the build ships, and tools/fs_price.py
prices a staged directory without a build. Both must format and fill an image
exactly as the platform's `build_fs_image()` (builder/main.py) does, so the
geometry, the mount handling, the file order and the writer live here, once.

What is read and what is restated. The `LittleFS(...)` call is parsed out of
the installed builder: its `mount` argument is taken from it (the two pins this
project builds disagree), and every other argument must equal
EXPECTED_LITTLEFS, or the caller refuses to write. The writer restates the
rest of build_fs_image - the 4-byte `t` mtime attribute on every entry and the
block count it derives - and image_expression_problems() checks the builder
still does both, so a platform update that changes either is a failure rather
than an image the droid formats differently.

littlefs itself is imported by write_image() at call time, from whichever
interpreter runs the caller: the platform's penv during a build, and the penv
fs_price re-executes under.
"""

import ast
import os
from pathlib import Path

# Arguments of build_fs_image's LittleFS(...) that are the same on both pins.
# A name is an expression in the builder; a literal is the value it passes.
# block_size is the partition's FS_BLOCK, which defaults to 4096, and
# cache_size follows it. disk_version is (major << 16) | minor, default 2.1.
# `mount` is not in this table: the pins disagree, and it is read per pin.
EXPECTED_LITTLEFS = {
    "block_size": "block_size",
    "block_count": "block_count",
    "read_size": 1,
    "prog_size": 1,
    "cache_size": "block_size",
    "lookahead_size": 32,
    "block_cycles": 500,
    "name_max": 64,
    "disk_version": "disk_version",
}

# The rest of build_fs_image that write_image() restates: the block count it
# derives, and the 4-byte little-endian `t` mtime attribute it sets on every
# file and directory (ESP-IDF's LittleFS reads it as the file's mtime).
IMAGE_EXPRESSIONS = (
    "fs_size // block_size",
    "int(item.stat().st_mtime)",
    "fs.setattr(fs_path, 't', mtime.to_bytes(4, 'little'))",
)

ORDERS = ("name", "size")


def _build_fs_image(source):
    """The parsed build_fs_image() FunctionDef, or a problem line."""
    try:
        tree = ast.parse(source)
    except SyntaxError as exc:
        return None, "could not be parsed: %s" % exc
    func = next(
        (node for node in ast.walk(tree)
         if isinstance(node, ast.FunctionDef) and node.name == "build_fs_image"),
        None,
    )
    if func is None:
        return None, "has no build_fs_image()"
    return func, None


def littlefs_call(source):
    """Read build_fs_image's LittleFS(...) call: return (mount, problems).

    `mount` is the call's own LittleFS(mount=...). artoo's 55.03.37 passes
    mount=True and never calls format(); the library formats when that first
    mount fails. The P4 pin passes mount=False and then calls format() and
    mount(). `problems` lists, as readable lines, every way the call differs
    from what write_image() writes; empty when they agree.
    """
    func, problem = _build_fs_image(source)
    if func is None:
        return None, [problem]
    calls = [
        node for node in ast.walk(func)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "LittleFS"
    ]
    if len(calls) != 1:
        return None, ["build_fs_image() makes %d LittleFS() calls" % len(calls)]
    found = {}
    for keyword in calls[0].keywords:
        if keyword.arg is None:
            return None, ["LittleFS() call has a positional argument"]
        value = keyword.value
        if isinstance(value, ast.Constant):
            found[keyword.arg] = value.value
        elif isinstance(value, ast.Name):
            found[keyword.arg] = value.id
        else:
            found[keyword.arg] = ast.unparse(value)
    problems = [
        "LittleFS(%s=%r, expected %r)" % (name, found.get(name, "<absent>"), expected)
        for name, expected in EXPECTED_LITTLEFS.items()
        if found.get(name, "<absent>") != expected
    ]
    problems += [
        "LittleFS(%s=...) is not written by this writer" % name
        for name in sorted(set(found) - set(EXPECTED_LITTLEFS) - {"mount"})
    ]
    mount = found.get("mount", "<absent>")
    body = {ast.unparse(node) for node in ast.walk(func) if isinstance(node, ast.expr)}
    if mount is True:
        if "fs.format()" in body:
            problems.append("LittleFS(mount=True) and build_fs_image() also calls fs.format()")
    elif mount is False:
        problems += [
            "LittleFS(mount=False) but build_fs_image() has no `%s`" % expr
            for expr in ("fs.format()", "fs.mount()")
            if expr not in body
        ]
    else:
        problems.append("LittleFS(mount=%r) is neither True nor False" % (mount,))
    return (mount if mount in (True, False) else None), problems


def image_expression_problems(source):
    """Every IMAGE_EXPRESSIONS line build_fs_image() no longer contains."""
    func, problem = _build_fs_image(source)
    if func is None:
        return [problem]
    body = {ast.unparse(node) for node in ast.walk(func) if isinstance(node, ast.expr)}
    return [
        "build_fs_image() no longer contains `%s`" % expression
        for expression in IMAGE_EXPRESSIONS
        if expression not in body
    ]


def ordered_entries(source, order):
    """Entries of `source` in the order write_image() writes them.

    `name` is a sorted walk: within a directory, files in name order, then
    subdirectories. A flat sort of every relative path writes a subdirectory's
    files before a later file in the parent, and that is a different image.
    `size` is directories by path, then files largest first, ties by path: the
    order tools/littlefs_image.py writes the shipped image in, and the order
    tools/gzip_fsdata.py writes the stage in.
    """
    if order not in ORDERS:
        raise ValueError("order must be one of %s, not %r" % (", ".join(ORDERS), order))
    source = Path(source)
    walked = []
    directories = []
    files = []
    for dirpath, dirnames, filenames in os.walk(source):
        dirnames.sort()
        filenames.sort()
        current = Path(dirpath)
        if current != source:
            walked.append(current)
            directories.append(current)
        for name in filenames:
            path = current / name
            walked.append(path)
            files.append(path)
    if order == "name":
        return walked
    directories.sort(key=lambda path: path.relative_to(source).as_posix())
    files.sort(key=lambda path: (-path.stat().st_size, path.relative_to(source).as_posix()))
    return directories + files


def disk_version(raw):
    """board_build.littlefs_version ("2.1" when unset) as build_fs_image formats
    it: (major << 16) | minor. A value that is not major[.minor] raises
    ValueError; build_fs_image would warn and format 2.1 instead, and a caller
    here refuses rather than image a version nobody asked for."""
    parts = str(raw).split(".")
    try:
        major = int(parts[0])
        minor = int(parts[1]) if len(parts) > 1 else 0
    except ValueError:
        raise ValueError("board_build.littlefs_version %r is not a major.minor" % (raw,))
    return (major << 16) | minor


def geometry(block_size, block_count, disk_version):
    """The LittleFS(...) keyword arguments build_fs_image passes, but `mount`."""
    arguments = dict(EXPECTED_LITTLEFS)
    arguments.update(block_size=block_size, block_count=block_count,
                     cache_size=block_size, disk_version=disk_version)
    return arguments


def write_image(source, block_size, block_count, disk_version, mount, order):
    """Image `source` the way build_fs_image() does, in `order`, and return the
    mounted LittleFS. `mount` is the pin's own LittleFS(mount=...): mounted, the
    library formats the blank buffer when the first mount fails; unmounted, the
    builder formats and mounts a fresh instance, and so does this. Every
    directory and file carries the `t` attribute the builder sets."""
    from littlefs import LittleFS

    source = Path(source)
    fs = LittleFS(mount=mount, **geometry(block_size, block_count, disk_version))
    if not mount:
        fs.format()
        fs.mount()
    for item in ordered_entries(source, order):
        rel = item.relative_to(source).as_posix()
        if item.is_dir():
            fs.makedirs(rel, exist_ok=True)
        else:
            parent = item.relative_to(source).parent
            if parent != Path("."):
                fs.makedirs(parent.as_posix(), exist_ok=True)
            with fs.open(rel, "wb") as dest:
                dest.write(item.read_bytes())
        fs.setattr(rel, "t", int(item.stat().st_mtime).to_bytes(4, "little"))
    return fs
