"""The walk through a cyclic call graph: exact, order-free and bounded (#430).

`Walker.depth()` reports the deepest path that enters no function twice
beyond the one call that closes a cycle: that call's frame is counted, and the
walk is cut there. A cut result is only valid for the stack that produced it,
and memoising one by function alone made the report depend on root order
(#245, #248). Not memoising cut results at all fixed that and made every
ancestor of a cycle re-walk its subtree once per path: exponential, and with the
IDF log hook stitched the firebeetle2 walk did not finish in 20 minutes.

A result depends only on the stack's members inside the function's own strongly
connected component, so that is the memo key. These graphs are small enough to
check against a brute-force walk of every path.
"""

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import stack_usage_report as sur  # noqa: E402


class GraphImage(sur.Image):
    """An Image built from {name: (frame, [callee names])}, with no objdump."""

    def __init__(self, graph):  # noqa: D107 - no listing to read
        self.funcs = {}
        addr = {name: 0x40000000 + 0x100 * i for i, name in enumerate(graph)}
        for name, (frame, _callees) in graph.items():
            fn = sur.Function(addr[name], name)
            fn.frame, fn.frame_kind = frame, "fixed"
            self.funcs[fn.addr] = fn
        for name, (_frame, callees) in graph.items():
            self.funcs[addr[name]].calls = [(addr[c], "call8", None) for c in callees]
        self.addr = addr

    def fn(self, name):
        return self.funcs[self.addr[name]]


def brute_force(graph, root):
    """The walk's answer below root, by trying every path with no memo at all.

    Walker's semantics, restated: a call to a function already on the stack
    is a real call - its frame is counted - and the walk stops there, cut.
    """
    def walk(name, seen):
        best = 0
        for callee in graph[name][1]:
            below = 0 if callee in seen else walk(callee, seen | {callee})
            best = max(best, graph[callee][0] + below)
        return best
    return walk(root, {root})


# Two cycles that share a node, entered from two roots, with the deepest exit
# reachable only by going round one of them first.
TANGLE = {
    "rootA": (32, ["log"]),
    "rootB": (48, ["malloc", "log"]),
    "log": (96, ["hook"]),
    "hook": (320, ["emit", "format"]),
    "emit": (32, ["malloc", "log"]),       # emit -> log closes a cycle
    "format": (144, ["malloc"]),
    "malloc": (32, ["align", "leaf"]),
    "align": (48, ["log"]),                # malloc -> align -> log: a second cycle
    "leaf": (16, []),
}


def ladder(rungs):
    """A chain of two-node cycles; each rung calls down to the next.

    Without a context-keyed memo every rung is walked once per path reaching
    it, 2**rungs times.
    """
    graph = {"root": (16, ["a0", "b0"])}
    for i in range(rungs):
        nxt = [f"a{i + 1}", f"b{i + 1}"] if i + 1 < rungs else ["end"]
        graph[f"a{i}"] = (16, [f"b{i}"] + nxt)
        graph[f"b{i}"] = (32, [f"a{i}"] + nxt)
    graph["end"] = (64, [])
    return graph


class ACyclicGraphIsWalkedExactly(unittest.TestCase):
    def test_every_function_matches_a_brute_force_walk(self):
        image = GraphImage(TANGLE)
        walker = sur.Walker([image])
        for name in TANGLE:
            with self.subTest(root=name):
                self.assertEqual(walker.depth(image, image.fn(name))[0],
                                 brute_force(TANGLE, name))

    def test_the_answer_does_not_depend_on_which_root_was_walked_first(self):
        results = []
        for order in (["rootA", "rootB", "emit"], ["emit", "rootB", "rootA"]):
            image = GraphImage(TANGLE)
            walker = sur.Walker([image])
            results.append({n: walker.depth(image, image.fn(n))[0] for n in order})
        self.assertEqual(results[0], results[1])

    def test_a_cut_is_still_reported(self):
        image = GraphImage(TANGLE)
        walker = sur.Walker([image])
        _total, _chain, cut = walker.depth(image, image.fn("rootA"))
        self.assertTrue(cut)
        self.assertIn("log", walker.cut_cycles)


class TheWalkIsBoundedInTime(unittest.TestCase):
    def test_cyclic_rungs_are_walked_a_linear_number_of_times(self):
        # Eight rungs: the walk this replaced makes 196,607 depth() calls here,
        # this one 95. Few enough rungs that the old walk still finishes in a
        # tenth of a second, so a regression fails on the count below rather
        # than by hanging; at sixteen it did not finish in five minutes.
        graph = ladder(8)
        image = GraphImage(graph)
        walker = sur.Walker([image])
        calls = 0
        depth = walker.depth

        def counted(*args):
            nonlocal calls
            calls += 1
            return depth(*args)

        walker.depth = counted
        total = walker.depth(image, image.fn("root"))[0]
        # Both nodes of every rung, then the end: 8 * (16 + 32) + 64.
        self.assertEqual(total, 8 * 48 + 64)
        self.assertLess(calls, 10 * len(graph))

    def test_a_component_past_the_state_cap_is_refused_not_truncated(self):
        image = GraphImage(TANGLE)
        walker = sur.Walker([image])
        walker.MAX_COMPONENT_STATES = 3
        with self.assertRaises(sur.Fatal):
            walker.depth(image, image.fn("rootA"))


if __name__ == "__main__":
    unittest.main()
