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
        # trampoline class. North holds two candidates here (not one, as
        # make_second_seat_layout's forced position does) so the rule's
        # own answer -- the high one, the opposite of what second_seat_low
        # would pick -- is distinctive proof it is this rule's return
        # value flowing through, not an accident of a forced position.
        calls = []

        def my_rule(context, best_cards):
            calls.append(context.seat)
            if context.seat == North:
                return Card(Spades, 13)  # the king: distinctive, and genuinely held
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
        # A chain rule is a plain callable with no interface constraining
        # what it returns. One that always answers with a card the seat
        # does not even hold, and that the solve never reported, must
        # never actually reach the caller -- proven here through the
        # Python binding, mirroring the C++-level proof of the same
        # guarantee.
        def misbehaving_rule(context, best_cards):
            return Card(Spades, 9)

        layout = make_second_seat_layout()
        ctx = dds3.SolverContext()
        chain = DefenderHeuristicChain()
        chain.add(misbehaving_rule)
        defender = HeuristicDefender(ctx, chain)

        state = types.SimpleNamespace(declarer=East)
        result = defender(layout, North, state)

        # North's only legal card in this layout is the two -- exactly
        # what spread() falls back to once the misbehaving rule's answer
        # is rejected, not the invalid nine it tried to return.
        self.assertEqual(len(result), 1)
        card, probability = result[0]
        self.assertEqual(card, Card(Spades, 2))
        self.assertEqual(probability, 1.0)

    def test_a_custom_rule_sees_the_full_bound_state_not_just_declarer(self):
        # Every standalone call elsewhere in this file hands HeuristicDefender
        # a types.SimpleNamespace exposing only .declarer -- deliberately
        # minimal, for a direct call with nothing else available. evaluate()
        # calls its own defender argument differently: with a real,
        # fully-populated ObservationState, the same one a declarer
        # strategy's play() receives. A custom chain rule must see the
        # whole thing through context.state in that case, not just
        # whatever the declarer-only stand-in path would give it.
        stashed = {}

        def stashing_rule(context, best_cards):
            stashed["tricks_needed"] = context.state.tricks_needed
            return None

        # solve_board requires all four hands to hold the same number of
        # cards -- an ordinary well-formed ending, not merely something
        # belief_evaluation's own machinery tolerates (see
        # test_belief_space_local_evaluation_docs_examples.py's own
        # make_solver_seam_root, which this mirrors): two spades and one
        # club filler each.
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

    def test_a_stashed_states_play_record_is_cleared_rather_than_left_dangling(self):
        # context.state() returns a copy -- documented safe to retain past
        # the one rule call it was handed to, unlike the context itself
        # (see the test above and the one below). But one field of that
        # copy, play_record, is a non-owning pointer into the
        # EvaluateOptions this evaluation was called with
        # (types.hpp's own doxygen on ObservationState::play_record), which
        # has no guarantee of outliving this call at all, let alone a rule
        # stashing the copy past it. evaluate() itself already clears this
        # same field for EvaluationValue::retained_root for exactly this
        # reason (evaluate.cpp); context.state() must do the same rather
        # than hand back a value whose play_record reads freed memory the
        # moment this test's own evaluate() call returns.
        stashed = {}

        def stashing_rule(context, best_cards):
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
        # The rest of the copy is still exactly what the live call saw --
        # only play_record, the one field with no safe lifetime past this
        # call, is cleared.
        self.assertEqual(stashed["state"].tricks_needed, 1)
        self.assertIsNone(stashed["state"].play_record)

    def test_a_stashed_context_raises_after_the_rule_that_received_it_returns(self):
        # DefenderHeuristicContext is only valid for the duration of the
        # one rule call it is handed to -- stashing it and reading it
        # later must raise, not read freed memory.
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
