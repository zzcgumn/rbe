"""Proves the benchmarks' own fixture ladder is reachable from Python too --
same deals, same sizes -- reading them from
benchmarks/belief_evaluation/ladder_reference, a small C++ program run once
as a subprocess and its stdout parsed as Python literals.

Same "build both sides of every comparison from one description" mechanism
as test_belief_space_local_evaluation_parity.py's own REFERENCE: hand-
transcribing the ladder a second time in Python is exactly how a slip
hides, so this reads the C++ program's own output instead. See that test
file's module docstring for the fuller rationale, which applies here
unchanged.
"""

import subprocess
import unittest
from pathlib import Path

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import ExhaustiveLayoutSource


def _repo_root(start: Path | None = None) -> Path:
    # Do not Path.resolve() -- under `bazel test` this file is often a
    # runfiles symlink into the execroot/source tree, and resolving leaves
    # the runfiles tree where the ladder_reference data dep lives. Mirrors
    # test_belief_space_local_evaluation_parity.py's own _repo_root exactly.
    here = (start or Path(__file__)).absolute()
    for parent in here.parents:
        workflows = parent / ".github" / "workflows"
        if workflows.is_dir() and any(workflows.glob("ci_*.yml")):
            return parent
    raise AssertionError("could not locate repository root from test file path")


def _ladder_reference_binary() -> Path:
    # Bazel emits a cc_binary's runfile as the bare target name on
    # Linux/macOS but with a .exe suffix on Windows -- prefer the bare
    # name, fall back to the suffixed one only when that is what actually
    # exists. Same reasoning as the parity test's own binary lookup.
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
    # Trusted, self-generated output (this process's own data dependency,
    # not external input) -- exec rather than ast.literal_eval because the
    # output is a sequence of assignment statements, not one literal.
    exec(compile(output, "<ladder_reference>", "exec"), namespace)  # noqa: S102
    return namespace


REFERENCE = _run_ladder_reference()


class TestFixtureLadderIsReachableFromPython(unittest.TestCase):
    # Every rung the C++ side reports, in both its unconstrained
    # (without-history) and history-constrained forms -- the same pairing
    # fixtures.hpp's own Rung documents, read from the same root each
    # program printed rather than a second, hand-built one.
    def test_every_rung_without_history_size_matches(self) -> None:
        for rung in REFERENCE["RUNGS"]:
            with self.subTest(rung=rung["unconstrained_size"]):
                source = ExhaustiveLayoutSource(rung, rung["declarer"], 1)
                self.assertEqual(source.size(), rung["unconstrained_size"])

    def test_every_rung_with_history_size_matches(self) -> None:
        for rung in REFERENCE["RUNGS"]:
            with self.subTest(rung=rung["constrained_size"]):
                history = [Card(suit, rank) for suit, rank in rung["history"]]
                source = ExhaustiveLayoutSource(
                    rung, rung["declarer"], 1, history, rung["opening_leader"])
                self.assertEqual(source.size(), rung["constrained_size"])

    def test_at_least_five_rungs_spanning_two_orders_of_magnitude(self) -> None:
        # Not a re-derivation of fixtures_test.cpp's own shape assertions
        # (it already owns that claim in C++) -- just confirming the
        # Python-visible mirror is not a truncated or stale copy of it.
        sizes = [rung["unconstrained_size"] for rung in REFERENCE["RUNGS"]]
        self.assertGreaterEqual(len(sizes), 5)
        self.assertGreaterEqual(max(sizes), min(sizes) * 100)


if __name__ == "__main__":
    unittest.main()
