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

`--order` is required. The order files are written into a LittleFS image
changes its block count, and the platform builder writes them in whatever
order the host directory lists.

`name` writes each directory's files in name order before it descends. A
flat sort of every path is a different image: a later file in the parent
would be written after a subdirectory's files. `size` makes every directory
first, by path, then writes files largest first, ties by path. This script
does not read tools/gzip_fsdata.py to choose. Pass the order the image
writer uses; the comment on that writer's loop names which one.

The LittleFS geometry is read from the installed platform builder for the
env's own pin (`builder/main.py`, `build_fs_image`, its `LittleFS(...)`
call). The unversioned `platforms/espressif32` directory is whichever pin
was installed last, so it is not consulted unless its `.piopm` uri is this
env's. The two pins disagree on `mount`: artoo's 55.03.37 constructs mounted
and the library formats the blank image when that mount fails; the P4 pin
constructs unmounted, then formats and mounts. A call whose other arguments
are not the ones this script writes fails the run, with the builder path.
`littlefs` comes from that env's PlatformIO environment. A missing import
exits with the path it tried; it does not fall through to system Python.

Two counters come back, and they are not the same number. `written_blocks`
counts blocks that are not 0xFF, which is what tools/check_build_budgets.py
measures. `lfs_fs_size` is what the firmware's free-space check multiplies by
the block size (`usedBytes()`).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass
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

# The builder's geometry, mount handling, file order and writer are shared with
# tools/littlefs_image.py, which writes the image the build ships, so the price
# and the build cannot read the platform differently.
from littlefs_builder import (  # noqa: E402
    disk_version,
    image_expression_problems,
    littlefs_call,
    ordered_entries,
    write_image,
)

_REENC_ENV = "FS_PRICE_REEXEC"


@dataclass(frozen=True)
class PlatformLittleFS:
    """The installed builder this env's pin actually images with."""

    mount: bool
    version: str
    builder: Path


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
    try:
        return disk_version(options.get("board_build.littlefs_version", "2.1"))
    except ValueError as exc:
        die(str(exc))


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


def platform_dir_for_url(platforms_dir: Path, url: str) -> Path:
    """The installed platform whose `.piopm` uri is `url`.

    PlatformIO keeps one pin at `platforms/espressif32` and detaches the other
    to `espressif32@src-<md5 of its uri>`. The plain directory is whichever
    pin was installed last, so matching the uri is the whole lookup.
    """
    if not platforms_dir.is_dir():
        die(f"PlatformIO platforms directory not found: {platforms_dir}")
    matches = []
    for child in sorted(platforms_dir.iterdir()):
        meta_path = child / ".piopm"
        if not meta_path.is_file():
            continue
        try:
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            die(f"platform metadata is not JSON: {meta_path} ({exc})")
        if meta.get("spec", {}).get("uri") == url:
            matches.append(child)
    if len(matches) == 1:
        return matches[0]
    if not matches:
        die(
            f"no installed platform under {platforms_dir} has uri {url}. "
            "The unversioned espressif32 directory is used only when its uri is this pin."
        )
    joined = ", ".join(str(path) for path in matches)
    die(f"more than one installed platform matches {url}: {joined}")


def littlefs_mount(source: str, origin: str = "the platform builder") -> bool:
    """Return build_fs_image's LittleFS(mount=...), or exit when the call drifted.

    Both pins pass the same geometry and disagree only on mount. artoo's
    55.03.37 passes mount=True and never calls format(); the library formats
    when that first mount fails. The P4 pin passes mount=False and then calls
    format() and mount(). Anything else is a builder this script would image
    differently from, so the run stops. The reading is tools/littlefs_builder.py's.
    """
    mount, problems = littlefs_call(source)
    if problems:
        die(f"{origin} LittleFS call does not match this pricer:\n  " + "\n  ".join(problems))
    return mount


def platform_littlefs(env_name: str) -> PlatformLittleFS:
    """The LittleFS construction the named env's installed pin images with."""
    url = resolved_env(env_name).get("platform", "")
    if not url.startswith(("http://", "https://")):
        die(f"{env_name} platform {url!r} is not a zip pin this script can locate")
    _key, spec = platform_for_env(env_name, load_budgets())
    core = Path(os.path.expanduser(spec["core_dir"]))
    directory = platform_dir_for_url(core / "platforms", url)
    builder = directory / "builder" / "main.py"
    if not builder.is_file():
        die(f"platform builder not found: {builder}")
    try:
        described = json.loads((directory / "platform.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        die(f"platform.json in {directory} could not be read: {exc}")
    version = str(described.get("version", ""))
    if not version:
        die(f"{directory / 'platform.json'} has no version")
    source = builder.read_text(encoding="utf-8")
    mount = littlefs_mount(source, str(builder))
    drift = image_expression_problems(source)
    if drift:
        die(f"{builder} does not match this pricer:\n  " + "\n  ".join(drift))
    return PlatformLittleFS(mount=mount, version=version, builder=builder)


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


def image_directory(source: Path, block_count: int, order: str, disk_version: int, mount: bool) -> tuple[int, int]:
    """Write `source` and return (written non-0xFF blocks, lfs_fs_size).

    Caller has already run ensure_littlefs, so the import inside write_image is
    the PlatformIO one. `mount` is the pin's own LittleFS(mount=...).
    """
    block = LITTLEFS_BLOCK_SIZE
    fs = write_image(source, block, block_count, disk_version, mount, order)
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
    built = platform_littlefs(env_name)
    blocks, csv_path = partition_blocks(env_name)
    version = disk_version_for(resolved_env(env_name))
    written, fs_size = image_directory(source, blocks, order, version, built.mount)
    rows = file_rows(source)
    return {
        "path": source,
        "env": env_name,
        "order": order,
        "platform_version": built.version,
        "littlefs_mount": built.mount,
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
        f"platform_version: {report['platform_version']}",
        f"littlefs_mount: {str(report['littlefs_mount']).lower()}",
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
    parser.add_argument(
        "--order",
        required=True,
        choices=("name", "size"),
        help="name: sorted walk, files before descent; size: directories by path, then largest file first",
    )
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
