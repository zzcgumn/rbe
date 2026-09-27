"""Tests for `bridge_notation`, promoted from `examples/` into its own
installed package.

`TestParseDeal` moved here verbatim from `examples/test_play_sequence.py` --
it always tested this module, not `play_sequence.py`, and belongs with what
it tests.

`TestFormatting` is new. `rank_letter`, `format_card`, `format_hand` and
`format_denomination` were used throughout `examples/` -- always as an
argument to `print(...)`, never asserted on -- so promoting this module to
shipped API is the first point any of them has had a direct test.
"""

import unittest

import bridge_notation
from bridge_notation import (
    CLUBS,
    DIAMONDS,
    HEARTS,
    NORTH,
    NOTRUMP,
    SPADES,
    format_card,
    format_denomination,
    format_hand,
    holding,
    parse_card,
    parse_deal,
    rank_letter,
    ranks_in,
)

from belief_space_local_evaluation import Card

DEAL = "N: KJ7.QJ.QT65.AJ42 A653.86432.J2.T9 T9.AKT.AK98.KQ73 Q842.975.743.865"


class TestParseDeal(unittest.TestCase):
    def test_all_52_cards_round_trip(self) -> None:
        remain_cards = parse_deal(DEAL)["remain_cards"]

        seen = {(suit, rank)
                for seat in range(4) for suit in range(4)
                for rank in ranks_in(remain_cards[seat][suit])}
        self.assertEqual(len(seen), 52)
        for seat in range(4):
            self.assertEqual(
                sum(len(ranks_in(remain_cards[seat][suit])) for suit in range(4)), 13)

    def test_a_duplicated_card_is_named(self) -> None:
        # Two spade kings: North's KJ7 and East's K653.
        with self.assertRaises(ValueError) as caught:
            parse_deal("N: KJ7.QJ.QT65.AJ42 K653.86432.J2.T9 T9.AKT.AK98.KQ73 Q842.975.743.865")
        self.assertIn("SK", str(caught.exception))

    def test_a_missing_card_is_named(self) -> None:
        # East's spade ace dropped.
        with self.assertRaises(ValueError) as caught:
            parse_deal("N: KJ7.QJ.QT65.AJ42 653.86432.J2.T9 T9.AKT.AK98.KQ73 Q842.975.743.865")
        self.assertIn("incomplete", str(caught.exception))
        self.assertIn("SA", str(caught.exception))

    def test_a_deal_without_a_leading_seat_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            parse_deal("KJ7.QJ.QT65.AJ42 A653.86432.J2.T9 T9.AKT.AK98.KQ73 Q842.975.743.865")

    def test_rank_first_and_suit_first_card_notation_agree(self) -> None:
        self.assertEqual(parse_card("SQ"), parse_card("QS"))
        self.assertEqual(parse_card("SQ"), Card(SPADES, 12))


class TestFormatting(unittest.TestCase):
    """Never directly asserted on before this promotion -- see module
    docstring. Perturbed once each and confirmed to fail before being left as
    written; see the commit message."""

    def test_rank_letter_names_every_rank(self) -> None:
        self.assertEqual(rank_letter(14), "A")
        self.assertEqual(rank_letter(10), "T")
        self.assertEqual(rank_letter(2), "2")

    def test_rank_letter_rejects_out_of_range(self) -> None:
        with self.assertRaises(ValueError):
            rank_letter(15)
        with self.assertRaises(ValueError):
            rank_letter(1)

    def test_format_card_with_symbols(self) -> None:
        self.assertEqual(format_card(Card(SPADES, 12)), "♠Q")
        self.assertEqual(format_card(Card(HEARTS, 14)), "♥A")

    def test_format_card_without_symbols(self) -> None:
        self.assertEqual(format_card(Card(SPADES, 12), symbols=False), "SQ")
        self.assertEqual(format_card(Card(CLUBS, 10), symbols=False), "CT")

    def test_format_hand_lists_each_suit_highest_first(self) -> None:
        row = [holding(13, 11, 7), holding(12), 0, 0]
        self.assertEqual(format_hand(row, symbols=False), "S KJ7  H Q  D -  C -")

    def test_format_hand_marks_a_void_with_a_dash(self) -> None:
        row = [0, 0, holding(2), 0]
        self.assertEqual(format_hand(row, symbols=False), "S -  H -  D 2  C -")

    def test_format_denomination_notrump(self) -> None:
        self.assertEqual(format_denomination(NOTRUMP), "NT")
        self.assertEqual(format_denomination(NOTRUMP, symbols=False), "NT")

    def test_format_denomination_a_suit(self) -> None:
        self.assertEqual(format_denomination(SPADES), "♠")
        self.assertEqual(format_denomination(SPADES, symbols=False), "S")
        self.assertEqual(format_denomination(DIAMONDS, symbols=False), "D")


class TestPromotedAsATopLevelPackage(unittest.TestCase):
    """The property this whole promotion is for: importable on its own,
    with no dependency on belief_space_local_evaluation."""

    def test_the_module_itself_is_importable(self) -> None:
        self.assertTrue(hasattr(bridge_notation, "parse_deal"))

    def test_seat_constant_is_present(self) -> None:
        self.assertEqual(NORTH, 0)


if __name__ == "__main__":
    unittest.main()
