# rbe — replenished belief evaluation

A belief-space local evaluator for bridge declarer play, built on the
[dds](https://github.com/dds-bridge/dds) double dummy solver.

The algorithm is `docs/replenished_belief_evaluation/algorithm.md`; the
measurements behind its defaults are `.../benchmarks.md`; the implemented
contract is `specs/replenished-belief-evaluation.md`.

## Layout

| Path | What is there |
| --- | --- |
| `src/belief_evaluation/` | the evaluator. Solver-free, except `double_dummy_defender` and `double_dummy_bound`, which are separate targets for that reason |
| `tests/belief_evaluation/` | 347 C++ cases across four targets |
| `benchmarks/` | the measuring instruments and their correctness guards |
| `python/` | the `belief_space_local_evaluation` extension and its tests |

## Building

```
bazelisk test //...
```

dds is a Bazel module dependency, pinned by `git_override` to a commit
rather than a branch, so a first build compiles the solver from source.

Two things about that dependency are not obvious:

- **dds is patched to be consumable.** Its own `MODULE.bazel` uses the
  `toolchains_llvm` extension, which refuses non-root use, so an unpatched
  dds cannot be a `bazel_dep` of anything that builds C++.
  `patches/dds_module_no_llvm_extension.patch` strips that and the toolchain
  registration; this repository registers the toolchain itself. The patch is
  tied to the pinned commit — bump one, regenerate the other.
- **The build flags are copied from dds, not inherited.** `CPPVARIABLES.bzl`
  and the bzlmod overrides in `MODULE.bazel` are restated here because bzlmod
  applies overrides only in the root module and never shares `.bazelrc`. They
  must stay in step with dds's copies: this code is compiled into the same
  binaries as the solver's, under the same `-Werror`.

The Python package imports `dds3` before its own extension, so that
`SolverContext`'s pybind registration is in place first. Both extensions
resolve one pybind11 through the module graph, so they share the
interpreter-level type registry.
