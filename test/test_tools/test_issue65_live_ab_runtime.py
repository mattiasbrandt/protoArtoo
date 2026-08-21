import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace


TOOLS_DIR = Path(__file__).parents[2] / "tools"
MODULE_PATH = TOOLS_DIR / "issue65_live_ab_runtime.py"
sys.path.insert(0, str(TOOLS_DIR))
SPEC = importlib.util.spec_from_file_location("issue65_live_ab_runtime", MODULE_PATH)
RUNTIME = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = RUNTIME
SPEC.loader.exec_module(RUNTIME)


class PingLossStopTest(unittest.TestCase):
    """ICMP is the least reliable liveness probe here, so it needs corroboration.

    The regression case is `test_single_dropped_echo_does_not_stop`: one lost
    reply used to end a run as "Unexpected controller failure" while HTTP kept
    serving and serial showed no panic or reset.
    """

    def _stop(self, **kwargs):
        base = dict(armed=True, success=False, loss_duration=99.0, status_recent=False)
        base.update(kwargs)
        return RUNTIME.should_stop_on_ping_loss(**base)

    def test_single_dropped_echo_does_not_stop(self):
        self.assertFalse(self._stop(loss_duration=RUNTIME.PING_INTERVAL_SECONDS))

    def test_sustained_loss_stops(self):
        self.assertTrue(self._stop(loss_duration=RUNTIME.PING_LOSS_STOP_SECONDS))

    def test_recent_http_success_vetoes_the_stop(self):
        self.assertFalse(self._stop(status_recent=True))

    def test_a_reply_never_stops(self):
        self.assertFalse(self._stop(success=True))

    def test_unarmed_never_stops(self):
        self.assertFalse(self._stop(armed=False))


if __name__ == "__main__":
    unittest.main()
