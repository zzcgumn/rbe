"""Tests for `belief_space_local_evaluation.play_sequence`, promoted from
`examples/play_sequence.py`.

Moved verbatim from `examples/test_play_sequence.py`, imports updated to the
installed-package path. Every assertion here is unchanged in what it
asserts -- this is a location change, not new coverage.

`play_sequence` used to re-implement `src/belief_evaluation/trick.hpp`,
because those mechanics were not bound to Python. They are bound now, so the
cases here that exercise `legal_cards` and `seat_on_play` are testing the
library through its own bindings, and the hand-derived assertions on them
live in `python/tests/test_belief_space_local_evaluation_trick.py`.

What is left to test here is what this module still owns, and it is the part
worth the most care: the trick *counting*. `evaluate()` is handed
`tricks_needed` and takes it on trust, so an off-by-one in
`tricks_won_by_declarer` silently evaluates a different contract -- which is
why a defence-won trick is asserted explicitly below, that branch having once
had no test at all.
"""

import dataclasses
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
    parse_deal,
)
from belief_space_local_evaluation import legal_cards, seat_on_play

from belief_space_local_evaluation.play_sequence import (
    DeclarerView,
    PlaySequence,
    cards_on_trick,
    play_card,
    suit_led,
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


class TestTrickPosition(unittest.TestCase):
    def test_an_empty_trick_reports_no_cards_and_the_leader_on_play(self) -> None:
        deal = deal_with(NOTRUMP, SOUTH, {s: {SPADES: [2 + s]} for s in range(4)})

        self.assertEqual(cards_on_trick(deal), [])
        self.assertEqual(deal["first"], SOUTH)  # the trick in progress
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

        self.assertEqual(deal["first"], WEST)  # the trick in progress
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

    def test_ranks_come_back_lowest_first_within_a_suit(self) -> None:
        # This module used to return them highest first. The library's own
        # order is ascending -- and it is contract, not incidental, because
        # root_children is built by indexing into this list. Nothing depended
        # on the old order: every strategy here reaches for min() or searches
        # by rank, which is why the switch moved no P_make.
        deal = deal_with(NOTRUMP, SOUTH, {SOUTH: {SPADES: [2, 10, 14]}})

        self.assertEqual([c.rank for c in legal_cards(deal, SOUTH)], [2, 10, 14])


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
        # The `else` of `if winner in (declarer, dummy)` -- the branch that
        # decides tricks_won_by_declarer, hence tricks_needed, hence what
        # "make" means to evaluate(). Nothing else in this package takes it:
        # all nine tricks of guess_6nt() go to declarer's side.
        sequence = self._sequence()

        # West leads a low club, North plays low, East's ten wins.
        sequence.play_trick("C5 C2 CT C3")

        self.assertEqual(sequence.current_deal["first"], EAST)
        self.assertEqual(sequence.tricks_won_by_declarer, 0)
        self.assertEqual(sequence.tricks_needed, 12)  # unchanged
        self.assertEqual(sequence.completed_tricks[-1][2], EAST)

    def test_the_counter_advances_only_on_tricks_the_declaring_side_wins(self) -> None:
        sequence = self._sequence()

        sequence.play_trick("C5 C2 CT C3")  # East wins.
        self.assertEqual(sequence.tricks_won_by_declarer, 0)

        # East leads, so the order is E, S, W, N -- South's king wins.
        sequence.play_trick("C9 CK C6 C4")
        self.assertEqual(sequence.tricks_won_by_declarer, 1)
        self.assertEqual(sequence.tricks_needed, 11)

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


class TestTheHistoryConstrainsTheBeliefSpace(unittest.TestCase):
    """That passing `history=` actually changes the answer.

    The guide gives a whole section to this ("omitting it does not fail, it
    quietly answers a different question"), and `PlaySequence` exists largely
    to collect the history -- but on the Guess 6NT root the constrained and
    unconstrained spaces are both 70, because every suit either defender
    showed out of is already exhausted there. So nothing in that example
    demonstrates the history doing anything, and a regression that silently
    dropped it would pass every other test in this package.

    This fixture is built for the purpose: East holds no spades at all, so a
    spade lead makes East discard while four spades are still outstanding in
    West's hand.
    """

    # North/East/South/West. East is void in spades; every suit is otherwise
    # distributed so that one trick establishes exactly one void.
    DEAL_EAST_VOID_IN_SPADES = (
        "N: AKQJ.76.AKQ.AKQJ"
        " .AKQJT98.765.432"
        " T987.54.JT9.T987"
        " 65432.32.8432.65")

    def _after_one_spade_trick(self):
        sequence = PlaySequence(
            parse_deal(self.DEAL_EAST_VOID_IN_SPADES),
            declarer=SOUTH, trump=NOTRUMP, level=3)
        # West leads, so the order is W, N, E, S: East's heart is the discard.
        sequence.play_trick("S2 SJ H8 S7")
        return sequence

    def test_the_show_out_removes_most_of_the_space(self) -> None:
        sequence = self._after_one_spade_trick()

        without = bsle.ExhaustiveLayoutSource(
            sequence.current_deal, sequence.declarer, 1)
        with_history = bsle.ExhaustiveLayoutSource(
            sequence.current_deal, sequence.declarer, 1,
            history=sequence.history, opening_leader=sequence.opening_leader)

        # C(24, 12): twelve of the defenders' twenty-four outstanding cards.
        self.assertEqual(without.size(), 2704156)
        # C(20, 12): East cannot hold any of the four outstanding spades, so
        # East's twelve come from the twenty non-spade cards.
        self.assertEqual(with_history.size(), 125970)
        self.assertLess(with_history.size(), without.size())

    def test_the_history_is_consistent(self) -> None:
        sequence = self._after_one_spade_trick()

        source = bsle.ExhaustiveLayoutSource(
            sequence.current_deal, sequence.declarer, 1,
            history=sequence.history, opening_leader=sequence.opening_leader)

        self.assertEqual(source.history_verdict(), bsle.HistoryVerdict.Consistent)


class TestDeclarerView(unittest.TestCase):
    def test_it_refuses_when_a_defender_is_on_play(self) -> None:
        sequence = PlaySequence(parse_deal(DEAL), declarer=SOUTH, trump=NOTRUMP, level=6)

        with self.assertRaises(ValueError) as caught:
            sequence.declarer_view()

        self.assertIn("West", str(caught.exception))
        self.assertIn("declarer's or dummy's turn", str(caught.exception))

    def test_it_reports_a_lead_position_correctly(self) -> None:
        sequence = PlaySequence(parse_deal(DEAL), declarer=SOUTH, trump=NOTRUMP, level=6)
        sequence.play_trick("C6 C4 C9 CQ")  # South wins with the queen

        view = sequence.declarer_view()

        self.assertIsInstance(view, DeclarerView)
        self.assertEqual(view.seat_on_play, SOUTH)
        self.assertEqual(view.position_in_trick, 0)
        self.assertEqual(view.current_trick, [])
        self.assertEqual(view.trick_leader, SOUTH)
        self.assertFalse(view.can_follow_led_suit)
        self.assertEqual(len(view.declarer_hand), 12)
        self.assertNotIn(bsle.Card(CLUBS, 12), view.declarer_hand)
        self.assertIn(bsle.Card(CLUBS, 13), view.declarer_hand)
        self.assertEqual(set(view.legal_cards), set(view.declarer_hand))
        self.assertEqual(
            view.play_history,
            [bsle.Card(CLUBS, 6), bsle.Card(CLUBS, 4), bsle.Card(CLUBS, 9), bsle.Card(CLUBS, 12)])
        self.assertEqual(view.tricks_won_by_declarer, 1)
        self.assertEqual(view.tricks_needed, 11)

    def test_it_reports_a_mid_trick_dummy_position_correctly(self) -> None:
        deal = deal_with(NOTRUMP, WEST, {
            WEST: {SPADES: [6, 7], HEARTS: [2]},
            NORTH: {SPADES: [8, 9], HEARTS: [3]},
            EAST: {SPADES: [3, 4], HEARTS: [4]},
            SOUTH: {SPADES: [14, 5], HEARTS: [5]},
        })
        sequence = PlaySequence(deal, declarer=SOUTH, trump=NOTRUMP, level=1)
        sequence.play_trick("S6 S9 S4 SA")  # W, N, E, S -- South's ace wins
        sequence.play("S5")                 # South leads trick 2
        sequence.play("S7")                 # West follows

        view = sequence.declarer_view()

        self.assertEqual(view.seat_on_play, NORTH)
        self.assertEqual(view.trick_leader, SOUTH)
        self.assertEqual(view.current_trick, [bsle.Card(SPADES, 5), bsle.Card(SPADES, 7)])
        self.assertEqual(view.position_in_trick, 2)
        self.assertEqual(view.legal_cards, [bsle.Card(SPADES, 8)])
        self.assertTrue(view.can_follow_led_suit)
        self.assertTrue(view.is_declaring_side)
        self.assertEqual(view.declarer_hand, [bsle.Card(HEARTS, 5)])
        self.assertEqual(view.dummy_hand, [bsle.Card(SPADES, 8), bsle.Card(HEARTS, 3)])
        self.assertEqual(
            view.play_history,
            [bsle.Card(SPADES, 6), bsle.Card(SPADES, 9), bsle.Card(SPADES, 4),
             bsle.Card(SPADES, 14), bsle.Card(SPADES, 5), bsle.Card(SPADES, 7)])
        self.assertEqual(view.tricks_won_by_declarer, 1)
        self.assertEqual(view.tricks_needed, 6)

    def test_it_does_not_carry_a_reference_to_the_real_deal(self) -> None:
        # Attribute-absence, not e.g. a check on dataclasses.fields()'s
        # length: a fixed-length check breaks the moment a legitimate new
        # field is added, which is not the failure this test is for. What
        # matters is that the specific escape hatch -- the real deal, with
        # every hand including the defenders' -- is unreachable from the
        # object handed back, however many fields it ends up with.
        sequence = PlaySequence(parse_deal(DEAL), declarer=SOUTH, trump=NOTRUMP, level=6)
        sequence.play_trick("C6 C4 C9 CQ")

        view = sequence.declarer_view()

        self.assertFalse(hasattr(view, "current_deal"))

    def test_it_is_frozen(self) -> None:
        sequence = PlaySequence(parse_deal(DEAL), declarer=SOUTH, trump=NOTRUMP, level=6)
        sequence.play_trick("C6 C4 C9 CQ")

        view = sequence.declarer_view()

        with self.assertRaises(dataclasses.FrozenInstanceError):
            view.declarer_hand = []

    def test_play_history_is_the_full_record_while_observation_state_history_is_root_relative(self) -> None:
        # The nine tricks that reach the four-card ending this project's own
        # examples already use (examples/guess_6nt_belief_space.py's
        # guess_6nt()) -- duplicated here, not imported: python/ may not
        # depend on examples/. A small hand-built deck doesn't work for this
        # test the way it did for the earlier ones in this class:
        # ExhaustiveLayoutSource requires history and root together to
        # account for all 52 cards, which only a real deal satisfies.
        sequence = PlaySequence(parse_deal(DEAL), declarer=SOUTH, trump=NOTRUMP, level=6)
        sequence.play_trick("C6 C4 C9 CQ")
        sequence.play_trick("CK C8 C2 CT")
        sequence.play_trick("C7 C5 CA H8")
        sequence.play_trick("CJ S3 C3 S8")
        sequence.play_trick("D6 D2 DK D3")
        sequence.play_trick("DA D4 D5 DJ")
        sequence.play_trick("D9 D7 DQ H4")
        sequence.play_trick("DT H3 D8 H5")
        sequence.play_trick("HJ H6 HK H7")
        # South is on lead for trick 10. Lead a spade and let West follow,
        # landing on dummy's turn mid-trick.
        sequence.play("S9")
        sequence.play("S4")

        view = sequence.declarer_view()
        self.assertEqual(view.seat_on_play, NORTH)
        self.assertEqual(view.position_in_trick, 2)
        self.assertEqual(len(view.play_history), 38)

        source = bsle.ExhaustiveLayoutSource(
            sequence.current_deal, sequence.declarer, 1,
            history=sequence.history, opening_leader=sequence.opening_leader)

        captured = []

        def pi(state, belief_view):
            del belief_view
            captured.append(len(state.history))
            deal = state.known_holdings
            return min(legal_cards(deal, seat_on_play(deal)),
                       key=lambda c: (c.rank, c.suit))

        def delta(layout, seat, state):
            del state
            card = min(legal_cards(layout, seat), key=lambda c: (c.rank, c.suit))
            return [(card, 1.0)]

        result = bsle.evaluate(
            sequence.current_deal, sequence.declarer, sequence.tricks_needed,
            source, pi, delta)

        self.assertNotIn("error", result)
        self.assertTrue(captured)
        self.assertEqual(captured[0], 2)  # the root's own trick-in-progress, not 0
        self.assertLess(captured[0], len(view.play_history))


if __name__ == "__main__":
    unittest.main()
