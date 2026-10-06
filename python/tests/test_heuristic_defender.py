import types
import unittest

import dds3
from belief_space_local_evaluation import Card
from belief_space_local_evaluation import DefenderHeuristicChain
from belief_space_local_evaluation import DoubleDummyDefender
from belief_space_local_evaluation import HeuristicDefender
from belief_space_local_evaluation import make_default_defender_heuristics
from belief_space_local_evaluation import second_seat_low

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def holding(*ranks: int) -> int:
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def make_second_seat_layout() -> dict:
    # West leads a spade (already played, consumed from West's hand).
    # North, second to act, holds the suit's only other remaining card --
    # no choice at all, which is all this binding-level test needs: the
    # rule's own bridge logic is already proven in C++.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(2)
    remain_cards[East][Spades] = holding(3)
    remain_cards[South][Spades] = holding(4)
    return {
        "trump": DDS_NOTRUMP,
        "first": West,
        "remain_cards": remain_cards,
        "current_trick_suit": (Spades, 0, 0),
        "current_trick_rank": (14, 0, 0),  # West's ace, already played
    }


class HeuristicDefenderTest(unittest.TestCase):
    def test_second_seat_low_fires_in_second_seat(self):
        layout = make_second_seat_layout()
        ctx = dds3.SolverContext()
        chain = DefenderHeuristicChain()
        chain.add(second_seat_low())
        defender = HeuristicDefender(ctx, chain)

        state = types.SimpleNamespace(declarer=East)
        result = defender(layout, North, state)

        self.assertEqual(len(result), 1)
        card, probability = result[0]
        self.assertEqual(card, Card(Spades, 2))
        self.assertEqual(probability, 1.0)

    def test_make_default_defender_heuristics_builds_the_shipped_order(self):
        layout = make_second_seat_layout()
        ctx = dds3.SolverContext()
        chain = make_default_defender_heuristics(DDS_NOTRUMP)
        defender = HeuristicDefender(ctx, chain)

        state = types.SimpleNamespace(declarer=East)
        result = defender(layout, North, state)

        self.assertEqual(len(result), 1)
        card, probability = result[0]
        self.assertEqual(card, Card(Spades, 2))
        self.assertEqual(probability, 1.0)

    def test_a_python_authored_rule_fires_in_its_place_in_the_chain(self):
        # A plain Python function, not a built-in, added between two
        # built-ins -- fires exactly where its own condition holds (here,
        # "is the seat on play North"), proving a caller's own rule is on
        # equal footing with the shipped ones, with no subclassing and no
        # trampoline class.
        calls = []

        def my_rule(context, best_cards):
            calls.append(context.seat)
            if context.seat == North:
                return Card(Spades, 9)  # a deliberately distinctive answer
            return None

        layout = make_second_seat_layout()
        ctx = dds3.SolverContext()
        chain = DefenderHeuristicChain()
        chain.add(my_rule)
        defender = HeuristicDefender(ctx, chain)

        state = types.SimpleNamespace(declarer=East)
        result = defender(layout, North, state)

        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0], North)
        self.assertEqual(len(result), 1)
        card, probability = result[0]
        self.assertEqual(card, Card(Spades, 9))
        self.assertEqual(probability, 1.0)

    def test_a_python_authored_rule_deferring_falls_through_to_the_next_entry(self):
        # The same custom rule, this time asked about a seat it does not
        # recognise -- it defers (returns None), and the chain falls
        # through to the built-in that follows it.
        def my_rule(context, best_cards):
            if context.seat == East:  # never true in this fixture
                return Card(Spades, 9)
            return None

        layout = make_second_seat_layout()
        ctx = dds3.SolverContext()
        chain = DefenderHeuristicChain()
        chain.add(my_rule)
        chain.add(second_seat_low())
        defender = HeuristicDefender(ctx, chain)

        state = types.SimpleNamespace(declarer=East)
        result = defender(layout, North, state)

        self.assertEqual(len(result), 1)
        card, probability = result[0]
        self.assertEqual(card, Card(Spades, 2))
        self.assertEqual(probability, 1.0)

    def test_heuristic_defender_falls_back_to_spread_when_no_rule_fires(self):
        # The queried seat (East) is the declarer itself: every built-in
        # rule's gate requires a defending seat, so none fire and the
        # result must fall through to spread(), matching DoubleDummyDefender's
        # own output for the same deal and (default) policy.
        remain_cards = [[0, 0, 0, 0] for _ in range(4)]
        remain_cards[North][Spades] = holding(14)  # ace
        remain_cards[East][Spades] = holding(13)  # king
        remain_cards[South][Spades] = holding(3)
        remain_cards[West][Spades] = holding(2)
        layout = {
            "trump": DDS_NOTRUMP,
            "first": East,
            "remain_cards": remain_cards,
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }

        ctx = dds3.SolverContext()
        state = types.SimpleNamespace(declarer=East)

        heuristic_defender = HeuristicDefender(ctx, make_default_defender_heuristics(DDS_NOTRUMP))
        heuristic_result = heuristic_defender(layout, East, state)

        double_dummy_defender = DoubleDummyDefender(ctx)
        double_dummy_result = double_dummy_defender(layout, East, state)

        self.assertEqual(len(heuristic_result), len(double_dummy_result))
        for (h_card, h_prob), (d_card, d_prob) in zip(heuristic_result, double_dummy_result):
            self.assertEqual(h_card, d_card)
            self.assertEqual(h_prob, d_prob)


if __name__ == "__main__":
    unittest.main()
