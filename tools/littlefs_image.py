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

import os
import sys

Import("env")  # noqa: F821  (PlatformIO injects this)

# The platform's builder is read, and the image written, by the one module
# tools/fs_price.py prices with too (#462), so the shipped image and its price
# cannot read the platform differently. SCons runs this file with exec(), so
# the tools directory is found from the project, not from __file__.
_TOOLS = os.path.join(env.subst("$PROJECT_DIR"), "tools")
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)
import littlefs_builder  # noqa: E402

# The order the image is written in: tools/fs_price.py --order size, the order
# tools/gzip_fsdata.py writes the stage in.
IMAGE_ORDER = "size"


def read_platform_builder(builder_source):
    """Return (mount, drift) for the platform's build_fs_image(): its own
    LittleFS(mount=...), and every way it differs from what write_ordered_image()
    writes, as readable lines (empty when they agree)."""
    mount, drift = littlefs_builder.littlefs_call(builder_source)
    return mount, drift + littlefs_builder.image_expression_problems(builder_source)


def write_ordered_image(stage_dir, fs_size, block_size, disk_version, mount):
    """Return the LittleFS image of `stage_dir`, written largest file first
    whatever order the host lists the directory in.

    It is the platform's build_fs_image() with one change: that function
    writes in Path.rglob order, which is the host filesystem's directory
    listing - creation order on btrfs, the reverse on tmpfs, hash order on
    ext4 - so one stage imaged to different bytes, and a different block
    count, on different hosts. `mount` is the platform's own LittleFS(mount=...)
    (the two pins differ; tools/littlefs_builder.py write_image()).

    The image must mount on the droid, so it is mounted again here from its
    bytes, with the same geometry, and every file read back at its size before
    it replaces anything.
    """
    from littlefs import LittleFS, UserContext  # the platform's penv provides it at build time

    block_count = fs_size // block_size
    fs = littlefs_builder.write_image(stage_dir, block_size, block_count, disk_version, mount, IMAGE_ORDER)
    image = bytes(fs.context.buffer)
    check = LittleFS(context=UserContext(buffer=bytearray(image)), mount=True,
                     **littlefs_builder.geometry(block_size, block_count, disk_version))
    for entry in littlefs_builder.ordered_entries(stage_dir, IMAGE_ORDER):
        if entry.is_file():
            rel = entry.relative_to(stage_dir).as_posix()
            if check.stat(rel).size != entry.stat().st_size:
                raise SystemExit("[littlefs_image] %s reads back from the image at the wrong size." % rel)
    check.unmount()
    return image


def _littlefs_disk_version(env):
    """The disk version build_fs_image() formats with: board_build.littlefs_version
    from this env's section, else [common], else 2.1."""
    config = env.GetProjectConfig()
    version = "2.1"
    for section in ["env:" + env["PIOENV"], "common"]:
        if config.has_option(section, "board_build.littlefs_version"):
            version = config.get(section, "board_build.littlefs_version")
            break
    try:
        return littlefs_builder.disk_version(version)
    except ValueError as exc:
        raise SystemExit("[littlefs_image] %s" % exc)


def _rewrite_image_in_order(target, source, env):
    """SCons post-action on the filesystem image: write it again from the
    stage in the fixed order, replacing the platform's host-ordered copy."""
    builder = os.path.join(env.PioPlatform().get_dir(), "builder", "main.py")
    with open(builder, "r", encoding="utf-8") as fh:
        mount, drift = read_platform_builder(fh.read())
    if drift:
        print("[littlefs_image] the platform's filesystem image builder (%s) no longer "
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
