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


def spades_each(north, east, south, west):
    """One suit, so following is always forced and every trick is decided by
    rank alone -- the counting is what is under test, not the trick rule."""
    return {NORTH: {SPADES: north}, EAST: {SPADES: east},
            SOUTH: {SPADES: south}, WEST: {SPADES: west}}


class TestPlayOut(unittest.TestCase):
    """Replaying a history to a root, and counting declarer's tricks with it.

    `evaluate()` takes `tricks_needed` and takes it on trust: the evaluator
    carries no trick counter. So the caller writes the arithmetic that decides
    what "make" means, and an off-by-one there evaluates a different contract
    and reports a confident number for it, with no diagnostic anywhere. That is
    the defect class this removes.

    The root and the count come back together for the same reason: the hazard is
    the two disagreeing, so there is no way to obtain one without the other.
    """

    def test_declarers_side_winning_every_trick_counts_every_trick(self) -> None:
        # North (declarer) holds A K, so both tricks are declarer's however
        # anyone else plays. East leads, being declarer's LHO.
        start = deal(EAST, spades_each([ACE, KING], [THREE, TWO], [4, 5], [6, 7]))
        history = [bsle.Card(SPADES, r) for r in
                   (THREE, 4, 6, ACE,   # East low, South low, West low, North's ace
                    KING, TWO, 5, 7)]   # North's king, and the rest follow

        root, won = bsle.play_out(start, history, EAST, NORTH)

        self.assertEqual(won, 2)
        self.assertEqual(root["current_trick_rank"], (0, 0, 0))
        # Every card played: the root is the end of the hand.
        self.assertEqual([row[SPADES] for row in root["remain_cards"]], [0, 0, 0, 0])

    def test_the_defence_winning_every_trick_counts_none(self) -> None:
        start = deal(EAST, spades_each([4, 5], [ACE, KING], [TWO, THREE], [6, 7]))
        history = [bsle.Card(SPADES, r) for r in
                   (ACE, TWO, 6, 4,
                    KING, THREE, 7, 5)]

        _root, won = bsle.play_out(start, history, EAST, NORTH)

        self.assertEqual(won, 0)

    def test_a_defence_won_trick_does_not_advance_the_count_and_moves_the_lead(self) -> None:
        # The branch that had no test anywhere until it was noticed missing: one
        # trick to each side. Asserted as two claims, because a counter that
        # simply counted tricks would pass the first.
        start = deal(EAST, spades_each([ACE, 4], [KING, TWO], [THREE, 5], [6, 7]))
        first_trick = [bsle.Card(SPADES, r) for r in (KING, THREE, 6, 4)]

        root, won = bsle.play_out(start, first_trick, EAST, NORTH)

        self.assertEqual(won, 0, "East's king won it, so declarer's count must not move")
        self.assertEqual(root["first"], EAST, "the winner leads the next trick")

        # And the second trick, which declarer's ace takes.
        both = first_trick + [bsle.Card(SPADES, r) for r in (TWO, 5, 7, ACE)]
        root, won = bsle.play_out(start, both, EAST, NORTH)

        self.assertEqual(won, 1)
        self.assertEqual(root["first"], NORTH)

    def test_dummys_trick_counts_for_declarer(self) -> None:
        # South is dummy to North's declarer. A trick won by dummy is won by
        # declarer's side -- the same declarer-or-dummy reading that is wrong in
        # the obvious way elsewhere.
        start = deal(EAST, spades_each([4, 5], [THREE, TWO], [ACE, KING], [6, 7]))
        history = [bsle.Card(SPADES, r) for r in (THREE, ACE, 6, 4)]

        root, won = bsle.play_out(start, history, EAST, NORTH)

        self.assertEqual(won, 1)
        self.assertEqual(root["first"], SOUTH)

    def test_a_trailing_incomplete_trick_is_not_counted(self) -> None:
        # The history includes the trick in progress, which has one to three
        # cards and no winner. A count that consumed the history four at a time
        # would either drop it or invent a winner for it.
        start = deal(EAST, spades_each([ACE, KING], [THREE, TWO], [4, 5], [6, 7]))
        history = [bsle.Card(SPADES, r) for r in (THREE, 4, 6, ACE,  # trick one
                                                 KING, TWO)]        # two cards only

        root, won = bsle.play_out(start, history, EAST, NORTH)

        self.assertEqual(won, 1, "only the completed trick counts")
        self.assertEqual(root["current_trick_rank"][:2], (KING, TWO))
        self.assertEqual(root["current_trick_rank"][2], 0)
        self.assertEqual(bsle.seat_on_play(root), SOUTH)

    def test_an_empty_history_is_the_deal_itself_with_nothing_won(self) -> None:
        start = deal(EAST, spades_each([ACE, KING], [THREE, TWO], [4, 5], [6, 7]))

        root, won = bsle.play_out(start, [], EAST, NORTH)

        self.assertEqual(won, 0)
        self.assertEqual(root["remain_cards"], start["remain_cards"])
        self.assertEqual(root["first"], EAST)

    def test_a_ruff_is_counted_for_the_side_that_ruffed(self) -> None:
        # Hearts are trumps and East is void in spades. North leads a spade,
        # East ruffs and takes the trick -- so the trick goes to the defence
        # even though declarer played the highest spade.
        start = deal(NORTH,
                     {NORTH: {SPADES: [ACE, KING]}, EAST: {HEARTS: [TWO, THREE]},
                      SOUTH: {SPADES: [4, 5]}, WEST: {SPADES: [6, 7]}},
                     trump=HEARTS)
        history = [bsle.Card(SPADES, ACE), bsle.Card(HEARTS, TWO),
                   bsle.Card(SPADES, 4), bsle.Card(SPADES, 6)]

        root, won = bsle.play_out(start, history, NORTH, NORTH)

        self.assertEqual(won, 0, "the ruff won it for the defence")
        self.assertEqual(root["first"], EAST)

    def test_the_opening_leader_decides_who_plays_the_first_card(self) -> None:
        # Same cards, different leader: the same history is a different hand.
        # This is the argument for the root and the count arriving together --
        # a caller passing a leader inconsistent with the deal gets a different
        # answer to the one they meant, and nothing about the count alone shows
        # it.
        holdings = spades_each([ACE, KING], [THREE, TWO], [4, 5], [6, 7])
        history = [bsle.Card(SPADES, r) for r in (THREE, 4, 6, ACE)]

        _root, won_from_east = bsle.play_out(deal(EAST, holdings), history, EAST, NORTH)

        with self.assertRaises(ValueError):
            # From North, the first card of that history is not North's to play.
            bsle.play_out(deal(NORTH, holdings), history, NORTH, NORTH)

        self.assertEqual(won_from_east, 1)

    def test_it_agrees_with_the_example_that_used_to_do_this_by_hand(self) -> None:
        # Nine tricks of a real hand, all won by declarer's side: the sequence
        # the example's own numbers are computed from. A hand-written counter
        # got this right; the point is that it no longer has to.
        start = deal(WEST, {
            NORTH: {SPADES: [KING, JACK, 7], HEARTS: [QUEEN, JACK],
                    DIAMONDS: [QUEEN, TEN, 6, 5], CLUBS: [ACE, JACK, 4, 2]},
            EAST: {SPADES: [ACE, 6, 5, THREE], HEARTS: [8, 6, 4, THREE, TWO],
                   DIAMONDS: [JACK, TWO], CLUBS: [TEN, 9]},
            SOUTH: {SPADES: [TEN, 9], HEARTS: [ACE, KING, TEN],
                    DIAMONDS: [ACE, KING, 9, 8], CLUBS: [KING, QUEEN, 7, THREE]},
            WEST: {SPADES: [QUEEN, 8, 4, TWO], HEARTS: [9, 7, 5],
                   DIAMONDS: [7, 4, THREE], CLUBS: [8, 6, 5]},
        })
        tricks = ["C6 C4 C9 CQ", "CK C8 C2 CT", "C7 C5 CA H8", "CJ S3 C3 S8",
                  "D6 D2 DK D3", "DA D4 D5 DJ", "D9 D7 DQ H4", "DT H3 D8 H5",
                  "HJ H6 HK H7"]
        suits = {"S": SPADES, "H": HEARTS, "D": DIAMONDS, "C": CLUBS}
        ranks = {"A": ACE, "K": KING, "Q": QUEEN, "J": JACK, "T": TEN}
        history = [bsle.Card(suits[c[0]], ranks.get(c[1], 0) or int(c[1]))
                   for trick in tricks for c in trick.split()]

        root, won = bsle.play_out(start, history, WEST, SOUTH)

        self.assertEqual(won, 9)
        self.assertEqual(root["first"], SOUTH)  # South is on play at the ending
        self.assertEqual(sum(bin(row[s]).count("1") for row in root["remain_cards"]
                             for s in range(4)), 16)  # a four-card ending


if __name__ == "__main__":
    unittest.main()
