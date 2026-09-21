---
name: Bazel Build System Rules (MCP-Integrated)
languages: ["bazel", "starlark", "build", "bazelbuild"]
alwaysApply: false
---


> **Tip:** Whenever you edit a `BUILD` or `.bzl` file, run `bazelisk build` on
> an affected target before going further — a label typo or a missing dep is an
> analysis error, and analysis is fast.

## Build Guidelines
| Rule     | What it means                       | Why it matters                           |
|----------|-------------------------------------|-------------------------------------------|
| Explicit deps | List every dependency in `deps = [...]` | Prevents hidden transitive pulls            |
| No wildcards | Avoid `glob([...])` unless absolutely needed | Keeps target graph deterministic             |
| Say why | Comment a non-obvious dep, visibility or ordering choice | The reason is not recoverable from the label |
| Target naming | `<component>_<purpose>_<type>`       | Easier to read & search                     |
| Idiomatic Starlark | Prefer built-in rules over custom macros | Reduces maintenance                        |
| Consistent formatting | Follow `.bazelrc` & `.bzlformat`   | Keeps codebase uniform                      |
## Best Practices
- Use `cc_library`, `cc_binary`, `cc_test` for C++ targets.
- Keep test suites small & fast.
- Restrict visibility (`visibility = ["//visibility:private"]`) when appropriate.
- Run `bazelisk build //...` & `bazelisk test //...` locally before pushing.
- Document any non-trivial macros or build logic in comments.

## Working with bazel commands
```bash
# Build / test a target (analysis alone catches most BUILD mistakes)
bazelisk build //src/belief_evaluation
bazelisk test //tests/belief_evaluation/...

# What does this target actually depend on?
bazelisk query 'labels(deps, //src/belief_evaluation)'

# Why does A depend on B? (empty output means it does not)
bazelisk query 'somepath(//python:_belief_space_local_evaluation, @dds//library/src:dds)'

# What depends on this target?
bazelisk query 'rdeps(//..., //src/belief_evaluation)'
```

## The dds dependency

Everything outside this repository comes from dds, pinned in `MODULE.bazel`
by `git_override` to a **commit**, never a branch.

- dds targets are `@dds//...`. Every one this repository depends on is
  public; if a label you want is not, that is dds's decision to change, not
  something to work around here.
- dds is **patched** to be usable as a dependency at all
  (`patches/dds_module_no_llvm_extension.patch`): its own `MODULE.bazel` uses
  the `toolchains_llvm` extension, which refuses non-root use. The patch is
  tied to the pinned commit — **bump one, regenerate the other.**
- Build flags, bzlmod overrides and `.bazelrc` are **copied** from dds, not
  inherited: bzlmod applies overrides only in the root module and never shares
  a `.bazelrc`. When dds changes one of these, this repository has to follow
  by hand. Things trimmed as "not needed here" have already bitten once — the
  transitive module graph, not this tree's own targets, decides what is
  needed.

