"""Tests for Card, the Deal and history converters, and the ObservationState
type."""

import unittest

from belief_space_local_evaluation import Card
from belief_space_local_evaluation import ObservationState
from belief_space_local_evaluation._belief_space_local_evaluation import (
    _deal_round_trip,
)
from belief_space_local_evaluation._belief_space_local_evaluation import (
    _history_round_trip,
)


def make_deal() -> dict:
    return {
        "trump": 0,
        "first": 0,
        "remain_cards": [
            [0x4000, 0x4000, 0x4000, 0x4000],
            [0x4000, 0x4000, 0x4000, 0x4000],
            [0x4000, 0x4000, 0x4000, 0x4000],
            [0x4000, 0x4000, 0x4000, 0x4000],
        ],
        "current_trick_suit": (0, 0, 0),
        "current_trick_rank": (0, 0, 0),
    }


class TestCard(unittest.TestCase):
    def test_construct_and_read(self) -> None:
        card = Card(suit=2, rank=12)
        self.assertEqual(card.suit, 2)
        self.assertEqual(card.rank, 12)

    def test_write(self) -> None:
        card = Card(0, 2)
        card.suit = 3
        card.rank = 14
        self.assertEqual(card, Card(3, 14))

    def test_equality(self) -> None:
        self.assertEqual(Card(1, 10), Card(1, 10))
        self.assertNotEqual(Card(1, 10), Card(1, 11))
        self.assertNotEqual(Card(1, 10), Card(2, 10))

    def test_repr(self) -> None:
        self.assertEqual(repr(Card(1, 10)), "Card(suit=1, rank=10)")

    def test_is_hashable(self) -> None:
        self.assertIsInstance(hash(Card(0, 14)), int)

    def test_equal_cards_hash_equal(self) -> None:
        # A hand-written __hash__ often hashes identity instead of the fields
        # __eq__ compares.
        self.assertEqual(hash(Card(0, 14)), hash(Card(0, 14)))

    def test_works_as_a_dict_key_and_a_set_member(self) -> None:
        # Needed to memoise by card, deduplicate a delta distribution or key
        # a transposition table without converting to tuples.
        self.assertEqual({Card(0, 14): "ace"}[Card(0, 14)], "ace")
        self.assertEqual(len({Card(0, 14), Card(0, 14)}), 1)
        self.assertEqual(len({Card(0, 14), Card(0, 13)}), 2)

    def test_a_mutated_card_hashes_as_its_new_value(self) -> None:
        # Card is mutable and the hash follows the fields. Mutating a Card
        # that is already a dict key loses the entry.
        card = Card(0, 2)
        card.rank = 14
        self.assertEqual(hash(card), hash(Card(0, 14)))


class TestDealRoundTrip(unittest.TestCase):
    def test_round_trips_every_field(self) -> None:
        deal = make_deal()
        result = _deal_round_trip(deal)
        self.assertEqual(result["trump"], deal["trump"])
        self.assertEqual(result["first"], deal["first"])
        self.assertEqual(tuple(result["current_trick_suit"]), deal["current_trick_suit"])
        self.assertEqual(tuple(result["current_trick_rank"]), deal["current_trick_rank"])
        self.assertEqual(result["remain_cards"], deal["remain_cards"])

    def test_round_trips_a_trick_in_progress(self) -> None:
        deal = make_deal()
        deal["current_trick_suit"] = (0, 1, 0)
        deal["current_trick_rank"] = (14, 2, 0)
        result = _deal_round_trip(deal)
        self.assertEqual(tuple(result["current_trick_suit"]), (0, 1, 0))
        self.assertEqual(tuple(result["current_trick_rank"]), (14, 2, 0))

    def test_rejects_out_of_range_trump(self) -> None:
        deal = make_deal()
        deal["trump"] = 5
        with self.assertRaises(ValueError) as ctx:
            _deal_round_trip(deal)
        self.assertIn("trump", str(ctx.exception))

    def test_rejects_out_of_range_first(self) -> None:
        deal = make_deal()
        deal["first"] = 4
        with self.assertRaises(ValueError) as ctx:
            _deal_round_trip(deal)
        self.assertIn("first", str(ctx.exception))

    def test_rejects_malformed_remain_cards(self) -> None:
        deal = make_deal()
        deal["remain_cards"][0][0] = -1
        with self.assertRaises(ValueError) as ctx:
            _deal_round_trip(deal)
        self.assertIn("remain_cards", str(ctx.exception))


class TestHistoryRoundTrip(unittest.TestCase):
    def test_round_trips_an_empty_history(self) -> None:
        self.assertEqual(_history_round_trip([]), [])

    def test_round_trips_a_card_sequence_in_order(self) -> None:
        history = [Card(0, 14), Card(1, 2), Card(2, 9), Card(3, 7)]
        result = _history_round_trip(history)
        self.assertEqual(result, history)

    def test_rejects_a_sequence_longer_than_the_deck(self) -> None:
        history = [Card(0, 14)] * 53
        with self.assertRaises(ValueError) as ctx:
            _history_round_trip(history)
        self.assertIn("52", str(ctx.exception))

    def test_accepts_a_sequence_of_exactly_the_deck_size(self) -> None:
        history = [Card(0, 14)] * 52
        result = _history_round_trip(history)
        self.assertEqual(len(result), 52)

    def test_rejects_an_out_of_range_suit(self) -> None:
        history = [Card(0, 14), Card(4, 2)]
        with self.assertRaises(ValueError) as ctx:
            _history_round_trip(history)
        self.assertIn("history[1].suit", str(ctx.exception))

    def test_rejects_a_negative_suit(self) -> None:
        history = [Card(-1, 2)]
        with self.assertRaises(ValueError) as ctx:
            _history_round_trip(history)
        self.assertIn("history[0].suit", str(ctx.exception))

    def test_rejects_an_out_of_range_rank(self) -> None:
        history = [Card(0, 15)]
        with self.assertRaises(ValueError) as ctx:
            _history_round_trip(history)
        self.assertIn("history[0].rank", str(ctx.exception))

    def test_rejects_a_rank_below_two(self) -> None:
        history = [Card(0, 1)]
        with self.assertRaises(ValueError) as ctx:
            _history_round_trip(history)
        self.assertIn("history[0].rank", str(ctx.exception))


class TestObservationStateExposesTheDerivedProperties(unittest.TestCase):
    # test_belief_space_local_evaluation_strategies.py checks the values
    # during play. Python cannot construct an ObservationState, and at a
    # root two of these equal `first` and 0. This only checks that each
    # property exists on the type, which catches one missing from the
    # package re-export.
    DERIVED = (
        "seat_on_play",
        "trick_leader",
        "current_trick",
        "position_in_trick",
        "legal_cards",
        "can_follow_led_suit",
        "is_declaring_side",
    )

    def test_every_derived_property_is_present(self) -> None:
        for name in self.DERIVED:
            with self.subTest(name=name):
                self.assertTrue(hasattr(ObservationState, name))

    def test_each_one_is_read_only(self) -> None:
        # A strategy reads its state and does not edit it.
        for name in self.DERIVED:
            with self.subTest(name=name):
                self.assertIsNone(getattr(ObservationState, name).fset)


class TestObservationStateIsReadOnly(unittest.TestCase):
    def test_not_constructible_from_python(self) -> None:
        # No __init__ is bound, so pybind11 raises its default constructor
        # error.
        with self.assertRaises(TypeError):
            ObservationState()


if __name__ == "__main__":
    unittest.main()
