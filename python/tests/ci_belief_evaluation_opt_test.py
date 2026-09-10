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

_OPT_BUILD = re.compile(
    r"bazelisk\s+build\b[^\n]*--config=opt\b[^\n]*//library/src/belief_evaluation\b"
)
_OPT_TEST = re.compile(
    r"bazelisk\s+test\b[^\n]*--config=opt\b[^\n]*//library/tests/belief_evaluation/\.\.\."
)


class TestOptBuildCoversBeliefEvaluation(unittest.TestCase):
    def test_each_guarded_workflow_builds_and_tests_belief_evaluation_optimised(self) -> None:
        for name in _GUARDED_WORKFLOWS:
            with self.subTest(workflow=name):
                text = (_repo_root() / ".github" / "workflows" / name).read_text(encoding="utf-8")
                self.assertRegex(
                    text,
                    _OPT_BUILD,
                    f"expected {name} to build //library/src/belief_evaluation under --config=opt",
                )
                self.assertRegex(
                    text,
                    _OPT_TEST,
                    f"expected {name} to test //library/tests/belief_evaluation/... under --config=opt",
                )


class TestOptRegexRejectsNarrowedOrRemovedCoverage(unittest.TestCase):
    """Exercises the two ways coverage can quietly regress: the invocation

    disappearing entirely, or staying but no longer naming this module —
    without waiting for either to actually happen in a workflow file.
    """

    def test_rejects_a_build_missing_config_opt(self) -> None:
        self.assertIsNone(
            _OPT_BUILD.search("bazelisk build --verbose_failures //library/src/belief_evaluation")
        )

    def test_rejects_a_test_target_pattern_narrowed_away_from_the_module(self) -> None:
        self.assertIsNone(
            _OPT_TEST.search("bazelisk test --config=opt //library/tests/some_other_module/...")
        )

    def test_accepts_the_committed_invocation_shape(self) -> None:
        self.assertIsNotNone(
            _OPT_BUILD.search(
                "bazelisk build --config=opt --verbose_failures //library/src/belief_evaluation"
            )
        )
        self.assertIsNotNone(
            _OPT_TEST.search(
                "bazelisk test --config=opt --verbose_failures --test_output=errors "
                "//library/tests/belief_evaluation/..."
            )
        )


if __name__ == "__main__":
    unittest.main()
