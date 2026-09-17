"""Shared plumbing for this directory's own sweep scripts: invoke the
built :instrument binary (see instrument.cpp) once per (fixture, history,
seed, ...) combination and parse its "key=value" output into a Python
dict.

This is *reading a C++ instrument's output in Python*, not driving
evaluate() from Python -- every number here was produced by the C++
instrument; this file's only job is to run it several times and make its
output easy to tabulate.

Not a py_binary: no bindings, no bazel py_test wraps it (nothing here is a
test), it is invoked directly with `python3` against an already-built
instrument binary. Build the instrument first:

    bazel build //benchmarks/belief_evaluation:instrument

then run whichever sweep script needs it (e.g. scan_to_hit.py).
"""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path

# The seven rungs fixtures.hpp defines, in the same order all_rungs()
# returns them. Kept here rather than derived, since deriving it would
# mean either parsing fixtures.cpp or adding a --list-fixtures mode to the
# instrument for a one-line convenience -- not worth either yet. A rung
# added there needs one line added here too.
RUNG_NAMES = [
    "pool4",
    "pool5",
    "pool6",
    "pool7",
    "pool8",
    "realistic_a",
    "realistic_b",
]

# The four finesse rungs fixtures.hpp's all_finesse_rungs() defines --
# genuine per-layout uncertainty, unlike RUNG_NAMES above (whose own
# p_make is exactly 1 in every layout by construction). No history form:
# --history is accepted for these but has no effect.
FINESSE_RUNG_NAMES = ["finesse1", "finesse2", "finesse3", "finesse4"]

# The two solver rungs fixtures.hpp's make_solver_rung_a()/_b() define --
# the only fixtures in this directory solve_board() actually accepts (see
# fixtures.hpp's own module doxygen for why every rung above this one
# fails DoubleDummyDefender). No history form.
SOLVER_RUNG_NAMES = ["solver_a", "solver_b"]

HISTORY_FORMS = ["without", "with"]


def bazel_bin_root(compilation_mode: str = "fastbuild") -> Path:
    """The real (non-symlink-name-dependent) bazel-bin directory, via
    `bazel info` -- portable across the convenience symlink's own name,
    which differs between a plain checkout and this project's container
    setup (bazel-bin vs bazel-container-bin, observed directly)."""
    args = ["bazel", "info", "bazel-bin"]
    if compilation_mode == "opt":
        args = ["bazel", "info", "-c", "opt", "bazel-bin"]
    output = subprocess.run(args, check=True, capture_output=True, text=True).stdout
    return Path(output.strip())


def instrument_binary(compilation_mode: str = "fastbuild") -> Path:
    path = bazel_bin_root(compilation_mode) / "benchmarks" / "belief_evaluation" / "instrument"
    if not path.is_file():
        raise AssertionError(
            f"{path} does not exist -- build it first: "
            f"bazel build{' -c opt' if compilation_mode == 'opt' else ''} "
            "//benchmarks/belief_evaluation:instrument")
    return path


_INDEXED_KEY = re.compile(r"^(?P<vector>\w+)\[(?P<index>\d+)\]\.(?P<field>\w+)=(?P<value>.+)$")
_LENGTH_KEY = re.compile(r"^(?P<vector>\w+)\.length=(?P<value>\d+)$")
# --mode=time's own per-trial lines -- "elapsed_ms[0]=1.23", "delta_calls[0]=45",
# "error_callback[0]=..." -- bracketed but with no ".field" after the
# bracket (unlike the per-depth vectors' own "vector[i].field=" shape
# above), so a distinct pattern rather than a special case of _INDEXED_KEY.
_TRIAL_KEY = re.compile(r"^(?P<key>\w+)\[(?P<index>\d+)\]=(?P<value>.+)$")
# [\w.]+, not \w+: covers plain scalars ("seed=7") and dotted summary
# scalars alike ("elapsed_ms.best=1.23") -- checked after _LENGTH_KEY
# above, which is the more specific pattern for a vector's own declared
# length and must win when both could match.
_PLAIN_KEY = re.compile(r"^(?P<key>[\w.]+)=(?P<value>.*)$")


@dataclass
class Record:
    """One instrument invocation's own output, parsed. `fields` carries
    every scalar key=value line (seed, mode, platform, p_make, ...) as
    strings -- callers that want a number convert it themselves, since
    which fields are numeric differs by mode (count vs time). `vectors`
    carries the two per-depth vectors, each a list of per-field dicts; a
    vector's own declared length (`*.length=`) is `len(vectors[name])`,
    preserving the short-tail distinction the instrument's own header
    comment explains -- a depth past this length was never visited, which
    is not the same thing as a depth present with every count at zero.
    `trials` carries --mode=time's own per-trial scalars (elapsed_ms,
    delta_calls, ...), each a list indexed by trial number.
    """

    fields: dict = field(default_factory=dict)
    vectors: dict = field(default_factory=dict)
    trials: dict = field(default_factory=dict)


def parse_output(text: str) -> Record:
    record = Record()
    pending_vector_len: dict[str, int] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        m = _INDEXED_KEY.match(line)
        if m:
            vector = m.group("vector")
            index = int(m.group("index"))
            entries = record.vectors.setdefault(vector, [])
            while len(entries) <= index:
                entries.append({})
            entries[index][m.group("field")] = m.group("value")
            continue
        m = _TRIAL_KEY.match(line)
        if m:
            key = m.group("key")
            index = int(m.group("index"))
            values = record.trials.setdefault(key, [])
            while len(values) <= index:
                values.append(None)
            values[index] = m.group("value")
            continue
        m = _LENGTH_KEY.match(line)
        if m:
            vector = m.group("vector")
            declared = int(m.group("value"))
            pending_vector_len[vector] = declared
            record.vectors.setdefault(vector, [])
            continue
        m = _PLAIN_KEY.match(line)
        if m:
            record.fields[m.group("key")] = m.group("value")
            continue
        raise AssertionError(f"unparseable instrument output line: {line!r}")

    # A vector with a declared length but zero entries printed (every
    # depth's own vector is genuinely empty) would otherwise vanish from
    # `record.vectors` instead of being an explicit empty list -- assert
    # the two agree rather than silently trust one over the other.
    for vector, declared in pending_vector_len.items():
        assert len(record.vectors[vector]) == declared, (
            f"{vector}.length={declared} but {len(record.vectors[vector])} entries were printed")
    return record


def run_instrument(
        binary: Path, *, fixture: str, history: str, seed: int,
        sample_size: int | None = None, scan_budget: int | None = None,
        replenish_below: int | None = None, mode: str = "count", repeat: int | None = None,
        strategy: str = "scripted", tier2: bool = False, policy: str = "touching",
) -> Record:
    assert history in ("with", "without"), history
    assert mode in ("count", "time", "uncut", "divergence"), mode
    assert strategy in ("scripted", "double_dummy"), strategy
    assert policy in ("touching", "all_optimal"), policy
    args = [str(binary), "--fixture", fixture, "--history", history, "--seed", str(seed)]
    if sample_size is not None:
        args += ["--sample-size", str(sample_size)]
    if scan_budget is not None:
        args += ["--scan-budget", str(scan_budget)]
    if replenish_below is not None:
        args += ["--replenish-below", str(replenish_below)]
    if mode != "count":
        args += ["--mode", mode]
    if repeat is not None:
        args += ["--repeat", str(repeat)]
    if strategy != "scripted":
        args += ["--strategy", strategy]
    if tier2:
        args += ["--tier2"]
    if policy != "touching":
        args += ["--policy", policy]
    output = subprocess.run(args, check=True, capture_output=True, text=True).stdout
    return parse_output(output)
