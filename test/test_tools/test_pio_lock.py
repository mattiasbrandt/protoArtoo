"""Pinned behavior for the machine-wide PlatformIO build lock.

tools/pio_lock.py is what makes "one build at a time" a mechanism instead of a
habit, so the two ways it can fail silently are worth pinning: joining the
queue behind a lock it already holds (the nested-flock deadlock, which hangs
forever and has no error to read), and suppressing the lock when it should
have taken it (two builds, one plausible wrong answer). Every test drives the
real script against a lock file in a temp dir — never /tmp/protor2-pio.lock,
which other agents are building under right now.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import pio_lock  # noqa: E402

PIO_LOCK = str(Path(pio_lock.__file__).resolve())
HAS_FLOCK = shutil.which("flock") is not None


def child_env(lock: Path, **overrides: str) -> dict[str, str]:
    """A clean environment for a child: nothing the ambient session set.

    The gate itself may be run inside the escape hatch, and inheriting
    PROTOR2_PIO_LOCK_HELD would quietly turn these tests into no-ops.
    PLATFORMIO_CORE_DIR points at the lock's own temp dir, which has no penv,
    so the penv check passes vacuously instead of reading this machine's.
    """
    env = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(("PROTOR2_PIO_LOCK", "PROTOR2_LOCK_OWNER"))
    }
    env[pio_lock.LOCK_PATH_ENV] = str(lock)
    env["PLATFORMIO_CORE_DIR"] = str(lock.parent)
    env.update(overrides)
    return env


def fake_penv(core: Path, version: str, scons: str) -> None:
    """A penv with just the two files the check reads, as PlatformIO lays them out."""
    (core / "penv").mkdir(parents=True)
    (core / "penv" / "pyvenv.cfg").write_text("home = /usr/bin\nversion_info = 3.14.7\n")
    pkg = core / "penv" / "lib" / "python3.14" / "site-packages" / "platformio"
    pkg.mkdir(parents=True)
    (pkg / "__init__.py").write_text(f"VERSION = ({version})\n")
    (pkg / "dependencies.py").write_text(
        "def get_core_dependencies():\n    return {\n"
        f'        "tool-scons": {scons},\n    }}\n'
    )


class LockPathHelpers(unittest.TestCase):
    def test_target_comes_from_the_pio_environment_flag(self):
        self.assertEqual(pio_lock.build_target(["pio", "run", "-e", "protoArtoo"]), "protoArtoo")
        self.assertEqual(pio_lock.build_target(["pio", "test", "--environment=native"]), "native")

    def test_target_without_an_env_flag_is_not_invented(self):
        # `pio run -t clean` names no env; BUILD_ENV is only set when exported.
        self.assertEqual(pio_lock.build_target(["pio", "run", "-t", "clean"]), "-")


class BuildLockBehavior(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="pio-lock-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.lock = self.tmp / "pio.lock"

    def wait_for_record(self, timeout=10.0):
        """Block until the holder has stamped the lock file, or fail the test."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.lock.exists() and self.lock.read_text().strip():
                return
            time.sleep(0.02)
        self.fail(f"no ownership record appeared in {self.lock} within {timeout}s")

    def run_locked(self, command, env=None, timeout=30):
        return subprocess.run(
            [sys.executable, PIO_LOCK, *command],
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env or child_env(self.lock),
        )

    def test_plain_run_takes_the_lock_and_records_the_holder(self):
        proc = self.run_locked(["true", "-e", "protoArtoo"])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        record = self.lock.read_text()
        self.assertIn("target: protoArtoo", record)
        self.assertIn("command: true -e protoArtoo", record)
        self.assertRegex(record, r"pid: \d+")
        self.assertRegex(record, r"worktree: /\S")
        self.assertRegex(record, r"branch: \S")

    def test_the_record_outlives_the_build_that_wrote_it(self):
        # The in-process form tools/slice_verify.py uses. The stale record is
        # the useful part - it is what tells the next agent which target last
        # touched the shared framework pools - so release must not clear it.
        with mock.patch.dict(os.environ, child_env(self.lock), clear=True):
            with pio_lock.build_lock(["pio", "run", "-e", "protoArtoo"]):
                self.assertIn("target: protoArtoo", self.lock.read_text())
        self.assertIn("target: protoArtoo", self.lock.read_text())

    @unittest.skipUnless(HAS_FLOCK, "flock(1) required to build the nested case")
    def test_nesting_under_an_outer_flock_fails_loudly_instead_of_hanging(self):
        # The old convention, typed verbatim: `flock <lockfile> make build`.
        # Without the guard the inner acquire blocks forever, so the subprocess
        # timeout below is itself part of the assertion.
        proc = subprocess.run(
            ["flock", str(self.lock), sys.executable, PIO_LOCK, "true"],
            capture_output=True,
            text=True,
            timeout=30,
            env=child_env(self.lock, PROTOR2_PIO_LOCK_WAIT="600"),
        )
        self.assertEqual(proc.returncode, pio_lock.EXIT_NESTED, proc.stderr)
        self.assertIn("refusing to nest", proc.stderr)
        self.assertIn(f"{pio_lock.HELD_ENV}=1", proc.stderr)

    @unittest.skipUnless(HAS_FLOCK, "flock(1) required to build the nested case")
    def test_the_escape_hatch_suppresses_the_inner_lock(self):
        proc = subprocess.run(
            ["flock", str(self.lock), sys.executable, PIO_LOCK, "true", "-e", "native"],
            capture_output=True,
            text=True,
            timeout=30,
            env=child_env(self.lock, PROTOR2_PIO_LOCK_HELD="1"),
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        # Suppressed, but still the holder as far as the next agent is
        # concerned: an outer flock(1) cannot write a record of its own.
        self.assertIn("target: native", self.lock.read_text())

    def test_a_waiter_is_told_who_it_waited_on_before_giving_up(self):
        holder = subprocess.Popen(
            [sys.executable, PIO_LOCK, sys.executable, "-c", "import time; time.sleep(3)"],
            stderr=subprocess.DEVNULL,
            env=child_env(self.lock, PROTOR2_LOCK_OWNER="the other worker"),
        )
        self.addCleanup(holder.wait)
        self.addCleanup(holder.terminate)
        self.wait_for_record()
        proc = self.run_locked(
            ["true"], env=child_env(self.lock, PROTOR2_PIO_LOCK_WAIT="1")
        )
        self.assertEqual(proc.returncode, pio_lock.EXIT_TIMEOUT, proc.stderr)
        self.assertIn("gave up waiting", proc.stderr)
        self.assertIn("owner: the other worker", proc.stderr)

    def test_two_builds_serialise_instead_of_overlapping(self):
        marker = self.tmp / "order.txt"
        script = (
            "import sys, time, pathlib\n"
            "p = pathlib.Path(sys.argv[1])\n"
            "with p.open('a') as fh: fh.write(sys.argv[2] + '-in\\n')\n"
            "time.sleep(0.4)\n"
            "with p.open('a') as fh: fh.write(sys.argv[2] + '-out\\n')\n"
        )
        procs = [
            subprocess.Popen(
                [sys.executable, PIO_LOCK, sys.executable, "-c", script, str(marker), name],
                stderr=subprocess.DEVNULL,
                env=child_env(self.lock),
            )
            for name in ("A", "B")
        ]
        for proc in procs:
            self.assertEqual(proc.wait(timeout=30), 0)
        order = " ".join(marker.read_text().split())
        # Whichever won, its window closes before the other one opens.
        self.assertIn(order, ("A-in A-out B-in B-out", "B-in B-out A-in A-out"))


class PenvPreflight(unittest.TestCase):
    """The penv on the wrong Core fails before the lock, with the fix printed."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="pio-lock-penv-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.lock = self.tmp / "pio.lock"

    def run_locked(self):
        return subprocess.run(
            [sys.executable, PIO_LOCK, "true", "-e", "artoo_esp32"],
            capture_output=True, text=True, timeout=30, env=child_env(self.lock),
        )

    def test_a_self_upgraded_penv_is_refused_with_the_fix(self):
        fake_penv(self.tmp, "6, 2, 0", '"~4.41100.0"')
        proc = self.run_locked()
        self.assertEqual(proc.returncode, pio_lock.EXIT_PENV, proc.stderr)
        self.assertIn("holds PlatformIO Core 6.2.0", proc.stderr)
        self.assertIn(f"{self.tmp}/penv/bin/uv pip install --python {self.tmp}/penv/bin/python "
                      "--reinstall-package pioarduino pioarduino==6.1.19", proc.stderr)
        # Refused before the lock: the next agent's record is not ours.
        self.assertFalse(self.lock.exists() and self.lock.read_text().strip())

    def test_both_spellings_of_scons_481_pass(self):
        for scons in ('"~4.40801.0"',
                      '(\n "https://github.com/pioarduino/scons/releases/"\n'
                      ' "download/4.8.1/scons-local-4.8.1.tar.gz"\n )'):
            with self.subTest(scons=scons):
                shutil.rmtree(self.tmp / "penv", ignore_errors=True)
                fake_penv(self.tmp, "6, 1, 19", scons)
                self.assertIsNone(pio_lock.penv_problem(self.tmp))

    def test_a_core_pinning_another_scons_is_refused(self):
        fake_penv(self.tmp, "6, 1, 19", '"~4.41100.0"')
        self.assertIn("pins tool-scons", pio_lock.penv_problem(self.tmp))

    def test_a_core_dir_with_no_penv_yet_is_not_a_fault(self):
        self.assertIsNone(pio_lock.penv_problem(self.tmp))

    def test_the_build_env_decides_the_core_dir_not_the_callers_shell(self):
        # slice_verify hands the core dir to the child only; a different one
        # exported in the caller's shell must not pick which penv is checked.
        fake_penv(self.tmp, "6, 2, 0", '"~4.41100.0"')
        healthy = self.tmp / "healthy"
        healthy.mkdir()
        with mock.patch.dict(os.environ, child_env(self.lock, PLATFORMIO_CORE_DIR=str(healthy)),
                             clear=True):
            with self.assertRaises(SystemExit) as raised:
                with pio_lock.build_lock(["pio", "run", "-e", "artoo_esp32"],
                                         env={"PLATFORMIO_CORE_DIR": str(self.tmp)}):
                    pass
        self.assertEqual(raised.exception.code, pio_lock.EXIT_PENV)

    def test_the_core_dir_follows_the_registry_when_nothing_is_exported(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            p4 = pio_lock.core_dir_for("firebeetle2")
            artoo = pio_lock.core_dir_for("artoo_esp32")
        self.assertEqual(p4.name, ".platformio-p4")
        self.assertEqual(artoo.name, ".platformio")


class PoolRecord(unittest.TestCase):
    """The record says which shared framework pool a build touched, and whether it moved."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="pio-lock-pool-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.lock = self.tmp / "pio.lock"
        self.pool = self.tmp / "libs" / "esp32"
        self.pool.mkdir(parents=True)
        (self.pool / "sdkconfig").write_text("CONFIG_X=y\n")
        self.worktree = self.tmp / "wt"
        self.worktree.mkdir()
        import check_framework_envelope as envelope
        patches = [
            mock.patch.object(envelope, "resolved_sdkconfig_path",
                              return_value=self.pool / "sdkconfig"),
            mock.patch.object(pio_lock, "_git", side_effect=lambda args: (
                str(self.worktree) if args[-1] == "--show-toplevel" else "chore/x")),
            mock.patch.dict(os.environ, child_env(self.lock), clear=True),
        ]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)

    def build(self, env_name="artoo_esp32"):
        with pio_lock.build_lock(["pio", "run", "-e", env_name]):
            pass
        return self.lock.read_text()

    def test_a_worktree_without_the_stamp_is_told_it_rebuilds_the_pool(self):
        record = self.build()
        self.assertIn(f"pool: {self.pool}", record)
        self.assertIn("pool_rebuild: expected", record)

    def test_a_stamped_worktree_records_the_pool_without_predicting_a_rebuild(self):
        (self.worktree / "sdkconfig.defaults").write_text("# TASMOTA__0\n")
        record = self.build()
        self.assertIn(f"pool: {self.pool}", record)
        self.assertNotIn("pool_rebuild", record)

    def test_the_next_holder_says_the_pool_moved_under_the_last_build(self):
        self.build()
        os.utime(self.pool / "sdkconfig", (1, 1))  # what a rebuild does to it
        record = self.build()
        self.assertIn(f"previous_build_rebuilt_pool: {self.pool} ({self.worktree} building artoo_esp32)",
                      record)

    def test_an_unmoved_pool_says_nothing(self):
        self.build()
        self.assertNotIn("previous_build_rebuilt_pool", self.build())

    def test_a_target_with_no_overrides_records_no_pool(self):
        self.assertNotIn("pool:", self.build("native"))


if __name__ == "__main__":
    unittest.main()
