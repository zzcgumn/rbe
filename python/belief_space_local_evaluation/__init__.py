# dds3 must be imported before the extension below. pybind11 shares a single
# interpreter-level type registry across extensions built by the same
# toolchain; SolverContext is registered once, in dds3, and this module only
# *uses* that registration (see the solver seam) rather than repeating it.
# Importing dds3 first here makes that ordering guaranteed rather than
# dependent on what a caller happened to import first.
import dds3  # noqa: F401

try:
    from ._belief_space_local_evaluation import BeliefEntry
    from ._belief_space_local_evaluation import BeliefSpaceLocalEvaluationError
    from ._belief_space_local_evaluation import BeliefView
    from ._belief_space_local_evaluation import BruteForceDeclarer
    from ._belief_space_local_evaluation import CallbackContractError
    from ._belief_space_local_evaluation import Card
    from ._belief_space_local_evaluation import CardIllegalForTrickError
    from ._belief_space_local_evaluation import CardNotHeldError
    from ._belief_space_local_evaluation import CardPlayedAndHeldError
    from ._belief_space_local_evaluation import ConstrainedSpaceEmptyError
    from ._belief_space_local_evaluation import ConstrainedSpaceStatus
    from ._belief_space_local_evaluation import ContradictoryVoidError
    from ._belief_space_local_evaluation import DeclarerObjective
    from ._belief_space_local_evaluation import DefenderHeuristic
    from ._belief_space_local_evaluation import DefenderHeuristicChain
    from ._belief_space_local_evaluation import DefenderHeuristicContext
    from ._belief_space_local_evaluation import discard_keep_winners
    from ._belief_space_local_evaluation import DistributionEmptyError
    from ._belief_space_local_evaluation import DoubleDummyBound
    from ._belief_space_local_evaluation import DoubleDummyDefender
    from ._belief_space_local_evaluation import DuplicatedCardError
    from ._belief_space_local_evaluation import evaluate
    from ._belief_space_local_evaluation import EvaluationCallback
    from ._belief_space_local_evaluation import ExhaustiveLayoutSource
    from ._belief_space_local_evaluation import ExpiredBeliefViewError
    from ._belief_space_local_evaluation import ForcedExceedsFixedSeatCountError
    from ._belief_space_local_evaluation import fourth_seat_low
    from ._belief_space_local_evaluation import HeuristicDefender
    from ._belief_space_local_evaluation import high_in_third
    from ._belief_space_local_evaluation import HistoryRejectedError
    from ._belief_space_local_evaluation import HistoryVerdict
    from ._belief_space_local_evaluation import InsufficientFreeCardsError
    from ._belief_space_local_evaluation import InvalidHistoryInputError
    from ._belief_space_local_evaluation import LayoutSource
    from ._belief_space_local_evaluation import LeaderMismatchError
    from ._belief_space_local_evaluation import legal_cards
    from ._belief_space_local_evaluation import make_default_defender_heuristics
    from ._belief_space_local_evaluation import MissingCardError
    from ._belief_space_local_evaluation import module_name
    from ._belief_space_local_evaluation import NoLayoutSurvivedError
    from ._belief_space_local_evaluation import ObservationState
    from ._belief_space_local_evaluation import play
    from ._belief_space_local_evaluation import play_out
    from ._belief_space_local_evaluation import PlayRecord
    from ._belief_space_local_evaluation import ProbabilitiesDoNotSumToOneError
    from ._belief_space_local_evaluation import ProbabilityNonPositiveError
    from ._belief_space_local_evaluation import RankMap
    from ._belief_space_local_evaluation import RootFailure
    from ._belief_space_local_evaluation import RootFailureError
    from ._belief_space_local_evaluation import ruff_small
    from ._belief_space_local_evaluation import SampleSizeZeroError
    from ._belief_space_local_evaluation import ScanBudgetExhaustedError
    from ._belief_space_local_evaluation import second_seat_low
    from ._belief_space_local_evaluation import SingleLayoutSource
    from ._belief_space_local_evaluation import seat_on_play
    from ._belief_space_local_evaluation import SourceNotEnumerableError
    from ._belief_space_local_evaluation import SpreadPolicy
    from ._belief_space_local_evaluation import third_seat_low
    from ._belief_space_local_evaluation import TrailingTrickMismatchError
    from ._belief_space_local_evaluation import trick_complete_winner
    from ._belief_space_local_evaluation import TrickLengthMismatchError
    from ._belief_space_local_evaluation import ValidationError
    from ._belief_space_local_evaluation import VoidContradictionError
except ImportError:
    # Fallback for environments where _belief_space_local_evaluation is
    # available as a top-level module.
    from _belief_space_local_evaluation import BeliefEntry
    from _belief_space_local_evaluation import BeliefSpaceLocalEvaluationError
    from _belief_space_local_evaluation import BeliefView
    from _belief_space_local_evaluation import BruteForceDeclarer
    from _belief_space_local_evaluation import CallbackContractError
    from _belief_space_local_evaluation import Card
    from _belief_space_local_evaluation import CardIllegalForTrickError
    from _belief_space_local_evaluation import CardNotHeldError
    from _belief_space_local_evaluation import CardPlayedAndHeldError
    from _belief_space_local_evaluation import ConstrainedSpaceEmptyError
    from _belief_space_local_evaluation import ConstrainedSpaceStatus
    from _belief_space_local_evaluation import ContradictoryVoidError
    from _belief_space_local_evaluation import DeclarerObjective
    from _belief_space_local_evaluation import DefenderHeuristic
    from _belief_space_local_evaluation import DefenderHeuristicChain
    from _belief_space_local_evaluation import DefenderHeuristicContext
    from _belief_space_local_evaluation import discard_keep_winners
    from _belief_space_local_evaluation import DistributionEmptyError
    from _belief_space_local_evaluation import DoubleDummyBound
    from _belief_space_local_evaluation import DoubleDummyDefender
    from _belief_space_local_evaluation import DuplicatedCardError
    from _belief_space_local_evaluation import evaluate
    from _belief_space_local_evaluation import EvaluationCallback
    from _belief_space_local_evaluation import ExhaustiveLayoutSource
    from _belief_space_local_evaluation import ExpiredBeliefViewError
    from _belief_space_local_evaluation import ForcedExceedsFixedSeatCountError
    from _belief_space_local_evaluation import fourth_seat_low
    from _belief_space_local_evaluation import HeuristicDefender
    from _belief_space_local_evaluation import high_in_third
    from _belief_space_local_evaluation import HistoryRejectedError
    from _belief_space_local_evaluation import HistoryVerdict
    from _belief_space_local_evaluation import InsufficientFreeCardsError
    from _belief_space_local_evaluation import InvalidHistoryInputError
    from _belief_space_local_evaluation import LayoutSource
    from _belief_space_local_evaluation import LeaderMismatchError
    from _belief_space_local_evaluation import legal_cards
    from _belief_space_local_evaluation import make_default_defender_heuristics
    from _belief_space_local_evaluation import MissingCardError
    from _belief_space_local_evaluation import module_name
    from _belief_space_local_evaluation import NoLayoutSurvivedError
    from _belief_space_local_evaluation import ObservationState
    from _belief_space_local_evaluation import play
    from _belief_space_local_evaluation import play_out
    from _belief_space_local_evaluation import PlayRecord
    from _belief_space_local_evaluation import ProbabilitiesDoNotSumToOneError
    from _belief_space_local_evaluation import ProbabilityNonPositiveError
    from _belief_space_local_evaluation import RankMap
    from _belief_space_local_evaluation import RootFailure
    from _belief_space_local_evaluation import RootFailureError
    from _belief_space_local_evaluation import ruff_small
    from _belief_space_local_evaluation import SampleSizeZeroError
    from _belief_space_local_evaluation import ScanBudgetExhaustedError
    from _belief_space_local_evaluation import second_seat_low
    from _belief_space_local_evaluation import SingleLayoutSource
    from _belief_space_local_evaluation import seat_on_play
    from _belief_space_local_evaluation import SourceNotEnumerableError
    from _belief_space_local_evaluation import SpreadPolicy
    from _belief_space_local_evaluation import third_seat_low
    from _belief_space_local_evaluation import TrailingTrickMismatchError
    from _belief_space_local_evaluation import trick_complete_winner
    from _belief_space_local_evaluation import TrickLengthMismatchError
    from _belief_space_local_evaluation import ValidationError
    from _belief_space_local_evaluation import VoidContradictionError

# Not from the compiled extension, so not part of either branch above:
# play_sequence.py is plain Python with no compiled-extension dependency of
# its own. It does `import belief_space_local_evaluation` (this package,
# still initialising at this point), but only inside function bodies -- no
# module-level code in play_sequence.py calls back into this package eagerly,
# so the partially-initialised module object already in sys.modules is
# sufficient, and by the time any caller actually invokes a PlaySequence
# method, this __init__ has long finished running.
from .play_sequence import PlaySequence  # noqa: E402

__all__ = [
    "BeliefEntry",
    "BeliefSpaceLocalEvaluationError",
    "BeliefView",
    "BruteForceDeclarer",
    "CallbackContractError",
    "Card",
    "CardIllegalForTrickError",
    "CardNotHeldError",
    "CardPlayedAndHeldError",
    "ConstrainedSpaceEmptyError",
    "ConstrainedSpaceStatus",
    "ContradictoryVoidError",
    "DeclarerObjective",
    "DefenderHeuristic",
    "DefenderHeuristicChain",
    "DefenderHeuristicContext",
    "discard_keep_winners",
    "DistributionEmptyError",
    "DoubleDummyBound",
    "DoubleDummyDefender",
    "DuplicatedCardError",
    "evaluate",
    "EvaluationCallback",
    "ExhaustiveLayoutSource",
    "ExpiredBeliefViewError",
    "ForcedExceedsFixedSeatCountError",
    "fourth_seat_low",
    "HeuristicDefender",
    "high_in_third",
    "HistoryRejectedError",
    "HistoryVerdict",
    "InsufficientFreeCardsError",
    "InvalidHistoryInputError",
    "LayoutSource",
    "LeaderMismatchError",
    "legal_cards",
    "make_default_defender_heuristics",
    "MissingCardError",
    "module_name",
    "NoLayoutSurvivedError",
    "ObservationState",
    "play",
    "play_out",
    "PlayRecord",
    "PlaySequence",
    "ProbabilitiesDoNotSumToOneError",
    "ProbabilityNonPositiveError",
    "RankMap",
    "RootFailure",
    "RootFailureError",
    "ruff_small",
    "SampleSizeZeroError",
    "ScanBudgetExhaustedError",
    "second_seat_low",
    "SingleLayoutSource",
    "seat_on_play",
    "SourceNotEnumerableError",
    "SpreadPolicy",
    "third_seat_low",
    "TrailingTrickMismatchError",
    "trick_complete_winner",
    "TrickLengthMismatchError",
    "ValidationError",
    "VoidContradictionError",
]
