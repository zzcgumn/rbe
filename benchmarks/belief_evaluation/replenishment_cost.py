#!/usr/bin/env python3
"""What a node-local replenishment scan costs, especially when it fails.

Three parts:

  1. A configuration where every attempt at some depth FAILS, adding
     nothing (SAMPLE_ONLY_FAILS). No test measures this case: in every
     other fixture at least one attempt succeeds.
  2. A configuration where every attempt SUCCEEDS (SAMPLE_ONLY_SUCCEEDS),
     for the matching cost.
  3. Delta calls and wall time with replenish_below set against without
     it (same fixture, seed and sample_size), on three rungs. This is a
     whole-configuration delta: the two runs also differ in source
     scanning, node expansion and every callback, so it is not the cost of
     replenishment scans alone. Every comparison is the median of paired
     per-seed ratios, not a ratio of two medians (the median is not
     linear).

Usage (after `bazel build //benchmarks/belief_evaluation:instrument`):

    python3 benchmarks/belief_evaluation/replenishment_cost.py
"""

from __future__ import annotations

import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sweep  # noqa: E402

# Found by trial against the instrument, not derived. finesse1 (N=6) with
# sample_size = replenish_below = 5 leaves one layout undrawn after the
# root sample, so every attempt below the root finds nothing new
# (SourceExhausted). pool8 (N=12,870) with a small sample_size and
# replenish_below is the mirror case: fresh candidates are never scarce,
# so every attempt succeeds.
SAMPLE_ONLY_FAILS = dict(
    fixture="finesse1", history="without", sample_size=5, replenish_below=5, scan_budget=500)
SAMPLE_ONLY_SUCCEEDS = dict(
    fixture="pool8", history="without", sample_size=10, replenish_below=8, scan_budget=5000)

WALL_TIME_SEEDS = list(range(1, 11))  # 10 seeds, best-of-3 repeats each
WALL_TIME_REPEAT = 3
# Three rungs: two at the scale the scan-to-hit sweep uses (a sizeable
# fraction of a large N), and one small (finesse2, sample_size well under
# N). The small one shows whether the cost grows with scale or is a fixed
# multiplier. It grows with scale; see the report.
WALL_TIME_RUNGS = [
    dict(fixture="pool7", history="without", sample_size=343, scan_budget=20_000),
    dict(fixture="finesse3", history="without", sample_size=204, scan_budget=20_000),
    dict(fixture="finesse2", history="without", sample_size=30, scan_budget=2_000),
]
# replenish_below as a fraction of sample_size. Swept, because one point
# cannot tell whether replenishment is expensive in general or only at an
# aggressive threshold.
WALL_TIME_REPLENISH_FRACTIONS = [0.10, 0.25, 0.50]


def print_scan_cost(binary: Path, label: str, config: dict) -> None:
    print(f"\n--- {label}: fixture={config['fixture']} {config} ---")
    record = sweep.run_instrument(binary, seed=1, **config)
    assert record.fields["error_callback"] == "none", record.fields
    print(f"  delta_calls={record.fields['delta_calls']}  p_make={record.fields['p_make']}")
    for depth, stats in enumerate(record.vectors["replenishment_by_depth"]):
        attempted, succeeded = int(stats["attempted"]), int(stats["succeeded"])
        if attempted == 0:
            continue
        at_calls, layouts_added = int(stats["at_calls"]), int(stats["layouts_added"])
        failed = attempted - succeeded
        print(
            f"  depth {depth}: attempted={attempted} succeeded={succeeded} failed={failed} "
            f"at_calls={at_calls} layouts_added={layouts_added}", end="")
        if attempted:
            print(f"  ({at_calls / attempted:.1f} at_calls/attempt)", end="")
        print()


def wall_time_and_delta(binary: Path, fixture: str, history: str, seed: int, sample_size: int,
                         scan_budget: int, replenish_below: int | None) -> tuple[float, int]:
    record = sweep.run_instrument(
        binary, fixture=fixture, history=history, seed=seed, sample_size=sample_size,
        scan_budget=scan_budget, replenish_below=replenish_below, mode="time",
        repeat=WALL_TIME_REPEAT)
    best_ms = float(record.fields["elapsed_ms.best"])
    # The delta_calls of the trial that gave elapsed_ms.best, not an
    # average. Both must describe the same run for a share of wall time to
    # mean anything.
    trials_ms = [float(x) for x in record.trials["elapsed_ms"]]
    best_index = trials_ms.index(min(trials_ms))
    delta_calls = int(record.trials["delta_calls"][best_index])
    return best_ms, delta_calls


def run_wall_time_comparison(binary: Path) -> None:
    print("\n" + "=" * 78)
    print("# With replenish_below set, against without -- same fixture, same seed,")
    print("# swept across what fraction of sample_size replenish_below is set to")
    print("=" * 78)

    summary = []  # (fixture, fraction, delta_pct, time_multiplier, time_share)

    for rung in WALL_TIME_RUNGS:
        fixture, history = rung["fixture"], rung["history"]
        sample_size, scan_budget = rung["sample_size"], rung["scan_budget"]
        print(f"\n{fixture} (history={history}) sample_size={sample_size} scan_budget={scan_budget}")

        without_ms, without_delta = [], []
        for seed in WALL_TIME_SEEDS:
            ms, delta = wall_time_and_delta(
                binary, fixture, history, seed, sample_size, scan_budget, replenish_below=None)
            without_ms.append(ms)
            without_delta.append(delta)
        without_ms_med = statistics.median(without_ms)
        without_delta_med = statistics.median(without_delta)
        print(f"  without replenishment: delta_calls={without_delta_med:.0f}  wall_ms={without_ms_med:.4f}")

        for fraction in WALL_TIME_REPLENISH_FRACTIONS:
            replenish_below = max(1, round(fraction * sample_size))
            with_ms, with_delta = [], []
            for seed in WALL_TIME_SEEDS:
                ms, delta = wall_time_and_delta(
                    binary, fixture, history, seed, sample_size, scan_budget,
                    replenish_below=replenish_below)
                with_ms.append(ms)
                with_delta.append(delta)
            with_ms_med = statistics.median(with_ms)
            with_delta_med = statistics.median(with_delta)

            # The median of paired per-seed ratios and percentages. The
            # lists are aligned by seed (WALL_TIME_SEEDS drives both loops),
            # and the median is not linear. Sampling makes the delta counts
            # vary by seed here, so pairing matters, unlike on the
            # exhaustive solver ladder in cut_rates.py.
            delta_pct = statistics.median(
                100.0 * (w - wo) / wo for w, wo in zip(with_delta, without_delta))
            time_multiplier = statistics.median(w / wo for w, wo in zip(with_ms, without_ms))
            time_share = statistics.median(100.0 * (w - wo) / w for w, wo in zip(with_ms, without_ms))

            print(
                f"  replenish_below={replenish_below} ({fraction:.0%} of sample_size): "
                f"delta_calls={with_delta_med:.0f} ({delta_pct:+.0f}%)  "
                f"wall_ms={with_ms_med:.4f} ({time_multiplier:.2f}x; the with/without "
                f"delta is {time_share:.0f}% of the with-replenishment total, not isolated "
                f"scan time; see the module docstring)")

            summary.append((fixture, fraction, delta_pct, time_multiplier, time_share))

    print("\n--- summary ---")
    for fixture, fraction, delta_pct, multiplier, share in summary:
        print(
            f"  {fixture} @ {fraction:.0%}: delta_calls {delta_pct:+.0f}%, "
            f"wall time {multiplier:.2f}x, {share:.0f}% of total")


def run() -> None:
    binary = sweep.instrument_binary()
    print(f"instrument: {binary}")

    print_scan_cost(binary, "every attempt FAILS", SAMPLE_ONLY_FAILS)
    print_scan_cost(binary, "every attempt SUCCEEDS", SAMPLE_ONLY_SUCCEEDS)

    run_wall_time_comparison(binary)


if __name__ == "__main__":
    run()
