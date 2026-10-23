"""Checks that the benchmark fixture ladder gives the same deals and sizes
from Python. benchmarks/belief_evaluation/ladder_reference, a C++ program, runs
once as a subprocess and prints the ladder as Python literals.

Reading the C++ output avoids a second, hand-copied ladder, as in
test_belief_space_local_evaluation_parity.py.
"""

import subprocess
import unittest
from pathlib import Path

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import PlayRecord


def _repo_root(start: Path | None = None) -> Path:
    # Do not call Path.resolve(). Under `bazel test` this file is often a
    # runfiles symlink, and resolving it leaves the runfiles tree that holds
    # ladder_reference. Same as _repo_root in the parity test.
    here = (start or Path(__file__)).absolute()
    for parent in here.parents:
        workflows = parent / ".github" / "workflows"
        if workflows.is_dir() and any(workflows.glob("ci_*.yml")):
            return parent
    raise AssertionError("could not locate repository root from test file path")


def _ladder_reference_binary() -> Path:
    # A cc_binary runfile has no suffix on Linux and macOS, and .exe on
    # Windows. Try both, bare name first, as the parity test does.
    root = _repo_root() / "benchmarks" / "belief_evaluation" / "ladder_reference"
    for candidate in (root, root.with_suffix(".exe")):
        if candidate.is_file():
            return candidate
    raise AssertionError(f"ladder_reference data dependency not found at {root} or {root}.exe")


def _run_ladder_reference() -> dict:
    output = subprocess.run(
        [str(_ladder_reference_binary())],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    namespace: dict = {}
    # The output comes from this test's data dependency, so it is trusted.
    # It is a series of assignments, not one literal, so ast.literal_eval
    # cannot read it.
    exec(compile(output, "<ladder_reference>", "exec"), namespace)  # noqa: S102
    return namespace


REFERENCE = _run_ladder_reference()


class TestFixtureLadderIsReachableFromPython(unittest.TestCase):
    # Every rung, without and with its history, as Rung in fixtures.hpp
    # pairs them.
    def test_every_rung_without_history_size_matches(self) -> None:
        for rung in REFERENCE["RUNGS"]:
            with self.subTest(rung=rung["unconstrained_size"]):
                source = ExhaustiveLayoutSource(rung, rung["declarer"], 1)
                self.assertEqual(source.size(), rung["unconstrained_size"])

    def test_every_rung_with_history_size_matches(self) -> None:
        for rung in REFERENCE["RUNGS"]:
            with self.subTest(rung=rung["constrained_size"]):
                history = [Card(suit, rank) for suit, rank in rung["history"]]
                record = PlayRecord(history, rung["opening_leader"])
                source = ExhaustiveLayoutSource(rung, rung["declarer"], 1, record=record)
                self.assertEqual(source.size(), rung["constrained_size"])

    def test_at_least_five_rungs_spanning_two_orders_of_magnitude(self) -> None:
        # fixtures_test.cpp checks the ladder's shape. This only checks that
        # the Python copy is not truncated.
        sizes = [rung["unconstrained_size"] for rung in REFERENCE["RUNGS"]]
        self.assertGreaterEqual(len(sizes), 5)
        self.assertGreaterEqual(max(sizes), min(sizes) * 100)


if __name__ == "__main__":
    unittest.main()
