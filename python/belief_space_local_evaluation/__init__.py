# dds3 must be imported before the extension below. pybind11 shares a single
# interpreter-level type registry across extensions built by the same
# toolchain; SolverContext is registered once, in dds3, and this module only
# *uses* that registration (see the solver seam) rather than repeating it.
# Importing dds3 first here makes that ordering guaranteed rather than
# dependent on what a caller happened to import first.
import dds3  # noqa: F401

try:
    from ._belief_space_local_evaluation import Card
    from ._belief_space_local_evaluation import module_name
    from ._belief_space_local_evaluation import ObservationState
except ImportError:
    # Fallback for environments where _belief_space_local_evaluation is
    # available as a top-level module.
    from _belief_space_local_evaluation import Card
    from _belief_space_local_evaluation import module_name
    from _belief_space_local_evaluation import ObservationState

__all__ = [
    "Card",
    "module_name",
    "ObservationState",
]
