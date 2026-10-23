#!/usr/bin/env python3
"""The spread of the sampled estimator across seeds, at several sample
sizes M, against the exhaustive answer on the same position. The
exhaustive answer is computed in this run, never stored: a fixture edited
later would invalidate a stored constant.

Runs on sweep.FINESSE_RUNG_NAMES, not sweep.RUNG_NAMES. On the pool and
realistic ladders p_make is exactly 1 in every layout, because declarer
holds a whole suit, so there is nothing to converge on. The finesse ladder
(make_finesse_rung in fixtures.hpp) has real layout-dependent uncertainty
at four sizes.

It reports where the exhaustive answer sits relative to the spread first,
and the width second. A tight spread that misses the true answer is a
bias, not a precision problem, and it is the one symptom of a badly
shuffled layout source. If this sweep finds one, it prints **BIAS FOUND**
rather than a normal-looking table.

SEEDS_BY_FRACTION was fixed before the first run on real data and has not
changed since. It gives more seeds at small M, where the spread is widest.
Seeds are never chosen after seeing how they behave.

Usage (after `bazel build //benchmarks/belief_evaluation:instrument`):

    python3 benchmarks/belief_evaluation/convergence.py
"""

from __future__ import annotations

import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sweep  # noqa: E402

# M as a fraction of each rung's N, and the seeds run at each. Fixed in
# advance, with more seeds where the spread is widest (small M).
SEEDS_BY_FRACTION = {
    0.10: list(range(1, 31)),  # 30 seeds
    0.25: list(range(1, 26)),  # 25 seeds
    0.50: list(range(1, 21)),  # 20 seeds
    0.75: list(range(1, 16)),  # 15 seeds
}

SCAN_BUDGET_MARGIN = 1000  # added to M so the root scan is never budget-bound


def true_p_make(binary: Path, fixture: str) -> tuple[float, list[dict]]:
    """The exhaustive answer, and layout_min by depth from the same
    unsampled run. Computed here, not cited."""
    record = sweep.run_instrument(binary, fixture=fixture, history="without", seed=1)
    assert record.fields["error_callback"] == "none", (fixture, record.fields)
    return float(record.fields["p_make"]), record.vectors["sample_size_by_depth"]


def sample_at(binary: Path, fixture: str, m: int, seed: int) -> tuple[float, list[dict]]:
    record = sweep.run_instrument(
        binary, fixture=fixture, history="without", seed=seed, sample_size=m,
        scan_budget=m + SCAN_BUDGET_MARGIN)
    assert record.fields["error_callback"] == "none", (fixture, seed, m, record.fields)
    return float(record.fields["p_make"]), record.vectors["sample_size_by_depth"]


def min_layout_min(depth_stats: list[dict]) -> int | None:
    values = [int(d["layout_min"]) for d in depth_stats if int(d["nodes"]) > 0]
    return min(values) if values else None


def run() -> None:
    binary = sweep.instrument_binary()
    print(f"instrument: {binary}")
    print(
        "seed lists by fraction: "
        f"{ {k: (v[0], v[-1], len(v)) for k, v in SEEDS_BY_FRACTION.items()} }\n")

    bias_found = []

    for fixture in sweep.FINESSE_RUNG_NAMES:
        true_value, true_depth_stats = true_p_make(binary, fixture)
        print(f"\n{fixture}  exhaustive p_make={true_value:.6f}")
        print(f"  exhaustive layout_min (min over depths): {min_layout_min(true_depth_stats)}")

        for fraction, seeds in SEEDS_BY_FRACTION.items():
            probe = sweep.run_instrument(binary, fixture=fixture, history="without", seed=seeds[0])
            n = int(probe.fields["expected_size"])
            # max(3, ...) guards against a degenerate 1- or 2-layout
            # sample at the smallest fixtures. So the requested and actual
            # fractions can differ: for finesse1 (N=6), every fraction from
            # 10% to 50% gives M=3. Each line shows the actual fraction, so
            # it never misstates what ran.
            requested_m = round(fraction * n)
            m = max(3, requested_m)
            actual_fraction = m / n
            floored_note = "" if m == requested_m else f", requested {fraction:.0%}={requested_m} floored up"
            if m >= n:
                print(
                    f"  M={m} (={actual_fraction:.0%} of N={n}{floored_note}) >= N -- skipped, "
                    "covered by the guard's M>=N check")
                continue

            values = []
            layout_mins = []
            for seed in seeds:
                p, depth_stats = sample_at(binary, fixture, m, seed)
                values.append(p)
                lm = min_layout_min(depth_stats)
                if lm is not None:
                    layout_mins.append(lm)

            lo, hi = min(values), max(values)
            spread = hi - lo
            mean = statistics.fmean(values)
            stdev = statistics.pstdev(values)
            inside = lo <= true_value <= hi
            worst_layout_min = min(layout_mins) if layout_mins else None

            status = "true value INSIDE spread" if inside else "*** true value OUTSIDE spread ***"
            print(
                f"  M={m:>6} (={actual_fraction:.0%} of N={n}{floored_note})  seeds={len(seeds):>2}  "
                f"range=[{lo:.4f}, {hi:.4f}]  spread={spread:.4f}  mean={mean:.4f}  "
                f"stdev={stdev:.4f}  worst layout_min={worst_layout_min}  -- {status}")

            if spread < 0.02 and not inside:
                bias_found.append((fixture, m, lo, hi, true_value))

    print("\n" + "=" * 78)
    if bias_found:
        print("*** BIAS FOUND: a tight spread missed the true answer ***")
        for fixture, m, lo, hi, true_value in bias_found:
            print(f"  {fixture} M={m}: spread=[{lo:.4f}, {hi:.4f}], true={true_value:.4f}")
        print("STOP: report this before measuring anything else.")
    else:
        print("No tight-spread-misses-truth case observed in this sweep.")


if __name__ == "__main__":
    run()
