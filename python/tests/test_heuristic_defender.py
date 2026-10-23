"""Tests for HeuristicDefender and DefenderHeuristicChain from Python: built-in
and Python-written rules, the spread() fallback, and the lifetime of what a
rule is handed."""

import types
import unittest

import dds3
from belief_space_local_evaluation import Card
from belief_space_local_evaluation import DefenderHeuristicChain
from belief_space_local_evaluation import DoubleDummyDefender
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import HeuristicDefender
from belief_space_local_evaluation import make_default_defender_heuristics
from belief_space_local_evaluation import PlayRecord
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
    # West has led the ♠A. North, second to play, holds only the ♠2, so it
    # has no choice. The C++ tests cover the rule's bridge logic.
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
        # A plain Python function works as a chain rule, with no
        # subclassing. It fires when the seat on play is North. North holds
        # ♠K 2 and the rule picks the king, which second_seat_low would not.
        # So the answer must come from this rule.
        calls = []

        def my_rule(context, best_cards):
            calls.append(context.seat)
            if context.seat == North:
                return Card(Spades, 13)  # the king, which North holds
            return None

        remain_cards = [[0, 0, 0, 0] for _ in range(4)]
        remain_cards[West][Hearts] = holding(2)  # West's one card left, having led its spade
        remain_cards[North][Spades] = holding(2, 13)
        remain_cards[East][Spades] = holding(4)
        remain_cards[East][Hearts] = holding(3)
        remain_cards[South][Hearts] = holding(4, 5)
        layout = {
            "trump": DDS_NOTRUMP,
            "first": West,
            "remain_cards": remain_cards,
            "current_trick_suit": (Spades, 0, 0),
            "current_trick_rank": (3, 0, 0),
        }

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
        self.assertEqual(card, Card(Spades, 13))
        self.assertEqual(probability, 1.0)

    def test_a_misbehaving_rule_returning_an_invalid_card_falls_back_to_spread(self):
        # Nothing constrains what a chain rule returns. A card the seat does
        # not hold, and the solve did not report, must not reach the
        # caller. The C++ tests check the same guarantee.
        def misbehaving_rule(context, best_cards):
            return Card(Spades, 9)

        layout = make_second_seat_layout()
        ctx = dds3.SolverContext()
        chain = DefenderHeuristicChain()
        chain.add(misbehaving_rule)
        defender = HeuristicDefender(ctx, chain)

        state = types.SimpleNamespace(declarer=East)
        result = defender(layout, North, state)

        # The nine is rejected. spread() returns North's only card, the
        # two.
        self.assertEqual(len(result), 1)
        card, probability = result[0]
        self.assertEqual(card, Card(Spades, 2))
        self.assertEqual(probability, 1.0)

    def test_a_custom_rule_sees_the_full_bound_state_not_just_declarer(self):
        # The direct calls in this file pass a SimpleNamespace with only
        # .declarer. evaluate() passes a full ObservationState, the one pi
        # receives. A chain rule must see all of it through context.state.
        stashed = {}

        def stashing_rule(context, best_cards):
            stashed["tricks_needed"] = context.state.tricks_needed
            return None

        # solve_board needs equal hand sizes: two spades and a club each.
        # Mirrors make_solver_seam_root in
        # test_belief_space_local_evaluation_docs_examples.py.
        remain_cards = [[0, 0, 0, 0] for _ in range(4)]
        remain_cards[North][Spades] = holding(12, 11)
        remain_cards[South][Spades] = holding(2, 3)
        remain_cards[East][Spades] = holding(14, 4)
        remain_cards[West][Spades] = holding(13, 5)
        remain_cards[North][Clubs] = holding(6)
        remain_cards[South][Clubs] = holding(7)
        remain_cards[East][Clubs] = holding(8)
        remain_cards[West][Clubs] = holding(9)
        root = {
            "trump": DDS_NOTRUMP,
            "first": East,
            "remain_cards": remain_cards,
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }

        def lowest_card_in(remain_cards_row):
            for suit in range(4):
                mask = remain_cards_row[suit]
                if mask:
                    rank = 2
                    while not (mask & (1 << rank)):
                        rank += 1
                    return Card(suit, rank)
            raise AssertionError("seat holds nothing")

        def lowest_legal_card(deal, seat):
            remain_cards = deal["remain_cards"][seat]
            if deal["current_trick_rank"][0] != 0:
                led = deal["current_trick_suit"][0]
                if remain_cards[led] != 0:
                    return lowest_card_in([remain_cards[led] if s == led else 0 for s in range(4)])
            return lowest_card_in(remain_cards)

        def declarer_play(state, view):
            del view
            return lowest_legal_card(state.known_holdings, state.seat_on_play)

        source = ExhaustiveLayoutSource(root, North, 1)
        ctx = dds3.SolverContext()
        chain = DefenderHeuristicChain()
        chain.add(stashing_rule)
        defender = HeuristicDefender(ctx, chain)

        tricks_needed = 1
        result = evaluate(root, North, tricks_needed, source, declarer_play, defender)

        self.assertNotIn("error", result)
        self.assertEqual(stashed.get("tricks_needed"), tricks_needed)

    def test_a_stashed_states_play_record_raises_rather_than_reading_freed_memory(self):
        # context.state is a copy, so a rule may keep it after the call.
        # The context itself may not be kept. play_record in the copy points
        # into the caller's EvaluateOptions, which may not outlive
        # evaluate(). After evaluate() returns, reading .play_record must
        # raise through the play_record_valid guard, as for pi and delta.
        # It must not read freed memory. It must not return None either,
        # because the other fields stay valid.
        stashed = {}

        def stashing_rule(context, best_cards):
            # Read during the call: the guard only blocks late reads.
            stashed["live_play_record"] = context.state.play_record
            stashed["state"] = context.state
            return None

        remain_cards = [[0, 0, 0, 0] for _ in range(4)]
        remain_cards[North][Spades] = holding(12, 11)
        remain_cards[South][Spades] = holding(2, 3)
        remain_cards[East][Spades] = holding(14, 4)
        remain_cards[West][Spades] = holding(13, 5)
        remain_cards[North][Clubs] = holding(6)
        remain_cards[South][Clubs] = holding(7)
        remain_cards[East][Clubs] = holding(8)
        remain_cards[West][Clubs] = holding(9)
        root = {
            "trump": DDS_NOTRUMP,
            "first": East,
            "remain_cards": remain_cards,
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }

        def lowest_card_in(remain_cards_row):
            for suit in range(4):
                mask = remain_cards_row[suit]
                if mask:
                    rank = 2
                    while not (mask & (1 << rank)):
                        rank += 1
                    return Card(suit, rank)
            raise AssertionError("seat holds nothing")

        def lowest_legal_card(deal, seat):
            remain_cards = deal["remain_cards"][seat]
            if deal["current_trick_rank"][0] != 0:
                led = deal["current_trick_suit"][0]
                if remain_cards[led] != 0:
                    return lowest_card_in([remain_cards[led] if s == led else 0 for s in range(4)])
            return lowest_card_in(remain_cards)

        def declarer_play(state, view):
            del view
            return lowest_legal_card(state.known_holdings, state.seat_on_play)

        source = ExhaustiveLayoutSource(root, North, 1)
        ctx = dds3.SolverContext()
        chain = DefenderHeuristicChain()
        chain.add(stashing_rule)
        defender = HeuristicDefender(ctx, chain)

        record = PlayRecord([], East)
        result = evaluate(root, North, 1, source, declarer_play, defender, play_record=record)

        self.assertNotIn("error", result)
        self.assertIn("state", stashed)
        self.assertIsNotNone(stashed["live_play_record"])
        self.assertEqual(stashed["live_play_record"].opening_leader, East)
        # The other fields of the kept copy still read correctly. Only
        # play_record raises.
        self.assertEqual(stashed["state"].tricks_needed, 1)
        with self.assertRaises(ValueError):
            stashed["state"].play_record

    def test_a_stashed_context_raises_after_the_rule_that_received_it_returns(self):
        # A DefenderHeuristicContext is valid only during the rule call.
        # Reading a kept one later must raise, not read freed memory.
        stashed = {}

        def stashing_rule(context, best_cards):
            stashed["context"] = context
            return None

        layout = make_second_seat_layout()
        ctx = dds3.SolverContext()
        chain = DefenderHeuristicChain()
        chain.add(stashing_rule)
        chain.add(second_seat_low())
        defender = HeuristicDefender(ctx, chain)

        state = types.SimpleNamespace(declarer=East)
        defender(layout, North, state)

        with self.assertRaises(ValueError):
            stashed["context"].seat
        with self.assertRaises(ValueError):
            stashed["context"].state

    def test_a_python_authored_rule_deferring_falls_through_to_the_next_entry(self):
        # The rule returns None for this seat, so the chain falls through to
        # the next rule, a built-in.
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
        # East, the queried seat, is the declarer. Every built-in rule needs
        # a defending seat, so none fires. The result falls through to
        # spread() and must match DoubleDummyDefender with its default
        # policy.
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
