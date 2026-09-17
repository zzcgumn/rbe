"""Runs timer_cpp and timer_py and tabulates the ratio between them --
this task's own "ships, and is re-runnable" measurement, one command.

    bazel run //benchmarks/python_cost:report [-- ITERATIONS] [-- --label LABEL]

`--label` is cosmetic only (e.g. "default" or "opt") -- this process has
no reliable way to ask "was I built with -c opt" from the Python side
(unlike timer.cpp's own NDEBUG check), since -c opt changes the compiled
extension's own optimisation level, not anything CPython exposes to a
running script. Run this target twice, once per build mode, passing
--label each time; see this task's own write-up for both runs' output.
"""

from __future__ import annotations

import argparse
import subprocess
import sys

from python.runfiles import runfiles


def _rlocation(path: str) -> str:
    r = runfiles.Create()
    if r is None:
        raise AssertionError("no runfiles environment -- run this via `bazel run`, not `python3` directly")
    located = r.Rlocation(f"_main/{path}")
    if located is None:
        raise AssertionError(f"{path} not found in runfiles -- check this target's own `data` deps")
    return located


def _parse(output: str) -> list[dict[str, str]]:
    records = []
    for line in output.splitlines():
        if not line.strip():
            continue
        fields = {}
        for token in line.split():
            if "=" in token:
                key, value = token.split("=", 1)
                fields[key] = value
        records.append(fields)
    return records


def run() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("iterations", nargs="?", default="2000")
    parser.add_argument("--label", default="unlabelled")
    args = parser.parse_args()

    cpp_binary = _rlocation("benchmarks/python_cost/timer_cpp")
    py_binary = _rlocation("benchmarks/python_cost/timer_py")

    print(f"label={args.label} iterations={args.iterations}")

    cpp_output = subprocess.run(
        [cpp_binary, args.iterations], check=True, capture_output=True, text=True).stdout
    py_output = subprocess.run(
        [py_binary, args.iterations], check=True, capture_output=True, text=True).stdout

    cpp_records = {(r["pi"], r["config"]): r for r in _parse(cpp_output)}
    py_records = {(r["pi"], r["config"]): r for r in _parse(py_output)}

    print(f"{'pi':<10s}{'config':<18s}{'cpp ms/iter':>14s}{'py ms/iter':>14s}{'ratio':>10s}")
    for key in sorted(cpp_records):
        cpp_ms = float(cpp_records[key]["ms_per_iteration"])
        py_ms = float(py_records[key]["ms_per_iteration"])
        ratio = py_ms / cpp_ms if cpp_ms else float("nan")
        pi_label, config_label = key
        print(f"{pi_label:<10s}{config_label:<18s}{cpp_ms:>14.6f}{py_ms:>14.6f}{ratio:>9.2f}x")

    print("\n-- raw cpp output --")
    print(cpp_output, end="")
    print("-- raw py output --")
    print(py_output, end="")


if __name__ == "__main__":
    sys.exit(run() or 0)
