---
name: Writing Rules for Docs and Comments
alwaysApply: false
applyTo: "**/*.md,**/*.py,**/*.cpp,**/*.hpp,**/*.h,**/BUILD.bazel"
---

# Writing rules for docs and comments

These rules apply to every document, docstring, doxygen block and comment
in this repository. They follow Simplified Technical English (STE) in a
loose form.

Write for a working developer who is new to this project. That reader
knows C++, Python and bridge. They do not know this library.

## Sentences and paragraphs

- Procedural sentences: 20 words or fewer. Descriptive sentences: 25 words
  or fewer. A code span such as `evaluate()` counts as one word.
- Use the active voice. Use the imperative for instructions.
- One idea per sentence. One action per numbered step, in order.
- Paragraphs: 6 sentences or fewer.
- Do not use a dash (` -- ` or `—`) to add a second idea to a sentence.
  Write a second sentence.
- Do not write "X's own". Write "X". Use "own" only to contrast two
  owners.
- Use bold and italics only for a real warning, or for a term at the
  place where you define it.

## Words and terms

- Use common words.
- Use one term for each concept. The canonical terms are in "The
  vocabulary" in `docs/belief_space_local_evaluation.md`.
- Define a project or domain term on first use in each document. Write
  "π (declarer strategy)" on first use, then "π".
- Do not define general programming or bridge terms.
- Do not change code identifiers, flags, paths, commands or error text.
  Put them in code formatting.

## Content

- Describe what the code does now. Do not describe how it came to be.
  History belongs in git. The spec is the exception: it records rejected
  designs, because that is its role.
- A comment says what the code must do. It says why only when the reason
  is not obvious. It does not argue for the design against alternatives.
- Explain a thing once, in the right place (see "Documentation" in
  `cpp.instructions.md`). Elsewhere, link to it with one short reference.
  Do not chain references.
- Keep every warning, precondition, limitation, unit, lifetime rule and
  version note.

## When you simplify existing text

- Remove repetition. Keep one canonical explanation and link to it.
- Remove explanations of general knowledge for the stated reader.
- List what you removed in the PR description, so the author can review
  it. Do not put the list in the file.

If a rule makes a sentence ambiguous, break the rule. Clarity comes first.
