"""Tests for SingleLayoutSource, ExhaustiveLayoutSource and LayoutSource
subclasses written in Python. They cover size and indexing, seeded order,
how a play history shrinks the space, and every history and decomposition
error."""

import unittest

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import CardPlayedAndHeldError
from belief_space_local_evaluation import ConstrainedSpaceStatus
from belief_space_local_evaluation import ContradictoryVoidError
from belief_space_local_evaluation import DuplicatedCardError
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import ForcedExceedsFixedSeatCountError
from belief_space_local_evaluation import HistoryVerdict
from belief_space_local_evaluation import InsufficientFreeCardsError
from belief_space_local_evaluation import InvalidHistoryInputError
from belief_space_local_evaluation import LayoutSource
from belief_space_local_evaluation import LeaderMismatchError
from belief_space_local_evaluation import SingleLayoutSource
from belief_space_local_evaluation import MissingCardError
from belief_space_local_evaluation import PlayRecord
from belief_space_local_evaluation import TrailingTrickMismatchError
from belief_space_local_evaluation import TrickLengthMismatchError
from belief_space_local_evaluation import VoidContradictionError
from belief_space_local_evaluation._belief_space_local_evaluation import (
    _layout_source_at_from_cpp,
)
from belief_space_local_evaluation._belief_space_local_evaluation import (
    _layout_source_size_from_cpp,
)

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4
DDS_HANDS_OUT_OF_RANGE = 4  # DDS_HANDS itself; a valid seat is 0..3


def empty_root() -> dict:
    return {
        "trump": DDS_NOTRUMP,
        "first": North,
        "remain_cards": [[0, 0, 0, 0] for _ in range(4)],
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def build_deal(played, hand_for):
    """Mirrors build_deal in history_verification_test.cpp. `played`, in
    order, becomes the history. Every other card goes to hand_for(suit, rank)."""
    played_set = set(played)
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    for suit in range(4):
        for rank in range(2, 15):
            if (suit, rank) in played_set:
                continue
            hand = hand_for(suit, rank)
            remain_cards[hand][suit] |= 1 << rank

    root = empty_root()
    root["remain_cards"] = remain_cards
    history = [Card(suit, rank) for suit, rank in played]
    return root, history


def everything_to_west(_suit: int, _rank: int) -> int:
    return West


def make_ten_card_pool_root() -> dict:
    # Ten pooled cards, five to each defender: C(10, 5) = 252. Mirrors the
    # fixture of the same name in exhaustive_layout_source_test.cpp.
    root = empty_root()
    root["remain_cards"][North][Spades] = (1 << 14) | (1 << 13)
    root["remain_cards"][South][Hearts] = (1 << 14) | (1 << 13)
    for rank in (2, 3, 4):
        root["remain_cards"][East][Diamonds] |= 1 << rank
    for rank in (5, 6):
        root["remain_cards"][East][Clubs] |= 1 << rank
    for rank in (7, 8):
        root["remain_cards"][West][Diamonds] |= 1 << rank
    for rank in (9, 10, 11):
        root["remain_cards"][West][Clubs] |= 1 << rank
    return root


def make_void_ending():
    # East, the fixed seat, shows out of diamonds on the first trick. West
    # must hold the ♦5, ♦7 and ♦9. Three clubs are free and East needs one:
    # C(3, 1) = 3. Mirrors the fixture in exhaustive_layout_source_test.cpp.
    played = [(Diamonds, 14), (Spades, 2), (Diamonds, 13), (Diamonds, 12)]

    def hand_for(suit: int, rank: int) -> int:
        if suit == Diamonds and rank in (5, 7, 9):
            return West
        if suit == Clubs and rank in (4, 6, 9):
            return East if rank == 4 else West
        return North

    return build_deal(played, hand_for)


def make_two_card_pool_root() -> dict:
    """North and South hold a card each, and the defenders share two cards.
    The two ways to split them give the smallest space where a per-layout mean
    differs from a single layout."""
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = 1 << 13
    remain_cards[South][Spades] = 1 << 12
    remain_cards[East][Spades] = 1 << 3
    remain_cards[West][Spades] = 1 << 2
    return {
        "trump": 4,
        "first": North,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def _lowest_card(remain_cards_row) -> Card:
    for suit in range(4):
        for rank in range(2, 15):
            if remain_cards_row[suit] & (1 << rank):
                return Card(suit, rank)
    raise AssertionError("no card held")


def lowest_declarer_play(state, view):
    del view
    return min(state.legal_cards, key=lambda card: (card.rank, card.suit))


def lowest_defender_play(layout, seat, state):
    del state
    return [(_lowest_card(layout["remain_cards"][seat]), 1.0)]


class TestSingleLayoutSource(unittest.TestCase):
    """A source over exactly one layout.

    It makes a natural check cheap: P_make over a belief space must equal the
    mean of P_make over each layout evaluated alone. That check caught
    DoubleDummyBound returning a "not evaluated" sentinel as a trick count.
    Without this type, a caller must subclass LayoutSource and can get the
    randomised-order rule wrong.
    """

    def test_size_is_one(self) -> None:
        self.assertEqual(SingleLayoutSource(make_ten_card_pool_root()).size(), 1)

    def test_at_zero_is_the_layout(self) -> None:
        layout = make_ten_card_pool_root()
        source = SingleLayoutSource(layout)

        self.assertEqual(source.at(0)["remain_cards"], layout["remain_cards"])
        self.assertEqual(source.at(0)["first"], layout["first"])
        self.assertEqual(source.at(0)["trump"], layout["trump"])

    def test_at_is_repeatable(self) -> None:
        # The LayoutSource contract: the same index returns the same layout.
        source = SingleLayoutSource(make_ten_card_pool_root())
        self.assertEqual(source.at(0), source.at(0))

    def test_at_one_past_the_end_raises_index_error(self) -> None:
        # As for ExhaustiveLayoutSource below: out of range raises IndexError
        # in every build, not an assert.
        with self.assertRaises(IndexError):
            SingleLayoutSource(make_ten_card_pool_root()).at(1)

    def test_a_negative_index_raises_index_error_not_overflow_error(self) -> None:
        with self.assertRaises(IndexError):
            SingleLayoutSource(make_ten_card_pool_root()).at(-1)

    def test_is_a_layout_source(self) -> None:
        self.assertIsInstance(SingleLayoutSource(make_ten_card_pool_root()), LayoutSource)

    def test_it_agrees_with_a_hand_written_one_layout_source(self) -> None:
        # The tests above would pass even if the layout never reached the
        # evaluator correctly. This runs one layout through the bound type and
        # through a hand-written Python subclass.
        root = make_two_card_pool_root()
        layout = ExhaustiveLayoutSource(root, North, 1).at(0)

        class HandWritten(LayoutSource):
            def __init__(self, one):
                super().__init__()
                self._one = one

            def size(self):
                return 1

            def at(self, index):
                del index
                return self._one

        bound = evaluate(root, North, 1, SingleLayoutSource(layout),
                         lowest_declarer_play, lowest_defender_play)
        by_hand = evaluate(root, North, 1, HandWritten(layout),
                           lowest_declarer_play, lowest_defender_play)

        self.assertNotIn("error", bound)
        self.assertNotIn("error", by_hand)
        self.assertEqual(bound["by_strategy"][1]["p_make"],
                         by_hand["by_strategy"][1]["p_make"])

    def test_the_mean_over_layouts_equals_p_make_over_the_space(self) -> None:
        # The check described in the class docstring. pi ignores the view, so
        # P_make over the space must be the mean of P_make per layout.
        root = make_two_card_pool_root()
        space = ExhaustiveLayoutSource(root, North, 1)
        size = space.size()
        self.assertGreater(size, 1, "a one-layout space would make this vacuous")

        whole = evaluate(root, North, 1, space,
                         lowest_declarer_play, lowest_defender_play)
        self.assertNotIn("error", whole)

        total = 0.0
        for index in range(size):
            one = evaluate(root, North, 1, SingleLayoutSource(space.at(index)),
                           lowest_declarer_play, lowest_defender_play)
            self.assertNotIn("error", one)
            total += one["by_strategy"][1]["p_make"]

        self.assertAlmostEqual(total / size, whole["by_strategy"][1]["p_make"], places=12)


class TestConstruction(unittest.TestCase):
    def test_size_matches_hand_derived_count(self) -> None:
        source = ExhaustiveLayoutSource(make_ten_card_pool_root(), North, 1)
        self.assertEqual(source.size(), 252)  # C(10, 5)
        self.assertEqual(source.history_verdict(), HistoryVerdict.Consistent)
        self.assertEqual(source.constrained_space_status(), ConstrainedSpaceStatus.Ok)

    def test_at_is_a_bijection_and_consistent_with_root(self) -> None:
        root = make_ten_card_pool_root()
        source = ExhaustiveLayoutSource(root, North, 1)
        keys = set()
        for index in range(source.size()):
            layout = source.at(index)
            self.assertEqual(layout["trump"], root["trump"])
            keys.add(tuple(tuple(row) for row in layout["remain_cards"]))
        self.assertEqual(len(keys), 252)

    def test_out_of_range_declarer_raises_invalid_history_input(self) -> None:
        with self.assertRaises(InvalidHistoryInputError):
            ExhaustiveLayoutSource(make_ten_card_pool_root(), DDS_HANDS_OUT_OF_RANGE, 1)

    def test_more_than_26_defender_cards_raises_value_error(self) -> None:
        # dict_to_deal checks that each remain_cards mask uses ranks 2..14.
        # It does not check that the defenders hold at most 26 cards
        # together. binomial_coefficient, behind size() and at(), is only
        # defined up to 26. Here East holds all 52 cards. A debug build would
        # assert, and -c opt would silently return 0 or a wrong layout. The
        # constructor rejects the root first.
        root = empty_root()
        for suit in range(4):
            root["remain_cards"][East][suit] = 0x7FFC  # every rank 2..14 in every suit
        with self.assertRaises(ValueError) as ctx:
            ExhaustiveLayoutSource(root, North, 1)
        self.assertIn("26", str(ctx.exception))

    def test_at_one_past_the_end_raises_index_error(self) -> None:
        # In C++, at() only asserts index < total, and -c opt drops the
        # assert. The binding checks the index and raises IndexError, as
        # list_to_history does for its input.
        source = ExhaustiveLayoutSource(make_ten_card_pool_root(), North, 1)
        with self.assertRaises(IndexError):
            source.at(source.size())

    def test_at_a_very_large_index_raises_index_error(self) -> None:
        source = ExhaustiveLayoutSource(make_ten_card_pool_root(), North, 1)
        with self.assertRaises(IndexError):
            source.at(2**63)

    def test_at_an_index_beyond_uint64_raises_index_error_not_overflow_error(self) -> None:
        # 2**64 does not fit in the uint64_t that C++ at() takes. If the
        # binding converted to uint64_t before its range check, pybind11
        # would raise OverflowError instead.
        source = ExhaustiveLayoutSource(make_ten_card_pool_root(), North, 1)
        with self.assertRaises(IndexError):
            source.at(2**64)

    def test_at_a_negative_index_raises_index_error_not_overflow_error(self) -> None:
        # The same failure, and the likelier one: a negative index has no
        # uint64_t value.
        source = ExhaustiveLayoutSource(make_ten_card_pool_root(), North, 1)
        with self.assertRaises(IndexError):
            source.at(-1)

    def test_at_a_non_integral_float_raises_type_error(self) -> None:
        # int(1.5) == 1, so an int()-style conversion would silently return
        # layout 1. Python indexing ([1, 2, 3][1.5]) raises TypeError, and
        # the binding follows operator.index() to match.
        source = ExhaustiveLayoutSource(make_ten_card_pool_root(), North, 1)
        with self.assertRaises(TypeError):
            source.at(1.5)

    def test_at_a_numeric_string_raises_type_error(self) -> None:
        source = ExhaustiveLayoutSource(make_ten_card_pool_root(), North, 1)
        with self.assertRaises(TypeError):
            source.at("1")

class TestDeterminism(unittest.TestCase):
    def test_same_seed_and_root_give_the_same_order(self) -> None:
        root = make_ten_card_pool_root()
        a = ExhaustiveLayoutSource(root, North, 7)
        b = ExhaustiveLayoutSource(root, North, 7)
        for index in range(20):
            self.assertEqual(a.at(index), b.at(index))

    def test_two_seeds_give_different_orders_of_the_same_set(self) -> None:
        root = make_ten_card_pool_root()
        a = ExhaustiveLayoutSource(root, North, 1)
        b = ExhaustiveLayoutSource(root, North, 2)
        total = a.size()
        self.assertEqual(total, b.size())

        def key(layout):
            return tuple(tuple(row) for row in layout["remain_cards"])

        keys_a = [key(a.at(i)) for i in range(total)]
        keys_b = [key(b.at(i)) for i in range(total)]
        self.assertNotEqual(keys_a, keys_b)
        self.assertEqual(set(keys_a), set(keys_b))


class TestHistoryConstrainsTheSpace(unittest.TestCase):
    def test_a_history_shrinks_size_to_the_constrained_count(self) -> None:
        root, history = make_void_ending()
        record = PlayRecord(history, North)
        source = ExhaustiveLayoutSource(root, North, 1, record=record)
        self.assertEqual(source.history_verdict(), HistoryVerdict.Consistent)
        self.assertEqual(source.constrained_space_status(), ConstrainedSpaceStatus.Ok)
        self.assertEqual(source.size(), 3)  # C(3, 1)

        keys = set()
        for index in range(source.size()):
            layout = source.at(index)
            self.assertEqual(layout["remain_cards"][East][Diamonds], 0)
            keys.add(tuple(tuple(row) for row in layout["remain_cards"]))
        self.assertEqual(len(keys), 3)

    def test_omitting_history_reproduces_the_unconstrained_enumeration(self) -> None:
        root, _ = make_void_ending()
        source = ExhaustiveLayoutSource(root, North, 1)
        self.assertEqual(source.history_verdict(), HistoryVerdict.Consistent)
        self.assertEqual(source.constrained_space_status(), ConstrainedSpaceStatus.Ok)
        self.assertGreater(source.size(), 3)


class TestEveryHistoryVerdictCauseRaises(unittest.TestCase):
    def test_invalid_input_out_of_range_declarer_with_a_non_empty_history(self) -> None:
        # The binding checks the declarer before the C++ constructor and
        # verify_history run. Both checks raise the same exception. This
        # pins that a non-empty history does not change that.
        root, history = make_void_ending()
        record = PlayRecord(history, North)
        with self.assertRaises(InvalidHistoryInputError):
            ExhaustiveLayoutSource(root, DDS_HANDS_OUT_OF_RANGE, 1, record=record)

    def test_invalid_input_out_of_range_suit_in_a_history_card(self) -> None:
        # list_to_history rejects the card inside the PlayRecord
        # constructor, before ExhaustiveLayoutSource sees it. It must raise
        # InvalidHistoryInputError, as verify_history does, not a bare
        # ValueError outside the history error hierarchy.
        history = [Card(4, 2)]  # suit 4 is out of range (0..3)
        with self.assertRaises(InvalidHistoryInputError):
            PlayRecord(history, North)

    def test_invalid_input_out_of_range_rank_in_a_history_card(self) -> None:
        history = [Card(Spades, 15)]  # rank 15 is out of range (2..14)
        with self.assertRaises(InvalidHistoryInputError):
            PlayRecord(history, North)

    def test_duplicated_card(self) -> None:
        history = [Card(Spades, 14), Card(Spades, 14)]
        with self.assertRaises(DuplicatedCardError):
            PlayRecord(history, North)

    def test_card_played_and_held(self) -> None:
        root = empty_root()
        root["remain_cards"][North][Spades] = 1 << 14
        history = [Card(Spades, 14)]
        record = PlayRecord(history, North)
        with self.assertRaises(CardPlayedAndHeldError):
            ExhaustiveLayoutSource(root, North, 1, record=record)

    def test_missing_card(self) -> None:
        history = [Card(Spades, 14)]
        record = PlayRecord(history, North)
        with self.assertRaises(MissingCardError):
            ExhaustiveLayoutSource(empty_root(), North, 1, record=record)

    def test_trick_length_mismatch(self) -> None:
        played = [
            (Diamonds, 14),
            (Diamonds, 2),
            (Diamonds, 3),
            (Diamonds, 12),
            (Spades, 13),
            (Spades, 2),
        ]
        root, history = build_deal(played, everything_to_west)
        root["current_trick_suit"] = (Spades, 0, 0)
        root["current_trick_rank"] = (13, 0, 0)
        record = PlayRecord(history, North)
        with self.assertRaises(TrickLengthMismatchError):
            ExhaustiveLayoutSource(root, North, 1, record=record)

    def test_trailing_trick_mismatch(self) -> None:
        played = [
            (Diamonds, 14),
            (Diamonds, 2),
            (Diamonds, 3),
            (Diamonds, 12),
            (Spades, 13),
            (Spades, 2),
        ]
        root, history = build_deal(played, everything_to_west)
        root["current_trick_suit"] = (Spades, Spades, 0)
        root["current_trick_rank"] = (13, 3, 0)  # history actually played the deuce here
        record = PlayRecord(history, North)
        with self.assertRaises(TrailingTrickMismatchError):
            ExhaustiveLayoutSource(root, North, 1, record=record)

    def test_leader_mismatch(self) -> None:
        played = [(Diamonds, 14), (Clubs, 2), (Diamonds, 3), (Diamonds, 12)]
        root, history = build_deal(played, lambda _s, _r: North)
        record = PlayRecord(history, South)
        with self.assertRaises(LeaderMismatchError):
            ExhaustiveLayoutSource(root, North, 1, record=record)

    def test_void_contradiction(self) -> None:
        def hand_for(suit: int, rank: int) -> int:
            if suit == Diamonds and rank == 5:
                return South
            return West

        played = [(Diamonds, 14), (Diamonds, 2), (Spades, 3), (Diamonds, 12)]
        root, history = build_deal(played, hand_for)
        record = PlayRecord(history, North)
        with self.assertRaises(VoidContradictionError):
            ExhaustiveLayoutSource(root, North, 1, record=record)


class TestEveryConstrainedSpaceStatusCauseRaises(unittest.TestCase):
    def test_contradictory_void(self) -> None:
        # Both defenders show out of diamonds, but West's root hand still
        # holds the ♦J. verify_history accepts this: the 52-card partition,
        # the trailing trick and the free cross-check all pass. The
        # constrained decomposition then finds no layout.
        def hand_for(suit: int, rank: int) -> int:
            if suit == Diamonds and rank == 11:
                return West
            return North

        played = [(Diamonds, 14), (Spades, 2), (Diamonds, 13), (Clubs, 3)]
        root, history = build_deal(played, hand_for)
        record = PlayRecord(history, North)
        with self.assertRaises(ContradictoryVoidError):
            ExhaustiveLayoutSource(root, North, 1, record=record)

    def test_forced_exceeds_fixed_seat_count(self) -> None:
        # West shows out of diamonds, so all three outstanding diamonds go to
        # East, the fixed seat. East's root hand has only one card. Mirrors
        # MoreCardsForcedToTheFixedSeatThanItHoldsIsRejected in
        # constrained_decomposition_test.cpp, through a root and history.
        def hand_for(suit: int, rank: int) -> int:
            if suit == Diamonds and rank in (5, 7, 9):
                return West
            if suit == Clubs and rank == 6:
                return East
            return North

        played = [(Diamonds, 14), (Diamonds, 13), (Diamonds, 4), (Spades, 2)]
        root, history = build_deal(played, hand_for)
        record = PlayRecord(history, North)
        with self.assertRaises(ForcedExceedsFixedSeatCountError):
            ExhaustiveLayoutSource(root, North, 1, record=record)

    def test_insufficient_free_cards(self) -> None:
        # East, the fixed seat, shows out of diamonds, so all four outstanding
        # diamonds go to West. East's root hand has four cards. The root holds
        # them in diamonds, which is not checked against the void. Only one
        # free club is left for East. Mirrors
        # TheFixedSeatCannotReachItsHandSizeFromWhatIsLeftIsRejected in
        # constrained_decomposition_test.cpp.
        def hand_for(suit: int, rank: int) -> int:
            if suit == Diamonds and rank in (5, 7, 9, 11):
                return East
            if suit == Clubs and rank == 6:
                return West
            return North

        played = [(Diamonds, 14), (Spades, 2), (Diamonds, 13), (Diamonds, 12)]
        root, history = build_deal(played, hand_for)
        record = PlayRecord(history, North)
        with self.assertRaises(InsufficientFreeCardsError):
            ExhaustiveLayoutSource(root, North, 1, record=record)


class TestRejectedHistoryIsDistinguishableFromNoLayoutSurvived(unittest.TestCase):
    def test_a_rejected_history_does_not_read_as_an_ordinary_empty_source(self) -> None:
        # A rejected history must raise a HistoryRejectedError, not the
        # RootFailure.NoLayoutSurvived of an empty source. The caller fixes
        # the history in one case and the source in the other. PlayRecord
        # raises here, since a duplicated card needs no root.
        # ExhaustiveLayoutSource raises the same class.
        history = [Card(Spades, 14), Card(Spades, 14)]
        with self.assertRaises(DuplicatedCardError) as ctx:
            PlayRecord(history, North)
        self.assertNotIsInstance(ctx.exception, ValueError)


class PythonLayoutSource(LayoutSource):
    def __init__(self, deals):
        super().__init__()
        self._deals = deals

    def size(self):
        return len(self._deals)

    def at(self, index):
        return self._deals[index]


class TestTrampoline(unittest.TestCase):
    def test_a_python_subclass_works_when_consumed_by_cpp(self) -> None:
        # Calling size() or at() from Python never reaches the C++
        # trampoline. The two _layout_source_*_from_cpp probes hold the
        # source as a be::LayoutSource const& and call through it, as
        # evaluate() does.
        deals = [make_ten_card_pool_root(), make_ten_card_pool_root()]
        source = PythonLayoutSource(deals)
        self.assertEqual(_layout_source_size_from_cpp(source), 2)
        self.assertEqual(_layout_source_at_from_cpp(source, 0), deals[0])
        self.assertEqual(_layout_source_at_from_cpp(source, 1), deals[1])


class MissingSizeOverride(LayoutSource):
    def at(self, index):
        return make_ten_card_pool_root()


class MissingAtOverride(LayoutSource):
    def size(self):
        return 1


class ReturnsNoneFromAt(LayoutSource):
    def size(self):
        return 1

    def at(self, index):
        return None


class ReturnsAMalformedDealFromAt(LayoutSource):
    def size(self):
        return 1

    def at(self, index):
        deal = make_ten_card_pool_root()
        deal["trump"] = 99  # out of [0, DDS_STRAINS)
        return deal


class TestMalformedSubclassesFailComprehensibly(unittest.TestCase):
    def test_missing_size_override_raises_rather_than_crashing(self) -> None:
        source = MissingSizeOverride()
        with self.assertRaises(Exception):
            _layout_source_size_from_cpp(source)

    def test_missing_at_override_raises_rather_than_crashing(self) -> None:
        source = MissingAtOverride()
        with self.assertRaises(Exception):
            _layout_source_at_from_cpp(source, 0)

    def test_at_returning_none_is_not_a_valid_deal(self) -> None:
        source = ReturnsNoneFromAt()
        with self.assertRaises(TypeError):
            _layout_source_at_from_cpp(source, 0)

    def test_at_returning_a_malformed_deal_raises_the_converters_value_error(self) -> None:
        # dict_to_deal raises the ValueError. This pins that nothing
        # swallows it.
        source = ReturnsAMalformedDealFromAt()
        with self.assertRaises(ValueError):
            _layout_source_at_from_cpp(source, 0)


if __name__ == "__main__":
    unittest.main()
