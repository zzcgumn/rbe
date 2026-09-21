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
    # North (declarer) holds only the queen and jack of spades -- East and
    # West hold the ace and king between them, so declarer never wins a
    # spade trick regardless of split or who leads. The club filler (one
    # card each) always goes to West too, so declarer's true double-dummy
    # value over the whole ending is 0. Mirrors double_dummy_bound_test.cpp's
    # own ReproductionRunBTierOneAndTwoTogetherAgainstAQualifyingDelta
    # fixture exactly.
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
    # Mirrors the C++ tests' own VectorLayoutSource: returns exactly the
    # one literal deal given, never re-enumerating a pool split the way
    # ExhaustiveLayoutSource does. Needed here because the solver fixture
    # below names an *exact* East/West split as part of what it tests
    # (declarer never wins a trick under that specific split) --
    # ExhaustiveLayoutSource would instead treat East/West's combined
    # holding as a fungible pool and re-split it across every layout it
    # enumerates, which is a different fixture entirely.
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
    # Mirrors trick.hpp's own seat_on_play(deal) exactly: deal["first"]
    # (this *current node's* own trick leader -- updated trick to trick by
    # whoever won the previous one, not the root's fixed opening leader)
    # advanced by however many cards are already in the trick in progress.
    # This fixture plays more than one trick (unlike every earlier task's
    # own single-trick fixtures, where state.first and this always
    # coincided trivially), so the distinction is load-bearing here.
    played = 0
    for rank in deal["current_trick_rank"]:
        if rank == 0:
            break
        played += 1
    return (deal["first"] + played) % 4


def lowest_legal_card(deal: dict, seat: int):
    # This fixture has two suits (unlike every earlier task's own
    # single-suit fixtures), so a strategy that ignores the led suit is
    # not merely simplified -- it is wrong. Mirrors test_support.hpp's own
    # lowest_legal_card(deal, seat) exactly: follow the led suit if held,
    # otherwise the lowest card in any suit.
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
        # A real test, not an argument from documentation: this module
        # never registers py::class_<SolverContext> itself (see
        # belief_space_local_evaluation.cpp's own comment) -- if it had,
        # this would fail at runtime with a message about incompatible
        # argument types naming the same class twice.
        ctx = dds3.SolverContext()
        defender = DoubleDummyDefender(ctx)
        self.assertIsNotNone(defender)
        bound = DoubleDummyBound(ctx, North)
        self.assertIsNotNone(bound)


class TestOutOfRangeDeclarerRaises(unittest.TestCase):
    # DoubleDummyBound stores declarer at construction (it is fixed for
    # the object's whole lifetime -- see the type's own docstring) and
    # as_bound() later indexes remainCards[declarer_] through
    # tricks_remaining with no range check of its own. Validated here, the
    # same way evaluate()'s own declarer argument is.
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
        # Mirrors ReproductionRunBTierOneAndTwoTogetherAgainstAQualifyingDelta:
        # tier 1 alone and tier 1+2 together both reach the true value
        # (0.0) bitwise, on the exact same fixture.
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

        # Evidence tier 2 did real work tier 1 alone could not: the bound
        # catches the all-dead position at the root itself.
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
        # Not asserting a specific wrong answer (that would pin an
        # implementation accident) -- just that the object itself records
        # which declarer it was built for, since a Python caller has no
        # other way to learn this obligation exists.
        ctx = dds3.SolverContext()
        bound_for_north = DoubleDummyBound(ctx, North)
        bound_for_east = DoubleDummyBound(ctx, East)
        root = make_declarer_never_wins_a_trick()
        # Same layout, two different fixed declarers -- legitimately
        # different questions, so nothing requires the two bounds to
        # agree; this only confirms both are independently callable.
        self.assertIsInstance(bound_for_north(root), int)
        self.assertIsInstance(bound_for_east(root), int)


class TestKeepAlive(unittest.TestCase):
    def test_dropping_the_context_reference_does_not_dangle_the_defender(self) -> None:
        root = make_declarer_never_wins_a_trick()

        def make_defender():
            ctx = dds3.SolverContext()
            return DoubleDummyDefender(ctx)

        defender = make_defender()  # the only remaining reference to ctx is
        # the one py::keep_alive<1, 2>() ties to `defender` itself
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
