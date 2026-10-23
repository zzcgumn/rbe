#!/usr/bin/env python3
"""Guard that CI builds and tests belief_evaluation under -c opt (--config=opt).

Without an optimised CI job, an NDEBUG-only error such as
-Werror=unused-variable goes unnoticed. The test reads the workflow files, not
the build outcome. It fails as soon as the job is removed or its target pattern
stops covering this module.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


def _repo_root(start: Path | None = None) -> Path:
    # Do not call Path.resolve(). Under `bazel test` this file is often a
    # runfiles symlink, and resolving it leaves the runfiles tree that holds
    # the //.github/workflows data.
    here = (start or Path(__file__)).absolute()
    for parent in here.parents:
        workflows = parent / ".github" / "workflows"
        if workflows.is_dir() and any(workflows.glob("ci_*.yml")):
            return parent
    raise AssertionError("could not locate repository root from test file path")


# Both platforms are guarded. DDS_CPPOPTS and DDS_LINKOPTS add -flto=thin on
# macOS to the shared -O3, so the two optimised builds differ. A Linux-only
# guard would leave macOS uncovered.
_GUARDED_WORKFLOWS = ("ci_linux.yml", "ci_macos.yml")

_OPT_BUILD_TARGET = "//src/belief_evaluation"
_OPT_TEST_TARGET = "//tests/belief_evaluation/..."


def _line_covers_module_under_opt(line: str, subcommand: str, target: str) -> bool:
    """True if `line` runs `bazelisk <subcommand>` with --config=opt and
    `target`, in either order, as Bazel ignores flag order. Comments are
    stripped first, so a commented-out invocation does not count.
    """
    code = line.split("#", 1)[0]
    if not code.strip():
        return False
    if not re.search(rf"bazelisk\s+{re.escape(subcommand)}\b", code):
        return False
    if not re.search(r"(?<![\w-])--config=opt\b", code):
        return False
    if not re.search(rf"(?<![\w/]){re.escape(target)}(?!\S)", code):
        return False
    return True


def _workflow_covers_module_under_opt(text: str, subcommand: str, target: str) -> bool:
    return any(_line_covers_module_under_opt(line, subcommand, target) for line in text.splitlines())


class TestOptBuildCoversBeliefEvaluation(unittest.TestCase):
    def test_each_guarded_workflow_builds_and_tests_belief_evaluation_optimised(self) -> None:
        for name in _GUARDED_WORKFLOWS:
            with self.subTest(workflow=name):
                text = (_repo_root() / ".github" / "workflows" / name).read_text(encoding="utf-8")
                self.assertTrue(
                    _workflow_covers_module_under_opt(text, "build", _OPT_BUILD_TARGET),
                    f"expected {name} to build {_OPT_BUILD_TARGET} under --config=opt",
                )
                self.assertTrue(
                    _workflow_covers_module_under_opt(text, "test", _OPT_TEST_TARGET),
                    f"expected {name} to test {_OPT_TEST_TARGET} under --config=opt",
                )


class TestOptRegexRejectsNarrowedOrRemovedCoverage(unittest.TestCase):
    """The two ways coverage can regress: the invocation disappears, or it
    stays but no longer names this module.
    """

    def test_rejects_a_build_missing_config_opt(self) -> None:
        self.assertFalse(
            _line_covers_module_under_opt(
                "bazelisk build --verbose_failures //src/belief_evaluation",
                "build",
                _OPT_BUILD_TARGET,
            )
        )

    def test_rejects_a_test_target_pattern_narrowed_away_from_the_module(self) -> None:
        self.assertFalse(
            _line_covers_module_under_opt(
                "bazelisk test --config=opt //tests/some_other_module/...",
                "test",
                _OPT_TEST_TARGET,
            )
        )

    def test_rejects_a_commented_out_invocation(self) -> None:
        self.assertFalse(
            _line_covers_module_under_opt(
                "# bazelisk build --config=opt //src/belief_evaluation",
                "build",
                _OPT_BUILD_TARGET,
            )
        )

    def test_rejects_the_wrong_subcommand(self) -> None:
        self.assertFalse(
            _line_covers_module_under_opt(
                "bazelisk fetch --config=opt //src/belief_evaluation",
                "build",
                _OPT_BUILD_TARGET,
            )
        )

    def test_accepts_the_committed_invocation_shape(self) -> None:
        self.assertTrue(
            _line_covers_module_under_opt(
                "bazelisk build --config=opt --verbose_failures //src/belief_evaluation",
                "build",
                _OPT_BUILD_TARGET,
            )
        )
        self.assertTrue(
            _line_covers_module_under_opt(
                "bazelisk test --config=opt --verbose_failures --test_output=errors "
                "//tests/belief_evaluation/...",
                "test",
                _OPT_TEST_TARGET,
            )
        )

    def test_accepts_config_opt_and_the_target_in_either_order(self) -> None:
        """Reordering flags in the workflow must not fail the guard."""
        self.assertTrue(
            _line_covers_module_under_opt(
                "bazelisk build //src/belief_evaluation --config=opt --verbose_failures",
                "build",
                _OPT_BUILD_TARGET,
            )
        )
        self.assertTrue(
            _line_covers_module_under_opt(
                "bazelisk test //tests/belief_evaluation/... --verbose_failures --config=opt",
                "test",
                _OPT_TEST_TARGET,
            )
        )


if __name__ == "__main__":
    unittest.main()
