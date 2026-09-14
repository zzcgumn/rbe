import unittest

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import CardPlayedAndHeldError
from belief_space_local_evaluation import ConstrainedSpaceStatus
from belief_space_local_evaluation import ContradictoryVoidError
from belief_space_local_evaluation import DuplicatedCardError
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import ForcedExceedsFixedSeatCountError
from belief_space_local_evaluation import HistoryVerdict
from belief_space_local_evaluation import InsufficientFreeCardsError
from belief_space_local_evaluation import InvalidHistoryInputError
from belief_space_local_evaluation import LayoutSource
from belief_space_local_evaluation import LeaderMismatchError
from belief_space_local_evaluation import MissingCardError
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
    """Mirrors history_verification_test.cpp's own build_deal: `played`
    (in order) becomes the history, every other card of the 52-card deck
    goes to hand_for(suit, rank)."""
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
    # Ten pooled cards, five each -- C(10, 5) = 252. Mirrors
    # exhaustive_layout_source_test.cpp's own fixture of the same name.
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
    # East (the fixed seat) has shown void in diamonds; diamonds remain in
    # the pool. D5/D7/D9 to West; three free clubs, East still needs one:
    # C(3, 1) = 3. Mirrors exhaustive_layout_source_test.cpp's own fixture.
    played = [(Diamonds, 14), (Spades, 2), (Diamonds, 13), (Diamonds, 12)]

    def hand_for(suit: int, rank: int) -> int:
        if suit == Diamonds and rank in (5, 7, 9):
            return West
        if suit == Clubs and rank in (4, 6, 9):
            return East if rank == 4 else West
        return North

    return build_deal(played, hand_for)


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

    def test_at_one_past_the_end_raises_index_error(self) -> None:
        # ExhaustiveLayoutSource::at()'s own C++ precondition is an
        # assert(index < total) -- a last resort against undefined
        # behaviour, not a diagnostic, and a no-op entirely once built
        # -c opt. Translated to a real IndexError at this binding boundary
        # rather than exposed directly, the same reasoning
        # list_to_history's own comment gives for why a boundary check
        # exists at all here.
        source = ExhaustiveLayoutSource(make_ten_card_pool_root(), North, 1)
        with self.assertRaises(IndexError):
            source.at(source.size())

    def test_at_a_very_large_index_raises_index_error(self) -> None:
        source = ExhaustiveLayoutSource(make_ten_card_pool_root(), North, 1)
        with self.assertRaises(IndexError):
            source.at(2**63)

    def test_out_of_range_opening_leader_raises_even_with_no_history(self) -> None:
        # The C++ constructor skips verify_history entirely when history is
        # empty (its own doxygen: "not checked against root at all"), so
        # opening_leader reaches derive_voids completely unvalidated in
        # that branch and trips its assert. This binding checks
        # declarer/opening_leader itself, unconditionally, before ever
        # constructing the C++ source -- so this must raise the same clean
        # exception an out-of-range declarer does, not abort the process.
        with self.assertRaises(InvalidHistoryInputError):
            ExhaustiveLayoutSource(
                make_ten_card_pool_root(), North, 1, [], opening_leader=DDS_HANDS_OUT_OF_RANGE)
        with self.assertRaises(InvalidHistoryInputError):
            ExhaustiveLayoutSource(make_ten_card_pool_root(), North, 1, [], opening_leader=-1)


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
        source = ExhaustiveLayoutSource(root, North, 1, history, North)
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
        # Caught by this binding's own declarer/opening_leader pre-check
        # (see test_out_of_range_opening_leader_raises_even_with_no_history)
        # before the C++ constructor -- and so before verify_history's own
        # InvalidInput -- ever runs. Same exception either way; this pins
        # that a non-empty history does not change that.
        root, history = make_void_ending()
        with self.assertRaises(InvalidHistoryInputError):
            ExhaustiveLayoutSource(root, DDS_HANDS_OUT_OF_RANGE, 1, history, North)

    def test_invalid_input_out_of_range_suit_in_a_history_card(self) -> None:
        # A malformed card inside history itself (as opposed to a bad
        # declarer/opening_leader, above) is caught by list_to_history's
        # own conversion, before ExhaustiveLayoutSource's constructor ever
        # runs -- this must raise the same InvalidHistoryInputError a
        # verify_history-derived InvalidInput does, not a bare ValueError
        # with no relation to this type's own exception hierarchy.
        root, _ = make_void_ending()
        history = [Card(4, 2)]  # suit 4 is out of range (0..3)
        with self.assertRaises(InvalidHistoryInputError):
            ExhaustiveLayoutSource(root, North, 1, history, North)

    def test_invalid_input_out_of_range_rank_in_a_history_card(self) -> None:
        root, _ = make_void_ending()
        history = [Card(Spades, 15)]  # rank 15 is out of range (2..14)
        with self.assertRaises(InvalidHistoryInputError):
            ExhaustiveLayoutSource(root, North, 1, history, North)

    def test_duplicated_card(self) -> None:
        history = [Card(Spades, 14), Card(Spades, 14)]
        with self.assertRaises(DuplicatedCardError):
            ExhaustiveLayoutSource(empty_root(), North, 1, history, North)

    def test_card_played_and_held(self) -> None:
        root = empty_root()
        root["remain_cards"][North][Spades] = 1 << 14
        history = [Card(Spades, 14)]
        with self.assertRaises(CardPlayedAndHeldError):
            ExhaustiveLayoutSource(root, North, 1, history, North)

    def test_missing_card(self) -> None:
        history = [Card(Spades, 14)]
        with self.assertRaises(MissingCardError):
            ExhaustiveLayoutSource(empty_root(), North, 1, history, North)

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
        with self.assertRaises(TrickLengthMismatchError):
            ExhaustiveLayoutSource(root, North, 1, history, North)

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
        with self.assertRaises(TrailingTrickMismatchError):
            ExhaustiveLayoutSource(root, North, 1, history, North)

    def test_leader_mismatch(self) -> None:
        played = [(Diamonds, 14), (Clubs, 2), (Diamonds, 3), (Diamonds, 12)]
        root, history = build_deal(played, lambda _s, _r: North)
        with self.assertRaises(LeaderMismatchError):
            ExhaustiveLayoutSource(root, North, 1, history, South)

    def test_void_contradiction(self) -> None:
        def hand_for(suit: int, rank: int) -> int:
            if suit == Diamonds and rank == 5:
                return South
            return West

        played = [(Diamonds, 14), (Diamonds, 2), (Spades, 3), (Diamonds, 12)]
        root, history = build_deal(played, hand_for)
        with self.assertRaises(VoidContradictionError):
            ExhaustiveLayoutSource(root, North, 1, history, North)


class TestEveryConstrainedSpaceStatusCauseRaises(unittest.TestCase):
    def test_contradictory_void(self) -> None:
        # Both defenders void in diamonds, with a diamond still outstanding
        # in the pool -- verify_history accepts this (the 52-card partition,
        # the trailing trick and the free cross-check all pass), so this is
        # the constrained decomposition's own empty cause, not a rejected
        # history.
        def hand_for(suit: int, rank: int) -> int:
            if suit == Diamonds and rank == 11:
                return West
            return North

        played = [(Diamonds, 14), (Spades, 2), (Diamonds, 13), (Clubs, 3)]
        root, history = build_deal(played, hand_for)
        with self.assertRaises(ContradictoryVoidError):
            ExhaustiveLayoutSource(root, North, 1, history, North)

    def test_forced_exceeds_fixed_seat_count(self) -> None:
        # West (the other seat) is void in diamonds, forcing all three
        # outstanding diamonds onto East (the fixed seat) -- but East's own
        # root hand size is only one card. Mirrors
        # constrained_decomposition_test.cpp's own
        # MoreCardsForcedToTheFixedSeatThanItHoldsIsRejected, reached here
        # through a real root and history rather than decompose_constrained
        # called directly.
        def hand_for(suit: int, rank: int) -> int:
            if suit == Diamonds and rank in (5, 7, 9):
                return West
            if suit == Clubs and rank == 6:
                return East
            return North

        played = [(Diamonds, 14), (Diamonds, 13), (Diamonds, 4), (Spades, 2)]
        root, history = build_deal(played, hand_for)
        with self.assertRaises(ForcedExceedsFixedSeatCountError):
            ExhaustiveLayoutSource(root, North, 1, history, North)

    def test_insufficient_free_cards(self) -> None:
        # East (the fixed seat) is void in diamonds, forcing all four
        # outstanding diamonds onto West -- but East's own root hand size
        # (four, held in diamonds despite the void: root bookkeeping only,
        # not cross-checked against derived voids) leaves only one free
        # club for East to actually reach it from. Mirrors
        # constrained_decomposition_test.cpp's own
        # TheFixedSeatCannotReachItsHandSizeFromWhatIsLeftIsRejected.
        def hand_for(suit: int, rank: int) -> int:
            if suit == Diamonds and rank in (5, 7, 9, 11):
                return East
            if suit == Clubs and rank == 6:
                return West
            return North

        played = [(Diamonds, 14), (Spades, 2), (Diamonds, 13), (Diamonds, 12)]
        root, history = build_deal(played, hand_for)
        with self.assertRaises(InsufficientFreeCardsError):
            ExhaustiveLayoutSource(root, North, 1, history, North)


class TestRejectedHistoryIsDistinguishableFromNoLayoutSurvived(unittest.TestCase):
    def test_a_rejected_history_does_not_read_as_an_ordinary_empty_source(self) -> None:
        # A rejected history must raise a HistoryRejectedError, never the
        # RootFailure.NoLayoutSurvived an ordinary empty source produces --
        # they are different faults (fix the history vs. fix the source)
        # and the exception types must never be confused. NoLayoutSurvived
        # itself is a later task's to bind; this only pins that a rejected
        # history raises something distinct from it.
        history = [Card(Spades, 14), Card(Spades, 14)]
        with self.assertRaises(DuplicatedCardError) as ctx:
            ExhaustiveLayoutSource(empty_root(), North, 1, history, North)
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
        # Calling source.size()/.at(i) directly from Python would never
        # reach the C++ trampoline at all -- Python's own method
        # resolution finds this subclass's plain method first. The two
        # _layout_source_*_from_cpp probes stand in for the real consumer
        # (evaluate()/make_root(), a later task): they hold this source as
        # a be::LayoutSource const& and call through it, exactly as C++
        # code actually will.
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
        # Routes through the converters' own dict_to_deal, whose own
        # ValueError is the right answer here -- pinned so a later change
        # does not swallow it.
        source = ReturnsAMalformedDealFromAt()
        with self.assertRaises(ValueError):
            _layout_source_at_from_cpp(source, 0)


if __name__ == "__main__":
    unittest.main()
