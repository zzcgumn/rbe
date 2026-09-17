#!/usr/bin/env python3
"""What a play history buys, at realistic positions -- constrained against
unconstrained `size()` across the whole pool/realistic ladder, and how
wrong the unconstrained odds are at the two rungs built to be positions a
bridge player would recognise (`realistic_a`/`realistic_b`).

No search run, no seed, no sampling -- `ladder_reference` (built for the
Python-side fixture-ladder test) already computes both forms' sizes
directly from `ExhaustiveLayoutSource::size()`, and this script's only job
is to read its output and tabulate it. Reading a C++ program's own output
from Python is not a Python-driven measurement (this plan's own decision
1) -- the same "build both sides of one comparison from one description"
mechanism `test_belief_space_local_evaluation_fixture_ladder.py` already
uses, applied here for a report instead of an assertion.

`history_verdict()` is not re-checked here: `fixtures_test.cpp`'s own
`FixtureLadderTest.WithHistorySizeMatchesExpected`, parameterised over
every rung `ladder_reference` below also emits, already asserts
`HistoryVerdict::Consistent` before trusting any of these sizes, in the
default test cycle -- cited, not re-run.

Usage (no separate build step -- `bazel build` runs automatically via the
`bazel run` invocation below, or build once yourself first):

    bazel build //benchmarks/belief_evaluation:ladder_reference
    python3 benchmarks/belief_evaluation/space_size.py
"""

from __future__ import annotations

import subprocess
import sys
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sweep  # noqa: E402

# Name, python-literal key ladder_reference.cpp prints it under, and the
# fixture's own `k` (half the unconstrained pool -- ladder_reference itself
# does not print k, so this is read off fixtures.cpp's own make_rung(name, k,
# ...) calls, not re-derived).
RUNGS = [
    ("pool4", "RUNG_POOL4", 4),
    ("pool5", "RUNG_POOL5", 5),
    ("pool6", "RUNG_POOL6", 6),
    ("pool7", "RUNG_POOL7", 7),
    ("pool8", "RUNG_POOL8", 8),
    ("realistic_a", "RUNG_REALISTIC_A", 6),
    ("realistic_b", "RUNG_REALISTIC_B", 8),
]

# realistic_a/b only: fixtures.cpp's own make_rung() forces exactly two
# low Diamonds to West by the void (h=2 outstanding void-suit cards),
# regardless of k -- read from that file's own comment, not re-derived.
VOID_FORCED_CARDS = 2


def ladder_reference_binary() -> Path:
    path = sweep.bazel_bin_root() / "benchmarks" / "belief_evaluation" / "ladder_reference"
    if not path.is_file():
        raise AssertionError(
            f"{path} does not exist -- build it first: "
            "bazel build //benchmarks/belief_evaluation:ladder_reference")
    return path


def run_ladder_reference() -> dict:
    output = subprocess.run(
        [str(ladder_reference_binary())], check=True, capture_output=True, text=True).stdout
    namespace: dict = {}
    # Trusted, self-generated output (this process's own data dependency),
    # a sequence of assignment statements -- exec, not ast.literal_eval,
    # matching test_belief_space_local_evaluation_fixture_ladder.py's own
    # consumption of the identical program.
    exec(compile(output, "<ladder_reference>", "exec"), namespace)  # noqa: S102
    return namespace


def sizes_and_ratio(reference: dict) -> None:
    print("#" * 78)
    print("# Sizes with and without history, across the pool/realistic ladder")
    print("#" * 78)
    print(f"{'rung':<14s}{'unconstrained':>15s}{'constrained':>15s}{'ratio':>10s}")
    for name, key, _k in RUNGS:
        record = reference[key]
        unconstrained = record["unconstrained_size"]
        constrained = record["constrained_size"]
        ratio = unconstrained / constrained if constrained else float("nan")
        print(f"{name:<14s}{unconstrained:>15d}{constrained:>15d}{ratio:>9.2f}x")


def how_wrong_at_realistic_rungs() -> None:
    print("\n" + "#" * 78)
    print("# How wrong the unconstrained odds are, at the two realistic rungs")
    print("#" * 78)
    print(
        "\n  Formula (read from analysis/validation_risk_analysis.md section A,\n"
        "  not re-derived): unconstrained P(a defender holds a given pool card)\n"
        "  = k/n; constrained = k/(n-h), for a pool of n with a defender holding\n"
        "  k and h cards of the voided suit still outstanding. realistic_a/b:\n"
        "  n=2k (the full pool, forced void cards included), h=2 (the two low\n"
        "  Diamonds the void forces to West).")
    for name, _key, k in RUNGS:
        if name not in ("realistic_a", "realistic_b"):
            continue
        n = 2 * k
        c = n - VOID_FORCED_CARDS
        wrong = Fraction(k, n)
        right = Fraction(k, c)
        swing_pp = float(right - wrong) * 100.0
        print(f"\n  {name} (k={k}):")
        print(
            f"    unconstrained={float(wrong):.3f} ({wrong})  constrained={float(right):.3f} "
            f"({right})  swing={swing_pp:+.1f}pp")
        print(f"    -- on every one of the {c} outstanding non-void cards")
        print(
            f"    the two forced void cards themselves: unconstrained={float(wrong):.3f}, "
            f"constrained=0 (impossible, already shown out)")


def run() -> None:
    reference = run_ladder_reference()
    sizes_and_ratio(reference)
    how_wrong_at_realistic_rungs()


if __name__ == "__main__":
    run()
