#!/usr/bin/env python3
"""Tier 1's opportunities compounding across a real tree, and whether
tier 2 pays.

Three parts:

  1. Tier 1 made/dead cut rates across the whole ladder (all three
     fixture sets), not from one fixture -- and how much of the tree a
     cut actually removes, via uncut_tree.hpp's own node count
     (--mode=uncut), not merely what fraction of *visited* nodes were
     cuts (a different and less informative number: a cut high in the
     tree removes a whole subtree from ever being visited at all, so a
     per-visited-node rate systematically understates it).
  2. Tier 2's cost against its benefit, on the solver ladder (the only
     fixtures solve_board accepts -- see fixtures.hpp's own module
     doxygen), exhaustive (tier 2's gate is closed under sampling; see
     this task's own background on why that is a fact about the gate,
     not a cost measurement). Cost per bound call against cost per
     defender-node call, re-measured rather than cited.
  3. Whether either measurement contradicts an unconditional "early cuts
     speed things up" claim -- read separately from the docs/specs, not
     computed here.

Usage (after `bazel build //benchmarks/belief_evaluation:instrument`):

    python3 benchmarks/belief_evaluation/cut_rates.py
"""

from __future__ import annotations

import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sweep  # noqa: E402

TIER1_SEEDS = list(range(1, 11))  # 10 seeds per fixture, tier 1 rates
TIER2_SEEDS = list(range(1, 11))  # 10 seeds per solver rung, cost/benefit
TIER2_REPEAT = 5  # --mode=time repeats per (rung, seed, tier2 on/off)


def tier1_rates_and_compounding(binary: Path) -> None:
    print("#" * 78)
    print("# Part 1: tier 1 made/dead cut rates, and the tree fraction a cut removes")
    print("#" * 78)

    rows = []  # (label, N, made_rate, dead_rate, nodes_visited, uncut_nodes, fraction_removed)

    def measure(fixture: str, history: str, report_uncut: bool) -> None:
        made = []
        dead = []
        visited = []
        n = None
        for seed in TIER1_SEEDS:
            record = sweep.run_instrument(binary, fixture=fixture, history=history, seed=seed)
            assert record.fields["error_callback"] == "none", record.fields
            n = int(record.fields["expected_size"])
            visited.append(int(record.fields["nodes_visited"]))
            made.append(int(record.fields["tier1_made_cuts"]))
            dead.append(int(record.fields["tier1_dead_cuts"]))

        made_med, dead_med, visited_med = (
            statistics.median(made), statistics.median(dead), statistics.median(visited))
        made_rate = made_med / visited_med if visited_med else float("nan")
        dead_rate = dead_med / visited_med if visited_med else float("nan")

        # uncut_tree.hpp's own count is not a valid upper bound on every
        # ladder -- confirmed directly, not assumed (see that file's own
        # doxygen): the pool/realistic ladder trips the real evaluator's
        # sibling-subtree revisiting, so their own uncut count can come
        # back *smaller* than what was actually visited. Reporting that
        # as a percentage would be actively misleading, not merely
        # incomplete, so it is not computed at all for those rungs.
        if report_uncut:
            uncut_record = sweep.run_instrument(
                binary, fixture=fixture, history=history, seed=TIER1_SEEDS[0], mode="uncut")
            uncut = uncut_record.fields.get("uncut_nodes")
            uncut_n = int(uncut) if uncut not in (None, "failed") else None
            if uncut_n:
                fraction_removed = 1.0 - (visited_med / uncut_n)
                uncut_str, fraction_str = str(uncut_n), f"{fraction_removed:.1%}"
            else:
                uncut_str, fraction_str = "n/a", "n/a"
        else:
            uncut_str, fraction_str = "not valid here", "not valid here"

        label = f"{fixture}/{history}"
        print(
            f"  {label:24s} N={n:>6}  nodes_visited(med)={visited_med:>6.0f}  "
            f"made_cuts(med)={made_med:>5.0f} ({made_rate:.1%} of visited)  "
            f"dead_cuts(med)={dead_med:>5.0f} ({dead_rate:.1%} of visited)  "
            f"uncut_nodes={uncut_str:>15}  tree_fraction_removed={fraction_str}")
        rows.append((label, n, made_rate, dead_rate, visited_med, uncut_str, fraction_str))

    for fixture in sweep.RUNG_NAMES:
        for history in sweep.HISTORY_FORMS:
            measure(fixture, history, report_uncut=False)
    for fixture in sweep.FINESSE_RUNG_NAMES:
        measure(fixture, "without", report_uncut=True)

    print(
        "\n  The reverted scratch prior this measurement replaces: 4 of 13 nodes\n"
        "  cut (~31%), one fixture, one cut opportunity by construction -- not\n"
        "  comparable to the *rates* above directly (those are per-visited-node,\n"
        "  not tree-fraction-removed), but the tree_fraction_removed column (finesse\n"
        "  rungs only -- see above for why the pool/realistic ladder cannot report\n"
        "  one honestly) is the number that answers the same question that prior\n"
        "  could not: how much of the tree compounding independent cut\n"
        "  opportunities actually removes.")


def solver_rung_n(binary: Path, fixture: str) -> int:
    probe = sweep.run_instrument(binary, fixture=fixture, history="without", seed=1)
    return int(probe.fields["expected_size"])


def tier2_cost_benefit(binary: Path) -> None:
    print("\n" + "#" * 78)
    print("# Part 2: tier 2 cost against benefit, on the solver ladder, exhaustive")
    print("#" * 78)

    for fixture in sweep.SOLVER_RUNG_NAMES:
        n = solver_rung_n(binary, fixture)
        print(f"\n{fixture}  N={n}")

        without_nodes, without_ms, without_delta = [], [], []
        with_nodes, with_ms, with_delta, with_bound = [], [], [], []

        for seed in TIER2_SEEDS:
            count_without = sweep.run_instrument(
                binary, fixture=fixture, history="without", seed=seed, strategy="double_dummy")
            assert count_without.fields["error_callback"] == "none", count_without.fields
            without_nodes.append(int(count_without.fields["nodes_visited"]))
            without_delta.append(int(count_without.fields["delta_calls"]))

            count_with = sweep.run_instrument(
                binary, fixture=fixture, history="without", seed=seed, strategy="double_dummy",
                tier2=True)
            assert count_with.fields["error_callback"] == "none", count_with.fields
            with_nodes.append(int(count_with.fields["nodes_visited"]))
            with_delta.append(int(count_with.fields["delta_calls"]))
            with_bound.append(int(count_with.fields["bound_calls"]))

            time_without = sweep.run_instrument(
                binary, fixture=fixture, history="without", seed=seed, strategy="double_dummy",
                mode="time", repeat=TIER2_REPEAT)
            without_ms.append(float(time_without.fields["elapsed_ms.best"]))

            time_with = sweep.run_instrument(
                binary, fixture=fixture, history="without", seed=seed, strategy="double_dummy",
                tier2=True, mode="time", repeat=TIER2_REPEAT)
            with_ms.append(float(time_with.fields["elapsed_ms.best"]))

        without_nodes_med = statistics.median(without_nodes)
        with_nodes_med = statistics.median(with_nodes)
        without_delta_med = statistics.median(without_delta)
        with_delta_med = statistics.median(with_delta)
        with_bound_med = statistics.median(with_bound)
        without_ms_med = statistics.median(without_ms)
        with_ms_med = statistics.median(with_ms)

        nodes_saved = without_nodes_med - with_nodes_med
        delta_saved = without_delta_med - with_delta_med

        print(
            f"  without tier2: nodes_visited(med)={without_nodes_med:.0f}  "
            f"delta_calls(med)={without_delta_med:.0f}  wall_ms(med)={without_ms_med:.4f}")
        print(
            f"  with tier2:    nodes_visited(med)={with_nodes_med:.0f}  "
            f"delta_calls(med)={with_delta_med:.0f}  bound_calls(med)={with_bound_med:.0f}  "
            f"wall_ms(med)={with_ms_med:.4f}")
        print(
            f"  benefit: {nodes_saved:.0f} fewer nodes visited, {delta_saved:.0f} fewer delta "
            f"calls ({fixture})")

        if with_bound_med > 0:
            # Cost per call, from the wall-clock difference and the extra
            # bound calls that difference bought -- re-measured here, not
            # cited from the earlier scratch figure (~6us bound / ~1.6us
            # defender-node), which this task's own background says not to
            # treat as a baseline.
            extra_ms = with_ms_med - without_ms_med
            us_per_bound_call = 1000.0 * extra_ms / with_bound_med if with_bound_med else float("nan")
            us_per_delta_call = (
                1000.0 * without_ms_med / without_delta_med if without_delta_med else float("nan"))
            print(
                f"  cost: ~{us_per_bound_call:.2f} us/bound_call (from the wall-time delta), "
                f"~{us_per_delta_call:.2f} us/delta_call (without-tier2 baseline)")
        else:
            print("  cost: bound_calls == 0 -- tier 2 never fired here, no cost to attribute")


def run() -> None:
    binary = sweep.instrument_binary()
    print(f"instrument: {binary}")
    tier1_rates_and_compounding(binary)
    tier2_cost_benefit(binary)


if __name__ == "__main__":
    run()
