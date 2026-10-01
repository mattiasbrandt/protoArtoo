#!/usr/bin/env python3
"""Price a staged LittleFS directory without a firmware build.

`make check-build-budgets` images the tree the build produced and is the
number that can fail a build. This script answers a different question: what
a directory of already-staged files would cost, including the same bytes
written in another order. It does not strip, minify, or recompress. Copy the
stage, change the copy, and compare.

    tools/fs_price.py --env artoo_esp32 --order name .pio/build/artoo_esp32/fsdata_gz
    tools/fs_price.py --env artoo_esp32 --order size .pio/build/artoo_esp32/fsdata_gz
    tools/fs_price.py --env artoo_esp32 --order name --compare stage-a stage-b

`--order` is required. `name` is the order tools/gzip_fsdata.py creates files
in, which is the order the platform builder then walks. `size` writes the
largest file first. A silent sort would match today and lie the day that
creation order changes.

The LittleFS parameters below are a copy of the platform builder's
`build_fs_image` (pioarduino platform-espressif32,
`builder/main.py`, the `LittleFS(...)` call in that function): read_size 1,
prog_size 1, cache_size = block size, lookahead_size 32, block_cycles 500,
name_max 64, disk version 2.1 unless `board_build.littlefs_version` says
otherwise. Both the artoo and the firebeetle2 platform trees carry that same
call. `littlefs` is imported from the PlatformIO environment that builds the
named env. A missing import exits with the path it tried; it does not fall
through to system Python.

Two counters come back, and they are not the same number. `written_blocks`
counts blocks that are not 0xFF, which is what tools/check_build_budgets.py
measures. `lfs_fs_size` is what the firmware's free-space check multiplies by
the block size (`usedBytes()`).
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from check_build_budgets import (  # noqa: E402
    LITTLEFS_BLOCK_SIZE,
    ERASED_BLOCK,
    load_budgets,
    platform_for_env,
)

# Copied from the platform builder's build_fs_image. block_size is the
# partition's FS_BLOCK, which defaults to 4096; cache_size follows it.
# disk_version is (major << 16) | minor, default 2.1.
READ_SIZE = 1
PROG_SIZE = 1
LOOKAHEAD_SIZE = 32
BLOCK_CYCLES = 500
NAME_MAX = 64

_REENC_ENV = "FS_PRICE_REEXEC"


def die(message: str, code: int = 1) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(code)


def ini_sections(text: str) -> dict[str, list[str]]:
    sections: dict[str, list[str]] = {}
    name = None
    body: list[str] = []

    def flush() -> None:
        if name is not None:
            sections[name] = body[:]

    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]") and "=" not in stripped:
            flush()
            name = stripped[1:-1].strip()
            body = []
        elif name is not None:
            body.append(line)
    flush()
    return sections


def section_options(lines: list[str]) -> dict[str, str]:
    options: dict[str, str] = {}
    for raw in lines:
        line = raw.split(";", 1)[0].strip()
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        options[key.strip()] = value.strip()
    return options


def resolved_env(env_name: str) -> dict[str, str]:
    """One environment's options, with `extends` folded in. The child wins."""
    path = ROOT / "platformio.ini"
    if not path.is_file():
        die(f"platformio.ini not found at {path}")
    sections = ini_sections(path.read_text(encoding="utf-8"))
    name = env_name if env_name.startswith("env:") or env_name in sections else f"env:{env_name}"
    if name not in sections and env_name not in sections:
        die(f"no [{name}] section in platformio.ini")
    seen: set[str] = set()
    chain: list[str] = []
    while name and name not in seen:
        if name not in sections:
            die(f"platformio.ini section [{name}] does not exist (extends chain from {env_name})")
        seen.add(name)
        chain.append(name)
        name = section_options(sections[name]).get("extends", "")
    merged: dict[str, str] = {}
    for section in reversed(chain):
        merged.update(section_options(sections[section]))
    return merged


def disk_version_for(options: dict[str, str]) -> int:
    raw = options.get("board_build.littlefs_version", "2.1")
    parts = raw.split(".")
    try:
        major = int(parts[0])
        minor = int(parts[1]) if len(parts) > 1 else 0
    except ValueError:
        die(f"board_build.littlefs_version {raw!r} is not a major.minor")
    return (major << 16) | minor


def partition_blocks(env_name: str) -> tuple[int, Path]:
    """Block count of the env's filesystem partition, and the csv it came from.

    The size is the `spiffs` row of board_build.partitions. Both boards name
    that row spiffs; the filesystem on it is LittleFS. Block size is 4096.
    """
    options = resolved_env(env_name)
    rel = options.get("board_build.partitions")
    if not rel:
        die(f"{env_name} does not set board_build.partitions")
    csv_path = ROOT / rel
    if not csv_path.is_file():
        die(f"partition table not found: {csv_path}")
    size = None
    for raw in csv_path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [part.strip() for part in line.split(",")]
        if len(fields) < 5:
            continue
        if fields[2] == "spiffs":
            size = int(fields[4], 0)
            break
    if size is None:
        die(f"{csv_path} has no spiffs row")
    if size % LITTLEFS_BLOCK_SIZE:
        die(f"{csv_path} spiffs size {size} is not a multiple of {LITTLEFS_BLOCK_SIZE}")
    return size // LITTLEFS_BLOCK_SIZE, csv_path


def penv_python(env_name: str) -> Path:
    """Python for the PlatformIO environment that builds this env.

    littlefs-python is installed there by the platform, not in system Python.
    """
    budgets = load_budgets()
    _key, spec = platform_for_env(env_name, budgets)
    core = Path(os.path.expanduser(spec["core_dir"]))
    return core / "penv" / "bin" / "python"


def ensure_littlefs(env_name: str):
    """Import littlefs from the PlatformIO environment that builds `env_name`.

    System Python is not a fallback. A different littlefs there would price a
    different image from the one `buildfs` writes.
    """
    python = penv_python(env_name)
    # A venv interpreter is often a symlink to the system one, so resolving
    # it makes system Python look like the penv. The venv root is the
    # symlink's parent directory, taken before resolve.
    venv_root = python.parent.parent
    already = Path(sys.prefix).resolve() == venv_root.resolve()
    if os.environ.get(_REENC_ENV) == "1" or already:
        try:
            from littlefs import LittleFS
        except ImportError:
            die(
                "littlefs is not importable from the PlatformIO environment at "
                f"{python}. This script does not install it and does not use system Python."
            )
        return LittleFS
    if not python.is_file():
        die(f"PlatformIO Python was not found at {python}")
    os.environ[_REENC_ENV] = "1"
    os.execv(str(python), [str(python), *sys.argv])


def ordered_entries(source: Path, order: str) -> list[Path]:
    entries = [item for item in source.rglob("*")]
    if order == "name":
        entries.sort(key=lambda item: item.relative_to(source).as_posix())
        return entries
    dirs = [item for item in entries if item.is_dir()]
    files = [item for item in entries if item.is_file()]
    dirs.sort(key=lambda item: item.relative_to(source).as_posix())
    files.sort(key=lambda item: (-item.stat().st_size, item.relative_to(source).as_posix()))
    return dirs + files


def image_directory(source: Path, block_count: int, order: str, disk_version: int) -> tuple[int, int]:
    """Write `source` and return (written non-0xFF blocks, lfs_fs_size).

    Caller has already run ensure_littlefs, so this import is the PlatformIO one.
    """
    from littlefs import LittleFS as LFS

    block = LITTLEFS_BLOCK_SIZE
    fs = LFS(
        block_size=block,
        block_count=block_count,
        read_size=READ_SIZE,
        prog_size=PROG_SIZE,
        cache_size=block,
        lookahead_size=LOOKAHEAD_SIZE,
        block_cycles=BLOCK_CYCLES,
        name_max=NAME_MAX,
        disk_version=disk_version,
        mount=False,
    )
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
    raw = bytes(fs.context.buffer)
    written = sum(
        1
        for index in range(block_count)
        if raw[index * block:(index + 1) * block] != ERASED_BLOCK
    )
    return written, int(fs.used_block_count)


def file_rows(source: Path) -> list[tuple[int, int, int, str]]:
    rows = []
    block = LITTLEFS_BLOCK_SIZE
    for item in sorted(source.rglob("*"), key=lambda path: path.relative_to(source).as_posix()):
        if not item.is_file():
            continue
        size = item.stat().st_size
        rows.append((size, size % block, (size + block - 1) // block, item.relative_to(source).as_posix()))
    return rows


def measure(source: Path, env_name: str, order: str) -> dict:
    if not source.is_dir():
        die(f"not a directory: {source}")
    ensure_littlefs(env_name)
    blocks, csv_path = partition_blocks(env_name)
    version = disk_version_for(resolved_env(env_name))
    written, fs_size = image_directory(source, blocks, order, version)
    rows = file_rows(source)
    return {
        "path": source,
        "env": env_name,
        "order": order,
        "partition_blocks": blocks,
        "partition_csv": csv_path.relative_to(ROOT).as_posix(),
        "block_size": LITTLEFS_BLOCK_SIZE,
        "files": len(rows),
        "content_bytes": sum(row[0] for row in rows),
        "written_blocks": written,
        "lfs_fs_size": fs_size,
        "rows": rows,
    }


def format_report(report: dict) -> str:
    lines = [
        f"path: {report['path']}",
        f"env: {report['env']}",
        f"order: {report['order']}",
        f"partition_csv: {report['partition_csv']}",
        f"partition_blocks: {report['partition_blocks']}",
        f"block_size: {report['block_size']}",
        f"files: {report['files']}",
        f"content_bytes: {report['content_bytes']}",
        f"written_blocks: {report['written_blocks']}  # non-0xFF; tools/check_build_budgets.py",
        f"lfs_fs_size: {report['lfs_fs_size']}  # firmware usedBytes() / block_size",
        "bytes past_block blocks file",
    ]
    for size, past, blocks, name in report["rows"]:
        lines.append(f"{size} {past} {blocks} {name}")
    return "\n".join(lines)


def format_compare(left: dict, right: dict) -> str:
    return "\n".join([
        "=== a ===",
        format_report(left),
        "=== b ===",
        format_report(right),
        "=== delta (b - a) ===",
        f"written_blocks: {right['written_blocks'] - left['written_blocks']}",
        f"lfs_fs_size: {right['lfs_fs_size'] - left['lfs_fs_size']}",
        f"content_bytes: {right['content_bytes'] - left['content_bytes']}",
    ])


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--env", required=True, help="PlatformIO environment, for its partition table")
    parser.add_argument("--order", required=True, choices=("name", "size"), help="name matches gzip_fsdata creation; size is largest file first")
    parser.add_argument("stage", nargs="?", type=Path, help="staged directory to image")
    parser.add_argument("--compare", nargs=2, metavar=("A", "B"), type=Path, help="image two directories and print the difference")
    args = parser.parse_args(argv)
    if args.compare and args.stage:
        parser.error("pass a stage or --compare, not both")
    if not args.compare and args.stage is None:
        parser.error("a stage directory is required")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.compare:
        left = measure(args.compare[0], args.env, args.order)
        right = measure(args.compare[1], args.env, args.order)
        print(format_compare(left, right))
    else:
        print(format_report(measure(args.stage, args.env, args.order)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
