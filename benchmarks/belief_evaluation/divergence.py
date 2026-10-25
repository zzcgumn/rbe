#!/usr/bin/env python3
"""How often SpreadPolicy::TouchingSequence and ::AllOptimal (spread.hpp)
diverge outside a fixture built for it, and whether that changes the final
`p_make`.

Two different questions:

  1. **Choice divergence**: at a real defender node, do the two policies'
     candidate sets differ? One solve_board call per node is enough (see
     counting_divergence_defender in instrument.cpp): TouchingSequence's
     set is always a subset of AllOptimal's.
  2. **Answer divergence**: does the choice change the root's `p_make`?
     Runs the real DoubleDummyDefender under each policy (--mode=count)
     and compares the two p_make values.

Both need solve_board, so both run only on the solver ladder, the only
fixtures solve_board accepts (see fixtures.hpp). Both use only the
instrument's --mode=divergence and --policy.

Usage (after `bazel build //benchmarks/belief_evaluation:instrument`):

    python3 benchmarks/belief_evaluation/divergence.py
"""

from __future__ import annotations

import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sweep  # noqa: E402

# The fixtures are exhaustive, so every number is bitwise the same for
# every seed: the source order never changes which nodes the tree visits
# when every layout is included. Three seeds still run and are compared,
# so that claim stays checked.
SEEDS = [1, 2, 3]

POLICIES = ["touching", "all_optimal"]


def assert_seed_invariant(values: list[float], label: str) -> float:
    assert len(set(values)) == 1, f"{label}: not seed-invariant -- {values}"
    return values[0]


def choice_divergence(binary: Path) -> None:
    print("#" * 78)
    print("# Part 1: choice divergence -- do the two policies pick different cards?")
    print("#" * 78)

    for fixture in sweep.SOLVER_RUNG_NAMES:
        print(f"\n{fixture}")
        for primary in POLICIES:
            calls_by_seed = []
            divergences_by_seed = []
            for seed in SEEDS:
                record = sweep.run_instrument(
                    binary, fixture=fixture, history="without", seed=seed, mode="divergence",
                    strategy="double_dummy", policy=primary)
                assert record.fields["error_callback"] == "none", record.fields
                calls_by_seed.append(int(record.fields["delta_calls"]))
                divergences_by_seed.append(int(record.fields["policy_divergences"]))
            calls = assert_seed_invariant(calls_by_seed, f"{fixture}/{primary} delta_calls")
            divergences = assert_seed_invariant(
                divergences_by_seed, f"{fixture}/{primary} policy_divergences")
            rate = divergences / calls if calls else float("nan")
            print(
                f"  driven by {primary:11s}: delta_calls={calls:.0f}  "
                f"policy_divergences={divergences:.0f}  rate={rate:.1%}")


def answer_divergence(binary: Path) -> None:
    print("\n" + "#" * 78)
    print("# Part 2: answer divergence -- does the choice change the final p_make?")
    print("#" * 78)

    for fixture in sweep.SOLVER_RUNG_NAMES:
        p_make_by_policy = {}
        for policy in POLICIES:
            p_make_by_seed = []
            for seed in SEEDS:
                record = sweep.run_instrument(
                    binary, fixture=fixture, history="without", seed=seed, mode="count",
                    strategy="double_dummy", policy=policy)
                assert record.fields["error_callback"] == "none", record.fields
                p_make_by_seed.append(float(record.fields["p_make"]))
            p_make_by_policy[policy] = assert_seed_invariant(
                p_make_by_seed, f"{fixture}/{policy} p_make")

        touching, all_optimal = p_make_by_policy["touching"], p_make_by_policy["all_optimal"]
        diff = all_optimal - touching
        print(
            f"\n{fixture}: p_make(touching)={touching:.6f}  p_make(all_optimal)={all_optimal:.6f}  "
            f"diff={diff:+.6f}")

    print(
        "\n  Both solver rungs give p_make=0 under either policy. The scripted\n"
        "  (lowest-legal-card) declarer is not the cause: a declarer that cashes\n"
        "  the side-suit winner first gives the same result. By hand: tricks_needed\n"
        "  is every remaining trick, and the pool holds as many defender winners\n"
        "  as tricks needed, so the defence always wins the trick that clears the\n"
        "  contested suit. These fixtures are built so a tier-1 dead cut can fire\n"
        "  (see fixtures.hpp), so declarer cannot make them against any defence.\n"
        "  So this ladder cannot show an answer divergence. That is a limit of the\n"
        "  only fixtures solve_board accepts, not evidence that none occurs.\n"
        "  Answering it needs a contested position with equal hand counts, which\n"
        "  this ladder does not have.")


def run() -> None:
    binary = sweep.instrument_binary()
    print(f"instrument: {binary}")
    choice_divergence(binary)
    answer_divergence(binary)


if __name__ == "__main__":
    run()
