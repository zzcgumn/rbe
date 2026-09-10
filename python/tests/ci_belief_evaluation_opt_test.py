#!/usr/bin/env python3
"""Guard that CI builds and tests belief_evaluation under -c opt (--config=opt).

belief_evaluation was never compiled under NDEBUG across six plans: nothing
in CI ever passed --config=opt for it, so the -Werror=unused-variable this
plan fixed went unnoticed until someone happened to build it by hand. An
instruction alone did not hold for the leak sweep this same plan repairs
elsewhere, so this follows the repository's existing ci_*_test.py
precedent instead: assert the CI configuration directly, rather than the
build outcome (which would just re-run the build this workflow already
runs), so the assertion fails the moment the job disappears or its target
pattern narrows away from this module -- not only when someone remembers to
check.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


def _repo_root(start: Path | None = None) -> Path:
    # Do not Path.resolve() — under `bazel test` this file is often a runfiles
    # symlink into the execroot/source tree, and resolving leaves the runfiles
    # tree where //.github/workflows data deps live.
    here = (start or Path(__file__)).absolute()
    for parent in here.parents:
        workflows = parent / ".github" / "workflows"
        if workflows.is_dir() and any(workflows.glob("ci_*.yml")):
            return parent
    raise AssertionError("could not locate repository root from test file path")


# Platforms this module's optimised build is guarded on. DDS_CPPOPTS and
# DDS_LINKOPTS give macOS -flto=thin (ThinLTO) on top of the -O3 both
# platforms share, so Linux and macOS are genuinely different builds here —
# a Linux-only guard would leave macOS, the untested one, uncovered.
_GUARDED_WORKFLOWS = ("ci_linux.yml", "ci_macos.yml")

_OPT_BUILD_TARGET = "//library/src/belief_evaluation"
_OPT_TEST_TARGET = "//library/tests/belief_evaluation/..."


def _line_covers_module_under_opt(line: str, subcommand: str, target: str) -> bool:
    """True if `line` invokes `bazelisk <subcommand>` with both --config=opt
    and `target` present, in either order -- flag order is not significant to
    Bazel, so neither is it here, matching ci_windows_cppopts_test.py's own
    _bazelisk_invocation_has_config_opt convention. Full-line and trailing
    comments are stripped first so a commented-out invocation cannot satisfy
    the guard.
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
    """Exercises the two ways coverage can quietly regress: the invocation

    disappearing entirely, or staying but no longer naming this module —
    without waiting for either to actually happen in a workflow file.
    """

    def test_rejects_a_build_missing_config_opt(self) -> None:
        self.assertFalse(
            _line_covers_module_under_opt(
                "bazelisk build --verbose_failures //library/src/belief_evaluation",
                "build",
                _OPT_BUILD_TARGET,
            )
        )

    def test_rejects_a_test_target_pattern_narrowed_away_from_the_module(self) -> None:
        self.assertFalse(
            _line_covers_module_under_opt(
                "bazelisk test --config=opt //library/tests/some_other_module/...",
                "test",
                _OPT_TEST_TARGET,
            )
        )

    def test_rejects_a_commented_out_invocation(self) -> None:
        self.assertFalse(
            _line_covers_module_under_opt(
                "# bazelisk build --config=opt //library/src/belief_evaluation",
                "build",
                _OPT_BUILD_TARGET,
            )
        )

    def test_rejects_the_wrong_subcommand(self) -> None:
        self.assertFalse(
            _line_covers_module_under_opt(
                "bazelisk fetch --config=opt //library/src/belief_evaluation",
                "build",
                _OPT_BUILD_TARGET,
            )
        )

    def test_accepts_the_committed_invocation_shape(self) -> None:
        self.assertTrue(
            _line_covers_module_under_opt(
                "bazelisk build --config=opt --verbose_failures //library/src/belief_evaluation",
                "build",
                _OPT_BUILD_TARGET,
            )
        )
        self.assertTrue(
            _line_covers_module_under_opt(
                "bazelisk test --config=opt --verbose_failures --test_output=errors "
                "//library/tests/belief_evaluation/...",
                "test",
                _OPT_TEST_TARGET,
            )
        )

    def test_accepts_config_opt_and_the_target_in_either_order(self) -> None:
        """--config=opt need not precede the target pattern: a harmless flag
        reordering in the workflow YAML must not false-fail this guard.
        """
        self.assertTrue(
            _line_covers_module_under_opt(
                "bazelisk build //library/src/belief_evaluation --config=opt --verbose_failures",
                "build",
                _OPT_BUILD_TARGET,
            )
        )
        self.assertTrue(
            _line_covers_module_under_opt(
                "bazelisk test //library/tests/belief_evaluation/... --verbose_failures --config=opt",
                "test",
                _OPT_TEST_TARGET,
            )
        )


if __name__ == "__main__":
    unittest.main()
