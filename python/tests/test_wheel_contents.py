"""Guard that the wheel ships both packages and both compiled extensions.

py_wheel packages a py_library's `srcs`, but not whatever its `data`
points at. So a wheel can carry dds3/__init__.py and no dds3/_dds3.so: it
builds clean, installs clean, imports clean, and raises on the first call
into the solver. That is precisely what this wheel did before
:_dds3_in_package was added to its deps, which is why the assertion is on
the archive rather than on an import.
"""

from __future__ import annotations

import unittest
import zipfile
from pathlib import Path

# Both are compiled extensions, so the suffix is platform-dependent: Bazel
# stages .so on Linux/macOS and .pyd on Windows.
_EXTENSION_SUFFIXES = (".so", ".pyd")

_REQUIRED_MODULES = (
    "belief_space_local_evaluation/__init__.py",
    "dds3/__init__.py",
)

_REQUIRED_EXTENSIONS = (
    "belief_space_local_evaluation/_belief_space_local_evaluation",
    "dds3/_dds3",
)


def _wheel() -> Path:
    # The data dep lands beside this test in the runfiles tree, under the
    # package that declares it. Do not Path.resolve(): under `bazel test`
    # this file is a runfiles symlink into the source tree, and resolving
    # leaves the runfiles tree where the wheel lives. Mirrors
    # ci_belief_evaluation_opt_test.py's own _repo_root reasoning.
    here = Path(__file__).absolute()
    for parent in here.parents:
        wheels = sorted((parent / "python").glob("*.whl"))
        if wheels:
            return wheels[0]
    raise AssertionError(f"no .whl found above {here}")


class TestWheelContents(unittest.TestCase):
    def setUp(self) -> None:
        with zipfile.ZipFile(_wheel()) as archive:
            self.names = archive.namelist()

    def test_ships_both_top_level_packages(self) -> None:
        for module in _REQUIRED_MODULES:
            with self.subTest(module=module):
                self.assertIn(module, self.names)

    def test_ships_a_compiled_extension_for_each_package(self) -> None:
        """The failure this guard exists for: a package without its extension."""
        for stem in _REQUIRED_EXTENSIONS:
            with self.subTest(extension=stem):
                candidates = [stem + suffix for suffix in _EXTENSION_SUFFIXES]
                self.assertTrue(
                    any(name in self.names for name in candidates),
                    f"wheel has no compiled extension for {stem}; "
                    f"looked for {candidates}, wheel contains {sorted(self.names)}",
                )

    def test_every_extension_is_inside_a_shipped_package(self) -> None:
        """No stray extension at the archive root, where nothing can import it."""
        for name in self.names:
            if name.endswith(_EXTENSION_SUFFIXES):
                with self.subTest(name=name):
                    self.assertIn("/", name, f"{name} is not inside a package")


if __name__ == "__main__":
    unittest.main()
