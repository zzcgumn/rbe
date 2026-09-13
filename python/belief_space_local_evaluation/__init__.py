# dds3 must be imported before the extension below. pybind11 shares a single
# interpreter-level type registry across extensions built by the same
# toolchain; SolverContext is registered once, in dds3, and this module only
# *uses* that registration (see the solver seam) rather than repeating it.
# Importing dds3 first here makes that ordering guaranteed rather than
# dependent on what a caller happened to import first.
import dds3  # noqa: F401

try:
    from ._belief_space_local_evaluation import Card
    from ._belief_space_local_evaluation import CardPlayedAndHeldError
    from ._belief_space_local_evaluation import ConstrainedSpaceEmptyError
    from ._belief_space_local_evaluation import ConstrainedSpaceStatus
    from ._belief_space_local_evaluation import ContradictoryVoidError
    from ._belief_space_local_evaluation import DuplicatedCardError
    from ._belief_space_local_evaluation import ExhaustiveLayoutSource
    from ._belief_space_local_evaluation import ForcedExceedsFixedSeatCountError
    from ._belief_space_local_evaluation import HistoryRejectedError
    from ._belief_space_local_evaluation import HistoryVerdict
    from ._belief_space_local_evaluation import InsufficientFreeCardsError
    from ._belief_space_local_evaluation import InvalidHistoryInputError
    from ._belief_space_local_evaluation import LayoutSource
    from ._belief_space_local_evaluation import LeaderMismatchError
    from ._belief_space_local_evaluation import MissingCardError
    from ._belief_space_local_evaluation import module_name
    from ._belief_space_local_evaluation import ObservationState
    from ._belief_space_local_evaluation import TrailingTrickMismatchError
    from ._belief_space_local_evaluation import TrickLengthMismatchError
    from ._belief_space_local_evaluation import VoidContradictionError
except ImportError:
    # Fallback for environments where _belief_space_local_evaluation is
    # available as a top-level module.
    from _belief_space_local_evaluation import Card
    from _belief_space_local_evaluation import CardPlayedAndHeldError
    from _belief_space_local_evaluation import ConstrainedSpaceEmptyError
    from _belief_space_local_evaluation import ConstrainedSpaceStatus
    from _belief_space_local_evaluation import ContradictoryVoidError
    from _belief_space_local_evaluation import DuplicatedCardError
    from _belief_space_local_evaluation import ExhaustiveLayoutSource
    from _belief_space_local_evaluation import ForcedExceedsFixedSeatCountError
    from _belief_space_local_evaluation import HistoryRejectedError
    from _belief_space_local_evaluation import HistoryVerdict
    from _belief_space_local_evaluation import InsufficientFreeCardsError
    from _belief_space_local_evaluation import InvalidHistoryInputError
    from _belief_space_local_evaluation import LayoutSource
    from _belief_space_local_evaluation import LeaderMismatchError
    from _belief_space_local_evaluation import MissingCardError
    from _belief_space_local_evaluation import module_name
    from _belief_space_local_evaluation import ObservationState
    from _belief_space_local_evaluation import TrailingTrickMismatchError
    from _belief_space_local_evaluation import TrickLengthMismatchError
    from _belief_space_local_evaluation import VoidContradictionError

__all__ = [
    "Card",
    "CardPlayedAndHeldError",
    "ConstrainedSpaceEmptyError",
    "ConstrainedSpaceStatus",
    "ContradictoryVoidError",
    "DuplicatedCardError",
    "ExhaustiveLayoutSource",
    "ForcedExceedsFixedSeatCountError",
    "HistoryRejectedError",
    "HistoryVerdict",
    "InsufficientFreeCardsError",
    "InvalidHistoryInputError",
    "LayoutSource",
    "LeaderMismatchError",
    "MissingCardError",
    "module_name",
    "ObservationState",
    "TrailingTrickMismatchError",
    "TrickLengthMismatchError",
    "VoidContradictionError",
]
