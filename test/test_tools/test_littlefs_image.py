#!/usr/bin/env python3
"""The filesystem image is written in one file order on every host (#461).

tools/littlefs_image.py rewrites the image the platform builds (through
tools/littlefs_builder.py, which tools/fs_price.py shares), because the
platform adds files in Path.rglob order - the host filesystem's listing - and
that moved the block count from host to host (118-121 on one stage). These
tests hold the three things the rewrite rests on: the image does not depend on
how the stage is listed, it carries what the platform's image carries (every
file, the `t` mtime attribute, the platform's geometry), and a platform update
that changes that geometry fails the build instead of shipping an image the
droid formats differently.

littlefs-python is the platform's own dependency, installed into its penv
(~/.platformio/penv), not into tools/requirements.txt. The penv is a venv of
the same interpreter, so its site-packages is borrowed when the running Python
lacks the package; with neither, the image tests skip and say why.
"""

import glob
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "littlefs_image.py"
PLATFORM_BUILDERS = sorted(
    glob.glob(os.path.expanduser("~/.platformio*/platforms/espressif32*/builder/main.py"))
)


def _littlefs_available():
    try:
        import littlefs  # noqa: F401

        return True
    except ImportError:
        pass
    version = "python%d.%d" % sys.version_info[:2]
    for penv in sorted(glob.glob(os.path.expanduser("~/.platformio*/penv/lib/%s/site-packages" % version))):
        if os.path.isdir(os.path.join(penv, "littlefs")):
            sys.path.append(penv)
            return True
    return False


class _NativeEnv:
    """Answers only what the script asks before main() returns for a native
    build: where the project's tools are, and the platform."""

    def subst(self, key):
        return {"$PROJECT_DIR": str(ROOT), "$PIOPLATFORM": "native"}[key]


def _load():
    namespace = {"__name__": "littlefs_image_under_test", "__file__": str(SCRIPT)}
    namespace["Import"] = lambda name: namespace.__setitem__("env", _NativeEnv())
    exec(compile(SCRIPT.read_text(encoding="utf-8"), str(SCRIPT), "exec"), namespace)
    return namespace


IMAGE = _load()
FS_SIZE = 64 * 4096
BLOCK = 4096
DISK_2_1 = (2 << 16) | 1


class PlatformBuilderAgreement(unittest.TestCase):
    """The geometry and attribute the writer restates are the platform's."""

    def test_every_installed_platform_builder_matches(self):
        if not PLATFORM_BUILDERS:
            self.skipTest("no espressif32 platform is installed under ~/.platformio*")
        for path in PLATFORM_BUILDERS:
            with self.subTest(path):
                mount, drift = IMAGE["read_platform_builder"](Path(path).read_text(encoding="utf-8"))
                self.assertEqual(drift, [])
                self.assertIn(mount, (True, False))

    def test_a_changed_parameter_or_attribute_is_named(self):
        if not PLATFORM_BUILDERS:
            self.skipTest("no espressif32 platform is installed under ~/.platformio*")
        source = Path(PLATFORM_BUILDERS[0]).read_text(encoding="utf-8")
        changed = source.replace("lookahead_size=32", "lookahead_size=64").replace(
            "mtime.to_bytes(4, 'little')", "mtime.to_bytes(8, 'little')"
        )
        self.assertNotEqual(changed, source)
        _mount, drift = IMAGE["read_platform_builder"](changed)
        self.assertTrue(any("lookahead_size=64" in line for line in drift), drift)
        self.assertTrue(any("'t', mtime.to_bytes(4, 'little')" in line for line in drift), drift)


@unittest.skipUnless(_littlefs_available(), "littlefs-python is in neither this Python nor a platform penv")
class OrderedImage(unittest.TestCase):
    FILES = {
        "big.js.gz": 9000,
        "mid.css.gz": 3000,
        "a.json": 40,
        "b.json": 40,
        "sub/deep.txt": 700,
    }

    def _stage(self, root, reverse=False):
        names = sorted(self.FILES, reverse=reverse)
        for rel in names:
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(bytes([len(rel)]) * self.FILES[rel])
        for index, rel in enumerate(sorted(self.FILES)):
            os.utime(root / rel, (1_700_000_000 + index, 1_700_000_000 + index))
        os.utime(root / "sub", (1_690_000_000, 1_690_000_000))
        return root

    def _image(self, root, mount):
        return IMAGE["write_ordered_image"](str(root), FS_SIZE, BLOCK, DISK_2_1, mount)

    def test_the_image_does_not_depend_on_how_the_stage_is_listed(self):
        real_walk = os.walk

        def reversed_walk(top, *args, **kwargs):
            for root, dirs, files in real_walk(top, *args, **kwargs):
                dirs.reverse()
                yield root, dirs, list(reversed(files))

        with tempfile.TemporaryDirectory() as tmp:
            first = self._stage(Path(tmp) / "first")
            second = self._stage(Path(tmp) / "second", reverse=True)
            for mount in (True, False):
                with self.subTest(mount=mount):
                    image = self._image(first, mount)
                    self.assertEqual(self._image(second, mount), image, "files created in the other order")
                    with mock.patch("os.walk", reversed_walk):
                        self.assertEqual(self._image(first, mount), image, "the stage listed in reverse")

    def test_the_image_carries_every_file_and_its_t_attribute(self):
        from littlefs import LittleFS, UserContext

        with tempfile.TemporaryDirectory() as tmp:
            stage = self._stage(Path(tmp) / "stage")
            image = self._image(stage, True)
            geometry = IMAGE["littlefs_builder"].geometry(BLOCK, FS_SIZE // BLOCK, DISK_2_1)
            fs = LittleFS(context=UserContext(buffer=bytearray(image)), mount=True, **geometry)
            for rel in list(self.FILES) + ["sub"]:
                with self.subTest(rel):
                    mtime = int(os.stat(stage / rel).st_mtime).to_bytes(4, "little")
                    self.assertEqual(fs.getattr(rel, "t"), mtime)
                    if rel in self.FILES:
                        with fs.open(rel, "rb") as fh:
                            self.assertEqual(fh.read(), (stage / rel).read_bytes())
            fs.unmount()


if __name__ == "__main__":
    unittest.main()
