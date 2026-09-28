"""Tests for `PlayRecord` and its Python-visible reach: `state.play_record`,
`evaluate(..., play_record=...)`, and `ExhaustiveLayoutSource(..., record=...)`
-- the sole way either entry point accepts a play history.
"""

import unittest

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import DuplicatedCardError
from belief_space_local_evaluation import evaluate
from belief_space_local_evaluation import ExhaustiveLayoutSource
from belief_space_local_evaluation import HistoryVerdict
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
        # The same exception type ExhaustiveLayoutSource's own record=
        # argument raises when its own root-consistency check catches a
        # malformed pair that reaches it some other way.
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
    def test_a_supplied_record_is_consistent_and_reaches_the_source(self) -> None:
        # ExhaustiveLayoutSource requires history and root together to
        # account for all 52 cards once a non-empty record is supplied --
        # and, mechanically, a real sequence of complete tricks always
        # leaves all four hands at the same remaining count (each trick
        # removes exactly one card from each hand), so a genuinely small
        # defender pool needs many tricks played, not one. One trick played
        # here instead (giving every hand 12 cards, a real but large
        # C(24, 12) space) -- all four played cards are clubs, so no
        # defender is void in anything, and the record does not narrow the
        # space at all: the point of this test is that a record reaches
        # ExhaustiveLayoutSource and is accepted as Consistent, not that it
        # shrinks anything here. Genuine narrowing is
        # TestTheHistoryConstrainsTheBeliefSpace's own, in
        # test_belief_space_local_evaluation_play_sequence.py.
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

        without_record = ExhaustiveLayoutSource(root, North, 1)
        record = PlayRecord(history, West)
        with_record = ExhaustiveLayoutSource(root, North, 1, record=record)

        self.assertEqual(with_record.history_verdict(), HistoryVerdict.Consistent)
        self.assertEqual(with_record.size(), without_record.size())
        self.assertEqual(with_record.size(), 2704156)  # C(24, 12)


if __name__ == "__main__":
    unittest.main()
