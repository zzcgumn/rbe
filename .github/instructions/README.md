Repository-level instructions for GitHub Copilot and agents

Purpose
- These files provide repository-scoped guidance that Copilot, Copilot Chat, and other repository-aware agents can surface before answering or making changes.

Provenance
- Copied from dds, this repository's one dependency, so that the two trees read
  the same way, and adapted where a statement was false here: paths and target
  labels, the `MODULE.bazel`/`WORKSPACE` distinction, the actual warning flags,
  and the cross-repository include rules. `.github/copilot-instructions.md` is
  written for this repository rather than copied — dds's is almost entirely
  about the solver.
- When dds changes one of these files, the change has to be brought over by
  hand. The same is true of the build glue; see `bazel.instructions.md`.

Files
- `bazel.instructions.md` — Bazel/build guidance
- `cpp.instructions.md` — C++/clangd/Serena guidance
- `git.instructions.md` — Git/MCP usage
- `github.instructions.md` — Branching and PR rules

Usage
- Agents should read these files to apply repository-specific context. Humans can use this README as a quick pointer.

Contact
- If you need changes to these instructions, edit the corresponding file and open a PR.
