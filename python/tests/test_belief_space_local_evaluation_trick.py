"""The trick primitives bound from src/belief_evaluation/trick.hpp.

Every expectation here is hand-derived from the position written above it, not
recorded from a run: this file is what stops a Python caller's mental model of
the follow-suit and trick-winner rules drifting from the evaluator's own, which
is the divergence that produces a wrong *root* and no error anywhere.

The differential check against examples/play_sequence.py lives in
examples/test_play_sequence.py, not here. examples/ is package-private and
python/ must not depend on it -- the dependency runs the other way -- so the
comparison belongs on the side that already has both.
"""

import unittest

import belief_space_local_evaluation as bsle

NORTH, EAST, SOUTH, WEST = 0, 1, 2, 3
SPADES, HEARTS, DIAMONDS, CLUBS = 0, 1, 2, 3
NOTRUMP = 4

TWO, THREE, FOUR, NINE, TEN, JACK, QUEEN, KING, ACE = 2, 3, 4, 9, 10, 11, 12, 13, 14


def deal(first, holdings, trump=NOTRUMP, trick=()):
    """A Deal dict: holdings is {seat: {suit: [ranks]}}, trick a list of (suit, rank)."""
    remain = [[0, 0, 0, 0] for _ in range(4)]
    for seat, per_suit in holdings.items():
        for suit, ranks in per_suit.items():
            for rank in ranks:
                remain[seat][suit] |= 1 << rank
    suits = [0, 0, 0]
    ranks = [0, 0, 0]
    for index, (suit, rank) in enumerate(trick):
        suits[index], ranks[index] = suit, rank
    return {"trump": trump, "first": first, "remain_cards": remain,
            "current_trick_suit": tuple(suits), "current_trick_rank": tuple(ranks)}


def as_pairs(cards):
    return [(card.suit, card.rank) for card in cards]


class TestSeatOnPlay(unittest.TestCase):
    def test_is_the_leader_when_nothing_has_been_played(self) -> None:
        self.assertEqual(bsle.seat_on_play(deal(EAST, {EAST: {SPADES: [ACE]}})), EAST)

    def test_advances_from_the_leader_by_cards_already_played(self) -> None:
        # East led, South and West have followed: North is on play.
        position = deal(EAST, {NORTH: {SPADES: [ACE]}},
                        trick=[(SPADES, TWO), (SPADES, THREE), (SPADES, FOUR)])
        self.assertEqual(bsle.seat_on_play(position), NORTH)

    def test_wraps_round_the_table(self) -> None:
        # West led and North has followed: East is on play, not seat 4.
        position = deal(WEST, {EAST: {SPADES: [ACE]}}, trick=[(SPADES, TWO), (SPADES, THREE)])
        self.assertEqual(bsle.seat_on_play(position), EAST)


class TestLegalCards(unittest.TestCase):
    def test_when_leading_is_every_held_card_suit_then_rank(self) -> None:
        # Order is part of the contract: suit ascending, rank ascending within
        # a suit. root_children's own order is this order.
        position = deal(NORTH, {NORTH: {SPADES: [QUEEN, THREE], DIAMONDS: [KING],
                                        CLUBS: [TWO]}})
        self.assertEqual(
            as_pairs(bsle.legal_cards(position, NORTH)),
            [(SPADES, THREE), (SPADES, QUEEN), (DIAMONDS, KING), (CLUBS, TWO)])

    def test_must_follow_the_led_suit_when_holding_it(self) -> None:
        # North led the king of spades. East holds the ace of hearts and must
        # not be offered it.
        position = deal(NORTH, {EAST: {SPADES: [QUEEN, THREE], HEARTS: [ACE]}},
                        trick=[(SPADES, KING)])
        self.assertEqual(as_pairs(bsle.legal_cards(position, EAST)),
                         [(SPADES, THREE), (SPADES, QUEEN)])

    def test_is_every_held_card_when_void_in_the_led_suit(self) -> None:
        position = deal(NORTH, {EAST: {HEARTS: [ACE], CLUBS: [TWO]}},
                        trick=[(SPADES, KING)])
        self.assertEqual(as_pairs(bsle.legal_cards(position, EAST)),
                         [(HEARTS, ACE), (CLUBS, TWO)])

    def test_is_empty_for_a_seat_with_no_cards(self) -> None:
        self.assertEqual(bsle.legal_cards(deal(NORTH, {}), NORTH), [])

    def test_returns_cards_not_bitmasks(self) -> None:
        # The whole reason this is bound rather than legal_cards' four masks.
        cards = bsle.legal_cards(deal(NORTH, {NORTH: {SPADES: [ACE]}}), NORTH)
        self.assertIsInstance(cards[0], bsle.Card)
        self.assertEqual((cards[0].suit, cards[0].rank), (SPADES, ACE))


class TestTrickCompleteWinner(unittest.TestCase):
    def test_highest_card_of_the_led_suit_wins_in_notrump(self) -> None:
        # North led the queen, East the three, South the two; West plays the
        # king and wins with it.
        position = deal(NORTH, {WEST: {SPADES: [KING]}},
                        trick=[(SPADES, QUEEN), (SPADES, THREE), (SPADES, TWO)])
        self.assertEqual(bsle.trick_complete_winner(position, bsle.Card(SPADES, KING)), WEST)

    def test_a_discard_never_wins_even_when_it_outranks_the_led_suit(self) -> None:
        # The ace of hearts is the highest card played and loses: it is not of
        # the led suit and hearts are not trumps.
        position = deal(NORTH, {WEST: {HEARTS: [ACE]}},
                        trick=[(SPADES, QUEEN), (SPADES, THREE), (SPADES, TWO)])
        self.assertEqual(bsle.trick_complete_winner(position, bsle.Card(HEARTS, ACE)), NORTH)

    def test_a_ruff_wins_over_a_higher_card_of_the_led_suit(self) -> None:
        # Hearts are trumps. North's ace of spades is the highest spade; West
        # ruffs with the two of hearts and wins. 6NT never reaches this branch,
        # which is why it is asserted here rather than left to the example.
        position = deal(NORTH, {WEST: {HEARTS: [TWO]}}, trump=HEARTS,
                       trick=[(SPADES, ACE), (SPADES, THREE), (SPADES, FOUR)])
        self.assertEqual(bsle.trick_complete_winner(position, bsle.Card(HEARTS, TWO)), WEST)


class TestPlay(unittest.TestCase):
    def test_appends_to_an_in_progress_trick_without_resolving_it(self) -> None:
        position = deal(NORTH, {NORTH: {SPADES: [ACE, TWO]}})
        after = bsle.play(position, bsle.Card(SPADES, ACE))

        self.assertEqual(after["current_trick_suit"][0], SPADES)
        self.assertEqual(after["current_trick_rank"][0], ACE)
        self.assertEqual(after["first"], NORTH)  # the leader does not move yet
        self.assertEqual(after["remain_cards"][NORTH][SPADES], 1 << TWO)

    def test_resolves_the_trick_on_the_fourth_card_and_first_becomes_the_winner(self) -> None:
        # North led the two; East, South play low; West wins with the ace, so
        # first becomes West and the trick is cleared.
        position = deal(NORTH, {WEST: {SPADES: [ACE]}},
                        trick=[(SPADES, TWO), (SPADES, THREE), (SPADES, FOUR)])
        after = bsle.play(position, bsle.Card(SPADES, ACE))

        self.assertEqual(after["first"], WEST)
        self.assertEqual(after["current_trick_rank"], (0, 0, 0))
        self.assertEqual(after["remain_cards"][WEST][SPADES], 0)

    def test_is_pure(self) -> None:
        # Pure per trick.hpp's own contract: the input Deal is unchanged. A
        # caller replaying a history depends on this to keep earlier roots.
        position = deal(NORTH, {NORTH: {SPADES: [ACE, TWO]}})
        before = (position["first"], position["current_trick_rank"],
                  [row[:] for row in position["remain_cards"]])

        bsle.play(position, bsle.Card(SPADES, ACE))

        self.assertEqual(position["first"], before[0])
        self.assertEqual(position["current_trick_rank"], before[1])
        self.assertEqual(position["remain_cards"], before[2])


if __name__ == "__main__":
    unittest.main()
