"""Tests for `BruteForceDeclarer` from Python. It can be passed to
`evaluate()` as `pi`, as `DoubleDummyDefender` and `HeuristicDefender` can be
passed as `delta`.
"""

import unittest

import dds3

from belief_space_local_evaluation import BruteForceDeclarer
from belief_space_local_evaluation import Card
from belief_space_local_evaluation import DeclarerObjective
from belief_space_local_evaluation import DistributionEmptyError
from belief_space_local_evaluation import DoubleDummyDefender
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import EvaluationCallback
from belief_space_local_evaluation import ExhaustiveLayoutSource

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def holding(*ranks: int) -> int:
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def make_one_card_root() -> dict:
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(14)  # Ace
    remain_cards[South][Spades] = holding(2)
    remain_cards[East][Spades] = holding(13)  # King
    remain_cards[West][Spades] = holding(3)
    return {
        "trump": DDS_NOTRUMP,
        "first": North,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def make_two_card_root() -> dict:
    # Two cards per hand, so the internal search builds and discards several
    # nodes after the opponent model's first call. A kept state that was not
    # copied then has a chance to show stale or reused memory.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(11, 2)  # Jack, Two
    remain_cards[South][Spades] = holding(3, 5)  # Three, Five
    remain_cards[East][Spades] = holding(13, 6)  # King, Six
    remain_cards[West][Spades] = holding(4, 7)  # Four, Seven
    return {
        "trump": DDS_NOTRUMP,
        "first": North,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def lowest_eligible_defender(layout, seat, state):
    del state
    remain = layout["remain_cards"][seat]
    for suit in range(4):
        mask = remain[suit]
        if mask:
            rank = 2
            while not (mask & (1 << rank)):
                rank += 1
            return [(Card(suit, rank), 1.0)]
    raise AssertionError("seat holds nothing")


class TestBruteForceDeclarerIsUsableDirectlyAsPi(unittest.TestCase):
    def test_brute_force_declarer_is_directly_usable_as_pi(self) -> None:
        ctx = dds3.SolverContext()
        pi = BruteForceDeclarer(ctx, DeclarerObjective.MaximizeProbabilityToMake)
        root = make_one_card_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        result = evaluate(root, North, 1, source, pi, lowest_eligible_defender)
        self.assertNotIn("error", result)

    def test_opponent_model_defaults_to_double_dummy_defender(self) -> None:
        ctx = dds3.SolverContext()
        pi_default = BruteForceDeclarer(ctx, DeclarerObjective.MaximizeProbabilityToMake)
        dd = DoubleDummyDefender(ctx)
        pi_explicit = BruteForceDeclarer(ctx, DeclarerObjective.MaximizeProbabilityToMake, dd)

        root = make_one_card_root()
        source = ExhaustiveLayoutSource(root, North, 1)

        result_default = evaluate(root, North, 1, source, pi_default, lowest_eligible_defender)
        result_explicit = evaluate(root, North, 1, source, pi_explicit, lowest_eligible_defender)
        self.assertNotIn("error", result_default)
        self.assertNotIn("error", result_explicit)
        # The binding gives every Python pi the id 1. The id 0 that
        # as_strategy() sets inside BruteForceDeclarer is not used.
        self.assertEqual(result_default["by_strategy"][1]["p_make"], result_explicit["by_strategy"][1]["p_make"])

    def test_a_python_callable_opponent_model_is_accepted(self) -> None:
        ctx = dds3.SolverContext()
        pi = BruteForceDeclarer(
            ctx, DeclarerObjective.MaximizeProbabilityToMake, lowest_eligible_defender)

        root = make_one_card_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        result = evaluate(root, North, 1, source, pi, lowest_eligible_defender)
        self.assertNotIn("error", result)

    def test_a_retained_opponent_model_state_is_not_corrupted_after_evaluate_returns(self) -> None:
        # make_opponent_model casts query.state with
        # py::return_value_policy::copy, as make_defender_strategy does for
        # delta. An opponent model may keep its state argument, like any
        # delta (see test_belief_space_local_evaluation_play_record.py).
        # Without the copy, the kept object would wrap memory the search
        # later changes or frees.
        #
        # Only the first call's state is kept, because later calls see
        # other holdings. During that call, the spade holding is copied into
        # a Python int. After evaluate() returns, the kept state must still
        # read the same value.
        first_call = {}

        def opponent_model(layout, seat, state):
            if not first_call:
                first_call["seat"] = seat
                first_call["state"] = state
                first_call["remain_cards_at_call_time"] = state.known_holdings["remain_cards"][seat][Spades]
            return lowest_eligible_defender(layout, seat, state)

        ctx = dds3.SolverContext()
        pi = BruteForceDeclarer(ctx, DeclarerObjective.MaximizeProbabilityToMake, opponent_model)
        root = make_two_card_root()
        source = ExhaustiveLayoutSource(root, North, 1)

        result = evaluate(root, North, 1, source, pi, lowest_eligible_defender)

        self.assertNotIn("error", result)
        self.assertIn("state", first_call)
        retained = first_call["state"]
        self.assertEqual(retained.trump, DDS_NOTRUMP)
        self.assertEqual(
            retained.known_holdings["remain_cards"][first_call["seat"]][Spades],
            first_call["remain_cards_at_call_time"])

    def test_a_malformed_internal_opponent_model_raises_the_typed_error_not_a_runtime_error(
        self,
    ) -> None:
        # From Python, BruteForceDeclarer is a callable like any pi. The
        # call crosses the pybind boundary before expand_declarer_node can
        # catch DeclarerStrategyContractViolation. PyBruteForceDeclarer::call
        # catches it instead and raises the typed error, with validation,
        # seat and layout. Without that catch the caller would get a plain
        # RuntimeError. See TestEveryValidationErrorCauseRaisesWithContext
        # in test_belief_space_local_evaluation_errors.py.
        def broken_internal_opponent_model(layout, seat, state):
            del layout, seat, state
            return []

        ctx = dds3.SolverContext()
        pi = BruteForceDeclarer(
            ctx, DeclarerObjective.MaximizeProbabilityToMake, broken_internal_opponent_model)

        root = make_one_card_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        with self.assertRaises(DistributionEmptyError) as ctx_manager:
            evaluate(root, North, 1, source, pi, lowest_eligible_defender)

        self.assertEqual(ctx_manager.exception.callback, EvaluationCallback.DefenderStrategy)
        self.assertEqual(ctx_manager.exception.seat, East)
        self.assertEqual(
            ctx_manager.exception.layout["remain_cards"][East][Spades], holding(13))


if __name__ == "__main__":
    unittest.main()
