#!/usr/bin/env python3
"""How often SpreadPolicy::TouchingSequence and ::AllOptimal (spread.hpp)
actually diverge, outside a fixture built to make them -- and, where they
do, whether that changes the final `p_make`.

Two numbers, not one (this task's own background says why they are
different questions):

  1. **Choice divergence**: at a real defender node, do the two policies'
     own candidate-card sets differ? Measured directly, one solve_board
     call per node (see instrument.cpp's counting_divergence_defender --
     TouchingSequence's set is always a subset of AllOptimal's, so a
     second solve is never needed to compare them).
  2. **Answer divergence**: does the choice change the root's own final
     `p_make`? Measured by running the *real* DoubleDummyDefender under
     each policy separately (--mode=count) and diffing the two runs'
     p_make.

Both need a real solve_board call, so both are restricted to the solver
ladder -- the only fixtures solve_board accepts (see fixtures.hpp's own
module doxygen; the same restriction cut_rates.py's own tier2_cost_benefit
ran into). No new fixture, no new instrumentation beyond instrument.cpp's
own --mode=divergence and --policy -- this task's own background says both
are meant to be cheap.

Usage (after `bazel build //benchmarks/belief_evaluation:instrument`):

    python3 benchmarks/belief_evaluation/divergence.py
"""

from __future__ import annotations

import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sweep  # noqa: E402

# Exhaustive fixtures: confirmed directly (three seeds, both rungs, both
# --mode=divergence and --mode=count) that every number below is bitwise
# identical across seeds, as an exhaustive run must be -- the source order
# a seed picks never changes which nodes the tree visits when every layout
# is included. Several seeds are still run and their values compared
# rather than assumed equal, so that invariance claim stays checked, not
# merely stated.
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
        "\n  Both solver rungs come back p_make=0 under either policy -- confirmed\n"
        "  this is not a defect of the scripted (lowest-legal-card) declarer used\n"
        "  throughout this ladder, not a property of the defender's own choice:\n"
        "  a second, temporary declarer script (cash the side-suit winner before\n"
        "  touching the contested suit, never committed -- see this task's own\n"
        "  write-up) gave the identical result. Worked through by hand as well: with\n"
        "  tricks_needed set to *every* remaining trick and exactly as many\n"
        "  defender winners in the pool as tricks needed, the single trick that\n"
        "  clears the contested suit always goes to the defence, whichever hand\n"
        "  leads it and in whichever order -- these two fixtures are, by\n"
        "  construction (see fixtures.hpp's own comment: built so a tier-1 dead cut\n"
        "  can fire), games declarer cannot win against *any* defence, not only the\n"
        "  two compared here. So this ladder cannot show a nonzero answer\n"
        "  divergence -- not evidence that one never occurs, a scope limit of the\n"
        "  only fixtures solve_board accepts. A fixture that could answer this needs\n"
        "  a genuinely contested position under equal hand counts, which is a new\n"
        "  fixture this task's own background rules out building.")


def run() -> None:
    binary = sweep.instrument_binary()
    print(f"instrument: {binary}")
    choice_divergence(binary)
    answer_divergence(binary)


if __name__ == "__main__":
    run()
