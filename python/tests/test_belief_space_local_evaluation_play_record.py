"""Tests for `PlayRecord` and its Python-visible reach: `state.play_record`,
`evaluate(..., play_record=...)`, and `ExhaustiveLayoutSource(..., record=...)`
as an alternative to the existing `history=`/`opening_leader=` pair.
"""

import unittest

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import DuplicatedCardError
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import InvalidHistoryInputError
from belief_space_local_evaluation import PlayRecord

Spades, Hearts, Diamonds, Clubs = 0, 1, 2, 3
North, East, South, West = 0, 1, 2, 3
DDS_NOTRUMP = 4


def holding(*ranks: int) -> int:
    mask = 0
    for rank in ranks:
        mask |= 1 << rank
    return mask


def make_one_card_finesse_root() -> dict:
    # North (declarer) holds a single middling card (Nine); South (dummy)
    # holds a single safely-lower card (Eight); East and West share a
    # seven-card pool {Two..Seven, Ten}. Mirrors
    # test_belief_space_local_evaluation_strategies.py's own fixture of the
    # same name.
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    remain_cards[North][Spades] = holding(9)
    remain_cards[South][Spades] = holding(8)
    remain_cards[East][Spades] = holding(2)
    remain_cards[West][Spades] = holding(3, 4, 5, 6, 7, 10)
    return {
        "trump": DDS_NOTRUMP,
        "first": East,
        "remain_cards": remain_cards,
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


def lowest_card_in(remain_cards_row) -> Card:
    for suit in range(4):
        mask = remain_cards_row[suit]
        if mask:
            rank = 2
            while not (mask & (1 << rank)):
                rank += 1
            return Card(suit, rank)
    raise AssertionError("seat holds nothing")


def defender_play(layout, seat, state):
    del state
    return [(lowest_card_in(layout["remain_cards"][seat]), 1.0)]


class TestPlayRecordConstruction(unittest.TestCase):
    def test_cards_and_opening_leader_round_trip(self) -> None:
        cards = [Card(Diamonds, 2)]
        record = PlayRecord(cards, West)

        self.assertEqual(record.cards, cards)
        self.assertEqual(record.opening_leader, West)

    def test_an_empty_record_is_accepted(self) -> None:
        record = PlayRecord([], North)

        self.assertEqual(record.cards, [])
        self.assertEqual(record.opening_leader, North)

    def test_an_out_of_range_opening_leader_is_rejected(self) -> None:
        # The same exception type ExhaustiveLayoutSource's own history=
        # already raises for the identical malformation.
        with self.assertRaises(InvalidHistoryInputError):
            PlayRecord([], 99)

    def test_a_duplicated_card_is_rejected(self) -> None:
        with self.assertRaises(DuplicatedCardError):
            PlayRecord([Card(Diamonds, 2), Card(Diamonds, 2)], North)


class TestEvaluatePlayRecord(unittest.TestCase):
    def test_play_record_reaches_the_state_pi_is_given(self) -> None:
        captured = {}

        def pi(state, view):
            del view
            captured["play_record"] = state.play_record
            seat = (state.first + len(state.history)) % 4
            return lowest_card_in(state.known_holdings["remain_cards"][seat])

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)
        record = PlayRecord([Card(Diamonds, 2)], West)

        result = evaluate(root, North, 1, source, pi, defender_play, play_record=record)

        self.assertNotIn("error", result)
        self.assertIsNotNone(captured["play_record"])
        self.assertEqual(captured["play_record"].cards, record.cards)

    def test_play_record_is_none_when_not_supplied(self) -> None:
        captured = {}

        def pi(state, view):
            del view
            captured["play_record"] = state.play_record
            seat = (state.first + len(state.history)) % 4
            return lowest_card_in(state.known_holdings["remain_cards"][seat])

        root = make_one_card_finesse_root()
        source = ExhaustiveLayoutSource(root, North, 5)

        result = evaluate(root, North, 1, source, pi, defender_play)

        self.assertNotIn("error", result)
        self.assertIsNone(captured["play_record"])


class TestExhaustiveLayoutSourceRecordArgument(unittest.TestCase):
    def test_a_record_gives_the_identical_space_to_the_equivalent_pair(self) -> None:
        # ExhaustiveLayoutSource requires history and root together to
        # account for all 52 cards once a non-empty history is supplied --
        # and, mechanically, a real sequence of complete tricks always
        # leaves all four hands at the same remaining count (each trick
        # removes exactly one card from each hand), so a genuinely small
        # defender pool needs many tricks played, not one. One trick played
        # here instead (giving every hand 12 cards, a real but large
        # C(24, 12) space) -- the point of this test is that the two
        # constructor forms agree, not that the space itself is small, so
        # only size() and a few at() samples are checked, not every index.
        history = [Card(Clubs, 2), Card(Clubs, 3), Card(Clubs, 4), Card(Clubs, 5)]  # W, N, E, S
        played = {(c.suit, c.rank) for c in history}
        remaining = [
            Card(suit, rank)
            for suit in range(4)
            for rank in range(2, 15)
            if (suit, rank) not in played
        ]
        remain_cards = [[0, 0, 0, 0] for _ in range(4)]
        for seat, cards in enumerate((remaining[0:12], remaining[12:24], remaining[24:36], remaining[36:48])):
            for card in cards:
                remain_cards[seat][card.suit] |= 1 << card.rank
        root = {
            "trump": DDS_NOTRUMP,
            "first": South,  # South's club five won the one trick played
            "remain_cards": remain_cards,
            "current_trick_suit": (0, 0, 0),
            "current_trick_rank": (0, 0, 0),
        }

        from_pair = ExhaustiveLayoutSource(root, North, 1, history=history, opening_leader=West)
        record = PlayRecord(history, West)
        from_record = ExhaustiveLayoutSource(root, North, 1, record=record)

        self.assertEqual(from_pair.size(), from_record.size())
        size = from_pair.size()
        for index in (0, 1, size // 2, size - 1):
            self.assertEqual(from_pair.at(index), from_record.at(index))

    def test_three_positional_arguments_still_construct_the_legacy_overload(self) -> None:
        # The overload-ambiguity check: record has no default, so this
        # must keep resolving to the (history, opening_leader) overload.
        root = make_one_card_finesse_root()

        source = ExhaustiveLayoutSource(root, North, 5)

        self.assertIsNotNone(source.size())


if __name__ == "__main__":
    unittest.main()
