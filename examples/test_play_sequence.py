"""Tests for the bridge plumbing in play_sequence.py and bridge_notation.py.

These matter more than example plumbing usually would. `play_sequence` is a
Python re-implementation of `src/belief_evaluation/trick.hpp` -- the library
does not expose those mechanics to Python, so a caller reaching a mid-play root
has to write them again. If this copy disagrees with the evaluator's own
follow-suit or trick-winner rule, the result is a wrong *root*, and every
number computed from it is confidently about a different position. Nothing
raises.

The trump branch of `_trick_winner` is the clearest case: the only example here
is 6NT, so no example exercises it at all.
"""

import unittest

import belief_space_local_evaluation as bsle

from bridge_notation import (
    CLUBS,
    EAST,
    HEARTS,
    NORTH,
    NOTRUMP,
    SOUTH,
    SPADES,
    WEST,
    holding,
    parse_card,
    parse_deal,
    ranks_in,
)
from play_sequence import (
    PlaySequence,
    cards_on_trick,
    legal_cards,
    play_card,
    seat_on_play,
    suit_led,
    trick_leader,
)

DEAL = "N: KJ7.QJ.QT65.AJ42 A653.86432.J2.T9 T9.AKT.AK98.KQ73 Q842.975.743.865"


def deal_with(trump, first, hands, trick=((0, 0, 0), (0, 0, 0))):
    remain_cards = [[0, 0, 0, 0] for _ in range(4)]
    for seat, per_suit in hands.items():
        for suit, ranks in per_suit.items():
            remain_cards[seat][suit] = holding(*ranks)
    return {
        "trump": trump,
        "first": first,
        "remain_cards": remain_cards,
        "current_trick_suit": trick[0],
        "current_trick_rank": trick[1],
    }


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
        self.assertEqual(parse_card("SQ"), bsle.Card(SPADES, 12))


class TestTrickPosition(unittest.TestCase):
    def test_an_empty_trick_reports_no_cards_and_the_leader_on_play(self) -> None:
        deal = deal_with(NOTRUMP, SOUTH, {s: {SPADES: [2 + s]} for s in range(4)})

        self.assertEqual(cards_on_trick(deal), [])
        self.assertEqual(trick_leader(deal), SOUTH)
        self.assertEqual(seat_on_play(deal), SOUTH)
        self.assertEqual(suit_led(deal), -1)

    def test_an_empty_trick_is_not_mistaken_for_a_spade_lead(self) -> None:
        # current_trick_suit is (0, 0, 0) with nothing played, and spades are
        # suit 0. The rank array is what distinguishes the two.
        deal = deal_with(NOTRUMP, SOUTH, {s: {SPADES: [2 + s]} for s in range(4)})

        self.assertEqual(deal["current_trick_suit"][0], SPADES)
        self.assertEqual(cards_on_trick(deal), [])
        self.assertNotEqual(suit_led(deal), SPADES)

    def test_seat_on_play_advances_past_the_cards_already_played(self) -> None:
        deal = deal_with(NOTRUMP, WEST,
                         {NORTH: {SPADES: [13]}, EAST: {SPADES: [6]},
                          SOUTH: {SPADES: [10]}, WEST: {SPADES: [12]}},
                         ((SPADES, 0, 0), (4, 11, 0)))

        self.assertEqual(trick_leader(deal), WEST)
        self.assertEqual(seat_on_play(deal), (WEST + 2) % 4)
        self.assertEqual([c.rank for c in cards_on_trick(deal)], [4, 11])


class TestLegalCards(unittest.TestCase):
    def test_a_seat_holding_the_led_suit_must_follow(self) -> None:
        deal = deal_with(NOTRUMP, SOUTH,
                         {NORTH: {SPADES: [13], HEARTS: [12]}, EAST: {SPADES: [6]},
                          SOUTH: {SPADES: [10]}, WEST: {SPADES: [12]}},
                         ((SPADES, 0, 0), (10, 0, 0)))

        legal = legal_cards(deal, WEST)

        self.assertEqual([c.suit for c in legal], [SPADES])

    def test_a_void_seat_may_play_anything(self) -> None:
        deal = deal_with(NOTRUMP, SOUTH,
                         {NORTH: {SPADES: [13]}, EAST: {HEARTS: [6], CLUBS: [3]},
                          SOUTH: {SPADES: [10]}, WEST: {SPADES: [12]}},
                         ((SPADES, 0, 0), (10, 0, 0)))

        legal = legal_cards(deal, EAST)

        self.assertEqual({c.suit for c in legal}, {HEARTS, CLUBS})

    def test_ranks_come_back_highest_first_within_a_suit(self) -> None:
        deal = deal_with(NOTRUMP, SOUTH, {SOUTH: {SPADES: [2, 10, 14]}})

        self.assertEqual([c.rank for c in legal_cards(deal, SOUTH)], [14, 10, 2])


class TestPlayCard(unittest.TestCase):
    def _four_singletons(self):
        return deal_with(NOTRUMP, SOUTH,
                         {NORTH: {SPADES: [13]}, EAST: {SPADES: [6]},
                          SOUTH: {SPADES: [10]}, WEST: {SPADES: [12]}})

    def test_it_does_not_modify_its_input(self) -> None:
        # Purity is claimed in play_card's docstring and relied on by callers
        # that keep the earlier position.
        deal = self._four_singletons()
        before = (deal["first"], deal["current_trick_suit"], deal["current_trick_rank"],
                  [list(row) for row in deal["remain_cards"]])

        play_card(deal, bsle.Card(SPADES, 10))

        self.assertEqual(deal["first"], before[0])
        self.assertEqual(deal["current_trick_suit"], before[1])
        self.assertEqual(deal["current_trick_rank"], before[2])
        self.assertEqual([list(row) for row in deal["remain_cards"]], before[3])

    def test_the_card_leaves_the_hand_and_joins_the_trick(self) -> None:
        after = play_card(self._four_singletons(), bsle.Card(SPADES, 10))

        self.assertEqual(after["remain_cards"][SOUTH][SPADES], 0)
        self.assertEqual([c.rank for c in cards_on_trick(after)], [10])

    def test_first_becomes_the_winner_when_the_trick_completes(self) -> None:
        deal = self._four_singletons()
        for card in (bsle.Card(SPADES, 10), bsle.Card(SPADES, 12),
                     bsle.Card(SPADES, 13), bsle.Card(SPADES, 6)):
            deal = play_card(deal, card)

        self.assertEqual(deal["first"], NORTH)  # the king
        self.assertEqual(cards_on_trick(deal), [])

    def test_a_card_not_held_is_rejected(self) -> None:
        with self.assertRaises(ValueError) as caught:
            play_card(self._four_singletons(), bsle.Card(SPADES, 3))
        self.assertIn("does not hold", str(caught.exception))

    def test_failing_to_follow_suit_is_rejected(self) -> None:
        deal = deal_with(NOTRUMP, SOUTH,
                         {NORTH: {SPADES: [13]}, EAST: {SPADES: [6]},
                          SOUTH: {SPADES: [10]}, WEST: {SPADES: [12], HEARTS: [9]}},
                         ((SPADES, 0, 0), (10, 0, 0)))

        with self.assertRaises(ValueError) as caught:
            play_card(deal, bsle.Card(HEARTS, 9))
        self.assertIn("follow suit", str(caught.exception))


class TestTrumps(unittest.TestCase):
    """The branch no example reaches: the only example here is 6NT."""

    def test_a_ruff_beats_the_highest_card_of_the_led_suit(self) -> None:
        # South leads the spade ace; West is void and ruffs with the heart two.
        deal = deal_with(HEARTS, SOUTH,
                         {NORTH: {SPADES: [3]}, EAST: {SPADES: [4]},
                          SOUTH: {SPADES: [14]}, WEST: {HEARTS: [2]}})
        for card in (bsle.Card(SPADES, 14), bsle.Card(HEARTS, 2),
                     bsle.Card(SPADES, 3), bsle.Card(SPADES, 4)):
            deal = play_card(deal, card)

        self.assertEqual(deal["first"], WEST)

    def test_the_higher_trump_wins_when_two_are_played(self) -> None:
        deal = deal_with(HEARTS, SOUTH,
                         {NORTH: {HEARTS: [5]}, EAST: {SPADES: [4]},
                          SOUTH: {SPADES: [14]}, WEST: {HEARTS: [2]}})
        for card in (bsle.Card(SPADES, 14), bsle.Card(HEARTS, 2),
                     bsle.Card(HEARTS, 5), bsle.Card(SPADES, 4)):
            deal = play_card(deal, card)

        self.assertEqual(deal["first"], NORTH)  # the five of trumps

    def test_a_trump_in_hand_does_not_win_when_the_suit_is_followed(self) -> None:
        # Holding a trump is not playing one: everyone follows spades here, so
        # the spade ace wins even though trumps exist in the deal.
        deal = deal_with(HEARTS, SOUTH,
                         {NORTH: {SPADES: [3], HEARTS: [5]}, EAST: {SPADES: [4]},
                          SOUTH: {SPADES: [14]}, WEST: {SPADES: [2]}})
        for card in (bsle.Card(SPADES, 14), bsle.Card(SPADES, 2),
                     bsle.Card(SPADES, 3), bsle.Card(SPADES, 4)):
            deal = play_card(deal, card)

        self.assertEqual(deal["first"], SOUTH)


class TestPlaySequence(unittest.TestCase):
    def _sequence(self):
        return PlaySequence(parse_deal(DEAL), declarer=SOUTH, trump=NOTRUMP, level=6)

    def test_the_opening_leader_is_declarer_s_left_hand_opponent(self) -> None:
        sequence = self._sequence()

        self.assertEqual(sequence.opening_leader, WEST)
        self.assertEqual(sequence.current_deal["first"], WEST)
        self.assertEqual(sequence.dummy, NORTH)

    def test_tricks_needed_counts_down_as_declarer_wins(self) -> None:
        sequence = self._sequence()
        self.assertEqual(sequence.tricks_to_make, 12)
        self.assertEqual(sequence.tricks_needed, 12)

        sequence.play_trick("C6 C4 C9 CQ")  # South wins.

        self.assertEqual(sequence.tricks_won_by_declarer, 1)
        self.assertEqual(sequence.tricks_needed, 11)

    def test_a_trick_won_by_the_defence_does_not_count(self) -> None:
        sequence = self._sequence()

        sequence.play_trick("C6 C2 CT CQ")  # South's queen... wins.
        self.assertEqual(sequence.tricks_won_by_declarer, 1)

        sequence.play_trick("CK C8 C4 C9")  # South leads the king; South wins.
        self.assertEqual(sequence.tricks_won_by_declarer, 2)

    def test_the_history_records_every_card_in_play_order(self) -> None:
        sequence = self._sequence()

        sequence.play_trick("C6 C4 C9 CQ")

        self.assertEqual([c.rank for c in sequence.history], [6, 4, 9, 12])

    def test_a_short_trick_is_rejected(self) -> None:
        with self.assertRaises(ValueError) as caught:
            self._sequence().play_trick("C6 C4 C9")
        self.assertIn("4 cards", str(caught.exception))

    def test_play_trick_refuses_to_start_mid_trick(self) -> None:
        sequence = self._sequence()
        sequence.play("C6")

        with self.assertRaises(ValueError) as caught:
            sequence.play_trick("C4 C9 CQ CK")
        self.assertIn("already in progress", str(caught.exception))


class TestAgainstTheLibrary(unittest.TestCase):
    """The reason this file exists: agreement with the evaluator's own rules.

    `evaluate()` rejects a card its own `legal_cards` considers illegal, so
    feeding it this module's choices at every node is a direct cross-check of
    the two follow-suit rules. A disagreement shows up as a ValidationError
    rather than as a plausible number.
    """

    def test_every_card_this_module_calls_legal_is_accepted_by_evaluate(self) -> None:
        import guess_6nt_belief_space as example
        from strategies import lowest_eligible_defender

        sequence = example.guess_6nt()
        source = bsle.ExhaustiveLayoutSource(
            sequence.current_deal, sequence.declarer, example.SEED,
            history=sequence.history, opening_leader=sequence.opening_leader)

        def pi(state, view):
            del view
            deal = state.known_holdings
            # Deliberately the *highest* legal card, so the choice exercises
            # a different part of the legal set than the example's own pi.
            return max(legal_cards(deal, seat_on_play(deal)),
                       key=lambda c: (c.rank, -c.suit))

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            source, pi, lowest_eligible_defender)

        self.assertNotIn("error", result)


if __name__ == "__main__":
    unittest.main()
