#!/usr/bin/env python3
"""
PlatformIO post: script - write the LittleFS image in a fixed file order (#461).

The platform builds the filesystem image in build_fs_image() (builder/main.py,
reached as the DataToBin builder for buildfs, uploadfs and uploadfsota), which
adds the stage's files in Path.rglob order: the host filesystem's directory
listing. btrfs lists in creation order, tmpfs in reverse, ext4 - the CI
runner's filesystem - in hash order. The order files go into a LittleFS image
moves its block count, so one stage imaged to different bytes on different
hosts: measured on the 70-file artoo stage, largest first 118 blocks, name
order 120, smallest first 120, and 200 random orders 118 x51, 119 x70, 120 x51,
121 x28. That the order matters was measured; why it packs this way was not
established.

This script writes the image again, from the same stage, largest file first
on every host, with the platform's own LittleFS geometry and `t` attribute, and
replaces the platform's copy before anything reads or uploads it.

Why a post: script and a post-action on the image node. The platform defines
DataToBin after every pre: script has run, and a builder call installs a fresh
executor on its target, dropping any action a pre: script hung on that node
first (SCons Builder._execute -> set_executor; on #461 a pre: script's
action did not run on a verbose buildfs). By the time a post: script runs, the image
node has its real executor, and AddPostAction joins it. Rejected: replacing
the platform's builder (it is bound to the node before any post: script runs);
patching Path.rglob (process-wide, and it changes every other rglob caller);
ordering the stage on disk (ext4's hash order ignores it).

Nothing here touches the image when the image is not being built: a firmware
build has no DataToBin node, and the action is never registered.
"""

import ast
import os
import posixpath

Import("env")  # noqa: F821  (PlatformIO injects this)


def _write_order_key(path, size):
    """The order files go into the image: largest first, ties by path.
    tools/gzip_fsdata.py writes the stage in the same order, so a host that
    lists a directory in creation order shows the stage as it is imaged."""
    return (-size, path)


# The LittleFS geometry the platform's build_fs_image() formats the image with
# (builder/main.py, its LittleFS(...) call), restated because the image is now
# written here and must mount where the platform's would. A call argument that
# is a name, not a literal, is listed by that name. _platform_image_builder()
# reads the platform's call at build time and fails the build on any
# difference, so this table cannot quietly fall behind a platform update.
# `mount` is the one argument read rather than restated: the two platform
# releases this project pins disagree on it (see write_ordered_image()).
PLATFORM_LITTLEFS_CALL = {
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
# The other expressions build_fs_image() is read for: the block count it
# derives, and the 4-byte little-endian `t` mtime attribute it sets on every
# file and directory (ESP-IDF's LittleFS reads it as the file's mtime).
PLATFORM_IMAGE_EXPRESSIONS = (
    "fs_size // block_size",
    "int(item.stat().st_mtime)",
    "fs.setattr(fs_path, 't', mtime.to_bytes(4, 'little'))",
)


def _platform_image_builder(builder_source):
    """Read the platform's build_fs_image(): return (mount, drift), where
    `mount` is the LittleFS(mount=...) it constructs with and `drift` lists
    every difference from what write_ordered_image() assumes, as readable
    lines; empty when they agree."""
    tree = ast.parse(builder_source)
    func = next(
        (node for node in ast.walk(tree)
         if isinstance(node, ast.FunctionDef) and node.name == "build_fs_image"),
        None,
    )
    if func is None:
        return None, ["the platform builder has no build_fs_image()"]
    calls = [
        node for node in ast.walk(func)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "LittleFS"
    ]
    if len(calls) != 1:
        return None, ["build_fs_image() makes %d LittleFS(...) calls, not one" % len(calls)]
    found = {}
    for keyword in calls[0].keywords:
        value = keyword.value
        if isinstance(value, ast.Constant):
            found[keyword.arg] = value.value
        elif isinstance(value, ast.Name):
            found[keyword.arg] = value.id
        else:
            found[keyword.arg] = ast.unparse(value)
    drift = [
        "LittleFS(%s=%r) in the platform, %r here" % (name, found.get(name), expected)
        for name, expected in sorted(PLATFORM_LITTLEFS_CALL.items())
        if found.get(name, "<absent>") != expected
    ]
    drift += [
        "LittleFS(%s=...) in the platform is not written here" % name
        for name in sorted(set(found) - set(PLATFORM_LITTLEFS_CALL) - {"mount"})
    ]
    mount = found.get("mount")
    body = {ast.unparse(node) for node in ast.walk(func) if isinstance(node, ast.expr)}
    expected = list(PLATFORM_IMAGE_EXPRESSIONS)
    if mount is False:
        # Constructed unmounted, the platform formats and mounts explicitly.
        expected += ["fs.format()", "fs.mount()"]
    elif mount is not True:
        drift.append("LittleFS(mount=%r) in the platform is neither True nor False" % (mount,))
    drift += [
        "build_fs_image() no longer contains `%s`" % expression
        for expression in expected
        if expression not in body
    ]
    return mount, drift


def write_ordered_image(stage_dir, fs_size, block_size, disk_version, mount):
    """Return the LittleFS image of `stage_dir`, written in _write_order_key()
    order whatever order the host lists the directory in.

    It is the platform's build_fs_image() with one change: that function
    writes in Path.rglob order, which is the host filesystem's directory
    listing - creation order on btrfs, the reverse on tmpfs, hash order on
    ext4 - so one stage imaged to different bytes, and a different block
    count, on different hosts. Directories are made first, by path; files
    follow in the fixed order. Every directory and file carries the `t`
    attribute the platform sets.

    `mount` is the platform's own LittleFS(mount=...): pioarduino 55.03.37
    (artoo) constructs mounted and lets the library format the blank buffer
    when that first mount fails; 55.03.311 (P4) constructs unmounted, then
    formats and mounts, because the implicit path leaves a superblock some
    targets refuse (its own comment). Each image is written the way its
    platform writes one.
    """
    from littlefs import LittleFS  # the platform's penv provides it at build time

    stage_dir = os.path.abspath(stage_dir)
    dirs = []
    files = []
    for root, dir_names, file_names in os.walk(stage_dir):
        rel_root = os.path.relpath(root, stage_dir)
        for name in dir_names:
            dirs.append(posixpath.normpath(posixpath.join(rel_root.replace(os.sep, "/"), name)))
        for name in file_names:
            full = os.path.join(root, name)
            rel = posixpath.normpath(posixpath.join(rel_root.replace(os.sep, "/"), name))
            files.append((rel, os.path.getsize(full)))

    def mtime(rel):
        return int(os.stat(os.path.join(stage_dir, rel)).st_mtime).to_bytes(4, "little")

    geometry = dict(PLATFORM_LITTLEFS_CALL)
    geometry.update(block_size=block_size, block_count=fs_size // block_size,
                    cache_size=block_size, disk_version=disk_version)
    fs = LittleFS(mount=mount, **geometry)
    if not mount:
        fs.format()
        fs.mount()
    for rel in sorted(dirs):
        fs.makedirs(rel, exist_ok=True)
        fs.setattr(rel, "t", mtime(rel))
    for rel, _size in sorted(files, key=lambda f: _write_order_key(f[0], f[1])):
        with open(os.path.join(stage_dir, rel), "rb") as fh:
            payload = fh.read()
        with fs.open(rel, "wb") as dest:
            dest.write(payload)
        fs.setattr(rel, "t", mtime(rel))
    image = bytes(fs.context.buffer)

    # The image must mount on the droid; mount it here, with the same
    # geometry, and read every file back before it replaces anything.
    from littlefs import UserContext
    check = LittleFS(context=UserContext(buffer=bytearray(image)), mount=True, **geometry)
    for rel, size in files:
        if check.stat(rel).size != size:
            raise SystemExit("[gzip_fsdata] %s reads back from the image at the wrong size." % rel)
    check.unmount()
    return image


def _littlefs_disk_version(env):
    """The disk version build_fs_image() formats with: board_build.littlefs_version
    from this env's section, else [common], else 2.1, as major << 16 | minor."""
    config = env.GetProjectConfig()
    version = "2.1"
    for section in ["env:" + env["PIOENV"], "common"]:
        if config.has_option(section, "board_build.littlefs_version"):
            version = config.get(section, "board_build.littlefs_version")
            break
    major, _, minor = str(version).partition(".")
    try:
        return (int(major) << 16) | int(minor or 0)
    except ValueError:
        raise SystemExit("[gzip_fsdata] board_build.littlefs_version '%s' is not major.minor." % version)


def _rewrite_image_in_order(target, source, env):
    """SCons post-action on the filesystem image: write it again from the
    stage in the fixed order, replacing the platform's host-ordered copy."""
    builder = os.path.join(env.PioPlatform().get_dir(), "builder", "main.py")
    with open(builder, "r", encoding="utf-8") as fh:
        mount, drift = _platform_image_builder(fh.read())
    if drift:
        print("[gzip_fsdata] the platform's filesystem image builder (%s) no longer "
              "matches the ordered writer:\n  %s" % (builder, "\n  ".join(drift)))
        return 1
    image = write_ordered_image(
        str(source[0]), env["FS_SIZE"], env.get("FS_BLOCK", 4096), _littlefs_disk_version(env), mount
    )
    with open(str(target[0]), "wb") as fh:
        fh.write(image)
    return 0


def main():
    if env.subst("$PIOPLATFORM") == "native":
        return
    if env.BoardConfig().get("build.filesystem", "littlefs") != "littlefs":
        return
    # Named the way the platform names it (builder/main.py, ESP32_FS_IMAGE_NAME).
    name = env.get(
        "ESP32_FS_IMAGE_NAME",
        env.get("ESP32_SPIFFS_IMAGE_NAME", env.BoardConfig().get("build.filesystem", "littlefs")),
    )
    image = env.File(os.path.join(env.subst("$BUILD_DIR"), name + ".bin"))
    if not image.has_builder():
        return  # not a filesystem build
    env.AddPostAction(
        image, env.VerboseAction(_rewrite_image_in_order, "Writing $TARGET in a fixed file order")
    )


main()
