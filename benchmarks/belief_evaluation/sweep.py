"""Shared plumbing for the sweep scripts here. It runs the built
:instrument binary (see instrument.cpp) once per (fixture, history, seed,
...) combination and parses its "key=value" output into a dict.

Python only reads the instrument's output; it does not drive evaluate().
Every number comes from the C++ instrument.

Not a py_binary and not a test. Run the scripts with `python3` against an
already-built instrument. Build it first:

    bazel build //benchmarks/belief_evaluation:instrument

then run a sweep script, such as scan_to_hit.py.
"""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path

# The seven rungs fixtures.hpp defines, in all_rungs() order. Listed, not
# derived: deriving them would need a parser for fixtures.cpp or a
# --list-fixtures mode. A new rung needs a line here too.
RUNG_NAMES = [
    "pool4",
    "pool5",
    "pool6",
    "pool7",
    "pool8",
    "realistic_a",
    "realistic_b",
]

# The four finesse rungs from all_finesse_rungs() in fixtures.hpp. They have
# real per-layout uncertainty, unlike RUNG_NAMES, whose p_make is exactly 1
# in every layout. No history form: --history is accepted but has no
# effect.
FINESSE_RUNG_NAMES = ["finesse1", "finesse2", "finesse3", "finesse4"]

# The two solver rungs (make_solver_rung_a and _b in fixtures.hpp), the only
# fixtures here that solve_board() accepts (see fixtures.hpp). No history
# form.
SOLVER_RUNG_NAMES = ["solver_a", "solver_b"]

HISTORY_FORMS = ["without", "with"]


def bazel_bin_root(compilation_mode: str = "fastbuild") -> Path:
    """The real bazel-bin directory, from `bazel info`. The convenience
    symlink's name differs between a plain checkout and the container setup
    (bazel-bin against bazel-container-bin).

    Uses `bazelisk`, not bare `bazel`: the repository pins a Bazel version
    in `.bazelversion`, and a bare `bazel` on the PATH can be any version.
    CI enforces this for workflow YAML (python/tests/ci_bazelisk_test.py);
    the same reasoning applies to a local script."""
    args = ["bazelisk", "info", "bazel-bin"]
    if compilation_mode == "opt":
        args = ["bazelisk", "info", "-c", "opt", "bazel-bin"]
    output = subprocess.run(args, check=True, capture_output=True, text=True).stdout
    return Path(output.strip())


def instrument_binary(compilation_mode: str = "fastbuild") -> Path:
    # Bazel names a cc_binary with no suffix on Linux and macOS, and with
    # .exe on Windows. Prefer the bare name. The same lookup as in
    # test_belief_space_local_evaluation_parity.py.
    root = bazel_bin_root(compilation_mode) / "benchmarks" / "belief_evaluation" / "instrument"
    for candidate in (root, root.with_suffix(".exe")):
        if candidate.is_file():
            return candidate
    raise AssertionError(
        f"{root} (or {root}.exe) does not exist -- build it first: "
        f"bazel build{' -c opt' if compilation_mode == 'opt' else ''} "
        "//benchmarks/belief_evaluation:instrument")


_INDEXED_KEY = re.compile(r"^(?P<vector>\w+)\[(?P<index>\d+)\]\.(?P<field>\w+)=(?P<value>.+)$")
_LENGTH_KEY = re.compile(r"^(?P<vector>\w+)\.length=(?P<value>\d+)$")
# --mode=time per-trial lines ("elapsed_ms[0]=1.23", "delta_calls[0]=45",
# "error_callback[0]=..."). They have a bracket but no ".field", unlike the
# per-depth vectors, so they get their own pattern.
_TRIAL_KEY = re.compile(r"^(?P<key>\w+)\[(?P<index>\d+)\]=(?P<value>.+)$")
# [\w.]+, not \w+, to match plain scalars ("seed=7") and dotted summary
# scalars ("elapsed_ms.best=1.23"). Checked after _LENGTH_KEY, which is
# more specific and must win when both match.
_PLAIN_KEY = re.compile(r"^(?P<key>[\w.]+)=(?P<value>.*)$")


@dataclass
class Record:
    """One instrument run, parsed.

    - `fields`: every scalar key=value line (seed, mode, platform, p_make,
      ...) as a string. Callers convert numbers, since which fields are
      numeric differs by mode.
    - `vectors`: the two per-depth vectors, each a list of per-field dicts.
      `len(vectors[name])` is the declared length (`*.length=`). A depth
      past it was never visited, which differs from a depth with every
      count at zero (see instrument.cpp).
    - `trials`: --mode=time per-trial scalars (elapsed_ms, delta_calls,
      ...), each a list indexed by trial.
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

    # A vector with a declared length but no printed entries would otherwise
    # be missing from `record.vectors`, not an empty list. Assert that the
    # declared and printed lengths agree.
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
