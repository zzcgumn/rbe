#!/usr/bin/env python3
"""What a play history buys at realistic positions: constrained against
unconstrained `size()` across the pool and realistic ladder, and how wrong
the unconstrained odds are at the two rungs a bridge player would
recognise (`realistic_a`, `realistic_b`).

No search, no seed, no sampling. `ladder_reference` (built for the Python
fixture-ladder test) already prints both sizes from
`ExhaustiveLayoutSource::size()`; this script only tabulates them, so every
number still comes from C++. `test_belief_space_local_evaluation_fixture_ladder.py`
reads the same program, for an assertion instead of a report.

`history_verdict()` is not checked here.
`FixtureLadderTest.WithHistorySizeMatchesExpected` in fixtures_test.cpp
asserts `HistoryVerdict::Consistent` for every rung, in the default test
cycle.

Usage: like the other scripts here, this runs with plain `python3` and
builds nothing, so build `ladder_reference` first:

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

# Name, the key ladder_reference.cpp prints it under, and the fixture's `k`
# (half the unconstrained pool). ladder_reference does not print k, so it
# is copied from the make_rung(name, k, ...) calls in fixtures.cpp.
RUNGS = [
    ("pool4", "RUNG_POOL4", 4),
    ("pool5", "RUNG_POOL5", 5),
    ("pool6", "RUNG_POOL6", 6),
    ("pool7", "RUNG_POOL7", 7),
    ("pool8", "RUNG_POOL8", 8),
    ("realistic_a", "RUNG_REALISTIC_A", 6),
    ("realistic_b", "RUNG_REALISTIC_B", 8),
]

# realistic_a and _b only: make_rung() in fixtures.cpp forces exactly two
# low Diamonds to West by the void (h=2), whatever k is.
VOID_FORCED_CARDS = 2


def ladder_reference_binary() -> Path:
    # The bare name on Linux and macOS, .exe on Windows. The same lookup as
    # _ladder_reference_binary() in
    # test_belief_space_local_evaluation_fixture_ladder.py.
    root = sweep.bazel_bin_root() / "benchmarks" / "belief_evaluation" / "ladder_reference"
    for candidate in (root, root.with_suffix(".exe")):
        if candidate.is_file():
            return candidate
    raise AssertionError(
        f"{root} (or {root}.exe) does not exist -- build it first: "
        "bazel build //benchmarks/belief_evaluation:ladder_reference")


def run_ladder_reference() -> dict:
    output = subprocess.run(
        [str(ladder_reference_binary())], check=True, capture_output=True, text=True).stdout
    namespace: dict = {}
    # Trusted output from our own data dependency: a sequence of assignment
    # statements. So exec, not ast.literal_eval, as
    # test_belief_space_local_evaluation_fixture_ladder.py does.
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
        "\n  Formula (specs/replenished-belief-evaluation.md, on vacant-spaces\n"
        "  reasoning): unconstrained P(a defender holds a given pool card) = k/n;\n"
        "  constrained = k/(n-h), for a pool of n with a defender holding k and h\n"
        "  cards of the voided suit still outstanding. realistic_a/b: n=2k (the\n"
        "  full pool, forced void cards included), h=2 (the two low Diamonds the\n"
        "  void forces to West).")
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
