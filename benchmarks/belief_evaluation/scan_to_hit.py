#!/usr/bin/env python3
"""Scan-to-hit: at() calls per accepted layout at replenishment, by depth,
against belief-space size N. sweep.py runs the C++ instrument and parses
its output; this file adds the sweeps and the ratio.

Usage (after `bazel build //benchmarks/belief_evaluation:instrument`):

    python3 benchmarks/belief_evaluation/scan_to_hit.py

Two sweeps, because a fixed sample_size and one scaled to N answer
different questions:

  - FIXED: sample_size and replenish_below are constant across the
    ladder. This is the realistic case, where a caller picks one practical
    sample size whatever the position. It reads "against N" directly.
  - SCALED: sample_size is a fixed fraction of each rung's N. This is the
    fair comparison for the with/without-history question; see "Why sweep
    2 exists" at the end of the output.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sweep  # noqa: E402

SEEDS = list(range(1, 21))


def sum_replenishment(records: list[sweep.Record]) -> list[dict]:
    """Raw counts summed over every seed, depth by depth. A ratio cannot
    be summed across runs, so the counts are summed first and the ratio is
    formed once.

    Vectors can differ in length across seeds. A seed whose sample hits
    tier-1 cuts earlier reaches a shallower depth (seen on the largest
    with-history rungs). A seed that never reached a depth contributes
    nothing there, as a zero entry would, so a short vector counts as zero
    past its end.
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
        # Every attempt at this depth ran and found nothing, so scan-to-hit
        # is infinite: not zero, and not at_calls.
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

    # --- sweep 2: sample_size scaled to 20% of each rung's N ------------
    print("\n\n" + "#" * 78)
    print("# Sweep 2: sample_size = 20% of N (same fraction at every rung)")
    print("#" * 78)
    for history in sweep.HISTORY_FORMS:
        print(f"\n=== history={history} ===")
        rows = []
        for fixture in sweep.RUNG_NAMES:
            probe = sweep.run_instrument(binary, fixture=fixture, history=history, seed=SEEDS[0])
            n_probe = int(probe.fields["expected_size"])
            # max(4, ...) guards against a degenerate 1-to-3-layout sample
            # at the smallest rungs. So the actual fraction can exceed 20%
            # there (pool4 with history, N=15: round(0.2*15)=3, raised to
            # 4, which is 26.7%). That breaks the matched-fraction
            # comparison at the rung the report leans on most, so the
            # actual fraction is printed below.
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
        "\n\nWhy sweep 2 exists: sweep 1 fixes sample_size while N varies over two orders of\n"
        "magnitude, so a small (with-history) space is sampled at a much higher fraction.\n"
        "Collision pressure during replenishment (a mostly drawn small space runs out of\n"
        "fresh candidates sooner) then confounds any effect of the history. Sweep 2 fixes\n"
        "the fraction instead, the fairer test of whether a history reduces scan-to-hit.\n"
        "At the smallest rungs the sample-size floor moves the fraction a few points off\n"
        "20%; a note above marks each such row.")


if __name__ == "__main__":
    run()
