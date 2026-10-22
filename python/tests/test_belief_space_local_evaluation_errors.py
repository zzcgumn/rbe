"""Tests for the exception hierarchy: one test per failure cause, and
exceptions raised in Python callbacks crossing evaluate() unchanged."""

import unittest

from belief_space_local_evaluation import BeliefSpaceLocalEvaluationError
from belief_space_local_evaluation import Card
from belief_space_local_evaluation import CardIllegalForTrickError
from belief_space_local_evaluation import CardNotHeldError
from belief_space_local_evaluation import CallbackContractError
from belief_space_local_evaluation import ConstrainedSpaceEmptyError
from belief_space_local_evaluation import ContradictoryVoidError
from belief_space_local_evaluation import DistributionEmptyError
from belief_space_local_evaluation import DuplicatedCardError
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import EvaluationCallback
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import ExpiredBeliefViewError
from belief_space_local_evaluation import HistoryRejectedError
from belief_space_local_evaluation import InvalidHistoryInputError
from belief_space_local_evaluation import LayoutSource
from belief_space_local_evaluation import NoLayoutSurvivedError
from belief_space_local_evaluation import ProbabilitiesDoNotSumToOneError
from belief_space_local_evaluation import ProbabilityNonPositiveError
from belief_space_local_evaluation import RootFailureError
from belief_space_local_evaluation import SampleSizeZeroError
from belief_space_local_evaluation import ScanBudgetExhaustedError
from belief_space_local_evaluation import SourceNotEnumerableError

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def holding(*ranks: int) -> int:
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def make_one_card_finesse_root() -> dict:
    # North/South hold one card each; East/West share a seven-card pool.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(9)
    remain_cards[South][Spades] = holding(8)
    remain_cards[East][Spades] = holding(2)
    remain_cards[West][Spades] = holding(3, 4, 5, 6, 7, 10)
    return {
        "trump": DDS_NOTRUMP,
        "first": East,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def make_two_card_finesse_root() -> dict:
    # The same pool, but East holds two of the seven cards: C(7, 2) = 21
    # splits. That gives a sampled root and replenishment real work. Mirrors
    # the fixture in exhaustive_layout_source_integration_test.cpp.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(9)
    remain_cards[South][Spades] = holding(8)
    remain_cards[East][Spades] = holding(2, 3)
    remain_cards[West][Spades] = holding(4, 5, 6, 7, 10)
    return {
        "trump": DDS_NOTRUMP,
        "first": East,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def lowest_card_in(remain_cards_row) -> Card:
    for suit in range(4):
        mask = remain_cards_row[suit]
        if mask:
            rank = 2
            while not (mask & (1 << rank)):
                rank += 1
            return Card(suit, rank)
    raise AssertionError("seat holds nothing")


def declarer_play(state, view):
    del view
    seat = (state.first + len(state.history)) % 4
    holdings = state.known_holdings["remain_cards"]
    return lowest_card_in(holdings[seat])


def defender_play(layout, seat, state):
    del state
    return [(lowest_card_in(layout["remain_cards"][seat]), 1.0)]


class TestTheHierarchyShape(unittest.TestCase):
    # Three families: a rejected history, from a source's constructor, and
    # a RootFailure and a ValidationError, from evaluate(). All share one
    # base class. A rejected history must not be catchable as
    # NoLayoutSurvived, because the two faults have different fixes.
    def test_every_family_derives_from_the_one_root(self) -> None:
        for cls in (
            HistoryRejectedError,
            ConstrainedSpaceEmptyError,
            RootFailureError,
            CallbackContractError,
            ExpiredBeliefViewError,
        ):
            self.assertTrue(issubclass(cls, BeliefSpaceLocalEvaluationError), cls)

    def test_a_rejected_history_is_not_a_root_failure_and_vice_versa(self) -> None:
        self.assertFalse(issubclass(HistoryRejectedError, RootFailureError))
        self.assertFalse(issubclass(RootFailureError, HistoryRejectedError))
        self.assertFalse(issubclass(NoLayoutSurvivedError, HistoryRejectedError))

    def test_invalid_history_input_is_also_a_value_error(self) -> None:
        # The only history cause that is bad input: a malformed
        # PlayTraceBin, detectable without a root.
        self.assertTrue(issubclass(InvalidHistoryInputError, ValueError))
        self.assertTrue(issubclass(InvalidHistoryInputError, HistoryRejectedError))
        # The other seven causes are a well-formed history that does not fit
        # the root, so they are not ValueError.
        self.assertFalse(issubclass(DuplicatedCardError, ValueError))

    def test_sample_size_zero_is_also_a_value_error(self) -> None:
        self.assertTrue(issubclass(SampleSizeZeroError, ValueError))
        self.assertTrue(issubclass(SampleSizeZeroError, RootFailureError))
        # The other RootFailure causes concern the source's content, not a
        # bad argument, so they are not ValueError.
        self.assertFalse(issubclass(NoLayoutSurvivedError, ValueError))
        self.assertFalse(issubclass(ScanBudgetExhaustedError, ValueError))
        self.assertFalse(issubclass(SourceNotEnumerableError, ValueError))

    def test_the_two_multiple_base_exceptions_report_this_extensions_own_module(self) -> None:
        # These two have two base classes, which py::exception<> does not
        # support. make_exception_with_bases builds them through
        # builtins.type and must set __module__ itself. Otherwise they would
        # report importlib's bootstrap module, not the extension.
        expected = ContradictoryVoidError.__module__  # a py::exception<>-built one, for comparison
        self.assertEqual(InvalidHistoryInputError.__module__, expected)
        self.assertEqual(SampleSizeZeroError.__module__, expected)


class TestEveryRootFailureRaises(unittest.TestCase):
    def test_source_not_enumerable(self) -> None:
        class UnenumerableSource(LayoutSource):
            def size(self):
                return None

            def at(self, index):
                raise AssertionError("never reached")

        root = make_one_card_finesse_root()
        with self.assertRaises(SourceNotEnumerableError):
            evaluate(root, North, 1, UnenumerableSource(), declarer_play, defender_play)

    def test_no_layout_survived(self) -> None:
        class InconsistentSource(LayoutSource):
            def size(self):
                return 1

            def at(self, index):
                deal = make_one_card_finesse_root()
                # Declarer holds a card it does not hold at the root, so
                # make_root drops the layout.
                deal["remain_cards"][North][Spades] = holding(9, 8)
                return deal

        root = make_one_card_finesse_root()
        with self.assertRaises(NoLayoutSurvivedError):
            evaluate(root, North, 1, InconsistentSource(), declarer_play, defender_play)

    def test_scan_budget_exhausted(self) -> None:
        class NeverConsistentSource(LayoutSource):
            def size(self):
                return 1000

            def at(self, index):
                deal = make_one_card_finesse_root()
                deal["remain_cards"][North][Spades] = holding(9, 8)  # always inconsistent
                return deal

        root = make_one_card_finesse_root()
        with self.assertRaises(ScanBudgetExhaustedError):
            evaluate(
                root, North, 1, NeverConsistentSource(), declarer_play, defender_play,
                scan_budget=5)

    def test_sample_size_zero(self) -> None:
        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        with self.assertRaises(SampleSizeZeroError):
            evaluate(root, North, 1, source, declarer_play, defender_play, sample_size=0)


class TestScanBudgetExhaustedIsNotNoLayoutSurvived(unittest.TestCase):
    def test_the_two_root_failures_are_distinguishable(self) -> None:
        # NoLayoutSurvived means fix the source. ScanBudgetExhausted means
        # raise the budget. Merging them would send a caller to debug the
        # wrong thing.
        self.assertFalse(issubclass(ScanBudgetExhaustedError, NoLayoutSurvivedError))
        self.assertFalse(issubclass(NoLayoutSurvivedError, ScanBudgetExhaustedError))


class TestEveryValidationErrorCauseRaisesWithContext(unittest.TestCase):
    def test_card_not_held(self) -> None:
        def bad_defender(layout, seat, state):
            del layout, state
            return [(Card(Hearts, 14), 1.0)]  # nobody holds hearts

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        with self.assertRaises(CardNotHeldError) as ctx:
            evaluate(root, North, 1, source, declarer_play, bad_defender)

        self.assertEqual(ctx.exception.callback, EvaluationCallback.DefenderStrategy)
        self.assertIsInstance(ctx.exception.seat, int)
        self.assertIsInstance(ctx.exception.layout, dict)
        self.assertIn("remain_cards", ctx.exception.layout)

    def test_card_illegal_for_trick(self) -> None:
        # Delta always plays the ♥2 for West. The ♥2 joins the defenders'
        # pool. If East holds a spade, it leads one and West revokes. If
        # East holds the ♥2, West plays a card it does not hold. The test
        # accepts either error.
        def bad_defender(layout, seat, state):
            del state
            if seat == East:
                return [(lowest_card_in(layout["remain_cards"][seat]), 1.0)]
            return [(Card(Hearts, 2), 1.0)]

        root = make_one_card_finesse_root()
        remain_cards = root["remain_cards"]
        remain_cards[West][Hearts] = holding(2)  # West does hold a heart now...
        remain_cards[West][Spades] &= ~holding(3)  # ...but still holds the led suit too
        source = ExhaustiveLayoutSource(root, North, 5)
        with self.assertRaises((CardIllegalForTrickError, CardNotHeldError)):
            evaluate(root, North, 1, source, declarer_play, bad_defender)

    def test_probability_non_positive(self) -> None:
        def bad_defender(layout, seat, state):
            del state
            return [(lowest_card_in(layout["remain_cards"][seat]), 0.0)]

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        with self.assertRaises(ProbabilityNonPositiveError):
            evaluate(root, North, 1, source, declarer_play, bad_defender)

    def test_probabilities_do_not_sum_to_one(self) -> None:
        def bad_defender(layout, seat, state):
            del state
            return [(lowest_card_in(layout["remain_cards"][seat]), 0.5)]

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        with self.assertRaises(ProbabilitiesDoNotSumToOneError):
            evaluate(root, North, 1, source, declarer_play, bad_defender)

    def test_distribution_empty(self) -> None:
        def bad_defender(layout, seat, state):
            del layout, seat, state
            return []

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        with self.assertRaises(DistributionEmptyError):
            evaluate(root, North, 1, source, declarer_play, bad_defender)


class TestBudgetExhaustedScanReturnsAResultNotARaise(unittest.TestCase):
    # A ScanOutcome is a result, not a failure. A scan that finds at least
    # one layout and then hits scan_budget returns a smaller sample. It
    # does not raise.
    def test_a_narrow_scan_budget_still_returns_a_result(self) -> None:
        root = make_two_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        result = evaluate(
            root, North, 1, source, declarer_play, defender_play,
            scan_budget=3, retain_root=True)
        self.assertNotIn("error", result)
        retained = result["by_strategy"][1]["retained_root"]
        self.assertGreaterEqual(len(retained["layouts"]), 1)
        self.assertLessEqual(len(retained["layouts"]), 3)
        self.assertTrue(retained["is_sample"])


class TestExceptionsPropagateThroughTheRecursionUnchanged(unittest.TestCase):
    # An exception raised in pi, delta or a Python source's at() must cross
    # evaluate() with its type and message, with no crash and no leak. The
    # tests raise deep in a sampled, replenishing run, not at the root. A
    # GIL mistake would show there as a crash somewhere unrelated.
    #
    # ctx.exception.__traceback__ is None with pybind11 3.0.1 and Python
    # 3.14. That holds even for a bare LayoutSource.at() override called
    # without evaluate() or any GIL release, so the binding is not the
    # cause. pybind11's error_already_set restores the trace it fetches,
    # but PyErr_Fetch already returns an empty one. The tests check only
    # type and message.

    def test_a_raise_from_pi_deep_in_the_recursion_propagates(self) -> None:
        class Boom(RuntimeError):
            pass

        def raising_declarer_play(state, view):
            del view
            if len(state.history) >= 2:  # not the root ply
                raise Boom("deliberate, from pi, at depth")
            seat = (state.first + len(state.history)) % 4
            holdings = state.known_holdings["remain_cards"]
            return lowest_card_in(holdings[seat])

        root = make_two_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        with self.assertRaises(Boom) as ctx:
            evaluate(
                root, North, 1, source, raising_declarer_play, defender_play,
                sample_size=10, replenish_below=5)
        self.assertEqual(str(ctx.exception), "deliberate, from pi, at depth")

    def test_a_raise_from_delta_deep_in_the_recursion_propagates(self) -> None:
        class Boom(RuntimeError):
            pass

        def raising_defender_play(layout, seat, state):
            if len(state.history) >= 2:
                raise Boom("deliberate, from delta, at depth")
            return [(lowest_card_in(layout["remain_cards"][seat]), 1.0)]

        root = make_two_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        with self.assertRaises(Boom) as ctx:
            evaluate(
                root, North, 1, source, declarer_play, raising_defender_play,
                sample_size=10, replenish_below=5)
        self.assertEqual(str(ctx.exception), "deliberate, from delta, at depth")

    def test_a_raise_from_a_python_sources_at_during_replenishment_propagates(self) -> None:
        class Boom(RuntimeError):
            pass

        class CountingSource(LayoutSource):
            def __init__(self, deals):
                super().__init__()
                self._deals = deals
                self.calls = 0

            def size(self):
                return len(self._deals)

            def at(self, index):
                self.calls += 1
                # Past the root scan (sample_size=4), inside a node-local
                # replenishment scan.
                if self.calls > 10:
                    raise Boom("deliberate, from at(), during replenishment")
                return self._deals[index % len(self._deals)]

        root = make_two_card_finesse_root()
        deals = []
        for east_low, east_high in ((2, 3), (2, 4), (2, 5), (2, 6), (2, 7), (2, 10),
                                     (3, 4), (3, 5), (3, 6), (3, 7), (3, 10),
                                     (4, 5), (4, 6), (4, 7), (4, 10)):
            deal = make_two_card_finesse_root()
            pool = {2, 3, 4, 5, 6, 7, 10}
            deal["remain_cards"][East][Spades] = holding(east_low, east_high)
            deal["remain_cards"][West][Spades] = holding(*(pool - {east_low, east_high}))
            deals.append(deal)
        source = CountingSource(deals)

        with self.assertRaises(Boom) as ctx:
            evaluate(
                root, North, 1, source, declarer_play, defender_play,
                sample_size=4, replenish_below=3)
        self.assertEqual(str(ctx.exception), "deliberate, from at(), during replenishment")
        self.assertGreater(source.calls, 10)


if __name__ == "__main__":
    unittest.main()
