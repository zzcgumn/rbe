# CLAUDE.md

Deliberately short. The detailed conventions live in
[`.github/copilot-instructions.md`](.github/copilot-instructions.md) and
[`.github/instructions/`](.github/instructions/), and restating them here
would only let the two drift.

**Read `.github/copilot-instructions.md` first.** It is written for this
repository (unlike most of `.github/instructions/`, which is copied from dds
and re-synced by hand) and it carries the document index, the build rules, and
the rule that the spec is read before behaviour changes.

## The remote is public

`github.com/zzcgumn/rbe` is a **public** repository. Anything committed here is
published, and a push cannot be taken back.

## Working notes live in `../rbe-notes`, a private sibling

Plans, analyses, reviews, task breakdowns and scratch files go to
`../rbe-notes`, a private sibling checkout, never into this tree. Its layout is
documented there; nothing in this repository points at a file inside it, for
the reason given below.

A sibling repository rather than an ignored directory here, for two reasons:

- **It cannot be staged by accident.** `git add` inside this repo cannot reach
  files outside it. An ignored directory is only a convention — one `git add -f`
  or one `!` line in a `.gitignore` defeats it.
- **Ignored files have no history and no backup**, and `git clean -xdf` deletes
  them, which is routine in a Bazel tree.

`claude/` survives only as a tripwire: `claude/.gitignore` keeps anything
written there uncommittable, so a tool that still writes to the old location
cannot leak into a public push. Nothing should be put there on purpose — and do
not reference a path under it, or under `../rbe-notes`, from a committed file.
The reference would dangle for everyone else.

Not to be confused with `.claude/` (leading dot), which is Claude Code's own
settings directory and is ignored separately by the root `.gitignore`.

## What stays public, and why

`specs/` and `docs/` are committed here on purpose. A spec is the contract a
coding agent — human or machine — needs in order to change behaviour correctly,
so it belongs with the code it constrains. Notes record *how the library came to
be*; specs and docs state *what it must do* and *how to use it*. If a document
is useful for using or changing rbe, it belongs in this repository.

## Building

```
bazelisk test //...
```
