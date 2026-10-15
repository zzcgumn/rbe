"""Tests for `BruteForceDeclarer`'s Python surface: usable directly as
`evaluate()`'s own `pi` argument, the same way `DoubleDummyDefender`/
`HeuristicDefender` are already usable directly as `delta`.
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
    # Two cards per hand, so the internal search's own recursion builds
    # and discards more than one node after the opponent model's first
    # call -- the state-retention test below needs that churn for a
    # retained-but-not-copied state to have a chance of showing stale or
    # reused memory rather than coincidentally-still-live bytes.
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
        # Keyed by pi's strategy id, always 1 for a Python pi -- the id
        # BruteForceDeclarer's own as_strategy() sets internally (0) is
        # never consulted here: evaluate()'s own Python binding wraps
        # whatever py::function is handed to it with its own fixed id.
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
        # make_opponent_model's own py::cast(query.state) used to omit
        # py::return_value_policy::copy -- unlike make_defender_strategy's
        # own cast of a delta's state argument, which has always used it.
        # A Python opponent_model is as free to retain its state argument
        # as any other Python delta is (see
        # test_a_delta_stashed_states_play_record_raises_after_evaluate_
        # returns and its sibling in test_belief_space_local_evaluation_
        # play_record.py for the general hazard this module already
        # guards against elsewhere); without the copy, the retained
        # object wraps memory this search's own internal recursion goes
        # on to mutate or free for later nodes, not a snapshot of the
        # holding at the call that was actually made.
        #
        # The internal search queries this opponent model many times, at
        # many different nodes -- only the *first* call's own state is
        # kept, since every later call would legitimately see a different
        # (correct) holding as the search explores further, which a fixed
        # expected value could not distinguish from genuine corruption.
        # remain_cards_at_call_time is read right there, during the call,
        # into a plain Python int -- an independent snapshot, unaffected
        # by anything the recursion does afterwards -- so comparing the
        # retained state's own reading of the same field *after*
        # evaluate() returns against that snapshot is a direct test of
        # whether the retained object still reflects the call it was
        # actually given.
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
        # BruteForceDeclarer is itself exposed to Python as a plain
        # callable object (__call__), the same shape every Python pi
        # already has -- so when it is used as evaluate()'s own pi *from
        # Python*, as here, the call into it crosses the pybind boundary
        # before expand_declarer_node's own C++-side catch for
        # DeclarerStrategyContractViolation ever gets a chance to run.
        # Without PyBruteForceDeclarer::call's own catch, this raised a
        # generic RuntimeError, losing validation/seat/layout and
        # contradicting the typed-error mapping every other callback
        # contract violation in this module already gets (see
        # TestEveryValidationErrorCauseRaisesWithContext in
        # test_belief_space_local_evaluation_errors.py).
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
