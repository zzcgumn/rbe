#!/usr/bin/env python3
"""What a node-local replenishment scan costs, especially when it fails.

Four parts:

  1. A configuration built to make every attempt at some depth FAIL --
     add nothing (SAMPLE_ONLY_FAILS below) -- since no committed test
     happens to measure this case (every existing fixture has at least
     one attempt succeed, by accident of what those fixtures needed, not
     by design).
  2. A configuration built to make every attempt SUCCEED
     (SAMPLE_ONLY_SUCCEEDS below), for the matching successful-cost
     number.
  3. delta call counts and wall clock, WITH replenish_below set against
     WITHOUT it (same fixture, same seed, same sample_size), on two
     rungs -- replenishment's own share of total cost.
  4. Comparison against a prior figure (delta calls up 63%, wall time
     roughly tripled, replenishment ~65% of total wall time) that exists
     only as a reverted scratch measurement upstream of this plan and
     cannot be cited as a baseline -- treated here as a prior to test.

Usage (after `bazel build //benchmarks/belief_evaluation:instrument`):

    python3 benchmarks/belief_evaluation/replenishment_cost.py
"""

from __future__ import annotations

import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sweep  # noqa: E402

# Found by direct trial against the instrument (not derived analytically):
# finesse1 (N=6) with sample_size=replenish_below=5 leaves exactly one
# layout never yet drawn after the root's own initial sample, so every
# node-local attempt below the root finds nothing new -- SourceExhausted
# by construction, not by chance. pool8 (N=12,870) with a sample_size and
# replenish_below both small relative to N is the mirror case: fresh
# candidates are never scarce, so every attempt succeeds.
SAMPLE_ONLY_FAILS = dict(
    fixture="finesse1", history="without", sample_size=5, replenish_below=5, scan_budget=500)
SAMPLE_ONLY_SUCCEEDS = dict(
    fixture="pool8", history="without", sample_size=10, replenish_below=8, scan_budget=5000)

WALL_TIME_SEEDS = list(range(1, 11))  # 10 seeds, best-of-3 repeats each
WALL_TIME_REPEAT = 3
# Three rungs, not one: two at the scale the scan-to-hit sweep already
# uses (a meaningful fraction of a large N), one deliberately small
# (finesse2, sample_size well under N) -- added after the first two came
# back far above the reverted scratch prior at every fraction tried, to
# check whether that gap tracks *scale* rather than being a fixed
# multiplier. It does -- see the report.
WALL_TIME_RUNGS = [
    dict(fixture="pool7", history="without", sample_size=343, scan_budget=20_000),
    dict(fixture="finesse3", history="without", sample_size=204, scan_budget=20_000),
    dict(fixture="finesse2", history="without", sample_size=30, scan_budget=2_000),
]
# replenish_below as a fraction of sample_size -- swept rather than fixed
# at one point, because the first run (fraction=0.5 only) came back far
# above the reverted scratch prior (delta calls +2918%/+403%, not +63%;
# wall time 159x/8x, not ~3x) and a single point cannot say whether that
# is because replenish_below itself is this expensive in general or
# because 0.5 is simply a much more aggressive threshold than whatever
# the scratch measurement used (its own parameters were never recorded).
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
    # The trial matching elapsed_ms.best's own delta_calls, not an
    # average across trials -- the two must describe the *same* run for
    # "replenishment's share of wall time" to mean anything.
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

            delta_pct = 100.0 * (with_delta_med - without_delta_med) / without_delta_med
            time_multiplier = with_ms_med / without_ms_med
            time_share = 100.0 * (with_ms_med - without_ms_med) / with_ms_med

            print(
                f"  replenish_below={replenish_below} ({fraction:.0%} of sample_size): "
                f"delta_calls={with_delta_med:.0f} ({delta_pct:+.0f}%)  "
                f"wall_ms={with_ms_med:.4f} ({time_multiplier:.2f}x, "
                f"replenishment is {time_share:.0f}% of the with-replenishment total)")

            summary.append((fixture, fraction, delta_pct, time_multiplier, time_share))

    print("\n--- against the prior (delta calls +63%, wall time ~3x, ~65% of total) ---")
    print("  (the prior's own replenish_below fraction of sample_size was never recorded,")
    print("   so read this as a range the prior sits inside, not a single point to match)")
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
