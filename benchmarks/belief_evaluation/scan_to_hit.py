#!/usr/bin/env python3
"""Scan-to-hit: at() calls per accepted layout at replenishment, by depth,
plotted against belief-space size N -- the headline acceptance number this
benchmarking effort exists to produce. Reads sweep.py's own doxygen for the
mechanism (a C++ instrument, run several times, output parsed) -- this
file only adds the specific sweeps and the ratio.

Usage (after `bazel build //benchmarks/belief_evaluation:instrument`):

    python3 benchmarks/belief_evaluation/scan_to_hit.py

Two sweeps, not one, because a single fixed sample_size across every rung
answers a different question than a sample_size scaled with each rung's
own N:

  - FIXED: sample_size/replenish_below held constant across the whole
    ladder. This is the operationally realistic regime (a caller who
    picks one practical sample size regardless of the position's own
    space) and it is what "plotted against N" means most directly.
  - SCALED: sample_size a fixed *fraction* of each rung's own N. This is
    the fair comparison for the with/without-history question -- see
    "why FIXED alone cannot answer it" below, discovered while building
    this sweep, not assumed going in.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sweep  # noqa: E402

SEEDS = list(range(1, 21))


def sum_replenishment(records: list[sweep.Record]) -> list[dict]:
    """Raw counts summed across every seed's own run, depth by depth --
    decision 3's own discipline (a ratio cannot be summed across runs, so
    sum the counts first and form the ratio once, here, not per seed and
    then averaged).

    Not assumed to be the same length across seeds: a seed whose own
    sample happens to hit tier-1 cuts earlier reaches a shallower maximum
    depth than another seed of the *same* fixture and options -- observed
    directly on the largest with-history rungs below. A seed that never
    reached a given depth contributes nothing there, the same thing
    summing a present-but-zero entry would do -- so a short vector is
    treated as implicitly zero past its own end, not as an error.
    """
    length = max(len(record.vectors["replenishment_by_depth"]) for record in records)
    totals = [
        {"attempted": 0, "succeeded": 0, "layouts_added": 0, "at_calls": 0, "seeds_reaching": 0}
        for _ in range(length)
    ]
    for record in records:
        for depth, entry in enumerate(record.vectors["replenishment_by_depth"]):
            for key in ("attempted", "succeeded", "layouts_added", "at_calls"):
                totals[depth][key] += int(entry[key])
            totals[depth]["seeds_reaching"] += 1
    return totals


def scan_to_hit_ratio(attempted: int, at_calls: int, layouts_added: int) -> str:
    if attempted == 0:
        return "n/a (never attempted)"
    if layouts_added == 0:
        # The trap the task's own background names: a scan to hit of
        # infinity, not of zero and not of at_calls -- every attempt at
        # this depth ran and found nothing.
        return f"inf ({at_calls} at() calls, 0 added, {attempted} attempt(s))"
    return f"{at_calls / layouts_added:.2f}"


def sweep_rung(binary: Path, fixture: str, history: str, sample_size: int, replenish_below: int,
               scan_budget: int) -> tuple[int, list[dict]]:
    records = [
        sweep.run_instrument(
            binary, fixture=fixture, history=history, seed=seed, sample_size=sample_size,
            scan_budget=scan_budget, replenish_below=replenish_below)
        for seed in SEEDS
    ]
    n = int(records[0].fields["expected_size"])
    for record in records:
        assert int(record.fields["expected_size"]) == n, fixture
        assert record.fields["error_callback"] == "none", (fixture, record.fields)
    return n, sum_replenishment(records)


def print_table(rows: list[tuple[str, int, list[dict]]]) -> None:
    rows = sorted(rows, key=lambda row: row[1])  # ascending N
    for fixture, n, totals in rows:
        print(f"\n{fixture}  N={n}")
        print(
            f"  {'depth':>5}  {'seeds':>6}  {'attempted':>9}  {'succeeded':>9}  {'at_calls':>10}  "
            "scan-to-hit")
        for depth, stats in enumerate(totals):
            print(
                f"  {depth:>5}  {stats['seeds_reaching']:>3}/{len(SEEDS):<2}  "
                f"{stats['attempted']:>9}  {stats['succeeded']:>9}  {stats['at_calls']:>10}  "
                f"{scan_to_hit_ratio(stats['attempted'], stats['at_calls'], stats['layouts_added'])}")


def run() -> None:
    binary = sweep.instrument_binary()
    print(f"instrument: {binary}")
    print(f"seeds={SEEDS[0]}..{SEEDS[-1]} ({len(SEEDS)} seeds)\n")

    # --- sweep 1: sample_size fixed across the whole ladder -------------
    print("#" * 78)
    print("# Sweep 1: sample_size=5, replenish_below=3 fixed across the ladder")
    print("#" * 78)
    for history in sweep.HISTORY_FORMS:
        print(f"\n=== history={history} ===")
        rows = [
            (fixture, *sweep_rung(binary, fixture, history, sample_size=5, replenish_below=3,
                                   scan_budget=2000))
            for fixture in sweep.RUNG_NAMES
        ]
        print_table(rows)

    # --- sweep 2: sample_size scaled to 20% of each rung's own N --------
    print("\n\n" + "#" * 78)
    print("# Sweep 2: sample_size = 20% of N (same fraction at every rung)")
    print("#" * 78)
    for history in sweep.HISTORY_FORMS:
        print(f"\n=== history={history} ===")
        rows = []
        for fixture in sweep.RUNG_NAMES:
            probe = sweep.run_instrument(binary, fixture=fixture, history=history, seed=SEEDS[0])
            n_probe = int(probe.fields["expected_size"])
            # max(4, ...): a floor against a degenerate 1-to-3-layout
            # sample at the smallest rungs, not a cosmetic minimum -- but
            # it means the actual sampled fraction can exceed the
            # requested 20% at the smallest ones (pool4-with-history,
            # N=15: round(0.2*15)=3, floored to 4 -- 26.7%, not 20%),
            # which breaks the "matched fraction" comparison specifically
            # at the rung this sweep's own report leans on hardest.
            # Printed explicitly below so a reader of this fixture's own
            # row never has to take "20%" on faith -- found directly (not
            # assumed) when a cold review asked why the with/without
            # comparison at N=15 was not actually matched.
            sample_size = max(4, round(0.2 * n_probe))
            replenish_below = max(2, sample_size // 2)
            actual_fraction = sample_size / n_probe
            if abs(actual_fraction - 0.20) > 0.005:
                print(
                    f"  {fixture}: sample_size={sample_size} is {actual_fraction:.1%} of N={n_probe}, "
                    "not the requested 20% (floored up from a smaller value)")
            rows.append((
                fixture,
                *sweep_rung(
                    binary, fixture, history, sample_size=sample_size,
                    replenish_below=replenish_below, scan_budget=50_000)))
        print_table(rows)

    print(
        "\n\nWhy sweep 2 exists: sweep 1 holds sample_size fixed at an absolute value while N\n"
        "varies over two orders of magnitude, so a smaller (with-history) space is sampled at a\n"
        "much *higher fraction* than a larger one -- collision pressure during replenishment (an\n"
        "already-mostly-drawn small space runs out of fresh candidates faster) confounds any\n"
        "effect of history's own narrowing. Sweep 2 removes that confound by holding the\n"
        "*fraction* fixed instead -- the fairer comparison for whether a play history reduces\n"
        "scan-to-hit -- except at the smallest rung or two, where the same sample-size floor\n"
        "sweep 1 needed reappears in a smaller way (a few points off 20%, not sweep 1's own\n"
        "multiple-of-N-away confound); see this sweep's own printed note above wherever that\n"
        "happens, rather than assuming every row hit 20% exactly.")


if __name__ == "__main__":
    run()
