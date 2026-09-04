"""Compiler proof for the chip-target-sized shared constants (#256).

Several numbers in shared code were sized against artoo-esp32's heap and flash
and the ESP32-P4 inherited them verbatim. They are now per chip target. Native
Unity tests (test/test_native/) always build PA_BOARD_ARTOO_ESP32 (platformio.ini
env:native), so a single binary can only ever pin one board's values -- and the
defect is by definition a cross-board fact. It is proven the way
test_board_uart_allocation.py proves the UART allocation: compile the real
headers once under each PA_BOARD value and compare what each actually produces.

Three things are asserted, and the third is the one that matters:

1. Each board's values, written here independently of the headers so a
   copy-paste that converged the two boards cannot also update the expectation.
2. That the ESP32-P4 arm satisfies the ceilings its derivations claim.
3. That artoo-esp32 and the ESP32-P4 do NOT converge, and that artoo-esp32's
   values are exactly the pre-#256 numbers. Both boards passing their own
   expectation in isolation would still hold if a later edit moved artoo-esp32
   too, which is the failure this ticket is most exposed to.
"""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
INCLUDE_DIR = REPO_ROOT / "include"

# Everything the probed headers pull in, staged into the probe's include path.
# config.h drags the two manifest .inc files and the FireBeetle pin inventory.
PROBE_HEADER_SET = (
    "config.h",
    "board_capabilities.inc",
    "build_flags.inc",
    "firebeetle_required_pins.inc",
    "log_buffer.h",
    "protocol_check.h",
    "seq_store_index.h",
    "seq_store_util.h",
    "sequence_engine.h",
    "sequence_run_evidence.h",
)

# What each board must produce. Order matches the printf in _probe_source().
EXPECTED_BY_BOARD = {
    # Pre-#256 values, unchanged. Every one was sized against this board's heap,
    # and every one must stay exactly where it is.
    "PA_BOARD_ARTOO_ESP32": {
        "seq_file_max_bytes": 12 * 1024,
        "seq_fs_free_floor": 24 * 1024,
        "evid_cmd_len": 48,
        "evid_tx_cap": 32,
        "evid_cleanup_cap": 12,
        "evid_record_bytes": 2204,
        "log_line_max": 128,
        "log_ladder": (16, 20, 24, 48),
        "log_ring_max_lines": 48,
        # Pre-#256 literals, unchanged. WebEvents is 6144 here even though the
        # #248 rule on its 5888 B chain would raise it -- the tight-heap decline
        # recorded beside the constant in include/config.h.
        "rc_input_stack": 7168,
        "audio_stack": 6144,
        "web_events_stack": 6144,
        # Raised from 5120 by #226 - the only stack here whose under-size was a
        # reproduced device fault rather than a walk - then re-derived by #269
        # once the config-write path stopped carrying three ConfigSnapshot
        # copies, and again at #271 from a walk on the merged tree:
        # 7024 * 1.25 = 8780 -> 9216.
        "console_stack": 9216,
    },
    # Re-derived from the sequence model's own ceilings. See the derivations in
    # include/seq_store_util.h and include/sequence_run_evidence.h.
    "PA_BOARD_FIREBEETLE2": {
        "seq_file_max_bytes": 24 * 1024,
        "seq_fs_free_floor": 48 * 1024,
        "evid_cmd_len": 64,
        "evid_tx_cap": 112,
        "evid_cleanup_cap": 16,
        "evid_record_bytes": 8284,
        # LOG_LINE_MAX is deliberately shared -- its rationale was never a heap
        # argument. Only the depth ladder is per chip.
        "log_line_max": 128,
        "log_ladder": (32, 64, 96, 112),
        "log_ring_max_lines": 112,
        # Re-derived by the #248 rule from tools/stack_usage_report.py chains
        # against the linked firebeetle2 image (product and profiler match).
        # RCInputTask 5376 * 1.25 = 6720 -> 7168; AudioTask 4848 * 1.25 = 6060
        # -> 6144; WebEvents 5808 * 1.25 = 7260 -> 7680. Only WebEvents moves:
        # 5808 already exceeds the inherited 6144.
        "rc_input_stack": 7168,
        "audio_stack": 6144,
        "web_events_stack": 7680,
        # ConsoleTask by the same rule (#226, re-derived by #269 and again at
        # #271): 7136 * 1.25 = 8920 -> 9216. The panic the original raise fixed
        # was captured on this board. The two chips land on the same 512-byte
        # step here by coincidence; their chains still differ.
        "console_stack": 9216,
    },
}

# Worst-case static chains, read from the recipe table rather than restated.
#
# The EXPECTED_BY_BOARD values above are deliberately written out by hand, so
# that a copy-paste converging the two boards cannot also update the
# expectation. The chains are the opposite case: since #271 they have a single
# source (tools/task_stack_recipes.json, checked against include/config.h by
# test_task_stack_recipes.py and re-walked from a linked image by
# tools/check_task_stack_chains.py), and a second hand-written copy here would
# be exactly the drift that ticket exists to remove -- one of these four was
# already 176 B stale against the merged tree when it was written out.
#
# The ConsoleTask chain is the stitched figure on both chips: onCliCommand's
# total plus consoleTask's and embeddedCliProcess's own frames, because
# embedded-cli reaches the command callback through cli->onCommand and the
# walker does not follow indirect calls. AudioTask's artoo-esp32 chain is the
# profiler image, the deeper of the two. Both are recorded per arm in the
# recipe table.
_RECIPES = json.loads(
    (REPO_ROOT / "tools" / "task_stack_recipes.json").read_text(encoding="utf-8")
)
_CHAIN_BY_KEY = {
    "rc_input_stack": "RCInputTask",
    "audio_stack": "AudioTask",
    "web_events_stack": "WebEvents",
    "console_stack": "Console",
}


def _chains_for(chip):
    by_task = {entry["task"]: entry for entry in _RECIPES["tasks"]}
    return {
        key: by_task[task]["chips"][chip]["chain_bytes"]
        for key, task in _CHAIN_BY_KEY.items()
    }


P4_STACK_CHAINS = _chains_for("esp32p4")
ESP32_STACK_CHAINS = _chains_for("esp32")


def stack_size_for_chain(chain_bytes):
    """#248 rule: chain + 25%, rounded up to the next 512 bytes."""
    need = (chain_bytes * 5 + 3) // 4
    return ((need + 511) // 512) * 512

# Boot-path log sites per level, measured from the call closure of setup() plus
# the web bring-up entered from the WiFi event callback, with the task entry
# points excluded. The ESP32-P4 ladder is this count rounded up to the next
# multiple of 16 (include/log_buffer.h carries the table and the method).
BOOT_PATH_LOG_SITES = (20, 55, 96, 104)

# The largest JSON the Learned Sequence format can produce: PC_MAX_STEPS (96)
# steps in each of the two branches, every step a dome command at PC_CMD_MAX
# (63). Measured against ArduinoJson 7.4.3 with the format's own serializer
# shape; see the table in include/seq_store_util.h.
FORMAT_MAX_SEQUENCE_JSON_BYTES = 18843

# The model ceilings the ESP32-P4 run-evidence derivation is taken from, restated
# here so a change to protocol_check.h or sequence_engine.h that invalidates the
# derivation fails the assertion that depends on it instead of silently rebasing
# onto the new value.
PC_MAX_STEPS = 96          # protocol_check.h
PC_CMD_MAX = 63            # protocol_check.h
ENGINE_FINAL_QUEUE = 16    # SeqEngineState::finalQ, sequence_engine.h


class BoardChipSizedConstants(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler_name = os.environ.get("CXX", "c++")
        cls.compiler = shutil.which(compiler_name)
        if cls.compiler is None:
            raise RuntimeError(f"C++ compiler not found: {compiler_name}")
        cls._cache = {}

    @staticmethod
    def _probe_source(board_macro):
        return "\n".join(
            [
                f"#define PA_BOARD {board_macro}",
                # config.h requires these per-env and #errors without them
                # (#244). A probe stands in for a build environment, so it
                # declares what any environment must.
                "#define PA_LOG_LEVEL 2",
                "#define PA_HEAP_PROFILE 0",
                '#include "seq_store_util.h"',
                '#include "sequence_run_evidence.h"',
                '#include "log_buffer.h"',
                '#include "config.h"',
                "#include <cstdio>",
                "int main() {",
                '    std::printf("%zu %zu %d %d %d %zu %zu %zu %zu %zu %zu %zu '
                '%u %u %u %u\\n",',
                "        (size_t)SEQ_FILE_MAX_BYTES, (size_t)SEQ_FS_FREE_FLOOR,",
                "        (int)SEQ_EVID_CMD_LEN, (int)SEQ_EVID_TX_CAP,",
                "        (int)SEQ_EVID_CLEANUP_CAP, sizeof(SeqRunEvidence),",
                "        LOG_LINE_MAX, LOG_RING_LINES_ERROR, LOG_RING_LINES_WARN,",
                "        LOG_RING_LINES_INFO, LOG_RING_LINES_DEBUG,",
                "        LOG_RING_MAX_LINES,",
                "        (unsigned)RC_INPUT_TASK_STACK_BYTES,",
                "        (unsigned)AUDIO_TASK_STACK_BYTES,",
                "        (unsigned)WEB_EVENTS_TASK_STACK_BYTES,",
                "        (unsigned)CONSOLE_TASK_STACK_BYTES);",
                "    return 0;",
                "}",
                "",
            ]
        )

    def _values(self, board_macro):
        cached = self._cache.get(board_macro)
        if cached is not None:
            return cached
        with tempfile.TemporaryDirectory() as tmp:
            staged = Path(tmp) / "include"
            staged.mkdir()
            for name in PROBE_HEADER_SET:
                shutil.copyfile(INCLUDE_DIR / name, staged / name)
            source_path = Path(tmp) / "chip_sized_constants_probe.cpp"
            source_path.write_text(self._probe_source(board_macro), encoding="utf-8")
            output_path = Path(tmp) / "chip_sized_constants_probe"
            compile_result = subprocess.run(
                [self.compiler, "-std=c++17", "-I", str(staged),
                 str(source_path), "-o", str(output_path)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(output_path)], capture_output=True, text=True,
                check=False, timeout=10,
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)
        fields = [int(v) for v in run_result.stdout.split()]
        self.assertEqual(len(fields), 16, run_result.stdout)
        values = {
            "seq_file_max_bytes": fields[0],
            "seq_fs_free_floor": fields[1],
            "evid_cmd_len": fields[2],
            "evid_tx_cap": fields[3],
            "evid_cleanup_cap": fields[4],
            # SeqRunEvidence is scalars and fixed arrays only -- no pointers --
            # so the host's word size does not change its layout and this figure
            # is the one the device pays, twice.
            "evid_record_bytes": fields[5],
            "log_line_max": fields[6],
            "log_ladder": tuple(fields[7:11]),
            "log_ring_max_lines": fields[11],
            "rc_input_stack": fields[12],
            "audio_stack": fields[13],
            "web_events_stack": fields[14],
            "console_stack": fields[15],
        }
        self._cache[board_macro] = values
        return values

    # -- 1. each board's declared values -------------------------------------

    def test_artoo_esp32_values_are_unchanged(self):
        self.assertEqual(
            self._values("PA_BOARD_ARTOO_ESP32"),
            EXPECTED_BY_BOARD["PA_BOARD_ARTOO_ESP32"],
        )

    def test_firebeetle2_values_are_the_re_derived_ones(self):
        self.assertEqual(
            self._values("PA_BOARD_FIREBEETLE2"),
            EXPECTED_BY_BOARD["PA_BOARD_FIREBEETLE2"],
        )

    # -- 2. the ESP32-P4 arm satisfies the ceilings it claims -----------------

    def test_p4_file_cap_holds_the_formats_largest_sequence(self):
        v = self._values("PA_BOARD_FIREBEETLE2")
        self.assertGreaterEqual(
            v["seq_file_max_bytes"], FORMAT_MAX_SEQUENCE_JSON_BYTES,
            "the ESP32-P4 cap must reach the format's own 96+96-step ceiling; "
            "not reaching it is the artoo-esp32 constraint this ticket removes",
        )

    def test_artoo_file_cap_is_below_the_formats_largest_sequence(self):
        """The inherited constraint itself, asserted so it is not read as a bug.

        artoo-esp32 genuinely cannot save a 96+96-step sequence: the raw body,
        the parsed document and the SeqStep staging are live at once and do not
        fit its measured free heap. That is a recorded property of the small
        board, and it is why the cap had to become per chip rather than simply
        be raised for everyone.
        """
        v = self._values("PA_BOARD_ARTOO_ESP32")
        self.assertLess(v["seq_file_max_bytes"], FORMAT_MAX_SEQUENCE_JSON_BYTES)

    def test_p4_evidence_ring_covers_a_whole_non_looping_run(self):
        """96 authored steps less the STEP_END sentinel, plus the drain queue.

        One run executes one branch, so PC_MAX_STEPS - 1 is the most commands
        its authored steps can emit, followed by at most finalQ cleanup actions.
        A ring that holds both captures every non-looping run whole.
        """
        v = self._values("PA_BOARD_FIREBEETLE2")
        self.assertGreaterEqual(
            v["evid_tx_cap"], (PC_MAX_STEPS - 1) + ENGINE_FINAL_QUEUE)
        self.assertGreaterEqual(v["evid_cleanup_cap"], ENGINE_FINAL_QUEUE)

    def test_p4_evidence_entry_holds_the_longest_command_whole(self):
        v = self._values("PA_BOARD_FIREBEETLE2")
        self.assertGreaterEqual(v["evid_cmd_len"], PC_CMD_MAX + 1)

    def test_artoo_evidence_ring_still_truncates(self):
        """The inherited constraint itself, asserted so it is not read as a bug.

        artoo-esp32 holds the record twice in static DRAM and has none to give,
        so both dimensions stay below the model's ceilings and the record says
        so through txOmittedRecentCount / cleanupTruncated. That is the price
        this ticket removes only on the board that can afford to pay it.
        """
        v = self._values("PA_BOARD_ARTOO_ESP32")
        self.assertLess(v["evid_cmd_len"], PC_CMD_MAX + 1)
        self.assertLess(v["evid_tx_cap"], (PC_MAX_STEPS - 1) + ENGINE_FINAL_QUEUE)

    def test_free_floor_covers_a_second_full_size_file_on_both_boards(self):
        for board in EXPECTED_BY_BOARD:
            with self.subTest(board=board):
                v = self._values(board)
                self.assertGreaterEqual(
                    v["seq_fs_free_floor"], v["seq_file_max_bytes"],
                    "the outgoing copy coexists with .tmp.json until the rename",
                )

    def test_p4_log_ladder_retains_the_whole_boot_at_every_level(self):
        """Every rung holds at least the measured boot-path site count.

        That is the ESP32-P4 rule: a post-boot /api/logs fetch answers what
        happened at boot without a serial capture, which is the only channel a
        board still bringing up its network backend reliably has.
        """
        ladder = self._values("PA_BOARD_FIREBEETLE2")["log_ladder"]
        for level, (rung, sites) in enumerate(zip(ladder, BOOT_PATH_LOG_SITES), 1):
            with self.subTest(level=level):
                self.assertGreaterEqual(rung, sites)
                # Rounded up to the next multiple of 16, so no rung may exceed
                # the count by a whole step -- that would be slack, not margin.
                self.assertLess(rung, sites + 16)

    def test_artoo_log_ladder_cannot_retain_the_whole_boot(self):
        """The inherited constraint itself, asserted so it is not read as a bug.

        artoo-esp32's deepest rung is 48 lines against 104 boot-path sites at
        DEBUG, so its ring cannot hold a whole verbose boot. That is the price
        of 42692 B of free heap, and it is why the ladder had to become per chip
        rather than simply be deepened for everyone.
        """
        ladder = self._values("PA_BOARD_ARTOO_ESP32")["log_ladder"]
        self.assertLess(ladder[-1], BOOT_PATH_LOG_SITES[-1])

    def test_p4_deepest_rung_costs_no_more_heap_share_than_artoos(self):
        """Depth is bought with heap, and over-sizing is the dangerous direction.

        A rung costs 2 x depth x LOG_LINE_MAX: the ring plus the /api/logs body
        buffer allocated beside it. Hold the ESP32-P4's deepest rung to no larger
        a share of its measured internal free heap than artoo-esp32's deepest
        rung takes of its own.
        """
        artoo_free_heap = 42692            # config.h task-stack block
        p4_free_heap_after_this_ticket = 102000  # ~114 KB (#245) less 12160 B of
                                                 # static growth from the evidence ring
        artoo = self._values("PA_BOARD_ARTOO_ESP32")
        p4 = self._values("PA_BOARD_FIREBEETLE2")
        artoo_share = (2 * artoo["log_ring_max_lines"] * artoo["log_line_max"]
                       / artoo_free_heap)
        p4_share = (2 * p4["log_ring_max_lines"] * p4["log_line_max"]
                    / p4_free_heap_after_this_ticket)
        self.assertLessEqual(p4_share, artoo_share)
        # And it must still be a real increase in retained history, or the
        # per-chip split bought nothing.
        self.assertGreater(p4["log_ring_max_lines"], artoo["log_ring_max_lines"])

    def test_log_ladder_is_monotone_and_ends_at_the_ring_ceiling(self):
        for board in EXPECTED_BY_BOARD:
            with self.subTest(board=board):
                v = self._values(board)
                ladder = v["log_ladder"]
                self.assertEqual(sorted(ladder), list(ladder),
                                 "depth must not fall as verbosity rises")
                self.assertEqual(ladder[-1], v["log_ring_max_lines"],
                                 "LOG_RING_MAX_LINES bounds the sized ring and the"
                                 " native test storage, so it must equal the"
                                 " deepest rung")

    # -- 3. the regression: the two boards must not converge ------------------

    def test_p4_task_stacks_cover_the_measured_chains(self):
        """ESP32-P4 stacks are the #248 rule applied to the measured chain.

        Under-sizing is the dangerous direction. A constant below chain+25%
        rounded up to 512 is an overrun waiting for a path the HWM has not
        run yet. RCInputTask and AudioTask land on the inherited literals;
        WebEvents does not -- 5808 already exceeds 6144.
        """
        v = self._values("PA_BOARD_FIREBEETLE2")
        for key, chain in P4_STACK_CHAINS.items():
            with self.subTest(key=key):
                self.assertGreaterEqual(v[key], stack_size_for_chain(chain))
                self.assertEqual(v[key] % 512, 0)

    def test_p4_webevents_is_the_one_that_moves(self):
        """The inherited 6144 is 336 B short of the ESP32-P4 chain.

        RCInputTask and AudioTask re-derive to the same number the shipping
        artoo image has always had. WebEvents is the overrun this ticket
        exists to fix, so a later edit that "simplified" it back to 6144
        on both chips is the regression.
        """
        artoo = self._values("PA_BOARD_ARTOO_ESP32")
        p4 = self._values("PA_BOARD_FIREBEETLE2")
        self.assertEqual(artoo["rc_input_stack"], p4["rc_input_stack"])
        self.assertEqual(artoo["audio_stack"], p4["audio_stack"])
        self.assertGreater(p4["web_events_stack"], artoo["web_events_stack"])
        self.assertGreater(p4["web_events_stack"], 6144)

    def test_artoo_task_stacks_are_not_below_the_measured_chains(self):
        """ESP32 stacks may stay above the rule; they must not drop below the chain.

        Xtensa chains are lower bounds (include/config.h). A value below the
        measured chain would be a proven overrun even on that weaker walk.
        """
        v = self._values("PA_BOARD_ARTOO_ESP32")
        for key, chain in ESP32_STACK_CHAINS.items():
            with self.subTest(key=key):
                self.assertGreaterEqual(v[key], chain)

    def test_the_two_boards_do_not_converge(self):
        artoo = self._values("PA_BOARD_ARTOO_ESP32")
        firebeetle = self._values("PA_BOARD_FIREBEETLE2")
        self.assertNotEqual(artoo, firebeetle)
        # console_stack is deliberately absent: see
        # test_console_stack_is_derived_per_chip_even_where_they_coincide.
        for key in ("seq_file_max_bytes", "seq_fs_free_floor",
                    "evid_cmd_len", "evid_tx_cap", "evid_cleanup_cap",
                    "evid_record_bytes", "log_ring_max_lines",
                    "web_events_stack"):
            with self.subTest(key=key):
                self.assertGreater(
                    firebeetle[key], artoo[key],
                    f"{key}: the ESP32-P4 must not inherit artoo-esp32's sizing",
                )

    def test_console_stack_is_derived_per_chip_even_where_they_coincide(self):
        """The rule is what each arm must follow, not "P4 is the bigger one".

        The Console chains differ per chip (ESP32_STACK_CHAINS vs
        P4_STACK_CHAINS) and since #269 they round to the same 512-byte step.
        A strictly-greater assertion would read that coincidence as the P4 arm
        inheriting artoo's number, which is the opposite of what happened - so
        this asserts the thing that actually matters instead: each arm is
        EXACTLY what the #248 rule gives for its OWN measured chain. An arm
        copied from the other chip fails here the moment the chains diverge
        again, and a hand-edited value that clears its chain but not the rule
        fails immediately.
        """
        artoo = self._values("PA_BOARD_ARTOO_ESP32")
        firebeetle = self._values("PA_BOARD_FIREBEETLE2")
        self.assertEqual(
            stack_size_for_chain(ESP32_STACK_CHAINS["console_stack"]),
            artoo["console_stack"],
            "the artoo-esp32 Console stack does not follow the #248 rule for its chain",
        )
        self.assertEqual(
            stack_size_for_chain(P4_STACK_CHAINS["console_stack"]),
            firebeetle["console_stack"],
            "the ESP32-P4 Console stack does not follow the #248 rule for its chain",
        )
        self.assertNotEqual(
            ESP32_STACK_CHAINS["console_stack"],
            P4_STACK_CHAINS["console_stack"],
            "the two chains became identical - one of them was copied, not measured",
        )

    def test_every_chip_arm_declares_every_constant(self):
        """A third chip target cannot inherit one of these by omission.

        Each per-chip block ends in an #error rather than falling through, so a
        new chip target that forgets one fails at the declaration instead of at
        some unrelated consumer. Assert the #error arm exists rather than
        trusting that it was written.
        """
        for header, needle in (
            ("seq_store_util.h",
             "the Learned Sequence per-file cap has no value for this chip target"),
            ("sequence_run_evidence.h",
             "sequence run-evidence ring dimensions have no value for this chip target"),
            ("log_buffer.h",
             "the log ring depth ladder has no value for this chip target"),
            ("config.h",
             "task stack sizes have no value for this chip target"),
        ):
            with self.subTest(header=header):
                text = (INCLUDE_DIR / header).read_text(encoding="utf-8")
                self.assertIn(f'#error "{needle}"', text)


if __name__ == "__main__":
    unittest.main()
