"""Tests for DoubleDummyDefender and DoubleDummyBound from Python: a dds3
SolverContext passed in, declarer checks, use as delta and as a bound, and
keeping the context alive."""

import gc
import unittest

import dds3
from belief_space_local_evaluation import Card
from belief_space_local_evaluation import DoubleDummyBound
from belief_space_local_evaluation import DoubleDummyDefender
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import LayoutSource
from belief_space_local_evaluation import SpreadPolicy

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def holding(*ranks: int) -> int:
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def make_declarer_never_wins_a_trick() -> dict:
    # North (declarer) holds the ♠Q J. East holds the ♠A 4 and West the ♠K 5,
    # and West holds the top club, so declarer's double-dummy value is 0.
    # Mirrors the fixture of
    # ReproductionRunBTierOneAndTwoTogetherAgainstAQualifyingDelta in
    # double_dummy_bound_test.cpp.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(12, 11)  # Q, J
    remain_cards[South][Spades] = holding(2, 3)
    remain_cards[East][Spades] = holding(14, 4)  # A, 4
    remain_cards[West][Spades] = holding(13, 5)  # K, 5
    remain_cards[North][Clubs] = holding(6)
    remain_cards[South][Clubs] = holding(7)
    remain_cards[East][Clubs] = holding(8)
    remain_cards[West][Clubs] = holding(9)
    return {
        "trump": DDS_NOTRUMP,
        "first": East,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


class FixedLayoutSource(LayoutSource):
    # Like VectorLayoutSource in the C++ tests: returns the one deal given.
    # The fixture depends on its exact East-West split.
    # ExhaustiveLayoutSource would enumerate every split of the pool.
    def __init__(self, deal):
        super().__init__()
        self._deal = deal

    def size(self):
        return 1

    def at(self, index):
        return self._deal


def _lowest_of(mask: int, suit: int):
    rank = 2
    while not (mask & (1 << rank)):
        rank += 1
    return Card(suit, rank)


def seat_on_play(deal: dict) -> int:
    # Mirrors seat_on_play(deal) in trick.hpp. deal["first"] is the leader
    # of the current trick, not the root's opening leader. Add the cards
    # already in the trick. This fixture plays more than one trick, so
    # state.first would be wrong.
    played = 0
    for rank in deal["current_trick_rank"]:
        if rank == 0:
            break
        played += 1
    return (deal["first"] + played) % 4


def lowest_legal_card(deal: dict, seat: int):
    # This fixture has two suits, so the strategy must follow the led suit.
    # Mirrors lowest_legal_card(deal, seat) in test_support.hpp: the lowest
    # card in the led suit if held, else the lowest card in any suit.
    remain_cards = deal["remain_cards"][seat]
    if deal["current_trick_rank"][0] != 0:
        led = deal["current_trick_suit"][0]
        if remain_cards[led] != 0:
            return _lowest_of(remain_cards[led], led)
    for suit in range(4):
        if remain_cards[suit]:
            return _lowest_of(remain_cards[suit], suit)
    raise AssertionError("seat holds nothing")


def declarer_play(state, view):
    del view
    return lowest_legal_card(state.known_holdings, seat_on_play(state.known_holdings))


class TestCrossModuleHandover(unittest.TestCase):
    def test_a_solvercontext_constructed_in_dds3_works_here(self) -> None:
        # This module does not register py::class_<SolverContext>. If it
        # did, this call would fail with an incompatible-argument error that
        # names the same class twice.
        ctx = dds3.SolverContext()
        defender = DoubleDummyDefender(ctx)
        self.assertIsNotNone(defender)
        bound = DoubleDummyBound(ctx, North)
        self.assertIsNotNone(bound)


class TestOutOfRangeDeclarerRaises(unittest.TestCase):
    # DoubleDummyBound fixes declarer at construction. as_bound() indexes
    # remainCards[declarer_] with no range check, so the constructor checks
    # declarer as evaluate() does.
    def test_negative_declarer_raises_value_error(self) -> None:
        ctx = dds3.SolverContext()
        with self.assertRaises(ValueError) as ctx_manager:
            DoubleDummyBound(ctx, -1)
        self.assertIn("declarer", str(ctx_manager.exception))

    def test_declarer_at_dds_hands_raises_value_error(self) -> None:
        ctx = dds3.SolverContext()
        with self.assertRaises(ValueError) as ctx_manager:
            DoubleDummyBound(ctx, 4)  # DDS_HANDS itself
        self.assertIn("declarer", str(ctx_manager.exception))


class TestDoubleDummyDefenderUsableAsDelta(unittest.TestCase):
    def test_reaches_the_same_answer_as_the_c_plus_plus_equivalent(self) -> None:
        # As in the C++ test: tier 1 alone and tiers 1 and 2 together both
        # give exactly 0.0.
        root = make_declarer_never_wins_a_trick()
        ctx = dds3.SolverContext()
        defender = DoubleDummyDefender(ctx)

        source_tier1 = FixedLayoutSource(root)
        tier1_only = evaluate(
            root, North, 1, source_tier1, declarer_play, defender,
            collect_counters=True)
        self.assertNotIn("error", tier1_only)
        self.assertEqual(tier1_only["by_strategy"][1]["p_make"], 0.0)

        bound = DoubleDummyBound(ctx, North)
        source_tier12 = FixedLayoutSource(root)
        tier1_and_2 = evaluate(
            root, North, 1, source_tier12, declarer_play, defender,
            collect_counters=True, bound=bound, delta_is_double_dummy_optimal=True)
        self.assertNotIn("error", tier1_and_2)

        self.assertEqual(tier1_only["by_strategy"][1]["p_make"], tier1_and_2["by_strategy"][1]["p_make"])
        self.assertEqual(tier1_and_2["by_strategy"][1]["p_make"], 0.0)

        # Tier 2 did work that tier 1 could not: the bound cuts the dead
        # position at the root.
        self.assertLess(
            tier1_and_2["by_strategy"][1]["counters"]["nodes_visited"],
            tier1_only["by_strategy"][1]["counters"]["nodes_visited"])
        self.assertEqual(tier1_and_2["by_strategy"][1]["counters"]["nodes_visited"], 1)

    def test_spread_policy_is_exposed_and_both_values_work(self) -> None:
        root = make_declarer_never_wins_a_trick()
        ctx = dds3.SolverContext()
        for policy in (SpreadPolicy.TouchingSequence, SpreadPolicy.AllOptimal):
            defender = DoubleDummyDefender(ctx, policy)
            source = FixedLayoutSource(root)
            result = evaluate(root, North, 1, source, declarer_play, defender)
            self.assertNotIn("error", result)


class TestDoubleDummyBoundFixesDeclarerAtConstruction(unittest.TestCase):
    def test_a_bound_built_for_one_declarer_is_not_reused_for_another(self) -> None:
        # A bound is built for one declarer. This does not pin the wrong
        # answer a misused bound gives, which is an accident of the
        # implementation.
        ctx = dds3.SolverContext()
        bound_for_north = DoubleDummyBound(ctx, North)
        bound_for_east = DoubleDummyBound(ctx, East)
        root = make_declarer_never_wins_a_trick()
        # Two declarers ask different questions, so the answers may
        # differ. Both bounds must be callable.
        self.assertIsInstance(bound_for_north(root), int)
        self.assertIsInstance(bound_for_east(root), int)


class TestKeepAlive(unittest.TestCase):
    def test_dropping_the_context_reference_does_not_dangle_the_defender(self) -> None:
        root = make_declarer_never_wins_a_trick()

        def make_defender():
            ctx = dds3.SolverContext()
            return DoubleDummyDefender(ctx)

        defender = make_defender()  # only py::keep_alive<1, 2>() now
        # holds ctx, through `defender`
        gc.collect()

        source = FixedLayoutSource(root)
        result = evaluate(root, North, 1, source, declarer_play, defender)
        self.assertNotIn("error", result)
        self.assertEqual(result["by_strategy"][1]["p_make"], 0.0)

    def test_dropping_the_context_reference_does_not_dangle_the_bound(self) -> None:
        root = make_declarer_never_wins_a_trick()

        def make_bound():
            ctx = dds3.SolverContext()
            return DoubleDummyBound(ctx, North)

        bound = make_bound()
        gc.collect()

        self.assertIsInstance(bound(root), int)


if __name__ == "__main__":
    unittest.main()
