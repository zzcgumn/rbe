# RBE (Replenished Belief Evaluation) Repository Instructions

## Project Overview

**rbe** implements belief-space local evaluation for bridge declarer play: given
a belief over the opponents' possible hands, it evaluates declarer's options by
walking a belief tree, replenishing the sampled belief set as the tree narrows.
It is built on the [dds](https://github.com/dds-bridge/dds) double dummy solver,
which it consumes as a Bazel module dependency.

- The caller's guide is `docs/belief_space_local_evaluation.md`
- The module map and the conventions common to every header are `docs/module_map.md`
- The implemented contract, and the reasoning behind it, is `specs/replenished-belief-evaluation.md`
- The algorithm is `docs/replenished_belief_evaluation/algorithm.md`
- The measurements behind its defaults are `docs/replenished_belief_evaluation/benchmarks.md`

**Read the spec before changing behaviour.** It is not a summary written after
the fact; it is what the tests encode, and a change that contradicts it is a
change to the spec first.

**Languages & tools**: C++20, Bazel with bzlmod (version pinned in
`.bazelversion`), GoogleTest, pybind11, Python 3.14.

The Bazel pin is 9.2.0 here and 9.1.0 in dds. They are independent pins —
dds is built from source as a dependency, using this repository's Bazel, not
its own.

**Size**: 46 C++ files in `src/belief_evaluation`, 38 test files, 25 Bazel test
targets, 350 C++ test cases.

**History**: extracted from dds, where it was `library/src/belief_evaluation`.
The commit history predates the split and refers to paths that no longer exist.

## Building and Testing

**Bazel only.**

```bash
# Build everything
bazelisk build //...

# Run all tests (25 targets)
bazelisk test //...

# The core library, and its tests
bazelisk build //src/belief_evaluation
bazelisk test //tests/belief_evaluation/...

# Optimised: -c opt plus -DNDEBUG. CI runs this separately on Linux and macOS,
# and python/tests/ci_belief_evaluation_opt_test.py guards that it keeps doing so
bazelisk build --config=opt //src/belief_evaluation
bazelisk test --config=opt //tests/belief_evaluation/...

# The wheel
bazelisk build //python:rbe_wheel_dist
```

### Build time expectations

A first build compiles dds from source, so expect several minutes cold. After
that, incremental builds are seconds and the full test suite is under a minute.

### Before and after making changes

```bash
bazelisk build //... && bazelisk test //...
```
Run it first, so a pre-existing failure is not attributed to your change, and
again after.

## Project Layout and Architecture

```
<repository-root>/
├── .github/
│   ├── copilot-instructions.md      # This file
│   ├── instructions/                # Path-specific instructions
│   └── workflows/                   # ci_linux.yml, ci_macos.yml
├── src/belief_evaluation/           # The evaluator
├── tests/belief_evaluation/         # 350 C++ cases across 4 targets
├── benchmarks/
│   ├── belief_evaluation/           # Fixture ladder, instrument, sweeps
│   └── python_cost/                 # Python-vs-C++ per-callback cost
├── python/
├── examples/                        # runnable examples of the Python surface
│   ├── src/                         # The pybind extension
│   ├── belief_space_local_evaluation/  # The Python package
│   └── tests/                       # 14 test modules
├── docs/  specs/
├── patches/                         # The dds MODULE.bazel patch (see below)
├── BUILD.bazel                      # config_settings CPPVARIABLES.bzl selects on
├── CPPVARIABLES.bzl                 # Compiler/link flags, copied from dds
├── MODULE.bazel                     # bzlmod deps, including the dds pin
└── .bazelrc
```

### The four C++ targets, and why there are four

- `//src/belief_evaluation` — the evaluator. **Solver-free**, deliberately: a
  caller supplying their own defender should not link the solver to get the
  evaluator. Do not add a solver dependency to this target.
- `//src/belief_evaluation:double_dummy_defender` and `:double_dummy_bound` —
  the solver seam, kept out of the core for the reason above. This is also
  where a future GIL-release boundary belongs, separate from the callback
  boundary that runs on the calling thread.
- `//src/belief_evaluation:position` — trick mechanics, depended on privately so
  the header is not re-exported to the core library's consumers.

The test targets mirror that split: `belief_evaluation_test` does not link the
solver; `double_dummy_defender_test` and `double_dummy_bound_test` do.

### The dds dependency — three things that are not obvious

1. **dds is patched to be consumable at all.** Its `MODULE.bazel` uses the
   `toolchains_llvm` extension, which fails with *"Only the root module can use
   the 'llvm' extension"*. `patches/dds_module_no_llvm_extension.patch` strips
   that and the toolchain registration; this repository registers the toolchain
   itself. **The patch is tied to the pinned commit — bump one, regenerate the
   other.**
2. **The build glue is copied, not inherited.** bzlmod honours
   `single_version_override` / `archive_override` only in the root module, and
   `.bazelrc` is never shared. `CPPVARIABLES.bzl`, the overrides in
   `MODULE.bazel` and the config settings in `BUILD.bazel` are all restated
   here and must stay in step with dds's.
3. **The transitive graph decides what the glue needs, not this tree.**
   `.bazelrc` pins `ANDROID_HOME=` empty although nothing here is Android or
   Java, because dds depends on `rules_jvm_external` → `rules_android`, whose
   extension probes that variable during analysis of *any* target. Trimming it
   as "not needed here" broke Linux CI once.

### The Python boundary

`python/belief_space_local_evaluation/__init__.py` imports `dds3` **before** its
own extension, so `SolverContext`'s pybind registration is in place first. Both
extensions resolve one pybind11 through the module graph and share the
interpreter-level type registry. Do not register `SolverContext` here; dds3 owns
that registration.

The Deal converters come from dds twice over: `@dds//python:converters_srcs`
compiled into the extension (so it keeps its own `-fvisibility=hidden` copy
rather than linking a library whose symbols could interpose on dds3's), and
`@dds//python:converters_hdrs` for `<dds3/converters.hpp>`.

## CI/CD Pipeline

`.github/workflows/ci_linux.yml` and `ci_macos.yml`. Each builds and tests
`//...`, then builds and tests the module again under `--config=opt`. macOS
additionally carries ThinLTO, so the two optimised builds are genuinely
different compilations — that is why both are guarded.

To reproduce a failure locally, run the exact command from the workflow. Note
that dds is compiled from source in CI, so a first run there is slow.

## Coding Standards and Conventions

**Follow `.github/instructions/cpp.instructions.md`.** Key points:

- **Types**: `PascalCase`. **Functions/variables**: `snake_case`. **Members**:
  `snake_case_`. **Constants**: `PascalCase`. **Macros**: `ALL_CAPS`.
  dds's public constants keep their `ALL_CAPS` names (`DDS_STRAINS`).
- **4 spaces**, no tabs. **Allman** braces for functions/classes/namespaces,
  **K&R** for control statements.
- **Trailing return types**: `auto f(int x) -> int`.
- `#pragma once`. Include order: standard headers, then project headers,
  each group sorted.
- Flags come from `CPPVARIABLES.bzl`: `-Wall -Wpedantic -Werror`. Do not
  disable a warning to get a build through.

### Testing

GoogleTest for C++, `unittest` for Python. This repository was built
test-first (see `AGENTS.md`) and its tests carry that shape: each says what it
proves, not merely what it calls.

**A passing test proves little on its own.** When you add or change a guard,
break the thing it guards and confirm it goes red, then restore it. Several
tests here exist because a green suite was hiding a real defect.

## Common Pitfalls

**A quoted include cannot reach a dds header.** `#include "converters.hpp"`
worked when the file was a sibling in dds; here it is in another repository.
Use the prefixed form (`<dds3/converters.hpp>`, `<api/...>`, `<dds/dds.hpp>`).

**A same-package consumer does not test visibility.** Bazel checks visibility on
direct edges across packages only. A test that consumes a public target from
inside its own package proves nothing about that target being public.

**`py_wheel` packages a `py_library`'s `srcs`, not what its `data` points at.**
A wheel can ship `__init__.py` with no extension beside it: it builds, installs
and imports cleanly, and fails on the first call.
`//python:wheel_contents_test` exists because that happened.

**The benchmarks are not tests.** `//benchmarks/...:*_test` targets are
correctness guards on the fixtures; the instruments themselves
(`:instrument`, `:timer_cpp`) are built by `//...` and never run by it. Running
them is a driver script's job.

## Important Facts for Agents

### Trust these instructions
Only search the codebase if the information here is incomplete for your task,
you find a discrepancy with reality, or you need implementation detail.
**If you find a discrepancy, fix this file as part of your change.**

### Don't
- Don't add a solver dependency to `//src/belief_evaluation`
- Don't disable warnings, or add `-Wno-*` to get a build through
- Don't change a test's expected value without establishing which side is wrong
- Don't add a dependency without strong justification
- Don't register `SolverContext` in this repository's extension
- Don't edit `patches/` without re-pinning dds, or vice versa
- Don't use `using namespace` at namespace scope in a header

### Always
- Always build and test before committing
- Always open a PR; never commit to `main` directly
- Always check `.github/instructions/*.instructions.md` for file-specific rules
- Always say *why* in a comment when a dependency, visibility or ordering
  choice is not obvious from the label
- Always verify a new guard by breaking what it guards
