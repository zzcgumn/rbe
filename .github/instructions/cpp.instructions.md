---
name: C++ Development Rules (clangd MCP)
languages: ["cpp", "c++", "h", "hpp", "cc"]
alwaysApply: false
applyTo: "**/*.cpp,**/*.cc,**/*.c,**/*.hpp,**/*.hh,**/*.h"
---

# C++ Development Rules with clangd MCP Integration

## MCP Server
These are the agent's tools, not this repository's: nothing here configures
them, and nothing here requires them. Use them if your editor provides them.
- **Name:** `cpp` — clangd integration
- **Name:** `serena` — semantic code retrieval and editing
## Tooling
| Feature | What it does | When to use |
|---------|--------------|-------------|
| Diagnostics | Real-time error reporting | Before building |
| Fix suggestions | Auto-apply clangd fixes when diagnostics appear |  |
| Code completion | Signature help | While typing |
| Go-to-definition / references | Navigate code during refactoring |  |
| Rename-symbol | Cross-file rename | Renaming identifiers |
| Organize-includes | Remove unused includes  |  Before committing |
| clang-tidy | Style & safety checks. Not wired into this repository's CI (dds runs it); use it locally if you have it | Before committing |

When suggesting code:
- Consider that the development environment has advanced semantic code analysis through Serena
- Follow patterns that work well with LSP-based tools (proper symbol definitions, clear function signatures)
- Maintain good code structure since Serena works best with well-organized, modular code
- Ensure changes are compatible with clangd analysis

Code organization preferences:
- Favor clear symbol-level organization (Serena operates at the symbol level)
- Use meaningful function and class names (aids semantic retrieval)
- Maintain proper C++ namespacing and module structure

> **Tip:** If a clangd command fails, fall back to manual analysis and report the issue.

## Workflow
1. **Diagnostics** – run first to catch compile errors before any changes.
2. **Refactor** – use *find-references* to locate all usages during refactoring.
3. **Rename** – use *rename-symbol* for safe cross-file changes when renaming identifiers.
4. **Include management** – run *organize-includes* before committing to remove unused includes.
5. **Post-change diagnostics** – verify no new errors after code changes.

# C++ Style Instructions

This project follows a consistent modern C++ style, inspired by Google/LLVM with some adjustments.

## General Principles
- Prefer clarity and consistency over brevity.
- Follow RAII and Core Guidelines best practices.
- Avoid Hungarian notation, `m_` prefixes, and leading underscores.

## Naming

Prefer American spelling in identifiers (files, functions, variables) for new
code and for anything not tied to an existing API. Leave legacy public-API
names that already use British spelling unchanged (for example `AnalysePlay*`).

### Types
- **PascalCase**
- Examples: `FixedArray`, `ErrorCode`, `SimulationRunner`

### Functions and Methods
- **snake_case**
- Examples: `compute_area()`, `reset_cache()`

### Variables (local / global)
- **snake_case**
- Examples: `max_size`, `error_message`

### Member Variables
- **snake_case with trailing underscore**
- Examples: `count_`, `element_size_`

### Constants
- **PascalCase**
- Examples: `MaxIterations`, `Pi`
- Use `ALL_CAPS` only for macros, with one exception: dds's public-API
  constants keep their historical `ALL_CAPS` names even though they are
  `constexpr` (e.g. `DDS_STRAINS`, `MAXNOOFBOARDS`, `RETURN_NO_FAULT`). This
  repository consumes them and spells them dds's way; it defines no such
  constants of its own.

### Template Parameters
- **PascalCase, short descriptive**
- Examples: `T`, `Key`, `Value`

### Namespaces
- **snake_case**
- Examples: `physics`, `cosmology`

### Files
- **snake_case**
- Match main type or purpose.
- Examples: `fixed_array.h`, `error_code.cpp`

### Macros
- Avoid when possible.
- If needed, use **ALL_CAPS** with underscores.
- Example: `#define BUFFER_SIZE 1024`

## Formatting

### Indentation
- **4 spaces** per indentation level.
- No hard tabs.

### Braces
- **New line (Allman style)** for:
  - classes
  - structs
  - enums
  - functions
  - namespaces
- **Same line (K&R style)** for control statements:
  ```cpp
  if (x > 0) {
      do_something();
  }
  ```

### Function declarations
- Use trailing return types consistently.
- Examples:
```cpp
auto compute_area(int radius) -> int;
auto get_iterator() -> std::vector<int>::iterator;

### Function calls
```
- If arguments don’t fit on one line, place each on its own line:
```cpp
auto result = compute_area(
    width,
    height
);
```

### General Rules
- **Standard:** C++20 (`-std=c++20`)
- **RAII** – manage resources via destructors.
- **Smart pointers** – prefer `std::unique_ptr` / `std::shared_ptr`.
- **Const-correctness** – mark data/functions const where possible.
- **Namespace `using`** – see [Namespace `using`](#namespace-using) below.
- **C++ Core Guidelines** – [Style Guide](https://isocpp.github.io/CppCoreGuidelines/) follow naming, formatting, and layout.
- **Enums** – use `enum class`.
- **Functions** – keep short; extract helpers if > 20 lines.
- **Variables** – snake_case; classes – PascalCase.
- **Header guards** – `#pragma once` or traditional guards.
- **Include paths** – use `<target/header.hpp>` (angle brackets) for headers from other Bazel targets; use `"header.hpp"` (quotes) only for the paired header of the current translation unit.
  - Headers from dds arrive under their own prefixes: `<api/...>`,
    `<dds/dds.hpp>`, `<solver_context/...>`, `<lookup_tables/...>`,
    `<dds3/converters.hpp>`. A quoted include never reaches them — they are in
    another repository, not a sibling directory. This is a real trap when
    moving a file here from dds, where some of them *were* siblings.

### Namespace `using`
- **Headers:** no namespace-scope `using namespace` (using-directives) and no
  namespace-scope `using X::name;` (using-declarations). Fully qualify names in
  header code (`std::string`, not `string`). Rationale: either form at namespace
  scope leaks names into every translation unit that includes the header,
  directly or transitively, and a later cleanup then breaks every includer that
  had come to rely on it.
  - A `using` inside a class or function body (e.g. inheriting constructors
    `using Base::Base;`) is fine — it does not leak.
  - Alias-declarations (`using Clock = std::chrono::steady_clock;`) are not
    using-directives/declarations and are allowed; prefer placing them inside a
    namespace rather than at global scope.
- **`.cpp` files:** `using X::name;` for specific names is allowed and preferred
  where it improves readability. A file-scope `using namespace` is permitted but
  discouraged — prefer specific using-declarations, a qualified sub-namespace
  (`using namespace std::chrono;`), or scoping the directive to a function body.
- **Never rely on a `using` supplied by an included header.** Qualify the name,
  or add your own using-declaration in the `.cpp`.

### Safety
- Initialize all variables.
- Check bounds on container access.
- Prefer exceptions over error codes when appropriate.
- Use `std::optional` for nullable values.
- Mark compile-time constants with `constexpr`.

### Documentation
- Public APIs: Doxygen-style comments.
- Explain non-obvious design choices.
- Provide usage examples for complex interfaces.

## Testing
- Use GoogleTest.
- Cover normal, edge, and failure cases.
- Keep tests fast and focused.

## Build System
- **Bazel** is the sole build system, with bzlmod: `MODULE.bazel`, no `WORKSPACE`.
- `BUILD` files are written and commented by hand here, not generated. A
  non-obvious `deps` entry or visibility choice is expected to say why.
- Compiler flags come from `CPPVARIABLES.bzl`, not from individual targets:
  `-Wall -Wpedantic -Werror` on Clang/GCC, `/W4 /WX /permissive-` on MSVC.
  Do not restate them per-target, and do not disable a warning to get a build
  through.
---

**Reminder:** All changes should be reviewed via a pull request. Use the `github` MCP server to create remote branches and open PRs.
```
