#!/usr/bin/env python3
"""tools/fs_price.py images a staged directory and requires an explicit order.

The fixture is a handful of files. It does not pin the artoo web image's
block count: that number moves whenever data/ does. The budget gate remains
make check-build-budgets.
"""

import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import fs_price  # noqa: E402

SCRIPT = ROOT / "tools" / "fs_price.py"


class PartitionResolution(unittest.TestCase):
    def test_artoo_and_firebeetle_read_their_own_tables(self):
        artoo, artoo_csv = fs_price.partition_blocks("artoo_esp32")
        p4, p4_csv = fs_price.partition_blocks("firebeetle2")
        self.assertEqual(artoo, 160)
        self.assertEqual(artoo_csv.name, "partitions_ota.csv")
        self.assertEqual(p4, 0x9E0000 // 4096)
        self.assertEqual(p4_csv.name, "partitions_16mb_ota.csv")

    def test_missing_order_fails(self):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--env", "artoo_esp32", "anywhere"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("--order", result.stderr)


class WriteOrder(unittest.TestCase):
    def test_name_is_the_sorted_walk_and_size_is_largest_first(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "a.txt").write_bytes(b"a")
            (root / "z.txt").write_bytes(b"z" * 50)
            (root / "m").mkdir()
            (root / "m" / "c.txt").write_bytes(b"c" * 10)
            name = [path.relative_to(root).as_posix() for path in fs_price.ordered_entries(root, "name")]
            size = [path.relative_to(root).as_posix() for path in fs_price.ordered_entries(root, "size")]
        # A flat path sort would write m/c.txt before z.txt. The stager does not.
        self.assertEqual(name, ["a.txt", "z.txt", "m", "m/c.txt"])
        self.assertEqual(size, ["m", "z.txt", "m/c.txt", "a.txt"])


def _builder(mount, *, formatted, read_size=1):
    tail = "    fs.format()\n    fs.mount()\n" if formatted else ""
    return f"""
def other():
    fs = LittleFS(mount=False)
def build_fs_image(target, source, env):
    fs = LittleFS(
        block_size=block_size,
        block_count=block_count,
        read_size={read_size},
        prog_size=1,
        cache_size=block_size,
        lookahead_size=32,
        block_cycles=500,
        name_max=64,
        disk_version=disk_version,
        mount={mount},
    )
{tail}
"""


class BuilderCall(unittest.TestCase):
    def test_mounted_and_unmounted_calls(self):
        self.assertTrue(fs_price.littlefs_mount(_builder(True, formatted=False)))
        self.assertFalse(fs_price.littlefs_mount(_builder(False, formatted=True)))

    def test_refuses_a_call_that_drifted(self):
        cases = {
            "read_size": _builder(True, formatted=False, read_size=16),
            "mounted and formatted": _builder(True, formatted=True),
            "unmounted without format": _builder(False, formatted=False),
        }
        for label, source in cases.items():
            with self.subTest(label=label):
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        fs_price.littlefs_mount(source)

    def test_uri_selects_the_detached_pin(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plain = root / "espressif32"
            detached = root / "espressif32@src-abc"
            plain.mkdir()
            detached.mkdir()
            (plain / ".piopm").write_text(json.dumps({"spec": {"uri": "https://example.test/new.zip"}}))
            (detached / ".piopm").write_text(json.dumps({"spec": {"uri": "https://example.test/old.zip"}}))
            found = fs_price.platform_dir_for_url(root, "https://example.test/old.zip")
            self.assertEqual(found, detached)
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    fs_price.platform_dir_for_url(root, "https://example.test/missing.zip")

    def test_installed_pins_disagree_on_mount(self):
        try:
            artoo = fs_price.platform_littlefs("artoo_esp32")
            p4 = fs_price.platform_littlefs("firebeetle2")
        except SystemExit as exc:
            self.skipTest(str(exc))
        self.assertEqual(artoo.version, "55.03.37")
        self.assertTrue(artoo.mount)
        self.assertEqual(p4.version, "55.03.311")
        self.assertFalse(p4.mount)


class ImageFixture(unittest.TestCase):
    def setUp(self):
        python = fs_price.penv_python("artoo_esp32")
        if not python.is_file():
            self.skipTest(f"PlatformIO Python not found at {python}")
        probe = subprocess.run(
            [str(python), "-c", "import littlefs"],
            capture_output=True,
            text=True,
        )
        if probe.returncode != 0:
            self.skipTest(f"littlefs is not importable from {python}")
        try:
            fs_price.platform_littlefs("artoo_esp32")
            fs_price.platform_littlefs("firebeetle2")
        except SystemExit as exc:
            self.skipTest(str(exc))

    def test_both_orders_image_and_compare(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp) / "stage"
            stage.mkdir()
            (stage / "a.txt").write_bytes(b"a" * 100)
            (stage / "b.txt").write_bytes(b"b" * 5000)
            (stage / "nested").mkdir()
            (stage / "nested" / "c.txt").write_bytes(b"c" * 10)
            other = Path(tmp) / "other"
            other.mkdir()
            (other / "only.txt").write_bytes(b"z" * 20)

            name = self._run(stage, "name")
            size = self._run(stage, "size")
            for report in (name, size):
                self.assertIn("written_blocks:", report)
                self.assertIn("non-0xFF; tools/check_build_budgets.py", report)
                self.assertIn("lfs_fs_size:", report)
                self.assertIn("firmware usedBytes()", report)
                self.assertIn("partition_blocks: 160", report)
                self.assertIn("100 ", report)
                self.assertIn("b.txt", report)
                self.assertIn("nested/c.txt", report)
            self.assertIn("order: name", name)
            self.assertIn("order: size", size)
            self.assertIn("platform_version: 55.03.37", name)
            self.assertIn("littlefs_mount: true", name)

            compared = subprocess.run(
                [sys.executable, str(SCRIPT), "--env", "artoo_esp32", "--order", "name",
                 "--compare", str(stage), str(other)],
                capture_output=True,
                text=True,
                cwd=ROOT,
            )
            self.assertEqual(compared.returncode, 0, compared.stderr)
            self.assertIn("=== delta (b - a) ===", compared.stdout)
            self.assertIn("written_blocks:", compared.stdout.split("=== delta")[1])

            p4 = self._run(stage, "name", env="firebeetle2")
            self.assertIn(f"partition_blocks: {0x9E0000 // 4096}", p4)
            self.assertIn("platform_version: 55.03.311", p4)
            self.assertIn("littlefs_mount: false", p4)

    def _run(self, stage: Path, order: str, env: str = "artoo_esp32") -> str:
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--env", env, "--order", order, str(stage)],
            capture_output=True,
            text=True,
            cwd=ROOT,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout


if __name__ == "__main__":
    unittest.main()
