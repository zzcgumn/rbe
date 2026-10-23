# rbe — replenished belief evaluation

A belief-space local evaluator for bridge declarer play, built on the
[dds](https://github.com/dds-bridge/dds) double dummy solver.

Four documents, in the order you are likely to want them:

| Document | For |
| --- | --- |
| [`docs/belief_space_local_evaluation.md`](docs/belief_space_local_evaluation.md) | using the library, in Python or C++ |
| [`docs/module_map.md`](docs/module_map.md) | changing it: which header holds what, and the conventions across all of them |
| [`specs/replenished-belief-evaluation.md`](specs/replenished-belief-evaluation.md) | the design record: every capability-wide contract, and why |
| [`docs/replenished_belief_evaluation/algorithm.md`](docs/replenished_belief_evaluation/algorithm.md) | the theory |

The measurements behind the defaults are
[`.../benchmarks.md`](docs/replenished_belief_evaluation/benchmarks.md).

## Layout

| Path | What is there |
| --- | --- |
| `src/belief_evaluation/` | the evaluator. Solver-free, except `double_dummy_defender`, `double_dummy_bound`, `heuristic_defender` and `brute_force_declarer`, which are separate targets for that reason |
| `tests/belief_evaluation/` | the C++ tests |
| `benchmarks/` | the measuring instruments and their correctness guards |
| `python/` | the `belief_space_local_evaluation` extension and its tests |
| `examples/` | runnable examples of the Python surface, and the small amount of bridge plumbing they need |

## Installing

```
bazelisk build //python:rbe_wheel_dist   # writes dist/rbe-<version>-*.whl
pip install dist/rbe-0.1.0-py3-none-any.whl
```

One wheel, two top-level imports: `belief_space_local_evaluation` and
`dds3`. Both extensions link the same solver library, and every caller
needs both. The package imports `dds3` first, so that `SolverContext` is
registered before the second extension loads.

**The wheel is tagged `py3-none-any` but holds compiled extensions.** So
pip will install it on a platform where it cannot be imported. The dds
wheel has the same tag. Real platform tags need a decision on which
platforms to publish.

## Building

```
bazelisk test //...
```

dds is a Bazel module dependency, pinned by `git_override` to a commit.
The first build compiles the solver from source.

Two things about that dependency are not obvious:

- **dds is patched.** The `MODULE.bazel` of dds uses the `toolchains_llvm`
  extension, which refuses non-root use. So an unpatched dds cannot be a
  `bazel_dep` of anything that builds C++.
  `patches/dds_module_no_llvm_extension.patch` removes that and the
  toolchain registration. This repository registers the toolchain itself.
  **The patch is tied to the pinned commit: bump one, regenerate the
  other.**
- **The build flags are copied from dds, not inherited.** bzlmod applies
  overrides only in the root module and never shares `.bazelrc`. So
  `CPPVARIABLES.bzl` and the overrides in `MODULE.bazel` repeat those of
  dds. **Keep them in step:** this code is compiled into the same binaries
  as the solver, under the same `-Werror`.
