"""Tests for `BruteForceDeclarer`'s Python surface: usable directly as
`evaluate()`'s own `pi` argument, the same way `DoubleDummyDefender`/
`HeuristicDefender` are already usable directly as `delta`.
"""

import unittest

import dds3

from belief_space_local_evaluation import BruteForceDeclarer
from belief_space_local_evaluation import Card
from belief_space_local_evaluation import DeclarerObjective
from belief_space_local_evaluation import DoubleDummyDefender
from belief_space_local_evaluation import evaluate
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


if __name__ == "__main__":
    unittest.main()
